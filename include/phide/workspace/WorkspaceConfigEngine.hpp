/**
 * @file WorkspaceConfigEngine.hpp
 * @brief Hierarchical .editorconfig resolver, multi-root .phide-workspace persistence,
 *        and per-workspace .phide/settings.json configuration manager.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace phide::editor {
class Buffer;
}

namespace phide::workspace {

/**
 * @struct EditorConfigRules
 * @brief Resolved formatting & whitespace settings from cascading .editorconfig files.
 */
struct EditorConfigRules {
    bool foundAny = false;

    bool hasIndentStyle = false;
    bool indentStyleTabs = false; // false = space, true = tab

    bool hasIndentSize = false;
    int indentSize = 4;

    bool hasTabWidth = false;
    int tabWidth = 4;

    bool hasEndOfLine = false;
    std::wstring endOfLine = L"\n"; // L"\n", L"\r\n", or L"\r"

    bool hasTrimTrailingWhitespace = false;
    bool trimTrailingWhitespace = false;

    bool hasInsertFinalNewline = false;
    bool insertFinalNewline = false;

    bool hasCharset = false;
    std::wstring charset; // e.g., L"utf-8", L"utf-8-bom", L"latin1"
};

/**
 * @struct WorkspaceFolderEntry
 * @brief One root folder inside a multi-root .phide-workspace file.
 */
struct WorkspaceFolderEntry {
    std::wstring name;
    std::wstring path; // Resolved absolute normalized path on disk
};

/**
 * @struct MultiRootWorkspace
 * @brief Multi-root workspace configuration parsed from / saved to *.phide-workspace.
 */
struct MultiRootWorkspace {
    std::wstring workspaceFilePath;
    std::vector<WorkspaceFolderEntry> folders;
    std::unordered_map<std::wstring, std::wstring> settings;
};

/**
 * @struct WorkspaceSettingsOverride
 * @brief Per-workspace settings persisted under <workspaceRoot>/.phide/settings.json.
 */
struct WorkspaceSettingsOverride {
    std::unordered_map<std::wstring, std::wstring> values;
};

/**
 * @class WorkspaceConfigEngine
 * @brief Resolves .editorconfig hierarchies, .phide-workspace multi-root definitions,
 *        and .phide/settings.json while keeping .gitignore / ignore files clean.
 */
class WorkspaceConfigEngine {
public:
    /**
     * @brief Walks ancestors from filePath to filesystem root (stopping at root = true)
     *        and merges matching .editorconfig sections.
     */
    [[nodiscard]] static EditorConfigRules ResolveEditorConfigForFile(const std::wstring& filePath);

    /**
     * @brief Matches an EditorConfig glob pattern against a POSIX-style path relative
     *        to the .editorconfig directory.
     */
    [[nodiscard]] static bool MatchEditorConfigGlob(const std::wstring& pattern, const std::wstring& relPosixPath);

    /**
     * @brief Applies .editorconfig whitespace/newline rules to text before saving to disk.
     */
    [[nodiscard]] static std::wstring ApplyEditorConfigToSaveText(
        const std::wstring& rawText,
        const EditorConfigRules& rules,
        std::wstring& inOutLineEnding,
        bool& inOutHasTrailingNewline);

    /**
     * @brief Applies .editorconfig properties (line ending, trailing newline) onto a Buffer.
     */
    static bool ApplyEditorConfigToBufferForSave(editor::Buffer& buffer, const EditorConfigRules& rules);

    /**
     * @brief Loads and resolves a .phide-workspace (or .code-workspace) JSON file.
     */
    static bool LoadMultiRootWorkspace(const std::wstring& workspaceFilePath, MultiRootWorkspace& outWs);

    /**
     * @brief Saves a .phide-workspace file cleanly to disk (normalizing paths to relative when possible).
     */
    static bool SaveMultiRootWorkspace(const std::wstring& workspaceFilePath, const MultiRootWorkspace& ws);

    /**
     * @brief Loads <workspaceRoot>/.phide/settings.json if present.
     */
    static bool LoadWorkspaceSettings(const std::wstring& workspaceRoot, WorkspaceSettingsOverride& outSettings);

    /**
     * @brief Saves <workspaceRoot>/.phide/settings.json and ensures .phide is listed
     *        in .gitignore (creating .gitignore if missing) and all repo ignore files.
     */
    static bool SaveWorkspaceSettings(const std::wstring& workspaceRoot, const WorkspaceSettingsOverride& settings);

    /**
     * @brief Resolves .phide/settings.json path for a workspace root.
     */
    [[nodiscard]] static std::wstring GetWorkspaceSettingsPath(const std::wstring& workspaceRoot);
};

} // namespace phide::workspace
