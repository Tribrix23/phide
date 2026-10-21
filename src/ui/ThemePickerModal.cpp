/**
 * @file ThemePickerModal.cpp
 * @brief Implementation of Theme Picker modal dialog with connected split preview.
 */

#include "phide/ui/ThemePickerModal.hpp"
#include <algorithm>

namespace phide::ui {

ThemePickerModal::ThemePickerModal() = default;

void ThemePickerModal::DrawDarkPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, const D2D1_RECT_F& rc) {
    // 1. Dark IDE Canvas (#1E1E1E)
    pRT->FillRectangle(rc, theme.brushCanvasBg.Get());

    const float headerH = config.ScaleF(13.0f);
    const float railW   = config.ScaleF(12.0f);
    const float drawerW = config.ScaleF(36.0f);
    const float tabH    = config.ScaleF(10.0f);

    // 2. Header Bar (#3D1461)
    D2D1_RECT_F rcHeader = D2D1::RectF(rc.left, rc.top, rc.right, rc.top + headerH);
    pRT->FillRectangle(rcHeader, theme.brushHeaderBg.Get());

    // Miniature Logo on left
    D2D1_RECT_F rcLogo = D2D1::RectF(rc.left + 3.0f, rc.top + 2.0f, rc.left + 10.0f, rc.top + 11.0f);
    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcLogo, 1.5f, 1.5f), theme.brushBrandAccent.Get());

    // Miniature Caption buttons on right
    float capX = rc.right - 14.0f;
    float capY = rc.top + headerH / 2.0f;
    pRT->DrawLine(D2D1::Point2F(capX, capY), D2D1::Point2F(capX + 3.0f, capY), theme.brushTextSecondary.Get(), 1.0f);
    pRT->DrawRectangle(D2D1::RectF(capX + 5.0f, capY - 1.5f, capX + 8.0f, capY + 1.5f), theme.brushTextSecondary.Get(), 0.8f);
    pRT->DrawLine(D2D1::Point2F(capX + 10.0f, capY - 1.5f), D2D1::Point2F(capX + 13.0f, capY + 1.5f), theme.brushTextSecondary.Get(), 0.8f);

    // 3. Activity Rail (#3D1461)
    D2D1_RECT_F rcRail = D2D1::RectF(rc.left, rc.top + headerH, rc.left + railW, rc.bottom);
    pRT->FillRectangle(rcRail, theme.brushRailBg.Get());
    // Miniature rail dots
    pRT->FillRectangle(D2D1::RectF(rc.left + 3.5f, rc.top + headerH + 4.0f, rc.left + 8.5f, rc.top + headerH + 8.0f), theme.brushTextWhite.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 3.5f, rc.top + headerH + 11.0f, rc.left + 8.5f, rc.top + headerH + 14.0f), theme.brushTextSecondary.Get());

    // 4. Drawer Panel (#181818)
    D2D1_RECT_F rcDrawer = D2D1::RectF(rc.left + railW, rc.top + headerH, rc.left + railW + drawerW, rc.bottom);
    pRT->FillRectangle(rcDrawer, theme.brushDrawerBg.Get());
    // Miniature explorer tree lines
    float treeY = rc.top + headerH + 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 4.0f, treeY, rc.left + railW + 18.0f, treeY + 2.0f), theme.brushTextSecondary.Get());
    treeY += 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 8.0f, treeY, rc.left + railW + 28.0f, treeY + 2.0f), theme.brushIconFolder.Get());
    treeY += 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 12.0f, treeY, rc.left + railW + 30.0f, treeY + 2.0f), theme.brushSetiPhp ? theme.brushSetiPhp.Get() : theme.brushIconPurple.Get());
    treeY += 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 12.0f, treeY, rc.left + railW + 26.0f, treeY + 2.0f), theme.brushSetiJs ? theme.brushSetiJs.Get() : theme.brushIconYellow.Get());

    // 5. Editor Tab Bar (#181818)
    float canvasX = rc.left + railW + drawerW;
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasX, rc.top + headerH, rc.right, rc.top + headerH + tabH);
    pRT->FillRectangle(rcTabBar, theme.brushTabBarBg.Get());

    // Active Tab (#1E1E1E) with blue top border
    const float activeTabW = 42.0f;
    D2D1_RECT_F rcActiveTab = D2D1::RectF(canvasX, rc.top + headerH, canvasX + activeTabW, rc.top + headerH + tabH);
    pRT->FillRectangle(rcActiveTab, theme.brushTabActiveBg.Get());
    pRT->DrawLine(D2D1::Point2F(rcActiveTab.left, rcActiveTab.top + 0.5f), D2D1::Point2F(rcActiveTab.right, rcActiveTab.top + 0.5f), theme.brushBrandAccent.Get(), 1.2f);
    // Tiny tab text line
    pRT->FillRectangle(D2D1::RectF(rcActiveTab.left + 8.0f, rcActiveTab.top + 4.0f, rcActiveTab.right - 8.0f, rcActiveTab.top + 6.0f), theme.brushTextWhite.Get());

    // 6. Syntax Code Lines
    float codeY = rc.top + headerH + tabH + 6.0f;
    float codeX = canvasX + 8.0f;

    // Line 1: Keyword purple
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 16.0f, codeY + 2.5f), theme.brushSyntaxKeyword.Get());
    codeY += 6.5f;

    // Line 2: Namespace + identifier
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 22.0f, codeY + 2.5f), theme.brushSyntaxKeyword.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 26.0f, codeY, codeX + 50.0f, codeY + 2.5f), theme.brushTextPrimary.Get());
    codeY += 6.5f;

    // Line 3: Object instantiation (teal)
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 14.0f, codeY + 2.5f), theme.brushSyntaxType.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 18.0f, codeY, codeX + 38.0f, codeY + 2.5f), theme.brushTextPrimary.Get());
    codeY += 6.5f;

    // Line 4: Comment (green)
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 44.0f, codeY + 2.5f), theme.brushSyntaxComment.Get());
    codeY += 6.5f;

    // Line 5: String (orange)
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 10.0f, codeY + 2.5f), theme.brushSyntaxKeyword.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 14.0f, codeY, codeX + 42.0f, codeY + 2.5f), theme.brushSyntaxString.Get());
}

void ThemePickerModal::DrawLightPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, const D2D1_RECT_F& rc) {
    // 1. Light IDE Canvas (#FFFFFF)
    pRT->FillRectangle(rc, theme.brushTextWhite.Get()); // White

    const float headerH = config.ScaleF(13.0f);
    const float railW   = config.ScaleF(12.0f);
    const float drawerW = config.ScaleF(36.0f);
    const float tabH    = config.ScaleF(10.0f);

    // Light Theme Brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightHeader;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightDrawer;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightTabBar;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightTabActive;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightCodeBlue;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightCodeTeal;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightCodeGreen;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightCodeRed;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLightDarkText;

    pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f), &brushLightHeader);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f), &brushLightDrawer);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.91f, 0.91f, 0.91f, 1.0f), &brushLightTabBar);
    pRT->CreateSolidColorBrush(D2D1::ColorF(1.00f, 1.00f, 1.00f, 1.0f), &brushLightTabActive);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.00f, 0.00f, 0.95f, 1.0f), &brushLightCodeBlue);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.50f, 0.60f, 1.0f), &brushLightCodeTeal);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.00f, 0.50f, 0.00f, 1.0f), &brushLightCodeGreen);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.08f, 0.08f, 1.0f), &brushLightCodeRed);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &brushLightDarkText);

    // 2. Header Bar (#3D1461)
    D2D1_RECT_F rcHeader = D2D1::RectF(rc.left, rc.top, rc.right, rc.top + headerH);
    pRT->FillRectangle(rcHeader, brushLightHeader.Get());

    // Miniature Logo
    D2D1_RECT_F rcLogo = D2D1::RectF(rc.left + 3.0f, rc.top + 2.0f, rc.left + 10.0f, rc.top + 11.0f);
    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcLogo, 1.5f, 1.5f), theme.brushBrandAccent.Get());

    // Miniature Caption buttons on right
    float capX = rc.right - 14.0f;
    float capY = rc.top + headerH / 2.0f;
    pRT->DrawLine(D2D1::Point2F(capX, capY), D2D1::Point2F(capX + 3.0f, capY), theme.brushTextPrimary.Get(), 1.0f);
    pRT->DrawRectangle(D2D1::RectF(capX + 5.0f, capY - 1.5f, capX + 8.0f, capY + 1.5f), theme.brushTextPrimary.Get(), 0.8f);
    pRT->DrawLine(D2D1::Point2F(capX + 10.0f, capY - 1.5f), D2D1::Point2F(capX + 13.0f, capY + 1.5f), theme.brushTextPrimary.Get(), 0.8f);

    // 3. Activity Rail
    D2D1_RECT_F rcRail = D2D1::RectF(rc.left, rc.top + headerH, rc.left + railW, rc.bottom);
    pRT->FillRectangle(rcRail, brushLightHeader.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 3.5f, rc.top + headerH + 4.0f, rc.left + 8.5f, rc.top + headerH + 8.0f), theme.brushTextPrimary.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 3.5f, rc.top + headerH + 11.0f, rc.left + 8.5f, rc.top + headerH + 14.0f), theme.brushTextSecondary.Get());

    // 4. Drawer Panel (#F3F3F3)
    D2D1_RECT_F rcDrawer = D2D1::RectF(rc.left + railW, rc.top + headerH, rc.left + railW + drawerW, rc.bottom);
    pRT->FillRectangle(rcDrawer, brushLightDrawer.Get());
    // Miniature light tree lines
    float treeY = rc.top + headerH + 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 4.0f, treeY, rc.left + railW + 18.0f, treeY + 2.0f), theme.brushTextMuted.Get());
    treeY += 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 8.0f, treeY, rc.left + railW + 28.0f, treeY + 2.0f), theme.brushIconFolder.Get());
    treeY += 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 12.0f, treeY, rc.left + railW + 30.0f, treeY + 2.0f), theme.brushSetiPhp ? theme.brushSetiPhp.Get() : theme.brushIconPurple.Get());
    treeY += 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 12.0f, treeY, rc.left + railW + 26.0f, treeY + 2.0f), theme.brushSetiJs ? theme.brushSetiJs.Get() : theme.brushIconYellow.Get());

    // 5. Editor Tab Bar (#EBEBEB)
    float canvasX = rc.left + railW + drawerW;
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasX, rc.top + headerH, rc.right, rc.top + headerH + tabH);
    pRT->FillRectangle(rcTabBar, brushLightTabBar.Get());

    // Active Tab (#FFFFFF) with blue top border
    const float activeTabW = 42.0f;
    D2D1_RECT_F rcActiveTab = D2D1::RectF(canvasX, rc.top + headerH, canvasX + activeTabW, rc.top + headerH + tabH);
    pRT->FillRectangle(rcActiveTab, brushLightTabActive.Get());
    pRT->DrawLine(D2D1::Point2F(rcActiveTab.left, rcActiveTab.top + 0.5f), D2D1::Point2F(rcActiveTab.right, rcActiveTab.top + 0.5f), theme.brushBrandAccent.Get(), 1.2f);
    pRT->FillRectangle(D2D1::RectF(rcActiveTab.left + 8.0f, rcActiveTab.top + 4.0f, rcActiveTab.right - 8.0f, rcActiveTab.top + 6.0f), brushLightDarkText.Get());

    // 6. Light Syntax Code Lines
    float codeY = rc.top + headerH + tabH + 6.0f;
    float codeX = canvasX + 8.0f;

    // Line 1: Blue keyword
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 16.0f, codeY + 2.5f), brushLightCodeBlue.Get());
    codeY += 6.5f;

    // Line 2: Blue keyword + dark text
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 22.0f, codeY + 2.5f), brushLightCodeBlue.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 26.0f, codeY, codeX + 50.0f, codeY + 2.5f), brushLightDarkText.Get());
    codeY += 6.5f;

    // Line 3: Teal type
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 14.0f, codeY + 2.5f), brushLightCodeTeal.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 18.0f, codeY, codeX + 38.0f, codeY + 2.5f), brushLightDarkText.Get());
    codeY += 6.5f;

    // Line 4: Green comment
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 44.0f, codeY + 2.5f), brushLightCodeGreen.Get());
    codeY += 6.5f;

    // Line 5: Red string
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 10.0f, codeY + 2.5f), brushLightCodeBlue.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 14.0f, codeY, codeX + 42.0f, codeY + 2.5f), brushLightCodeRed.Get());
}

void ThemePickerModal::DrawAutoSplitPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, const D2D1_RECT_F& rc) {
    const float midX = (rc.left + rc.right) / 2.0f;

    // Left Half: Renders the exact left 50% of the IDE in Light Mode!
    D2D1_RECT_F rcLeftClip = D2D1::RectF(rc.left, rc.top, midX, rc.bottom);
    pRT->PushAxisAlignedClip(rcLeftClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DrawLightPreview(pRT, theme, config, rc);
    pRT->PopAxisAlignedClip();

    // Right Half: Renders the exact right 50% of the IDE in Dark Mode!
    D2D1_RECT_F rcRightClip = D2D1::RectF(midX, rc.top, rc.right, rc.bottom);
    pRT->PushAxisAlignedClip(rcRightClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DrawDarkPreview(pRT, theme, config, rc);
    pRT->PopAxisAlignedClip();

    // Center Connected Vertical Dividing Slice Line (Accent Blue / White)
    pRT->DrawLine(
        D2D1::Point2F(midX, rc.top),
        D2D1::Point2F(midX, rc.bottom),
        theme.brushBrandAccent.Get(),
        1.5f
    );
}

void ThemePickerModal::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              float clientW, float clientH) {
    if (!m_isOpen || !pRT) return;

    // 1. Semi-transparent dark background scrim
    D2D1_RECT_F rcScrim = D2D1::RectF(0.0f, 0.0f, clientW, clientH);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushScrim;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f), &brushScrim);
    pRT->FillRectangle(rcScrim, brushScrim.Get());

    // 2. Centered Modal Box (580px x 320px)
    const float modalW = config.ScaleF(580.0f);
    const float modalH = config.ScaleF(320.0f);
    const float modalX = (clientW - modalW) / 2.0f;
    const float modalY = (clientH - modalH) / 2.0f;

    m_modalBounds = D2D1::RectF(modalX, modalY, modalX + modalW, modalY + modalH);
    D2D1_ROUNDED_RECT rrcModal = D2D1::RoundedRect(m_modalBounds, 8.0f, 8.0f);

    // Modal Background (#202020) and Border (#454545)
    pRT->FillRoundedRectangle(rrcModal, theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrcModal, theme.brushBorderHover.Get(), 1.0f);

    // 3. Header: "Color Theme" and Close (×) Button
    D2D1_RECT_F rcTitle = D2D1::RectF(
        modalX + config.ScaleF(24.0f),
        modalY + config.ScaleF(20.0f),
        modalX + modalW - config.ScaleF(60.0f),
        modalY + config.ScaleF(42.0f)
    );
    const wchar_t* titleStr = L"Color Theme";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  theme.fontButtonCenter.Get(), rcTitle, theme.brushTextWhite.Get());

    D2D1_RECT_F rcSubtitle = D2D1::RectF(
        modalX + config.ScaleF(24.0f),
        modalY + config.ScaleF(44.0f),
        modalX + modalW - config.ScaleF(60.0f),
        modalY + config.ScaleF(64.0f)
    );
    const wchar_t* subStr = L"Choose how phide looks to you";
    pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)),
                  theme.fontMenuShortcut.Get(), rcSubtitle, theme.brushTextMuted.Get());

    // Close button (×)
    const float closeSize = config.ScaleF(24.0f);
    m_closeBounds = D2D1::RectF(
        modalX + modalW - config.ScaleF(36.0f),
        modalY + config.ScaleF(16.0f),
        modalX + modalW - config.ScaleF(12.0f),
        modalY + config.ScaleF(40.0f)
    );
    if (m_hoveredClose) {
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_closeBounds, 4.0f, 4.0f), theme.brushItemHover.Get());
    }
    float cx = (m_closeBounds.left + m_closeBounds.right) / 2.0f;
    float cy = (m_closeBounds.top + m_closeBounds.bottom) / 2.0f;
    float r = config.ScaleF(4.0f);
    pRT->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), theme.brushTextSecondary.Get(), 1.4f);
    pRT->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx - r, cy + r), theme.brushTextSecondary.Get(), 1.4f);

    // 4. Three Theme Cards Side-by-Side: Dark, Light, Auto
    const float cardW = config.ScaleF(164.0f);
    const float cardH = config.ScaleF(210.0f);
    const float gapX  = config.ScaleF(20.0f);
    const float startX = modalX + config.ScaleF(24.0f);
    const float startY = modalY + config.ScaleF(76.0f);

    struct CardMeta {
        ThemeMode mode;
        const wchar_t* label;
    };
    CardMeta metas[3] = {
        { ThemeMode::Dark, L"Dark Mode" },
        { ThemeMode::Light, L"Light Mode" },
        { ThemeMode::Auto, L"Auto (System)" }
    };

    for (int i = 0; i < 3; ++i) {
        float x = startX + static_cast<float>(i) * (cardW + gapX);
        m_cardBounds[i] = D2D1::RectF(x, startY, x + cardW, startY + cardH);

        bool isSelected = (m_selectedMode == metas[i].mode);
        bool isHovered = (m_hoveredCard == i);

        // Card Container
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_cardBounds[i], 6.0f, 6.0f);
        pRT->FillRoundedRectangle(rrcCard, theme.brushDrawerBg.Get());

        if (isSelected) {
            pRT->DrawRoundedRectangle(rrcCard, theme.brushBrandAccent.Get(), 2.0f);
        } else if (isHovered) {
            pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderHover.Get(), 1.5f);
        } else {
            pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 1.0f);
        }

        // Top Preview Box (152px x 105px)
        const float padBox = config.ScaleF(8.0f);
        m_previewBoxBounds[i] = D2D1::RectF(
            x + padBox,
            startY + padBox,
            x + cardW - padBox,
            startY + padBox + config.ScaleF(120.0f)
        );

        D2D1_ROUNDED_RECT rrcPreview = D2D1::RoundedRect(m_previewBoxBounds[i], 4.0f, 4.0f);
        pRT->PushAxisAlignedClip(m_previewBoxBounds[i], D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (i == 0) {
            DrawDarkPreview(pRT, theme, config, m_previewBoxBounds[i]);
        } else if (i == 1) {
            DrawLightPreview(pRT, theme, config, m_previewBoxBounds[i]);
        } else {
            DrawAutoSplitPreview(pRT, theme, config, m_previewBoxBounds[i]);
        }

        pRT->PopAxisAlignedClip();
        pRT->DrawRoundedRectangle(rrcPreview, theme.brushBorderSubtle.Get(), 1.0f);

        // Bottom: Radio Circle + Label
        float bottomAreaY = m_previewBoxBounds[i].bottom + config.ScaleF(18.0f);
        float radioX = x + config.ScaleF(16.0f);
        float radioRadius = config.ScaleF(6.0f);

        // Radio circle outline
        D2D1_ELLIPSE elOuter = D2D1::Ellipse(D2D1::Point2F(radioX, bottomAreaY), radioRadius, radioRadius);
        pRT->DrawEllipse(elOuter, isSelected ? theme.brushBrandAccent.Get() : theme.brushTextMuted.Get(), 1.5f);

        // Radio center dot if selected
        if (isSelected) {
            D2D1_ELLIPSE elInner = D2D1::Ellipse(D2D1::Point2F(radioX, bottomAreaY), radioRadius - 2.5f, radioRadius - 2.5f);
            pRT->FillEllipse(elInner, theme.brushBrandAccent.Get());
        }

        // Label text
        D2D1_RECT_F rcLabel = D2D1::RectF(
            radioX + radioRadius + config.ScaleF(8.0f),
            bottomAreaY - config.ScaleF(10.0f),
            x + cardW - config.ScaleF(6.0f),
            bottomAreaY + config.ScaleF(14.0f)
        );
        pRT->DrawText(metas[i].label, static_cast<UINT32>(wcslen(metas[i].label)),
                      theme.fontButtonCenter.Get(), rcLabel,
                      isSelected ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
    }
}

bool ThemePickerModal::HandleMouseMove(POINT clientPt) {
    if (!m_isOpen) return false;

    int oldHover = m_hoveredCard;
    bool oldClose = m_hoveredClose;

    m_hoveredCard = -1;
    m_hoveredClose = (clientPt.x >= m_closeBounds.left && clientPt.x <= m_closeBounds.right &&
                      clientPt.y >= m_closeBounds.top && clientPt.y <= m_closeBounds.bottom);

    for (int i = 0; i < 3; ++i) {
        if (clientPt.x >= m_cardBounds[i].left && clientPt.x <= m_cardBounds[i].right &&
            clientPt.y >= m_cardBounds[i].top && clientPt.y <= m_cardBounds[i].bottom) {
            m_hoveredCard = i;
            break;
        }
    }

    return (m_hoveredCard != oldHover || m_hoveredClose != oldClose);
}

bool ThemePickerModal::HandleLButtonDown(POINT clientPt, ThemeMode& outChosenMode) {
    if (!m_isOpen) return false;

    // Click on Close button
    if (clientPt.x >= m_closeBounds.left && clientPt.x <= m_closeBounds.right &&
        clientPt.y >= m_closeBounds.top && clientPt.y <= m_closeBounds.bottom) {
        Close();
        return true;
    }

    // Click on Cards
    for (int i = 0; i < 3; ++i) {
        if (clientPt.x >= m_cardBounds[i].left && clientPt.x <= m_cardBounds[i].right &&
            clientPt.y >= m_cardBounds[i].top && clientPt.y <= m_cardBounds[i].bottom) {
            if (i == 0) outChosenMode = ThemeMode::Dark;
            else if (i == 1) outChosenMode = ThemeMode::Light;
            else outChosenMode = ThemeMode::Auto;
            m_selectedMode = outChosenMode;
            Close();
            return true;
        }
    }

    // Click outside modal closes it
    if (clientPt.x < m_modalBounds.left || clientPt.x > m_modalBounds.right ||
        clientPt.y < m_modalBounds.top || clientPt.y > m_modalBounds.bottom) {
        Close();
        return true;
    }

    return true; // Click inside modal swallowed
}

} // namespace phide::ui
