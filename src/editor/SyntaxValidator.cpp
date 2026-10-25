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

std::vector<lsp::Diagnostic> SyntaxValidator::Validate(
    const std::wstring& filePath,
    const Buffer& buffer,
    bool validateScripts,
    bool validateStyles
) {
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
    if (ext == L".css" || ext == L".scss" || ext == L".less") {
        return ValidateCss(buffer);
    }
    if (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" || ext == L".mjs" || ext == L".cjs") {
        return ValidateJavaScript(buffer);
    }
    if (ext == L".html" || ext == L".htm" || ext == L".xml" || ext == L".svg") {
        return ValidateHtml(buffer, validateScripts, validateStyles);
    }
    if (ext == L".php") {
        auto htmlDiags = ValidateHtml(buffer, validateScripts, validateStyles);
        auto phpDiags = ValidateBracketsAndQuotes(buffer, true);
        htmlDiags.insert(htmlDiags.end(), phpDiags.begin(), phpDiags.end());
        return htmlDiags;
    }
    if (ext == L".yml" || ext == L".yaml") {
        return ValidateYaml(buffer);
    }
    // Only perform bracket & quote validation on recognized code file extensions.
    // Plain text, markdown, METADATA, documentation, licenses, and config files must never report false syntax errors!
    if (ext == L".cpp" || ext == L".c" || ext == L".cc" || ext == L".cxx" ||
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

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateCss(
    const Buffer& buffer,
    size_t startLine,
    size_t endLine
) {
    std::vector<lsp::Diagnostic> diagnostics;
    size_t lineCount = buffer.GetLineCount();
    if (endLine > lineCount) endLine = lineCount;

    static const std::unordered_set<std::wstring> knownProps = {
        // Layout & Display
        L"display", L"position", L"top", L"right", L"bottom", L"left", L"z-index", L"float", L"clear",
        L"visibility", L"box-sizing", L"overflow", L"overflow-x", L"overflow-y", L"clip-path",
        L"isolation", L"contain", L"container", L"container-name", L"container-type",
        // Flexbox & Grid
        L"flex", L"flex-direction", L"flex-wrap", L"flex-flow", L"justify-content", L"align-items",
        L"align-content", L"align-self", L"justify-items", L"justify-self", L"place-items", L"place-content", L"place-self",
        L"flex-grow", L"flex-shrink", L"flex-basis", L"order",
        L"gap", L"row-gap", L"column-gap", L"grid", L"grid-template-columns", L"grid-template-rows",
        L"grid-template-areas", L"grid-column", L"grid-column-start", L"grid-column-end",
        L"grid-row", L"grid-row-start", L"grid-row-end", L"grid-area", L"grid-auto-flow",
        L"grid-auto-columns", L"grid-auto-rows",
        // Dimensions & Insets
        L"width", L"min-width", L"max-width", L"height", L"min-height", L"max-height",
        L"inline-size", L"min-inline-size", L"max-inline-size", L"block-size", L"min-block-size", L"max-block-size",
        L"aspect-ratio", L"inset", L"inset-block", L"inset-block-start", L"inset-block-end",
        L"inset-inline", L"inset-inline-start", L"inset-inline-end",
        // Margins & Paddings
        L"margin", L"margin-top", L"margin-right", L"margin-bottom", L"margin-left",
        L"margin-block", L"margin-block-start", L"margin-block-end",
        L"margin-inline", L"margin-inline-start", L"margin-inline-end",
        L"padding", L"padding-top", L"padding-right", L"padding-bottom", L"padding-left",
        L"padding-block", L"padding-block-start", L"padding-block-end",
        L"padding-inline", L"padding-inline-start", L"padding-inline-end",
        // Colors & Backgrounds
        L"color", L"color-scheme", L"accent-color", L"caret-color",
        L"background", L"background-color", L"background-image", L"background-position", L"background-position-x", L"background-position-y",
        L"background-size", L"background-repeat", L"background-attachment", L"background-clip", L"background-origin", L"background-blend-mode",
        L"opacity", L"filter", L"backdrop-filter", L"mix-blend-mode",
        // Borders & Outlines
        L"border", L"border-width", L"border-style", L"border-color", L"border-radius",
        L"border-top", L"border-top-width", L"border-top-style", L"border-top-color", L"border-top-left-radius", L"border-top-right-radius",
        L"border-right", L"border-right-width", L"border-right-style", L"border-right-color",
        L"border-bottom", L"border-bottom-width", L"border-bottom-style", L"border-bottom-color", L"border-bottom-left-radius", L"border-bottom-right-radius",
        L"border-left", L"border-left-width", L"border-left-style", L"border-left-color",
        L"border-block", L"border-block-width", L"border-block-style", L"border-block-color",
        L"border-block-start", L"border-block-end",
        L"border-inline", L"border-inline-width", L"border-inline-style", L"border-inline-color",
        L"border-inline-start", L"border-inline-end",
        L"border-collapse", L"border-spacing", L"box-shadow",
        L"outline", L"outline-width", L"outline-style", L"outline-color", L"outline-offset",
        // Typography & Text
        L"font", L"font-family", L"font-size", L"font-weight", L"font-style", L"font-variant", L"font-display",
        L"line-height", L"letter-spacing", L"word-spacing", L"text-align", L"text-align-last",
        L"text-decoration", L"text-decoration-line", L"text-decoration-style", L"text-decoration-color", L"text-decoration-thickness",
        L"text-underline-offset", L"text-transform", L"text-overflow", L"text-shadow", L"text-indent",
        L"white-space", L"word-break", L"overflow-wrap", L"word-wrap", L"text-wrap", L"hyphens",
        L"vertical-align", L"writing-mode", L"direction", L"unicode-bidi",
        // Transitions & Animations
        L"transition", L"transition-property", L"transition-duration", L"transition-timing-function", L"transition-delay",
        L"animation", L"animation-name", L"animation-duration", L"animation-timing-function", L"animation-delay",
        L"animation-iteration-count", L"animation-direction", L"animation-fill-mode", L"animation-play-state",
        // Transforms
        L"transform", L"transform-origin", L"transform-style", L"perspective", L"perspective-origin", L"backface-visibility",
        L"scale", L"rotate", L"translate",
        // User Interface & Behavior
        L"cursor", L"pointer-events", L"user-select", L"resize", L"appearance",
        L"touch-action", L"overscroll-behavior", L"overscroll-behavior-x", L"overscroll-behavior-y",
        L"scroll-behavior", L"scroll-margin", L"scroll-padding", L"scroll-snap-type", L"scroll-snap-align",
        L"scrollbar-width", L"scrollbar-color", L"scrollbar-gutter",
        L"content", L"will-change", L"all",
        // SVG & Masks
        L"fill", L"fill-opacity", L"fill-rule", L"stroke", L"stroke-width", L"stroke-linecap", L"stroke-linejoin", L"stroke-dasharray", L"stroke-dashoffset", L"stroke-opacity",
        L"mask", L"mask-image", L"mask-size", L"mask-repeat", L"mask-position", L"mask-clip",
        // Tables & Lists
        L"list-style", L"list-style-type", L"list-style-position", L"list-style-image",
        L"table-layout", L"caption-side", L"empty-cells"
    };

    struct BraceInfo { size_t line; size_t col; };
    std::vector<BraceInfo> braceStack;

    bool inBlock = false;
    bool inComment = false;

    for (size_t l = startLine; l < endLine; ++l) {
        const auto& line = buffer.GetLine(l);
        size_t c = 0;
        while (c < line.length()) {
            if (inComment) {
                if (c + 1 < line.length() && line[c] == L'*' && line[c + 1] == L'/') {
                    inComment = false;
                    c += 2;
                } else {
                    c++;
                }
                continue;
            }

            if (c + 1 < line.length() && line[c] == L'/' && line[c + 1] == L'*') {
                inComment = true;
                c += 2;
                continue;
            }

            wchar_t ch = line[c];
            if (ch == L'{') {
                braceStack.push_back({ l, c });
                inBlock = true;
                c++;
                continue;
            } else if (ch == L'}') {
                if (braceStack.empty()) {
                    diagnostics.push_back({ l, c, 1, L"CSS Syntax Error: Unexpected closing brace '}'", 1 });
                } else {
                    braceStack.pop_back();
                    inBlock = !braceStack.empty();
                }
                c++;
                continue;
            }

            // Inside CSS declaration block { ... }
            if (inBlock && iswalpha(ch)) {
                size_t propStart = c;
                while (c < line.length() && (iswalnum(line[c]) || line[c] == L'-' || line[c] == L'_')) {
                    c++;
                }
                std::wstring propName = line.substr(propStart, c - propStart);
                for (auto& k : propName) k = static_cast<wchar_t>(towlower(k));

                while (c < line.length() && iswspace(line[c])) c++;

                // Check if this is a selector or nested rule (e.g. '@media ... { .sidebar { ... } }')
                size_t nextOpenBrace = line.find(L'{', c);
                size_t nextSemi = line.find(L';', c);
                size_t nextCloseBrace = line.find(L'}', c);
                bool isNestedRule = (nextOpenBrace != std::wstring::npos &&
                                     (nextSemi == std::wstring::npos || nextOpenBrace < nextSemi) &&
                                     (nextCloseBrace == std::wstring::npos || nextOpenBrace < nextCloseBrace));

                if (isNestedRule) {
                    // Do not treat nested selectors as missing colon errors
                    continue;
                }

                if (c >= line.length() || line[c] != L':') {
                    if (c < line.length() && (line[c] == L'{' || line[c] == L',' || line[c] == L';' || line[c] == L'}')) {
                        // Ignore selector or end of block
                    } else if (c >= line.length() || (line[c] != L'(' && line[c] != L'.' && line[c] != L'#')) {
                        diagnostics.push_back({ l, propStart, propName.length(), L"CSS Syntax Error: Missing colon ':' after property '" + propName + L"'", 1 });
                    }
                } else {
                    // Colon found! Check if property is known
                    if (propName.rfind(L"--", 0) != 0 && propName.rfind(L"-webkit-", 0) != 0 &&
                        propName.rfind(L"-moz-", 0) != 0 && propName.rfind(L"-ms-", 0) != 0 && propName.rfind(L"-o-", 0) != 0) {
                        if (knownProps.find(propName) == knownProps.end()) {
                            diagnostics.push_back({ l, propStart, propName.length(), L"CSS Linter: Unknown CSS property '" + propName + L"'", 2 });
                        }
                    }
                    c++; // skip ':'

                    // Scan value until ';' or '}'
                    size_t valStart = c;
                    while (c < line.length() && line[c] != L';' && line[c] != L'}') {
                        c++;
                    }
                    std::wstring valStr = line.substr(valStart, c - valStart);

                    // Check for invalid hex colors in value
                    size_t hashPos = 0;
                    while ((hashPos = valStr.find(L'#', hashPos)) != std::wstring::npos) {
                        size_t hEnd = hashPos + 1;
                        while (hEnd < valStr.length() && iswxdigit(valStr[hEnd])) hEnd++;
                        size_t hLen = hEnd - hashPos - 1;
                        bool atBoundary = (hEnd >= valStr.length() || iswspace(valStr[hEnd]) ||
                                           valStr[hEnd] == L';' || valStr[hEnd] == L',' ||
                                           valStr[hEnd] == L')' || valStr[hEnd] == L'}');
                        if (atBoundary && hLen > 0 && hLen != 3 && hLen != 4 && hLen != 6 && hLen != 8) {
                            diagnostics.push_back({ l, valStart + hashPos, hEnd - hashPos, L"CSS Syntax Error: Invalid hex color length in '" + valStr.substr(hashPos, hEnd - hashPos) + L"'", 1 });
                        }
                        hashPos = (hEnd > hashPos) ? hEnd : (hashPos + 1);
                    }
                }
            } else {
                c++;
            }
        }
    }

    for (const auto& b : braceStack) {
        diagnostics.push_back({ b.line, b.col, 1, L"CSS Syntax Error: Unclosed brace '{'", 1 });
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateJavaScript(
    const Buffer& buffer,
    size_t startLine,
    size_t endLine
) {
    std::vector<lsp::Diagnostic> diagnostics;
    size_t lineCount = buffer.GetLineCount();
    if (endLine > lineCount) endLine = lineCount;

    struct Bracket { wchar_t ch; size_t line; size_t col; };
    std::vector<Bracket> stack;

    bool inBlockComment = false;

    for (size_t l = startLine; l < endLine; ++l) {
        const auto& line = buffer.GetLine(l);
        bool inStr = false;
        wchar_t quoteChar = L'\0';

        for (size_t c = 0; c < line.length(); ++c) {
            if (inBlockComment) {
                if (c + 1 < line.length() && line[c] == L'*' && line[c + 1] == L'/') {
                    inBlockComment = false;
                    c++;
                }
                continue;
            }

            if (inStr) {
                if (line[c] == L'\\') {
                    c++;
                    continue;
                }
                if (line[c] == quoteChar) {
                    inStr = false;
                }
                continue;
            }

            if (c + 1 < line.length() && line[c] == L'/' && line[c + 1] == L'/') {
                break;
            }
            if (c + 1 < line.length() && line[c] == L'/' && line[c + 1] == L'*') {
                inBlockComment = true;
                c++;
                continue;
            }

            wchar_t ch = line[c];
            if (ch == L'"' || ch == L'\'' || ch == L'`') {
                inStr = true;
                quoteChar = ch;
                continue;
            }

            if (ch == L'(' || ch == L'[' || ch == L'{') {
                stack.push_back({ ch, l, c });
            } else if (ch == L')' || ch == L']' || ch == L'}') {
                if (stack.empty()) {
                    diagnostics.push_back({ l, c, 1, std::wstring(L"JS Syntax Error: Unexpected closing bracket '") + ch + L"'", 1 });
                } else {
                    wchar_t expected = (stack.back().ch == L'(' ? L')' : (stack.back().ch == L'[' ? L']' : L'}'));
                    if (ch == expected) {
                        stack.pop_back();
                    } else {
                        diagnostics.push_back({ l, c, 1, std::wstring(L"JS Syntax Error: Mismatched bracket: expected '") + expected + L"' but found '" + ch + L"'", 1 });
                        stack.pop_back();
                    }
                }
            }
        }

        if (inStr && quoteChar != L'`') {
            diagnostics.push_back({ l, line.length() > 0 ? line.length() - 1 : 0, 1, L"JS Syntax Error: Unclosed string literal", 1 });
        }
    }

    for (const auto& b : stack) {
        diagnostics.push_back({ b.line, b.col, 1, std::wstring(L"JS Syntax Error: Unclosed bracket '") + b.ch + L"'", 1 });
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateHtml(
    const Buffer& buffer,
    bool validateScripts,
    bool validateStyles
) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct TagInfo {
        std::wstring name;
        size_t line;
        size_t col;
    };
    std::vector<TagInfo> tagStack;
    std::unordered_map<std::wstring, std::pair<size_t, size_t>> seenIds;

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

                // Find closing '>'
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

                // Duplicate ID check: id="..."
                size_t idPos = tagContent.find(L"id=\"");
                if (idPos != std::wstring::npos) {
                    size_t idValStart = idPos + 4;
                    size_t idValEnd = tagContent.find(L'"', idValStart);
                    if (idValEnd != std::wstring::npos) {
                        std::wstring idVal = tagContent.substr(idValStart, idValEnd - idValStart);
                        if (!idVal.empty()) {
                            auto it = seenIds.find(idVal);
                            if (it != seenIds.end()) {
                                diagnostics.push_back({ tagStartLine, tagStartCol, idVal.length(),
                                    L"HTML Validation: Duplicate ID '" + idVal + L"'. Element IDs must be unique within an HTML document", 2 });
                            } else {
                                seenIds[idVal] = { tagStartLine, tagStartCol };
                            }
                        }
                    }
                }

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
                            diagnostics.push_back({ tagStartLine, tagStartCol, closeName.length() + 3,
                                L"Syntax Error: Mismatched tag: expected </" + tagStack.back().name + L"> but found </" + closeName + L">", 1 });

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
                            size_t blockStartLine = l + 1;

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
                            } else {
                                // Image 3: Production-Ready Validation on embedded styles and scripts!
                                if (lowerTag == L"style" && validateStyles && l > blockStartLine) {
                                    auto styleDiags = ValidateCss(buffer, blockStartLine, l);
                                    diagnostics.insert(diagnostics.end(), styleDiags.begin(), styleDiags.end());
                                } else if (lowerTag == L"script" && validateScripts && l > blockStartLine) {
                                    auto scriptDiags = ValidateJavaScript(buffer, blockStartLine, l);
                                    diagnostics.insert(diagnostics.end(), scriptDiags.begin(), scriptDiags.end());
                                }
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
