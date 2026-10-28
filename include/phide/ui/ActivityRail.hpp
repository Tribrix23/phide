/**
 * @file ActivityRail.hpp
 * @brief Dark IDE collapsed left sidebar containing Lucide Files, Search, and GitBranch icons with customized tooltips.
 * 
 * Implements GPU-accelerated Lucide vector rendering for:
 * 1. Lucide File icon for Project Explorer
 * 2. Lucide Search icon directly under File
 * 3. Lucide Git Branch icon directly under Search
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
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

/**
 * @enum RailTabId
 * @brief Identifies active perspectives in the dark IDE activity rail.
 */
enum class RailTabId {
    None = -1,
    Files = 0,          ///< Files / Explorer perspective (two overlapping sheets)
    Search = 1,         ///< Search across workspace
    Git = 2,            ///< Source Control (Git)
    Packages = 3,       ///< Package Manager (PyCharm style)
    Notebook = 4        ///< Interactive Notebook (Jupyter / Colab)
};

/**
 * @struct RailItem
 * @brief Represents a single tool button on the activity rail with custom tooltip information.
 */
struct RailItem {
    RailTabId id;
    const wchar_t* tooltipText;
    D2D1_RECT_F bounds{};
};

/**
 * @class ActivityRail
 * @brief Handles rendering, Lucide vector icons, hit-testing, and floating tooltips.
 */
class ActivityRail {
public:
    ActivityRail();
    ~ActivityRail() = default;

    /**
     * @brief Releases device-dependent Direct2D resources.
     */
    void DiscardDeviceResources() {
        m_pRailGradientBrush.Reset();
        m_pLogoBitmap.Reset();
        m_lastRailH = -1.0f;
    }

    /**
     * @brief Paints the activity rail background, divider, Lucide icons, and customized tooltip.
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

    void SetGitLoading(bool loading) noexcept { m_isGitLoading = loading; }
    [[nodiscard]] bool IsGitLoading() const noexcept { return m_isGitLoading; }
    void UpdateGitAnim(float deltaAngle) noexcept { m_gitAnimAngle += deltaAngle; }
    void SetGitBadgeCount(size_t count) noexcept { m_gitBadgeCount = count; }
    [[nodiscard]] size_t GetGitBadgeCount() const noexcept { return m_gitBadgeCount; }
    [[nodiscard]] const std::vector<RailItem>& GetItems() const noexcept { return m_items; }

private:
    RailTabId m_hoveredTab = RailTabId::None;
    bool m_isGitLoading = false;
    float m_gitAnimAngle = 0.0f;
    size_t m_gitBadgeCount = 0;
    std::vector<RailItem> m_items;
    LucideIcons m_lucide;

    /**
     * @brief Renders the customized floating tooltip card for the currently hovered item.
     */
    void RenderCustomTooltip(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                             const RailItem& item);

    // Direct2D Purple Glass Vertical Gradient Brush and theme state cache
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_pRailGradientBrush;
    float m_lastRailH = -1.0f;
    bool  m_lastIsDark = true; ///< Tracks theme transitions to invalidate gradient cache
    ThemeMode m_lastThemeMode = static_cast<ThemeMode>(-1);

    // Phide Square Brand Logo (embedded from user image)
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_pLogoBitmap;
    D2D1_RECT_F m_rcLogo{};
};

} // namespace phide::ui
