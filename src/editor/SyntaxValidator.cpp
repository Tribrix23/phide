/**
 * @file SyntaxValidator.cpp
 * @brief High-performance multi-language syntax validator implementation.
 */

#include "phide/editor/SyntaxValidator.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <stack>

namespace phide::editor {

std::vector<lsp::Diagnostic> SyntaxValidator::Validate(const std::wstring& filePath, const Buffer& buffer) {
    if (buffer.GetLineCount() == 0) return {};

    std::wstring ext;
    try {
        std::filesystem::path p(filePath);
        ext = p.extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    } catch (...) {}

    if (ext == L".json" || ext == L".jsonc" || ext == L".eslintrc" || ext == L".prettierrc") {
        return ValidateJson(buffer);
    }
    if (ext == L".html" || ext == L".htm" || ext == L".xml" || ext == L".svg") {
        return ValidateHtml(buffer);
    }
    if (ext == L".yml" || ext == L".yaml") {
        return ValidateYaml(buffer);
    }
    if (ext == L".php") {
        return ValidateBracketsAndQuotes(buffer, true);
    }
    // Only perform bracket & quote validation on recognized code file extensions.
    // Plain text, markdown, METADATA, documentation, licenses, and config files must never report false syntax errors!
    if (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" ||
        ext == L".cpp" || ext == L".c" || ext == L".cc" || ext == L".cxx" ||
        ext == L".h" || ext == L".hpp" || ext == L".hxx" || ext == L".cs" ||
        ext == L".java" || ext == L".rs" || ext == L".go") {
        return ValidateBracketsAndQuotes(buffer, false);
    }
    return {};
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateJson(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct BracketInfo {
        wchar_t ch;
        size_t line;
        size_t col;
        bool isObject;
    };
    std::vector<BracketInfo> bracketStack;

    bool inString = false;
    size_t strStartLine = 0;
    size_t strStartCol = 0;
    bool hasLastComma = false;
    size_t commaLine = 0;
    size_t commaCol = 0;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        for (size_t c = 0; c < line.length(); ++c) {
            wchar_t ch = line[c];

            if (inString) {
                if (ch == L'\\') {
                    c++; // Skip escaped char
                    continue;
                }
                if (ch == L'"') {
                    inString = false;
                }
                continue;
            }

            // Single line comment in JSONC
            if (ch == L'/' && c + 1 < line.length() && line[c + 1] == L'/') {
                break;
            }

            if (ch == L'"') {
                inString = true;
                strStartLine = l;
                strStartCol = c;
                hasLastComma = false;
                continue;
            }

            if (ch == L',') {
                hasLastComma = true;
                commaLine = l;
                commaCol = c;
                continue;
            }

            if (iswspace(ch)) {
                continue;
            }

            if (ch == L'{') {
                bracketStack.push_back({ ch, l, c, true });
                hasLastComma = false;
            } else if (ch == L'[') {
                bracketStack.push_back({ ch, l, c, false });
                hasLastComma = false;
            } else if (ch == L'}') {
                if (hasLastComma) {
                    diagnostics.push_back({ commaLine, commaCol, 1, L"Trailing comma is not allowed in JSON", 1 });
                    hasLastComma = false;
                }
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, L"Unexpected closing brace '}'", 1 });
                } else if (!bracketStack.back().isObject) {
                    diagnostics.push_back({ l, c, 1, L"Mismatched closing brace: expected ']' but found '}'", 1 });
                    bracketStack.pop_back();
                } else {
                    bracketStack.pop_back();
                }
            } else if (ch == L']') {
                if (hasLastComma) {
                    diagnostics.push_back({ commaLine, commaCol, 1, L"Trailing comma is not allowed in JSON array", 1 });
                    hasLastComma = false;
                }
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, L"Unexpected closing bracket ']'", 1 });
                } else if (bracketStack.back().isObject) {
                    diagnostics.push_back({ l, c, 1, L"Mismatched closing bracket: expected '}' but found ']'", 1 });
                    bracketStack.pop_back();
                } else {
                    bracketStack.pop_back();
                }
            } else {
                hasLastComma = false;
            }
        }

        if (inString) {
            // String didn't close before end of line in JSON
            diagnostics.push_back({ strStartLine, strStartCol, (std::max)(1ULL, line.length() - strStartCol), L"Unterminated string literal in JSON", 1 });
            inString = false;
        }
    }

    while (!bracketStack.empty()) {
        const auto& b = bracketStack.back();
        std::wstring msg = b.isObject ? L"Unclosed brace '{' (missing matching '}')" : L"Unclosed bracket '[' (missing matching ']')";
        diagnostics.push_back({ b.line, b.col, 1, msg, 1 });
        bracketStack.pop_back();
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateBracketsAndQuotes(const Buffer& buffer, bool isPhp) {
    (void)isPhp;
    std::vector<lsp::Diagnostic> diagnostics;

    struct OpenBracket {
        wchar_t ch;
        size_t line;
        size_t col;
    };
    std::vector<OpenBracket> bracketStack;

    bool inBlockComment = false;
    size_t blockCommentLine = 0;
    size_t blockCommentCol = 0;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        bool inDoubleQuote = false;
        bool inSingleQuote = false;
        size_t quoteStartCol = 0;

        for (size_t c = 0; c < line.length(); ++c) {
            wchar_t ch = line[c];

            // Inside multi-line block comment /* ... */
            if (inBlockComment) {
                if (ch == L'*' && c + 1 < line.length() && line[c + 1] == L'/') {
                    inBlockComment = false;
                    c++;
                }
                continue;
            }

            // Inside double quote string
            if (inDoubleQuote) {
                if (ch == L'\\') {
                    c++; // Skip escaped char
                    continue;
                }
                if (ch == L'"') {
                    inDoubleQuote = false;
                }
                continue;
            }

            // Inside single quote string
            if (inSingleQuote) {
                if (ch == L'\\') {
                    c++; // Skip escaped char
                    continue;
                }
                if (ch == L'\'') {
                    inSingleQuote = false;
                }
                continue;
            }

            // Check for line comment //
            if (ch == L'/' && c + 1 < line.length() && line[c + 1] == L'/') {
                break; // Ignore rest of line
            }

            // Check for block comment /*
            if (ch == L'/' && c + 1 < line.length() && line[c + 1] == L'*') {
                inBlockComment = true;
                blockCommentLine = l;
                blockCommentCol = c;
                c++;
                continue;
            }

            // String start
            if (ch == L'"') {
                inDoubleQuote = true;
                quoteStartCol = c;
                continue;
            }
            if (ch == L'\'') {
                inSingleQuote = true;
                quoteStartCol = c;
                continue;
            }

            // Brackets
            if (ch == L'{' || ch == L'(' || ch == L'[') {
                bracketStack.push_back({ ch, l, c });
            } else if (ch == L'}' || ch == L')' || ch == L']') {
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, L"Syntax Error: Unmatched closing bracket '" + std::wstring(1, ch) + L"'", 1 });
                } else {
                    wchar_t openCh = bracketStack.back().ch;
                    bool matches = (openCh == L'{' && ch == L'}') ||
                                   (openCh == L'(' && ch == L')') ||
                                   (openCh == L'[' && ch == L']');
                    if (!matches) {
                        wchar_t expected = (openCh == L'{') ? L'}' : (openCh == L'(' ? L')' : L']');
                        diagnostics.push_back({ l, c, 1,
                            L"Syntax Error: Mismatched bracket: expected '" + std::wstring(1, expected) + L"' but found '" + std::wstring(1, ch) + L"'", 1 });
                        bracketStack.pop_back();
                    } else {
                        bracketStack.pop_back();
                    }
                }
            }
        }

        // Unterminated single line string literal
        if (inDoubleQuote) {
            diagnostics.push_back({ l, quoteStartCol, (std::max)(1ULL, line.length() - quoteStartCol), L"Syntax Error: Unterminated string literal", 1 });
        } else if (inSingleQuote) {
            diagnostics.push_back({ l, quoteStartCol, (std::max)(1ULL, line.length() - quoteStartCol), L"Syntax Error: Unterminated character/string literal", 1 });
        }
    }

    if (inBlockComment) {
        diagnostics.push_back({ blockCommentLine, blockCommentCol, 2, L"Syntax Error: Unterminated block comment '/*'", 1 });
    }

    // Unclosed brackets at EOF
    while (!bracketStack.empty()) {
        const auto& b = bracketStack.back();
        wchar_t expected = (b.ch == L'{') ? L'}' : (b.ch == L'(' ? L')' : L']');
        diagnostics.push_back({ b.line, b.col, 1,
            L"Syntax Error: Unclosed bracket '" + std::wstring(1, b.ch) + L"' (missing matching '" + std::wstring(1, expected) + L"')", 1 });
        bracketStack.pop_back();
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateHtml(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct TagInfo {
        std::wstring name;
        size_t line;
        size_t col;
    };
    std::vector<TagInfo> tagStack;

    auto isVoidTag = [](const std::wstring& name) {
        std::wstring lower = name;
        for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
        if (lower.rfind(L"!", 0) == 0 || lower.rfind(L"?", 0) == 0) return true;
        return lower == L"area" || lower == L"base" || lower == L"br" || lower == L"col" ||
               lower == L"embed" || lower == L"hr" || lower == L"img" || lower == L"input" ||
               lower == L"link" || lower == L"meta" || lower == L"param" || lower == L"source" ||
               lower == L"track" || lower == L"wbr";
    };

    bool inComment = false;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        for (size_t c = 0; c < line.length(); ++c) {
            if (inComment) {
                if (c + 2 < line.length() && line[c] == L'-' && line[c + 1] == L'-' && line[c + 2] == L'>') {
                    inComment = false;
                    c += 2;
                }
                continue;
            }

            if (line[c] == L'<' && c + 3 < line.length() && line[c + 1] == L'!' && line[c + 2] == L'-' && line[c + 3] == L'-') {
                inComment = true;
                c += 3;
                continue;
            }

            if (line[c] == L'<') {
                size_t tagStartLine = l;
                size_t tagStartCol = c;

                // Find closing '>' possibly across multiple lines (e.g. multi-line <meta ... /> in Image 2)
                bool foundClose = false;
                size_t endLine = l;
                size_t endCol = 0;
                bool inQuotes = false;
                wchar_t quoteChar = L'\0';

                size_t curScanL = l;
                size_t curScanC = c + 1;

                while (curScanL < buffer.GetLineCount()) {
                    const auto& scanText = buffer.GetLine(curScanL);
                    for (size_t k = curScanC; k < scanText.length(); ++k) {
                        wchar_t ch = scanText[k];
                        if (inQuotes) {
                            if (ch == quoteChar) inQuotes = false;
                        } else {
                            if (ch == L'"' || ch == L'\'') {
                                inQuotes = true;
                                quoteChar = ch;
                            } else if (ch == L'>') {
                                endLine = curScanL;
                                endCol = k;
                                foundClose = true;
                                break;
                            }
                        }
                    }
                    if (foundClose) break;
                    curScanL++;
                    curScanC = 0;
                }

                if (!foundClose) {
                    diagnostics.push_back({ tagStartLine, tagStartCol, 1, L"Syntax Error: Unclosed tag bracket '<'", 1 });
                    break;
                }

                std::wstring tagContent;
                if (endLine == l) {
                    tagContent = line.substr(c + 1, endCol - (c + 1));
                    c = endCol;
                } else {
                    tagContent = line.substr(c + 1);
                    for (size_t nl = l + 1; nl < endLine; ++nl) {
                        tagContent += L" " + buffer.GetLine(nl);
                    }
                    tagContent += L" " + buffer.GetLine(endLine).substr(0, endCol);
                    l = endLine;
                    c = endCol;
                }

                size_t lastNonWs = tagContent.find_last_not_of(L" \t\r\n");
                if (lastNonWs != std::wstring::npos) {
                    tagContent = tagContent.substr(0, lastNonWs + 1);
                }

                if (tagContent.empty()) continue;

                // Self-closing: e.g. <div /> or multiline <meta ... />
                if (!tagContent.empty() && tagContent.back() == L'/') {
                    continue;
                }

                // Closing tag: </tag>
                if (tagContent.front() == L'/') {
                    std::wstring closeName = tagContent.substr(1);
                    size_t sp = closeName.find_first_of(L" \t\r\n");
                    if (sp != std::wstring::npos) closeName = closeName.substr(0, sp);
                    for (auto& ch : closeName) ch = static_cast<wchar_t>(towlower(ch));

                    if (tagStack.empty()) {
                        diagnostics.push_back({ tagStartLine, tagStartCol, closeName.length() + 3,
                            L"Syntax Error: Unexpected closing tag </" + closeName + L">", 1 });
                    } else {
                        std::wstring expected = tagStack.back().name;
                        for (auto& ch : expected) ch = static_cast<wchar_t>(towlower(ch));
                        if (expected == closeName) {
                            tagStack.pop_back();
                        } else {
                            // Mismatched tag: pinpoint expected vs actual
                            diagnostics.push_back({ tagStartLine, tagStartCol, closeName.length() + 3,
                                L"Syntax Error: Mismatched tag: expected </" + tagStack.back().name + L"> but found </" + closeName + L">", 1 });

                            // Recover by finding ancestor match if present
                            int matchIdx = -1;
                            for (int i = static_cast<int>(tagStack.size()) - 1; i >= 0; --i) {
                                std::wstring curName = tagStack[i].name;
                                for (auto& ch : curName) ch = static_cast<wchar_t>(towlower(ch));
                                if (curName == closeName) {
                                    matchIdx = i;
                                    break;
                                }
                            }
                            if (matchIdx >= 0) {
                                tagStack.erase(tagStack.begin() + matchIdx, tagStack.end());
                            } else {
                                tagStack.pop_back();
                            }
                        }
                    }
                } else {
                    // Opening tag
                    size_t sp = tagContent.find_first_of(L" \t\r\n/");
                    std::wstring tagName = (sp == std::wstring::npos) ? tagContent : tagContent.substr(0, sp);
                    if (!tagName.empty() && !isVoidTag(tagName)) {
                        std::wstring lowerTag = tagName;
                        for (auto& ch : lowerTag) ch = static_cast<wchar_t>(towlower(ch));
                        if (lowerTag == L"script" || lowerTag == L"style") {
                            // Raw text element: scan until </script> or </style>
                            std::wstring endPattern = L"</" + lowerTag;
                            bool foundRawEnd = false;
                            while (l < buffer.GetLineCount()) {
                                const auto& rawLine = buffer.GetLine(l);
                                std::wstring rawLower = rawLine;
                                for (auto& ch : rawLower) ch = static_cast<wchar_t>(towlower(ch));
                                size_t foundPos = rawLower.find(endPattern, (l == tagStartLine) ? c + 1 : 0);
                                if (foundPos != std::wstring::npos) {
                                    size_t closeGt = rawLine.find(L'>', foundPos);
                                    if (closeGt != std::wstring::npos) {
                                        c = closeGt;
                                        foundRawEnd = true;
                                        break;
                                    }
                                }
                                l++;
                                c = 0;
                            }
                            if (!foundRawEnd) {
                                diagnostics.push_back({ tagStartLine, tagStartCol, tagName.length() + 2,
                                    L"Syntax Error: Unclosed <" + tagName + L"> block (missing </" + tagName + L">)", 1 });
                            }
                        } else {
                            tagStack.push_back({ tagName, tagStartLine, tagStartCol });
                        }
                    }
                }
            }
        }
    }

    while (!tagStack.empty()) {
        const auto& t = tagStack.back();
        diagnostics.push_back({ t.line, t.col, t.name.length() + 2,
            L"Syntax Error: Unclosed HTML tag <" + t.name + L"> (missing </" + t.name + L">)", 1 });
        tagStack.pop_back();
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateYaml(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& fullLine = buffer.GetLine(l);
        if (fullLine.empty()) continue;

        // In YAML, tab indentation is strictly prohibited!
        size_t firstNonSpace = fullLine.find_first_not_of(L" ");
        if (firstNonSpace != std::wstring::npos && fullLine[firstNonSpace] == L'\t') {
            diagnostics.push_back({ l, firstNonSpace, 1, L"Syntax Error: Tab characters are not allowed for indentation in YAML", 1 });
            continue;
        }

        // Strip comments that begin with '#' (preceded by whitespace or at start of line)
        // unless '#' is inside quotes
        std::wstring line = fullLine;
        bool inSq = false, inDq = false;
        size_t commentPos = std::wstring::npos;
        for (size_t i = 0; i < line.length(); ++i) {
            wchar_t c = line[i];
            if (c == L'\'' && !inDq) inSq = !inSq;
            else if (c == L'"' && !inSq && (i == 0 || line[i - 1] != L'\\')) inDq = !inDq;
            else if (c == L'#' && !inSq && !inDq) {
                if (i == 0 || iswspace(line[i - 1])) {
                    commentPos = i;
                    break;
                }
            }
        }
        if (commentPos != std::wstring::npos) {
            line = line.substr(0, commentPos);
        }

        size_t start = line.find_first_not_of(L" \t");
        if (start == std::wstring::npos) continue;

        // Skip list marker '- '
        if (line[start] == L'-' && start + 1 < line.length() && iswspace(line[start + 1])) {
            start = line.find_first_not_of(L" \t", start + 2);
            if (start == std::wstring::npos) continue;
        }

        // Helper to check for unclosed quotes ONLY when a scalar actually begins with a quote delimiter
        auto checkQuotedScalar = [&](size_t pos, const std::wstring& context) {
            if (pos >= line.length()) return;
            wchar_t q = line[pos];
            if (q == L'\'') {
                bool closed = false;
                for (size_t i = pos + 1; i < line.length(); ++i) {
                    if (line[i] == L'\'') {
                        if (i + 1 < line.length() && line[i + 1] == L'\'') {
                            i++; // Skip escaped quote ''
                        } else {
                            closed = true;
                            break;
                        }
                    }
                }
                if (!closed) {
                    diagnostics.push_back({ l, pos, line.length() - pos, L"Syntax Error: Unclosed single quote in YAML " + context, 1 });
                }
            } else if (q == L'"') {
                bool closed = false;
                for (size_t i = pos + 1; i < line.length(); ++i) {
                    if (line[i] == L'\\') {
                        i++; // Skip escaped char
                        continue;
                    }
                    if (line[i] == L'"') {
                        closed = true;
                        break;
                    }
                }
                if (!closed) {
                    diagnostics.push_back({ l, pos, line.length() - pos, L"Syntax Error: Unclosed double quote in YAML " + context, 1 });
                }
            }
        };

        // Check if key starts with quote
        if (line[start] == L'\'' || line[start] == L'"') {
            checkQuotedScalar(start, L"key");
        }

        // Find ':' delimiter for key-value pair
        size_t colon = line.find(L':');
        if (colon != std::wstring::npos) {
            size_t valStart = line.find_first_not_of(L" \t", colon + 1);
            if (valStart != std::wstring::npos) {
                // In YAML, apostrophes inside unquoted plain strings (e.g. "device's camera") are plain text!
                // Only inspect quotes if the value explicitly begins with a quote delimiter.
                if (line[valStart] == L'\'' || line[valStart] == L'"') {
                    checkQuotedScalar(valStart, L"value");
                }
            }
        }
    }

    return diagnostics;
}

} // namespace phide::editor
