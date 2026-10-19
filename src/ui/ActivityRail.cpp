/**
 * @file ActivityRail.cpp
 * @brief Implementation of the Dark IDE activity rail with Lucide Files, Search, Git, and customized tooltips.
 */

#include "phide/ui/ActivityRail.hpp"
#include "phide/ui/PhideLogo.hpp"

namespace phide::ui {

ActivityRail::ActivityRail() {
    // 1. Files / Explorer icon (two overlapping sheets, matching VS Code)
    m_items.push_back({ RailTabId::Files,  L"Explorer",                      {} });

    // 2. Search icon (directly under Files)
    m_items.push_back({ RailTabId::Search, L"Search (Ctrl+Shift+F)",         {} });

    // 3. Git icon (directly under Search)
    m_items.push_back({ RailTabId::Git,    L"Source Control (Ctrl+Shift+G)", {} });
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

    // Ensure vertical purple glass gradient brush is initialized/updated
    if (!m_pRailGradientBrush || std::abs(m_lastRailH - clientHeight) >= 1.0f) {
        m_lastRailH = clientHeight;
        m_pRailGradientBrush.Reset();

        // Getting darker downwards, fading to pure black at the bottom (matching user request)
        D2D1_GRADIENT_STOP stops[5];
        stops[0] = { 0.00f, D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f) }; // #3D1461 - Seamlessly matches header's left edge
        stops[1] = { 0.20f, D2D1::ColorF(0.16f, 0.05f, 0.26f, 1.0f) }; // #290D42 - Deep Violet
        stops[2] = { 0.45f, D2D1::ColorF(0.09f, 0.03f, 0.15f, 1.0f) }; // #170826 - Darkening Twilight
        stops[3] = { 0.70f, D2D1::ColorF(0.05f, 0.02f, 0.07f, 1.0f) }; // #0D0512 - Very Dark
        stops[4] = { 1.00f, D2D1::ColorF(0.03f, 0.03f, 0.04f, 1.0f) }; // #08080A - Pure Obsidian Black at bottom

        ComPtr<ID2D1GradientStopCollection> pStopColl;
        if (SUCCEEDED(pRT->CreateGradientStopCollection(stops, 5, &pStopColl))) {
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0.0f, 0.0f),
                D2D1::Point2F(0.0f, clientHeight)
            );
            pRT->CreateLinearGradientBrush(props, pStopColl.Get(), &m_pRailGradientBrush);
        }
    }

    // 1. Draw Rail Background with Vertical Purple Glass Gradient (seamless fusion with header)
    D2D1_RECT_F rcRail = D2D1::RectF(0.0f, 0.0f, railW, clientHeight);
    if (m_pRailGradientBrush) {
        pRT->FillRectangle(rcRail, m_pRailGradientBrush.Get());
    } else {
        pRT->FillRectangle(rcRail, theme.brushRailBg.Get());
    }

    // Right border divider: starts below the upper-left rounded corner (no line in header or clashing with curve)
    const float dividerStartY = headerH + config.ScaleF(14.0f);
    pRT->DrawLine(
        D2D1::Point2F(railW - 0.5f, dividerStartY),
        D2D1::Point2F(railW - 0.5f, clientHeight),
        theme.brushBorderSubtle.Get(),
        0.8f
    );

    // 2. Render Square Phide Brand Logo in Upper-Left Box ([0..railW] x [0..headerH])
    if (!m_pLogoBitmap) {
        D2D1_SIZE_U size = D2D1::SizeU(kPhideLogoWidth, kPhideLogoHeight);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        pRT->CreateBitmap(size, g_phideLogoBgra, kPhideLogoWidth * 4, props, &m_pLogoBitmap);
    }

    const float logoSize = config.ScaleF(28.0f);
    const float logoX = (railW - logoSize) / 2.0f;
    const float logoY = (headerH - logoSize) / 2.0f;
    m_rcLogo = D2D1::RectF(logoX, logoY, logoX + logoSize, logoY + logoSize);

    if (m_pLogoBitmap) {
        // Render the user's Phide icon strictly as a crisp square ("it should stay as quare")
        pRT->DrawBitmap(m_pLogoBitmap.Get(), m_rcLogo);
    }

    // 3. Render Lucide Vector Icons matching Image 2
    float curY = headerH + config.ScaleF(12.0f);
    const RailItem* hoveredItemPtr = nullptr;

    for (auto& item : m_items) {
        item.bounds = D2D1::RectF(startX, curY, startX + btnSize, curY + btnSize);

        bool isHovered = (m_hoveredTab == item.id);
        bool isActive  = (config.activeRailTab == static_cast<int>(item.id));

        if (isHovered) {
            hoveredItemPtr = &item;
        }

        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 6.0f, 6.0f);
        ID2D1Brush* iconBrush = theme.brushTextSecondary.Get();

        if (isActive) {
            // Dark grey rounded badge background (#2A2D2E, matching Image 2)
            pRT->FillRoundedRectangle(rrc, theme.brushHomeBadgeBg.Get());
            iconBrush = theme.brushTextWhite.Get();
        } else if (isHovered) {
            // Subtle hover background
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
            iconBrush = theme.brushTextWhite.Get();
        }

        // Render vector icon matching official VS Code / Lucide specifications
        switch (item.id) {
            case RailTabId::Files:
                m_lucide.DrawFiles(pRT, item.bounds, iconBrush, 1.6f);
                break;
            case RailTabId::Search:
                m_lucide.DrawSearch(pRT, item.bounds, iconBrush, 1.6f);
                break;
            case RailTabId::Git:
                m_lucide.DrawGitBranch(pRT, item.bounds, iconBrush, 1.6f);
                break;
            default:
                break;
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
