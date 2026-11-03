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

private:
    std::vector<ShortcutItem> m_shortcuts;
    std::wstring m_searchQuery;
    bool m_isSearchFocused = true;
    bool m_cursorVisible = true;
    float m_scrollOffsetY = 0.0f;
    int m_hoveredRow = -1;

    D2D1_RECT_F m_searchBoxBounds{};
    D2D1_RECT_F m_viewBounds{};

    void InitializeShortcuts();
    [[nodiscard]] std::vector<size_t> GetFilteredIndices() const;
    void DrawKeycapBadge(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         const std::wstring& text, float& curX, float centerY, float badgeH);
};

} // namespace phide::ui
