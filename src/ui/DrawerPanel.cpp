/**
 * @file DrawerPanel.cpp
 * @brief Implementation of the slide-out sidebar drawer view.
 */

#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/ActivityRail.hpp"

namespace phide::ui {

DrawerPanel::DrawerPanel() {
    // Populate mock project tree representing phide & PHP IDE structure
    m_mockFileTree.push_back({ L"phide",        L"\u25BE \u25A4", 0, true,  true,  {} });
    m_mockFileTree.push_back({ L"src",          L"\u25BE \u25A4", 1, true,  true,  {} });
    m_mockFileTree.push_back({ L"main.cpp",     L"\u2699",        2, false, false, {} });
    m_mockFileTree.push_back({ L"index.php",    L"\u272A",        2, false, false, {} });
    m_mockFileTree.push_back({ L"Kernel.php",   L"\u272A",        2, false, false, {} });
    m_mockFileTree.push_back({ L"include",      L"\u25B8 \u25A4", 1, true,  false, {} });
    m_mockFileTree.push_back({ L"composer.json",L"\u2261",        1, false, false, {} });
    m_mockFileTree.push_back({ L"CMakeLists.txt",L"\u2261",       1, false, false, {} });
}

void DrawerPanel::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight) {
    if (!pRT || !config.isDrawerOpen) return;

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float drawerW = config.ScaleF(static_cast<float>(config.drawerWidth));

    // 1. Draw Drawer Background
    D2D1_RECT_F rcDrawer = D2D1::RectF(railW, headerH, railW + drawerW, clientHeight);
    pRT->FillRectangle(rcDrawer, theme.brushDrawerBg.Get());

    // Right border divider
    pRT->DrawLine(
        D2D1::Point2F(railW + drawerW - 0.5f, headerH),
        D2D1::Point2F(railW + drawerW - 0.5f, clientHeight),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    // 2. Draw Drawer Header
    float curY = headerH + config.ScaleF(14.0f);
    const float paddingX = config.ScaleF(16.0f);

    const wchar_t* title = L"EXPLORER";
    if (config.activeRailTab == static_cast<int>(RailTabId::Search)) title = L"SEARCH WORKSPACE";
    else if (config.activeRailTab == static_cast<int>(RailTabId::Git)) title = L"SOURCE CONTROL (GIT)";

    D2D1_RECT_F rcTitle = D2D1::RectF(
        railW + paddingX,
        curY,
        railW + drawerW - config.ScaleF(36.0f),
        curY + config.ScaleF(20.0f)
    );
    pRT->DrawText(title, static_cast<UINT32>(wcslen(title)), theme.fontTitle.Get(), rcTitle, theme.brushTextPrimary.Get());

    // Close button (X) at top right of drawer
    m_rcCloseBtn = D2D1::RectF(
        railW + drawerW - config.ScaleF(30.0f),
        curY,
        railW + drawerW - config.ScaleF(10.0f),
        curY + config.ScaleF(20.0f)
    );
    pRT->DrawText(L"\u2715", 1, theme.fontIcon.Get(), m_rcCloseBtn, theme.brushTextMuted.Get());

    curY += config.ScaleF(32.0f);

    // 3. Render Tree Items (for Explorer or perspective list)
    const float itemH = config.ScaleF(26.0f);
    for (size_t i = 0; i < m_mockFileTree.size(); ++i) {
        auto& item = m_mockFileTree[i];
        float indent = config.ScaleF(static_cast<float>(item.depth * 14));
        item.bounds = D2D1::RectF(railW + config.ScaleF(8.0f), curY, railW + drawerW - config.ScaleF(8.0f), curY + itemH);

        if (m_hoveredIndex == static_cast<int>(i)) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        }

        // Draw Icon
        D2D1_RECT_F rcIcon = D2D1::RectF(
            railW + config.ScaleF(12.0f) + indent,
            curY,
            railW + config.ScaleF(32.0f) + indent,
            curY + itemH
        );
        pRT->DrawText(
            item.iconGlyph.c_str(),
            static_cast<UINT32>(item.iconGlyph.length()),
            theme.fontMenu.Get(),
            rcIcon,
            theme.brushTextSecondary.Get()
        );

        // Draw Label
        D2D1_RECT_F rcLabel = D2D1::RectF(
            railW + config.ScaleF(36.0f) + indent,
            curY,
            railW + drawerW - config.ScaleF(12.0f),
            curY + itemH
        );
        pRT->DrawText(
            item.label.c_str(),
            static_cast<UINT32>(item.label.length()),
            theme.fontBody.Get(),
            rcLabel,
            theme.brushTextPrimary.Get()
        );

        curY += itemH;
    }
}

int DrawerPanel::HitTest(POINT pt, const app::AppConfig& config) const {
    if (!config.isDrawerOpen) return -1;
    for (size_t i = 0; i < m_mockFileTree.size(); ++i) {
        const auto& item = m_mockFileTree[i];
        if (pt.x >= item.bounds.left && pt.x < item.bounds.right &&
            pt.y >= item.bounds.top && pt.y < item.bounds.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool DrawerPanel::HitTestCloseButton(POINT pt) const {
    return (pt.x >= m_rcCloseBtn.left && pt.x < m_rcCloseBtn.right &&
            pt.y >= m_rcCloseBtn.top && pt.y < m_rcCloseBtn.bottom);
}

bool DrawerPanel::SetHoveredIndex(int index) {
    if (m_hoveredIndex != index) {
        m_hoveredIndex = index;
        return true;
    }
    return false;
}

} // namespace phide::ui
