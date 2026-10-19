/**
 * @file TitleHeader.cpp
 * @brief Implementation of the Dark IDE frameless header with Lucide vector caption buttons.
 */

#include "phide/ui/TitleHeader.hpp"

namespace phide::ui {

static bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) {
    return (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom);
}

TitleHeader::TitleHeader() {
    // Header icon buttons with custom tooltips (Sidebar, File, Edit, View, Debug, Code, Tools, Window, Help)
    m_menus.push_back({ HeaderMenuId::Sidebar, L"Toggle Sidebar (Ctrl+B)", {} });
    m_menus.push_back({ HeaderMenuId::File,    L"File",                    {} });
    m_menus.push_back({ HeaderMenuId::Edit,    L"Edit",                    {} });
    m_menus.push_back({ HeaderMenuId::View,    L"View",                    {} });
    m_menus.push_back({ HeaderMenuId::Debug,   L"Debug",                   {} });
    m_menus.push_back({ HeaderMenuId::Code,    L"Code",                    {} });
    m_menus.push_back({ HeaderMenuId::Tools,   L"Tools",                   {} });
    m_menus.push_back({ HeaderMenuId::Window,  L"Window",                  {} });
    m_menus.push_back({ HeaderMenuId::Help,    L"Help",                    {} });
}

void TitleHeader::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         float clientWidth, bool isZoomed) {
    if (!pRT) return;

    // Initialize vector icon stroke styles if not yet created
    ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        m_icons.Initialize(pFactory.Get());
    }

    const float h = config.ScaleF(static_cast<float>(config.headerHeight));

    const float railW = config.ScaleF(static_cast<float>(config.railWidth));

    // Ensure horizontal purple-to-black glass gradient brush is initialized/updated
    if (!m_pHeaderGradientBrush || std::abs(m_lastWidth - clientWidth) >= 1.0f) {
        m_lastWidth = clientWidth;
        m_pHeaderGradientBrush.Reset();

        D2D1_GRADIENT_STOP stops[6];
        stops[0] = { 0.00f, D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f) }; // #3D1461 - Seamlessly matches rail's purple
        stops[1] = { 0.20f, D2D1::ColorF(0.18f, 0.06f, 0.30f, 1.0f) }; // #2E0F4D - Deep Royal Violet
        stops[2] = { 0.42f, D2D1::ColorF(0.12f, 0.04f, 0.21f, 1.0f) }; // #1F0B36 - Dark Twilight Purple
        stops[3] = { 0.65f, D2D1::ColorF(0.07f, 0.03f, 0.13f, 1.0f) }; // #120721 - Midnight Shadow
        stops[4] = { 0.85f, D2D1::ColorF(0.04f, 0.04f, 0.05f, 1.0f) }; // #0A0A0D - Transition to Black
        stops[5] = { 1.00f, D2D1::ColorF(0.03f, 0.03f, 0.04f, 1.0f) }; // #08080A - Pure Obsidian Black on right side

        ComPtr<ID2D1GradientStopCollection> pStopColl;
        if (SUCCEEDED(pRT->CreateGradientStopCollection(stops, 6, &pStopColl))) {
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(railW, 0.0f),
                D2D1::Point2F(clientWidth, 0.0f)
            );
            pRT->CreateLinearGradientBrush(props, pStopColl.Get(), &m_pHeaderGradientBrush);
        }
    }

    // 1. Draw Glass Purple-to-Black Header Background (starting at railW for seamless fusion with rail)
    D2D1_RECT_F rcHeader = D2D1::RectF(railW, 0.0f, clientWidth, h);
    if (m_pHeaderGradientBrush) {
        pRT->FillRectangle(rcHeader, m_pHeaderGradientBrush.Get());
    } else {
        pRT->FillRectangle(rcHeader, theme.brushHeaderBg.Get());
    }

    // Top subtle 1px glass specular reflection sheen
    pRT->DrawLine(
        D2D1::Point2F(0.0f, 0.5f),
        D2D1::Point2F(clientWidth, 0.5f),
        theme.brushBorderHover.Get(),
        0.8f
    );

    // 2. Render Header Icon Bar (Sidebar, File, Edit, View, Debug, Code, Tools, Window, Help)
    const float btnSize = config.ScaleF(28.0f);
    const float btnY    = (h - btnSize) / 2.0f;
    float curX          = railW + config.ScaleF(10.0f);
    const MenuItem* hoveredItemPtr = nullptr;

    for (auto& item : m_menus) {
        item.bounds = D2D1::RectF(curX, btnY, curX + btnSize, btnY + btnSize);

        bool isHovered = (m_hoveredMenu == item.id);
        if (isHovered) {
            hoveredItemPtr = &item;
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        }

        ID2D1Brush* brush = isHovered ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get();

        switch (item.id) {
            case HeaderMenuId::Sidebar:
                m_icons.DrawSidebar(pRT, item.bounds, brush, 1.4f);
                break;
            case HeaderMenuId::File:
                m_icons.DrawFile(pRT, item.bounds, brush, 1.4f);
                break;
            case HeaderMenuId::Edit:
                m_icons.DrawEdit(pRT, item.bounds, brush, 1.4f);
                break;
            case HeaderMenuId::View:
                m_icons.DrawView(pRT, item.bounds, brush, 1.4f);
                break;
            case HeaderMenuId::Debug:
                m_icons.DrawDebug(pRT, item.bounds, brush, 1.4f);
                break;
            case HeaderMenuId::Code:
                m_icons.DrawCode(pRT, item.bounds, brush, 1.4f);
                break;
            case HeaderMenuId::Tools:
                m_icons.DrawTools(pRT, item.bounds, brush, 1.4f);
                break;
            case HeaderMenuId::Window:
                m_icons.DrawWindow(pRT, item.bounds, brush, 1.4f);
                break;
            case HeaderMenuId::Help:
                m_icons.DrawHelp(pRT, item.bounds, brush, 1.4f);
                break;
            default:
                break;
        }

        curX += btnSize + config.ScaleF(4.0f);
    }

    // Floating Tooltip for Hovered Header Button
    if (hoveredItemPtr) {
        const float tipH    = config.ScaleF(24.0f);
        const float tipPadX = config.ScaleF(10.0f);

        size_t textLen = wcslen(hoveredItemPtr->label);
        float textW    = config.ScaleF(static_cast<float>(textLen * 7.0f + 14.0f));
        float tipW     = textW + tipPadX * 2.0f;

        float tipX     = hoveredItemPtr->bounds.left + (btnSize - tipW) / 2.0f;
        float tipY     = hoveredItemPtr->bounds.bottom + config.ScaleF(6.0f);

        if (tipX < 10.0f) tipX = 10.0f;
        if (tipX + tipW > clientWidth - 10.0f) tipX = clientWidth - tipW - 10.0f;

        D2D1_RECT_F rcTip = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
        D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 4.0f, 4.0f);

        pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg.Get());
        pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder.Get(), 1.0f);

        pRT->DrawText(
            hoveredItemPtr->label,
            static_cast<UINT32>(textLen),
            theme.fontHint.Get(),
            rcTip,
            theme.brushTextPrimary.Get()
        );
    }

    // 3. Render Vector Window Caption Controls (Minimize, Maximize, Close) in Top-Right
    const float btnW = config.ScaleF(static_cast<float>(config.captionBtnWidth)); // 46px
    float rightEdge = clientWidth;

    // --- Close Button (✕) ---
    m_rcClose = D2D1::RectF(rightEdge - btnW, 0.0f, rightEdge, h);
    if (m_hoveredCaption == CaptionButtonType::Close) {
        // Red background when hovered as requested!
        pRT->FillRectangle(m_rcClose, theme.brushCloseHoverBg.Get());
        m_icons.DrawClose(pRT, m_rcClose, theme.brushTextWhite.Get(), 1.25f);
    } else {
        m_icons.DrawClose(pRT, m_rcClose, theme.brushTextSecondary.Get(), 1.25f);
    }
    rightEdge -= btnW;

    // --- Maximize / Restore Button (□) ---
    m_rcMaximize = D2D1::RectF(rightEdge - btnW, 0.0f, rightEdge, h);
    if (m_hoveredCaption == CaptionButtonType::Maximize) {
        pRT->FillRectangle(m_rcMaximize, theme.brushCaptionHover.Get());
        m_icons.DrawMaximize(pRT, m_rcMaximize, theme.brushTextWhite.Get(), isZoomed, 1.25f);
    } else {
        m_icons.DrawMaximize(pRT, m_rcMaximize, theme.brushTextSecondary.Get(), isZoomed, 1.25f);
    }
    rightEdge -= btnW;

    // --- Minimize Button (—) ---
    m_rcMinimize = D2D1::RectF(rightEdge - btnW, 0.0f, rightEdge, h);
    if (m_hoveredCaption == CaptionButtonType::Minimize) {
        pRT->FillRectangle(m_rcMinimize, theme.brushCaptionHover.Get());
        m_icons.DrawMinimize(pRT, m_rcMinimize, theme.brushTextWhite.Get(), 1.25f);
    } else {
        m_icons.DrawMinimize(pRT, m_rcMinimize, theme.brushTextSecondary.Get(), 1.25f);
    }
}

HeaderMenuId TitleHeader::HitTestMenu(POINT pt) const {
    for (const auto& item : m_menus) {
        if (PtInRectF(pt, item.bounds)) {
            return item.id;
        }
    }
    return HeaderMenuId::None;
}

CaptionButtonType TitleHeader::HitTestCaptionButton(POINT pt) const {
    if (PtInRectF(pt, m_rcClose)) return CaptionButtonType::Close;
    if (PtInRectF(pt, m_rcMaximize)) return CaptionButtonType::Maximize;
    if (PtInRectF(pt, m_rcMinimize)) return CaptionButtonType::Minimize;
    return CaptionButtonType::None;
}

LRESULT TitleHeader::HitTestNCHitTest(POINT pt, const app::AppConfig& config) {
    const float h = config.ScaleF(static_cast<float>(config.headerHeight));
    if (pt.y >= h) return HTCLIENT;

    // If mouse is over any interactive element (menus or caption buttons), return HTCLIENT
    if (HitTestMenu(pt) != HeaderMenuId::None) return HTCLIENT;
    if (HitTestCaptionButton(pt) != CaptionButtonType::None) return HTCLIENT;

    // The remaining empty header area allows dragging the window
    return HTCAPTION;
}

bool TitleHeader::UpdateHoverState(HeaderMenuId hoveredMenu, CaptionButtonType hoveredCaption) {
    bool changed = false;
    if (m_hoveredMenu != hoveredMenu) {
        m_hoveredMenu = hoveredMenu;
        changed = true;
    }
    if (m_hoveredCaption != hoveredCaption) {
        m_hoveredCaption = hoveredCaption;
        changed = true;
    }
    return changed;
}

} // namespace phide::ui
