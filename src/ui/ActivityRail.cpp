/**
 * @file ActivityRail.cpp
 * @brief Implementation of the Dark IDE activity rail with Lucide Home, Search, Git, and customized tooltips.
 */

#include "phide/ui/ActivityRail.hpp"

namespace phide::ui {

ActivityRail::ActivityRail() {
    // 1. Home icon (top)
    m_items.push_back({ RailTabId::Home,   L"Home",                        {} });

    // 2. Search icon (directly under Home)
    m_items.push_back({ RailTabId::Search, L"Search Workspace (Ctrl+F)",   {} });

    // 3. Git icon (directly under Search)
    m_items.push_back({ RailTabId::Git,    L"Source Control (Git)",        {} });
}

void ActivityRail::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight) {
    if (!pRT) return;

    // Initialize vector icon stroke styles if not yet created
    ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        m_lucide.Initialize(pFactory.Get());
    }

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float btnSize = config.ScaleF(static_cast<float>(config.railBtnSize)); // 36px
    const float spacing = config.ScaleF(10.0f);
    const float startX  = (railW - btnSize) / 2.0f;

    // 1. Draw Rail Background (#141414)
    D2D1_RECT_F rcRail = D2D1::RectF(0.0f, headerH, railW, clientHeight);
    pRT->FillRectangle(rcRail, theme.brushRailBg.Get());

    // Right border divider (#2C2C2C)
    pRT->DrawLine(
        D2D1::Point2F(railW - 0.5f, headerH),
        D2D1::Point2F(railW - 0.5f, clientHeight),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    // 2. Render Lucide Vector Icons: Home -> Search -> Git
    float curY = headerH + config.ScaleF(12.0f);
    const RailItem* hoveredItemPtr = nullptr;

    for (auto& item : m_items) {
        item.bounds = D2D1::RectF(startX, curY, startX + btnSize, curY + btnSize);

        bool isHovered = (m_hoveredTab == item.id);
        bool isActive  = (config.isDrawerOpen && config.activeRailTab == static_cast<int>(item.id));

        if (isHovered) {
            hoveredItemPtr = &item;
        }

        if (item.id == RailTabId::Home) {
            // Home icon: styled as a modern rounded square badge (8px radius)
            D2D1_ROUNDED_RECT rrcHome = D2D1::RoundedRect(item.bounds, 8.0f, 8.0f);

            if (isHovered) {
                pRT->FillRoundedRectangle(rrcHome, theme.brushItemHover.Get());
                pRT->DrawRoundedRectangle(rrcHome, theme.brushBorderHover.Get(), 1.0f);
                m_lucide.DrawHome(pRT, item.bounds, theme.brushTextWhite.Get(), 1.8f);
            } else {
                pRT->FillRoundedRectangle(rrcHome, theme.brushHomeBadgeBg.Get());
                m_lucide.DrawHome(pRT, item.bounds, theme.brushTextPrimary.Get(), 1.8f);
            }
        } else if (item.id == RailTabId::Search) {
            // Search icon directly under Home
            if (isActive) {
                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 6.0f, 6.0f);
                pRT->FillRoundedRectangle(rrc, theme.brushItemActive.Get());

                pRT->DrawLine(
                    D2D1::Point2F(1.0f, item.bounds.top + 4.0f),
                    D2D1::Point2F(1.0f, item.bounds.bottom - 4.0f),
                    theme.brushBrandAccent.Get(),
                    2.5f
                );
                m_lucide.DrawSearch(pRT, item.bounds, theme.brushTextWhite.Get(), 1.8f);
            } else if (isHovered) {
                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 6.0f, 6.0f);
                pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
                m_lucide.DrawSearch(pRT, item.bounds, theme.brushTextWhite.Get(), 1.8f);
            } else {
                m_lucide.DrawSearch(pRT, item.bounds, theme.brushTextSecondary.Get(), 1.8f);
            }
        } else if (item.id == RailTabId::Git) {
            // Git Branch icon directly under Search
            if (isActive) {
                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 6.0f, 6.0f);
                pRT->FillRoundedRectangle(rrc, theme.brushItemActive.Get());

                pRT->DrawLine(
                    D2D1::Point2F(1.0f, item.bounds.top + 4.0f),
                    D2D1::Point2F(1.0f, item.bounds.bottom - 4.0f),
                    theme.brushBrandAccent.Get(),
                    2.5f
                );
                m_lucide.DrawGitBranch(pRT, item.bounds, theme.brushTextWhite.Get(), 1.8f);
            } else if (isHovered) {
                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 6.0f, 6.0f);
                pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
                m_lucide.DrawGitBranch(pRT, item.bounds, theme.brushTextWhite.Get(), 1.8f);
            } else {
                m_lucide.DrawGitBranch(pRT, item.bounds, theme.brushTextSecondary.Get(), 1.8f);
            }
        }

        curY += btnSize + spacing;
    }

    // 3. Render Customized Floating Tooltip Card for Hovered Icon
    if (hoveredItemPtr) {
        RenderCustomTooltip(pRT, theme, config, *hoveredItemPtr);
    }
}

void ActivityRail::RenderCustomTooltip(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                      const RailItem& item) {
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float btnSize = config.ScaleF(static_cast<float>(config.railBtnSize));
    const float tipH    = config.ScaleF(26.0f);
    const float tipPadX = config.ScaleF(12.0f);

    // Calculate tooltip text width
    size_t textLen = wcslen(item.tooltipText);
    float textW = config.ScaleF(static_cast<float>(textLen * 7 + 16));
    float tipW = textW + tipPadX * 2.0f;

    // Position tooltip to the right of the rail, vertically centered on the active icon
    float tipX = railW + config.ScaleF(10.0f);
    float tipY = item.bounds.top + (btnSize - tipH) / 2.0f;

    D2D1_RECT_F rcTip = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
    D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 5.0f, 5.0f);

    // Draw Tooltip Card Background (#252526) & Border (#3E3E3E)
    pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg.Get());
    pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder.Get(), 1.0f);

    // Draw Small Pointer Triangle pointing left towards the rail button
    D2D1_POINT_2F p1 = D2D1::Point2F(tipX, tipY + tipH / 2.0f);
    D2D1_POINT_2F p2 = D2D1::Point2F(tipX - config.ScaleF(5.0f), tipY + tipH / 2.0f);
    pRT->DrawLine(p1, p2, theme.brushTooltipBorder.Get(), 1.0f);

    // Draw Crisp White Tooltip Label Text
    D2D1_RECT_F rcText = D2D1::RectF(tipX + tipPadX, tipY, tipX + tipW - tipPadX, tipY + tipH);
    pRT->DrawText(
        item.tooltipText,
        static_cast<UINT32>(textLen),
        theme.fontTooltip.Get(),
        rcText,
        theme.brushTextWhite.Get()
    );
}

RailTabId ActivityRail::HitTest(POINT pt) const {
    for (const auto& item : m_items) {
        if (pt.x >= item.bounds.left && pt.x < item.bounds.right &&
            pt.y >= item.bounds.top && pt.y < item.bounds.bottom) {
            return item.id;
        }
    }
    return RailTabId::None;
}

bool ActivityRail::SetHoveredTab(RailTabId hoveredId) {
    if (m_hoveredTab != hoveredId) {
        m_hoveredTab = hoveredId;
        return true;
    }
    return false;
}

} // namespace phide::ui
