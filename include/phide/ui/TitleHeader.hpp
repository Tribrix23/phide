/**
 * @file TitleHeader.hpp
 * @brief Clean Dark IDE frameless header containing menu bar and project title.
 * 
 * Removes browser-style navigation and web action buttons as requested.
 * Integrates natively with Windows 11 DWM dark caption buttons without duplication.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @enum HeaderMenuId
 * @brief Identifies menu bar items in the header.
 */
enum class HeaderMenuId {
    None = -1,
    File = 0,
    Edit = 1,
    View = 2,
    Project = 3,
    Help = 4
};

/**
 * @struct MenuItem
 * @brief Visual descriptor for a top menu item.
 */
struct MenuItem {
    HeaderMenuId id;
    const wchar_t* label;
    D2D1_RECT_F bounds{};
};

/**
 * @class TitleHeader
 * @brief Renders the dark IDE title bar and routes menu clicks.
 */
class TitleHeader {
public:
    TitleHeader();
    ~TitleHeader() = default;

    /**
     * @brief Paints the header bar background, menu bar, and centered title.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark brushes and text formats.
     * @param config Application configuration and DPI metrics.
     * @param clientWidth Width of the client window in physical pixels.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientWidth);

    /**
     * @brief Determines which menu item is located at the given coordinates.
     * @param pt Mouse cursor position in client coordinates.
     * @return HeaderMenuId under the cursor, or HeaderMenuId::None.
     */
    HeaderMenuId HitTest(POINT pt) const;

    /**
     * @brief Determines whether the cursor is in a draggable title bar zone or over menus.
     * @param pt Mouse cursor position in client coordinates.
     * @param config Application configuration.
     * @param clientWidth Width of the client window.
     * @return LRESULT HTCAPTION, HTCLIENT, or -1 for native caption button pass-through.
     */
    LRESULT HitTestNCHitTest(POINT pt, const app::AppConfig& config, float clientWidth);

    /**
     * @brief Updates hover state when the mouse cursor moves across menu items.
     * @param hoveredId Currently hovered menu.
     * @return True if state changed and a repaint is required.
     */
    bool SetHoveredMenu(HeaderMenuId hoveredId);

    [[nodiscard]] HeaderMenuId GetHoveredMenu() const noexcept { return m_hoveredMenu; }

private:
    HeaderMenuId m_hoveredMenu = HeaderMenuId::None;
    std::vector<MenuItem> m_menus;
};

} // namespace phide::ui
