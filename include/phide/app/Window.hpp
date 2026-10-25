/**
 * @file Window.hpp
 * @brief Native Win32 frameless window implementation with Direct2D GPU rendering.
 * 
 * Coordinates the entire UI tree: TitleHeader, ActivityRail, DrawerPanel, EditorCanvas,
 * PopupMenu, and ThemePickerModal. Handles Windows 11 DWM message routing, DPI scaling,
 * and hardware acceleration.
 */

#pragma once

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>

#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/TitleHeader.hpp"
#include "phide/ui/PopupMenu.hpp"
#include "phide/ui/ActivityRail.hpp"
#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/ThemePickerModal.hpp"
#include "phide/ui/QuickOpenModal.hpp"
#include "phide/ui/ConfirmDiscardModal.hpp"
#include "phide/ui/DatabaseSettingsModal.hpp"
#include "phide/ui/TerminalPanel.hpp"
#include "phide/ui/ExtensionsPanel.hpp"
#include "phide/server/LiveServer.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/workspace/WorkspaceManager.hpp"
#include "phide/workspace/ExplorerUndoManager.hpp"
#include "phide/editor/SnippetManager.hpp"
#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/editor/SmartImportHelper.hpp"
#include "phide/lsp/LspClient.hpp"

namespace phide::app {

using Microsoft::WRL::ComPtr;

/**
 * @class Window
 * @brief Encapsulates the application window, message loop dispatch, and Direct2D pipeline.
 */
class Window {
public:
    Window();
    ~Window();

    /**
     * @brief Creates the window and initializes Direct2D GPU infrastructure.
     * @param hInstance Win32 application instance handle.
     * @param nCmdShow Initial display mode.
     * @return True if window creation succeeded.
     */
    bool Create(HINSTANCE hInstance, int nCmdShow);

    /**
     * @brief Executes the message loop until WM_QUIT is received.
     * @return Process exit code.
     */
    int Run();

    /**
     * @brief Static window procedure routing messages to the Window instance.
     */
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    enum class ProjectFramework {
        None,
        Php,
        NextJs,
        Vite,
        Electron,
        Django,
        LiveServerHtml,
        NodeScript,
        PythonScript
    };

    struct ProjectDecision {
        ProjectFramework framework = ProjectFramework::None;
        std::wstring rootDir;
        std::wstring command;
        std::wstring title;
        std::wstring customUrl;
        int port = 0;
        std::wstring markerPath;
        std::filesystem::file_time_type markerTime{};
    };

    std::wstring FindProjectRootForFile(const std::wstring& filePath) const;
    ProjectDecision DetectFramework(const std::wstring& rootDir, const std::wstring& activeFilePath);
    void RunProjectDevServer(const std::wstring& projectRoot, const ProjectDecision& decision);
    static bool IsFileInDirectory(const std::wstring& filePath, const std::wstring& dirPath);

private:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    void OnPaint();
    void OnResize(UINT width, UINT height);
    void OnDpiChanged(int newDpi, const RECT* pNewRect);

    // Hit-testing and Mouse Events
    LRESULT HandleNCHitTest(POINT screenPt);
    void HandleMouseMove(POINT clientPt, WPARAM wParam = 0);
    void HandleLButtonDown(POINT clientPt);
    void HandleLButtonUp(POINT clientPt);
    void HandleRButtonDown(POINT clientPt);

    /**
     * @brief Toggles the sidebar drawer panel with smooth sliding animation.
     */
    void ToggleSidebar();

    // Multi-Document Life-cycle & Multi-Group Management
    editor::EditorGroup& GetActiveGroup() noexcept {
        if (m_editorGroups.empty()) {
            m_editorGroups.push_back(editor::EditorGroup{});
        }
        if (m_focusedGroupIdx < 0 || m_focusedGroupIdx >= static_cast<int>(m_editorGroups.size())) {
            m_focusedGroupIdx = 0;
        }
        return m_editorGroups[m_focusedGroupIdx];
    }

    const editor::EditorGroup& GetActiveGroup() const noexcept {
        static const editor::EditorGroup s_empty{};
        if (m_editorGroups.empty()) return s_empty;
        int idx = (m_focusedGroupIdx >= 0 && m_focusedGroupIdx < static_cast<int>(m_editorGroups.size()))
            ? m_focusedGroupIdx : 0;
        return m_editorGroups[idx];
    }

    editor::OpenDocument* GetActiveDoc() noexcept {
        auto& group = GetActiveGroup();
        if (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size())) {
            return &group.docs[group.activeDocIdx];
        }
        return nullptr;
    }

    const editor::OpenDocument* GetActiveDoc() const noexcept {
        const auto& group = GetActiveGroup();
        if (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size())) {
            return &group.docs[group.activeDocIdx];
        }
        return nullptr;
    }

    /**
     * @brief Checks if there is at least one active document opened in the editor.
     * @return True if a document is open.
     */
    [[nodiscard]] bool HasOpenFile() const noexcept {
        return (GetActiveDoc() != nullptr);
    }

    /**
     * @brief Opens a physical file from disk into the editor tab set.
     * @param path Full filesystem path to the file.
     * @param isPreview If true, opens as a temporary preview tab that is replaced by subsequent clicks unless edited.
     */
    void OpenFileDocument(const std::wstring& path, bool isPreview = false);

    /**
     * @brief Opens a side-by-side diff comparison between HEAD and working copy (Images 2, 3, 4, 5).
     * Left pane is HEAD (read-only); right pane is working tree (editable).
     */
    void OpenDiffDocument(const std::wstring& relPath, bool isPreview = false);

    /**
     * @brief Opens a historical commit diff comparison between parent commit and target commit (Image 5).
     */
    void OpenCommitDiffDocument(const std::wstring& commitHash, const std::wstring& relPath = L"");

    /**
     * @brief Computes diff hunks and line highlights between diffOriginalBuffer and buffer for a document.
     */
    void ComputeDiffHunks(editor::OpenDocument& doc);

    /**
     * @brief Opens an in-memory virtual document (e.g. Untitled-1).
     * @param name Display name of the virtual document.
     * @param content Initial buffer text.
     * @param isPreview True if opened in preview mode.
     */
    void OpenVirtualDocument(const std::wstring& name, const std::wstring& content, bool isPreview = false);

    /**
     * @brief Synchronizes buffer contents across all open tabs (normal docs and diff views) matching sourceDoc's filePath.
     */
    void SyncDocumentBuffers(editor::OpenDocument* sourceDoc);

    /**
     * @brief Opens a dedicated special full-surface view (e.g. Keyboard Shortcuts).
     * @param viewType Unique identifier of the view (e.g. L"keybindings").
     * @param title Title displayed on the tab header.
     */
    void OpenSpecialView(const std::wstring& viewType, const std::wstring& title);

    /**
     * @brief Closes the open document at the given group and tab index.
     * Auto-collapses the editor group if it becomes empty and multiple groups exist.
     * @param groupIdx 0-based editor group index.
     * @param docIdx 0-based document tab index inside that group.
     */
    void CloseDocument(int groupIdx, int docIdx);

    /**
     * @brief Closes the open document at the given tab index within the active group.
     * @param index 0-based document index to close.
     */
    void CloseDocument(int index);

    /**
     * @brief Splits the editor canvas into multiple side-by-side columns (up to 3).
     * Clones only the currently active file into the new pane.
     */
    void SplitEditor();

    // --- File & Window Action Handlers ---

    /** @brief Creates a new empty untitled text file document (Ctrl+N). */
    void DoNewTextFile();

    /** @brief Creates a new custom file prompt / untitled file (Ctrl+Alt+N). */
    void DoNewFile();

    /** @brief Spawns an independent non-maximized new application window (Ctrl+Shift+N). */
    void DoNewWindow();

    /** @brief Prompts with Open File dialog and loads chosen file into editor (Ctrl+O). */
    void DoOpenFile();

    /** @brief Prompts with Open Folder dialog and populates explorer tree (Ctrl+K Ctrl+O). */
    void DoOpenFolder();

    /** @brief Directly opens the given directory path into the sidebar tree. */
    void DoOpenFolderFromPath(const std::wstring& path);

    /** @brief Prompts to open a saved workspace file. */
    void DoOpenWorkspaceFromFile();

    /** @brief Adds an additional folder to the current workspace session. */
    void DoAddFolderToWorkspace();

    /** @brief Prompts to save the active workspace configuration to a file. */
    void DoSaveWorkspaceAs();

    /** @brief Clones the active workspace into a duplicate configuration. */
    void DoDuplicateWorkspace();

    /** @brief Executes a quick open palette action or command. */
    void ExecuteQuickOpenAction(const std::wstring& actionCmd);

    /** @brief Jumps the active editor document cursor and viewport to the target 1-based line number. */
    void JumpToEditorLine(size_t targetLine);

    /** @brief Saves current buffer changes to disk (Ctrl+S). */
    void DoSaveFile();

    /** @brief Prompts with Save As dialog to save current buffer under a new path (Ctrl+Shift+S). */
    void DoSaveFileAs();

    /** @brief Saves all modified/dirty open documents to disk (Ctrl+K S). */
    void DoSaveAll();

    /** @brief Exports user IDE profile settings to a JSON file. */
    void DoExportProfile();

    /** @brief Imports user IDE profile settings from a JSON file. */
    void DoImportProfile();

    /** @brief Opens user settings view or preferences. */
    void DoOpenSettings();

    /** @brief Opens the interactive Keyboard Shortcuts view with keycap badges (Ctrl+K Ctrl+S). */
    void DoOpenKeyboardShortcuts();

    /** @brief Opens user code snippets configuration. */
    void DoOpenUserSnippets();

    /** @brief Opens or toggles the Theme Picker modal dialog (Ctrl+K Ctrl+T). */
    void DoToggleThemeModal();

    /** @brief Opens the Quick Open file search palette modal (Ctrl+K, Ctrl+P). */
    void DoQuickOpenFileSearch();

    /** @brief Opens the Database & phpMyAdmin configuration settings modal. */
    void DoOpenDatabaseSettings();

    /** @brief Opens the Nginx Web Server configuration settings modal. */
    void DoOpenServerSettings();

    /** @brief Cycles through Dark, Light, and Auto themes directly. */
    void DoToggleTheme();

    /** @brief Reverts active document buffer back to its saved state on disk. */
    void DoRevertFile();

    /** @brief Closes the currently active editor tab (Ctrl+W). */
    void DoCloseEditor();

    /** @brief Unloads the open project folder and returns explorer to empty state (Ctrl+K F). */
    void DoCloseFolder();

    /** @brief Re-opens the most recently closed editor document tab (Ctrl+Shift+T). */
    void DoReopenClosedEditor();

    // --- Edit & Clipboard Commands ---

    /** @brief Cuts selected text to system clipboard (Ctrl+X). */
    void DoCut();

    /** @brief Copies selected text to system clipboard (Ctrl+C). */
    void DoCopy();

    /** @brief Pastes clipboard text into active buffer (Ctrl+V). */
    void DoPaste();

    /** @brief Reverts last buffer mutation (Ctrl+Z). */
    void DoUndo();

    /** @brief Redoes previously undone buffer mutation (Ctrl+Y). */
    void DoRedo();

    /** @brief Focuses Find / Search in document (Ctrl+F). */
    void DoFind();

    /** @brief Focuses Replace in document (Ctrl+H). */
    void DoReplace();

    /** @brief Focuses Replace perspective in Search drawer (Ctrl+Shift+H). */
    void DoShowReplace();

    /** @brief Toggles line comment on active line or selection (Ctrl+/). */
    void DoToggleLineComment();

    /** @brief Toggles block comment on active line or selection (Shift+Alt+A). */
    void DoToggleBlockComment();

    /** @brief Expands Emmet abbreviation before cursor (Tab). */
    void DoEmmetExpand();

    /** @brief Formats active document (Shift+Alt+F). */
    void DoFormatDocument();

    /** @brief Formats selected code range (Ctrl+K Ctrl+F). */
    void DoFormatSelection();

    /** @brief Sorts and organizes imports (Shift+Alt+O). */
    void DoOrganizeImports();

    /** @brief Jumps to definition of symbol under cursor (F12). */
    void DoGoToDefinition();

    /** @brief Prompts symbol rename (F2). */
    void DoRenameSymbol();

    /** @brief Collects all open document paths across all editor groups and synchronizes with Drawer. */
    void RefreshOpenDocPathsForSearch();

    /** @brief Undoes the most recent Explorer operation (rename, delete, create, move) (Ctrl+Z when explorer active). */
    void DoExplorerUndo();

    /** @brief Redoes the most recently undone Explorer operation (Ctrl+Y when explorer active). */
    void DoExplorerRedo();

    /**
     * @brief Marks a document as edited, honoring Auto Save to write changes immediately to disk.
     * If Auto Save is active and the file is backed by disk, persists changes without setting dirty flag.
     * @param pDoc Pointer to open document buffer being modified.
     */
    void MarkDocumentModified(editor::OpenDocument* pDoc);

    /** @brief Selects all text in current document (Ctrl+A). */
    void DoSelectAll();

    /** @brief Selects and highlights all occurrences of active word or selection (Ctrl+F2). */
    void DoChangeAllOccurrences();

    /** @brief Displays refactoring options or 'No refactorings available' popover balloon (Ctrl+Shift+R). */
    void DoRefactor();

    /** @brief Opens Command Palette / Keybindings lookup (Ctrl+Shift+P). */
    void DoCommandPalette();

    // --- View & Navigation Commands ---

    /** @brief Zooms in editor font size by 2pt (Ctrl+=). */
    void DoZoomIn();

    /** @brief Zooms out editor font size by 2pt (Ctrl+-). */
    void DoZoomOut();

    /** @brief Resets editor font size to default 14pt (100%) (Ctrl+0). */
    void DoResetZoom();

    /** @brief Toggles line wrap in code editor canvas (Alt+Z). */
    void DoToggleWordWrap();

    /** @brief Toggles side-by-side split editor mode (Ctrl+\). */
    void ToggleSplitEditor();

    /** @brief Activates Explorer perspective in activity rail (Ctrl+Shift+E). */
    void DoShowExplorer();

    /** @brief Activates Search perspective in activity rail (Ctrl+Shift+F). */
    void DoShowSearch();

    /** @brief Activates Source Control perspective in activity rail (Ctrl+Shift+G). */
    void DoShowSourceControl();

    /** @brief Spawns or toggles integrated terminal panel (Ctrl+Shift+`). */
    void DoCreateTerminal();

    /** @brief Executes the active file: Live Server for HTML with clickable URLs, Node for JS, disabled for CSS. */
    void DoRunActiveDocument();

    /** @brief Starts an interactive debugging session with the active document (same layout as terminal). */
    void DoDebugActiveDocument();

    /** @brief Stops the running Live Server or debugging session. */
    void DoStopServer();

    /** @brief Restarts the running Live Server or debugging session. */
    void DoRestartServer();

    /** @brief Opens dedicated realtime Nginx server log tab in terminal. */
    void DoOpenServerLog();

    /** @brief Opens dedicated realtime phpMyAdmin database log tab in terminal. */
    void DoOpenDatabaseLog();

    /** @brief Opens process execution failure error report in terminal. */
    void DoOpenErrorLog();

    /** @brief Launches PHP development server with embedded Nginx and phpMyAdmin orchestration. */
    void DoRunPhpApplication(const std::wstring& filePath, const std::wstring& projectRoot);

    /** @brief Updates enabled/disabled state of Run and Debug buttons based on active file type. */
    void UpdateExecutionButtonStates();

    /** @brief Switches to next document tab in the active editor group (Ctrl+Tab). */
    void DoNextTab();

    /** @brief Switches to previous document tab in the active editor group (Ctrl+Shift+Tab). */
    void DoPrevTab();

    /** @brief Opens or focuses a file in the workspace matching the specified relative filename. */
    void OpenFileByName(const std::wstring& name);

    /** @brief Loads file from filesystem path into active buffer. */
    void LoadFileFromPath(const std::wstring& path);

    /** @brief Writes contents of active buffer to the specified filesystem path. */
    void SaveFileToPath(const std::wstring& path);

    // --- Session State Persistence ---

    /** @brief Saves current workspace folder, open tabs, and active tab index to AppData JSON file. */
    void SaveSessionState();

    /** @brief Restores previous workspace folder, open tabs, and active tab index from AppData JSON file. */
    void LoadSessionState();

    /** @brief Computes absolute path to the session persistence file in %APPDATA%\phide\session.json. */
    std::wstring GetSessionFilePath() const;

    /** @brief Computes absolute path to the user snippets configuration file in %APPDATA%\phide\snippets\snippets.json. */
    std::wstring GetSnippetsConfigPath() const;

    /** @brief Starts background filesystem watcher on active workspace for event-driven Git & tree reactivity. */
    void StartWorkspaceWatcher(const std::wstring& folderPath);
    void StopWorkspaceWatcher();

    /** @brief Queues a debounced Git status refresh (e.g. 2s after typing/saving stops). */
    void TriggerDebouncedGitRefresh(DWORD delayMs = 2000);

    /** @brief Synchronizes all open in-memory dirty documents into the Source Control drawer (Images 3 & 4). */
    void UpdateDrawerDirtyFiles();

    /** @brief Synchronizes documents with syntax/diagnostic errors into the Explorer drawer (Image 3). */
    void UpdateDrawerErrorFiles();

    /** @brief Commits selected autocomplete item or HTML tag block (Images 1 & 5). */
    void CommitAutocompleteSelection();

    // Win32 & D2D handles
    HWND m_hwnd = nullptr;
    ComPtr<ID2D1Factory> m_pD2DFactory;
    ComPtr<ID2D1HwndRenderTarget> m_pRenderTarget;
    ComPtr<IDWriteFactory> m_pDWriteFactory;

    // Subsystems
    AppConfig m_config;
    ui::Theme m_theme;
    ui::TitleHeader m_header;
    ui::PopupMenu m_popupMenu;
    ui::ActivityRail m_rail;
    ui::DrawerPanel m_drawer;
    ui::EditorCanvas m_editor;
    ui::ThemePickerModal m_themePicker;
    ui::QuickOpenModal m_quickOpen;
    ui::ConfirmDiscardModal m_confirmDiscardModal;
    ui::DatabaseSettingsModal m_dbSettingsModal;
    ui::TerminalPanel m_terminal;
    ui::ExtensionsPanel m_extensionsPanel;
    bool m_isTerminalFocused = false;

    std::unordered_map<std::wstring, ProjectDecision> m_projectDecisionCache;
    std::wstring m_runningProjectRoot;
    std::wstring m_lastTerminalErrorText;
    ProjectDecision m_runningProjectDecision;
    bool m_isProjectRunning = false;
    void ExecuteDiscardOrRestore(const std::wstring& targetPath, const std::wstring& targetFileName, const std::wstring& targetStatus);

    // Multi-LiveServer Architecture
    struct ActiveLiveServer {
        std::wstring filePath;
        std::wstring fileName;
        std::wstring rootDir;
        int port = 5500;
        std::unique_ptr<server::LiveServer> server;
        float progress = 0.0f;              ///< 0.0f to 1.0f progression
        float progressDurationMs = 2200.0f; ///< ~2.2 seconds progression
        float elapsedMs = 0.0f;             ///< Milliseconds elapsed
        bool browserOpened = false;         ///< True after browser launched
        bool isStarting = false;            ///< True while progression active
        bool hasError = false;              ///< True if dev server failed/errored
        std::wstring errorMsg;              ///< Error message from dev server
        std::wstring framework;             ///< Framework name (Next.js, Vite, Django, Live Server)
        std::wstring title;                 ///< Display title (Next.js Dev Server, Vite Dev Server, Django Server, Live Server)
        std::wstring customUrl;             ///< http://localhost:3000, etc.
    };
    std::vector<ActiveLiveServer> m_activeLiveServers;
    ActiveLiveServer* FindLiveServer(const std::wstring& filePath);
    ActiveLiveServer* GetActiveDocLiveServer();
    void StopLiveServer(const std::wstring& filePath, bool closeTab = true);
    void StopAllLiveServers();
    int GetNextAvailableLiveServerPort() const;

    // Background web server processes (PHP, Nginx, phpMyAdmin)
    HANDLE m_hPhpServerProcess = nullptr;
    HANDLE m_hNginxProcess = nullptr;
    HANDLE m_hPmaProcess = nullptr;
    void StartPhpServer(const std::wstring& rootDir, int port = 8080);
    void StopPhpServer();
    void StartNginxServer();
    void StopNginxServer();
    void StartPmaServer();
    void StopPmaServer();

    server::LiveServer m_liveServer; // backwards compatibility / fallback
    D2D1_RECT_F m_liveServerPillBounds{};
    D2D1_RECT_F m_liveServerCloseBounds{};
    bool m_hoverLiveServerPill = false;
    bool m_hoverLiveServerClose = false;
    workspace::WorkspaceManager m_workspace;
    workspace::ExplorerUndoManager m_explorerUndo;
    editor::SnippetManager m_snippetManager;
    editor::IntelliSenseEngine m_intelliSense;
    lsp::LspClient m_lsp;

    // Filesystem Watcher Handles
    HANDLE m_hWatcherThread = nullptr;
    HANDLE m_hWatcherStopEvent = nullptr;

    // Multi-Editor Groups Architecture (Up to 3 columns)
    std::vector<editor::EditorGroup> m_editorGroups;
    int m_focusedGroupIdx = 0;
    int m_untitledCounter = 0;

    bool m_trackingMouse = false;
    bool m_wordWrap = false;
    bool m_autoSave = true;
    bool m_chordCtrlK = false;
    bool m_suppressNextChar = false;
    bool m_isCleanNewWindow = false;
    bool m_isDraggingGitSplitter = false;
    bool m_gitRefreshPending = false;
    ULONGLONG m_gitRefreshTargetTime = 0;
    std::vector<std::wstring> m_recentPaths;
    std::wstring m_lastClosedFilePath;

    // Explorer Context Menu & Clipboard State (Images 4 & 5)
    std::wstring m_contextTargetPath;
    bool m_contextTargetIsFolder = false;
    std::wstring m_clipboardPath;
    bool m_clipboardIsCut = false;
    std::wstring m_compareSourcePath;
    std::wstring m_selectedExplorerPath;
    bool m_explorerHasFocus = false;
    void DoDeleteSelectedExplorerItem();

    UINT_PTR m_caretTimerId    = 1001;
    UINT_PTR m_animTimerId     = 1002;
    UINT_PTR m_throttleTimerId = 1003; ///< 50ms timer for throttled code search queries

    float m_drawerAnimProgress   = 0.0f;
    float m_targetDrawerProgress = 0.0f;
    bool  m_isDrawerAnimating    = false;

    float m_terminalAnimProgress   = 0.0f;
    float m_targetTerminalProgress = 0.0f;
    bool  m_isTerminalAnimating    = false;

    float m_termFullScreenProgress    = 0.0f;
    float m_targetTermFullScreen      = 0.0f;
    bool  m_isTermFullScreenAnimating = false;

    float m_rightSidebarAnimProgress   = 0.0f;
    float m_targetRightSidebarProgress = 0.0f;
    bool  m_isRightSidebarAnimating    = false;

    [[nodiscard]] float GetCurrentTerminalHeight(float clientH) const noexcept {
        if (!m_terminal.IsOpen() && !m_isTerminalAnimating) return 0.0f;
        const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
        float normalH = m_terminal.GetHeight();
        float fullH = (std::max)(normalH, clientH - headerH);
        float effectiveMax = normalH + (fullH - normalH) * m_termFullScreenProgress;
        return effectiveMax * m_terminalAnimProgress;
    }

    // Explorer Drag & Drop Import State (Godot / VS Code style)
    bool m_isDraggingExplorerFile = false;
    std::wstring m_dragCandidatePath;
    std::wstring m_dragCandidateName;
    POINT m_dragStartPt{ 0, 0 };
    POINT m_dragCurrentPt{ 0, 0 };
    bool m_hasDragDropTarget = false;
    size_t m_dragDropTargetLine = 0;
    size_t m_dragDropTargetCol = 0;
    int m_dragDropTargetGroup = -1;

    // Tab Drag & Drop Reordering State (Image 2)
    bool m_isDraggingTab = false;
    int m_dragTabGroup = -1;
    int m_dragTabIdx = -1;
    int m_tabDropTargetGroup = -1;
    int m_tabDropTargetIdx = -1;
    POINT m_dragTabStartPt{ 0, 0 };
    POINT m_dragTabCurrentPt{ 0, 0 };

    void AnimateTerminalOpen();
    void AnimateTerminalClose();
    void AnimateRightSidebarOpen();
    void AnimateRightSidebarClose();
};

} // namespace phide::app
