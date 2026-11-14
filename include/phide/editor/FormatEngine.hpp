/**
 * @file FormatEngine.hpp
 * @brief Native offline code formatter for HTML, CSS, JavaScript/TypeScript/JSX, JSON, and PHP.
 *        Zero external LLM or network dependencies. PSR-12 and W3C compliant formatting.
 */

#pragma once

#include <string>
#include <vector>

namespace phide::editor {

class FormatEngine {
public:
    /**
     * @brief Formats raw document source code based on its file extension.
     * @param src Unformatted source code string.
     * @param ext File extension including leading dot (e.g. L".php", L".html", L".css", L".json", L".js").
     * @param tabSize Number of spaces per indentation level (default: 4).
     * @param filePath Optional real file path so Prettier/Clang-Format/Ruff discover local project configs.
     * @param insertSpaces True for space indentation, false for tab indentation when no local config overrides it.
     * @return Formatted code string.
     */
    static std::wstring FormatDocument(
        const std::wstring& src,
        const std::wstring& ext,
        int tabSize = 4,
        const std::wstring& filePath = L"",
        bool insertSpaces = true);

    /**
     * @brief Formats only the selected line/column range within a document, preserving surrounding lines.
     */
    static std::wstring FormatRange(
        const std::wstring& src,
        size_t startLine,
        size_t startCol,
        size_t endLine,
        size_t endCol,
        const std::wstring& ext,
        int tabSize = 4,
        const std::wstring& filePath = L"",
        bool insertSpaces = true);

    /**
     * @brief Formats HTML source code, including DOCTYPE, tags, attributes, and embedded CSS/JS.
     */
    static std::wstring FormatHtml(const std::wstring& src, int tabSize = 4);

    /**
     * @brief Formats CSS stylesheets, rules, media queries, keyframes, selectors, and properties.
     */
    static std::wstring FormatCss(const std::wstring& src, int tabSize = 4);

    /**
     * @brief Formats JavaScript, TypeScript, and JSX code with clean statements, expressions, and indentation.
     */
    static std::wstring FormatJavaScript(const std::wstring& src, int tabSize = 4);

    /**
     * @brief Formats JSON documents with canonical object/array layout and 4-space indentation.
     */
    static std::wstring FormatJson(const std::wstring& src, int tabSize = 4);

    /**
     * @brief Formats PHP code following PSR-12 coding standard, handling both pure PHP and mixed PHP/HTML.
     */
    static std::wstring FormatPhp(const std::wstring& src, int tabSize = 4);

    /**
     * @brief Returns true if FormatEngine provides integrated formatting for this extension.
     */
    static bool CanFormat(const std::wstring& ext);
};

} // namespace phide::editor
