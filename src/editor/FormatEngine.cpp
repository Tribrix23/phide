/**
 * @file FormatEngine.cpp
 * @brief High-performance native offline code formatter for HTML, CSS, JavaScript/TypeScript/JSX, JSON, and PHP.
 *        100% local C++20 implementation. No external tools, cloud APIs, or LLMs required.
 */

#include "phide/editor/FormatEngine.hpp"
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <cwctype>
#include <vector>

namespace phide::editor {

// ==============================================================================
// Common Formatting Helpers
// ==============================================================================

static std::wstring MakeIndent(int level, int tabSize) {
    if (level <= 0) return L"";
    return std::wstring(static_cast<size_t>(level * tabSize), L' ');
}

static std::wstring Trim(const std::wstring& s) {
    size_t start = 0;
    while (start < s.length() && (s[start] == L' ' || s[start] == L'\t' || s[start] == L'\r' || s[start] == L'\n')) {
        start++;
    }
    size_t end = s.length();
    while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        end--;
    }
    return (start < end) ? s.substr(start, end - start) : L"";
}

static std::wstring TrimRight(const std::wstring& s) {
    size_t end = s.length();
    while (end > 0 && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        end--;
    }
    return s.substr(0, end);
}

static bool StartsWith(const std::wstring& str, const std::wstring& prefix) {
    return str.rfind(prefix, 0) == 0;
}

static bool ch_starts(const std::wstring& s, size_t pos, const wchar_t* prefix) {
    if (!prefix) return false;
    size_t plen = wcslen(prefix);
    if (pos + plen > s.length()) return false;
    return s.compare(pos, plen, prefix) == 0;
}

static bool EndsWith(const std::wstring& str, const std::wstring& suffix) {
    if (str.length() < suffix.length()) return false;
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

// ==============================================================================
// 1. JSON Formatter
// ==============================================================================

std::wstring FormatEngine::FormatJson(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";

    std::wstring out;
    out.reserve(src.length() * 2);

    int indentLevel = 0;
    bool inString = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool escapeNext = false;
    bool needNewlineIndent = false;

    auto appendIndent = [&]() {
        out += MakeIndent(indentLevel, tabSize);
    };

    size_t i = 0;
    const size_t len = src.length();

    while (i < len) {
        wchar_t ch = src[i];

        // 1. Single-line comment handling (JSONC)
        if (inLineComment) {
            out += ch;
            if (ch == L'\n') {
                inLineComment = false;
                appendIndent();
            }
            i++;
            continue;
        }

        // 2. Block comment handling (JSONC)
        if (inBlockComment) {
            out += ch;
            if (ch == L'*' && i + 1 < len && src[i + 1] == L'/') {
                out += L'/';
                i += 2;
                inBlockComment = false;
                continue;
            }
            i++;
            continue;
        }

        // 3. String literal handling (preserve exact string contents & escapes)
        if (inString) {
            out += ch;
            if (escapeNext) {
                escapeNext = false;
            } else if (ch == L'\\') {
                escapeNext = true;
            } else if (ch == L'"') {
                inString = false;
            }
            i++;
            continue;
        }

        // Check for comment start
        if (ch == L'/' && i + 1 < len) {
            if (src[i + 1] == L'/') {
                inLineComment = true;
                out += L"//";
                i += 2;
                continue;
            } else if (src[i + 1] == L'*') {
                inBlockComment = true;
                out += L"/*";
                i += 2;
                continue;
            }
        }

        // Check for string start
        if (ch == L'"') {
            if (needNewlineIndent) {
                out += L'\n';
                appendIndent();
                needNewlineIndent = false;
            }
            inString = true;
            escapeNext = false;
            out += ch;
            i++;
            continue;
        }

        // Whitespace outside strings is ignored in raw stream
        if (iswspace(ch)) {
            i++;
            continue;
        }

        if (needNewlineIndent) {
            out += L'\n';
            appendIndent();
            needNewlineIndent = false;
        }

        if (ch == L'{' || ch == L'[') {
            // Check if empty structure: {} or []
            size_t nextIdx = i + 1;
            while (nextIdx < len && iswspace(src[nextIdx])) nextIdx++;
            wchar_t closing = (ch == L'{') ? L'}' : L']';
            if (nextIdx < len && src[nextIdx] == closing) {
                out += ch;
                out += closing;
                i = nextIdx + 1;
                continue;
            }

            out += ch;
            indentLevel++;
            needNewlineIndent = true;
            i++;
            continue;
        }

        if (ch == L'}' || ch == L']') {
            if (indentLevel > 0) indentLevel--;
            if (!out.empty() && out.back() != L'\n') {
                out += L'\n';
            }
            appendIndent();
            out += ch;
            i++;
            continue;
        }

        if (ch == L',') {
            out += L',';
            needNewlineIndent = true;
            i++;
            continue;
        }

        if (ch == L':') {
            out += L": ";
            i++;
            continue;
        }

        // Literals (numbers, true, false, null)
        out += ch;
        i++;
    }

    if (!out.empty() && out.back() != L'\n') {
        out += L'\n';
    }
    return out;
}

// ==============================================================================
// 2. CSS Formatter
// ==============================================================================

std::wstring FormatEngine::FormatCss(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";

    std::wstring out;
    out.reserve(src.length() * 2);

    int indentLevel = 0;
    bool inComment = false;
    bool inString = false;
    wchar_t stringDelimiter = 0;
    bool needIndent = true;

    auto appendIndent = [&]() {
        out += MakeIndent(indentLevel, tabSize);
    };

    size_t i = 0;
    const size_t len = src.length();

    while (i < len) {
        wchar_t ch = src[i];

        // 1. Comment handling
        if (inComment) {
            out += ch;
            if (ch == L'*' && i + 1 < len && src[i + 1] == L'/') {
                out += L'/';
                i += 2;
                inComment = false;
                out += L'\n';
                needIndent = true;
                continue;
            }
            i++;
            continue;
        }

        // 2. String literal handling
        if (inString) {
            out += ch;
            if (ch == L'\\' && i + 1 < len) {
                out += src[i + 1];
                i += 2;
                continue;
            }
            if (ch == stringDelimiter) {
                inString = false;
            }
            i++;
            continue;
        }

        // Comment start
        if (ch == L'/' && i + 1 < len && src[i + 1] == L'*') {
            inComment = true;
            if (needIndent) {
                appendIndent();
                needIndent = false;
            }
            out += L"/*";
            i += 2;
            continue;
        }

        // String start
        if (ch == L'"' || ch == L'\'') {
            inString = true;
            stringDelimiter = ch;
            if (needIndent) {
                appendIndent();
                needIndent = false;
            }
            out += ch;
            i++;
            continue;
        }

        // Open brace: start of rule block or media query
        if (ch == L'{') {
            while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
            out += L" {\n";
            indentLevel++;
            needIndent = true;
            i++;
            while (i < len && iswspace(src[i])) i++;
            continue;
        }

        // Close brace: end of rule block
        if (ch == L'}') {
            while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            if (indentLevel > 0) indentLevel--;
            appendIndent();
            out += L"}\n";
            if (indentLevel == 0) out += L'\n'; // Blank line between top-level rule blocks
            needIndent = true;
            i++;
            while (i < len && iswspace(src[i])) i++;
            continue;
        }

        // Semicolon: end of property declaration
        if (ch == L';') {
            out += L";\n";
            needIndent = true;
            i++;
            while (i < len && iswspace(src[i])) i++;
            continue;
        }

        // Colon: separator between property and value
        if (ch == L':') {
            out += L": ";
            i++;
            while (i < len && (src[i] == L' ' || src[i] == L'\t')) i++;
            continue;
        }

        // Comma in selector lists
        if (ch == L',') {
            out += L",\n";
            needIndent = true;
            i++;
            while (i < len && (src[i] == L' ' || src[i] == L'\t')) i++;
            continue;
        }

        // Whitespace collapsing
        if (iswspace(ch)) {
            if (!needIndent && !out.empty() && out.back() != L' ' && out.back() != L'\n') {
                out += L' ';
            }
            i++;
            continue;
        }

        if (needIndent) {
            appendIndent();
            needIndent = false;
        }
        out += ch;
        i++;
    }

    // Clean up excessive trailing blank lines
    while (out.length() >= 2 && out.substr(out.length() - 2) == L"\n\n") {
        out.pop_back();
    }
    if (!out.empty() && out.back() != L'\n') {
        out += L'\n';
    }
    return out;
}

// ==============================================================================
// 3. HTML Formatter
// ==============================================================================

std::wstring FormatEngine::FormatHtml(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";

    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"link", L"meta", L"param", L"source", L"track", L"wbr", L"!doctype"
    };

    std::wstring out;
    out.reserve(src.length() * 2);
    int indentLevel = 0;
    bool inComment = false;

    auto appendIndent = [&]() {
        out += MakeIndent(indentLevel, tabSize);
    };

    size_t i = 0;
    const size_t len = src.length();

    while (i < len) {
        // 1. HTML Comment: <!-- ... -->
        if (!inComment && ch_starts(src, i, L"<!--")) {
            inComment = true;
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            appendIndent();
            out += L"<!--";
            i += 4;
            continue;
        }
        if (inComment) {
            out += src[i];
            if (ch_starts(src, i, L"-->")) {
                out += L"->";
                i += 3;
                inComment = false;
                out += L'\n';
                continue;
            }
            i++;
            continue;
        }

        // 2. Embedded PHP block: <?php ... ?> or <?= ... ?>
        if (ch_starts(src, i, L"<?")) {
            size_t phpEnd = src.find(L"?>", i + 2);
            if (phpEnd != std::wstring::npos) {
                std::wstring phpBlock = src.substr(i, (phpEnd + 2) - i);
                if (!out.empty() && out.back() != L'\n') out += L'\n';
                appendIndent();
                out += Trim(phpBlock) + L'\n';
                i = phpEnd + 2;
                while (i < len && (src[i] == L'\r' || src[i] == L'\n' || src[i] == L' ' || src[i] == L'\t')) i++;
                continue;
            }
        }

        // 3. HTML Tag
        if (src[i] == L'<') {
            size_t tagEnd = src.find(L'>', i);
            if (tagEnd != std::wstring::npos) {
                std::wstring tagContent = src.substr(i + 1, tagEnd - (i + 1));
                std::wstring tagContentTrimmed = Trim(tagContent);
                bool isClosing = (!tagContentTrimmed.empty() && tagContentTrimmed[0] == L'/');
                bool isSelfClosing = (!tagContentTrimmed.empty() && tagContentTrimmed.back() == L'/');

                size_t nameStart = isClosing ? 1 : 0;
                while (nameStart < tagContentTrimmed.length() && iswspace(tagContentTrimmed[nameStart])) nameStart++;
                size_t nameEnd = nameStart;
                while (nameEnd < tagContentTrimmed.length() && !iswspace(tagContentTrimmed[nameEnd]) && tagContentTrimmed[nameEnd] != L'/') {
                    nameEnd++;
                }
                std::wstring tagName = tagContentTrimmed.substr(nameStart, nameEnd - nameStart);
                for (auto& c : tagName) c = static_cast<wchar_t>(towlower(c));

                bool isVoid = (voidTags.count(tagName) > 0 || isSelfClosing);

                if (isClosing) {
                    if (indentLevel > 0) indentLevel--;
                    if (!out.empty() && out.back() != L'\n') out += L'\n';
                    appendIndent();
                } else {
                    if (!out.empty() && out.back() != L'\n') out += L'\n';
                    appendIndent();
                }

                out += L'<' + tagContentTrimmed + L">\n";
                i = tagEnd + 1;

                // Special handling for <style> and <script> contents
                if (!isClosing && !isVoid) {
                    if (tagName == L"style") {
                        size_t styleEnd = src.find(L"</style>", i);
                        if (styleEnd == std::wstring::npos) styleEnd = src.find(L"</STYLE>", i);
                        if (styleEnd != std::wstring::npos) {
                            std::wstring cssContent = src.substr(i, styleEnd - i);
                            std::wstring formattedCss = FormatCss(cssContent, tabSize);
                            std::wistringstream ss(formattedCss);
                            std::wstring cssLine;
                            while (std::getline(ss, cssLine)) {
                                if (!cssLine.empty() && cssLine.back() == L'\r') cssLine.pop_back();
                                std::wstring trimmedLine = Trim(cssLine);
                                if (!trimmedLine.empty()) {
                                    out += MakeIndent(indentLevel + 1, tabSize) + trimmedLine + L'\n';
                                }
                            }
                            i = styleEnd;
                            continue;
                        }
                    } else if (tagName == L"script") {
                        size_t scriptEnd = src.find(L"</script>", i);
                        if (scriptEnd == std::wstring::npos) scriptEnd = src.find(L"</SCRIPT>", i);
                        if (scriptEnd != std::wstring::npos) {
                            std::wstring jsContent = src.substr(i, scriptEnd - i);
                            std::wstring formattedJs = FormatJavaScript(jsContent, tabSize);
                            std::wistringstream ss(formattedJs);
                            std::wstring jsLine;
                            while (std::getline(ss, jsLine)) {
                                if (!jsLine.empty() && jsLine.back() == L'\r') jsLine.pop_back();
                                std::wstring trimmedLine = Trim(jsLine);
                                if (!trimmedLine.empty()) {
                                    out += MakeIndent(indentLevel + 1, tabSize) + trimmedLine + L'\n';
                                }
                            }
                            i = scriptEnd;
                            continue;
                        }
                    }

                    indentLevel++;
                }

                while (i < len && (src[i] == L' ' || src[i] == L'\t' || src[i] == L'\r' || src[i] == L'\n')) i++;
                continue;
            }
        }

        // 4. Text content between tags
        size_t nextTag = src.find(L'<', i);
        if (nextTag == std::wstring::npos) nextTag = len;
        std::wstring text = Trim(src.substr(i, nextTag - i));

        if (!text.empty()) {
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            appendIndent();
            out += text + L'\n';
        }
        i = nextTag;
    }

    if (!out.empty() && out.back() != L'\n') out += L'\n';
    return out;
}

// ==============================================================================
// 4. JavaScript / TypeScript / JSX Formatter
// ==============================================================================

std::wstring FormatEngine::FormatJavaScript(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";

    std::wstring out;
    out.reserve(src.length() * 2);
    int indentLevel = 0;
    std::wistringstream iss(src);
    std::wstring line;

    bool inBlockComment = false;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::wstring trimmed = Trim(line);
        if (trimmed.empty()) {
            // Avoid more than one consecutive blank line
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            if (out.length() >= 2 && out[out.length() - 2] != L'\n') out += L'\n';
            continue;
        }

        // Block comment lines
        if (inBlockComment) {
            out += MakeIndent(indentLevel, tabSize) + trimmed + L'\n';
            if (trimmed.find(L"*/") != std::wstring::npos) {
                inBlockComment = false;
            }
            continue;
        }
        if (StartsWith(trimmed, L"/*") && trimmed.find(L"*/") == std::wstring::npos) {
            inBlockComment = true;
            out += MakeIndent(indentLevel, tabSize) + trimmed + L'\n';
            continue;
        }

        // Count leading closing braces / brackets on this line to dedent before printing
        int leadingCloses = 0;
        size_t idx = 0;
        while (idx < trimmed.length() && (trimmed[idx] == L'}' || trimmed[idx] == L']' || trimmed[idx] == L')')) {
            leadingCloses++;
            idx++;
        }

        // Special case: } else, } catch, } finally
        if (StartsWith(trimmed, L"} else") || StartsWith(trimmed, L"} catch") || StartsWith(trimmed, L"} finally")) {
            leadingCloses = 1;
        }

        int currentIndent = indentLevel - leadingCloses;
        if (currentIndent < 0) currentIndent = 0;

        // Apply clean operator spacing to the trimmed line (outside strings)
        std::wstring spacedLine;
        spacedLine.reserve(trimmed.length() * 2);
        bool inStr = false;
        wchar_t strDelim = 0;
        for (size_t k = 0; k < trimmed.length(); ++k) {
            wchar_t c = trimmed[k];
            if (inStr) {
                spacedLine += c;
                if (c == L'\\' && k + 1 < trimmed.length()) {
                    spacedLine += trimmed[k + 1];
                    k++;
                    continue;
                }
                if (c == strDelim) inStr = false;
                continue;
            }
            if (c == L'\'' || c == L'"' || c == L'`') {
                inStr = true;
                strDelim = c;
                spacedLine += c;
                continue;
            }
            // Add clean space after comma if followed by non-space
            if (c == L',') {
                spacedLine += c;
                if (k + 1 < trimmed.length() && trimmed[k + 1] != L' ' && trimmed[k + 1] != L'\t') {
                    spacedLine += L' ';
                }
                continue;
            }
            // Colon in object literals or type annotations: "key: value"
            if (c == L':') {
                spacedLine += c;
                if (k + 1 < trimmed.length() && trimmed[k + 1] != L' ' && trimmed[k + 1] != L'\t' && trimmed[k + 1] != L':') {
                    spacedLine += L' ';
                }
                continue;
            }
            spacedLine += c;
        }

        out += MakeIndent(currentIndent, tabSize) + spacedLine + L'\n';

        // Compute indent delta for subsequent lines
        bool inQuotes = false;
        wchar_t qChar = 0;
        int delta = 0;

        for (size_t k = 0; k < trimmed.length(); ++k) {
            wchar_t c = trimmed[k];
            if (inQuotes) {
                if (c == L'\\' && k + 1 < trimmed.length()) { k++; continue; }
                if (c == qChar) inQuotes = false;
                continue;
            }
            if (c == L'\'' || c == L'"' || c == L'`') {
                inQuotes = true;
                qChar = c;
                continue;
            }
            if (c == L'/' && k + 1 < trimmed.length() && trimmed[k + 1] == L'/') {
                break; // Line comment ignores remainder of line
            }

            if (c == L'{' || c == L'[' || c == L'(') delta++;
            else if (c == L'}' || c == L']' || c == L')') delta--;
        }

        indentLevel += delta;
        if (indentLevel < 0) indentLevel = 0;
    }

    if (!out.empty() && out.back() != L'\n') out += L'\n';
    return out;
}

// ==============================================================================
// 5. PHP Formatter (PSR-12 Standard)
// ==============================================================================

std::wstring FormatEngine::FormatPhp(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";

    std::wstring out;
    out.reserve(src.length() * 2);
    int indentLevel = 0;
    std::wistringstream iss(src);
    std::wstring line;

    bool inBlockComment = false;
    bool inPhpTag = false;

    auto appendIndent = [&](int lvl) {
        out += MakeIndent(lvl, tabSize);
    };

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::wstring trimmed = Trim(line);
        if (trimmed.empty()) {
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            if (out.length() >= 2 && out[out.length() - 2] != L'\n') out += L'\n';
            continue;
        }

        // 1. Comments
        if (inBlockComment) {
            out += MakeIndent(indentLevel, tabSize) + trimmed + L'\n';
            if (trimmed.find(L"*/") != std::wstring::npos) inBlockComment = false;
            continue;
        }
        if (StartsWith(trimmed, L"/*") && trimmed.find(L"*/") == std::wstring::npos) {
            inBlockComment = true;
            out += MakeIndent(indentLevel, tabSize) + trimmed + L'\n';
            continue;
        }

        // 2. Track PHP tag boundaries
        if (StartsWith(trimmed, L"<?php") || StartsWith(trimmed, L"<?=")) {
            inPhpTag = true;
            out += trimmed + L'\n';
            continue;
        }
        if (trimmed == L"?>") {
            inPhpTag = false;
            out += trimmed + L'\n';
            continue;
        }

        // 3. Determine dedent for lines starting with closing tags / braces / alternative syntax
        int lineDedent = 0;
        if (trimmed[0] == L'}' || trimmed[0] == L']' || trimmed[0] == L')') {
            for (wchar_t ch : trimmed) {
                if (ch == L'}' || ch == L']' || ch == L')') lineDedent++;
                else if (!iswspace(ch)) break;
            }
        } else if (StartsWith(trimmed, L"</")) {
            lineDedent++;
        } else {
            std::wstring lower = trimmed;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            if (StartsWith(lower, L"endif") || StartsWith(lower, L"endforeach") ||
                StartsWith(lower, L"endwhile") || StartsWith(lower, L"endfor") ||
                StartsWith(lower, L"endswitch") || StartsWith(lower, L"else") ||
                StartsWith(lower, L"elseif") || StartsWith(lower, L"} else") ||
                StartsWith(lower, L"} elseif") || StartsWith(lower, L"} catch") ||
                StartsWith(lower, L"} finally")) {
                lineDedent++;
            }
        }

        int currentIndent = indentLevel - lineDedent;
        if (currentIndent < 0) currentIndent = 0;

        // Clean up arrow operators: "=>" with spaces, "->" without spaces
        std::wstring cleanedLine;
        cleanedLine.reserve(trimmed.length() * 2);
        bool inStr = false;
        wchar_t strChar = 0;

        for (size_t k = 0; k < trimmed.length(); ++k) {
            wchar_t c = trimmed[k];
            if (inStr) {
                cleanedLine += c;
                if (c == L'\\' && k + 1 < trimmed.length()) {
                    cleanedLine += trimmed[k + 1];
                    k++;
                    continue;
                }
                if (c == strChar) inStr = false;
                continue;
            }
            if (c == L'\'' || c == L'"') {
                inStr = true;
                strChar = c;
                cleanedLine += c;
                continue;
            }
            // Double arrow: "=>"
            if (c == L'=' && k + 1 < trimmed.length() && trimmed[k + 1] == L'>') {
                // Ensure single space before and after =>
                while (!cleanedLine.empty() && cleanedLine.back() == L' ') cleanedLine.pop_back();
                cleanedLine += L" => ";
                k++;
                while (k + 1 < trimmed.length() && (trimmed[k + 1] == L' ' || trimmed[k + 1] == L'\t')) k++;
                continue;
            }
            // Single arrow object access: "->"
            if (c == L'-' && k + 1 < trimmed.length() && trimmed[k + 1] == L'>') {
                while (!cleanedLine.empty() && cleanedLine.back() == L' ') cleanedLine.pop_back();
                cleanedLine += L"->";
                k++;
                while (k + 1 < trimmed.length() && (trimmed[k + 1] == L' ' || trimmed[k + 1] == L'\t')) k++;
                continue;
            }
            // Comma spacing
            if (c == L',') {
                cleanedLine += c;
                if (k + 1 < trimmed.length() && trimmed[k + 1] != L' ' && trimmed[k + 1] != L'\t') {
                    cleanedLine += L' ';
                }
                continue;
            }
            cleanedLine += c;
        }

        appendIndent(currentIndent);
        out += cleanedLine + L'\n';

        // 4. Compute indent delta for subsequent lines
        int delta = 0;
        bool inStringLiteral = false;
        wchar_t strDelim = 0;

        for (size_t k = 0; k < trimmed.length(); ++k) {
            wchar_t c = trimmed[k];
            if (inStringLiteral) {
                if (c == L'\\' && k + 1 < trimmed.length()) { k++; continue; }
                if (c == strDelim) inStringLiteral = false;
                continue;
            }
            if (c == L'\'' || c == L'"') {
                inStringLiteral = true;
                strDelim = c;
                continue;
            }
            if ((c == L'/' && k + 1 < trimmed.length() && trimmed[k + 1] == L'/') || c == L'#') {
                break;
            }

            if (c == L'{' || c == L'[' || c == L'(') delta++;
            else if (c == L'}' || c == L']' || c == L')') delta--;
        }

        // Alternative control structure colon: "if (...):" or "foreach (...):"
        if (trimmed.back() == L':') {
            std::wstring lower = trimmed;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            if (lower.find(L"if") != std::wstring::npos || lower.find(L"foreach") != std::wstring::npos ||
                lower.find(L"while") != std::wstring::npos || lower.find(L"for") != std::wstring::npos ||
                lower.find(L"else") != std::wstring::npos || lower.find(L"switch") != std::wstring::npos) {
                delta++;
            }
        }

        indentLevel += delta;
        if (indentLevel < 0) indentLevel = 0;
    }

    if (!out.empty() && out.back() != L'\n') out += L'\n';
    return out;
}

// ==============================================================================
// 6. FormatDocument Master Dispatcher
// ==============================================================================

bool FormatEngine::CanFormat(const std::wstring& ext) {
    std::wstring lower = ext;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    return (lower == L".html" || lower == L".htm" || lower == L".svg" || lower == L".xml" ||
            lower == L".css"  || lower == L".scss" || lower == L".less" ||
            lower == L".js"   || lower == L".jsx" || lower == L".ts" || lower == L".tsx" ||
            lower == L".mjs"  || lower == L".cjs" ||
            lower == L".json" || lower == L".jsonc" ||
            lower == L".php"  || lower == L".phtml" || lower == L".php8");
}

std::wstring FormatEngine::FormatDocument(const std::wstring& src, const std::wstring& ext, int tabSize) {
    if (src.empty()) return L"";

    std::wstring lower = ext;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));

    if (lower == L".json" || lower == L".jsonc") {
        return FormatJson(src, tabSize);
    }
    if (lower == L".css" || lower == L".scss" || lower == L".less") {
        return FormatCss(src, tabSize);
    }
    if (lower == L".html" || lower == L".htm" || lower == L".svg" || lower == L".xml") {
        return FormatHtml(src, tabSize);
    }
    if (lower == L".js" || lower == L".jsx" || lower == L".ts" || lower == L".tsx" || lower == L".mjs" || lower == L".cjs") {
        return FormatJavaScript(src, tabSize);
    }
    if (lower == L".php" || lower == L".phtml" || lower == L".php8") {
        return FormatPhp(src, tabSize);
    }

    // Default fallback: normalize line endings and trim trailing whitespace
    std::wistringstream ss(src);
    std::wstring line;
    std::wstring out;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        out += TrimRight(line) + L'\n';
    }
    return out;
}


} // namespace phide::editor
