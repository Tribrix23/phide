/**
 * @file TitleHeader.hpp
 * @brief Dark IDE frameless header containing menu bar, title, and Lucide vector caption buttons.
 * 
 * Features crisp vector-drawn window controls (minimize, maximize, close) with red hover
 * background on the Close button, seamless title dragging, and no horizontal divider line.
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
 * @enum CaptionButtonType
 * @brief Identifies top-right window control buttons.
 */
enum class CaptionButtonType {
    None,
    Minimize,   ///< Minimize window to taskbar
    Maximize,   ///< Maximize / Restore window
    Close       ///< Close / Exit application
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
 * @brief Renders the dark IDE title bar, menus, and top-right caption controls.
 */
class TitleHeader {
public:
    TitleHeader();
    ~TitleHeader() = default;

    /**
     * @brief Paints the header background, menus, centered title, and caption controls.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark brushes and text formats.
     * @param config Application configuration and DPI metrics.
     * @param clientWidth Width of the client window in physical pixels.
     * @param isZoomed True if window is currently maximized.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float clientWidth, bool isZoomed);

    /**
     * @brief Tests if mouse is over a menu item.
     * @param pt Mouse point in client coordinates.
     * @return HeaderMenuId or HeaderMenuId::None.
     */
    HeaderMenuId HitTestMenu(POINT pt) const;

    /**
     * @brief Tests if mouse is over a caption button (min, max, close).
     * @param pt Mouse point in client coordinates.
     * @return CaptionButtonType under the cursor.
     */
    CaptionButtonType HitTestCaptionButton(POINT pt) const;

    /**
     * @brief Determines whether the cursor is in a draggable title bar zone or over interactive buttons.
     * @param pt Mouse point in client coordinates.
     * @param config Application configuration.
     * @return LRESULT HTCAPTION if draggable, HTCLIENT if over a button.
     */
    LRESULT HitTestNCHitTest(POINT pt, const app::AppConfig& config);

    /**
     * @brief Updates hover state for menus and caption buttons.
     * @param hoveredMenu Currently hovered menu.
     * @param hoveredCaption Currently hovered caption button.
     * @return True if state changed and repaint is needed.
     */
    bool UpdateHoverState(HeaderMenuId hoveredMenu, CaptionButtonType hoveredCaption);

    [[nodiscard]] HeaderMenuId GetHoveredMenu() const noexcept { return m_hoveredMenu; }
    [[nodiscard]] CaptionButtonType GetHoveredCaption() const noexcept { return m_hoveredCaption; }

private:
    HeaderMenuId m_hoveredMenu = HeaderMenuId::None;
    CaptionButtonType m_hoveredCaption = CaptionButtonType::None;

    std::vector<MenuItem> m_menus;
    LucideIcons m_icons;

    // Bounding rectangles for caption buttons
    D2D1_RECT_F m_rcMinimize{};
    D2D1_RECT_F m_rcMaximize{};
    D2D1_RECT_F m_rcClose{};
};

} // namespace phide::ui
