/**
 * @file ActivityRail.hpp
 * @brief Dark IDE collapsed left sidebar containing Home, Search, and Git with customized tooltips.
 * 
 * Implements the requested icon hierarchy:
 * 1. Home icon in a rounded square badge
 * 2. Search icon directly under Home
 * 3. Git icon directly under Search
 * 
 * Features customized floating tooltip cards rendered on hover with rich styling.
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
 * @enum RailTabId
 * @brief Identifies active perspectives in the dark IDE activity rail.
 */
enum class RailTabId {
    None = -1,
    Home = 0,       ///< Home perspective / welcome dashboard
    Search = 1,     ///< Search & Replace across workspace
    Git = 2         ///< Source Control (Git)
};

/**
 * @struct RailItem
 * @brief Represents a single tool button on the activity rail with custom tooltip information.
 */
struct RailItem {
    RailTabId id;
    const wchar_t* glyph;
    const wchar_t* tooltipText;
    D2D1_RECT_F bounds{};
};

/**
 * @class ActivityRail
 * @brief Handles rendering, rounded button geometry, hit-testing, and floating tooltips.
 */
class ActivityRail {
public:
    ActivityRail();
    ~ActivityRail() = default;

    /**
     * @brief Paints the activity rail background, divider, button items, and customized tooltip.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark brushes and typography.
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
     * @brief Updates the hover state when mouse moves over the rail.
     * @param hoveredId Currently hovered item.
     * @return True if state changed and a repaint is needed.
     */
    bool SetHoveredTab(RailTabId hoveredId);

    [[nodiscard]] RailTabId GetHoveredTab() const noexcept { return m_hoveredTab; }

private:
    RailTabId m_hoveredTab = RailTabId::None;
    std::vector<RailItem> m_items;

    /**
     * @brief Renders the customized floating tooltip card for the currently hovered item.
     */
    void RenderCustomTooltip(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                             const RailItem& item);
};

} // namespace phide::ui
