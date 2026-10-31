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
    m_items.push_back({ RailTabId::Git,      L"Source Control (Ctrl+Shift+G)", {} });

    // 4. File network icon directly under horizontal separation line (User prompt: tooltip "File network")
    m_items.push_back({ RailTabId::FileNetwork, L"File network",               {} });

    // 5. Settings icon directly under File network (matching user's green circle)
    m_items.push_back({ RailTabId::Settings,    L"Settings (Ctrl+,)",          {} });

    // 6. Notebook icon (directly above Packages)
    m_items.push_back({ RailTabId::Notebook, L"Interactive Notebook (Ctrl+Shift+J)", {} });

    // 7. Packages icon (bottom of Activity Rail, matching user's request)
    m_items.push_back({ RailTabId::Packages, L"Packages",                      {} });
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
    if (!m_pRailGradientBrush || std::abs(m_lastRailH - clientHeight) >= 1.0f || m_lastThemeMode != theme.GetThemeMode()) {
        m_lastRailH = clientHeight;
        m_lastIsDark = theme.IsDarkMode();
        m_lastThemeMode = theme.GetThemeMode();
        m_pRailGradientBrush.Reset();

        ComPtr<ID2D1GradientStopCollection> pStopColl;
        if (theme.IsDarkMode()) {
            D2D1_COLOR_F topCol = theme.colors.railBg;
            D2D1_GRADIENT_STOP stops[3];
            stops[0] = { 0.00f, topCol };
            stops[1] = { 0.50f, D2D1::ColorF(topCol.r * 0.75f, topCol.g * 0.75f, topCol.b * 0.75f, 1.0f) };
            stops[2] = { 1.00f, D2D1::ColorF(topCol.r * 0.45f, topCol.g * 0.45f, topCol.b * 0.45f, 1.0f) };
            pRT->CreateGradientStopCollection(stops, 3, &pStopColl);
        } else {
            D2D1_COLOR_F topCol = theme.colors.railBg;
            D2D1_GRADIENT_STOP stops[3];
            stops[0] = { 0.00f, topCol };
            stops[1] = { 0.45f, D2D1::ColorF((std::min)(1.0f, topCol.r * 1.05f), (std::min)(1.0f, topCol.g * 1.05f), (std::min)(1.0f, topCol.b * 1.05f), 1.0f) };
            stops[2] = { 1.00f, D2D1::ColorF((std::min)(1.0f, topCol.r * 1.10f), (std::min)(1.0f, topCol.g * 1.10f), (std::min)(1.0f, topCol.b * 1.10f), 1.0f) };
            pRT->CreateGradientStopCollection(stops, 3, &pStopColl);
        }

        if (pStopColl) {
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
        if (item.id == RailTabId::Packages) {
            // Anchor Packages at bottom of Activity Rail (matching user's green arrow in Image 1)
            float botY = clientHeight - btnSize - config.ScaleF(14.0f);
            item.bounds = D2D1::RectF(startX, botY, startX + btnSize, botY + btnSize);
        } else if (item.id == RailTabId::Notebook) {
            // Anchor Notebook directly above Packages (matching user's red circle)
            float botY = clientHeight - (btnSize * 2.0f) - config.ScaleF(22.0f);
            item.bounds = D2D1::RectF(startX, botY, startX + btnSize, botY + btnSize);
        } else if (item.id == RailTabId::MoreActions) {
            // Separation line on top of the 3 dot icon (white color per user request)
            curY += config.ScaleF(2.0f);
            float sepY = curY;
            ID2D1Brush* pSepBrush = theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushBorderSubtle.Get();
            pRT->DrawLine(
                D2D1::Point2F(startX + config.ScaleF(4.0f), sepY),
                D2D1::Point2F(startX + btnSize - config.ScaleF(4.0f), sepY),
                pSepBrush,
                1.0f
            );
            curY += config.ScaleF(7.0f);
            item.bounds = D2D1::RectF(startX, curY, startX + btnSize, curY + btnSize);
            curY += btnSize + spacing;
        } else if (item.id == RailTabId::Student) {
            // Anchor Student Hat directly above Notebook (matching user's green circle)
            float botY = clientHeight - (btnSize * 3.0f) - config.ScaleF(30.0f);
            item.bounds = D2D1::RectF(startX, botY, startX + btnSize, botY + btnSize);
        } else {
            item.bounds = D2D1::RectF(startX, curY, startX + btnSize, curY + btnSize);
            curY += btnSize + spacing;
        }

        bool isHovered = (m_hoveredTab == item.id);
        bool isActive  = (config.activeRailTab == static_cast<int>(item.id));

        if (isHovered) {
            hoveredItemPtr = &item;
        }

        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 6.0f, 6.0f);
        // In Dark Mode: bright silver (#D1D1DB) or white on hover/active.
        // In Light Mode: deep black (#141414) as explicitly requested.
        ID2D1Brush* iconBrush = theme.IsDarkMode()
            ? (theme.brushHeaderIcon ? theme.brushHeaderIcon.Get() : theme.brushTextSecondary.Get())
            : (theme.brushHeaderIcon ? theme.brushHeaderIcon.Get() : theme.brushTextPrimary.Get());

        if (isActive) {
            pRT->FillRoundedRectangle(rrc, theme.brushHomeBadgeBg.Get());
            iconBrush = theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
        } else if (isHovered) {
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
            iconBrush = theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
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
                if (m_isGitLoading) {
                    // Render blue circular badge with rotating sync/loading arrows on bottom-right (Image 4)
                    const float badgeCenterX = item.bounds.right - config.ScaleF(7.0f);
                    const float badgeCenterY = item.bounds.bottom - config.ScaleF(7.0f);
                    const float badgeR = config.ScaleF(6.5f);
                    D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(
                        D2D1::RectF(badgeCenterX - badgeR, badgeCenterY - badgeR, badgeCenterX + badgeR, badgeCenterY + badgeR),
                        3.5f, 3.5f
                    );
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadge;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f), &brushBadge);
                    pRT->FillRoundedRectangle(rrcBadge, brushBadge.Get());

                    // Draw rotating circular sync arrows in white
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushWhite;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brushWhite);

                    D2D1_MATRIX_3X2_F oldTrans;
                    pRT->GetTransform(&oldTrans);
                    pRT->SetTransform(D2D1::Matrix3x2F::Rotation(m_gitAnimAngle, D2D1::Point2F(badgeCenterX, badgeCenterY)) * oldTrans);

                    const float r = badgeR * 0.58f;
                    Microsoft::WRL::ComPtr<ID2D1Factory> pD2DFactory;
                    pRT->GetFactory(&pD2DFactory);

                    // Top curved arc with arrowhead (Image 1)
                    Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPathTop;
                    if (pD2DFactory && SUCCEEDED(pD2DFactory->CreatePathGeometry(&pPathTop))) {
                        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                        if (SUCCEEDED(pPathTop->Open(&sink))) {
                            sink->BeginFigure(D2D1::Point2F(badgeCenterX - r, badgeCenterY + 0.5f), D2D1_FIGURE_BEGIN_HOLLOW);
                            D2D1_ARC_SEGMENT arc{};
                            arc.point = D2D1::Point2F(badgeCenterX + r * 0.88f, badgeCenterY - r * 0.35f);
                            arc.size = D2D1::SizeF(r, r);
                            arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                            sink->AddArc(arc);
                            sink->EndFigure(D2D1_FIGURE_END_OPEN);
                            sink->Close();
                            pRT->DrawGeometry(pPathTop.Get(), brushWhite.Get(), 1.4f);
                        }
                    }
                    // Top arrowhead
                    float tx = badgeCenterX + r * 0.88f;
                    float ty = badgeCenterY - r * 0.35f;
                    pRT->DrawLine(D2D1::Point2F(tx, ty), D2D1::Point2F(tx - 2.2f, ty - 1.8f), brushWhite.Get(), 1.4f);
                    pRT->DrawLine(D2D1::Point2F(tx, ty), D2D1::Point2F(tx - 2.3f, ty + 0.8f), brushWhite.Get(), 1.4f);

                    // Bottom curved arc with arrowhead (Image 1)
                    Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPathBottom;
                    if (pD2DFactory && SUCCEEDED(pD2DFactory->CreatePathGeometry(&pPathBottom))) {
                        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                        if (SUCCEEDED(pPathBottom->Open(&sink))) {
                            sink->BeginFigure(D2D1::Point2F(badgeCenterX + r, badgeCenterY - 0.5f), D2D1_FIGURE_BEGIN_HOLLOW);
                            D2D1_ARC_SEGMENT arc{};
                            arc.point = D2D1::Point2F(badgeCenterX - r * 0.88f, badgeCenterY + r * 0.35f);
                            arc.size = D2D1::SizeF(r, r);
                            arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                            sink->AddArc(arc);
                            sink->EndFigure(D2D1_FIGURE_END_OPEN);
                            sink->Close();
                            pRT->DrawGeometry(pPathBottom.Get(), brushWhite.Get(), 1.4f);
                        }
                    }
                    // Bottom arrowhead
                    float bx = badgeCenterX - r * 0.88f;
                    float by = badgeCenterY + r * 0.35f;
                    pRT->DrawLine(D2D1::Point2F(bx, by), D2D1::Point2F(bx + 2.2f, by + 1.8f), brushWhite.Get(), 1.4f);
                    pRT->DrawLine(D2D1::Point2F(bx, by), D2D1::Point2F(bx + 2.3f, by - 0.8f), brushWhite.Get(), 1.4f);

                    pRT->SetTransform(oldTrans);
                } else if (m_gitBadgeCount > 0) {
                    // Render blue circular / pill badge with the number of changes (Images 2 & 5, capped at 10K if >= 10,000)
                    const float badgeCenterX = item.bounds.right - config.ScaleF(5.0f);
                    const float badgeCenterY = item.bounds.bottom - config.ScaleF(5.0f);
                    std::wstring countStr = (m_gitBadgeCount >= 10000) ? L"10K" : std::to_wstring(m_gitBadgeCount);
                    const float badgeW = config.ScaleF(m_gitBadgeCount >= 10000 ? 24.0f : (m_gitBadgeCount >= 10 ? 18.0f : 14.0f));
                    const float badgeH = config.ScaleF(14.0f);
                    D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(
                        D2D1::RectF(badgeCenterX - badgeW / 2.0f, badgeCenterY - badgeH / 2.0f, badgeCenterX + badgeW / 2.0f, badgeCenterY + badgeH / 2.0f),
                        badgeH / 2.0f, badgeH / 2.0f
                    );
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadge;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f), &brushBadge);
                    pRT->FillRoundedRectangle(rrcBadge, brushBadge.Get());

                    pRT->DrawText(countStr.c_str(), static_cast<UINT32>(countStr.length()),
                                  theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuShortcut.Get(),
                                  rrcBadge.rect, theme.brushTextWhite.Get());
                }
                break;
            case RailTabId::Student:
                m_lucide.DrawGraduationCap(pRT, item.bounds, iconBrush, 1.6f);
                break;
            case RailTabId::Notebook:
                m_lucide.DrawTerminalNotebook(pRT, item.bounds, iconBrush, 1.6f);
                break;
            case RailTabId::Packages:
                m_lucide.DrawPackage(pRT, item.bounds, iconBrush, 1.6f);
                break;
            case RailTabId::FileNetwork:
                m_lucide.DrawFileNetwork(pRT, item.bounds, iconBrush, 1.7f);
                break;
            case RailTabId::Settings:
                m_lucide.DrawSettings(pRT, item.bounds, iconBrush, 1.6f);
                break;
            default:
                break;
        }
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
        theme.brushTooltipText ? theme.brushTooltipText.Get() : theme.brushTextPrimary.Get()
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

D2D1_RECT_F ActivityRail::GetItemBounds(RailTabId id) const noexcept {
    for (const auto& item : m_items) {
        if (item.id == id) return item.bounds;
    }
    return D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
}

} // namespace phide::ui
