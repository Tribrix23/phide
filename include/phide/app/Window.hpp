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
#include <unordered_set>
#include <filesystem>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

#include "phide/app/AppConfig.hpp"
#include "phide/app/Logger.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/TitleHeader.hpp"
#include "phide/ui/PopupMenu.hpp"
#include "phide/ui/ActivityRail.hpp"
#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/ThemePickerModal.hpp"
#include "phide/editor/WorkspaceNavigation.hpp"
#include "phide/ui/ColorPickerModal.hpp"
#include "phide/ui/QuickOpenModal.hpp"
#include "phide/ui/RemoteWindowModal.hpp"
#include "phide/ui/ConfirmDiscardModal.hpp"
#include "phide/ui/GitErrorModal.hpp"
#include "phide/ui/GitLogManager.hpp"
#include "phide/ui/DatabaseSettingsModal.hpp"
#include "phide/ui/TerminalPanel.hpp"
#include "phide/ui/ExtensionsPanel.hpp"
#include "phide/dev/DevWorkflowEngines.hpp"
#include "phide/dev/DapClient.hpp"
#include "phide/server/LiveServer.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/workspace/WorkspaceManager.hpp"
#include "phide/workspace/WorkspaceConfigEngine.hpp"
#include "phide/workspace/ExplorerUndoManager.hpp"
#include "phide/editor/SnippetManager.hpp"
#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/editor/PredictionEngine.hpp"
#include "phide/editor/AdaptiveCodeEngine.hpp"
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

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void OpenVirtualDocument(const std::wstring& name, const std::wstring& content, bool isPreview = false);
    void SetAutoSave(bool enable) { m_autoSave = enable; }
    bool IsAutoSave() const { return m_autoSave; }
    void FlushPendingAutoSaves();

    void OpenSpecialView(const std::wstring& viewType, const std::wstring& title);

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
        static const editor::EditorGroup s_emptyGroup{};
        if (m_editorGroups.empty()) {
            return s_emptyGroup;
        }
        int idx = m_focusedGroupIdx;
        if (idx < 0 || idx >= static_cast<int>(m_editorGroups.size())) {
            idx = 0;
        }
        return m_editorGroups[idx];
    }

private:

    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    void OnPaint();
    void RenderStatusBar(float clientW, float clientH);
    void OnResize(UINT width, UINT height);
    void OnDpiChanged(int newDpi, const RECT* pNewRect);

    // Hit-testing and Mouse Events
    LRESULT HandleNCHitTest(POINT screenPt);
    void HandleMouseMove(POINT clientPt, WPARAM wParam = 0);
    void HandleLButtonDown(POINT clientPt);
    bool HandlePopupMenuClick(POINT clientPt);
    void HandleLButtonUp(POINT clientPt);
    void HandleRButtonDown(POINT clientPt);

    /**
     * @brief Toggles the sidebar drawer panel with smooth sliding animation.
     */
    void ToggleSidebar();

    /**
     * @brief Toggles Zen Mode (distraction-free full-screen editor layout).
     */
    void ToggleZenMode();

    /**
     * @brief Computes canvasLeft and canvasWidth respecting drawer, sidebars, centered layout and zen mode.
     */
    void GetCanvasLayout(float clientW, float& outCanvasLeft, float& outCanvasWidth) const;

public:
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
     * @brief Computes diff hunks and line highlights between diffOriginalBuffer and buffer for a document.
     */
    void ComputeDiffHunks(editor::OpenDocument& doc);
    void RefreshMergeConflicts(editor::OpenDocument& doc);
    void CalculateImportCosts(editor::OpenDocument& doc);
    void MarkDocumentModified(editor::OpenDocument* pDoc);

    // Git Branch Switcher Dropdown (Status bar popup)
    ui::DrawerPanel& GetDrawer() noexcept { return m_drawer; }
    ui::EditorCanvas& GetEditor() noexcept { return m_editor; }
    const ui::EditorCanvas& GetEditor() const noexcept { return m_editor; }
    ui::Theme& GetTheme() noexcept { return m_theme; }
    app::AppConfig& GetConfigMutable() noexcept { return m_config; }
    void SetDrawerOpenStateForTest(bool open, int railTab) noexcept {
        m_config.isDrawerOpen = open;
        m_drawerAnimProgress = open ? 1.0f : 0.0f;
        m_config.activeRailTab = railTab;
    }
    bool UpdateMultiFileDiffSelection(editor::OpenDocument& doc, POINT clientPt, bool allowEdgeScroll);
    bool UpdateDiffViewSelection(editor::OpenDocument& doc, POINT clientPt, bool allowEdgeScroll);
    void ToggleBranchDropdown();
    void CloseBranchDropdown();
    void RenderBranchDropdown(ID2D1RenderTarget* pRT, float clientWidth, float clientHeight);
    bool IsBranchDropdownOpen() const noexcept { return m_showBranchDropdown; }
    const std::vector<std::pair<std::wstring, bool>>& GetBranchList() const noexcept { return m_branchList; }
    bool HandleBranchDropdownMouseDown(POINT pt);
    bool HandleBranchDropdownMouseMove(POINT pt);
    void SetStatusBarGitBranchBounds(const D2D1_RECT_F& r) noexcept { m_rcStatusBarGitBranch = r; }

    /** @brief Executes a quick open palette action or command. */
    void ExecuteQuickOpenAction(const std::wstring& actionCmd);

    const AppConfig& GetConfig() const noexcept { return m_config; }
    void SetPrivacyMaskEnabled(bool enabled) noexcept { m_config.enablePrivacyMask = enabled; }
    void SaveSessionStatePublic() { SaveSessionState(); }
    void LoadSessionStatePublic() { LoadSessionState(); }
    void OpenFileDocumentForTest(const std::wstring& path, bool isPreview = false) { OpenFileDocument(path, isPreview); }
    void ProcessPendingFileLoadsForTest() { ApplyPendingFileLoads(); }

    /** @brief Tab context menu, pinning, and detached window helpers. */
    void ToggleTabPin(int groupIdx, int docIdx);
    void CloseOtherTabs(int groupIdx, int keepDocIdx);
    void CloseTabsToRight(int groupIdx, int fromDocIdx);
    void CloseSavedTabs(int groupIdx);
    void SplitTabRight(int groupIdx, int docIdx);
    bool DetachEditorToNewWindow(int groupIdx, int docIdx, bool moveOut);
    bool ApplyDetachHandoffFile(const std::wstring& handoffPath);
    void ToggleActiveStructuredViewer(bool openHexIfBinary = false);

    /**
     * @brief Identifies what subsystem an in-process floating/undocked window hosts.
     */
    enum class FloatingDockKind : int {
        EditorGroup = 0,   ///< Undocked editor tabs (code, AssetViewer, Monitor, ERD, Audit, etc.)
        TerminalPanel = 1, ///< Undocked integrated Terminal panel (preserves live PTY sessions)
        ProblemsPanel = 2  ///< Undocked Workspace Problems / Diagnostics panel
    };

    /**
     * @brief Secondary top-level Win32 window hosting an undocked IDE panel or editor group.
     *
     * Runs in-process with its own ID2D1HwndRenderTarget and ui::Theme so Direct2D brushes
     * and bitmaps stay strictly domain-isolated while sharing workspace IntelliSense, LSP, and PTY state.
     */
    struct FloatingDockWindow {
        uint64_t id = 0;
        FloatingDockKind kind = FloatingDockKind::EditorGroup;
        Window* owner = nullptr;
        HWND hwnd = nullptr;
        ComPtr<ID2D1HwndRenderTarget> pRenderTarget;
        ui::Theme theme;
        std::vector<editor::EditorGroup> editorGroups;
        int focusedGroupIdx = 0;
        editor::EditorSplitState splitState{};

        // Custom frameless floating titlebar bounds & hover tracking
        D2D1_RECT_F rcTitleBar{};
        D2D1_RECT_F rcTitleText{}; ///< Centered title text layout rect
        D2D1_RECT_F rcDockBtn{};   ///< "Attach" pill button (docks back into main window)
        D2D1_RECT_F rcMinBtn{};    ///< Minimize button (-)
        D2D1_RECT_F rcMaxBtn{};    ///< Maximize/Restore button (□)
        D2D1_RECT_F rcCloseBtn{};  ///< Close button (×)
        int hoveredCaptionBtn = 0; ///< 0=None, 1=Dock/Attach, 2=Minimize, 3=Maximize, 4=Close

        void DiscardDeviceResources() noexcept {
            theme.DiscardDeviceResources();
            for (auto& grp : editorGroups) {
                for (auto& doc : grp.docs) {
                    doc.pImageBitmap.Reset();
                    doc.pImageBitmapRT = nullptr;
                    if (doc.assetView) doc.assetView->DiscardDeviceResources();
                }
            }
            pRenderTarget.Reset();
        }
    };

    // In-process Dock / Undock API for tabs, special views, Terminal, and Problems panels
    bool UndockTabToFloatingWindow(int groupIdx, int docIdx, const POINT* pScreenPos = nullptr);
    bool DockFloatingEditorToMainWindow(HWND floatHwnd, int floatGroupIdx = -1, int floatDocIdx = -1, int targetMainGroupIdx = -1);
    bool DockFloatingTabToFloatingWindow(HWND srcFloatHwnd, int srcGroupIdx, int srcDocIdx, HWND dstFloatHwnd, int targetGroupIdx = -1);
    bool UndockTerminalToFloatingWindow(const POINT* pScreenPos = nullptr);
    bool DockTerminalToMainWindow(int targetDockPos = 0);
    bool UndockProblemsToFloatingWindow(const POINT* pScreenPos = nullptr);
    bool DockProblemsToMainWindow();
    [[nodiscard]] size_t GetFloatingWindowCount() const noexcept { return m_floatingWindows.size(); }
    [[nodiscard]] FloatingDockWindow* FindFloatingWindow(HWND hwnd) noexcept;
    [[nodiscard]] const FloatingDockWindow* FindFloatingWindow(HWND hwnd) const noexcept;
    [[nodiscard]] const std::vector<std::unique_ptr<FloatingDockWindow>>& GetFloatingWindows() const noexcept { return m_floatingWindows; }
    [[nodiscard]] bool IsProblemsPanelFloated() const noexcept { return m_problemsPanelFloated; }
    [[nodiscard]] bool ShouldHideFloatingTabBar() const noexcept {
        return m_activeFloatingWindow &&
               m_activeFloatingWindow->kind == FloatingDockKind::EditorGroup &&
               m_editorGroups.size() == 1 &&
               m_editorGroups[0].docs.size() <= 1;
    }
    [[nodiscard]] float EffectiveTabBarHeight() const noexcept {
        return ShouldHideFloatingTabBar() ? 0.0f : m_config.ScaleF(34.0f);
    }
    [[nodiscard]] HWND GetMainPostHwnd() const noexcept { return m_mainHwnd ? m_mainHwnd : m_hwnd.load(); }
    void InvalidateFloatingWindows() noexcept;
    void DestroyAllFloatingWindows() noexcept;
    void ProcessPendingFloatingActions();
    void SyncThemeAndFontToFloatingWindows();
    static LRESULT CALLBACK FloatingWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleFloatingMessage(FloatingDockWindow& fw, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void RenderFloatingWindow(FloatingDockWindow& fw);
    void RenderFloatingTitleBar(ID2D1RenderTarget* pRT, FloatingDockWindow& fw, float clientW);

private:
    /**
     * @brief Checks if there is at least one active document opened in the editor.
     * @return True if a document is open.
     */
    [[nodiscard]] bool HasOpenFile() const noexcept {
        return (GetActiveDoc() != nullptr);
    }

    [[nodiscard]] bool HasActiveFileEditor() const noexcept {
        auto* pDoc = GetActiveDoc();
        return (pDoc != nullptr && !pDoc->isSpecialView && !GetActiveGroup().docs.empty());
    }

    /**
     * @brief Opens a physical file from disk into the editor tab set.
     * @param path Full filesystem path to the file.
     * @param isPreview If true, opens as a temporary preview tab that is replaced by subsequent clicks unless edited.
     */
    void OpenFileDocument(const std::wstring& path, bool isPreview = false);

    /**
     * @brief Opens a side-by-side diff comparison between HEAD/Index and working tree (or HEAD vs Staged Index).
     */
    void OpenDiffDocument(const std::wstring& relPath, bool isPreview = false, bool isStagedMode = false);
    void OpenCompareFilesDiffDocument(const std::wstring& leftPath, const std::wstring& rightPath);
    void OpenThreeWayMergeDocument(const std::wstring& filePath);

    /**
     * @brief Opens a historical commit diff comparison between parent commit and target commit (Image 5).
     */
    void OpenCommitDiffDocument(const std::wstring& commitHash, const std::wstring& relPath = L"");

    /**
     * @brief Synchronizes buffer contents across all open tabs (normal docs and diff views) matching sourceDoc's filePath.
     */
    void SyncDocumentBuffers(editor::OpenDocument* sourceDoc);

    /**
     * @brief Opens a dedicated special full-surface view (e.g. Keyboard Shortcuts).
     * @param viewType Unique identifier of the view (e.g. L"keybindings").
     * @param title Title displayed on the tab header.
     */
    void OpenCourseTab(const std::wstring& courseTitle, const std::wstring& courseId);
    void ToggleMarkdownPreview();
    void ToggleCodeLens();
    void OpenMarkdownPreviewSplit(const std::wstring& path);
    void CloseMarkdownPreviewSplit();
    void OpenExtensionDetail(const std::wstring& extId);
    void RunCodeLens(size_t bufferLine, bool isDebug);
    void HandleCodeLensClick(const ui::EditorCanvas::CodeLensAction& act);
    static bool IsMarkdownFile(const std::wstring& path);
    void SyncActiveTabWithDrawer();
    void ClearExplorerSelectionOverride() noexcept;

    /**
     * @brief Opens an interactive Jupyter / Colab-style notebook tab (.ipynb).
     * @param filePath Optional path to an existing .ipynb file. If empty, creates a new scratchpad notebook.
     */
    void OpenNotebookView(const std::wstring& filePath = L"");

    /**
     * @brief Closes the open document at the given group and tab index.
     * Auto-collapses the editor group if it becomes empty and multiple groups exist.
     * @param groupIdx 0-based editor group index.
     * @param docIdx 0-based document tab index inside that group.
     */
    void CloseDocument(int groupIdx, int docIdx, bool forceClosePinned = true);

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

    /** @brief Keeps cursor within horizontal viewport bounds when typing or navigating. */
    void EnsureCursorVisible(bool centerVertically = false);

    // --- File & Window Action Handlers ---

    /** @brief Creates a new empty untitled text file document (Ctrl+N). */
    void DoNewTextFile();

    void UpdateCssPeekHover(POINT clientPt);
    std::vector<std::wstring> FindCssDefinition(const std::wstring& selector, const std::wstring& currentFilePath);
    static std::vector<std::wstring> SearchCssInText(const std::wstring& selector, const std::wstring& cssText);
    bool TryAutoRenameMatchingTag(editor::OpenDocument* pDoc, size_t editLine, const std::wstring& oldLineText);

    /** @brief Creates a new custom file prompt / untitled file (Ctrl+Alt+N). */
    void DoNewFile();

    /** @brief Spawns an independent non-maximized new application window (Ctrl+Shift+N). */
    void DoNewWindow();

    /** @brief Prompts with Open File dialog and loads chosen file into editor (Ctrl+O). */
    void DoOpenFile();

    /** @brief Prompts with Open Folder dialog and populates explorer tree (Ctrl+K Ctrl+O). */
    void DoOpenFolder();

    /** @brief Stages changes and opens temporary .git/COMMIT_EDITMSG editor tab when commit has empty message. */
    void DoOpenCommitEditMsg();

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

    /** @brief Jumps the active editor document cursor and viewport to the target 1-based line number. */
    void JumpToEditorLine(size_t targetLine);

    /** @brief Saves current buffer changes to disk (Ctrl+S). */
    void DoSaveFile();

    /** @brief Prompts with Save As dialog to save current buffer under a new path (Ctrl+Shift+S). */
    void DoSaveFileAs();

    /** @brief Saves all modified/dirty open documents to disk (Ctrl+K S). */
    void DoSaveAll();

    /** @brief Ensures GitLens line-by-line blame metadata is cached for the document. */
    void EnsureGitBlameLoaded(editor::OpenDocument& doc);
    struct PendingGitBlameResult {
        std::wstring docPath;
        std::unordered_map<size_t, editor::OpenDocument::LineBlameInfo> blameMap;
    };
    std::mutex m_blameMutex;
    std::vector<PendingGitBlameResult> m_pendingGitBlames;
    void DoNavigateDiffHunk(bool backward = false);
    void StageSelectedGitRange();
    void OpenDiffPeekAtLine(editor::OpenDocument& doc, size_t bufferLine);
    void OpenActiveFileHistory();
    void RefreshActiveFileHistoryAsync(const std::wstring& fullPath);
    struct PendingFileHistoryResult {
        std::wstring fullPath;
        std::wstring relPath;
        std::vector<phide::git::GitWorkflowEngine::FileHistoryEntry> entries;
    };
    std::mutex m_fileHistoryMutex;
    std::vector<PendingFileHistoryResult> m_pendingFileHistory;
    void OpenMergeConflictNext(editor::OpenDocument& doc);

    /** @brief Exports user IDE profile settings to a JSON file. */
    void DoExportProfile();

    /** @brief Imports user IDE profile settings from a JSON file. */
    void DoImportProfile();

    /** @brief Opens user settings view or preferences. */
    void DoOpenSettings();

    /** @brief Opens the interactive Keyboard Shortcuts view with keycap badges (Ctrl+K Ctrl+S). */
    void DoOpenKeyboardShortcuts();

    /** @brief Opens the specified categorized PHIDE log file inside the built-in Log Tail viewer. */
    void DoShowLog(LogCategory category);

    /** @brief Opens the %APPDATA%\phide\logs folder in Windows File Explorer. */
    void DoOpenLogsFolder();

    /** @brief Opens user code snippets configuration. */
    void DoOpenUserSnippets();

    /** @brief Opens or toggles the Theme Picker modal dialog (Ctrl+K Ctrl+T). */
    void DoToggleThemeModal();

    /** @brief Opens the Quick Open file search palette modal (Ctrl+K, Ctrl+P). */
    void DoQuickOpenFileSearch();

    /** @brief Opens the Database (phpMyAdmin) configuration settings modal. */
    void DoOpenDatabaseSettings(ui::DatabaseModalTab initialTab = ui::DatabaseModalTab::PhpMyAdmin);

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

    /** @brief Shows Git error modal dialog on non-zero exit code or fatal error (matching Image 2). */
    void ShowGitError(const std::wstring& cmd, const std::wstring& output, const std::wstring& repoDir);

    /** @brief Writes command and output to temporary file in .git/ and opens as an editor tab (matching Image 5). */
    void ShowGitCommandOutput(const std::wstring& cmd, const std::wstring& output, const std::wstring& repoDir);

    /** @brief Opens terminal bottom panel and activates Git real-time command log tab (matching Images 3 & 4). */
    void OpenGitLogTerminal();

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

    /** @brief Focuses a specific editor split group (0-based). */
    void FocusEditorGroup(int groupIdx);

    /** @brief Cycles keyboard focus to next or previous editor split group. */
    void FocusAdjacentEditorGroup(bool next);

    /** @brief Opens inline Peek Definition / References widget under cursor (Alt+F12). */
    void DoPeekDefinition();

    /** @brief Jumps to type definition of symbol under cursor. */
    void DoTypeDefinition();

    /** @brief Shows LSP Type Hierarchy (supertypes / subtypes) peek for symbol under cursor. */
    void DoTypeHierarchy();

    /** @brief Jumps to implementation of symbol under cursor. */
    void DoImplementation();

    /** @brief Finds all references of symbol under cursor (Shift+F12). */
    void DoFindReferences();

    /** @brief Opens Call Hierarchy view for symbol under cursor. */
    void DoCallHierarchy();

    /** @brief Computes and opens Code Metrics report for current file & workspace. */
    void DoCodeMetrics();

    /** @brief Opens Document Symbol quick outline (@) or focuses Outline pane (Ctrl+Shift+O). */
    void DoGoToSymbolInEditor();

    /** @brief Opens Workspace Symbol search (#) in Quick Open (Ctrl+T). */
    void DoGoToWorkspaceSymbol();

    /** @brief Toggles Explorer Outline tree section. */
    void DoToggleOutline();

    /** @brief Navigates backward in cursor history stack (Alt+Left). */
    void NavigateBack();

    /** @brief Navigates forward in cursor history stack (Alt+Right). */
    void NavigateForward();

    /** @brief Records current file/line/col checkpoint in navigation history. */
    void RecordNavCheckpoint();

    /** @brief Toggles a bookmark on the active document's cursor line. */
    void DoToggleBookmark();

    /** @brief Jumps to the next bookmark in the active document. */
    void DoNextBookmark();

    /** @brief Jumps to the previous bookmark in the active document. */
    void DoPrevBookmark();

    /** @brief Clears all bookmarks in the active document. */
    void DoClearBookmarks();

    /** @brief Persists all open-document bookmarks to .phide/bookmarks.json (ignoring .phide). */
    void SaveBookmarksForWorkspace();

    /** @brief Restores persisted bookmarks for currently open documents. */
    void LoadBookmarksForWorkspace();

    /** @brief Jumps to 0-based line/col in the currently active document. */
    void JumpToEditorLocation(size_t line0Based, size_t col0Based);

    /** @brief Opens (or focuses) filePath and jumps to 0-based line/col. */
    void JumpToFileAndLocation(const std::wstring& filePath, size_t line0Based, size_t col0Based);

    /** @brief Refreshes Outline panel, Breadcrumbs symbols, and QuickOpen symbols for active doc. */
    void RefreshActiveDocOutlineAndBreadcrumbs();

    /** @brief Applies replacement for the active inline find match. */
    void ApplyInlineFindReplaceCurrent();

    /** @brief Applies replacement for all inline find matches (or in-selection). */
    void ApplyInlineFindReplaceAll();

    /** @brief Applies multi-file symbol rename from the inline rename widget. */
    void ApplyCrossFileRename(const std::wstring& newName);

    /** @brief Toggles line comment on active line or selection (Ctrl+/). */
    void DoToggleLineComment();

    /** @brief Toggles block comment on active line or selection (Shift+Alt+A). */
    void DoToggleBlockComment();

    /** @brief Expands Emmet abbreviation before cursor (Tab). */
    void DoEmmetExpand();

    /** @brief Wraps current selection/line with a prompted Emmet abbreviation. */
    void DoEmmetWrapWithAbbreviation();

    /** @brief Balances HTML/JSX/XML tag selection inward or outward. */
    void DoEmmetBalance(bool outward);

    /** @brief Transforms selection (or word at cursor) case across single or multi-cursors. */
    void DoTransformCase(editor::CaseTransform mode);

    /** @brief Formats active document (Shift+Alt+F). */
    void DoFormatDocument(bool showFeedback = true);

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

    /** @brief Synchronizes File Network Graph View with filesystem creation, update, rename, delete, move, or save. */
    void NotifyGraphViewFilesystemChanged();

    /** @brief Undoes the most recent Explorer operation (rename, delete, create, move) (Ctrl+Z when explorer active). */
    void DoExplorerUndo();

    /** @brief Redoes the most recently undone Explorer operation (Ctrl+Y when explorer active). */
    void DoExplorerRedo();



    /** @brief Selects all text in current document (Ctrl+A). */
    void DoSelectAll();

    /** @brief Selects and highlights all occurrences of active word or selection (Ctrl+F2). */
    void DoChangeAllOccurrences();

    /** @brief Displays refactoring options or 'No refactorings available' popover balloon (Ctrl+Shift+R). */
    void DoRefactor();

    /** @brief Opens Command Palette / Keybindings lookup (Ctrl+Shift+P). */
    void DoCommandPalette();

    /** @brief Moves current line or selection up/down (Alt+Up / Alt+Down). */
    void DoMoveLines(bool up);

    /** @brief Duplicates current line or selection up/down (Shift+Alt+Up / Shift+Alt+Down). */
    void DoDuplicateLines(bool up);

    /** @brief Joins current line with the next line or joins selected lines (Ctrl+J). */
    void DoJoinLines();

    /** @brief Sorts selected lines (or all lines when no selection) alphabetically. */
    void DoSortLines(bool ascending);

    /** @brief Trims trailing spaces and tabs on selected lines or the whole document. */
    void DoTrimTrailingWhitespace();

    /** @brief Folds all foldable blocks and #region sections in the active editor. */
    void DoFoldAll();

    /** @brief Expands all folded regions in the active editor. */
    void DoUnfoldAll();

    /** @brief Folds code blocks at or deeper than 1-based nesting level. */
    void DoFoldLevel(int level);

    /** @brief Jumps cursor to matching bracket or HTML/PHP block delimiter (Ctrl+Shift+\). */
    void DoJumpToMatchingBracket();

    /** @brief Opens the clickable status-bar Encoding / End-of-Line switcher popup. */
    void OpenStatusBarEncodingPopup();

    /** @brief Opens the clickable status-bar Indentation switcher popup. */
    void OpenStatusBarIndentationPopup();

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

    /** @brief Crash-safe Hot Exit: persists dirty and untitled buffers to %APPDATA%\phide\recovery\. */
    void SaveDirtyBuffersRecovery();
    void RestoreDirtyBuffersRecovery();
    void ClearDirtyBuffersRecovery();

    /** @brief Computes absolute path to the session persistence file in %APPDATA%\phide\session.json. */
    std::wstring GetSessionFilePath() const;

    /** @brief Computes absolute path to the user snippets configuration file in %APPDATA%\phide\snippets\snippets.json. */
    std::wstring GetSnippetsConfigPath() const;

    /** @brief Computes absolute path to %APPDATA%\phide\user_keybindings.json. */
    std::wstring GetKeybindingsConfigPath() const;

    /** @brief Effective header height in physical pixels (0 in Zen Mode, 34px inside floating dock windows). */
    [[nodiscard]] float EffectiveHeaderHeight() const noexcept {
        if (m_activeFloatingWindow) return m_config.ScaleF(34.0f);
        return m_config.zenMode ? 0.0f : m_config.ScaleF(static_cast<float>(m_config.headerHeight));
    }

    /** @brief Auto-save mode predicates distinguishing off / afterDelay / onFocusChange / onWindowChange. */
    [[nodiscard]] bool ShouldAutoSaveAfterDelay() const noexcept {
        return m_autoSave && m_config.autoSave == L"afterDelay";
    }
    [[nodiscard]] bool ShouldAutoSaveOnFocusChange() const noexcept {
        return m_autoSave && (m_config.autoSave == L"afterDelay" || m_config.autoSave == L"onFocusChange");
    }
    [[nodiscard]] bool ShouldAutoSaveOnWindowChange() const noexcept {
        return m_autoSave && m_config.autoSave != L"off";
    }

    /** @brief Applies formatOnSave (when allowed), .editorconfig rules, trimTrailingWhitespace, and insertFinalNewline. */
    void ApplySaveHooks(editor::OpenDocument& doc, const std::wstring& targetPath, bool allowFormatter);

    /** @brief Effective indentation size/mode combining global config and per-file .editorconfig. */
    [[nodiscard]] int GetDocTabSize(const editor::OpenDocument* pDoc) const;
    [[nodiscard]] bool GetDocInsertSpaces(const editor::OpenDocument* pDoc) const;

    /** @brief Loads/saves .phide/settings.json and multi-root .phide-workspace state cleanly. */
    void ApplyWorkspaceSettingsToConfig(const workspace::WorkspaceSettingsOverride& settings);
    void LoadAndApplyWorkspaceSettings(const std::wstring& workspaceRoot);
    void SaveCurrentWorkspaceSetting(const std::wstring& settingId);
    void LoadUserKeybindings();
    void SaveUserKeybindings() const;

    /** @brief Starts background filesystem watcher on active workspace for event-driven Git & tree reactivity. */
    void StartWorkspaceWatcher(const std::wstring& folderPath);
    void StopWorkspaceWatcher();

    /** @brief Queues a debounced Git status refresh (e.g. 2s after typing/saving stops). */
    void TriggerDebouncedGitRefresh(DWORD delayMs = 2000);

    /** @brief Synchronizes all open in-memory dirty documents into the Source Control drawer (Images 3 & 4). */
    void UpdateDrawerDirtyFiles();

    /** @brief Synchronizes documents with syntax/diagnostic errors into the Explorer drawer (Image 3). */
    void UpdateDrawerErrorFiles();
    void UpdatePrediction();
    [[nodiscard]] bool IsCursorInComment(const editor::OpenDocument* pDoc) const;

    /** @brief Commits selected autocomplete item or HTML tag block (Images 1 & 5). */
    void CommitAutocompleteSelection();

    // Atomic wrapper for active HWND so temporary floating-context swaps remain data-race free with background readers
    struct AtomicHwnd {
        std::atomic<HWND> val{ nullptr };
        AtomicHwnd() noexcept = default;
        AtomicHwnd(HWND h) noexcept : val(h) {}
        AtomicHwnd& operator=(HWND h) noexcept {
            val.store(h, std::memory_order_release);
            return *this;
        }
        operator HWND() const noexcept { return val.load(std::memory_order_acquire); }
        explicit operator bool() const noexcept { return val.load(std::memory_order_acquire) != nullptr; }
        [[nodiscard]] HWND load() const noexcept { return val.load(std::memory_order_acquire); }
        void store(HWND h) noexcept { val.store(h, std::memory_order_release); }
    };

    // Win32 & D2D handles
    AtomicHwnd m_hwnd{};
    HWND m_mainHwnd = nullptr;
    ComPtr<ID2D1Factory> m_pD2DFactory;
    ComPtr<ID2D1HwndRenderTarget> m_pRenderTarget;
    ComPtr<IDWriteFactory> m_pDWriteFactory;

    // Floating dock window state
    struct ScopedFloatingEditorContext;
    friend struct ScopedFloatingEditorContext;
    struct PendingFloatingAction {
        enum class Kind {
            DockAllEditorToMain = 0,
            DockOneTabToMain = 1,
            CloseFloatingEditor = 2,
            DockTerminal = 3,
            CloseFloatingTerminal = 4,
            DockProblems = 5,
            CloseFloatingProblems = 6,
            UndockTabFromMain = 7,
            MoveFloatingTabToOtherFloating = 8
        } kind = Kind::DockAllEditorToMain;
        uint64_t winId = 0;
        uint64_t targetWinId = 0;
        int groupIdx = -1;
        int docIdx = -1;
        int targetGroupIdx = -1;
        POINT screenPt{};
    };
    std::vector<std::unique_ptr<FloatingDockWindow>> m_floatingWindows;
    uint64_t m_nextFloatingWinId = 1;
    FloatingDockWindow* m_activeFloatingWindow = nullptr;
    HWND m_lastActiveEditorHwnd = nullptr;
    HWND m_popupMenuOwnerHwnd = nullptr;
    HWND m_editorOverlayOwnerHwnd = nullptr;
    bool m_problemsPanelFloated = false;
    D2D1_RECT_F m_rcProblemsUndockBtn{};
    std::vector<PendingFloatingAction> m_pendingFloatingActions;
    bool m_processingFloatingActions = false;
    HRESULT CreateFloatingDeviceResources(FloatingDockWindow& fw);
    bool EnsureFloatingWindowClassRegistered(HINSTANCE hInst);
    HWND CreateFloatingDockHwnd(FloatingDockWindow& fw, const std::wstring& title, int width, int height, const POINT* pScreenPos);

    // Subsystems
    AppConfig m_config;
    ui::Theme m_theme;
    ui::TitleHeader m_header;
    ui::PopupMenu m_popupMenu;
    ui::ActivityRail m_rail;
    ui::DrawerPanel m_drawer;
    ui::EditorCanvas m_editor;
    ui::ThemePickerModal m_themePicker;
    ui::ColorPickerModal m_colorPicker;
    ui::QuickOpenModal m_quickOpen;
    ui::ConfirmDiscardModal m_confirmDiscardModal;
    ui::GitErrorModal m_gitErrorModal;
    struct PendingGitError { std::wstring command; std::wstring output; std::wstring repoDir; };
    std::mutex m_gitErrorMutex;
    std::optional<PendingGitError> m_pendingGitError;
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
    void ExecuteDiscardOrRestoreAsync(const std::wstring& targetPath, const std::wstring& targetFileName, const std::wstring& targetStatus);
    void FinishDiscardOrRestore(const std::wstring& targetPath, const std::wstring& targetFileName, const std::wstring& targetStatus);
    void ExecuteDeleteAsync(const std::wstring& targetPath, bool isFolder);
    void FinishDelete(const std::wstring& targetPath, bool isFolder, bool success);

    // Remote Explorer & Remote Window Modal helpers
    ui::RemoteWindowModal m_remoteModal;
    ui::remote::RemoteWorkspaceSession m_activeRemoteSession;
    std::wstring m_activeWorkspaceFilePath;
    workspace::WorkspaceSettingsOverride m_workspaceSettings;
    ui::LucideIcons m_remoteStatusLucide;
    void SetupRemoteCallbacks();
    void SyncRemoteModalTargetCache();
    void OpenRemoteWindowPopup(ui::RemoteModalMode mode = ui::RemoteModalMode::MainMenu, bool sshCurrentWindow = false);
    void ConnectRemoteSshTerminal(const ui::remote::SshHostEntry& host, const std::wstring& initialDir);
    void OpenRemoteSshFile(const ui::remote::SshHostEntry& host, const std::wstring& remotePath, const std::wstring& localCachePath);
    void OpenRemoteContainerFile(const ui::remote::DevContainerEntry& container, const std::wstring& remotePath, const std::wstring& localCachePath);
    void OpenVirtualTextTab(const std::wstring& title, const std::wstring& content);
    void SyncRemoteDocumentIfNeeded(const editor::OpenDocument& doc);
    void OpenSshConfigFile();
    void SetupOrCopySshKey();
    void ConnectWslDistro(const std::wstring& distroName, bool openFolderInExplorer);
    void InstallWslInTerminal();
    void AttachDevContainer(const ui::remote::DevContainerEntry& container);
    void OpenOrCreateDevContainerConfig();

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
        bool waitForTerminalReady = false;  ///< True for terminal dev servers: wait for terminal ready signal before opening browser
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

    // Background web server processes (PHP, Nginx, phpMyAdmin, MySQL)
    HANDLE m_hPhpServerProcess = nullptr;
    std::wstring m_preferredPhpExe;
    HANDLE m_hNginxProcess = nullptr;
    HANDLE m_hPmaProcess = nullptr;
    HANDLE m_hMysqlProcess = nullptr;
    int m_mysqlPort = 3306;
    bool m_isPmaRunning = false;
    void UpdateDatabaseModalRuntimeStatus();
    void RunManualGarbageCollection(bool aggressive = true);
    void StartPhpServer(const std::wstring& rootDir, int port = 8080);
    void StopPhpServer();
    void StartNginxServer();
    void StopNginxServer();
    void StartPmaServer();
    void StopPmaServer();
    void StartMysqlServer();
    void StopMysqlServer();

    server::LiveServer m_liveServer; // backwards compatibility / fallback
    D2D1_RECT_F m_liveServerPillBounds{};
    D2D1_RECT_F m_liveServerCloseBounds{};
    bool m_hoverLiveServerPill = false;
    bool m_hoverLiveServerClose = false;
    workspace::WorkspaceManager m_workspace;
    workspace::ExplorerUndoManager m_explorerUndo;
    editor::SnippetManager m_snippetManager;
    editor::IntelliSenseEngine m_intelliSense;
    editor::PredictionEngine m_predictionEngine;
    editor::AdaptiveCodeEngine m_adaptiveEngine;
    editor::WorkspaceSymbolIndex m_symbolIndex;
    lsp::LspManager m_lsp;

    struct NavCheckpoint {
        std::wstring filePath;
        size_t line = 0;
        size_t col = 0;
    };
    std::vector<NavCheckpoint> m_navBackStack;
    std::vector<NavCheckpoint> m_navForwardStack;
    bool m_isNavigatingHistory = false;
    std::wstring m_lastOutlineFileKey;
    size_t m_lastOutlineDocVersion = static_cast<size_t>(-1);
    std::unordered_map<std::wstring, std::vector<size_t>> m_persistedBookmarks;

    // Filesystem Watcher Handles
    HANDLE m_hWatcherThread = nullptr;
    HANDLE m_hWatcherStopEvent = nullptr;

    // Multi-Editor Groups Architecture (2D Resizable Split Layouts, up to 4 panes)
    std::vector<editor::EditorGroup> m_editorGroups;
    int m_focusedGroupIdx = 0;
    int m_untitledCounter = 0;
    editor::EditorSplitState m_splitState{};
    int m_activeEditorSplitter = 0;
    void SetEditorSplitLayout(editor::EditorSplitLayout mode);
    void UpdateEditorGroupBounds() noexcept;
    [[nodiscard]] int FindEditorGroupAtPoint(POINT pt) const noexcept;
    [[nodiscard]] int HitTestEditorSplitter(POINT pt) const noexcept;
    void TransitionLayoutOnGroupRemoved(int removedGroupIdx) noexcept;
    void CollapseEmptyEditorGroups() noexcept;
    void EnsureAutoImportAndUseClient(editor::OpenDocument* pDoc, const std::wstring& symbolOrHook, const std::wstring& moduleSpec = L"react");
    void ExecuteRenameAndUpdateImports(const std::wstring& oldPath, const std::wstring& newPath, bool isMove);

    bool m_trackingMouse = false;
    bool m_wordWrap = false;
    bool m_autoSave = true;
    bool m_chordCtrlK = false;
    bool m_suppressNextChar = false;
    bool m_isCleanNewWindow = false;
    bool m_isRestoringSession = false;
    bool m_isDraggingGitSplitter = false;
    bool m_gitRefreshPending = false;
    ULONGLONG m_gitRefreshTargetTime = 0;

    // External Disk-Change Detector
    void CheckOpenDocumentsDiskChanges();
    void OnDocumentDiskSaved(editor::OpenDocument& doc);

    // Workspace .env + Live MySQL Schema IntelliSense
    void RefreshWorkspaceEnvVars();
    void RefreshDatabaseSchemaAsync();
    void SyncActiveSqlSchemaToIntelliSense();
    void RunExtraDocDiagnostics(const editor::OpenDocument& doc, std::vector<lsp::Diagnostic>& diags) const;
    void RefreshEnvDriftDiagnostics();
    void DoSyncEnvKeysToExample();
    std::unordered_set<std::wstring> m_cachedKnownEnvKeys;
    bool m_hasWorkspaceEnvFile = false;
    std::vector<editor::ProblemEntry> m_envDriftProblems;
    std::atomic<bool> m_isRefreshingDbSchema{false};
    std::atomic<bool> m_isDestroying{false};
    std::mutex m_bgWorkersMutex;
    std::condition_variable m_bgWorkersCv;
    size_t m_activeBgWorkers{0};
    std::mutex m_pendingLspDiagMutex;
    struct PendingLspDiagUpdate {
        std::wstring filePath;
        std::vector<lsp::Diagnostic> diags;
    };
    struct PendingLspCompletionResult {
        uint64_t gen = 0;
        std::wstring filePath;
        size_t line = 0;
        size_t col = 0;
        std::vector<lsp::CompletionItem> lspItems;
    };
    struct CachedLspCompletions {
        std::wstring filePath;
        size_t line = 0;
        size_t col = 0;
        std::vector<lsp::CompletionItem> items;
    } m_cachedLspCompletions;
    std::vector<PendingLspDiagUpdate> m_pendingLspDiags;
    std::atomic<uint64_t> m_lspCompletionGen{0};
    void StartLspForDocumentAsync(const std::wstring& filePath, const std::wstring& text);
    void NotifyLspDidChangeAsync(const std::wstring& filePath, const std::wstring& text);
    void RequestLspCompletionsAsync(const editor::OpenDocument* pDoc, const std::wstring& lineText, size_t col);
    void CancelPendingLspCompletions() noexcept { m_lspCompletionGen.fetch_add(1); }
    [[nodiscard]] std::vector<editor::IntelliSenseItem> QueryCombinedCompletions(
        const editor::OpenDocument* pDoc, const std::wstring& lineText, size_t col);

    // --- #19-#25 Multi-LSP & Language Intelligence Async Coordinators ---
    struct PendingLspHoverResult {
        uint64_t gen = 0;
        int groupIdx = -1;
        std::wstring filePath;
        size_t line = 0;
        size_t col = 0;
        POINT clientPt{};
        lsp::HoverResult hover;
    };
    struct PendingLspSigHelpResult {
        uint64_t gen = 0;
        std::wstring filePath;
        lsp::SignatureHelpResult sigHelp;
    };
    struct PendingLspCodeActionResult {
        uint64_t gen = 0;
        std::wstring filePath;
        size_t line = 0;
        bool openMenu = false;
        bool isRefactorMode = false;
        std::vector<lsp::CodeActionItem> actions;
    };
    struct PendingLangIntelUpdate {
        uint64_t gen = 0;
        std::wstring filePath;
        uint64_t docVersion = 0;
        std::vector<editor::OpenDocument::InlayHint> inlayHints;
        std::unordered_map<size_t, editor::OpenDocument::CodeLensMeta> codeLensMap;
        bool hasSemanticTokens = false;
        std::vector<uint32_t> semanticTokensData;
        bool hasLspFolds = false;
        std::vector<lsp::LspFoldRange> lspFoldingRanges;
        bool hasLspCodeLenses = false;
        std::vector<lsp::LspCodeLens> lspCodeLenses;
        bool hasLinterResults = false;
        std::vector<editor::ProblemEntry> linterProblems;
    };
    std::atomic<uint64_t> m_lspHoverGen{0};
    std::atomic<uint64_t> m_lspSigHelpGen{0};
    std::atomic<uint64_t> m_lspCodeActionGen{0};
    std::atomic<uint64_t> m_langIntelGen{0};
    bool m_hoverDwellPending = false;
    ULONGLONG m_hoverDwellTargetTime = 0;
    POINT m_hoverDwellPt{};

    void PushCustomLspServerPaths();
    [[nodiscard]] bool IsOverlayBlockingHover() const noexcept;
    void ScheduleHoverLookup(POINT clientPt);
    void RequestHoverAsync(editor::OpenDocument* pDoc, size_t line, size_t col, POINT clientPt, int groupIdx = -1);
    void TriggerSignatureHelpAsync(editor::OpenDocument* pDoc, bool forceShow = false);
    void RequestCodeActionsAsync(editor::OpenDocument* pDoc, bool openMenu, bool isRefactorMode);
    void DoCodeActions();
    void ApplyCodeActionItem(editor::OpenDocument* pDoc, lsp::CodeActionItem action);
    void ScheduleLangIntelAndLintersAsync(editor::OpenDocument* pDoc, bool runExternalLinters, bool forceFreshLinters = false);
    void ClearStaleExternalLinterProblemsForFile(const std::wstring& filePath);

    // --- #23 Bottom Filterable Problems Panel ---
    bool m_problemsPanelOpen = false;
    bool m_bottomProblemsActive = false;
    float m_problemsPanelHeight = 220.0f;
    bool m_isResizingProblemsPanel = false;
    float m_problemsScrollY = 0.0f;
    float m_problemsMaxScrollY = 0.0f;
    int m_problemsSelectedIdx = -1;
    bool m_problemsSearchFocused = false;
    editor::ProblemsFilterState m_problemsFilter;
    std::unordered_map<std::wstring, std::vector<editor::ProblemEntry>> m_externalLinterProblems;
    std::vector<editor::ProblemEntry> m_cachedFilteredProblems;
    std::vector<std::wstring> m_problemsCollapsedFiles;
    std::vector<editor::ProblemDisplayRow> m_problemsDisplayRows;
    int m_totalProblemErrors = 0;
    int m_totalProblemWarnings = 0;
    int m_totalProblemInfos = 0;
    D2D1_RECT_F m_rcProblemsPanel{};
    D2D1_RECT_F m_rcProblemsResizeBorder{};
    D2D1_RECT_F m_rcProblemsTabBtn{};
    D2D1_RECT_F m_rcProblemsTermTabBtn{};
    D2D1_RECT_F m_rcProblemsFilterAll{};
    D2D1_RECT_F m_rcProblemsFilterErr{};
    D2D1_RECT_F m_rcProblemsFilterWarn{};
    D2D1_RECT_F m_rcProblemsFilterInfo{};
    D2D1_RECT_F m_rcProblemsCurFileBtn{};
    D2D1_RECT_F m_rcProblemsGroupBtn{};
    D2D1_RECT_F m_rcProblemsSarifBtn{};
    D2D1_RECT_F m_rcProblemsRefreshBtn{};
    D2D1_RECT_F m_rcProblemsSearchBox{};
    D2D1_RECT_F m_rcProblemsCloseBtn{};
    std::vector<D2D1_RECT_F> m_problemsRowBounds;
    std::vector<D2D1_RECT_F> m_problemsFixBtnBounds;

    [[nodiscard]] bool IsBottomProblemsVisible() const noexcept {
        if (m_activeFloatingWindow || m_problemsPanelFloated) return false;
        return !m_config.zenMode && m_problemsPanelOpen && m_bottomProblemsActive;
    }
    void ToggleProblemsPanel(bool forceOpen = false);
    void CloseProblemsPanel();
    void SwitchBottomDockToTerminal();
    void RebuildProblemsCache();
    void NavigateNextProblem(bool backward = false);
    void ApplyQuickFixForProblem(const editor::ProblemEntry& prob);
    void ExportProblemsSarif();
    void RenderBottomProblemsPanel(ID2D1RenderTarget* pRT, float clientW, float clientH, float statusBarH, float railLeft);
    void RenderProblemsPanelInRect(ID2D1RenderTarget* pRT, ui::Theme& theme, const D2D1_RECT_F& panelRect, bool isFloating = false);
    bool HandleProblemsPanelMouseDown(POINT pt, bool isFloatingOverride = false);
    bool HandleProblemsPanelMouseWheel(POINT pt, short delta);
    bool HandleProblemsPanelChar(wchar_t ch);
    bool HandleProblemsPanelKeyDown(WPARAM vk);

    // --- #26-#30 Dev Workflow Integration (DAP / Tasks DAG / Test Explorer / Conditional BPs) ---
    enum class InlinePromptMode {
        Breakpoint = 0,
        EmmetWrap = 1
    };
    bool m_bpPromptOpen = false;
    InlinePromptMode m_bpPromptMode = InlinePromptMode::Breakpoint;
    size_t m_bpPromptLine0 = 0;
    std::wstring m_bpPromptInput;
    D2D1_RECT_F m_rcBpPromptBox{};
    D2D1_RECT_F m_rcBpPromptSaveBtn{};
    D2D1_RECT_F m_rcBpPromptCancelBtn{};
    void OpenBreakpointConditionPrompt(size_t line0);
    void SubmitBreakpointConditionPrompt();
    void RenderBreakpointConditionPrompt(float clientW, float clientH);
    [[nodiscard]] std::vector<phide::dev::DapBreakpoint> BuildDapBreakpointsForDoc(const editor::OpenDocument& doc) const;
    [[nodiscard]] std::vector<phide::dev::DapBreakpoint> BuildAllOpenDapBreakpoints() const;
    void SyncAllBreakpointsToDebugger();
    void ApplyDebugPausedUiState(const std::wstring& filePath, int lineNum1);
    void ClearDebugPausedUiState(bool sessionEnded);
    void JumpToDebugFileLine(const std::wstring& filePath, int line1Based);
    bool IsLineBreakpointAllowedForDoc(const editor::OpenDocument& doc, size_t line0) const;
    std::mutex m_debugUiMutex;
    std::wstring m_pendingDebugPausedFile;
    int m_pendingDebugPausedLine = 1;
    bool m_hasPendingDebugPaused = false;
    bool ExecuteWorkspaceTaskByLabel(const std::wstring& taskLabel);
    void LaunchDebugWithConfig(const phide::dev::LaunchConfig& cfg);
    void RefreshWorkspaceTestsAsync();
    void RunWorkspaceTestsAsync(const std::wstring& filterName = L"",
                                phide::dev::TestFramework fw = phide::dev::TestFramework::Unknown,
                                const std::wstring& filePath = L"");
    void StopWorkspaceTests();
    void RunFailedWorkspaceTestsAsync();
    void RefreshWorkspaceCoverageAsync();
    void RefreshWorkspaceTasks();
    void SyncMarkdownCompanionScroll(const editor::OpenDocument& changedDoc, bool fromPreview);
    std::mutex m_covMutex;
    phide::dev::WorkspaceCoverageSummary m_workspaceCoverage;
    void SyncTestResultsToOpenDocs(const std::vector<phide::dev::TestItem>& items);
    std::atomic<bool> m_testsRunningAsync{false};
    std::atomic<bool> m_testCancelRequested{false};
    std::mutex m_testsMutex;
    std::vector<phide::dev::TestItem> m_workspaceTests;
    phide::dev::TestRunSummary m_workspaceTestSummary{};

    // Port Conflict Detector & Process Killer Modal
    struct PortConflictModalState {
        bool isOpen = false;
        int port = 0;
        DWORD pid = 0;
        std::wstring processName;
        std::wstring serverLabel;
        std::function<void(int)> onProceedWithPort;
        D2D1_RECT_F rcKillStartBtn{};
        D2D1_RECT_F rcSwitchPortBtn{};
        D2D1_RECT_F rcCancelBtn{};
    } m_portConflictModal;
    bool m_skipNextPortPrompt = false;
    bool CheckPortBeforeServerStart(int port, const std::wstring& serverLabel, HANDLE ownServerHandle,
                                    std::function<void(int)> onStartWithPort);
    void RenderPortConflictModal(float clientW, float clientH);

    // Pre-Commit Secret & Bloat Guard Modal
    struct PreCommitFinding {
        std::wstring relPath;
        std::wstring reason;
        bool canAddGitignore = false;
    };
    struct PreCommitGuardModalState {
        bool isOpen = false;
        bool isPublishFlow = false;
        int publishVisibilityChoice = 0;
        std::wstring commitMessage;
        std::vector<PreCommitFinding> findings;
        D2D1_RECT_F rcIgnoreUnstageBtn{};
        D2D1_RECT_F rcCommitAnywayBtn{};
        D2D1_RECT_F rcCancelBtn{};
    } m_preCommitModal;
    struct PendingPreCommitScanResult {
        bool isPublish = false;
        int publishChoice = 0;
        std::wstring commitMsg;
        std::vector<PreCommitFinding> findings;
    };
    std::atomic<bool> m_preCommitScanInFlight{false};
    bool StartPreCommitCheckAsync(const std::wstring& commitMsg);
    std::vector<PreCommitFinding> CollectPreCommitFindings(const std::wstring& wsRoot, const std::wstring& commitMsg, bool isPublish) const;
    bool RunPreCommitSecretAndBloatCheck(const std::wstring& commitMsg);
    void RenderPreCommitGuardModal(float clientW, float clientH);
    std::vector<std::wstring> m_recentPaths;
    struct ClosedTabEntry {
        std::wstring filePath;
        int groupIdx = 0;
        size_t cursorLine = 0;
        size_t cursorCol = 0;
        size_t scrollLine = 0;
        float scrollOffsetX = 0.0f;
    };
    std::vector<ClosedTabEntry> m_closedTabHistory;

    // Explorer Context Menu & Clipboard State (Images 4 & 5)
    std::wstring m_contextTargetPath;
    bool m_contextTargetIsFolder = false;
    std::wstring m_clipboardPath;
    std::vector<std::wstring> m_clipboardPaths;
    bool m_clipboardIsCut = false;
    std::wstring m_compareSourcePath;
    std::wstring m_selectedExplorerPath;
    bool m_explorerHasFocus = false;
    bool m_explorerClearedSelection = false;
    std::atomic<bool> m_terminalRepaintPosted{false};
    bool m_pendingSingleSelectOnRelease = false;
    bool m_suppressExplorerClickOpen = false;
    [[nodiscard]] std::wstring ResolveExplorerCreateTargetDir(bool fallbackToActiveDoc = false) const;
    [[nodiscard]] std::vector<std::wstring> GetExplorerDragSourcePaths() const;
    [[nodiscard]] std::vector<std::wstring> GetActiveExplorerTargetPaths(const std::wstring& fallbackPath = L"") const;
    [[nodiscard]] bool IsExplorerKeyboardFocused() const noexcept;
    void CopyExplorerSelectionToClipboard(bool isCut, const std::wstring& fallbackPath = L"");
    void PasteClipboardToExplorerTarget(const std::wstring& targetPath, bool targetIsFolder);
    bool ExecuteMoveExplorerItems(const std::vector<std::wstring>& srcPaths, const std::filesystem::path& destDir);
    void ExecuteDeleteBatchAsync(const std::vector<std::wstring>& targetPaths);
    void FinishDeleteBatch(const std::vector<std::wstring>& deletedPaths, size_t successCount);
    void CleanupDeletedExplorerPath(const std::wstring& deletedPath, bool isFolder);
    void DoDeleteSelectedExplorerItem();

    UINT_PTR m_caretTimerId    = 1001;
    UINT_PTR m_animTimerId     = 1002;
    UINT_PTR m_throttleTimerId = 1003; ///< 50ms timer for throttled code search queries
    UINT_PTR m_gcTimerId       = 1004; ///< 60s periodic idle garbage collection timer
    ULONGLONG m_lastUserInputTick = 0; ///< Last timestamp of user scroll/key/click input
    ULONGLONG m_lastGitAutoFetchTick = 0; ///< Last timestamp of background git.autofetch

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

    void CloseTerminalEditorTab();
    [[nodiscard]] bool HasAnyTerminalEditorTab() const noexcept;
    [[nodiscard]] int FindVisibleTerminalEditorGroup() const noexcept;

    [[nodiscard]] float GetCurrentTerminalHeight(float clientH) const noexcept {
        if (m_activeFloatingWindow) return 0.0f;
        if (!m_terminal.IsOpen() && !m_isTerminalAnimating) return 0.0f;
        if (m_terminal.GetDockPosition() != 0) return 0.0f;
        const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
        const float statusBarH = m_config.showStatusBar ? m_config.ScaleF(22.0f) : 0.0f;
        float normalH = m_terminal.GetHeight();
        float fullH = (std::max)(normalH, clientH - headerH - statusBarH);
        float effectiveMax = normalH + (fullH - normalH) * m_termFullScreenProgress;
        return effectiveMax * m_terminalAnimProgress;
    }

    [[nodiscard]] float GetCurrentBottomDockHeight(float clientH) const noexcept {
        if (m_activeFloatingWindow) return 0.0f;
        if (IsBottomProblemsVisible()) {
            return (std::clamp)(m_problemsPanelHeight, m_config.ScaleF(130.0f), (std::max)(m_config.ScaleF(150.0f), clientH * 0.55f));
        }
        return GetCurrentTerminalHeight(clientH);
    }

    // Explorer Drag & Drop State (Moving files/folders & Editor import)
    bool m_isDraggingExplorerFile = false;
    bool m_dragCandidateIsFolder = false;
    int m_dragCandidateTreeIdx = -1;
    std::wstring m_dragCandidatePath;
    std::wstring m_dragCandidateName;
    POINT m_dragStartPt{ 0, 0 };
    POINT m_dragCurrentPt{ 0, 0 };
    bool m_hasDragDropTarget = false;
    size_t m_dragDropTargetLine = 0;
    size_t m_dragDropTargetCol = 0;
    int m_dragDropTargetGroup = -1;
    bool m_isDragOverExplorer = false;
    int m_explorerDropTargetIndex = -1;
    bool m_explorerDropTargetIsRoot = false;

    // Tab Drag & Drop Reordering & Windows Snap State
    bool m_isDraggingTab = false;
    int m_dragTabGroup = -1;
    int m_dragTabIdx = -1;
    int m_tabContextGroupIdx = -1;
    int m_tabContextDocIdx = -1;
    int m_tabDropTargetGroup = -1;
    int m_tabDropTargetIdx = -1;
    editor::TabSnapZone m_tabSnapZone = editor::TabSnapZone::None;
    int m_hoveredSnapBarSlot = -1;
    D2D1_RECT_F m_tabSnapPreviewRect{};
    POINT m_dragTabStartPt{ 0, 0 };
    POINT m_dragTabCurrentPt{ 0, 0 };

    D2D1_RECT_F m_rcStatusBarVenvTag{};
    bool m_isHoveredStatusBarVenvTag = false;

    D2D1_RECT_F m_rcStatusBarGraphView{};
    bool m_isHoveredStatusBarGraphView = false;

    // Status Bar Git & GitHub Cache and Interactive State
    std::wstring m_cachedGitDir;
    std::wstring m_cachedGitOpenDir;
    ULONGLONG m_lastGitStatCheckTick = 0;
    std::filesystem::file_time_type m_cachedHeadTime{};
    std::filesystem::file_time_type m_cachedConfigTime{};
    std::wstring m_cachedGitBranch;
    std::wstring m_cachedGitHubName;
    std::wstring m_cachedGitHubRepo;
    std::wstring m_cachedGitUserName;
    bool m_isGitCached = false;
    std::wstring m_cachedLaunchRoot;
    ULONGLONG m_lastLaunchCheckTick = 0;
    bool m_cachedHasLaunchCfgs = false;
    void UpdateCachedGitInfo();

    D2D1_RECT_F m_rcStatusBarGitBranch{};
    bool m_isHoveredStatusBarGitBranch = false;
    D2D1_RECT_F m_rcStatusBarRemote{};
    bool m_isHoveredStatusBarRemote = false;
    D2D1_RECT_F m_rcStatusBarGitUser{};
    bool m_isHoveredStatusBarGitUser = false;
    D2D1_RECT_F m_rcStatusBarLnCol{};
    bool m_isHoveredStatusBarLnCol = false;
    D2D1_RECT_F m_rcStatusBarSpaces{};
    bool m_isHoveredStatusBarSpaces = false;
    D2D1_RECT_F m_rcStatusBarEncoding{};
    bool m_isHoveredStatusBarEncoding = false;
    D2D1_RECT_F m_rcStatusBarPrivacyMask{};
    bool m_isHoveredStatusBarPrivacyMask = false;
    D2D1_RECT_F m_rcStatusBarStructuredView{};
    bool m_isHoveredStatusBarStructuredView = false;
    D2D1_RECT_F m_rcStatusBarProblems{};
    bool m_isHoveredStatusBarProblems = false;
    D2D1_RECT_F m_rcStatusBarCommentAnnots{};
    bool m_isHoveredStatusBarCommentAnnots = false;
    std::wstring m_statusCommentTooltip;
    bool m_hasHoveredGutterComment = false;
    ui::EditorCanvas::CommentIndicatorHit m_hoveredGutterComment{};
    bool m_isColumnSelecting = false;

public:
    /// Single comment annotation entry collected across open tabs or closed workspace files.
    struct CommentAnnotDropdownItem {
        std::wstring filePath;          ///< Full file path on disk (empty for unsaved virtual/untitled tabs)
        std::wstring displayPath;       ///< Relative path to workspace root or tab filename
        int groupIdx = -1;              ///< Open group index when already open (-1 if closed workspace file)
        int docIdx = -1;                ///< Open doc index when already open
        uint64_t floatingWinId = 0;     ///< Non-zero if hosted in a floating dock window
        size_t line = 0;                ///< 0-based line index of annotation
        size_t col = 0;                 ///< 0-based column index of annotation
        ui::EditorCanvas::BetterCommentType type = ui::EditorCanvas::BetterCommentType::Normal;
        std::wstring tagLabel;          ///< Short label ("Urgent", "TODO", "Warn", ...)
        std::wstring preview;           ///< Trimmed comment text preview
    };

    /// Visual row inside the Status Bar Comment Annotations dropdown (either a file header or an annotation item).
    struct CommentAnnotDropdownRow {
        bool isFileHeader = false;
        size_t itemIdx = 0;             ///< Index in m_commentAnnotsDropdownItems (first item for headers)
        std::wstring fileHeaderPath;
        std::wstring fileHeaderLabel;
        size_t fileItemCount = 0;
    };

    /// Cached comment annotation summary for a closed workspace file scanned asynchronously.
    struct WorkspaceCommentAnnotFile {
        std::wstring filePath;
        std::wstring displayPath;
        ui::EditorCanvas::DocCommentSummary summary;
    };

    /// Aggregated counts & distinct file metadata for Status Bar pill rendering (zero per-entry allocation).
    struct CommentAnnotationStats {
        size_t distinctFiles = 0;
        bool activeFileHasAnnots = false;
        size_t urgentCount = 0;
        size_t asapCount = 0;
        size_t alertCount = 0;
        size_t todoCount = 0;
        size_t warningCount = 0;
        size_t hackCount = 0;
        size_t reviewCount = 0;
        size_t noteCount = 0;
        size_t importantCount = 0;
        size_t questionCount = 0;
        std::vector<std::wstring> fileLabels; ///< Up to 3 representative file labels
    };

    bool JumpToNextCommentAnnotation();
    /** @brief Scans rootDir recursively on a background-safe path for BetterComment annotations across closed workspace files. */
    [[nodiscard]] static std::vector<WorkspaceCommentAnnotFile> ScanWorkspaceCommentAnnots(
        const std::wstring& rootDir,
        const std::atomic<bool>* isDestroying = nullptr,
        const std::atomic<uint64_t>* activeGen = nullptr,
        uint64_t expectedGen = 0);
    /** @brief Collects all comment annotations across open tabs and closed scanned workspace files. */
    [[nodiscard]] std::vector<CommentAnnotDropdownItem> CollectAllCommentAnnotations(size_t* outDistinctFiles = nullptr) const;
    /** @brief Fast zero-entry-allocation aggregator for Status Bar pill counts & tooltip composition. */
    [[nodiscard]] CommentAnnotationStats CollectCommentAnnotationStats() const;
    /** @brief Handles clicks on the Status Bar comment annotations pill (opens dropdown for >1 files, jumps directly for 1 file). */
    void HandleStatusBarCommentAnnotsClick();
    void ToggleCommentAnnotsDropdown();
    void CloseCommentAnnotsDropdown();
    void RenderCommentAnnotsDropdown(ID2D1RenderTarget* pRT, float clientWidth, float clientHeight);
    bool HandleCommentAnnotsDropdownMouseDown(POINT pt);
    bool HandleCommentAnnotsDropdownMouseMove(POINT pt);
    bool HandleCommentAnnotsDropdownMouseWheel(short delta, POINT pt);
    void RefreshWorkspaceCommentAnnotsAsync();
    void ApplyPendingWorkspaceCommentAnnots();
    [[nodiscard]] bool IsCommentAnnotsDropdownOpen() const noexcept { return m_showCommentAnnotsDropdown; }
    [[nodiscard]] const std::vector<CommentAnnotDropdownItem>& GetCommentAnnotsDropdownItems() const noexcept { return m_commentAnnotsDropdownItems; }
    [[nodiscard]] const std::wstring& GetStatusCommentTooltip() const noexcept { return m_statusCommentTooltip; }
    [[nodiscard]] D2D1_RECT_F GetStatusBarCommentAnnotsRect() const noexcept { return m_rcStatusBarCommentAnnots; }

private:

    // Git Branch Switcher Dropdown (Status bar popup)
    bool m_showBranchDropdown = false;
    std::vector<std::pair<std::wstring, bool>> m_branchList;
    int m_hoveredBranchIdx = -1;
    D2D1_RECT_F m_rcBranchDropdown{};

    // Comment Annotations Dropdown & Background Workspace Scanner
    bool m_showCommentAnnotsDropdown = false;
    std::vector<CommentAnnotDropdownItem> m_commentAnnotsDropdownItems;
    std::vector<CommentAnnotDropdownRow> m_commentAnnotsDropdownRows;
    size_t m_commentAnnotsDropdownDistinctFiles = 0;
    int m_hoveredCommentAnnotRowIdx = -1;
    float m_commentAnnotsDropdownScrollY = 0.0f;
    float m_commentAnnotsDropdownMaxScrollY = 0.0f;
    D2D1_RECT_F m_rcCommentAnnotsDropdown{};
    std::vector<WorkspaceCommentAnnotFile> m_wsCommentAnnots;
    std::atomic<uint64_t> m_wsCommentScanGen{ 0 };
    std::mutex m_wsCommentMutex;
    std::vector<WorkspaceCommentAnnotFile> m_pendingWsCommentAnnots;
    uint64_t m_pendingWsCommentGen = 0;
    bool m_hasPendingWsCommentAnnots = false;

    void RenderFloatingTooltip(ID2D1RenderTarget* pRT, const std::wstring& text, const D2D1_RECT_F& anchorBounds, float clientW);

    // Sidebar Resizing State (Left Explorer & Right Runtimes/Extensions)
    bool m_isDraggingLeftSidebar = false;
    float m_dragLeftSidebarStartX = 0.0f;
    int m_dragLeftSidebarStartW = 280;

    bool m_isDraggingRightSidebar = false;
    float m_dragRightSidebarStartX = 0.0f;
    int m_dragRightSidebarStartW = 340;

    bool HitTestLeftSidebarSplitter(POINT pt) const;
    bool HitTestRightSidebarSplitter(POINT pt) const;

    std::wstring m_cachedWorkspaceDir = L"<uninitialized>";
    std::wstring m_cachedProjectRuntimeId;
    std::wstring m_currentStatusBarRuntimeId;
    std::wstring DetectWorkspaceProjectRuntime(const std::wstring& openDir);

    void AnimateTerminalOpen();
    void AnimateTerminalClose();
    void AnimateRightSidebarOpen();
    void AnimateRightSidebarClose();

    struct PendingCommitDiffResult {
        uint64_t generation = 0;
        std::wstring commitHash;
        std::wstring focusRelPath;
        std::wstring commitMsg;
        std::wstring author;
        std::wstring dateStr;
        std::vector<editor::OpenDocument::MultiFileDiffEntry> entries;
    };
    std::mutex m_commitDiffMutex;
    std::vector<PendingCommitDiffResult> m_pendingCommitDiffs;
    std::atomic<uint64_t> m_commitDiffGenCounter{ 0 };
    void ApplyPendingCommitDiffs();

    struct PendingCodeMetricsResult {
        uint64_t generation = 0;
        editor::WorkspaceCodeMetrics metrics{};
        std::wstring activeSummary;
        size_t anchorLine = 0;
    };
    std::mutex m_codeMetricsMutex;
    PendingCodeMetricsResult m_pendingCodeMetrics;
    bool m_hasPendingCodeMetrics = false;
    std::atomic<uint64_t> m_codeMetricsGen{ 0 };
    void ApplyPendingCodeMetrics();

    struct PendingFileLoadResult {
        uint64_t generation = 0;
        std::wstring resolvedPath;
        bool loadOk = false;
        bool isLargeFile = false;
        std::shared_ptr<editor::Buffer> loadedBuffer;
        std::wstring originalDiskText;
        std::shared_ptr<editor::Buffer> diffOriginalBuffer;
        std::vector<lsp::Diagnostic> diagnostics;
        std::shared_ptr<ui::StructuredViewerState> structuredView;
        int effectiveTabSize = 0;
        int effectiveIndentStyle = 0;
        std::filesystem::file_time_type lastDiskWriteTime{};
        bool hasLastDiskWriteTime = false;
    };
    std::mutex m_fileLoadMutex;
    std::vector<PendingFileLoadResult> m_pendingFileLoads;
    std::atomic<uint64_t> m_fileLoadGenCounter{ 0 };
    std::wstring ResolveWorkspaceRelativePath(const std::wstring& path) const;
    void FinalizeOpenedTextDocument(editor::OpenDocument& doc, const std::wstring& resolvedPath, bool runLspAndLinters, bool runGitAndExtraDiagnostics);
    void ApplyPendingFileLoads();
};

} // namespace phide::app
