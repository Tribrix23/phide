/**
 * @file QuickOpenModal.hpp
 * @brief Modal quick open palette (Ctrl+K, Ctrl+P) for searching files and commands with file extension icons.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

/**
 * @struct QuickOpenItem
 * @brief Represents a file, action, or command in the Quick Open palette.
 */
struct QuickOpenItem {
    enum class Type {
        Action,
        File
    } type = Type::File;

    std::wstring title;         ///< File name or Action label
    std::wstring description;   ///< Directory path or Action suffix
    std::wstring fullPath;      ///< Full file path (for Type::File)
    std::wstring shortcut;      ///< Shortcut badge (e.g. "Ctrl + P")
    FileIconType iconType = FileIconType::DefaultFile;
    bool isRecent = false;
    D2D1_RECT_F bounds{};
};

/**
 * @class QuickOpenModal
 * @brief Manages the Quick Open search dialog matching user reference Image 5.
 */
class QuickOpenModal {
public:
    QuickOpenModal();
    ~QuickOpenModal() = default;

    /**
     * @brief Opens the quick open palette with recent files and workspace scan.
     */
    void Open(const std::vector<std::wstring>& recentPaths,
              const std::vector<std::wstring>& openPaths,
              const std::wstring& workspaceFolder);

    /**
     * @brief Dismisses the quick open modal.
     */
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    /**
     * @brief Renders the quick open palette overlay.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientW, float clientH);

    /**
     * @brief Handles mouse movement and hover over items.
     */
    bool HandleMouseMove(POINT pt);

    /**
     * @brief Handles mouse clicks on items or background.
     */
    bool HandleMouseDown(POINT pt, std::wstring& outSelectedFile);

    /**
     * @brief Handles keyboard navigation (arrows, Enter, Escape, Backspace).
     */
    bool HandleKeyDown(WPARAM wParam, std::wstring& outSelectedFile);

    /**
     * @brief Handles printable character typing into the search input.
     */
    bool HandleChar(wchar_t ch);

    /**
     * @brief Handles mouse wheel scrolling of results list.
     */
    bool HandleMouseWheel(short delta);

    [[nodiscard]] const std::vector<QuickOpenItem>& GetFilteredItems() const noexcept { return m_filteredItems; }
    [[nodiscard]] const std::vector<QuickOpenItem>& GetItems() const noexcept { return m_filteredItems; }
    [[nodiscard]] const std::wstring& GetQuery() const noexcept { return m_query; }
    [[nodiscard]] const std::wstring& GetFilterQuery() const noexcept { return m_query; }
    [[nodiscard]] int GetSelectedIndex() const noexcept { return m_selectedIndex; }
    void SetQuery(const std::wstring& q) { m_query = q; FilterItems(); }

private:
    void FilterItems();

    bool m_isOpen = false;
    std::wstring m_query;
    int m_selectedIndex = 0;
    int m_hoveredIndex = -1;
    float m_scrollOffset = 0.0f;

    std::vector<QuickOpenItem> m_allItems;
    std::vector<QuickOpenItem> m_filteredItems;

    D2D1_RECT_F m_modalBounds{};
    D2D1_RECT_F m_inputBounds{};

    LucideIcons m_icons;
};

} // namespace phide::ui
