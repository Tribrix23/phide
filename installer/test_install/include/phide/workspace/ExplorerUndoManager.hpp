/**
 * @file ExplorerUndoManager.hpp
 * @brief Comprehensive undo and redo stack for workspace file explorer operations.
 * Supports undoing file and folder deletions (with safe staging trash cache),
 * inline renames, file/folder creations, moves, and copies.
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <system_error>
#include <windows.h>

namespace phide::workspace {

enum class ExplorerActionType {
    Rename,
    Delete,
    Create,
    Move,
    Copy
};

struct ExplorerUndoAction {
    ExplorerActionType type = ExplorerActionType::Rename;
    std::wstring primaryPath;     ///< Target or original path
    std::wstring secondaryPath;   ///< Destination path (rename/move) or staging trash path (delete)
    bool isFolder = false;
    std::wstring description;     ///< Diagnostic summary for logs and tooltips
};

/**
 * @class ExplorerUndoManager
 * @brief Thread-safe undo and redo transaction manager for explorer operations.
 */
class ExplorerUndoManager {
public:
    ExplorerUndoManager() = default;
    ~ExplorerUndoManager() {
        CleanupTrash();
    }

    /**
     * @brief Records a file or folder rename operation on the undo stack.
     */
    void PushRename(const std::wstring& oldPath, const std::wstring& newPath) {
        ExplorerUndoAction act;
        act.type = ExplorerActionType::Rename;
        act.primaryPath = oldPath;
        act.secondaryPath = newPath;
        std::error_code ec;
        act.isFolder = std::filesystem::is_directory(newPath, ec);
        act.description = L"Rename";
        m_undoStack.push_back(std::move(act));
        m_redoStack.clear();
    }

    /**
     * @brief Safely stages a file or directory tree into the temporary trash cache prior to deletion.
     * @param path Path to file or folder to back up.
     * @param isFolder True if path is a directory.
     * @param outBackupPath Returns the absolute path where backup data is cached.
     * @return True if staging was successful.
     */
    bool PrepareDelete(const std::wstring& path, bool isFolder, std::wstring& outBackupPath) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return false;

        std::filesystem::path tempBase = std::filesystem::temp_directory_path() / L"phide_trash";
        std::filesystem::path tempDir = tempBase / std::to_wstring(GetTickCount64());
        std::filesystem::create_directories(tempDir, ec);
        if (ec) return false;

        std::filesystem::path src(path);
        std::filesystem::path backup = tempDir / src.filename();
        if (isFolder) {
            std::filesystem::copy(src, backup, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        } else {
            std::filesystem::copy_file(src, backup, std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) return false;

        outBackupPath = backup.wstring();
        m_trashFolders.push_back(tempDir.wstring());
        return true;
    }

    /**
     * @brief Records a deletion operation on the undo stack with its associated staging backup.
     */
    void PushDelete(const std::wstring& originalPath, const std::wstring& backupPath, bool isFolder) {
        ExplorerUndoAction act;
        act.type = ExplorerActionType::Delete;
        act.primaryPath = originalPath;
        act.secondaryPath = backupPath;
        act.isFolder = isFolder;
        act.description = L"Delete";
        m_undoStack.push_back(std::move(act));
        m_redoStack.clear();
    }

    /**
     * @brief Records the creation of a new file or folder on the undo stack.
     */
    void PushCreate(const std::wstring& createdPath, bool isFolder) {
        ExplorerUndoAction act;
        act.type = ExplorerActionType::Create;
        act.primaryPath = createdPath;
        act.isFolder = isFolder;
        act.description = L"Create";
        m_undoStack.push_back(std::move(act));
        m_redoStack.clear();
    }

    /**
     * @brief Records a cut-and-paste (move) operation on the undo stack.
     */
    void PushMove(const std::wstring& srcPath, const std::wstring& destPath) {
        ExplorerUndoAction act;
        act.type = ExplorerActionType::Move;
        act.primaryPath = srcPath;
        act.secondaryPath = destPath;
        std::error_code ec;
        act.isFolder = std::filesystem::is_directory(destPath, ec);
        act.description = L"Move";
        m_undoStack.push_back(std::move(act));
        m_redoStack.clear();
    }

    /**
     * @brief Records a copy-and-paste operation on the undo stack.
     */
    void PushCopy(const std::wstring& destPath) {
        ExplorerUndoAction act;
        act.type = ExplorerActionType::Copy;
        act.primaryPath = destPath;
        std::error_code ec;
        act.isFolder = std::filesystem::is_directory(destPath, ec);
        act.description = L"Copy";
        m_undoStack.push_back(std::move(act));
        m_redoStack.clear();
    }

    [[nodiscard]] bool CanUndo() const noexcept { return !m_undoStack.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !m_redoStack.empty(); }

    /**
     * @brief Undoes the most recent explorer file system operation.
     * @param outAffectedOldPath Old path that was changed (for updating open document tabs).
     * @param outAffectedNewPath New restored path (for updating open document tabs).
     * @param outWasRename Set to true if the operation was a rename/move requiring tab path updates.
     * @return True if an operation was successfully undone.
     */
    bool Undo(std::wstring& outAffectedOldPath, std::wstring& outAffectedNewPath, bool& outWasRename) {
        outWasRename = false;
        if (m_undoStack.empty()) return false;

        auto act = std::move(m_undoStack.back());
        m_undoStack.pop_back();

        std::error_code ec;
        switch (act.type) {
            case ExplorerActionType::Rename: {
                if (std::filesystem::exists(act.secondaryPath, ec)) {
                    std::filesystem::rename(act.secondaryPath, act.primaryPath, ec);
                    if (!ec) {
                        outWasRename = true;
                        outAffectedOldPath = act.secondaryPath;
                        outAffectedNewPath = act.primaryPath;
                        m_redoStack.push_back(std::move(act));
                        return true;
                    }
                }
                break;
            }
            case ExplorerActionType::Delete: {
                // Restore from staging trash backup!
                if (std::filesystem::exists(act.secondaryPath, ec)) {
                    if (act.isFolder) {
                        std::filesystem::copy(act.secondaryPath, act.primaryPath,
                            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                    } else {
                        std::filesystem::copy_file(act.secondaryPath, act.primaryPath,
                            std::filesystem::copy_options::overwrite_existing, ec);
                    }
                    if (!ec) {
                        outAffectedNewPath = act.primaryPath;
                        m_redoStack.push_back(std::move(act));
                        return true;
                    }
                }
                break;
            }
            case ExplorerActionType::Create: {
                // Undo create -> delete created item (back up first so redo can restore)
                if (std::filesystem::exists(act.primaryPath, ec)) {
                    std::wstring backup;
                    PrepareDelete(act.primaryPath, act.isFolder, backup);
                    act.secondaryPath = backup;
                    std::filesystem::remove_all(act.primaryPath, ec);
                    outAffectedOldPath = act.primaryPath;
                    m_redoStack.push_back(std::move(act));
                    return true;
                }
                break;
            }
            case ExplorerActionType::Move: {
                // Undo move -> move back from secondary to primary
                if (std::filesystem::exists(act.secondaryPath, ec)) {
                    std::filesystem::rename(act.secondaryPath, act.primaryPath, ec);
                    if (!ec) {
                        outWasRename = true;
                        outAffectedOldPath = act.secondaryPath;
                        outAffectedNewPath = act.primaryPath;
                        m_redoStack.push_back(std::move(act));
                        return true;
                    }
                }
                break;
            }
            case ExplorerActionType::Copy: {
                // Undo copy -> remove destination
                if (std::filesystem::exists(act.primaryPath, ec)) {
                    std::filesystem::remove_all(act.primaryPath, ec);
                    outAffectedOldPath = act.primaryPath;
                    m_redoStack.push_back(std::move(act));
                    return true;
                }
                break;
            }
        }
        return false;
    }

    /**
     * @brief Redoes the most recently undone explorer operation.
     */
    bool Redo(std::wstring& outAffectedOldPath, std::wstring& outAffectedNewPath, bool& outWasRename) {
        outWasRename = false;
        if (m_redoStack.empty()) return false;

        auto act = std::move(m_redoStack.back());
        m_redoStack.pop_back();

        std::error_code ec;
        switch (act.type) {
            case ExplorerActionType::Rename: {
                if (std::filesystem::exists(act.primaryPath, ec)) {
                    std::filesystem::rename(act.primaryPath, act.secondaryPath, ec);
                    if (!ec) {
                        outWasRename = true;
                        outAffectedOldPath = act.primaryPath;
                        outAffectedNewPath = act.secondaryPath;
                        m_undoStack.push_back(std::move(act));
                        return true;
                    }
                }
                break;
            }
            case ExplorerActionType::Delete: {
                // Redo delete -> remove again
                if (std::filesystem::exists(act.primaryPath, ec)) {
                    std::wstring backup;
                    PrepareDelete(act.primaryPath, act.isFolder, backup);
                    act.secondaryPath = backup;
                    std::filesystem::remove_all(act.primaryPath, ec);
                    outAffectedOldPath = act.primaryPath;
                    m_undoStack.push_back(std::move(act));
                    return true;
                }
                break;
            }
            case ExplorerActionType::Create: {
                // Redo create -> restore from backup
                if (std::filesystem::exists(act.secondaryPath, ec)) {
                    if (act.isFolder) {
                        std::filesystem::copy(act.secondaryPath, act.primaryPath,
                            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                    } else {
                        std::filesystem::copy_file(act.secondaryPath, act.primaryPath,
                            std::filesystem::copy_options::overwrite_existing, ec);
                    }
                    if (!ec) {
                        outAffectedNewPath = act.primaryPath;
                        m_undoStack.push_back(std::move(act));
                        return true;
                    }
                }
                break;
            }
            case ExplorerActionType::Move: {
                if (std::filesystem::exists(act.primaryPath, ec)) {
                    std::filesystem::rename(act.primaryPath, act.secondaryPath, ec);
                    if (!ec) {
                        outWasRename = true;
                        outAffectedOldPath = act.primaryPath;
                        outAffectedNewPath = act.secondaryPath;
                        m_undoStack.push_back(std::move(act));
                        return true;
                    }
                }
                break;
            }
            case ExplorerActionType::Copy: {
                break;
            }
        }
        return false;
    }

    /**
     * @brief Removes temporary trash staging folders on application exit.
     */
    void CleanupTrash() {
        std::error_code ec;
        for (const auto& f : m_trashFolders) {
            std::filesystem::remove_all(f, ec);
        }
        m_trashFolders.clear();
    }

private:
    std::vector<ExplorerUndoAction> m_undoStack;
    std::vector<ExplorerUndoAction> m_redoStack;
    std::vector<std::wstring> m_trashFolders;
};

} // namespace phide::workspace
