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
     * @brief Imports a VS Code .code-snippets or language JSON snippets file (bounded <= 1 MB).
     */
    bool ImportVscodeSnippetsFromFile(const std::wstring& filePath, const std::wstring& defaultScope = L"");

    /**
     * @brief Imports all .vscode/*.code-snippets files inside a workspace folder (bounded <= 32 files).
     */
    size_t ImportVscodeWorkspaceSnippets(const std::wstring& workspaceRoot);

    /**
     * @brief Parses a JSON/JSONC snippets payload in memory and merges matching entries into m_snippets.
     */
    size_t ParseSnippetsJsonContent(const std::wstring& content, const std::wstring& defaultScope = L"");

    /**
     * @brief Creates the snippets file with rich default templates if it does not exist or is legacy stub.
     */
    void EnsureDefaultSnippetsFile();

    /**
     * @brief Serializes all loaded snippets to target JSON file.
     * @param filePath Destination path for snippets.json.
     */
    void SaveSnippets(const std::wstring& filePath) const;

    /**
     * @brief Retrieves all loaded snippets (mutable for GUI snippet editor).
     */
    [[nodiscard]] const std::vector<Snippet>& GetSnippets() const noexcept { return m_snippets; }
    [[nodiscard]] std::vector<Snippet>& GetMutableSnippets() noexcept { return m_snippets; }

    /**
     * @brief Adds or updates a snippet and optionally saves snippets.json to disk.
     */
    size_t UpsertSnippet(size_t existingIndex, const Snippet& snippet, bool saveToDisk = true);

    /**
     * @brief Deletes a snippet by index and optionally saves snippets.json to disk.
     */
    bool DeleteSnippet(size_t index, bool saveToDisk = true);

    /**
     * @brief Retrieves the snippets file path on disk.
     */
    [[nodiscard]] const std::wstring& GetSnippetsFilePath() const noexcept { return m_snippetsFilePath; }

    /**
     * @brief Attempts to expand a snippet matching the prefix before the active cursor.
     * @param buffer Active text buffer to modify.
     * @param fileExt Lowercase file extension without dot (e.g. "cpp", "ts", "py").
     * @param filename Optional active document filename used to derive dynamic names (e.g. PascalCase React component names).
     * @return True if a snippet was matched and successfully expanded.
     */
    bool TryExpand(Buffer& buffer, const std::wstring& fileExt, const std::wstring& filename = L"");

    /**
     * @brief Finds a snippet matching the exact prefix and file scope.
     */
    [[nodiscard]] const Snippet* FindSnippetByPrefix(const std::wstring& prefix, const std::wstring& fileExt) const;

    /**
     * @brief Expands the given snippet into the buffer, replacing prefixLen chars.
     * @param filename Optional active document filename to replace ComponentName / $TM_FILENAME_BASE.
     */
    bool ExpandSnippet(const Snippet& snippet, Buffer& buffer, size_t prefixLen, const std::wstring& filename = L"");

    /**
     * @brief Derives a clean PascalCase component name from a filename (e.g. "practice.tsx" -> "Practice", "user-card.tsx" -> "UserCard").
     */
    [[nodiscard]] static std::wstring DeriveComponentName(const std::wstring& filename);

    /**
     * @brief Checks whether the snippet scope matches the given file extension.
     */
    [[nodiscard]] static bool MatchesScope(const std::wstring& scope, const std::wstring& fileExt);

private:
    std::vector<Snippet> m_snippets;
    std::wstring m_snippetsFilePath;

    void LoadDefaults();
};

} // namespace phide::editor
