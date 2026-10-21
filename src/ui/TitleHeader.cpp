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

    // Top-right execution widget buttons (Run, Debug, Terminal)
    m_execButtons.push_back({ HeaderExecId::Run,      L"Run (Ctrl+F5)",     {} });
    m_execButtons.push_back({ HeaderExecId::Debug,    L"Debug (F5)",        {} });
    m_execButtons.push_back({ HeaderExecId::Terminal, L"Terminal (Ctrl+`)", {} });
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

    // Ensure horizontal purple glass gradient brush is initialized/updated
    if (!m_pHeaderGradientBrush || std::abs(m_lastWidth - clientWidth) >= 1.0f || m_lastIsDark != theme.IsDarkMode()) {
        m_lastWidth = clientWidth;
        m_lastIsDark = theme.IsDarkMode();
        m_pHeaderGradientBrush.Reset();

        ComPtr<ID2D1GradientStopCollection> pStopColl;
        if (theme.IsDarkMode()) {
            D2D1_GRADIENT_STOP stops[6];
            stops[0] = { 0.00f, D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f) }; // #3D1461 - Seamlessly matches rail's purple
            stops[1] = { 0.20f, D2D1::ColorF(0.18f, 0.06f, 0.30f, 1.0f) }; // #2E0F4D - Deep Royal Violet
            stops[2] = { 0.42f, D2D1::ColorF(0.12f, 0.04f, 0.21f, 1.0f) }; // #1F0B36 - Dark Twilight Purple
            stops[3] = { 0.65f, D2D1::ColorF(0.07f, 0.03f, 0.13f, 1.0f) }; // #120721 - Midnight Shadow
            stops[4] = { 0.85f, D2D1::ColorF(0.04f, 0.04f, 0.05f, 1.0f) }; // #0A0A0D - Transition to Black
            stops[5] = { 1.00f, D2D1::ColorF(0.03f, 0.03f, 0.04f, 1.0f) }; // #08080A - Pure Obsidian Black on right side
            pRT->CreateGradientStopCollection(stops, 6, &pStopColl);
        } else {
            // Light Mode: Starts with vibrant Light Purple (#CFB3E8) on the left ("light purple here so it should be still purple"),
            // transitioning smoothly towards crisp pure white on the right ("should be whiter here not this dark")
            // where the caption buttons and solid black execution widget are located.
            D2D1_GRADIENT_STOP stops[4];
            stops[0] = { 0.00f, D2D1::ColorF(0.812f, 0.702f, 0.910f, 1.0f) }; // #CFB3E8 Light Purple base
            stops[1] = { 0.25f, D2D1::ColorF(0.863f, 0.784f, 0.937f, 1.0f) }; // #DCC8EF Soft Light Purple
            stops[2] = { 0.60f, D2D1::ColorF(0.941f, 0.902f, 0.973f, 1.0f) }; // #F0E6F8 Very soft Lavender
            stops[3] = { 1.00f, D2D1::ColorF(1.000f, 1.000f, 1.000f, 1.0f) }; // #FFFFFF Pure White on right
            pRT->CreateGradientStopCollection(stops, 4, &pStopColl);
        }

        if (pStopColl) {
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
        bool isActiveSidebar = (item.id == HeaderMenuId::Sidebar && config.isDrawerOpen);
        bool isActiveMenu = (m_activeMenu == item.id);

        if (isActiveSidebar || isActiveMenu) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 4.0f, 4.0f);
            if (isHovered) {
                pRT->FillRoundedRectangle(rrc, theme.brushCaptionHover.Get());
            } else {
                pRT->FillRoundedRectangle(rrc, theme.brushItemActive.Get());
            }
            // Crisp border highlight indicating active state
            pRT->DrawRoundedRectangle(rrc, theme.brushBorderHover.Get(), 1.0f);
        } else if (isHovered) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        }

        if (isHovered && m_activeMenu == HeaderMenuId::None) {
            hoveredItemPtr = &item;
        }

        // In Dark Mode: bright silver (#D1D1DB) or white on hover.
        // In Light Mode: crisp deep black (#141414) as explicitly requested.
        ID2D1Brush* brush = theme.IsDarkMode()
            ? ((isActiveSidebar || isActiveMenu || isHovered) ? theme.brushTextWhite.Get() : (theme.brushHeaderIcon ? theme.brushHeaderIcon.Get() : theme.brushTextSecondary.Get()))
            : (theme.brushHeaderIcon ? theme.brushHeaderIcon.Get() : theme.brushTextPrimary.Get());

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
            theme.fontTooltip ? theme.fontTooltip.Get() : theme.fontHint.Get(),
            rcTip,
            theme.brushTooltipText ? theme.brushTooltipText.Get() : theme.brushTextPrimary.Get()
        );
    }

    // 3. Render Top-Right Execution Control Widget (Run ▶, Debug 🐞, Terminal >_)
    // Positioned with a generous 100px buffer to the left of the window caption controls (Minimize, Maximize, Close)
    const float btnW          = config.ScaleF(static_cast<float>(config.captionBtnWidth)); // 46px
    const float captionTotalW = 3.0f * btnW;
    const float execGap       = config.ScaleF(4.0f);
    const float execBtnW      = config.ScaleF(28.0f);
    const float execBtnH      = config.ScaleF(26.0f);
    const float execY         = (h - execBtnH) / 2.0f;
    const float execRight     = clientWidth - captionTotalW - config.ScaleF(100.0f);
    const float execStartX    = execRight - (3.0f * execBtnW + 2.0f * execGap);

    // Grouped pill container backdrop for the execution controls:
    // In Dark Mode: subtle dark pill background.
    // In Light Mode: solid black container ("and the container of these button is not transparent but black on the light mode")
    D2D1_RECT_F rcExecContainer = D2D1::RectF(
        execStartX - config.ScaleF(4.0f),
        execY - config.ScaleF(2.0f),
        execRight + config.ScaleF(4.0f),
        execY + execBtnH + config.ScaleF(2.0f)
    );
    D2D1_ROUNDED_RECT rrcContainer = D2D1::RoundedRect(rcExecContainer, 5.0f, 5.0f);
    if (theme.IsDarkMode()) {
        pRT->FillRoundedRectangle(rrcContainer, theme.brushItemHover.Get());
        pRT->DrawRoundedRectangle(rrcContainer, theme.brushBorderSubtle.Get(), 0.8f);
    } else {
        // Light Mode: Solid Black container fill with dark border
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushExecBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f, 1.0f), &brushExecBg); // #141414 Black
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushExecBorder;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f), &brushExecBorder); // #333333 Subtle dark border
        if (brushExecBg) {
            pRT->FillRoundedRectangle(rrcContainer, brushExecBg.Get());
        }
        if (brushExecBorder) {
            pRT->DrawRoundedRectangle(rrcContainer, brushExecBorder.Get(), 0.8f);
        }
    }

    const ExecButton* hoveredExecPtr = nullptr;
    float curExecX = execStartX;

    for (auto& btn : m_execButtons) {
        btn.bounds = D2D1::RectF(curExecX, execY, curExecX + execBtnW, execY + execBtnH);
        bool isHovered = (m_hoveredExec == btn.id);
        if (isHovered) {
            hoveredExecPtr = &btn;
        }

        D2D1_ROUNDED_RECT rrcBtn = D2D1::RoundedRect(btn.bounds, 4.0f, 4.0f);

        switch (btn.id) {
            case HeaderExecId::Run: {
                if (isHovered) {
                    pRT->FillRoundedRectangle(rrcBtn, theme.brushRunGreenHover.Get());
                }
                // Vibrant green Run icon (▶)
                m_icons.DrawRun(pRT, btn.bounds, theme.brushRunGreen.Get(), 1.4f);
                break;
            }
            case HeaderExecId::Debug: {
                if (isHovered) {
                    pRT->FillRoundedRectangle(rrcBtn, theme.brushRunGreenHover.Get());
                }
                // Vibrant green Debug icon (beetle 🐞)
                m_icons.DrawBug(pRT, btn.bounds, theme.brushRunGreen.Get(), 1.4f);
                break;
            }
            case HeaderExecId::Terminal: {
                // Inside the solid black container in both Dark and Light modes, the terminal icon
                // must be rendered with white/silver for high contrast and legibility.
                if (isHovered) {
                    if (theme.IsDarkMode()) {
                        pRT->FillRoundedRectangle(rrcBtn, theme.brushItemHover.Get());
                    } else {
                        // Subtle dark hover highlight within the black container in Light Mode
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHoverDark;
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.22f, 0.25f, 1.0f), &brushHoverDark);
                        if (brushHoverDark) {
                            pRT->FillRoundedRectangle(rrcBtn, brushHoverDark.Get());
                        }
                    }
                }
                ID2D1Brush* termBrush = theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushHeaderIcon.Get();
                m_icons.DrawTerminal(pRT, btn.bounds, termBrush, 1.4f);
                break;
            }
            default:
                break;
        }

        curExecX += execBtnW + execGap;
    }

    // Floating Tooltip for Hovered Execution Button
    if (hoveredExecPtr) {
        const float tipH    = config.ScaleF(24.0f);
        const float tipPadX = config.ScaleF(10.0f);

        size_t textLen = wcslen(hoveredExecPtr->label);
        float textW    = config.ScaleF(static_cast<float>(textLen * 7.0f + 14.0f));
        float tipW     = textW + tipPadX * 2.0f;

        float tipX     = hoveredExecPtr->bounds.left + (execBtnW - tipW) / 2.0f;
        float tipY     = hoveredExecPtr->bounds.bottom + config.ScaleF(6.0f);

        if (tipX < 10.0f) tipX = 10.0f;
        if (tipX + tipW > clientWidth - 10.0f) tipX = clientWidth - tipW - 10.0f;

        D2D1_RECT_F rcTip = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
        D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 4.0f, 4.0f);

        pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg.Get());
        pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder.Get(), 1.0f);

        pRT->DrawText(
            hoveredExecPtr->label,
            static_cast<UINT32>(textLen),
            theme.fontTooltip ? theme.fontTooltip.Get() : theme.fontHint.Get(),
            rcTip,
            theme.brushTooltipText ? theme.brushTooltipText.Get() : theme.brushTextPrimary.Get()
        );
    }

    // 4. Render Split Editor Button in Top-Right (Positioned between Run controls and Caption buttons)
    // As requested: In Dark Mode icon is white; in Light Mode icon is black!
    const float splitW = config.ScaleF(28.0f);
    const float splitH = config.ScaleF(26.0f);
    const float splitX = rcExecContainer.right + config.ScaleF(14.0f);
    const float splitY = (h - splitH) / 2.0f;
    m_rcSplitEditor = D2D1::RectF(splitX, splitY, splitX + splitW, splitY + splitH);

    D2D1_ROUNDED_RECT rrcSplit = D2D1::RoundedRect(m_rcSplitEditor, 4.0f, 4.0f);
    if (m_isSplitActive) {
        pRT->FillRoundedRectangle(rrcSplit, theme.brushItemActive.Get());
        pRT->DrawRoundedRectangle(rrcSplit, theme.brushBrandAccent.Get(), 1.0f);
    } else if (m_hoveredSplitEditor) {
        pRT->FillRoundedRectangle(rrcSplit, theme.brushItemHover.Get());
        pRT->DrawRoundedRectangle(rrcSplit, theme.brushBorderSubtle.Get(), 0.8f);
    }

    ID2D1Brush* pSplitBrush = theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
    m_icons.DrawSplitEditor(pRT, m_rcSplitEditor, pSplitBrush, 1.3f);

    // Floating Tooltip for Hovered Split Editor Button
    if (m_hoveredSplitEditor) {
        const float tipH    = config.ScaleF(24.0f);
        const float tipPadX = config.ScaleF(10.0f);
        const wchar_t* tipText = L"Split Editor Right (Ctrl+\\)";
        size_t textLen = wcslen(tipText);
        float textW    = config.ScaleF(static_cast<float>(textLen * 6.5f + 14.0f));
        float tipW     = textW + tipPadX * 2.0f;

        float tipX     = m_rcSplitEditor.left + (splitW - tipW) / 2.0f;
        float tipY     = m_rcSplitEditor.bottom + config.ScaleF(6.0f);

        if (tipX < 10.0f) tipX = 10.0f;
        if (tipX + tipW > clientWidth - 10.0f) tipX = clientWidth - tipW - 10.0f;

        D2D1_RECT_F rcTip = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
        D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 4.0f, 4.0f);

        pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg.Get());
        pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder.Get(), 1.0f);

        pRT->DrawText(
            tipText,
            static_cast<UINT32>(textLen),
            theme.fontTooltip ? theme.fontTooltip.Get() : theme.fontHint.Get(),
            rcTip,
            theme.brushTooltipText ? theme.brushTooltipText.Get() : theme.brushTextPrimary.Get()
        );
    }

    // 5. Render Vector Window Caption Controls (Minimize, Maximize, Close) in Top-Right
    float rightEdge = clientWidth;
    ID2D1Brush* defaultCaptionBrush = theme.brushHeaderIcon ? theme.brushHeaderIcon.Get() : theme.brushTextSecondary.Get();
    ID2D1Brush* captionHoverGlyphBrush = theme.IsDarkMode() ? theme.brushTextWhite.Get() : (theme.brushHeaderIcon ? theme.brushHeaderIcon.Get() : theme.brushTextPrimary.Get());

    // --- Close Button (✕) ---
    m_rcClose = D2D1::RectF(rightEdge - btnW, 0.0f, rightEdge, h);
    if (m_hoveredCaption == CaptionButtonType::Close) {
        // Red background when hovered as requested!
        pRT->FillRectangle(m_rcClose, theme.brushCloseHoverBg.Get());
        m_icons.DrawClose(pRT, m_rcClose, theme.brushTextWhite.Get(), 1.25f);
    } else {
        m_icons.DrawClose(pRT, m_rcClose, defaultCaptionBrush, 1.25f);
    }
    rightEdge -= btnW;

    // --- Maximize / Restore Button (□) ---
    m_rcMaximize = D2D1::RectF(rightEdge - btnW, 0.0f, rightEdge, h);
    if (m_hoveredCaption == CaptionButtonType::Maximize) {
        pRT->FillRectangle(m_rcMaximize, theme.brushCaptionHover.Get());
        m_icons.DrawMaximize(pRT, m_rcMaximize, captionHoverGlyphBrush, isZoomed, 1.25f);
    } else {
        m_icons.DrawMaximize(pRT, m_rcMaximize, defaultCaptionBrush, isZoomed, 1.25f);
    }
    rightEdge -= btnW;

    // --- Minimize Button (—) ---
    m_rcMinimize = D2D1::RectF(rightEdge - btnW, 0.0f, rightEdge, h);
    if (m_hoveredCaption == CaptionButtonType::Minimize) {
        pRT->FillRectangle(m_rcMinimize, theme.brushCaptionHover.Get());
        m_icons.DrawMinimize(pRT, m_rcMinimize, captionHoverGlyphBrush, 1.25f);
    } else {
        m_icons.DrawMinimize(pRT, m_rcMinimize, defaultCaptionBrush, 1.25f);
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

D2D1_RECT_F TitleHeader::GetMenuItemBounds(HeaderMenuId id) const {
    for (const auto& item : m_menus) {
        if (item.id == id) {
            return item.bounds;
        }
    }
    return D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
}

HeaderExecId TitleHeader::HitTestExecButton(POINT pt) const {
    for (const auto& item : m_execButtons) {
        if (PtInRectF(pt, item.bounds)) {
            return item.id;
        }
    }
    return HeaderExecId::None;
}

CaptionButtonType TitleHeader::HitTestCaptionButton(POINT pt) const {
    if (PtInRectF(pt, m_rcClose)) return CaptionButtonType::Close;
    if (PtInRectF(pt, m_rcMaximize)) return CaptionButtonType::Maximize;
    if (PtInRectF(pt, m_rcMinimize)) return CaptionButtonType::Minimize;
    return CaptionButtonType::None;
}

bool TitleHeader::HitTestSplitEditor(POINT pt) const noexcept {
    return (pt.x >= m_rcSplitEditor.left && pt.x <= m_rcSplitEditor.right &&
            pt.y >= m_rcSplitEditor.top && pt.y <= m_rcSplitEditor.bottom);
}

bool TitleHeader::SetHoveredSplitEditor(bool hovered) noexcept {
    if (m_hoveredSplitEditor != hovered) {
        m_hoveredSplitEditor = hovered;
        return true;
    }
    return false;
}

LRESULT TitleHeader::HitTestNCHitTest(POINT pt, const app::AppConfig& config) {
    const float h = config.ScaleF(static_cast<float>(config.headerHeight));
    if (pt.y >= h) return HTCLIENT;

    // If mouse is over any interactive element (menus, exec buttons, split editor, or caption buttons), return HTCLIENT
    if (HitTestMenu(pt) != HeaderMenuId::None) return HTCLIENT;
    if (HitTestExecButton(pt) != HeaderExecId::None) return HTCLIENT;
    if (HitTestSplitEditor(pt)) return HTCLIENT;
    if (HitTestCaptionButton(pt) != CaptionButtonType::None) return HTCLIENT;

    // The remaining empty header area allows dragging the window
    return HTCAPTION;
}

bool TitleHeader::UpdateHoverState(HeaderMenuId hoveredMenu, CaptionButtonType hoveredCaption, HeaderExecId hoveredExec) {
    bool changed = false;
    if (m_hoveredMenu != hoveredMenu) {
        m_hoveredMenu = hoveredMenu;
        changed = true;
    }
    if (m_hoveredCaption != hoveredCaption) {
        m_hoveredCaption = hoveredCaption;
        changed = true;
    }
    if (m_hoveredExec != hoveredExec) {
        m_hoveredExec = hoveredExec;
        changed = true;
    }
    return changed;
}

} // namespace phide::ui
