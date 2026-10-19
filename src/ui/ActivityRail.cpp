/**
 * @file ActivityRail.cpp
 * @brief Implementation of the Dark IDE activity rail.
 */

#include "phide/ui/ActivityRail.hpp"

namespace phide::ui {

ActivityRail::ActivityRail() {
    // Tool Perspectives (Files/Explorer, Search, Git, Run/Debug)
    m_items.push_back({ RailTabId::Explorer,      L"\u25A4", L"Explorer (Files)",        {} });
    m_items.push_back({ RailTabId::Search,        L"\u26B2", L"Search Workspace",        {} });
    m_items.push_back({ RailTabId::SourceControl, L"\u23F1", L"Source Control (Git)",   {} });
    m_items.push_back({ RailTabId::RunDebug,      L"\u25B6", L"Run & Debug (F5)",        {} });
}

void ActivityRail::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight) {
    if (!pRT) return;

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float btnSize = config.ScaleF(static_cast<float>(config.railBtnSize));
    const float spacing = config.ScaleF(8.0f);
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

    // 2. Render Tool Buttons
    float curY = headerH + config.ScaleF(10.0f);

    for (auto& item : m_items) {
        item.bounds = D2D1::RectF(startX, curY, startX + btnSize, curY + btnSize);

        bool isHovered = (m_hoveredTab == item.id);
        bool isActive  = (config.isDrawerOpen && config.activeRailTab == static_cast<int>(item.id));

        if (isActive) {
            // Active background
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrc, theme.brushItemActive.Get());

            // Left bright accent indicator line (VS Code / JetBrains style)
            pRT->DrawLine(
                D2D1::Point2F(1.0f, item.bounds.top + 4.0f),
                D2D1::Point2F(1.0f, item.bounds.bottom - 4.0f),
                theme.brushBrandAccent.Get(),
                2.5f
            );
        } else if (isHovered) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        }

        ID2D1SolidColorBrush* textBrush = (isActive || isHovered)
            ? theme.brushTextPrimary.Get()
            : theme.brushTextSecondary.Get();

        UINT32 glyphLen = static_cast<UINT32>(wcslen(item.glyph));
        pRT->DrawText(item.glyph, glyphLen, theme.fontIcon.Get(), item.bounds, textBrush);

        curY += btnSize + spacing;
    }
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
