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
     */
    static std::vector<lsp::Diagnostic> Validate(const std::wstring& filePath, const Buffer& buffer);

private:
    static std::vector<lsp::Diagnostic> ValidateJson(const Buffer& buffer);
    static std::vector<lsp::Diagnostic> ValidateBracketsAndQuotes(const Buffer& buffer, bool isPhp);
    static std::vector<lsp::Diagnostic> ValidateHtml(const Buffer& buffer);
    static std::vector<lsp::Diagnostic> ValidateYaml(const Buffer& buffer);
};

} // namespace phide::editor
