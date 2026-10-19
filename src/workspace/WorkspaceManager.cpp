/**
 * @file WorkspaceManager.cpp
 * @brief Implementation of workspace directory scanning and file retrieval.
 */

#include "phide/workspace/WorkspaceManager.hpp"
#include <fstream>
#include <sstream>

namespace phide::workspace {

WorkspaceManager::WorkspaceManager()
    : m_rootPath(fs::current_path()) {}

WorkspaceManager::WorkspaceManager(const fs::path& rootPath)
    : m_rootPath(rootPath) {}

void WorkspaceManager::OpenWorkspace(const fs::path& rootPath) {
    if (fs::exists(rootPath) && fs::is_directory(rootPath)) {
        m_rootPath = fs::canonical(rootPath);
    }
}

std::vector<FileNode> WorkspaceManager::ListDirectory(const fs::path& dirPath) const {
    std::vector<FileNode> nodes;
    std::error_code ec;

    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        return nodes;
    }

    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        std::wstring filename = entry.path().filename().wstring();

        // Filter hidden or build folders
        if (filename.rfind(L".", 0) == 0 || filename == L"bin" || filename == L"build") {
            continue;
        }

        FileNode node;
        node.name = filename;
        node.fullPath = entry.path();
        node.isDirectory = entry.is_directory();
        node.fileSize = node.isDirectory ? 0 : entry.file_size(ec);
        nodes.push_back(node);
    }

    return nodes;
}

std::wstring WorkspaceManager::ReadFileContent(const fs::path& filePath) const {
    std::wifstream wif(filePath);
    if (!wif.is_open()) return L"";

    std::wstringstream wss;
    wss << wif.rdbuf();
    return wss.str();
}

} // namespace phide::workspace
