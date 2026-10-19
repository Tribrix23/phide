/**
 * @file WorkspaceManager.hpp
 * @brief Manages project directories, virtual file trees, and file system interactions.
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace phide::workspace {

namespace fs = std::filesystem;

/**
 * @struct FileNode
 * @brief Metadata for a file or directory within the workspace tree.
 */
struct FileNode {
    std::wstring name;
    fs::path fullPath;
    bool isDirectory = false;
    size_t fileSize = 0;
};

/**
 * @class WorkspaceManager
 * @brief Handles project root scanning, file loading, and workspace indexing.
 */
class WorkspaceManager {
public:
    WorkspaceManager();
    explicit WorkspaceManager(const fs::path& rootPath);

    /**
     * @brief Sets and scans a new workspace root directory.
     * @param rootPath Absolute or relative filesystem path.
     */
    void OpenWorkspace(const fs::path& rootPath);

    /**
     * @brief Gets the active workspace root path.
     */
    [[nodiscard]] const fs::path& GetRootPath() const noexcept { return m_rootPath; }

    /**
     * @brief Scans immediate directory entries.
     * @return List of FileNode descriptors.
     */
    [[nodiscard]] std::vector<FileNode> ListDirectory(const fs::path& dirPath) const;

    /**
     * @brief Reads entire file content as a wide string.
     * @param filePath Path to file.
     * @return File content.
     */
    [[nodiscard]] std::wstring ReadFileContent(const fs::path& filePath) const;

private:
    fs::path m_rootPath;
};

} // namespace phide::workspace
