/**
 * @file EmmetEngine.hpp
 * @brief High-performance native Emmet abbreviation parser and multi-line structured code expander.
 * Supports standard Emmet HTML/XML syntax, hierarchy, multipliers, numbering, attributes,
 * text nodes, lorem ipsum, CSS property abbreviations, and Tab-to-Expand.
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace phide::editor {

class EmmetEngine {
public:
    /**
     * @brief Checks if a character string looks like an Emmet abbreviation.
     */
    static bool IsEmmetAbbreviation(const std::wstring& abbr, bool isCss = false);

    /**
     * @brief Extracts an Emmet abbreviation string from line ending at cursorCol.
     * @param line Text line content.
     * @param cursorCol Cursor column index (1-based or 0-based boundary).
     * @param isCss True if document or context is CSS stylesheet.
     * @param pStartCol Optional output for the starting column of the abbreviation.
     * @return Extracted abbreviation string, or empty string if none.
     */
    static std::wstring ExtractAbbreviation(
        const std::wstring& line,
        size_t cursorCol,
        bool isCss = false,
        size_t* pStartCol = nullptr
    );

    /**
     * @brief Expands an Emmet abbreviation into fully structured multi-line HTML or CSS code.
     * @param abbr Raw abbreviation (e.g. "div.container>ul>li*4>a" or "div.hello" or "m10").
     * @param indentUnit Indentation unit for nested levels (e.g. "    " or "\t").
     * @param lineIndent Existing leading whitespace of the current line to preserve alignment.
     * @param isCss True if in CSS context.
     * @param pCursorOffset Output for relative offset in result where caret should be positioned.
     * @return Expanded code string, or empty string if invalid.
     */
    static std::wstring Expand(
        const std::wstring& abbr,
        const std::wstring& indentUnit = L"    ",
        const std::wstring& lineIndent = L"",
        bool isCss = false,
        size_t* pCursorOffset = nullptr
    );

    /**
     * @brief Checks if a tag is a self-closing/void HTML element.
     */
    static bool IsVoidTag(const std::wstring& tag);

    /**
     * @brief Resolves default tag name based on parent context (e.g. inside <ul> -> <li>).
     */
    static std::wstring ResolveDefaultTag(const std::wstring& parentTag);

    /**
     * @brief Expands CSS property abbreviations (e.g. "m10" -> "margin: 10px;").
     */
    static std::wstring ExpandCss(const std::wstring& abbr);

    /**
     * @brief Generates standard Latin lorem ipsum filler text.
     */
    static std::wstring GenerateLorem(size_t wordCount = 30);
};

} // namespace phide::editor
