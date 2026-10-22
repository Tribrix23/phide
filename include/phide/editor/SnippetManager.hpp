#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include "phide/editor/Buffer.hpp"

namespace phide::editor {

/**
 * @brief Represents a single user code snippet.
 */
struct Snippet {
    std::wstring name;
    std::wstring prefix;
    std::vector<std::wstring> body;
    std::wstring description;
    std::wstring scope; ///< Comma-separated language extensions (e.g. "cpp,hpp", "ts,js,tsx,jsx", "py"), or empty for global.
};

/**
 * @brief Manages loading, persistence, and expansion of user code snippets.
 */
class SnippetManager {
public:
    SnippetManager() = default;

    /**
     * @brief Initializes snippet manager with target snippets JSON path and loads defaults.
     * @param snippetsFilePath Absolute path to user snippets.json in AppData or workspace.
     */
    void Initialize(const std::wstring& snippetsFilePath);

    /**
     * @brief Parses and loads user snippets from disk.
     */
    void LoadSnippets();

    /**
     * @brief Creates the snippets file with rich default templates if it does not exist.
     */
    void EnsureDefaultSnippetsFile();

    /**
     * @brief Retrieves all loaded snippets.
     */
    [[nodiscard]] const std::vector<Snippet>& GetSnippets() const noexcept { return m_snippets; }

    /**
     * @brief Retrieves the snippets file path on disk.
     */
    [[nodiscard]] const std::wstring& GetSnippetsFilePath() const noexcept { return m_snippetsFilePath; }

    /**
     * @brief Attempts to expand a snippet matching the prefix before the active cursor.
     * @param buffer Active text buffer to modify.
     * @param fileExt Lowercase file extension without dot (e.g. "cpp", "ts", "py").
     * @return True if a snippet was matched and successfully expanded.
     */
    bool TryExpand(Buffer& buffer, const std::wstring& fileExt);

private:
    std::vector<Snippet> m_snippets;
    std::wstring m_snippetsFilePath;

    void LoadDefaults();
    [[nodiscard]] bool MatchesScope(const std::wstring& scope, const std::wstring& fileExt) const;
};

} // namespace phide::editor
