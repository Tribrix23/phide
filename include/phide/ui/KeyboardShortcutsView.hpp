/**
 * @file KeyboardShortcutsView.hpp
 * @brief Interactive VS Code-styled Keyboard Shortcuts table view with live search filtering and keycap badges.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @struct ShortcutItem
 * @brief Defines a command and its assigned keyboard shortcut combination.
 */
struct ShortcutItem {
    std::wstring command;
    std::vector<std::wstring> keyPills; ///< e.g. {"Ctrl", "K"}, {"Ctrl", "S"}
    std::wstring when;                  ///< e.g. "global", "editorTextFocus"
    std::wstring source;                ///< e.g. "Default", "User"
    std::wstring id;                    ///< Stable action id (e.g. "cmd_save", "cmd_find")
    std::vector<std::wstring> defaultKeyPills; ///< Original default keys for reset
    std::wstring conflictCommand;       ///< Non-empty when colliding with another binding
};

/**
 * @class KeyboardShortcutsView
 * @brief Dedicated editor surface rendering the searchable keybindings table.
 */
class KeyboardShortcutsView {
public:
    KeyboardShortcutsView();
    ~KeyboardShortcutsView() = default;

    /**
     * @brief Renders the searchable shortcuts view.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float left, float width, float top, float height);

    /**
     * @brief Handles mouse movement and hover over rows.
     */
    bool HandleMouseMove(POINT clientPt);

    /**
     * @brief Handles mouse clicks (e.g. focusing search bar).
     */
    bool HandleLButtonDown(POINT clientPt);

    /**
     * @brief Handles mouse wheel scrolling.
     */
    bool HandleMouseWheel(short delta);

    /**
     * @brief Handles character typed into the search bar.
     */
    bool HandleChar(wchar_t ch);

    /**
     * @brief Handles special keys (Backspace, Delete, Escape) in search bar.
     */
    bool HandleKeyDown(WPARAM key);

    [[nodiscard]] bool IsSearchFocused() const noexcept { return m_isSearchFocused; }
    void SetSearchFocused(bool focused) noexcept { m_isSearchFocused = focused; }
    [[nodiscard]] const std::wstring& GetSearchQuery() const noexcept { return m_searchQuery; }
    [[nodiscard]] size_t GetShortcutsCount() const noexcept { return m_shortcuts.size(); }

    // Interactive Editable Keybindings & Conflict Detection (#49)
    [[nodiscard]] bool IsRecordingKeybinding() const noexcept { return m_isRecording; }
    [[nodiscard]] int GetRecordingItemIdx() const noexcept { return m_recordingItemIdx; }
    void BeginRecordingRow(int filteredRowIdx);
    void BeginRecordingItem(int itemIdx);
    void CancelRecording() noexcept;
    bool CommitRecordedKeybinding();
    bool ResetItemToDefault(int itemIdx);
    bool HandleKeyRecording(WPARAM vk, bool isCtrl, bool isShift, bool isAlt);

    [[nodiscard]] const std::vector<ShortcutItem>& GetShortcuts() const noexcept { return m_shortcuts; }
    bool SetShortcutKeys(const std::wstring& idOrCommand, const std::vector<std::wstring>& newKeyPills);
    [[nodiscard]] std::vector<std::wstring> FindConflictsForPills(
        const std::vector<std::wstring>& pills,
        int excludeItemIdx,
        const std::wstring& whenScope) const;

    bool LoadUserKeybindings(const std::wstring& jsonPath);
    bool SaveUserKeybindings(const std::wstring& jsonPath) const;

    [[nodiscard]] static std::vector<std::wstring> BuildKeyPillsFromEvent(bool isCtrl, bool isShift, bool isAlt, UINT vk);
    [[nodiscard]] std::wstring ResolveOverrideActionForKey(
        bool isCtrl, bool isShift, bool isAlt, UINT vk, bool hasOpenEditor,
        bool isDebugActive = false, bool isTerminalFocused = false,
        bool isExplorerFocused = false, bool isScmFocused = false);
    [[nodiscard]] bool IsActionDefaultUnchanged(const std::wstring& actionId) const;

private:
    std::vector<ShortcutItem> m_shortcuts;
    std::wstring m_searchQuery;
    std::wstring m_userKeybindingsPath;
    bool m_isSearchFocused = true;
    bool m_cursorVisible = true;
    float m_scrollOffsetY = 0.0f;
    int m_hoveredRow = -1;

    bool m_isRecording = false;
    int m_recordingItemIdx = -1;
    std::vector<std::wstring> m_recordedPills;
    std::wstring m_recordingConflictWarning;
    std::vector<std::wstring> m_pendingChordPrefix;
    ULONGLONG m_pendingChordTick = 0;

    struct RowActionRect {
        int itemIdx = -1;
        D2D1_RECT_F editBounds{};
        D2D1_RECT_F resetBounds{};
    };
    mutable std::vector<RowActionRect> m_rowActionRects;
    D2D1_RECT_F m_recordCommitBounds{};
    D2D1_RECT_F m_recordCancelBounds{};

    D2D1_RECT_F m_searchBoxBounds{};
    D2D1_RECT_F m_viewBounds{};

    void InitializeShortcuts();
    void PopulateDefaultIdsAndBackups();
    void RecomputeConflicts();
    [[nodiscard]] std::vector<size_t> GetFilteredIndices() const;
    void DrawKeycapBadge(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         const std::wstring& text, float& curX, float centerY, float badgeH,
                         bool isConflict = false, bool isUserModified = false);
};

} // namespace phide::ui
