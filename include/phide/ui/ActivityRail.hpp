/**
 * @file ActivityRail.hpp
 * @brief Dark IDE collapsed 48px left sidebar rail containing essential tool perspectives.
 * 
 * Removes the bottom extensions and avatar buttons marked with X by the user.
 * Displays clean IDE perspective buttons: Explorer, Search, Source Control, and Run/Debug.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @enum RailTabId
 * @brief Identifies active perspectives in the dark IDE activity rail.
 */
enum class RailTabId {
    None = -1,
    Explorer = 0,       ///< Project File Tree
    Search = 1,         ///< Global Search & Replace
    SourceControl = 2,  ///< Git status and commits
    RunDebug = 3        ///< Build, Run, and Debugger
};

/**
 * @struct RailItem
 * @brief Represents a single tool button on the activity rail.
 */
struct RailItem {
    RailTabId id;
    const wchar_t* glyph;
    const wchar_t* tooltip;
    D2D1_RECT_F bounds{};
};

/**
 * @class ActivityRail
 * @brief Handles rendering and mouse interaction for the vertical icon strip.
 */
class ActivityRail {
public:
    ActivityRail();
    ~ActivityRail() = default;

    /**
     * @brief Paints the activity rail background, divider, and all button items.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark brushes and fonts.
     * @param config Application configuration.
     * @param clientHeight Height of the window client area.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);

    /**
     * @brief Performs hit-testing against rail items.
     * @param pt Mouse position in client coordinates.
     * @return RailTabId of the item hit, or RailTabId::None.
     */
    RailTabId HitTest(POINT pt) const;

    /**
     * @brief Updates the hover state.
     * @param hoveredId Currently hovered item.
     * @return True if state changed and a repaint is needed.
     */
    bool SetHoveredTab(RailTabId hoveredId);

    [[nodiscard]] RailTabId GetHoveredTab() const noexcept { return m_hoveredTab; }

private:
    RailTabId m_hoveredTab = RailTabId::None;
    std::vector<RailItem> m_items;
};

} // namespace phide::ui
