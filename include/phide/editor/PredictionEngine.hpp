#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include "phide/editor/Buffer.hpp"

namespace phide::editor {

/**
 * @brief NEXT: In-File & Cross-File Multi-Point Code Prediction struct.
 */
struct CodePrediction {
    bool isValid = false;
    size_t line = 0;              ///< Target line index where prediction is positioned
    size_t col = 0;               ///< Target column
    std::wstring ghostText;       ///< Multi-line or single-line ghost text suggestion
    std::wstring displaySummary;  ///< Friendly tooltip or label
    bool isCrossFile = false;     ///< True if cross-file suggestion
    std::wstring targetFilePath;  ///< Full path to target file for cross-file jump
    std::wstring targetFileName;  ///< Display filename (e.g. "route.ts")
    size_t targetLine = 0;        ///< Target line index to jump to
    bool hasLightbulb = false;    ///< Show lightbulb in gutter
};

/**
 * @brief High-performance native local prediction engine.
 * Synthesizes in-file and cross-file next-edit predictions with zero external LLMs,
 * sub-millisecond latency, and negligible memory overhead.
 */
class PredictionEngine {
public:
    PredictionEngine() = default;

    void SetWorkspaceRoot(const std::wstring& root) { m_workspaceRoot = root; }
    [[nodiscard]] const std::wstring& GetWorkspaceRoot() const noexcept { return m_workspaceRoot; }

    /**
     * @brief Predict the next edit block at or near the given cursor position.
     * @param filePath Absolute path of the current document.
     * @param buffer The current document buffer.
     * @param cursorLine Line where cursor is currently located.
     * @param cursorCol Column where cursor is currently located.
     * @param openFiles List of other currently open file paths in the editor.
     * @return CodePrediction with ghost text and jump metadata, or isValid=false if no prediction.
     */
    [[nodiscard]] CodePrediction PredictNextEdit(
        const std::wstring& filePath,
        const Buffer& buffer,
        size_t cursorLine,
        size_t cursorCol,
        const std::vector<std::wstring>& openFiles = {}
    );

private:
    std::wstring m_workspaceRoot;

    CodePrediction PredictApiRouteHandlers(const std::wstring& filePath, const Buffer& buffer, size_t line, size_t col);
    CodePrediction PredictFromComments(const std::wstring& filePath, const Buffer& buffer, size_t line, size_t col);
    CodePrediction PredictCrossFile(const std::wstring& filePath, const Buffer& buffer, size_t line, size_t col, const std::vector<std::wstring>& openFiles);
    CodePrediction PredictComponentOrMethod(const std::wstring& filePath, const Buffer& buffer, size_t line, size_t col);
};

} // namespace phide::editor
