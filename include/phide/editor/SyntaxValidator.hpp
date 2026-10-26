/**
 * @file SyntaxValidator.hpp
 * @brief High-performance multi-language syntax validator and error diagnostic engine.
 */

#pragma once

#include <string>
#include <vector>
#include "phide/editor/Buffer.hpp"
#include "phide/lsp/LspClient.hpp"

namespace phide::editor {

class SyntaxValidator {
public:
    /**
     * @brief Analyzes document buffer content according to file extension and returns diagnostic syntax errors.
     * @param filePath Document file path (used for extension detection).
     * @param buffer Document buffer content.
     * @param validateScripts Whether to validate embedded scripts in HTML (Image 3 setting: html.validate.scripts).
     * @param validateStyles Whether to validate embedded styles in HTML (Image 3 setting: html.validate.styles).
     */
    static std::vector<lsp::Diagnostic> Validate(
        const std::wstring& filePath,
        const Buffer& buffer,
        bool validateScripts = true,
        bool validateStyles = true
    );

    /**
     * @brief Validates CSS syntax, invalid properties, missing colons/semicolons, and color values.
     */
    static std::vector<lsp::Diagnostic> ValidateCss(
        const Buffer& buffer,
        size_t startLine = 0,
        size_t endLine = static_cast<size_t>(-1)
    );

    /**
     * @brief Validates JavaScript syntax, unmatched braces/parentheses, and unclosed literals.
     */
    static std::vector<lsp::Diagnostic> ValidateJavaScript(
        const Buffer& buffer,
        size_t startLine = 0,
        size_t endLine = static_cast<size_t>(-1)
    );

private:
    static std::vector<lsp::Diagnostic> ValidateJson(const Buffer& buffer);
    static std::vector<lsp::Diagnostic> ValidateBracketsAndQuotes(const Buffer& buffer, bool isPhp);
    static std::vector<lsp::Diagnostic> ValidateHtml(
        const Buffer& buffer,
        bool validateScripts = true,
        bool validateStyles = true
    );
    static std::vector<lsp::Diagnostic> ValidateYaml(const Buffer& buffer);
    static std::vector<lsp::Diagnostic> ValidatePython(const Buffer& buffer);
    static std::vector<lsp::Diagnostic> ValidatePhp(const Buffer& buffer);
};

} // namespace phide::editor
