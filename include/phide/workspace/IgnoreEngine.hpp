#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <filesystem>

namespace phide::workspace {

/**
 * @brief Represents a single parsed rule from an ignore file (.gitignore, .dockerignore, .npmignore, etc.)
 */
struct IgnoreRule {
    std::wstring rawPattern;       ///< Original line from ignore file
    std::vector<std::wstring> patternSegments; ///< Pre-split path segments
    std::wstring baseDir;          ///< Normalized directory where the ignore file resides
    std::wstring sourceFileName;   ///< Source filename (e.g. L".gitignore", L".dockerignore")
    bool isNegative = false;       ///< Starts with '!' (negates ignore)
    bool directoryOnly = false;    ///< Ends with '/' (only applies to directories)
    bool rootOnly = false;         ///< Starts with '/' (anchored to baseDir)
    bool hasSlash = false;         ///< Contains '/' within pattern (if false, matches any depth)
};

/**
 * @brief High-performance multi-file ignore & exclusion engine.
 * Supports .gitignore, .dockerignore, .npmignore, .eslintignore, .prettierignore, .ignore, etc.
 * Features:
 *  - Full gitignore/dockerignore glob semantics (*, **, ?, !, trailing /)
 *  - Support for multiple ignore files simultaneously
 *  - Subdirectory-aware ignore file scoping
 *  - Sub-microsecond path evaluation with zero memory allocations in hot paths
 *  - Thread-safe reload and lookup
 */
class IgnoreEngine {
public:
    IgnoreEngine();
    ~IgnoreEngine() = default;

    /**
     * @brief Clear all loaded rules and cached states.
     */
    void Clear();

    /**
     * @brief Scan and load all supported ignore files in the given workspace root.
     * Searches for .gitignore, .dockerignore, .npmignore, .eslintignore, .prettierignore, .ignore, etc.
     * Also recursively scans for nested ignore files.
     * @param workspaceRoot Absolute path to workspace root.
     */
    void LoadWorkspaceIgnoreFiles(const std::wstring& workspaceRoot);

    /**
     * @brief Load rules from a specific ignore file on disk.
     * @param filePath Absolute path to the ignore file.
     * @return Number of rules loaded from this file.
     */
    size_t LoadIgnoreFile(const std::filesystem::path& filePath);

    /**
     * @brief Parse rules from in-memory text with a specified base directory.
     * @param content Raw text of the ignore file.
     * @param baseDir Base directory that relative patterns anchor to.
     * @param sourceName Descriptive name (e.g. L".dockerignore").
     */
    void ParseRules(const std::wstring& content, const std::wstring& baseDir, const std::wstring& sourceName);

    /**
     * @brief Check whether a file or directory is ignored/excluded.
     * @param fullPath Full absolute path or relative path to check.
     * @param fileName File or folder name (label).
     * @param isDirectory True if target is a folder/directory.
     * @return True if path is ignored/excluded by any rule.
     */
    [[nodiscard]] bool IsIgnored(
        const std::wstring& fullPath,
        const std::wstring& fileName,
        bool isDirectory
    ) const;

    /**
     * @brief Set or merge external git status ignore set (e.g. from git status --ignored).
     */
    void SetGitIgnoredSet(std::unordered_set<std::wstring> gitSet);

    /**
     * @brief Get count of active loaded rules.
     */
    [[nodiscard]] size_t GetRuleCount() const noexcept;

    /**
     * @brief Get list of all loaded ignore file sources.
     */
    [[nodiscard]] std::vector<std::wstring> GetLoadedSourceFiles() const;

    /**
     * @brief Check if a filename is one of the supported ignore file names.
     */
    static bool IsSupportedIgnoreFileName(const std::wstring& fileName);

    /**
     * @brief Wildcard glob match between candidate text and glob pattern (supporting * and ?).
     */
    static bool GlobMatch(std::wstring_view text, std::wstring_view pattern);

private:
    static std::wstring NormalizePath(const std::wstring& path);
    static std::vector<std::wstring> SplitSegments(std::wstring_view path);
    static bool MatchSegments(
        const std::vector<std::wstring>& pathSegs,
        size_t pathIdx,
        const std::vector<std::wstring>& patSegs,
        size_t patIdx
    );

    mutable std::mutex m_mutex;
    std::wstring m_workspaceRoot;
    std::vector<IgnoreRule> m_rules;
    std::unordered_set<std::wstring> m_gitIgnoredSet;
    std::vector<std::wstring> m_loadedSourceFiles;
};

} // namespace phide::workspace
