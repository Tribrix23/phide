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
    m_menus.push_back({ HeaderMenuId::File,    L"File",    {} });
    m_menus.push_back({ HeaderMenuId::Edit,    L"Edit",    {} });
    m_menus.push_back({ HeaderMenuId::View,    L"View",    {} });
    m_menus.push_back({ HeaderMenuId::Project, L"Project", {} });
    m_menus.push_back({ HeaderMenuId::Help,    L"Help",    {} });
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

    // 1. Draw Header Background (#181818)
    D2D1_RECT_F rcHeader = D2D1::RectF(0.0f, 0.0f, clientWidth, h);
    pRT->FillRectangle(rcHeader, theme.brushHeaderBg.Get());

    // NOTE: The horizontal dividing line under the header has been REMOVED as requested (marked with X).
    // The header now blends seamlessly into the workspace.

    // 2. Render Left Menu Bar (File, Edit, View, Project, Help)
    float curX = config.ScaleF(12.0f);
    const float menuH = config.ScaleF(24.0f);
    const float menuY = (h - menuH) / 2.0f;

    for (auto& item : m_menus) {
        float textWidth = config.ScaleF(static_cast<float>(wcslen(item.label) * 9 + 16));
        item.bounds = D2D1::RectF(curX, menuY, curX + textWidth, menuY + menuH);

        if (m_hoveredMenu == item.id) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        }

        ID2D1SolidColorBrush* brush = (m_hoveredMenu == item.id)
            ? theme.brushTextPrimary.Get()
            : theme.brushTextSecondary.Get();

        pRT->DrawText(
            item.label,
            static_cast<UINT32>(wcslen(item.label)),
            theme.fontMenu.Get(),
            item.bounds,
            brush
        );

        curX += textWidth + config.ScaleF(2.0f);
    }

    // 3. Render Centered Application & Project Title
    const wchar_t* titleStr = L"phide \u2014 C++ & PHP IDE";
    const float titleW = config.ScaleF(260.0f);
    const float titleX = (clientWidth - titleW) / 2.0f;
    D2D1_RECT_F rcTitle = D2D1::RectF(titleX, 0.0f, titleX + titleW, h);

    pRT->DrawText(
        titleStr,
        static_cast<UINT32>(wcslen(titleStr)),
        theme.fontTitle.Get(),
        rcTitle,
        theme.brushTextSecondary.Get()
    );

    // 4. Render Vector Window Caption Controls (Minimize, Maximize, Close) in Top-Right
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
