/**
 * @file TitleHeader.cpp
 * @brief Implementation of the Dark IDE frameless header.
 */

#include "phide/ui/TitleHeader.hpp"

namespace phide::ui {

TitleHeader::TitleHeader() {
    m_menus.push_back({ HeaderMenuId::File,    L"File",    {} });
    m_menus.push_back({ HeaderMenuId::Edit,    L"Edit",    {} });
    m_menus.push_back({ HeaderMenuId::View,    L"View",    {} });
    m_menus.push_back({ HeaderMenuId::Project, L"Project", {} });
    m_menus.push_back({ HeaderMenuId::Help,    L"Help",    {} });
}

void TitleHeader::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientWidth) {
    if (!pRT) return;

    const float h = config.ScaleF(static_cast<float>(config.headerHeight));

    // 1. Draw Header Background
    D2D1_RECT_F rcHeader = D2D1::RectF(0.0f, 0.0f, clientWidth, h);
    pRT->FillRectangle(rcHeader, theme.brushHeaderBg.Get());

    // Bottom subtle border divider
    pRT->DrawLine(
        D2D1::Point2F(0.0f, h - 0.5f),
        D2D1::Point2F(clientWidth, h - 0.5f),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

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

    // Note: The rightmost ~138px is reserved exclusively for native Windows 11 DWM caption buttons.
    // Custom duplicate buttons are completely omitted to prevent the overlapping button glitch.
}

HeaderMenuId TitleHeader::HitTest(POINT pt) const {
    for (const auto& item : m_menus) {
        if (pt.x >= item.bounds.left && pt.x < item.bounds.right &&
            pt.y >= item.bounds.top && pt.y < item.bounds.bottom) {
            return item.id;
        }
    }
    return HeaderMenuId::None;
}

LRESULT TitleHeader::HitTestNCHitTest(POINT pt, const app::AppConfig& config, float clientWidth) {
    const float h = config.ScaleF(static_cast<float>(config.headerHeight));
    if (pt.y >= h) return HTCLIENT;

    // Check if cursor is over the native Windows 11 caption button area (top right ~138px)
    const float captionAreaW = config.ScaleF(138.0f);
    if (static_cast<float>(pt.x) >= clientWidth - captionAreaW) {
        // Return -1 to signal WindowProc to delegate directly to DefWindowProc / DWM!
        return -1;
    }

    // If over any clickable menu item, return HTCLIENT so clicks are dispatched to our app
    if (HitTest(pt) != HeaderMenuId::None) {
        return HTCLIENT;
    }

    // The remaining empty title bar space allows dragging the window
    return HTCAPTION;
}

bool TitleHeader::SetHoveredMenu(HeaderMenuId hoveredId) {
    if (m_hoveredMenu != hoveredId) {
        m_hoveredMenu = hoveredId;
        return true;
    }
    return false;
}

} // namespace phide::ui
