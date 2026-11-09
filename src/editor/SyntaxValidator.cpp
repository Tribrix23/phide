/**
 * @file SyntaxValidator.cpp
 * @brief High-performance multi-language syntax validator implementation.
 */

#include "phide/editor/SyntaxValidator.hpp"
#include "phide/editor/WebIntelliSenseData.hpp"
#include "phide/plugin/PluginManager.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <stack>
#include <iostream>

namespace phide::editor {

namespace {

bool IsJsRegexStart(const std::wstring& str, size_t pos) {
    if (pos >= str.length() || str[pos] != L'/') return false;
    // JSX closing tag '</' or self-closing '/>' or comment '//' or '/*' is NEVER a regex
    if (pos > 0 && str[pos - 1] == L'<') return false;
    if (pos + 1 < str.length() && (str[pos + 1] == L'>' || str[pos + 1] == L'/' || str[pos + 1] == L'*')) return false;

    // Find preceding non-whitespace character and preceding token
    int p = static_cast<int>(pos) - 1;
    while (p >= 0 && iswspace(str[p])) p--;
    if (p < 0) return true; // Start of line

    wchar_t prevChar = str[p];
    if (prevChar == L'(' || prevChar == L'[' || prevChar == L'{' ||
        prevChar == L',' || prevChar == L';' || prevChar == L':' ||
        prevChar == L'=' || prevChar == L'!' || prevChar == L'?' ||
        prevChar == L'&' || prevChar == L'|' || prevChar == L'+' ||
        prevChar == L'-' || prevChar == L'*' || prevChar == L'%' ||
        prevChar == L'^' || prevChar == L'~') {
        return true;
    }
    // Check for arrow function '=>'
    if (prevChar == L'>' && p > 0 && str[p - 1] == L'=') {
        return true;
    }
    // Check if preceding token is a keyword: return, case, default, yield, await, typeof, void, delete, in, instanceof, do, else
    if (iswalnum(prevChar) || prevChar == L'_' || prevChar == L'$') {
        int wordEnd = p;
        int wordStart = p;
        while (wordStart >= 0 && (iswalnum(str[wordStart]) || str[wordStart] == L'_' || str[wordStart] == L'$')) {
            wordStart--;
        }
        wordStart++;
        std::wstring word = str.substr(wordStart, wordEnd - wordStart + 1);
        if (word == L"return" || word == L"case" || word == L"default" ||
            word == L"yield" || word == L"await" || word == L"typeof" ||
            word == L"void" || word == L"delete" || word == L"in" ||
            word == L"instanceof" || word == L"do" || word == L"else") {
            return true;
        }
    }
    return false;
}

size_t ScanJsRegexEnd(const std::wstring& str, size_t pos) {
    if (pos >= str.length() || str[pos] != L'/') return std::wstring::npos;
    bool inCharClass = false;
    size_t r = pos + 1;
    for (; r < str.length(); ++r) {
        if (str[r] == L'\\') {
            r++; // skip escaped char
            continue;
        }
        if (str[r] == L'[' && !inCharClass) {
            inCharClass = true;
        } else if (str[r] == L']' && inCharClass) {
            inCharClass = false;
        } else if (str[r] == L'/' && !inCharClass) {
            break;
        }
    }
    if (r < str.length() && str[r] == L'/') {
        // Regex successfully matched: consume regex and its flags (g, i, m, s, u, y, d, v)
        while (r + 1 < str.length() && iswalpha(str[r + 1])) {
            r++;
        }
        return r;
    }
    return std::wstring::npos;
}

} // anonymous namespace

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

    std::vector<lsp::Diagnostic> diags;
    if (ext == L".json" || ext == L".jsonc" || ext == L".eslintrc" || ext == L".prettierrc") {
        diags = ValidateJson(buffer);
    } else if (ext == L".css" || ext == L".scss" || ext == L".less") {
        diags = ValidateCss(buffer);
    } else if (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" || ext == L".mjs" || ext == L".cjs") {
        bool isJsx = (ext == L".jsx" || ext == L".tsx");
        diags = ValidateJavaScript(buffer, 0, static_cast<size_t>(-1), isJsx);
        auto reactDiags = ValidateReactAndESLint(buffer, isJsx);
        diags.insert(diags.end(), reactDiags.begin(), reactDiags.end());
    } else if (ext == L".html" || ext == L".htm" || ext == L".xml" || ext == L".svg") {
        diags = ValidateHtml(buffer, validateScripts, validateStyles);
    } else if (ext == L".php") {
        diags = ValidatePhp(buffer);
        auto htmlDiags = ValidateHtml(buffer, validateScripts, validateStyles);
        diags.insert(diags.end(), htmlDiags.begin(), htmlDiags.end());
    } else if (ext == L".py") {
        diags = ValidatePython(buffer);
    } else if (ext == L".yml" || ext == L".yaml") {
        diags = ValidateYaml(buffer);
    } else if (ext == L".cpp" || ext == L".c" || ext == L".cc" || ext == L".cxx" ||
               ext == L".h" || ext == L".hpp" || ext == L".hxx" || ext == L".cs" ||
               ext == L".java" || ext == L".rs" || ext == L".go") {
        diags = ValidateBracketsAndQuotes(buffer, false);
    }

    auto pluginDiags = phide::plugin::PluginManager::Instance().RunDiagnostics(filePath, ext, buffer);
    if (!pluginDiags.empty()) {
        diags.insert(diags.end(), pluginDiags.begin(), pluginDiags.end());
    }
    return diags;
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
                if (c > 0 && line[c - 1] == L':') {
                    continue; // Part of a URL (http://, https://, etc.), NOT a line comment
                }
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
        L"scroll-behavior", L"scroll-margin", L"scroll-margin-top", L"scroll-margin-right", L"scroll-margin-bottom", L"scroll-margin-left",
        L"scroll-margin-inline", L"scroll-margin-block", L"scroll-padding", L"scroll-padding-top", L"scroll-padding-right", L"scroll-padding-bottom", L"scroll-padding-left",
        L"scroll-padding-inline", L"scroll-padding-block", L"scroll-snap-type", L"scroll-snap-align",
        L"scrollbar-width", L"scrollbar-color", L"scrollbar-gutter",
        L"content", L"will-change", L"all",
        // SVG & Masks
        L"fill", L"fill-opacity", L"fill-rule", L"stroke", L"stroke-width", L"stroke-linecap", L"stroke-linejoin", L"stroke-dasharray", L"stroke-dashoffset", L"stroke-opacity",
        L"mask", L"mask-image", L"mask-size", L"mask-repeat", L"mask-position", L"mask-clip",
        // Object & Media
        L"object-fit", L"object-position", L"image-rendering", L"image-orientation",
        // Containment & Intrinsic
        L"contain-intrinsic-size", L"contain-intrinsic-width", L"contain-intrinsic-height",
        L"contain-intrinsic-inline-size", L"contain-intrinsic-block-size",
        // Multi-column
        L"columns", L"column-count", L"column-width", L"column-rule", L"column-rule-width",
        L"column-rule-style", L"column-rule-color", L"column-span", L"column-fill",
        // Font & Typography Extensions
        L"font-feature-settings", L"font-variation-settings", L"font-synthesis", L"font-kerning",
        L"font-optical-sizing", L"font-palette", L"font-smooth",
        L"text-rendering", L"text-orientation", L"text-emphasis", L"text-emphasis-color",
        L"text-emphasis-style", L"text-emphasis-position", L"text-size-adjust", L"tab-size",
        L"line-clamp",
        // Paging & Break
        L"break-after", L"break-before", L"break-inside", L"page-break-after", L"page-break-before", L"page-break-inside",
        L"box-decoration-break",
        // Counters & Quotes
        L"quotes", L"counter-reset", L"counter-increment", L"counter-set",
        // Scroll & Animation Timelines
        L"scroll-timeline", L"scroll-timeline-name", L"scroll-timeline-axis",
        L"view-timeline", L"view-timeline-name", L"view-timeline-axis", L"timeline-scope",
        L"animation-timeline", L"animation-range", L"animation-range-start", L"animation-range-end",
        // Form & Sizing
        L"field-sizing", L"zoom",
        // Color & Print Adjustment
        L"print-color-adjust", L"forced-color-adjust", L"color-interpolation", L"color-interpolation-filters",
        // SVG & Math
        L"clip", L"clip-rule", L"stroke-miterlimit", L"math-style", L"math-depth",
        // CSS Anchoring
        L"anchor-name", L"position-anchor", L"position-try", L"position-try-options", L"position-try-order", L"position-visibility",
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

            // Skip string literals "..." or '...'
            if (ch == L'"' || ch == L'\'') {
                wchar_t quote = ch;
                c++;
                while (c < line.length()) {
                    if (line[c] == L'\\') {
                        c += 2;
                    } else if (line[c] == quote) {
                        c++;
                        break;
                    } else {
                        c++;
                    }
                }
                continue;
            }

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
            if (inBlock && (iswalpha(ch) || (ch == L'-' && c + 1 < line.length() && (line[c + 1] == L'-' || iswalpha(line[c + 1]))))) {
                size_t propStart = c;
                while (c < line.length() && (iswalnum(line[c]) || line[c] == L'-' || line[c] == L'_')) {
                    c++;
                }
                std::wstring propName = line.substr(propStart, c - propStart);
                for (auto& k : propName) k = static_cast<wchar_t>(towlower(k));

                // Find the first delimiter among '{', '}', ';' following this property name (skipping strings)
                size_t dIdx = c;
                wchar_t firstDelim = L'\0';
                size_t firstDelimPos = std::wstring::npos;
                while (dIdx < line.length()) {
                    wchar_t dc = line[dIdx];
                    if (dc == L'"' || dc == L'\'') {
                        wchar_t dq = dc;
                        dIdx++;
                        while (dIdx < line.length()) {
                            if (line[dIdx] == L'\\') {
                                dIdx += 2;
                            } else if (line[dIdx] == dq) {
                                dIdx++;
                                break;
                            } else {
                                dIdx++;
                            }
                        }
                        continue;
                    }
                    if (dc == L'{' || dc == L'}' || dc == L';') {
                        firstDelim = dc;
                        firstDelimPos = dIdx;
                        break;
                    }
                    dIdx++;
                }

                // If the first delimiter is '{', this is a nested selector or nested rule (e.g. '@media ... {' or '.nested {')
                if (firstDelim == L'{') {
                    c = firstDelimPos;
                    continue;
                }

                while (c < line.length() && iswspace(line[c])) c++;

                if (c >= line.length() || line[c] != L':') {
                    // Only flag missing colon if the statement actually ends with a ';'
                    if (firstDelim == L';') {
                        diagnostics.push_back({ l, propStart, propName.length(), L"CSS Syntax Error: Missing colon ':' after property '" + propName + L"'", 1 });
                        c = firstDelimPos + 1;
                    } else {
                        c = propStart + 1;
                    }
                    continue;
                }

                // Colon found! Check if property is known
                static const std::unordered_set<std::wstring> webDataProps = []() {
                    std::unordered_set<std::wstring> s;
                    for (const auto& item : WebIntelliSenseData::GetCssProperties()) {
                        std::wstring lower = item.label;
                        for (auto& chL : lower) chL = static_cast<wchar_t>(towlower(chL));
                        s.insert(std::move(lower));
                    }
                    return s;
                }();

                if (propName.rfind(L"--", 0) != 0 && propName.rfind(L"-webkit-", 0) != 0 &&
                    propName.rfind(L"-moz-", 0) != 0 && propName.rfind(L"-ms-", 0) != 0 && propName.rfind(L"-o-", 0) != 0 &&
                    propName.rfind(L"scroll-margin", 0) != 0 && propName.rfind(L"scroll-padding", 0) != 0) {
                    if (knownProps.find(propName) == knownProps.end() && webDataProps.find(propName) == webDataProps.end()) {
                        diagnostics.push_back({ l, propStart, propName.length(), L"CSS Linter: Unknown CSS property '" + propName + L"'", 2 });
                    }
                }
                size_t colonCol = c;
                c++; // skip ':'

                // Scan value until ';' or '}'
                size_t valStart = c;
                while (c < line.length() && line[c] != L';' && line[c] != L'}') {
                    c++;
                }
                std::wstring valStr = line.substr(valStart, c - valStart);

                // Strip inline /* ... */ comments from value before checking emptiness & tokens
                std::wstring cleanVal;
                cleanVal.reserve(valStr.length());
                for (size_t vi = 0; vi < valStr.length();) {
                    if (vi + 1 < valStr.length() && valStr[vi] == L'/' && valStr[vi + 1] == L'*') {
                        size_t endCmt = valStr.find(L"*/", vi + 2);
                        if (endCmt != std::wstring::npos) {
                            cleanVal.append(endCmt + 2 - vi, L' ');
                            vi = endCmt + 2;
                        } else {
                            cleanVal.append(valStr.length() - vi, L' ');
                            break;
                        }
                    } else {
                        cleanVal.push_back(valStr[vi]);
                        vi++;
                    }
                }

                std::wstring trimmedVal = cleanVal;
                while (!trimmedVal.empty() && iswspace(trimmedVal.front())) trimmedVal.erase(trimmedVal.begin());
                while (!trimmedVal.empty() && iswspace(trimmedVal.back())) trimmedVal.pop_back();

                // 1. Check for empty property value (e.g. `width: ;`, `width:;`, or `width:` with no continuation)
                if (trimmedVal.empty()) {
                    bool continuesOnNextLine = false;
                    if (c >= line.length() && l + 1 < endLine) {
                        std::wstring nextLn = buffer.GetLine(l + 1);
                        size_t ns = 0;
                        while (ns < nextLn.length() && iswspace(nextLn[ns])) ns++;
                        if (ns < nextLn.length() && nextLn[ns] != L'}' && nextLn[ns] != L';') {
                            size_t nextColon = nextLn.find(L':', ns);
                            size_t nextParen = nextLn.find(L'(', ns);
                            if (nextColon == std::wstring::npos || (nextParen != std::wstring::npos && nextParen < nextColon)) {
                                continuesOnNextLine = true;
                            }
                        }
                    }
                    if (!continuesOnNextLine) {
                        size_t endCol = (c < line.length() && line[c] == L';') ? (c + 1) : c;
                        size_t errLen = (endCol > propStart) ? (endCol - propStart) : (colonCol - propStart + 1);
                        diagnostics.push_back({ l, propStart, errLen, L"CSS Syntax Error: Property value expected for '" + propName + L"'", 1 });
                    }
                } else {
                    // 2. Check for unbalanced parentheses in value (e.g. `calc(100% - 20px;`)
                    int parenDepth = 0;
                    size_t firstOpenParen = 0;
                    bool inValQuote = false;
                    wchar_t valQuoteCh = 0;
                    for (size_t vi = 0; vi < cleanVal.length(); ++vi) {
                        wchar_t vc = cleanVal[vi];
                        if (inValQuote) {
                            if (vc == L'\\') { vi++; continue; }
                            if (vc == valQuoteCh) inValQuote = false;
                            continue;
                        }
                        if (vc == L'"' || vc == L'\'') {
                            inValQuote = true;
                            valQuoteCh = vc;
                            continue;
                        }
                        if (vc == L'(') {
                            if (parenDepth == 0) firstOpenParen = vi;
                            parenDepth++;
                        } else if (vc == L')') {
                            if (parenDepth == 0) {
                                diagnostics.push_back({ l, valStart + vi, 1, L"CSS Syntax Error: Unmatched closing parenthesis ')' in property value", 1 });
                            } else {
                                parenDepth--;
                            }
                        }
                    }
                    if (parenDepth > 0 && (c < line.length() && (line[c] == L';' || line[c] == L'}'))) {
                        diagnostics.push_back({ l, valStart + firstOpenParen, 1, L"CSS Syntax Error: Unclosed parenthesis '(' in property value", 1 });
                    }

                    // 3. Check for unknown/invalid CSS units on numeric dimensions (e.g. `20abc`)
                    static const std::unordered_set<std::wstring> validCssUnits = {
                        L"%", L"px", L"em", L"rem", L"vh", L"vw", L"vmin", L"vmax", L"vi", L"vb",
                        L"svh", L"svw", L"svmin", L"svmax", L"svi", L"svb",
                        L"lvh", L"lvw", L"lvmin", L"lvmax", L"lvi", L"lvb",
                        L"dvh", L"dvw", L"dvmin", L"dvmax", L"dvi", L"dvb",
                        L"ch", L"ex", L"cap", L"ic", L"lh", L"rlh",
                        L"cm", L"mm", L"q", L"in", L"pc", L"pt",
                        L"deg", L"grad", L"rad", L"turn",
                        L"s", L"ms", L"hz", L"khz",
                        L"dpi", L"dpcm", L"dppx", L"x",
                        L"fr", L"cqw", L"cqh", L"cqi", L"cqb", L"cqmin", L"cqmax",
                        L"n" // e.g. 2n+1 in nth-child
                    };
                    if (cleanVal.find(L"url(") == std::wstring::npos && propName != L"unicode-range" &&
                        propName != L"grid-area" && propName != L"grid-column" && propName != L"grid-row" &&
                        propName != L"font-family" && propName != L"animation" && propName != L"animation-name") {
                        bool inQ = false;
                        wchar_t qC = 0;
                        for (size_t vi = 0; vi < cleanVal.length();) {
                            wchar_t vc = cleanVal[vi];
                            if (inQ) {
                                if (vc == L'\\') { vi += 2; continue; }
                                if (vc == qC) inQ = false;
                                vi++;
                                continue;
                            }
                            if (vc == L'"' || vc == L'\'') {
                                inQ = true;
                                qC = vc;
                                vi++;
                                continue;
                            }
                            if (vc == L'#') {
                                vi++;
                                while (vi < cleanVal.length() && (iswalnum(cleanVal[vi]) || cleanVal[vi] == L'-' || cleanVal[vi] == L'_')) vi++;
                                continue;
                            }
                            bool prevIsIdent = (vi > 0 && (iswalpha(cleanVal[vi - 1]) || cleanVal[vi - 1] == L'-' || cleanVal[vi - 1] == L'_' || cleanVal[vi - 1] == L'#'));
                            if (!prevIsIdent && iswdigit(vc)) {
                                size_t numStart = vi;
                                while (vi < cleanVal.length() && iswdigit(cleanVal[vi])) vi++;
                                if (vi < cleanVal.length() && cleanVal[vi] == L'.') {
                                    vi++;
                                    while (vi < cleanVal.length() && iswdigit(cleanVal[vi])) vi++;
                                }
                                size_t unitStart = vi;
                                while (vi < cleanVal.length() && (iswalpha(cleanVal[vi]) || cleanVal[vi] == L'%')) {
                                    vi++;
                                }
                                if (vi > unitStart) {
                                    std::wstring unitStr = cleanVal.substr(unitStart, vi - unitStart);
                                    for (auto& cu : unitStr) cu = static_cast<wchar_t>(towlower(cu));
                                    if (validCssUnits.find(unitStr) == validCssUnits.end()) {
                                        diagnostics.push_back({ l, valStart + numStart, vi - numStart,
                                            L"CSS Syntax Error: Unknown CSS unit '" + unitStr + L"'", 1 });
                                    }
                                }
                                continue;
                            }
                            vi++;
                        }
                    }

                    // 4. Check for missing semicolon at end of single-line declaration when followed by another property
                    if (c >= line.length() && !trimmedVal.empty()) {
                        wchar_t lastCh = trimmedVal.back();
                        if (lastCh != L',' && lastCh != L'{' && lastCh != L'(' && lastCh != L'+' && lastCh != L'-' && lastCh != L'*' && lastCh != L'/') {
                            for (size_t nl = l + 1; nl < endLine; ++nl) {
                                std::wstring nextLn = buffer.GetLine(nl);
                                size_t ns = 0;
                                while (ns < nextLn.length() && iswspace(nextLn[ns])) ns++;
                                if (ns >= nextLn.length()) continue;
                                if (ns + 1 < nextLn.length() && nextLn[ns] == L'/' && nextLn[ns + 1] == L'*') break;
                                if (iswalpha(nextLn[ns]) || (nextLn[ns] == L'-' && ns + 1 < nextLn.length() && (nextLn[ns + 1] == L'-' || iswalpha(nextLn[ns + 1])))) {
                                    size_t np = ns;
                                    while (np < nextLn.length() && (iswalnum(nextLn[np]) || nextLn[np] == L'-' || nextLn[np] == L'_')) np++;
                                    while (np < nextLn.length() && iswspace(nextLn[np])) np++;
                                    if (np < nextLn.length() && nextLn[np] == L':') {
                                        size_t nextBrace = nextLn.find(L'{', np);
                                        if (nextBrace == std::wstring::npos) {
                                            size_t endNonWs = line.length();
                                            while (endNonWs > valStart && iswspace(line[endNonWs - 1])) endNonWs--;
                                            size_t colMark = (endNonWs > 0) ? (endNonWs - 1) : colonCol;
                                            diagnostics.push_back({ l, colMark, 1, L"CSS Syntax Error: Missing semicolon ';' at end of declaration", 1 });
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }
                }

                // Check for invalid hex colors in value (ignore url(...) fragments)
                if (valStr.find(L"url(") == std::wstring::npos) {
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

                if (c < line.length() && line[c] == L';') {
                    c++;
                }
                // NOTE: If line[c] == L'}', we do not consume it here, allowing the outer loop to process the block close
                continue;
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
    size_t endLine,
    bool isJsx
) {
    std::vector<lsp::Diagnostic> diagnostics;
    size_t lineCount = buffer.GetLineCount();
    if (endLine > lineCount) endLine = lineCount;

    struct Bracket { wchar_t ch; size_t line; size_t col; };
    std::vector<Bracket> stack;

    bool inBlockComment = false;
    struct TemplateContext { int braceDepth = 0; };
    std::vector<TemplateContext> templateStack;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    std::vector<std::wstring> jsxTagStack;
    std::vector<size_t> jsxExprBraceLevels;
    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"link", L"meta", L"param", L"source", L"track", L"wbr"
    };

    for (size_t l = startLine; l < endLine; ++l) {
        const auto& line = buffer.GetLine(l);

        for (size_t c = 0; c < line.length(); ++c) {
            // 1. Block comment handling
            if (inBlockComment) {
                if (c + 1 < line.length() && line[c] == L'*' && line[c + 1] == L'/') {
                    inBlockComment = false;
                    c++;
                }
                continue;
            }

            // 2. Template literal text mode (when not in an interpolated ${...} expression)
            if (!templateStack.empty() && templateStack.back().braceDepth == 0) {
                if (line[c] == L'\\') {
                    c++; // skip escaped character inside template literal
                    continue;
                }
                if (line[c] == L'`') {
                    templateStack.pop_back(); // template literal ended
                    continue;
                }
                if (line[c] == L'$' && c + 1 < line.length() && line[c + 1] == L'{') {
                    // Enter template interpolation ${...}
                    templateStack.back().braceDepth = 1;
                    c++; // skip '$'
                    continue;
                }
                continue;
            }

            // 3. Single-quote string literal
            if (inSingleQuote) {
                if (line[c] == L'\\') {
                    c++;
                    continue;
                }
                if (line[c] == L'\'') {
                    inSingleQuote = false;
                }
                continue;
            }

            // 4. Double-quote string literal
            if (inDoubleQuote) {
                if (line[c] == L'\\') {
                    c++;
                    continue;
                }
                if (line[c] == L'"') {
                    inDoubleQuote = false;
                }
                continue;
            }

            // 4.5 JSX syntax and children text mode handling
            if (isJsx) {
                bool inJsxText = !jsxTagStack.empty() && jsxExprBraceLevels.empty();
                if (inJsxText) {
                    // Inside JSX text: '{' enters a JavaScript expression
                    if (line[c] == L'{') {
                        stack.push_back({ L'{', l, c });
                        jsxExprBraceLevels.push_back(stack.size());
                        continue;
                    }
                    if (line[c] == L'<') {
                        // Check for closing tag </Tag> or </>
                        if (c + 1 < line.length() && line[c + 1] == L'/') {
                            size_t gt = line.find(L'>', c + 2);
                            if (gt != std::wstring::npos) {
                                std::wstring closeTag;
                                for (size_t k = c + 2; k < gt; ++k) {
                                    if (!iswspace(line[k])) closeTag.push_back(towlower(line[k]));
                                }
                                if (!jsxTagStack.empty()) {
                                    if (closeTag.empty() || jsxTagStack.back() == closeTag) {
                                        jsxTagStack.pop_back();
                                    } else {
                                        for (int idx = static_cast<int>(jsxTagStack.size()) - 1; idx >= 0; --idx) {
                                            if (jsxTagStack[idx] == closeTag) {
                                                jsxTagStack.erase(jsxTagStack.begin() + idx, jsxTagStack.end());
                                                break;
                                            }
                                        }
                                    }
                                }
                                c = gt;
                                continue;
                            }
                        } else if (c + 1 < line.length() && line[c + 1] == L'>') {
                            // Opening fragment <>
                            jsxTagStack.push_back(L"");
                            c++;
                            continue;
                        } else if (c + 1 < line.length() && (iswalpha(line[c + 1]) || line[c + 1] == L'_')) {
                            // Find the matching '>' of the JSX opening tag, properly ignoring quotes, expression braces, and '=>'
                            size_t gt = std::wstring::npos;
                            bool inQ = false;
                            wchar_t qC = L'\0';
                            int bDepth = 0;
                            for (size_t k = c + 1; k < line.length(); ++k) {
                                wchar_t kc = line[k];
                                if (inQ) {
                                    if (kc == L'\\' && k + 1 < line.length()) { k++; continue; }
                                    if (kc == qC) inQ = false;
                                } else {
                                    if (kc == L'"' || kc == L'\'' || kc == L'`') {
                                        inQ = true;
                                        qC = kc;
                                    } else if (kc == L'{') {
                                        bDepth++;
                                    } else if (kc == L'}') {
                                        if (bDepth > 0) bDepth--;
                                    } else if (kc == L'>' && bDepth == 0) {
                                        if (k > 0 && line[k - 1] == L'=') continue; // Ignore arrow function '=>'
                                        gt = k;
                                        break;
                                    }
                                }
                            }
                            if (gt != std::wstring::npos) {
                                size_t tagEnd = c + 1;
                                while (tagEnd < line.length() && (iswalnum(line[tagEnd]) || line[tagEnd] == L'_' || line[tagEnd] == L'-' || line[tagEnd] == L'.')) {
                                    tagEnd++;
                                }
                                std::wstring tag;
                                for (size_t k = c + 1; k < tagEnd; ++k) tag.push_back(towlower(line[k]));
                                bool isSelfClosing = (gt > 0 && line[gt - 1] == L'/') || (voidTags.count(tag) > 0);
                                if (!isSelfClosing) {
                                    jsxTagStack.push_back(tag);
                                }
                                c = gt;
                                continue;
                            }
                        }
                    }
                    // All other characters inside JSX element children (prose, punctuation, &nbsp;) are literal JSX text!
                    continue;
                }

                // In normal JS code: check if '<' starts a JSX element (Fragment or Tag)
                if (line[c] == L'<') {
                    // In JS/TS, a JSX tag CANNOT immediately follow an identifier or closing bracket!
                    int p = static_cast<int>(c) - 1;
                    while (p >= 0 && iswspace(line[p])) p--;
                    wchar_t pch = (p >= 0) ? line[p] : L'\0';
                    bool isPrevIdent = iswalnum(pch) || pch == L'_' || pch == L'$' || pch == L')' || pch == L']';

                    if (!isPrevIdent) {
                        if (c + 1 < line.length() && line[c + 1] == L'/') {
                            size_t gt = line.find(L'>', c + 2);
                            if (gt != std::wstring::npos) {
                                std::wstring closeTag;
                                for (size_t k = c + 2; k < gt; ++k) {
                                    if (!iswspace(line[k])) closeTag.push_back(towlower(line[k]));
                                }
                                if (!jsxTagStack.empty()) {
                                    if (closeTag.empty() || jsxTagStack.back() == closeTag) {
                                        jsxTagStack.pop_back();
                                    } else {
                                        for (int idx = static_cast<int>(jsxTagStack.size()) - 1; idx >= 0; --idx) {
                                            if (jsxTagStack[idx] == closeTag) {
                                                jsxTagStack.erase(jsxTagStack.begin() + idx, jsxTagStack.end());
                                                break;
                                            }
                                        }
                                    }
                                }
                                c = gt;
                                continue;
                            }
                        } else if (c + 1 < line.length() && line[c + 1] == L'>') {
                            // Opening fragment <>
                            jsxTagStack.push_back(L"");
                            c++;
                            continue;
                        } else if (c + 1 < line.length() && (iswalpha(line[c + 1]) || line[c + 1] == L'_')) {
                            // Find the matching '>' of the JSX opening tag, properly ignoring quotes, expression braces, and '=>'
                            size_t gt = std::wstring::npos;
                            bool inQ = false;
                            wchar_t qC = L'\0';
                            int bDepth = 0;
                            for (size_t k = c + 1; k < line.length(); ++k) {
                                wchar_t kc = line[k];
                                if (inQ) {
                                    if (kc == L'\\' && k + 1 < line.length()) { k++; continue; }
                                    if (kc == qC) inQ = false;
                                } else {
                                    if (kc == L'"' || kc == L'\'' || kc == L'`') {
                                        inQ = true;
                                        qC = kc;
                                    } else if (kc == L'{') {
                                        bDepth++;
                                    } else if (kc == L'}') {
                                        if (bDepth > 0) bDepth--;
                                    } else if (kc == L'>' && bDepth == 0) {
                                        if (k > 0 && line[k - 1] == L'=') continue; // Ignore arrow function '=>'
                                        gt = k;
                                        break;
                                    }
                                }
                            }
                            if (gt != std::wstring::npos) {
                                size_t tagEnd = c + 1;
                                while (tagEnd < line.length() && (iswalnum(line[tagEnd]) || line[tagEnd] == L'_' || line[tagEnd] == L'-' || line[tagEnd] == L'.')) {
                                    tagEnd++;
                                }
                                std::wstring tag;
                                for (size_t k = c + 1; k < tagEnd; ++k) tag.push_back(towlower(line[k]));
                                bool isSelfClosing = (gt > 0 && line[gt - 1] == L'/') || (voidTags.count(tag) > 0);
                                if (!isSelfClosing) {
                                    jsxTagStack.push_back(tag);
                                }
                                c = gt;
                                continue;
                            }
                        }
                    }
                }
            }

            // 5. Comments
            if (c + 1 < line.length() && line[c] == L'/' && line[c + 1] == L'/') {
                if (c > 0 && line[c - 1] == L':') {
                    continue; // Part of a URL (http://, https://, etc.), NOT a line comment
                }
                if (line.find(L"</", c + 2) != std::wstring::npos) {
                    continue; // Inside JSX text followed by closing tag (e.g. <span>// comment</span>), NOT a JS comment
                }
                break; // Single line comment: ignore rest of line
            }
            if (c + 1 < line.length() && line[c] == L'/' && line[c + 1] == L'*') {
                inBlockComment = true;
                c++;
                continue;
            }

            // 6. Regular expression literal detection (e.g. line.replace(/"([^"]+)":/g, ...))
            if (line[c] == L'/') {
                if (IsJsRegexStart(line, c)) {
                    size_t r = ScanJsRegexEnd(line, c);
                    if (r != std::wstring::npos) {
                        c = r;
                        continue;
                    }
                }
            }

            // 7. Start of new strings / template literals
            wchar_t ch = line[c];
            if (ch == L'\'') {
                if (c > 0 && (iswalnum(line[c - 1]) || line[c - 1] == L'_')) {
                    continue; // Contraction / apostrophe in JSX/HTML text (e.g. isn't, don't, user's), NOT a string literal
                }
                inSingleQuote = true;
                continue;
            }
            if (ch == L'"') {
                inDoubleQuote = true;
                continue;
            }
            if (ch == L'`') {
                templateStack.push_back({ 0 });
                continue;
            }

            // 8. Bracket parsing and balance verification
            if (ch == L'(' || ch == L'[' || ch == L'{') {
                if (!templateStack.empty() && ch == L'{') {
                    templateStack.back().braceDepth++;
                }
                stack.push_back({ ch, l, c });
            } else if (ch == L')' || ch == L']' || ch == L'}') {
                if (!templateStack.empty() && ch == L'}') {
                    if (templateStack.back().braceDepth == 1) {
                        // Closing brace of the ${...} interpolation
                        templateStack.back().braceDepth = 0;
                        continue;
                    } else if (templateStack.back().braceDepth > 1) {
                        templateStack.back().braceDepth--;
                    }
                }
                if (isJsx && ch == L'}' && !jsxExprBraceLevels.empty() && stack.size() == jsxExprBraceLevels.back()) {
                    jsxExprBraceLevels.pop_back();
                }

                if (stack.empty()) {
                    diagnostics.push_back({ l, c, 1, std::wstring(L"JS Syntax Error: Unexpected closing bracket '") + ch + L"'", 1 });
                } else {
                    wchar_t expected = (stack.back().ch == L'(' ? L')' : (stack.back().ch == L'[' ? L']' : L'}'));
                    if (ch == expected) {
                        stack.pop_back();
                    } else {
                        diagnostics.push_back({ l, c, 1, std::wstring(L"JS Syntax Error: Mismatched bracket: expected '") + expected + L"' (opened at line " + std::to_wstring(stack.back().line + 1) + L":" + std::to_wstring(stack.back().col) + L") but found '" + ch + L"'", 1 });
                        stack.pop_back();
                    }
                }
            }
        }

        // Single and double quotes cannot span across lines in JS unless ended with a backslash
        if (inSingleQuote) {
            if (line.empty() || line.back() != L'\\') {
                diagnostics.push_back({ l, line.length() > 0 ? line.length() - 1 : 0, 1, L"JS Syntax Error: Unclosed string literal", 1 });
                inSingleQuote = false;
            }
        }
        if (inDoubleQuote) {
            if (line.empty() || line.back() != L'\\') {
                diagnostics.push_back({ l, line.length() > 0 ? line.length() - 1 : 0, 1, L"JS Syntax Error: Unclosed string literal", 1 });
                inDoubleQuote = false;
            }
        }
    }

    if (!templateStack.empty()) {
        diagnostics.push_back({ endLine > 0 ? endLine - 1 : 0, 0, 1, L"JS Syntax Error: Unclosed template literal", 1 });
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
    bool inPhp = false;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        for (size_t c = 0; l < buffer.GetLineCount() && c < buffer.GetLine(l).length(); ++c) {
            const auto& line = buffer.GetLine(l);
            // Skip PHP code blocks completely in mixed PHP/HTML templates
            if (inPhp) {
                if (c + 1 < line.length() && line[c] == L'?' && line[c + 1] == L'>') {
                    inPhp = false;
                    c++;
                }
                continue;
            }

            if (inComment) {
                if (c + 2 < line.length() && line[c] == L'-' && line[c + 1] == L'-' && line[c + 2] == L'>') {
                    inComment = false;
                    c += 2;
                }
                continue;
            }

            // Detect PHP opening tag
            if (line[c] == L'<' && c + 1 < line.length() && line[c + 1] == L'?') {
                inPhp = true;
                c++;
                continue;
            }

            if (line[c] == L'<' && c + 3 < line.length() && line[c + 1] == L'!' && line[c + 2] == L'-' && line[c + 3] == L'-') {
                inComment = true;
                c += 3;
                continue;
            }

            if (line[c] == L'<') {
                // In HTML, a tag bracket MUST be followed by an ASCII letter, '/', or '!'
                // Comparison operators (e.g. '< 1', '<=', '<<') or standalone '<' are NOT HTML tags!
                if (c + 1 >= line.length()) continue;
                wchar_t nextCh = line[c + 1];
                if (!iswalpha(nextCh) && nextCh != L'/' && nextCh != L'!') {
                    continue;
                }
                size_t tagStartLine = l;
                size_t tagStartCol = c;

                // Find closing '>'
                bool foundClose = false;
                size_t endLine = l;
                size_t endCol = 0;
                bool inQuotes = false;
                wchar_t quoteChar = L'\0';
                bool inTagPhp = false;

                size_t curScanL = l;
                size_t curScanC = c + 1;

                while (curScanL < buffer.GetLineCount()) {
                    const auto& scanText = buffer.GetLine(curScanL);
                    for (size_t k = curScanC; k < scanText.length(); ++k) {
                        wchar_t ch = scanText[k];
                        if (inTagPhp) {
                            if (ch == L'?' && k + 1 < scanText.length() && scanText[k + 1] == L'>') {
                                inTagPhp = false;
                                k++;
                            }
                            continue;
                        }
                        if (ch == L'<' && k + 1 < scanText.length() && scanText[k + 1] == L'?') {
                            inTagPhp = true;
                            k++;
                            continue;
                        }
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

static size_t CalculateLevenshtein(const std::wstring& s1, const std::wstring& s2) {
    const size_t m = s1.length();
    const size_t n = s2.length();
    if (m == 0) return n;
    if (n == 0) return m;
    if (std::abs(static_cast<long long>(m) - static_cast<long long>(n)) > 3) return 99;

    std::vector<size_t> v0(n + 1);
    std::vector<size_t> v1(n + 1);
    for (size_t i = 0; i <= n; ++i) v0[i] = i;

    for (size_t i = 0; i < m; ++i) {
        v1[0] = i + 1;
        for (size_t j = 0; j < n; ++j) {
            size_t cost = (towlower(s1[i]) == towlower(s2[j])) ? 0 : 1;
            v1[j + 1] = (std::min)({ v1[j] + 1, v0[j + 1] + 1, v0[j] + cost });
        }
        v0 = v1;
    }
    return v0[n];
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidatePython(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct BracketInfo {
        wchar_t ch;
        size_t line;
        size_t col;
    };
    std::vector<BracketInfo> bracketStack;

    bool inTripleString = false;
    wchar_t tripleChar = 0;
    bool hasSeenTabs = false;
    bool hasSeenSpaces = false;

    std::vector<size_t> indentStack = { 0 };
    bool expectIndent = false;
    size_t colonLine = 0;
    bool inExplicitContinuation = false;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);

        // Check leading indentation and calculate visual indentation
        size_t indentCol = 0;
        size_t visualIndent = 0;
        bool lineHasTab = false;
        bool lineHasSpace = false;
        while (indentCol < line.length() && (line[indentCol] == L' ' || line[indentCol] == L'	')) {
            if (line[indentCol] == L'	') {
                lineHasTab = true;
                visualIndent = (visualIndent / 8 + 1) * 8;
            }
            if (line[indentCol] == L' ') {
                lineHasSpace = true;
                visualIndent++;
            }
            indentCol++;
        }

        if (indentCol < line.length() && line[indentCol] != L'#') {
            if (lineHasTab && lineHasSpace) {
                diagnostics.push_back({ l, 0, indentCol, L"Python: Inconsistent indentation (mixed tabs and spaces)", 1 });
            } else if (lineHasTab) {
                hasSeenTabs = true;
                if (hasSeenSpaces) {
                    diagnostics.push_back({ l, 0, indentCol, L"Python: TabError: inconsistent use of tabs and spaces in indentation", 1 });
                }
            } else if (lineHasSpace) {
                hasSeenSpaces = true;
                if (hasSeenTabs) {
                    diagnostics.push_back({ l, 0, indentCol, L"Python: TabError: inconsistent use of tabs and spaces in indentation", 1 });
                }
            }
        }

        bool startedInTripleString = inTripleString;
        bool startedInBracket = !bracketStack.empty();

        bool inString = false;
        wchar_t strChar = 0;
        size_t strStart = 0;
        size_t commentCol = std::wstring::npos;

        for (size_t c = indentCol; c < line.length(); ++c) {
            wchar_t ch = line[c];

            if (inTripleString) {
                if (ch == tripleChar && c + 2 < line.length() && line[c + 1] == tripleChar && line[c + 2] == tripleChar) {
                    inTripleString = false;
                    c += 2;
                }
                continue;
            }

            if (inString) {
                if (ch == L'\\') {
                    c++;
                    continue;
                }
                if (ch == strChar) {
                    inString = false;
                }
                continue;
            }

            if ((ch == L'"' || ch == L'\'') && c + 2 < line.length() && line[c + 1] == ch && line[c + 2] == ch) {
                inTripleString = true;
                tripleChar = ch;
                c += 2;
                continue;
            }

            if (ch == L'"' || ch == L'\'') {
                inString = true;
                strChar = ch;
                strStart = c;
                continue;
            }

            if (ch == L'#') {
                commentCol = c;
                break;
            }

            if (ch == L'(' || ch == L'[' || ch == L'{') {
                bracketStack.push_back({ ch, l, c });
            } else if (ch == L')' || ch == L']' || ch == L'}') {
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, std::wstring(L"Python: Unmatched closing '") + ch + L"'", 1 });
                } else {
                    wchar_t expected = (ch == L')') ? L'(' : (ch == L']') ? L'[' : L'{';
                    if (bracketStack.back().ch != expected) {
                        diagnostics.push_back({ l, c, 1, std::wstring(L"Python: Mismatched bracket: expected '") + (bracketStack.back().ch == L'(' ? L')' : bracketStack.back().ch == L'[' ? L']' : L'}') + L"' but found '" + ch + L"'", 1 });
                        bracketStack.pop_back();
                    } else {
                        bracketStack.pop_back();
                    }
                }
            }
        }

        if (inString && !inTripleString) {
            diagnostics.push_back({ l, strStart, line.length() - strStart, L"Python: EOL while scanning string literal", 1 });
        }

        std::wstring codePart = (commentCol != std::wstring::npos) ? line.substr(0, commentCol) : line;
        size_t codeStart = 0;
        while (codeStart < codePart.length() && (codePart[codeStart] == L' ' || codePart[codeStart] == L'	')) {
            codeStart++;
        }
        std::wstring trimmed = (codeStart < codePart.length()) ? codePart.substr(codeStart) : L"";
        while (!trimmed.empty() && iswspace(trimmed.back())) {
            trimmed.pop_back();
        }

        // Indentation validation (Python PEP 8 / official Python syntax rules)
        if (!startedInTripleString && !inExplicitContinuation && !startedInBracket && !trimmed.empty()) {
            if (expectIndent) {
                if (visualIndent <= indentStack.back()) {
                    diagnostics.push_back({ l, 0, (std::max)(line.length(), 1ULL), L"Python: IndentationError: expected an indented block", 1 });
                    expectIndent = false;
                } else {
                    indentStack.push_back(visualIndent);
                    expectIndent = false;
                }
            } else {
                if (visualIndent > indentStack.back()) {
                    diagnostics.push_back({ l, 0, (std::max)(line.length(), 1ULL), L"Python: IndentationError: unexpected indent", 1 });
                } else if (visualIndent < indentStack.back()) {
                    while (!indentStack.empty() && indentStack.back() > visualIndent) {
                        indentStack.pop_back();
                    }
                    if (indentStack.empty() || indentStack.back() != visualIndent) {
                        diagnostics.push_back({ l, 0, (std::max)(indentCol, 1ULL), L"Python: IndentationError: unindent does not match any outer indentation level", 1 });
                        indentStack.push_back(visualIndent);
                    }
                }
            }
        }

        if (!inTripleString && bracketStack.empty() && !trimmed.empty()) {
            static const std::vector<std::wstring> colonKws = {
                L"def ", L"class ", L"if ", L"elif ", L"else", L"for ", L"while ", L"try", L"except", L"finally", L"with ",
                L"async def ", L"async for ", L"async with ", L"match ", L"case "
            };
            for (const auto& kw : colonKws) {
                if (trimmed == kw || trimmed.rfind(kw, 0) == 0) {
                    if (trimmed.back() != L':') {
                        diagnostics.push_back({ l, indentCol + trimmed.length() - 1, 1, L"Python: SyntaxError: expected ':' at end of statement", 1 });
                    }
                    break;
                }
            }

            if (trimmed.back() == L':') {
                expectIndent = true;
                colonLine = l;
            }
        }

        if (!inTripleString && bracketStack.empty() && !trimmed.empty() && trimmed.back() == L'\\') {
            inExplicitContinuation = true;
        } else if (bracketStack.empty()) {
            inExplicitContinuation = false;
        }
    }

    if (expectIndent) {
        diagnostics.push_back({ colonLine, 0, (std::max)(buffer.GetLine(colonLine).length(), 1ULL), L"Python: IndentationError: expected an indented block", 1 });
    }

    if (inTripleString) {
        diagnostics.push_back({ buffer.GetLineCount() > 0 ? buffer.GetLineCount() - 1 : 0, 0, 1, L"Python: EOF while scanning triple-quoted string literal", 1 });
    }

    for (const auto& b : bracketStack) {
        diagnostics.push_back({ b.line, b.col, 1, std::wstring(L"Python: Unclosed bracket '") + b.ch + L"'", 1 });
    }

    // -------------------------------------------------------------
    // Real-time Python Scope & Undefined Variable Analysis (PEP 8 / Flake8 F821 / NameError)
    // -------------------------------------------------------------
    static const std::unordered_set<std::wstring> pyBuiltins = {
        L"abs", L"aiter", L"all", L"anext", L"any", L"ascii", L"bin", L"bool", L"breakpoint",
        L"bytearray", L"bytes", L"callable", L"chr", L"classmethod", L"compile", L"complex",
        L"delattr", L"dict", L"dir", L"divmod", L"enumerate", L"eval", L"exec", L"filter",
        L"float", L"format", L"frozenset", L"getattr", L"globals", L"hasattr", L"hash", L"help",
        L"hex", L"id", L"input", L"int", L"isinstance", L"issubclass", L"iter", L"len", L"list",
        L"locals", L"map", L"max", L"memoryview", L"min", L"next", L"object", L"oct", L"open",
        L"ord", L"pow", L"print", L"property", L"range", L"repr", L"reversed", L"round", L"set",
        L"setattr", L"slice", L"sorted", L"staticmethod", L"str", L"sum", L"super", L"tuple",
        L"type", L"vars", L"zip", L"__import__",
        L"True", L"False", L"None", L"Ellipsis", L"NotImplemented",
        L"__name__", L"__file__", L"__doc__", L"__package__", L"__annotations__", L"__builtins__",
        L"__debug__", L"__dict__", L"__class__", L"self", L"cls",
        L"BaseException", L"Exception", L"ArithmeticError", L"BufferError", L"LookupError",
        L"AssertionError", L"AttributeError", L"EOFError", L"FloatingPointError", L"GeneratorExit",
        L"ImportError", L"ModuleNotFoundError", L"IndexError", L"KeyError", L"KeyboardInterrupt",
        L"MemoryError", L"NameError", L"NotImplementedError", L"OSError", L"OverflowError",
        L"RecursionError", L"ReferenceError", L"RuntimeError", L"StopIteration", L"StopAsyncIteration",
        L"SyntaxError", L"IndentationError", L"TabError", L"SystemError", L"SystemExit", L"TypeError",
        L"UnboundLocalError", L"UnicodeError", L"UnicodeEncodeError", L"UnicodeDecodeError",
        L"UnicodeTranslateError", L"ValueError", L"ZeroDivisionError", L"EnvironmentError",
        L"IOError", L"WindowsError", L"BlockingIOError", L"ChildProcessError", L"ConnectionError",
        L"BrokenPipeError", L"ConnectionAbortedError", L"ConnectionRefusedError", L"ConnectionResetError",
        L"FileExistsError", L"FileNotFoundError", L"InterruptedError", L"IsADirectoryError",
        L"NotADirectoryError", L"PermissionError", L"ProcessLookupError", L"TimeoutError",
        L"Warning", L"UserWarning", L"DeprecationWarning", L"PendingDeprecationWarning",
        L"SyntaxWarning", L"RuntimeWarning", L"FutureWarning", L"ImportWarning", L"UnicodeWarning",
        L"BytesWarning", L"ResourceWarning"
    };

    static const std::unordered_set<std::wstring> pyKeywords = {
        L"and", L"as", L"assert", L"async", L"await", L"break", L"case", L"class", L"continue",
        L"def", L"del", L"elif", L"else", L"except", L"finally", L"for", L"from", L"global",
        L"if", L"import", L"in", L"is", L"lambda", L"match", L"nonlocal", L"not", L"or",
        L"pass", L"raise", L"return", L"try", L"while", L"with", L"yield"
    };

    std::unordered_set<std::wstring> definedSymbols;
    bool hasStarImport = false;

    auto isIdentChar = [](wchar_t c) {
        return iswalnum(c) || c == L'_';
    };
    auto isIdentStart = [](wchar_t c) {
        return iswalpha(c) || c == L'_';
    };

    // Pass 1: Collect defined symbols across the entire document
    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        std::wstring code = line;
        bool inStr = false;
        wchar_t qChar = 0;
        for (size_t i = 0; i < code.length(); ++i) {
            wchar_t ch = code[i];
            if (!inStr && (ch == L'"' || ch == L'\'')) {
                inStr = true;
                qChar = ch;
            } else if (inStr && ch == qChar && (i == 0 || code[i - 1] != L'\\')) {
                inStr = false;
            } else if (!inStr && ch == L'#') {
                code = code.substr(0, i);
                break;
            }
        }
        size_t start = 0;
        while (start < code.length() && (code[start] == L' ' || code[start] == L'\t')) start++;
        std::wstring trimmedCode = code.substr(start);
        if (trimmedCode.empty()) continue;

        if (trimmedCode.rfind(L"from ", 0) == 0) {
            size_t impPos = trimmedCode.find(L" import ");
            if (impPos != std::wstring::npos) {
                std::wstring fromMod = trimmedCode.substr(5, impPos - 5);
                size_t fm = 0;
                while (fm < fromMod.length()) {
                    while (fm < fromMod.length() && !isIdentStart(fromMod[fm])) fm++;
                    if (fm >= fromMod.length()) break;
                    size_t fStart = fm;
                    while (fm < fromMod.length() && isIdentChar(fromMod[fm])) fm++;
                    std::wstring mTok = fromMod.substr(fStart, fm - fStart);
                    if (!mTok.empty() && pyKeywords.find(mTok) == pyKeywords.end()) {
                        definedSymbols.insert(mTok);
                    }
                }
                std::wstring impPart = trimmedCode.substr(impPos + 8);
                if (impPart.find(L'*') != std::wstring::npos) {
                    hasStarImport = true;
                } else {
                    size_t p = 0;
                    while (p < impPart.length()) {
                        while (p < impPart.length() && (impPart[p] == L' ' || impPart[p] == L',' || impPart[p] == L'(' || impPart[p] == L')')) p++;
                        if (p >= impPart.length()) break;
                        size_t tokenStart = p;
                        while (p < impPart.length() && isIdentChar(impPart[p])) p++;
                        if (p == tokenStart) { p++; continue; }
                        std::wstring sym = impPart.substr(tokenStart, p - tokenStart);
                        if (!sym.empty() && pyKeywords.find(sym) == pyKeywords.end()) {
                            definedSymbols.insert(sym);
                        }
                        while (p < impPart.length() && impPart[p] == L' ') p++;
                        if (p + 3 <= impPart.length() && impPart.substr(p, 3) == L"as ") {
                            p += 3;
                            while (p < impPart.length() && impPart[p] == L' ') p++;
                            size_t aliasStart = p;
                            while (p < impPart.length() && isIdentChar(impPart[p])) p++;
                            std::wstring aliasSym = impPart.substr(aliasStart, p - aliasStart);
                            if (!aliasSym.empty() && pyKeywords.find(aliasSym) == pyKeywords.end()) {
                                definedSymbols.insert(aliasSym);
                            }
                        }
                    }
                }
            }
            continue;
        }

        if (trimmedCode.rfind(L"import ", 0) == 0) {
            std::wstring impPart = trimmedCode.substr(7);
            size_t p = 0;
            while (p < impPart.length()) {
                while (p < impPart.length() && (impPart[p] == L' ' || impPart[p] == L',')) p++;
                if (p >= impPart.length()) break;
                size_t tokenStart = p;
                while (p < impPart.length() && (isIdentChar(impPart[p]) || impPart[p] == L'.')) p++;
                if (p == tokenStart) { p++; continue; }
                std::wstring mod = impPart.substr(tokenStart, p - tokenStart);
                size_t dot = mod.find(L'.');
                std::wstring baseMod = (dot != std::wstring::npos) ? mod.substr(0, dot) : mod;
                if (!baseMod.empty() && pyKeywords.find(baseMod) == pyKeywords.end()) {
                    definedSymbols.insert(baseMod);
                }

                while (p < impPart.length() && impPart[p] == L' ') p++;
                if (p + 3 <= impPart.length() && impPart.substr(p, 3) == L"as ") {
                    p += 3;
                    while (p < impPart.length() && impPart[p] == L' ') p++;
                    size_t aliasStart = p;
                    while (p < impPart.length() && isIdentChar(impPart[p])) p++;
                    std::wstring aliasMod = impPart.substr(aliasStart, p - aliasStart);
                    if (!aliasMod.empty() && pyKeywords.find(aliasMod) == pyKeywords.end()) {
                        definedSymbols.insert(aliasMod);
                    }
                }
            }
            continue;
        }

        if (trimmedCode.rfind(L"def ", 0) == 0 || trimmedCode.rfind(L"async def ", 0) == 0) {
            size_t defOffset = (trimmedCode.rfind(L"async def ", 0) == 0) ? 10 : 4;
            size_t p = defOffset;
            while (p < trimmedCode.length() && trimmedCode[p] == L' ') p++;
            size_t fnStart = p;
            while (p < trimmedCode.length() && isIdentChar(trimmedCode[p])) p++;
            if (p > fnStart) {
                definedSymbols.insert(trimmedCode.substr(fnStart, p - fnStart));
            }
            size_t openP = trimmedCode.find(L'(', p);
            size_t closeP = trimmedCode.rfind(L')');
            if (openP != std::wstring::npos && closeP != std::wstring::npos && closeP > openP) {
                std::wstring params = trimmedCode.substr(openP + 1, closeP - openP - 1);
                size_t pp = 0;
                while (pp < params.length()) {
                    while (pp < params.length() && (params[pp] == L' ' || params[pp] == L'*' || params[pp] == L'/')) pp++;
                    if (pp >= params.length()) break;
                    if (isIdentStart(params[pp])) {
                        size_t pStart = pp;
                        while (pp < params.length() && isIdentChar(params[pp])) pp++;
                        std::wstring paramName = params.substr(pStart, pp - pStart);
                        if (!paramName.empty() && pyKeywords.find(paramName) == pyKeywords.end()) {
                            definedSymbols.insert(paramName);
                        }
                    }
                    while (pp < params.length() && params[pp] != L',') pp++;
                    if (pp < params.length() && params[pp] == L',') pp++;
                }
            }
            continue;
        }

        if (trimmedCode.rfind(L"class ", 0) == 0) {
            size_t p = 6;
            while (p < trimmedCode.length() && trimmedCode[p] == L' ') p++;
            size_t clStart = p;
            while (p < trimmedCode.length() && isIdentChar(trimmedCode[p])) p++;
            if (p > clStart) {
                definedSymbols.insert(trimmedCode.substr(clStart, p - clStart));
            }
            continue;
        }

        if (trimmedCode.rfind(L"for ", 0) == 0 || trimmedCode.rfind(L"async for ", 0) == 0) {
            size_t forOffset = (trimmedCode.rfind(L"async for ", 0) == 0) ? 10 : 4;
            size_t inPos = trimmedCode.find(L" in ", forOffset);
            if (inPos != std::wstring::npos) {
                std::wstring varsPart = trimmedCode.substr(forOffset, inPos - forOffset);
                size_t vp = 0;
                while (vp < varsPart.length()) {
                    while (vp < varsPart.length() && !isIdentStart(varsPart[vp])) vp++;
                    if (vp >= varsPart.length()) break;
                    size_t vStart = vp;
                    while (vp < varsPart.length() && isIdentChar(varsPart[vp])) vp++;
                    std::wstring vName = varsPart.substr(vStart, vp - vStart);
                    if (!vName.empty() && pyKeywords.find(vName) == pyKeywords.end()) {
                        definedSymbols.insert(vName);
                    }
                }
            }
            continue;
        }

        size_t asPos = trimmedCode.find(L" as ");
        if (asPos != std::wstring::npos && (trimmedCode.rfind(L"with ", 0) == 0 || trimmedCode.rfind(L"async with ", 0) == 0 || trimmedCode.rfind(L"except ", 0) == 0)) {
            size_t p = asPos + 4;
            while (p < trimmedCode.length() && trimmedCode[p] == L' ') p++;
            size_t varStart = p;
            while (p < trimmedCode.length() && isIdentChar(trimmedCode[p])) p++;
            if (p > varStart) {
                std::wstring asVar = trimmedCode.substr(varStart, p - varStart);
                if (!asVar.empty() && pyKeywords.find(asVar) == pyKeywords.end()) {
                    definedSymbols.insert(asVar);
                }
            }
        }

        size_t eqPos = std::wstring::npos;
        int bDepth = 0;
        for (size_t i = 0; i < code.length(); ++i) {
            wchar_t c = code[i];
            if (c == L'(' || c == L'[' || c == L'{') bDepth++;
            else if (c == L')' || c == L']' || c == L'}') { if (bDepth > 0) bDepth--; }
            else if (bDepth == 0 && c == L'=') {
                if (i + 1 < code.length() && code[i + 1] == L'=') { i++; continue; }
                if (i > 0 && (code[i - 1] == L'!' || code[i - 1] == L'<' || code[i - 1] == L'>')) continue;
                eqPos = i;
                break;
            }
        }
        if (eqPos != std::wstring::npos) {
            std::wstring lhs = code.substr(0, eqPos);
            while (!lhs.empty() && (lhs.back() == L'+' || lhs.back() == L'-' || lhs.back() == L'*' || lhs.back() == L'/' || lhs.back() == L':' || iswspace(lhs.back()))) {
                lhs.pop_back();
            }
            size_t lp = 0;
            while (lp < lhs.length()) {
                while (lp < lhs.length() && !isIdentStart(lhs[lp])) lp++;
                if (lp >= lhs.length()) break;
                bool isAttr = (lp > 0 && lhs[lp - 1] == L'.');
                size_t idStart = lp;
                while (lp < lhs.length() && isIdentChar(lhs[lp])) lp++;
                std::wstring idName = lhs.substr(idStart, lp - idStart);
                if (!isAttr && !idName.empty() && pyKeywords.find(idName) == pyKeywords.end()) {
                    definedSymbols.insert(idName);
                }
            }
        }
    }

    // Pass 2: Detect undefined names and typos
    if (!hasStarImport) {
        bool inTripleStr = false;
        wchar_t tChar = 0;
        for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
            const auto& line = buffer.GetLine(l);
            if (!inTripleStr) {
                size_t ls = 0;
                while (ls < line.length() && (line[ls] == L' ' || line[ls] == L'\t')) ls++;
                std::wstring trimmedLine = line.substr(ls);
                if (trimmedLine.rfind(L"import ", 0) == 0 || trimmedLine == L"import" ||
                    trimmedLine.rfind(L"from ", 0) == 0 || trimmedLine == L"from") {
                    continue;
                }
            }
            bool inSingleStr = false;
            wchar_t sChar = 0;
            size_t c = 0;
            int parenDepth = 0;

            while (c < line.length()) {
                wchar_t ch = line[c];

                if (inTripleStr) {
                    if (ch == tChar && c + 2 < line.length() && line[c + 1] == tChar && line[c + 2] == tChar) {
                        inTripleStr = false;
                        c += 3;
                    } else {
                        c++;
                    }
                    continue;
                }

                if (inSingleStr) {
                    if (ch == L'\\') { c += 2; continue; }
                    if (ch == sChar) inSingleStr = false;
                    c++;
                    continue;
                }

                if ((ch == L'"' || ch == L'\'') && c + 2 < line.length() && line[c + 1] == ch && line[c + 2] == ch) {
                    inTripleStr = true;
                    tChar = ch;
                    c += 3;
                    continue;
                }
                if (ch == L'"' || ch == L'\'') {
                    inSingleStr = true;
                    sChar = ch;
                    c++;
                    continue;
                }

                if (ch == L'#') {
                    break;
                }

                if (ch == L'(' || ch == L'[' || ch == L'{') { parenDepth++; c++; continue; }
                if (ch == L')' || ch == L']' || ch == L'}') { if (parenDepth > 0) parenDepth--; c++; continue; }

                if (isIdentStart(ch)) {
                    bool isAttr = false;
                    size_t backP = c;
                    while (backP > 0 && (line[backP - 1] == L' ' || line[backP - 1] == L'\t')) backP--;
                    if (backP > 0 && line[backP - 1] == L'.') {
                        isAttr = true;
                    }

                    size_t tokenStart = c;
                    while (c < line.length() && isIdentChar(line[c])) c++;
                    std::wstring sym = line.substr(tokenStart, c - tokenStart);

                    if (isAttr) {
                        continue;
                    }

                    if (parenDepth > 0) {
                        size_t nextP = c;
                        while (nextP < line.length() && (line[nextP] == L' ' || line[nextP] == L'\t')) nextP++;
                        if (nextP < line.length() && line[nextP] == L'=' && (nextP + 1 >= line.length() || line[nextP + 1] != L'=')) {
                            continue;
                        }
                    }

                    if (parenDepth == 0) {
                        size_t nextP = c;
                        while (nextP < line.length() && (line[nextP] == L' ' || line[nextP] == L'\t')) nextP++;
                        if (nextP < line.length() && line[nextP] == L'=' && (nextP + 1 >= line.length() || line[nextP + 1] != L'=')) {
                            continue;
                        }
                        if (nextP + 1 < line.length() && (line[nextP] == L'+' || line[nextP] == L'-' || line[nextP] == L'*' || line[nextP] == L'/') && line[nextP + 1] == L'=') {
                            continue;
                        }
                    }

                    if (pyKeywords.find(sym) != pyKeywords.end() ||
                        pyBuiltins.find(sym) != pyBuiltins.end() ||
                        definedSymbols.find(sym) != definedSymbols.end()) {
                        continue;
                    }

                    std::wstring bestMatch;
                    size_t bestDist = 3;

                    for (const auto& candidate : pyBuiltins) {
                        size_t d = CalculateLevenshtein(sym, candidate);
                        if (d < bestDist) {
                            bestDist = d;
                            bestMatch = candidate;
                        }
                    }
                    for (const auto& candidate : definedSymbols) {
                        size_t d = CalculateLevenshtein(sym, candidate);
                        if (d < bestDist) {
                            bestDist = d;
                            bestMatch = candidate;
                        }
                    }

                    std::wstring msg = L"Python: NameError: name '" + sym + L"' is not defined";
                    if (!bestMatch.empty() && bestDist <= 2) {
                        msg += L". Did you mean: '" + bestMatch + L"'?";
                    }

                    diagnostics.push_back({ l, tokenStart, sym.length(), msg, 1 });
                    continue;
                }

                c++;
            }
        }
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidatePhp(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct BracketInfo {
        wchar_t ch;
        size_t line;
        size_t col;
    };
    std::vector<BracketInfo> bracketStack;

    bool inPhp = false;
    bool inBlockComment = false;
    size_t blockCommentLine = 0;
    size_t blockCommentCol = 0;

    // In PHP, single-quoted and double-quoted string literals validly span multiple lines!
    bool inDoubleQuote = false;
    bool inSingleQuote = false;
    size_t quoteStartLine = 0;
    size_t quoteStartCol = 0;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);

        for (size_t c = 0; c < line.length(); ++c) {
            wchar_t ch = line[c];

            // Check PHP open/close tags
            if (!inPhp) {
                if (c + 4 < line.length() && line.substr(c, 5) == L"<?php") {
                    inPhp = true;
                    c += 4;
                    continue;
                } else if (c + 2 < line.length() && line.substr(c, 3) == L"<?=") {
                    inPhp = true;
                    c += 2;
                    continue;
                }
                continue; // Skip HTML outside PHP tags
            } else {
                if (!inDoubleQuote && !inSingleQuote && !inBlockComment) {
                    if (c + 1 < line.length() && line.substr(c, 2) == L"?>") {
                        inPhp = false;
                        c += 1;
                        continue;
                    }
                }
            }

            // Inside multi-line comment /* ... */
            if (inBlockComment) {
                if (ch == L'*' && c + 1 < line.length() && line[c + 1] == L'/') {
                    inBlockComment = false;
                    c++;
                }
                continue;
            }

            // Inside string literals (can span multiple lines in PHP)
            if (inDoubleQuote) {
                if (ch == L'\\') { c++; continue; }
                if (ch == L'"') { inDoubleQuote = false; }
                continue;
            }
            if (inSingleQuote) {
                if (ch == L'\\') { c++; continue; }
                if (ch == L'\'') { inSingleQuote = false; }
                continue;
            }

            // Single line comments // or # (ignoring #[...] PHP 8 attributes)
            if ((ch == L'/' && c + 1 < line.length() && line[c + 1] == L'/') ||
                (ch == L'#' && !(c + 1 < line.length() && line[c + 1] == L'['))) {
                break;
            }

            // Block comment /*
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
                quoteStartLine = l;
                quoteStartCol = c;
                continue;
            }
            if (ch == L'\'') {
                inSingleQuote = true;
                quoteStartLine = l;
                quoteStartCol = c;
                continue;
            }

            // Brackets
            if (ch == L'{' || ch == L'(' || ch == L'[') {
                bracketStack.push_back({ ch, l, c });
            } else if (ch == L'}' || ch == L')' || ch == L']') {
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, std::wstring(L"PHP: Unmatched closing '") + ch + L"'", 1 });
                } else {
                    wchar_t openCh = bracketStack.back().ch;
                    bool match = (openCh == L'{' && ch == L'}') ||
                                 (openCh == L'(' && ch == L')') ||
                                 (openCh == L'[' && ch == L']');
                    if (match) {
                        bracketStack.pop_back();
                    } else {
                        diagnostics.push_back({ l, c, 1, std::wstring(L"PHP: Mismatched bracket: expected '") +
                            (openCh == L'{' ? L'}' : (openCh == L'(' ? L')' : L']')) +
                            L"', found '" + ch + L"'", 1 });
                        bracketStack.pop_back();
                    }
                }
            }
        }
    }

    if (inDoubleQuote || inSingleQuote) {
        const auto& qLine = buffer.GetLine(quoteStartLine);
        size_t len = (qLine.length() > quoteStartCol) ? (qLine.length() - quoteStartCol) : 1;
        diagnostics.push_back({ quoteStartLine, quoteStartCol, len, L"PHP: Unclosed string literal", 1 });
    }

    if (inBlockComment) {
        diagnostics.push_back({ blockCommentLine, blockCommentCol, 2, L"PHP: Unclosed block comment '/*'", 1 });
    }

    for (const auto& b : bracketStack) {
        diagnostics.push_back({ b.line, b.col, 1, std::wstring(L"PHP: Unclosed bracket '") + b.ch + L"'", 1 });
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateReactAndESLint(const Buffer& buffer, bool isJsx) {
    std::vector<lsp::Diagnostic> diagnostics;
    size_t lineCount = buffer.GetLineCount();
    if (lineCount == 0) return diagnostics;

    static const std::unordered_set<std::wstring> reactHooks = {
        L"useState", L"useEffect", L"useContext", L"useReducer",
        L"useCallback", L"useMemo", L"useRef", L"useImperativeHandle",
        L"useLayoutEffect", L"useId", L"useDeferredValue",
        L"useTransition", L"useSyncExternalStore", L"useInsertionEffect"
    };

    int conditionDepth = 0;
    int loopDepth = 0;
    std::vector<int> controlStack; // 1 = condition (if/else/switch), 2 = loop (for/while)

    struct ComponentState {
        std::wstring name;
        size_t declLine = 0;
        int braceDepth = 0;
        int parenDepth = 0;
        bool hasOpenedBody = false;
        bool hasReturn = false;
        bool hasReturnedValue = false;
    };
    std::vector<ComponentState> componentStack;

    struct JsxTagInfo {
        std::wstring name;
        size_t line;
        size_t col;
    };
    std::vector<JsxTagInfo> jsxStack;

    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"meta", L"param", L"source", L"track", L"wbr"
    };

    bool inBacktickAcrossLines = false;

    for (size_t l = 0; l < lineCount; ++l) {
        const std::wstring& line = buffer.GetLine(l);
        if (line.empty()) continue;

        // Skip line comments, being careful to ignore // inside strings (e.g. "https://...", 'http://...', `http://...`)
        bool inSingle = false;
        bool inDouble = false;
        bool inBacktick = inBacktickAcrossLines;
        size_t lineComment = std::wstring::npos;
        for (size_t i = 0; i < line.length(); ++i) {
            wchar_t ch = line[i];
            if (ch == L'\\' && (inSingle || inDouble || inBacktick)) {
                i++; // skip escaped character
                continue;
            }
            if (ch == L'\'' && !inDouble && !inBacktick) {
                inSingle = !inSingle;
            } else if (ch == L'"' && !inSingle && !inBacktick) {
                inDouble = !inDouble;
            } else if (ch == L'`' && !inSingle && !inDouble) {
                inBacktick = !inBacktick;
            } else if (!inSingle && !inDouble && !inBacktick) {
                if (ch == L'/' && IsJsRegexStart(line, i)) {
                    size_t r = ScanJsRegexEnd(line, i);
                    if (r != std::wstring::npos) {
                        i = r;
                        continue;
                    }
                }
                if (ch == L'/' && i + 1 < line.length() && line[i + 1] == L'/') {
                    if (i > 0 && line[i - 1] == L':') {
                        continue; // Part of a URL like https:// or http://, NOT a line comment
                    }
                    if (line.find(L"</", i + 2) != std::wstring::npos) {
                        continue; // Inside JSX text followed by closing tag (e.g. <span>// comment</span>), NOT a line comment
                    }
                    lineComment = i;
                    break;
                }
            }
        }
        std::wstring code = (lineComment != std::wstring::npos) ? line.substr(0, lineComment) : line;

        // Mask string literals, regex literals, and comments so strings containing '{', '}', 'if', etc. do not trigger false positives
        std::wstring cleanCode = code;
        bool inS = false, inD = false, inB = inBacktickAcrossLines;
        for (size_t i = 0; i < cleanCode.length(); ++i) {
            wchar_t ch = cleanCode[i];
            if (ch == L'\\' && (inS || inD || inB)) {
                cleanCode[i] = L' ';
                if (i + 1 < cleanCode.length()) { cleanCode[i + 1] = L' '; i++; }
                continue;
            }
            if (!inS && !inD && !inB && ch == L'/' && IsJsRegexStart(cleanCode, i)) {
                size_t r = ScanJsRegexEnd(cleanCode, i);
                if (r != std::wstring::npos) {
                    for (size_t k = i; k <= r; ++k) cleanCode[k] = L' ';
                    i = r;
                    continue;
                }
            }
            if (ch == L'\'' && !inD && !inB) { inS = !inS; cleanCode[i] = L' '; continue; }
            if (ch == L'"' && !inS && !inB) { inD = !inD; cleanCode[i] = L' '; continue; }
            if (ch == L'`' && !inS && !inD) { inB = !inB; cleanCode[i] = L' '; continue; }
            if (inS || inD || inB) {
                cleanCode[i] = L' ';
            }
        }
        inBacktickAcrossLines = inB;

        auto findControlKeyword = [](const std::wstring& str) -> int {
            auto isWord = [&](size_t idx, const std::wstring& kw) -> bool {
                if (idx + kw.length() > str.length()) return false;
                if (str.substr(idx, kw.length()) != kw) return false;
                bool bOk = (idx == 0 || (!iswalnum(str[idx - 1]) && str[idx - 1] != L'_' && str[idx - 1] != L'$'));
                size_t aIdx = idx + kw.length();
                bool aOk = (aIdx >= str.length() || (!iswalnum(str[aIdx]) && str[aIdx] != L'_' && str[aIdx] != L'$'));
                return bOk && aOk;
            };
            for (int p = static_cast<int>(str.length()) - 1; p >= 0; --p) {
                if (isWord(p, L"if") || isWord(p, L"else") || isWord(p, L"switch")) return 1;
                if (isWord(p, L"for") || isWord(p, L"while")) return 2;
                if (str[p] == L';' || isWord(p, L"function") || isWord(p, L"return")) return 0;
            }
            return 0;
        };

        // 0. Component Declaration Detection: accurate identification of function Comp() or const Comp = ...
        size_t fnPos = cleanCode.find(L"function ");
        if (fnPos != std::wstring::npos) {
            size_t nameStart = fnPos + 9;
            while (nameStart < cleanCode.length() && iswspace(cleanCode[nameStart])) nameStart++;
            size_t nameEnd = nameStart;
            while (nameEnd < cleanCode.length() && (iswalnum(cleanCode[nameEnd]) || cleanCode[nameEnd] == L'_' || cleanCode[nameEnd] == L'$')) nameEnd++;
            if (nameEnd > nameStart) {
                std::wstring compName = cleanCode.substr(nameStart, nameEnd - nameStart);
                if (iswupper(compName[0]) && compName != L"React" && compName != L"Component" && compName != L"PureComponent" && compName != L"Fragment") {
                    componentStack.push_back({ compName, l, 0, 0, false, false, false });
                }
            }
        } else if ((cleanCode.find(L"const ") != std::wstring::npos || cleanCode.find(L"let ") != std::wstring::npos || cleanCode.find(L"export ") != std::wstring::npos) &&
                   cleanCode.find(L"=>") != std::wstring::npos) {
            size_t varPos = cleanCode.find(L"const ");
            if (varPos == std::wstring::npos) varPos = cleanCode.find(L"let ");
            if (varPos != std::wstring::npos) {
                size_t nameStart = varPos + (cleanCode.substr(varPos, 4) == L"let " ? 4 : 6);
                while (nameStart < cleanCode.length() && iswspace(cleanCode[nameStart])) nameStart++;
                size_t nameEnd = nameStart;
                while (nameEnd < cleanCode.length() && (iswalnum(cleanCode[nameEnd]) || cleanCode[nameEnd] == L'_' || cleanCode[nameEnd] == L'$')) nameEnd++;
                if (nameEnd > nameStart) {
                    std::wstring compName = cleanCode.substr(nameStart, nameEnd - nameStart);
                    if (iswupper(compName[0]) && compName != L"React" && compName != L"Component" && compName != L"PureComponent" && compName != L"Fragment") {
                        componentStack.push_back({ compName, l, 0, 0, false, false, false });
                    }
                }
            }
        }

        // Track conditions, loops, and component scopes
        for (size_t i = 0; i < cleanCode.length(); ++i) {
            wchar_t cChar = cleanCode[i];
            if (cChar == L'(') {
                if (!componentStack.empty() && !componentStack.back().hasOpenedBody) {
                    componentStack.back().parenDepth++;
                }
            } else if (cChar == L')') {
                if (!componentStack.empty() && !componentStack.back().hasOpenedBody) {
                    if (componentStack.back().parenDepth > 0) componentStack.back().parenDepth--;
                }
            } else if (cChar == L'{') {
                std::wstring prefix = cleanCode.substr(0, i);
                int type = findControlKeyword(prefix);
                controlStack.push_back(type);
                if (type == 1) conditionDepth++;
                else if (type == 2) loopDepth++;
                if (!componentStack.empty()) {
                    auto& comp = componentStack.back();
                    if (!comp.hasOpenedBody) {
                        if (comp.parenDepth == 0) {
                            comp.hasOpenedBody = true;
                            comp.braceDepth = 1;
                        }
                    } else {
                        comp.braceDepth++;
                    }
                }
            } else if (cChar == L'}') {
                if (!controlStack.empty()) {
                    int topType = controlStack.back();
                    controlStack.pop_back();
                    if (topType == 1 && conditionDepth > 0) conditionDepth--;
                    else if (topType == 2 && loopDepth > 0) loopDepth--;
                }
                if (!componentStack.empty()) {
                    auto& comp = componentStack.back();
                    if (comp.hasOpenedBody) {
                        comp.braceDepth--;
                        if (comp.braceDepth <= 0) {
                            auto finishedComp = comp;
                            componentStack.pop_back();
                            if (!finishedComp.hasReturn || !finishedComp.hasReturnedValue) {
                                lsp::Diagnostic diag;
                                diag.line = l;
                                diag.col = i;
                                diag.length = 1;
                                diag.message = L"Component '" + finishedComp.name + L"' must return a valid React element, JSX, or null. (react/require-render-return)";
                                diag.severity = 1; // Error
                                diagnostics.push_back(std::move(diag));
                            }
                        }
                    }
                }
            }
        }

        // Return Statement Analysis: check for missing return expression in components & missing semicolons
        size_t retPos = 0;
        while ((retPos = cleanCode.find(L"return", retPos)) != std::wstring::npos) {
            bool validBefore = (retPos == 0 || (!iswalnum(cleanCode[retPos - 1]) && cleanCode[retPos - 1] != L'_' && cleanCode[retPos - 1] != L'$'));
            size_t afterIdx = retPos + 6;
            bool validAfter = (afterIdx >= cleanCode.length() || (!iswalnum(cleanCode[afterIdx]) && cleanCode[afterIdx] != L'_' && cleanCode[afterIdx] != L'$'));

            if (validBefore && validAfter) {
                if (!componentStack.empty()) {
                    componentStack.back().hasReturn = true;
                }

                std::wstring afterReturn = cleanCode.substr(afterIdx);
                size_t s = 0;
                while (s < afterReturn.length() && (afterReturn[s] == L' ' || afterReturn[s] == L'	')) s++;
                afterReturn = afterReturn.substr(s);

                size_t lastNonWs = code.find_last_not_of(L" \t\r\n");
                bool hasSemi = (lastNonWs != std::wstring::npos && code[lastNonWs] == L';');
                if (!afterReturn.empty() && afterReturn.back() == L';') {
                    hasSemi = true;
                    afterReturn.pop_back();
                    while (!afterReturn.empty() && (afterReturn.back() == L' ' || afterReturn.back() == L'	')) afterReturn.pop_back();
                }

                bool hasValOnSameLine = !afterReturn.empty();
                bool multilineContinuation = afterReturn.ends_with(L"(") || afterReturn.ends_with(L"{") || afterReturn.ends_with(L"[");

                bool hasValOnNextLine = false;
                if (!hasValOnSameLine) {
                    for (size_t nl = l + 1; nl < lineCount; ++nl) {
                        const std::wstring& nLine = buffer.GetLine(nl);
                        size_t ns = 0;
                        while (ns < nLine.length() && (nLine[ns] == L' ' || nLine[ns] == L'	')) ns++;
                        if (ns >= nLine.length()) continue;
                        if (nLine.substr(ns, 2) == L"//" || nLine.substr(ns, 2) == L"/*") continue;
                        if (nLine[ns] == L'}' || nLine[ns] == L')') {
                            hasValOnNextLine = false;
                        } else {
                            hasValOnNextLine = true;
                            multilineContinuation = true;
                        }
                        break;
                    }
                }

                if (hasValOnSameLine || hasValOnNextLine) {
                    if (!componentStack.empty()) {
                        componentStack.back().hasReturnedValue = true;
                    }
                } else {
                    std::wstring compName = componentStack.empty() ? L"Component" : componentStack.back().name;
                    lsp::Diagnostic diag;
                    diag.line = l;
                    diag.col = retPos;
                    diag.length = 6;
                    diag.message = L"Component '" + compName + L"' must return a valid React element, JSX, or null. (react/require-render-return)";
                    diag.severity = 1; // Error
                    diagnostics.push_back(std::move(diag));

                    if (!hasSemi) {
                        lsp::Diagnostic semiDiag;
                        semiDiag.line = l;
                        semiDiag.col = retPos + 6;
                        semiDiag.length = 1;
                        semiDiag.message = L"Missing semicolon. (semi)";
                        semiDiag.severity = 2; // Warning
                        diagnostics.push_back(std::move(semiDiag));
                    }
                }

                if (hasValOnSameLine && !hasSemi && !multilineContinuation) {
                    lsp::Diagnostic semiDiag;
                    semiDiag.line = l;
                    semiDiag.col = cleanCode.length();
                    semiDiag.length = 1;
                    semiDiag.message = L"Missing semicolon. (semi)";
                    semiDiag.severity = 2; // Warning
                    diagnostics.push_back(std::move(semiDiag));
                }
            }
            retPos += 6;
        }

        // 1. React Hook Rules: Cannot call hooks conditionally or inside loops
        for (const auto& hook : reactHooks) {
            size_t pos = 0;
            while ((pos = code.find(hook, pos)) != std::wstring::npos) {
                bool validBefore = (pos == 0 || (!iswalnum(code[pos - 1]) && code[pos - 1] != L'_' && code[pos - 1] != L'$'));
                size_t afterIdx = pos + hook.length();
                bool validAfter = (afterIdx >= code.length() || (!iswalnum(code[afterIdx]) && code[afterIdx] != L'_' && code[afterIdx] != L'$'));

                if (validBefore && validAfter) {
                    if (conditionDepth > 0 || loopDepth > 0) {
                        lsp::Diagnostic diag;
                        diag.line = l;
                        diag.col = pos;
                        diag.length = hook.length();
                        diag.message = L"React Hook \"" + hook + L"\" is called conditionally. React Hooks must be called in the exact same order in every component render. (react-hooks/rules-of-hooks)";
                        diag.severity = 1; // Error
                        diagnostics.push_back(std::move(diag));
                    }

                    if (hook == L"useEffect" || hook == L"useCallback" || hook == L"useMemo") {
                        size_t callParen = code.find(L'(', afterIdx);
                        if (callParen != std::wstring::npos) {
                            if (hook == L"useEffect" || hook == L"useCallback" || hook == L"useMemo") {
                                // Multi-line scanner tracking parenDepth from callParen
                                int parenDepth = 1;
                                int curBraceDepth = 0;
                                bool hasCommaAtTopLevel = false;
                                bool inQ = false;
                                wchar_t qC = 0;

                                size_t scanL = l;
                                size_t scanC = callParen + 1;
                                while (scanL < buffer.GetLineCount() && scanL - l <= 60) {
                                    const std::wstring& sLine = buffer.GetLine(scanL);
                                    for (; scanC < sLine.length(); ++scanC) {
                                        wchar_t sc = sLine[scanC];
                                        if (inQ) {
                                            if (sc == L'\\' && scanC + 1 < sLine.length()) {
                                                scanC++;
                                            } else if (sc == qC) {
                                                inQ = false;
                                            }
                                        } else {
                                            if (sc == L'"' || sc == L'\'' || sc == L'`') {
                                                inQ = true;
                                                qC = sc;
                                            } else if (sc == L'(') {
                                                parenDepth++;
                                            } else if (sc == L')') {
                                                parenDepth--;
                                                if (parenDepth == 0) break;
                                            } else if (sc == L'{') {
                                                curBraceDepth++;
                                            } else if (sc == L'}') {
                                                if (curBraceDepth > 0) curBraceDepth--;
                                            } else if (sc == L',' && parenDepth == 1 && curBraceDepth == 0) {
                                                hasCommaAtTopLevel = true;
                                                break;
                                            }
                                        }
                                    }
                                    if (hasCommaAtTopLevel || parenDepth == 0) break;
                                    scanL++;
                                    scanC = 0;
                                }

                                if (parenDepth == 0 && !hasCommaAtTopLevel) {
                                    lsp::Diagnostic diag;
                                    diag.line = l;
                                    diag.col = pos;
                                    diag.length = hook.length();
                                    diag.message = L"React Hook " + hook + L" has no dependency array. It will re-run on every render. (react-hooks/exhaustive-deps)";
                                    diag.severity = 3; // Information
                                    diagnostics.push_back(std::move(diag));
                                }
                            }
                        }
                    }
                }
                pos += hook.length();
            }
        }

        // 2. Direct State Mutation: this.state.x = ...
        size_t statePos = code.find(L"this.state.");
        if (statePos != std::wstring::npos) {
            size_t eqPos = code.find(L'=', statePos + 11);
            if (eqPos != std::wstring::npos && (eqPos + 1 >= code.length() || code[eqPos + 1] != L'=')) {
                lsp::Diagnostic diag;
                diag.line = l;
                diag.col = statePos;
                diag.length = (eqPos - statePos + 1);
                diag.message = L"Do not mutate state directly. Use setState(). (react/no-direct-mutation-state)";
                diag.severity = 1; // Error
                diagnostics.push_back(std::move(diag));
            }
        }

        // 3. JSX Validation: duplicate props and tag balancing
        if (isJsx) {
            static const std::unordered_set<std::wstring> tsTypeKeywords = {
                L"any", L"string", L"number", L"boolean", L"unknown", L"never",
                L"void", L"null", L"undefined", L"object", L"symbol", L"bigint",
                L"typeof", L"keyof", L"readonly", L"infer", L"unique",
                L"T", L"K", L"V", L"U", L"P", L"R", L"E"
            };

            size_t p = 0;
            int jsxCurlyDepth = 0;
            while (p < code.length()) {
                // If we are outside JSX tags (jsxStack is empty) or inside a JSX expression `{ ... }`,
                // quotes ("...", '...', `...`) delimit string literals and /.../ delimits regex literals. Skip them completely so that
                // characters like <, >, <=, >=, </, and <> inside strings or regexes are NEVER treated as JSX tags.
                if (jsxStack.empty() || jsxCurlyDepth > 0) {
                    if (code[p] == L'{') {
                        jsxCurlyDepth++;
                        p++;
                        continue;
                    }
                    if (code[p] == L'}') {
                        if (jsxCurlyDepth > 0) jsxCurlyDepth--;
                        p++;
                        continue;
                    }
                    if (code[p] == L'/') {
                        if (IsJsRegexStart(code, p)) {
                            size_t r = ScanJsRegexEnd(code, p);
                            if (r != std::wstring::npos) {
                                p = r + 1;
                                continue;
                            }
                        }
                    }
                    if (code[p] == L'"' || code[p] == L'\'' || code[p] == L'`') {
                        wchar_t q = code[p++];
                        while (p < code.length()) {
                            if (code[p] == L'\\' && p + 1 < code.length()) {
                                p += 2;
                            } else if (code[p] == q) {
                                p++;
                                break;
                            } else {
                                p++;
                            }
                        }
                        continue;
                    }
                } else {
                    if (code[p] == L'{') {
                        jsxCurlyDepth++;
                        p++;
                        continue;
                    }
                }

                if (code[p] == L'<') {
                    // Double check: if cleanCode masked this position (i.e. inside quote or comment), skip it!
                    if (p < cleanCode.length() && cleanCode[p] == L' ' && code[p] != L' ') {
                        p++;
                        continue;
                    }
                    // In JS/TSX, an opening JSX tag outside JSX children cannot immediately follow an identifier or dollar sign without whitespace.
                    // e.g. `useRef<...>`, `ReturnType<...>`, `LayoutProps<...>`, `KeyboardEvent<...>` are generics!
                    // But inside JSX children, text can precede a tag (e.g. `<span>Cleave<span className=...>DB</span></span>`).
                    if (jsxStack.empty() && p + 1 < code.length() && code[p + 1] != L'/' && p > 0 && (iswalnum(code[p - 1]) || code[p - 1] == L'_' || code[p - 1] == L'$')) {
                        p++;
                        continue;
                    }

                    if (p + 1 < code.length() && (code[p + 1] == L'/' || code[p + 1] == L'>' || iswalpha(code[p + 1]))) {
                        // Safely find the matching '>' across single-line or multi-line JSX tags
                        // without stopping on '>' inside string attributes or expressions
                        size_t tagEndLine = l;
                        size_t tagEndCol = std::wstring::npos;
                        bool abortTag = false;
                        bool inQuote = false;
                        wchar_t qChar = 0;
                        int braceDepth = 0;
                        std::wstring accumulatedTag = L"";

                        size_t scanLine = l;
                        size_t scanCol = p + 1;
                        const std::wstring* currLineStr = &code;

                        while (scanLine < buffer.GetLineCount()) {
                            for (size_t scan = scanCol; scan < currLineStr->length(); ++scan) {
                                wchar_t sc = (*currLineStr)[scan];
                                if (inQuote) {
                                    if (sc == L'\\' && scan + 1 < currLineStr->length()) {
                                        accumulatedTag += sc;
                                        scan++;
                                        if (scan < currLineStr->length()) accumulatedTag += (*currLineStr)[scan];
                                    } else if (sc == qChar) {
                                        inQuote = false;
                                        accumulatedTag += sc;
                                    } else {
                                        accumulatedTag += sc;
                                    }
                                } else {
                                    if (sc == L'/' && braceDepth > 0 && IsJsRegexStart(*currLineStr, scan)) {
                                        size_t r = ScanJsRegexEnd(*currLineStr, scan);
                                        if (r != std::wstring::npos) {
                                            for (size_t k = scan; k <= r; ++k) accumulatedTag += (*currLineStr)[k];
                                            scan = r;
                                            continue;
                                        }
                                    }
                                    if (sc == L'"' || sc == L'\'' || sc == L'`') {
                                        inQuote = true;
                                        qChar = sc;
                                        accumulatedTag += sc;
                                    } else if (sc == L'{') {
                                        braceDepth++;
                                        accumulatedTag += sc;
                                    } else if (sc == L'}') {
                                        if (braceDepth > 0) braceDepth--;
                                        accumulatedTag += sc;
                                    } else if (sc == L'>' && braceDepth == 0) {
                                        if (scan > 0 && (*currLineStr)[scan - 1] == L'=') {
                                            accumulatedTag += sc; // Arrow function '=>' inside attribute, not closing tag!
                                            continue;
                                        }
                                        tagEndLine = scanLine;
                                        tagEndCol = scan;
                                        break;
                                    } else if (sc == L'<' && braceDepth == 0) {
                                        // A new '<' outside of expression brackets means current tag was unclosed or broken!
                                        // Immediately abort scanning this tag so subsequent tags across lines are not swallowed
                                        abortTag = true;
                                        break;
                                    } else {
                                        accumulatedTag += sc;
                                    }
                                }
                            }

                            if (tagEndCol != std::wstring::npos || abortTag) {
                                break;
                            }

                            scanLine++;
                            if (scanLine < buffer.GetLineCount() && scanLine - l <= 25) {
                                currLineStr = &buffer.GetLine(scanLine);
                                scanCol = 0;
                                accumulatedTag += L" ";
                            } else {
                                break;
                            }
                        }

                        if (tagEndCol != std::wstring::npos) {
                            std::wstring tagStr = accumulatedTag;
                            size_t tagEnd = tagEndCol;
                            // Trim trailing whitespace from tagStr
                            while (!tagStr.empty() && iswspace(tagStr.back())) tagStr.pop_back();

                            bool isClose = (!tagStr.empty() && tagStr[0] == L'/');
                            bool isSelfClose = (!tagStr.empty() && tagStr.back() == L'/');

                            // A generic type parameter in a call cannot have JSX attributes (e.g. key=, className=, onClick=)
                            bool hasJsxAttrs = (tagStr.find(L'=') != std::wstring::npos);

                            // If tagStr contains type operators or generic syntax (; | & , { [ typeof keyof extends as) and has NO attributes, this is a TS generic type parameter
                            if (!isClose && !hasJsxAttrs && (tagStr.find(L';') != std::wstring::npos ||
                                                             tagStr.find(L'|') != std::wstring::npos ||
                                                             tagStr.find(L'&') != std::wstring::npos ||
                                                             tagStr.find(L',') != std::wstring::npos ||
                                                             tagStr.find(L'{') != std::wstring::npos ||
                                                             tagStr.find(L'[') != std::wstring::npos ||
                                                             tagStr.find(L"typeof ") != std::wstring::npos ||
                                                             tagStr.find(L"keyof ") != std::wstring::npos ||
                                                             tagStr.find(L" extends ") != std::wstring::npos ||
                                                             tagStr.find(L" as ") != std::wstring::npos)) {
                                if (tagEndLine > l) {
                                    l = tagEndLine;
                                    code = buffer.GetLine(l);
                                }
                                p = tagEnd + 1;
                                continue;
                            }

                            size_t nStart = isClose ? 1 : 0;
                            while (nStart < tagStr.length() && iswspace(tagStr[nStart])) nStart++;
                            size_t nEnd = nStart;
                            while (nEnd < tagStr.length() && !iswspace(tagStr[nEnd]) && tagStr[nEnd] != L'/') nEnd++;
                            std::wstring tagName = tagStr.substr(nStart, nEnd - nStart);
                            if (tagName.empty() || tagName == L"/") {
                                tagName = L"<>";
                            }

                            // Skip TypeScript primitive types (e.g. useRef<any>(null), useState<number>)
                            if (!isClose && !hasJsxAttrs && tsTypeKeywords.count(tagName) > 0) {
                                if (tagEndLine > l) {
                                    l = tagEndLine;
                                    code = buffer.GetLine(l);
                                }
                                p = tagEnd + 1;
                                continue;
                            }

                            // Check what follows '>'. If followed by '(', ')', ';', ',', '=', '.', or ':' it's a generic call or type annotation
                            if (!isClose && !hasJsxAttrs) {
                                size_t nextCharIdx = tagEnd + 1;
                                const std::wstring& targetLine = (tagEndLine > l) ? buffer.GetLine(tagEndLine) : code;
                                while (nextCharIdx < targetLine.length() && iswspace(targetLine[nextCharIdx])) nextCharIdx++;
                                if (nextCharIdx < targetLine.length() && (targetLine[nextCharIdx] == L'(' || targetLine[nextCharIdx] == L')' ||
                                                                    targetLine[nextCharIdx] == L';' || targetLine[nextCharIdx] == L',' ||
                                                                    targetLine[nextCharIdx] == L'=' || targetLine[nextCharIdx] == L'.' ||
                                                                    targetLine[nextCharIdx] == L':')) {
                                    if (tagEndLine > l) {
                                        l = tagEndLine;
                                        code = buffer.GetLine(l);
                                    }
                                    p = tagEnd + 1;
                                    continue;
                                }
                            }

                            if (!isClose && !isSelfClose && voidTags.count(tagName) == 0 && !tagName.empty()) {
                                jsxStack.push_back({ tagName, l, p });

                                std::unordered_set<std::wstring> propNames;
                                size_t aIdx = nEnd;
                                while (aIdx < tagStr.length()) {
                                    while (aIdx < tagStr.length() && iswspace(tagStr[aIdx])) aIdx++;
                                    if (aIdx >= tagStr.length() || tagStr[aIdx] == L'/' || tagStr[aIdx] == L'>') break;

                                    // Skip spread attributes: {...props}
                                    if (tagStr[aIdx] == L'{') {
                                        int depth = 1;
                                        aIdx++;
                                        bool inQ = false;
                                        wchar_t qC = 0;
                                        while (aIdx < tagStr.length() && depth > 0) {
                                            wchar_t ch = tagStr[aIdx];
                                            if (inQ) {
                                                if (ch == L'\\' && aIdx + 1 < tagStr.length()) aIdx++;
                                                else if (ch == qC) inQ = false;
                                            } else {
                                                if (ch == L'"' || ch == L'\'' || ch == L'`') { inQ = true; qC = ch; }
                                                else if (ch == L'{') depth++;
                                                else if (ch == L'}') depth--;
                                            }
                                            aIdx++;
                                        }
                                        continue;
                                    }

                                    if (!iswalpha(tagStr[aIdx]) && tagStr[aIdx] != L'_' && tagStr[aIdx] != L'$') {
                                        aIdx++;
                                        continue;
                                    }

                                    size_t nameStart = aIdx;
                                    while (aIdx < tagStr.length() && (iswalnum(tagStr[aIdx]) || tagStr[aIdx] == L'-' || tagStr[aIdx] == L':' || tagStr[aIdx] == L'_' || tagStr[aIdx] == L'$')) {
                                        aIdx++;
                                    }
                                    std::wstring propName = tagStr.substr(nameStart, aIdx - nameStart);

                                    while (aIdx < tagStr.length() && iswspace(tagStr[aIdx])) aIdx++;

                                    if (aIdx < tagStr.length() && tagStr[aIdx] == L'=') {
                                        aIdx++;
                                        while (aIdx < tagStr.length() && iswspace(tagStr[aIdx])) aIdx++;
                                        if (aIdx < tagStr.length()) {
                                            if (tagStr[aIdx] == L'"' || tagStr[aIdx] == L'\'' || tagStr[aIdx] == L'`') {
                                                wchar_t q = tagStr[aIdx++];
                                                while (aIdx < tagStr.length()) {
                                                    if (tagStr[aIdx] == L'\\' && aIdx + 1 < tagStr.length()) aIdx += 2;
                                                    else if (tagStr[aIdx] == q) { aIdx++; break; }
                                                    else aIdx++;
                                                }
                                            } else if (tagStr[aIdx] == L'{') {
                                                int depth = 1;
                                                aIdx++;
                                                bool inQ = false;
                                                wchar_t qC = 0;
                                                while (aIdx < tagStr.length() && depth > 0) {
                                                    wchar_t ch = tagStr[aIdx];
                                                    if (inQ) {
                                                        if (ch == L'\\' && aIdx + 1 < tagStr.length()) aIdx++;
                                                        else if (ch == qC) inQ = false;
                                                    } else {
                                                        if (ch == L'"' || ch == L'\'' || ch == L'`') { inQ = true; qC = ch; }
                                                        else if (ch == L'{') depth++;
                                                        else if (ch == L'}') depth--;
                                                    }
                                                    aIdx++;
                                                }
                                            }
                                        }
                                    }

                                    if (!propName.empty()) {
                                        if (propNames.count(propName) > 0) {
                                            lsp::Diagnostic diag;
                                            diag.line = l;
                                            diag.col = p;
                                            diag.length = tagEnd - p + 1;
                                            diag.message = L"No duplicate props allowed: \"" + propName + L"\" (react/jsx-no-duplicate-props)";
                                            diag.severity = 2; // Warning
                                            diagnostics.push_back(std::move(diag));
                                        } else {
                                            propNames.insert(propName);
                                        }
                                    }
                                }
                            } else if (isClose && !tagName.empty()) {
                                if (!jsxStack.empty()) {
                                    if (jsxStack.back().name == tagName || _wcsicmp(jsxStack.back().name.c_str(), tagName.c_str()) == 0) {
                                        jsxStack.pop_back();
                                    } else {
                                        // Look backward down jsxStack for a matching ancestor
                                        auto matchIt = jsxStack.end();
                                        for (auto it = jsxStack.rbegin(); it != jsxStack.rend(); ++it) {
                                            if (it->name == tagName || _wcsicmp(it->name.c_str(), tagName.c_str()) == 0) {
                                                matchIt = (it + 1).base(); // iterator pointing to matching item
                                                break;
                                            }
                                        }

                                        if (matchIt != jsxStack.end()) {
                                            // Every item above matchIt was never closed! Report them at their opening tag
                                            for (auto it = matchIt + 1; it != jsxStack.end(); ++it) {
                                                lsp::Diagnostic diag;
                                                diag.line = it->line;
                                                diag.col = it->col;
                                                diag.length = it->name.length() + 1;
                                                std::wstring disp = (it->name == L"<>" ? L"<>" : (L"<" + it->name + L">"));
                                                diag.message = L"JSX: Unclosed JSX element '" + disp + L"'";
                                                diag.severity = 1; // Error
                                                diagnostics.push_back(std::move(diag));
                                            }
                                            // Pop all items from matchIt through the end
                                            jsxStack.erase(matchIt, jsxStack.end());
                                        } else {
                                            // No matching opening tag exists anywhere in the stack; report unexpected closing tag without corrupting stack
                                            lsp::Diagnostic diag;
                                            diag.line = l;
                                            diag.col = p;
                                            diag.length = tagEnd - p + 1;
                                            std::wstring disp = (tagName == L"<>" ? L"</>" : (L"</" + tagName + L">"));
                                            diag.message = L"JSX: Unexpected closing tag '" + disp + L"', no matching opening tag found";
                                            diag.severity = 1; // Error
                                            diagnostics.push_back(std::move(diag));
                                        }
                                    }
                                } else {
                                    lsp::Diagnostic diag;
                                    diag.line = l;
                                    diag.col = p;
                                    diag.length = tagEnd - p + 1;
                                    std::wstring disp = (tagName == L"<>" ? L"</>" : (L"</" + tagName + L">"));
                                    diag.message = L"JSX: Unexpected closing tag '" + disp + L"', no matching opening tag found";
                                    diag.severity = 1; // Error
                                    diagnostics.push_back(std::move(diag));
                                }
                            }
                            if (tagEndLine > l) {
                                l = tagEndLine;
                                code = buffer.GetLine(l);
                                p = tagEnd + 1;
                                continue;
                            } else {
                                p = tagEnd + 1;
                                continue;
                            }
                        }
                    }
                }
                p++;
            }
        }
    }

    for (const auto& tag : jsxStack) {
        lsp::Diagnostic diag;
        diag.line = tag.line;
        diag.col = tag.col;
        diag.length = tag.name.length() + 1;
        std::wstring disp = (tag.name == L"<>" ? L"<>" : (L"<" + tag.name + L">"));
        diag.message = L"JSX: Unclosed JSX element '" + disp + L"'";
        diag.severity = 1; // Error
        diagnostics.push_back(std::move(diag));
    }

    return diagnostics;
}

} // namespace phide::editor
