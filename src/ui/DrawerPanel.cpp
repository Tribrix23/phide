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

void DrawerPanel::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW) {
    if (!pRT || currentDrawerW <= 0.0f) return;

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float cornerRadius = 14.0f;

    // Check if cached geometry needs rebuilding
    if (!m_pDrawerBgGeometry ||
        m_cachedRailW != railW ||
        m_cachedHeaderH != headerH ||
        m_cachedDrawerW != currentDrawerW ||
        m_cachedClientH != clientHeight)
    {
        m_cachedRailW   = railW;
        m_cachedHeaderH = headerH;
        m_cachedDrawerW = currentDrawerW;
        m_cachedClientH = clientHeight;
        m_pDrawerBgGeometry.Reset();

        Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
        pRT->GetFactory(&pFactory);
        if (pFactory) {
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
            if (SUCCEEDED(pFactory->CreatePathGeometry(&pPath))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pPath->Open(&sink))) {
                    const float maxR = (std::min)(currentDrawerW, (clientHeight - headerH) / 2.0f);
                    const float r = (maxR > 0.0f) ? (std::min)(cornerRadius, maxR) : 0.0f;

                    // 1. Begin at the top edge right after the upper-left rounded corner
                    sink->BeginFigure(D2D1::Point2F(railW + r, headerH), D2D1_FIGURE_BEGIN_FILLED);

                    // 2. Top edge straight to top-right corner (SHARP)
                    sink->AddLine(D2D1::Point2F(railW + currentDrawerW, headerH));

                    // 3. Right edge straight down to bottom-right corner (SHARP)
                    sink->AddLine(D2D1::Point2F(railW + currentDrawerW, clientHeight));

                    // 4. Bottom edge straight to bottom-left corner (SHARP)
                    sink->AddLine(D2D1::Point2F(railW, clientHeight));

                    // 5. Left edge straight up to start of the upper-left curve
                    sink->AddLine(D2D1::Point2F(railW, headerH + r));

                    // 6. Arc around ONLY the upper-left corner connecting into purple header
                    if (r > 0.0f) {
                        D2D1_ARC_SEGMENT arc = {};
                        arc.point = D2D1::Point2F(railW + r, headerH);
                        arc.size = D2D1::SizeF(r, r);
                        arc.rotationAngle = 0.0f;
                        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                        arc.arcSize = D2D1_ARC_SIZE_SMALL;
                        sink->AddArc(arc);
                    }

                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();

                    m_pDrawerBgGeometry = pPath;
                }
            }
        }
    }

    // 1. Fill Drawer Background with upper-left curve
    if (m_pDrawerBgGeometry) {
        pRT->FillGeometry(m_pDrawerBgGeometry.Get(), theme.brushDrawerBg.Get());
    } else {
        D2D1_RECT_F rcDrawer = D2D1::RectF(railW, headerH, railW + currentDrawerW, clientHeight);
        pRT->FillRectangle(rcDrawer, theme.brushDrawerBg.Get());
    }

    // Right border divider
    pRT->DrawLine(
        D2D1::Point2F(railW + currentDrawerW - 0.5f, headerH),
        D2D1::Point2F(railW + currentDrawerW - 0.5f, clientHeight),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    // 2. Render Drawer Contents (Header & Tree Items)
    // Only render when drawer is wide enough, preventing any squishing during open/close
    const float fullDrawerW = config.ScaleF(static_cast<float>(config.drawerWidth));
    const float minContentW = config.ScaleF(40.0f);

    if (currentDrawerW >= minContentW) {
        // Push clip so content stays cleanly inside current drawer bounds during animation
        D2D1_RECT_F clipRect = D2D1::RectF(railW, headerH, railW + currentDrawerW, clientHeight);
        pRT->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        float curY = headerH + config.ScaleF(14.0f);
        const float paddingX = config.ScaleF(16.0f);

        const wchar_t* title = L"EXPLORER";
        if (config.activeRailTab == static_cast<int>(RailTabId::Search)) title = L"SEARCH WORKSPACE";
        else if (config.activeRailTab == static_cast<int>(RailTabId::Git)) title = L"SOURCE CONTROL (GIT)";

        // Full width fixed rectangle so text never squishes or breaks into vertical lines
        D2D1_RECT_F rcTitle = D2D1::RectF(
            railW + paddingX,
            curY,
            railW + fullDrawerW - paddingX,
            curY + config.ScaleF(20.0f)
        );
        pRT->DrawText(title, static_cast<UINT32>(wcslen(title)),
                      theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get(),
                      rcTitle, theme.brushTextPrimary.Get());

        curY += config.ScaleF(32.0f);

        // 3. Render Tree Items (for Explorer or perspective list)
        const float itemH = config.ScaleF(26.0f);
        for (size_t i = 0; i < m_mockFileTree.size(); ++i) {
            auto& item = m_mockFileTree[i];
            float indent = config.ScaleF(static_cast<float>(item.depth * 14));
            item.bounds = D2D1::RectF(railW + config.ScaleF(8.0f), curY, railW + fullDrawerW - config.ScaleF(8.0f), curY + itemH);

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

            // Draw Label (full width, never wraps!)
            D2D1_RECT_F rcLabel = D2D1::RectF(
                railW + config.ScaleF(36.0f) + indent,
                curY,
                railW + fullDrawerW - config.ScaleF(12.0f),
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

        pRT->PopAxisAlignedClip();
    }
}

int DrawerPanel::HitTest(POINT pt, const app::AppConfig& config) const {
    (void)config;
    for (size_t i = 0; i < m_mockFileTree.size(); ++i) {
        const auto& item = m_mockFileTree[i];
        if (pt.x >= item.bounds.left && pt.x < item.bounds.right &&
            pt.y >= item.bounds.top && pt.y < item.bounds.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool DrawerPanel::SetHoveredIndex(int index) {
    if (m_hoveredIndex != index) {
        m_hoveredIndex = index;
        return true;
    }
    return false;
}

} // namespace phide::ui
