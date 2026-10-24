/**
 * @file DrawerPanel.hpp
 * @brief Slide-out contextual sidebar drawer for project explorer, AI assistant, and git history.
 * 
 * Expands when an icon on the activity rail is clicked, and collapses back to 0 width
 * when toggled off, restoring full canvas width.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <filesystem>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

/**
 * @struct DrawerTreeItem
 * @brief Visual entry in the file tree or perspective list.
 */
struct DrawerTreeItem {
    std::wstring label;
    std::wstring iconGlyph;
    std::wstring fullPath;
    int depth = 0;
    bool isFolder = false;
    bool isExpanded = true;
    D2D1_RECT_F bounds{};
};

/**
 * @struct SearchMatch
 * @brief Individual matched occurrence inside a source file.
 */
struct SearchMatch {
    size_t lineIndex = 0;       ///< 0-based buffer line
    std::wstring lineText;      ///< Full text of the matched line
    size_t matchCol = 0;        ///< Column offset of match
    size_t matchLen = 0;        ///< Length of match string
    D2D1_RECT_F bounds{};       ///< Clickable row bounding box
    D2D1_RECT_F replaceBtnBounds{0, 0, 0, 0};
    D2D1_RECT_F dismissBtnBounds{0, 0, 0, 0};
};

/**
 * @struct SearchFileGroup
 * @brief Grouping of all matches located inside a single file.
 */
struct SearchFileGroup {
    std::wstring filePath;
    std::wstring fileName;
    std::wstring relDir;
    bool isExpanded = true;
    D2D1_RECT_F headerBounds{};
    D2D1_RECT_F replaceAllBtnBounds{0, 0, 0, 0};
    D2D1_RECT_F dismissBtnBounds{0, 0, 0, 0};
    std::vector<SearchMatch> matches;
};

/**
 * @struct GitChangeItem
 * @brief Represents a modified, untracked, added, or deleted file in working tree.
 */
struct GitChangeItem {
    std::wstring path;
    std::wstring fileName;
    std::wstring relDir;
    std::wstring status; ///< "M", "A", "D", "U"
    bool isStaged = false;
    D2D1_RECT_F bounds{};
    D2D1_RECT_F stageBtnBounds{};
    D2D1_RECT_F discardBtnBounds{};
};

struct GitCmdResult {
    int exitCode = -1;
    std::wstring output;
};

GitCmdResult RunGitCmd(const std::wstring& cmd, const std::wstring& workDir);

/**
 * @struct GitCommitFile
 * @brief Represents a file modified in a specific git commit (Image 3).
 */
struct GitCommitFile {
    std::wstring path;
    std::wstring fileName;
    std::wstring relDir;
    std::wstring status; ///< "M", "A", "D"
    D2D1_RECT_F bounds{};
};

/**
 * @struct GitCommitItem
 * @brief Represents a commit in the git history tree.
 */
struct GitCommitItem {
    std::wstring hash;
    std::wstring parentHash;
    std::wstring message;
    std::wstring author;
    std::wstring relativeTime;
    std::wstring dateFormatted;
    std::wstring statSummary;
    std::wstring refNames;
    bool isHead = false;
    bool isExpanded = false;
    bool filesLoaded = false;
    int lane = 0;
    int colorIdx = 0;
    std::vector<int> parentLanes;
    std::vector<int> passThroughLanes;
    std::vector<GitCommitFile> files;
    D2D1_RECT_F bounds{};
    D2D1_RECT_F chevronBounds{};
    D2D1_RECT_F openChangesBtnBounds{};
};

/**
 * @class DrawerPanel
 * @brief Renders the expandable sidebar drawer when active.
 */
class DrawerPanel {
public:
    DrawerPanel();
    ~DrawerPanel() = default;

    enum class GitMenuType { None, HeaderViews, ChangesMore, GraphMore };
    struct GitMenuItemDef {
        int id = 0;
        std::wstring label;
        bool isHeader = false;
        bool isSeparator = false;
        bool isChecked = false;
        D2D1_RECT_F bounds{};
    };

    // --- Source Control (Git) Operations & Interactions (Images 2, 3, 4, 5) ---
    void OpenSourceControl();
    void RefreshGitState(bool async = true, bool showLoading = false);
    void RefreshGitStatusOnly(bool async = true);
    void SetDirtyFiles(const std::vector<std::wstring>& dirtyFilePaths);
    void SetErrorFiles(const std::map<std::wstring, size_t>& errorFiles);
    void StageAllChangesAsync();
    void DiscardAllChangesAsync();
    bool DiscardAllChanges();
    void StageFileAsync(const std::wstring& relPath);
    void DiscardFileAsync(const std::wstring& relPath);
    bool DiscardFile(const std::wstring& relPath);
    void CommitChangesAsync(const std::wstring& message);
    void UncommitLastCommitAsync();
    void PushCommitsAsync();
    void PullCommitsAsync();
    void FetchAllRemotesAsync();
    void StashChangesAsync();
    void RunGarbageCollectionAsync();
    void GenerateAiCommitMessage();
    void SetCommitMessage(const std::wstring& msg) {
        std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
        m_commitMessage = msg;
    }
    [[nodiscard]] std::wstring GetCommitMessage() const {
        std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
        return m_commitMessage;
    }
    [[nodiscard]] std::wstring GetGitFileContent(const std::wstring& relPath);
    [[nodiscard]] std::wstring GetCommitFileContent(const std::wstring& commitHash, const std::wstring& relPath);
    void ComputeGitDiffLines(const std::wstring& relPath, std::set<size_t>& outAdded, std::set<size_t>& outDeleted);
    bool HitTestGit(POINT pt, const app::AppConfig& config,
                    std::wstring& outOpenFile,
                    std::wstring& outDiscardedFile,
                    bool& outIsDiff,
                    std::wstring& outOpenCommitDiff,
                    std::wstring& outOpenUrl);
    bool HitTestGit(POINT pt, const app::AppConfig& config, std::wstring& outOpenFile, std::wstring& outDiscardedFile) {
        bool isDiff = false;
        std::wstring dummyCommit;
        std::wstring dummyUrl;
        return HitTestGit(pt, config, outOpenFile, outDiscardedFile, isDiff, dummyCommit, dummyUrl);
    }
    bool HitTestGit(POINT pt, const app::AppConfig& config, std::wstring& outOpenFile) {
        std::wstring dummyDiscard;
        bool isDiff = false;
        std::wstring dummyCommit;
        std::wstring dummyUrl;
        return HitTestGit(pt, config, outOpenFile, dummyDiscard, isDiff, dummyCommit, dummyUrl);
    }
    void LoadCommitFiles(GitCommitItem& ci);
    static void ComputeGitGraphLanes(std::vector<GitCommitItem>& commits);
    bool HandleGitMouseMove(POINT pt);
    void HandleGitChar(wchar_t ch);
    void HandleGitKeyDown(WPARAM wParam);

    [[nodiscard]] bool HitTestGitSplitter(POINT pt) const noexcept;
    void SetGitSplitY(float y, float minY, float maxY) noexcept;
    [[nodiscard]] float GetGitSplitY() const noexcept { return m_gitSplitY; }

    [[nodiscard]] bool IsGitRepo() const noexcept { return m_isGitRepo; }
    [[nodiscard]] bool IsGitLoading() const noexcept { return m_isGitLoading.load(); }
    [[nodiscard]] bool IsGitRefreshing() const noexcept { return m_isGitRefreshing.load(); }
    [[nodiscard]] std::recursive_mutex& GetGitMutex() const noexcept { return m_gitMutex; }
    void UpdateGitLoadingAnim(float deltaAngle) noexcept { m_gitAnimAngle += deltaAngle; }
    [[nodiscard]] bool IsGitCommitBoxFocused() const noexcept { return m_focusedGitInput == 1; }
    void SetGitCommitBoxFocus(int focus) noexcept { m_focusedGitInput = focus; }
    void ToggleCaretBlink() noexcept { m_gitCaretVisible = !m_gitCaretVisible; }
    [[nodiscard]] bool IsGitCaretVisible() const noexcept { return m_gitCaretVisible; }
    [[nodiscard]] int GetUnpushedCommitsCount() const noexcept { return m_unpushedCommitsCount; }
    void SetUnpushedCommitsCount(int count) noexcept { m_unpushedCommitsCount = count; }
    [[nodiscard]] int GetHoveredGitChangeIndex() const noexcept { return m_hoveredGitChangeIdx; }
    [[nodiscard]] int GetSelectedCommitIndex() const noexcept { return m_selectedCommitIdx; }
    void SetSelectedCommitIndex(int idx) noexcept {
        m_selectedCommitIdx = idx;
        m_hoveredCommitIdx = idx;
        if (idx >= 0 && idx < static_cast<int>(m_gitCommits.size())) {
            m_commitCardHash = m_gitCommits[idx].hash;
            if (m_gitCommits[idx].statSummary.empty()) {
                LoadCommitFiles(m_gitCommits[idx]);
            }
        }
    }
    [[nodiscard]] const std::vector<GitChangeItem>& GetGitChanges() const noexcept { return m_gitChanges; }
    [[nodiscard]] std::vector<GitChangeItem>& GetGitChangesMutable() noexcept { return m_gitChanges; }
    [[nodiscard]] const std::map<std::wstring, size_t>& GetErrorFiles() const noexcept { return m_errorFiles; }
    [[nodiscard]] const std::vector<GitCommitItem>& GetGitCommits() const noexcept { return m_gitCommits; }
    [[nodiscard]] const std::wstring& GetGitBranch() const noexcept { return m_gitBranch; }
    [[nodiscard]] const D2D1_RECT_F& GetGitHeaderMoreBounds() const noexcept { return m_gitHeaderMoreBounds; }
    [[nodiscard]] const std::wstring& GetGitTooltipText() const noexcept { return m_gitTooltipText; }
    [[nodiscard]] bool IsGitTooltipVisible() const noexcept { return m_showGitTooltip; }
    [[nodiscard]] bool IsGitViewMenuOpen() const noexcept { return m_activeGitMenu == GitMenuType::HeaderViews || m_showGitViewMenu; }
    [[nodiscard]] GitMenuType GetActiveGitMenu() const noexcept { return m_activeGitMenu; }
    void DismissCommitCard() noexcept {
        m_hoveredCommitIdx = -1;
        m_selectedCommitIdx = -1;
        m_commitCardBounds = D2D1::RectF(0, 0, 0, 0);
        m_commitCardCloseBounds = D2D1::RectF(0, 0, 0, 0);
        m_commitCardOpenChangesBounds = D2D1::RectF(0, 0, 0, 0);
        m_commitCardGitHubBounds = D2D1::RectF(0, 0, 0, 0);
    }
    [[nodiscard]] bool IsCommitCardVisible() const noexcept {
        return (m_hoveredCommitIdx >= 0 || m_selectedCommitIdx >= 0);
    }
    [[nodiscard]] const D2D1_RECT_F& GetCommitCardBounds() const noexcept { return m_commitCardBounds; }
    [[nodiscard]] bool HitTestCommitCard(POINT pt) const noexcept {
        return IsCommitCardVisible() &&
               pt.x >= m_commitCardBounds.left && pt.x <= m_commitCardBounds.right &&
               pt.y >= m_commitCardBounds.top && pt.y <= m_commitCardBounds.bottom;
    }
    void SetOnGitStateChanged(std::function<void()> cb) { m_onGitStateChanged = std::move(cb); }

    /**
     * @brief Paints the drawer background, header, and contents with animated width and upper-left curve.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme.
     * @param config Application configuration.
     * @param clientHeight Height of the client window.
     * @param currentDrawerW Current animated drawer width.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW);

    /**
     * @brief Performs hit-testing on drawer items.
     * @param pt Mouse point in client coordinates.
     * @param config Application configuration.
     * @return Index of clicked tree item, or -1.
     */
    int HitTest(POINT pt, const app::AppConfig& config) const;

    /**
     * @brief Performs hit-testing on the "Open Folder" button shown when no folder is open.
     */
    bool HitTestOpenFolderButton(POINT pt, const app::AppConfig& config) const;

    /**
     * @brief Sets the hovered tree item.
     * @param index Hovered item index.
     * @return True if state changed.
     */
    bool SetHoveredIndex(int index);

    /**
     * @brief Sets the hovered state of the "Open Folder" button.
     * @return True if state changed.
     */
    bool SetHoveredOpenFolderBtn(bool hovered);

    /**
     * @brief Gets a pointer to the tree item at the specified index.
     */
    [[nodiscard]] const DrawerTreeItem* GetItem(int index) const {
        if (index >= 0 && index < static_cast<int>(m_visibleItems.size())) {
            return &m_visibleItems[index];
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<DrawerTreeItem>& GetTreeItems() const noexcept {
        return m_visibleItems;
    }

    [[nodiscard]] size_t GetItemCount() const noexcept {
        return m_visibleItems.size();
    }

    /**
     * @brief Toggles folder open/closed state.
     */
    void ToggleFolder(int index);

    /**
     * @brief Opens a folder and populates the explorer tree with real filesystem contents.
     */
    void OpenFolder(const std::wstring& folderPath, const std::set<std::wstring>* pInitialExpanded = nullptr);

    /**
     * @brief Gets the currently expanded directories in the tree.
     */
    [[nodiscard]] const std::set<std::wstring>& GetExpandedDirs() const noexcept { return m_expandedDirs; }

    /**
     * @brief Restores the expanded directories and refreshes visible tree items.
     */
    void SetExpandedDirs(const std::set<std::wstring>& dirs);

    /**
     * @brief Closes the currently opened folder, returning the explorer to the "NO FOLDER OPENED" state.
     */
    void CloseFolder();

    /**
     * @brief Refreshes the tree contents from disk.
     */
    void RefreshTree();

    /**
     * @brief Handles mouse wheel scrolling across directory tree entries or search results.
     * @param delta Wheel delta from WM_MOUSEWHEEL.
     * @param activeRailTab Current active perspective on the rail (0 = Explorer, 1 = Search).
     * @return True if scroll position changed.
     */
    bool HandleMouseWheel(short delta, int activeRailTab = 0, float mouseY = 0.0f);

    [[nodiscard]] bool HasOpenFolder() const noexcept { return m_hasOpenFolder; }
    [[nodiscard]] const std::wstring& GetOpenFolderPath() const noexcept { return m_openFolderPath; }
    [[nodiscard]] const std::wstring& GetRootName() const noexcept { return m_rootName; }

    // --- Search Pane Interactions (Image 2) ---
    /**
     * @brief Dispatches mouse click inside the Code Search sidebar pane.
     * @param pt Mouse coordinate in client space.
     * @param config Application metrics.
     * @param outJumpFile Returns path of file clicked in search results.
     * @param outJumpLine Returns 0-based line to jump to.
     * @return True if a UI element was handled.
     */
    bool HitTestSearch(POINT pt, const app::AppConfig& config, std::wstring& outJumpFile, size_t& outJumpLine,
                       size_t& outMatchCol, size_t& outMatchLen);
    bool HitTestSearch(POINT pt, const app::AppConfig& config, std::wstring& outJumpFile, size_t& outJumpLine) {
        size_t c = 0, l = 0;
        return HitTestSearch(pt, config, outJumpFile, outJumpLine, c, l);
    }

    /**
     * @brief Handles character typed into the active search or replace input box.
     */
    void HandleSearchChar(wchar_t ch);

    /**
     * @brief Handles special key navigation in the active search/replace box.
     */
    void HandleSearchKeyDown(WPARAM wParam);

    /**
     * @brief Schedules a throttled workspace search.
     * Executes immediately if minimum interval has elapsed, otherwise marks pending.
     */
    void TriggerSearchThrottled(ULONGLONG nowMs = 0);

    /**
     * @brief Polls throttled search timer (called on animation / timer loop).
     * @param nowMs Current monotonic millisecond timestamp.
     * @return True if a pending search was executed and UI needs repaint.
     */
    bool PollSearchThrottle(ULONGLONG nowMs);

    [[nodiscard]] bool IsSearchFocused() const noexcept { return m_focusedSearchInput > 0; }
    void SetSearchFocus(int inputId) noexcept { m_focusedSearchInput = inputId; }

    /**
     * @brief Sets search query programmatically and runs search.
     */
    void SetSearchQuery(const std::wstring& q) {
        m_searchQuery = q;
        ExecuteSearch(true);
    }
    [[nodiscard]] const std::wstring& GetSearchQuery() const noexcept { return m_searchQuery; }
    [[nodiscard]] const std::vector<SearchFileGroup>& GetSearchGroups() const noexcept { return m_searchGroups; }
    [[nodiscard]] size_t GetTotalMatches() const noexcept { return m_totalMatches; }
    void SetReplaceQuery(const std::wstring& rq) { m_replaceQuery = rq; }
    [[nodiscard]] const std::wstring& GetReplaceQuery() const noexcept { return m_replaceQuery; }
    void ExecuteReplaceSingleMatch(const std::wstring& filePath, const SearchMatch& match);
    void ExecuteReplaceInFile(const std::wstring& filePath);
    void DismissSearchMatch(size_t groupIdx, size_t matchIdx);
    void DismissSearchGroup(size_t groupIdx);
    void SetOnRefreshOpenDocs(std::function<void()> cb) { m_onRefreshOpenDocs = cb; }
    void SetOnFilesReplaced(std::function<void(const std::vector<std::wstring>&)> cb) { m_onFilesReplaced = cb; }
    [[nodiscard]] int GetHoveredIndex() const noexcept { return m_hoveredIndex; }
    [[nodiscard]] bool GetMatchCase() const noexcept { return m_matchCase; }
    void SetMatchCase(bool val) noexcept { m_matchCase = val; }
    [[nodiscard]] bool GetMatchWholeWord() const noexcept { return m_matchWholeWord; }
    void SetMatchWholeWord(bool val) noexcept { m_matchWholeWord = val; }
    [[nodiscard]] bool GetUseRegex() const noexcept { return m_useRegex; }
    void SetUseRegex(bool val) noexcept { m_useRegex = val; }
    [[nodiscard]] bool IsReplaceOpen() const noexcept { return m_isReplaceOpen; }
    void SetReplaceOpen(bool open) noexcept { m_isReplaceOpen = open; }

    [[nodiscard]] bool IsSearchDetailsOpen() const noexcept { return m_isSearchDetailsOpen; }
    void SetSearchDetailsOpen(bool open) noexcept { m_isSearchDetailsOpen = open; }
    [[nodiscard]] bool GetSearchOnlyOpenEditors() const noexcept { return m_searchOnlyOpenEditors; }
    [[nodiscard]] bool IsSearchOnlyOpenEditors() const noexcept { return m_searchOnlyOpenEditors; }
    void SetSearchOnlyOpenEditors(bool openOnly) noexcept { m_searchOnlyOpenEditors = openOnly; }
    [[nodiscard]] bool GetUseExcludeSettings() const noexcept { return m_useExcludeSettings; }
    void SetUseExcludeSettings(bool useExclude) noexcept { m_useExcludeSettings = useExclude; }
    [[nodiscard]] const std::wstring& GetFilesToInclude() const noexcept { return m_filesToInclude; }
    void SetFilesToInclude(const std::wstring& inc) { m_filesToInclude = inc; }
    [[nodiscard]] const std::wstring& GetFilesToExclude() const noexcept { return m_filesToExclude; }
    void SetFilesToExclude(const std::wstring& exc) { m_filesToExclude = exc; }
    void SetOpenDocPaths(const std::vector<std::wstring>& paths) { m_openDocPaths = paths; }
    [[nodiscard]] const std::vector<std::wstring>& GetOpenDocPaths() const noexcept { return m_openDocPaths; }
    [[nodiscard]] int GetSearchFocus() const noexcept { return m_focusedSearchInput; }

    [[nodiscard]] bool HitTestSearchButtons(POINT pt) const noexcept {
        auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
            return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
        };
        if (inRect(pt, m_rcToggleReplaceChevron) || inRect(pt, m_rcMatchCaseBtn) ||
            inRect(pt, m_rcMatchWholeWordBtn) || inRect(pt, m_rcUseRegexBtn) ||
            inRect(pt, m_rcPreserveCaseBtn) || inRect(pt, m_rcReplaceAllBtn) ||
            inRect(pt, m_rcRefreshSearchBtn) || inRect(pt, m_rcToggleSearchDetailsBtn) ||
            inRect(pt, m_rcOpenEditorsOnlyBtn) || inRect(pt, m_rcUseExcludeSettingsBtn)) {
            return true;
        }
        for (const auto& grp : m_searchGroups) {
            if (inRect(pt, grp.headerBounds) || inRect(pt, grp.replaceAllBtnBounds) || inRect(pt, grp.dismissBtnBounds)) {
                return true;
            }
            for (const auto& m : grp.matches) {
                if (inRect(pt, m.replaceBtnBounds) || inRect(pt, m.dismissBtnBounds)) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool HitTestSearchInputs(POINT pt) const noexcept {
        auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
            return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
        };
        return inRect(pt, m_rcSearchBox) || inRect(pt, m_rcReplaceBox) ||
               inRect(pt, m_rcFilesToIncludeBox) || inRect(pt, m_rcFilesToExcludeBox);
    }

    [[nodiscard]] bool HitTestGitInputs(POINT pt) const noexcept {
        auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
            return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
        };
        return inRect(pt, m_gitMsgBoxBounds);
    }

    [[nodiscard]] bool HitTestGitButtons(POINT pt) const noexcept {
        auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
            return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
        };
        for (const auto& item : m_gitChanges) {
            if (inRect(pt, item.discardBtnBounds) || inRect(pt, item.stageBtnBounds)) return true;
        }
        return inRect(pt, m_gitCommitBtnBounds) || inRect(pt, m_gitCommitDropdownBounds) ||
               inRect(pt, m_gitGenerateAiBounds) || inRect(pt, m_gitStageAllBounds) ||
               inRect(pt, m_gitDiscardAllBounds) || inRect(pt, m_gitChangesChevronBounds) ||
               inRect(pt, m_gitChangesMoreBounds) || inRect(pt, m_gitHeaderMoreBounds) ||
               inRect(pt, m_gitGraphChevronBounds) || inRect(pt, m_gitGraphAutoBounds) ||
               inRect(pt, m_gitGraphBullseyeBounds) || inRect(pt, m_gitGraphSplitBounds) ||
               inRect(pt, m_gitGraphMergeBounds) || inRect(pt, m_gitGraphPushBounds) ||
               inRect(pt, m_gitGraphRefreshBounds) || inRect(pt, m_gitGraphMoreBounds) ||
               inRect(pt, m_commitCardCloseBounds) || inRect(pt, m_commitCardOpenChangesBounds) ||
               inRect(pt, m_commitCardGitHubBounds);
    }
    [[nodiscard]] bool IsCommitting() const noexcept { return m_isCommitting.load(); }
    [[nodiscard]] bool IsSyncing() const noexcept { return m_isSyncing.load(); }

    // --- Inline Tree Item Rename ---
    /**
     * @brief Initiates inline rename mode on a tree item by its visible index.
     */
    void StartInlineRename(int itemIdx);

    /**
     * @brief Initiates inline rename mode on a tree item matching full filesystem path.
     */
    void StartInlineRenameByPath(const std::wstring& fullPath);

    /**
     * @brief Checks if the explorer tree is currently in inline rename mode.
     */
    [[nodiscard]] bool IsRenaming() const noexcept { return m_isRenaming; }

    /**
     * @brief Cancels inline rename mode without saving changes.
     */
    void CancelInlineRename();

    /**
     * @brief Commits inline rename, updating filesystem and returning affected paths.
     */
    bool CommitInlineRename(std::wstring& outOldPath, std::wstring& outNewPath);

    /**
     * @brief Handles character typed during inline rename.
     */
    void HandleRenameChar(wchar_t ch);

    /**
     * @brief Handles special key during inline rename (Enter to commit, Escape to cancel).
     */
    void HandleRenameKeyDown(WPARAM wParam);

    /**
     * @brief Gets current inline rename input box bounding rect for hit-testing outside clicks.
     */
    [[nodiscard]] const D2D1_RECT_F& GetRenameBox() const noexcept { return m_rcRenameBox; }

    // --- Inline Tree Item Create (Images 2 & 3: VS Code style new file / new folder) ---
    void StartInlineCreate(const std::wstring& parentPath, bool isFolder);
    [[nodiscard]] bool IsCreating() const noexcept { return m_isCreating; }
    [[nodiscard]] bool IsCreatingFolder() const noexcept { return m_createIsFolder; }
    void CancelInlineCreate();
    bool CommitInlineCreate(std::wstring& outCreatedPath);
    void HandleCreateChar(wchar_t ch);
    void HandleCreateKeyDown(WPARAM wParam);
    [[nodiscard]] const D2D1_RECT_F& GetCreateBox() const noexcept { return m_rcCreateBox; }
    [[nodiscard]] const D2D1_RECT_F& GetCreatePill() const noexcept { return m_rcCreatePill; }
    [[nodiscard]] const std::wstring& GetCreateParentDir() const noexcept { return m_createParentDir; }
    [[nodiscard]] const std::wstring& GetCreateText() const noexcept { return m_createText; }

    /**
     * @brief Sets canonical path of the currently active editor document to synchronize explorer selection (Images 2 & 3).
     */
    void SetActiveFilePath(const std::wstring& path) noexcept { m_activeFilePath = path; }
    [[nodiscard]] const std::wstring& GetActiveFilePath() const noexcept { return m_activeFilePath; }

    // --- Search Tooltips ---
    /**
     * @brief Handles mouse movement over Code Search buttons to update floating tooltips.
     * @return True if tooltip state changed and requires repaint.
     */
    bool HandleSearchMouseMove(POINT pt);

    /**
     * @brief Manually clears previous search results and trims vector capacities (manual garbage collection).
     */
    void ClearSearchResults();

    /**
     * @brief Fetches next batch of search results incrementally as user scrolls down.
     */
    void FetchNextSearchBatch();

    [[nodiscard]] bool HasMoreSearchResults() const noexcept { return m_searchHasMore; }
    [[nodiscard]] bool IsSearching() const noexcept { return m_isSearching.load() || m_isFetchingMore.load(); }

    /**
     * @brief Launches recursive workspace search on a background worker thread.
     */
    void LaunchAsyncSearch();

    /**
     * @brief Executes recursive workspace file search matching current search query.
     * @param async If true, runs on a background worker thread; otherwise runs synchronously.
     */
    void ExecuteSearch(bool async = true);

    /**
     * @brief Replaces all matched occurrences across the workspace with the replace query.
     */
    void ExecuteReplaceAll();

private:
    void RenderExplorerPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW);
    void RenderSearchPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW);
    void RenderSourceControlPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW);

    // --- Git / Source Control State (Images 2, 3, 4, 5) ---
    bool m_isGitRepo = true;
    std::wstring m_gitBranch = L"master";
    std::vector<GitChangeItem> m_gitChanges;
    std::vector<GitCommitItem> m_gitCommits;
    std::vector<std::wstring> m_dirtyFilePaths;
    std::map<std::wstring, size_t> m_errorFiles;
    std::atomic<bool> m_isGitLoading{false};
    std::atomic<bool> m_isGitRefreshing{false};
    std::atomic<bool> m_isCommitting{false};
    std::atomic<bool> m_isSyncing{false};
    bool m_gitCommitBtnPressed = false;
    float m_gitAnimAngle = 0.0f;
    mutable std::recursive_mutex m_gitMutex;

    bool m_showChangesSection = true;
    bool m_showGraphSection = true;
    bool m_changesExpanded = true;
    bool m_graphExpanded = true;
    bool m_gitAutoRefresh = true;

    std::wstring m_commitMessage;
    int m_focusedGitInput = 0; // 0 = none, 1 = commit message input box
    bool m_gitCaretVisible = true;
    int m_unpushedCommitsCount = 0;
    std::set<std::wstring> m_expandedCommitHashes;

    // Hit-testing bounds
    D2D1_RECT_F m_gitHeaderMoreBounds{};
    D2D1_RECT_F m_gitChangesHeaderBounds{};
    D2D1_RECT_F m_gitChangesChevronBounds{};
    D2D1_RECT_F m_gitStageAllBounds{};
    D2D1_RECT_F m_gitDiscardAllBounds{};
    D2D1_RECT_F m_gitChangesMoreBounds{};

    D2D1_RECT_F m_gitMsgBoxBounds{};
    D2D1_RECT_F m_gitGenerateAiBounds{};
    D2D1_RECT_F m_gitCommitBtnBounds{};
    D2D1_RECT_F m_gitCommitDropdownBounds{};

    D2D1_RECT_F m_gitGraphHeaderBounds{};
    D2D1_RECT_F m_gitGraphChevronBounds{};
    D2D1_RECT_F m_gitGraphAutoBounds{};
    D2D1_RECT_F m_gitGraphBullseyeBounds{};
    D2D1_RECT_F m_gitGraphSplitBounds{};
    D2D1_RECT_F m_gitGraphMergeBounds{};
    D2D1_RECT_F m_gitGraphPushBounds{};
    D2D1_RECT_F m_gitGraphRefreshBounds{};
    D2D1_RECT_F m_gitGraphMoreBounds{};

    // Commit Card Popup state (Image 5)
    int m_hoveredCommitIdx = -1;
    int m_selectedCommitIdx = -1;
    D2D1_RECT_F m_commitCardBounds{};
    D2D1_RECT_F m_commitCardCloseBounds{};
    D2D1_RECT_F m_commitCardOpenChangesBounds{};
    D2D1_RECT_F m_commitCardGitHubBounds{};
    std::wstring m_commitCardHash;
    std::function<void()> m_onGitStateChanged;

    // Tooltip state
    std::wstring m_gitTooltipText;
    D2D1_RECT_F m_gitTooltipTarget{};
    bool m_showGitTooltip = false;

    // Multi-menu state (Header Views, Changes More, Graph More)
    GitMenuType m_activeGitMenu = GitMenuType::None;
    D2D1_RECT_F m_gitMenuBounds{};
    std::vector<GitMenuItemDef> m_gitMenuItems;
    int m_hoveredGitMenuItem = -1;

    // Draggable Splitter between Changes and Graph (Image 2 & 3)
    float m_gitSplitY = 320.0f;
    D2D1_RECT_F m_gitSplitterBounds{};

    // View popup menu (Image 3: Repositories, ✓ Changes, ✓ Graph)
    bool m_showGitViewMenu = false;
    D2D1_RECT_F m_gitViewMenuBounds{};
    D2D1_RECT_F m_gitViewMenuChangesBounds{};
    D2D1_RECT_F m_gitViewMenuGraphBounds{};

    size_t m_gitGraphScrollIdx = 0;
    size_t m_gitChangesScrollIdx = 0;
    int m_hoveredGitChangeIdx = -1;
    int m_hoveredGitCommitIdx = -1;

    void RebuildVisibleItems();
    void AddDirectoryChildren(const std::wstring& dirPath, int depth);
    std::wstring GetIconForFile(const std::wstring& filename, bool isDir, bool isExpanded) const;

    bool m_hasOpenFolder = false;
    std::wstring m_openFolderPath;
    std::wstring m_rootName;

    std::set<std::wstring> m_expandedDirs;
    std::vector<DrawerTreeItem> m_visibleItems;
    size_t m_scrollIndex = 0;           ///< Vertical scroll index for tree items

    int m_hoveredIndex = -1;
    bool m_hoveredOpenFolderBtn = false;
    D2D1_RECT_F m_rcOpenFolderBtn{};

    // Inline Tree Item Rename State
    bool m_isRenaming = false;
    int m_renamingItemIdx = -1;
    std::wstring m_renameText;
    std::wstring m_renameOriginalPath;
    D2D1_RECT_F m_rcRenameBox{};
    size_t m_renameSelStart = 0;
    size_t m_renameSelEnd = 0;
    size_t m_renameCaretPos = 0;
    std::wstring m_activeFilePath;

    // Inline Tree Item Create State (Images 2 & 3: VS Code style new file / new folder)
    bool m_isCreating = false;
    bool m_createIsFolder = false;
    std::wstring m_createParentDir;
    std::wstring m_createText;
    size_t m_createCaretPos = 0;
    D2D1_RECT_F m_rcCreateBox{};
    D2D1_RECT_F m_rcCreatePill{};
    int m_createInsertAfterIdx = -1;
    int m_createDepth = 0;

    // Search Pane State (Image 2)
    std::wstring m_searchQuery;
    std::wstring m_replaceQuery;
    bool m_matchCase = false;
    bool m_matchWholeWord = false;
    bool m_useRegex = false;
    bool m_preserveCase = false;
    bool m_isReplaceOpen = true;
    int m_focusedSearchInput = 1;       ///< 1 = Search, 2 = Replace, 0 = Unfocused
    std::vector<SearchFileGroup> m_searchGroups;
    std::function<void()> m_onRefreshOpenDocs;
    std::function<void(const std::vector<std::wstring>&)> m_onFilesReplaced;
    int m_hoveredSearchGroupIdx = -1;
    int m_hoveredSearchMatchIdx = -1;
    POINT m_searchMousePos{ -1, -1 };
    size_t m_totalMatches = 0;
    size_t m_searchScrollIdx = 0;

    // Search Throttler State (rate-limits searches across intervals, flushes trailing queries)
    ULONGLONG m_lastSearchExecutionTime = 0;
    ULONGLONG m_lastKeystrokeTime = 0;
    ULONGLONG m_throttleIntervalMs = 300;
    bool m_searchPending = false;

    // Asynchronous Search Execution & Garbage Collection
    std::mutex m_searchMutex;
    std::atomic<uint32_t> m_searchGeneration{0};
    std::atomic<bool> m_isSearching{false};
    std::vector<SearchFileGroup> m_pendingSearchGroups;
    size_t m_pendingTotalMatches = 0;
    bool m_hasPendingResults = false;

    // Search Pagination & Incremental Scrolling
    std::vector<std::filesystem::path> m_searchCandidateFiles;
    size_t m_searchCandidateFileIndex = 0;
    bool m_searchHasMore = false;
    std::atomic<bool> m_isFetchingMore{false};
    bool m_isBatchAppend = false;

    // Search Tooltip State
    std::wstring m_searchTooltipText;
    D2D1_RECT_F m_searchTooltipAnchor{};

    D2D1_RECT_F m_rcSearchBox{};
    D2D1_RECT_F m_rcReplaceBox{};
    D2D1_RECT_F m_rcToggleReplaceChevron{};
    D2D1_RECT_F m_rcMatchCaseBtn{};
    D2D1_RECT_F m_rcMatchWholeWordBtn{};
    D2D1_RECT_F m_rcUseRegexBtn{};
    D2D1_RECT_F m_rcPreserveCaseBtn{};
    D2D1_RECT_F m_rcReplaceAllBtn{};
    D2D1_RECT_F m_rcRefreshSearchBtn{};

    // Search Details / Include Exclude State (Images 2, 3, 4)
    bool m_isSearchDetailsOpen = false;
    bool m_searchOnlyOpenEditors = false;
    bool m_useExcludeSettings = true;
    std::wstring m_filesToInclude;
    std::wstring m_filesToExclude;
    std::vector<std::wstring> m_openDocPaths;

    D2D1_RECT_F m_rcToggleSearchDetailsBtn{};
    D2D1_RECT_F m_rcFilesToIncludeBox{};
    D2D1_RECT_F m_rcFilesToExcludeBox{};
    D2D1_RECT_F m_rcOpenEditorsOnlyBtn{};
    D2D1_RECT_F m_rcUseExcludeSettingsBtn{};

    LucideIcons m_icons;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> m_pDrawerBgGeometry;
    float m_cachedRailW = -1.0f;
    float m_cachedHeaderH = -1.0f;
    float m_cachedDrawerW = -1.0f;
    float m_cachedClientH = -1.0f;
};

} // namespace phide::ui
