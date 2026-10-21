/**
 * @file ThemePickerModal.cpp
 * @brief Implementation of Theme Picker modal dialog with connected split preview.
 */

#include "phide/ui/ThemePickerModal.hpp"
#include <algorithm>

namespace phide::ui {

namespace {

struct DarkPreviewPalette {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> canvas;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> header;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rail;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> drawer;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tabBar;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tabActive;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textWhite;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textSec;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textMuted;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lineNum;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> kw;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> type;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> str;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> comment;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> php;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> js;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cpp;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> folder;

    DarkPreviewPalette(ID2D1RenderTarget* pRT) {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f), &canvas);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f), &header);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f), &rail);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f), &drawer);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f), &tabBar);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f), &tabActive);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f), &accent);
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &textWhite);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.65f, 0.65f, 1.0f), &textSec);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.40f, 0.40f, 1.0f), &textMuted);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.522f, 0.522f, 0.522f, 1.0f), &lineNum);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.173f, 0.173f, 0.173f, 1.0f), &border);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.773f, 0.525f, 0.753f, 1.0f), &kw);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.306f, 0.788f, 0.690f, 1.0f), &type);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.808f, 0.569f, 0.471f, 1.0f), &str);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.416f, 0.600f, 0.333f, 1.0f), &comment);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.467f, 0.482f, 0.706f, 1.0f), &php);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.969f, 0.875f, 0.118f, 1.0f), &js);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.318f, 0.604f, 0.729f, 1.0f), &cpp);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.475f, 0.596f, 0.710f, 1.0f), &folder);
    }
};

struct LightPreviewPalette {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> canvas;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> header;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rail;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> drawer;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tabBar;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tabActive;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textDark;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textSec;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textMuted;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lineNum;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> kw;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> type;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> str;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> comment;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> php;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> js;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cpp;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> folder;

    LightPreviewPalette(ID2D1RenderTarget* pRT) {
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &canvas);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.812f, 0.702f, 0.910f, 1.0f), &header); // #CFB3E8 Light Purple
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.812f, 0.702f, 0.910f, 1.0f), &rail);   // #CFB3E8 Light Purple
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.953f, 0.953f, 0.953f, 1.0f), &drawer);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.922f, 0.922f, 0.922f, 1.0f), &tabBar);
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &tabActive);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f), &accent);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &textDark);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.38f, 0.38f, 1.0f), &textSec);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.0f), &textMuted);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.43f, 0.46f, 0.51f, 1.0f), &lineNum);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.88f, 0.88f, 0.88f, 1.0f), &border);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.00f, 0.00f, 0.95f, 1.0f), &kw);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.50f, 0.60f, 1.0f), &type);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.08f, 0.08f, 1.0f), &str);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.00f, 0.50f, 0.00f, 1.0f), &comment);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.467f, 0.482f, 0.706f, 1.0f), &php);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.65f, 0.05f, 1.0f), &js);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f), &cpp);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.475f, 0.596f, 0.710f, 1.0f), &folder);
    }
};

} // anonymous namespace

ThemePickerModal::ThemePickerModal() = default;

void ThemePickerModal::DrawDarkPreview(ID2D1RenderTarget* pRT, const app::AppConfig& config, const D2D1_RECT_F& rc) {
    DarkPreviewPalette pal(pRT);

    // 1. Dark IDE Canvas (#1E1E1E)
    pRT->FillRectangle(rc, pal.canvas.Get());

    const float headerH = config.ScaleF(15.0f);
    const float railW   = config.ScaleF(15.0f);
    const float drawerW = config.ScaleF(56.0f);
    const float tabH    = config.ScaleF(13.0f);

    // 2. Header Bar (#3D1461)
    D2D1_RECT_F rcHeader = D2D1::RectF(rc.left, rc.top, rc.right, rc.top + headerH);
    pRT->FillRectangle(rcHeader, pal.header.Get());

    // Miniature Logo on left
    D2D1_RECT_F rcLogo = D2D1::RectF(rc.left + 4.0f, rc.top + 2.5f, rc.left + 12.0f, rc.top + 12.5f);
    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcLogo, 2.0f, 2.0f), pal.accent.Get());

    // Miniature Caption buttons on right
    float capX = rc.right - 18.0f;
    float capY = rc.top + headerH / 2.0f;
    pRT->DrawLine(D2D1::Point2F(capX, capY), D2D1::Point2F(capX + 4.0f, capY), pal.textSec.Get(), 1.0f);
    pRT->DrawRectangle(D2D1::RectF(capX + 6.0f, capY - 2.0f, capX + 10.0f, capY + 2.0f), pal.textSec.Get(), 0.9f);
    pRT->DrawLine(D2D1::Point2F(capX + 12.0f, capY - 2.0f), D2D1::Point2F(capX + 16.0f, capY + 2.0f), pal.textSec.Get(), 0.9f);

    // 3. Activity Rail (#3D1461)
    D2D1_RECT_F rcRail = D2D1::RectF(rc.left, rc.top + headerH, rc.left + railW, rc.bottom);
    pRT->FillRectangle(rcRail, pal.rail.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 4.0f, rc.top + headerH + 5.0f, rc.left + 11.0f, rc.top + headerH + 11.0f), pal.textWhite.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 4.0f, rc.top + headerH + 15.0f, rc.left + 11.0f, rc.top + headerH + 19.0f), pal.textSec.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 4.0f, rc.top + headerH + 23.0f, rc.left + 11.0f, rc.top + headerH + 27.0f), pal.textSec.Get());

    // 4. Drawer Panel (#181818)
    D2D1_RECT_F rcDrawer = D2D1::RectF(rc.left + railW, rc.top + headerH, rc.left + railW + drawerW, rc.bottom);
    pRT->FillRectangle(rcDrawer, pal.drawer.Get());
    pRT->DrawLine(D2D1::Point2F(rcDrawer.right, rcDrawer.top), D2D1::Point2F(rcDrawer.right, rcDrawer.bottom), pal.border.Get(), 1.0f);

    float treeY = rc.top + headerH + 6.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 6.0f, treeY, rc.left + railW + 28.0f, treeY + 3.0f), pal.textSec.Get());
    treeY += 7.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 10.0f, treeY, rc.left + railW + 42.0f, treeY + 3.0f), pal.folder.Get());
    treeY += 7.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 16.0f, treeY, rc.left + railW + 46.0f, treeY + 3.0f), pal.php.Get());
    treeY += 7.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 16.0f, treeY, rc.left + railW + 40.0f, treeY + 3.0f), pal.js.Get());
    treeY += 7.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 16.0f, treeY, rc.left + railW + 44.0f, treeY + 3.0f), pal.cpp.Get());

    // 5. Editor Tab Bar (#181818)
    float canvasX = rc.left + railW + drawerW;
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasX, rc.top + headerH, rc.right, rc.top + headerH + tabH);
    pRT->FillRectangle(rcTabBar, pal.tabBar.Get());

    // Active Tab (#1E1E1E) with blue top border
    const float activeTabW = 60.0f;
    D2D1_RECT_F rcActiveTab = D2D1::RectF(canvasX, rc.top + headerH, canvasX + activeTabW, rc.top + headerH + tabH);
    pRT->FillRectangle(rcActiveTab, pal.tabActive.Get());
    pRT->DrawLine(D2D1::Point2F(rcActiveTab.left, rcActiveTab.top + 0.5f), D2D1::Point2F(rcActiveTab.right, rcActiveTab.top + 0.5f), pal.accent.Get(), 1.5f);
    pRT->FillRectangle(D2D1::RectF(rcActiveTab.left + 10.0f, rcActiveTab.top + 5.0f, rcActiveTab.right - 10.0f, rcActiveTab.top + 8.0f), pal.textWhite.Get());

    // Inactive Tab (#181818)
    D2D1_RECT_F rcInactTab = D2D1::RectF(canvasX + activeTabW, rc.top + headerH, canvasX + activeTabW + 55.0f, rc.top + headerH + tabH);
    pRT->FillRectangle(D2D1::RectF(rcInactTab.left + 10.0f, rcInactTab.top + 5.0f, rcInactTab.right - 10.0f, rcInactTab.top + 8.0f), pal.textMuted.Get());

    // 6. Code Gutter
    const float gutterW = 20.0f;
    D2D1_RECT_F rcGutter = D2D1::RectF(canvasX, rc.top + headerH + tabH, canvasX + gutterW, rc.bottom);
    pRT->FillRectangle(rcGutter, pal.canvas.Get());
    pRT->DrawLine(D2D1::Point2F(rcGutter.right, rcGutter.top), D2D1::Point2F(rcGutter.right, rcGutter.bottom), pal.border.Get(), 0.8f);

    float numY = rc.top + headerH + tabH + 6.0f;
    for (int l = 0; l < 9; ++l) {
        pRT->FillRectangle(D2D1::RectF(canvasX + 6.0f, numY + 1.0f, canvasX + 14.0f, numY + 3.0f), pal.lineNum.Get());
        numY += 9.5f;
    }

    // 7. Syntax Code Lines
    float codeY = rc.top + headerH + tabH + 6.0f;
    float codeX = canvasX + gutterW + 8.0f;

    // Line 1: Keyword purple
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 24.0f, codeY + 3.0f), pal.kw.Get());
    codeY += 9.5f;

    // Line 2: Namespace + identifier
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 34.0f, codeY + 3.0f), pal.kw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 38.0f, codeY, codeX + 80.0f, codeY + 3.0f), pal.textWhite.Get());
    codeY += 9.5f;

    // Line 3: Class definition
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 20.0f, codeY + 3.0f), pal.kw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 24.0f, codeY, codeX + 60.0f, codeY + 3.0f), pal.type.Get());
    codeY += 9.5f;

    // Line 4: Comment
    pRT->FillRectangle(D2D1::RectF(codeX + 12.0f, codeY, codeX + 85.0f, codeY + 3.0f), pal.comment.Get());
    codeY += 9.5f;

    // Line 5: Type + Variable
    pRT->FillRectangle(D2D1::RectF(codeX + 12.0f, codeY, codeX + 34.0f, codeY + 3.0f), pal.type.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 38.0f, codeY, codeX + 70.0f, codeY + 3.0f), pal.textWhite.Get());
    codeY += 9.5f;

    // Line 6: Function method
    pRT->FillRectangle(D2D1::RectF(codeX + 12.0f, codeY, codeX + 36.0f, codeY + 3.0f), pal.kw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 40.0f, codeY, codeX + 90.0f, codeY + 3.0f), pal.str.Get());
    codeY += 9.5f;

    // Line 7: Return string
    pRT->FillRectangle(D2D1::RectF(codeX + 24.0f, codeY, codeX + 46.0f, codeY + 3.0f), pal.kw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 50.0f, codeY, codeX + 110.0f, codeY + 3.0f), pal.str.Get());
}

void ThemePickerModal::DrawLightPreview(ID2D1RenderTarget* pRT, const app::AppConfig& config, const D2D1_RECT_F& rc) {
    LightPreviewPalette pal(pRT);

    // 1. Light IDE Canvas (#FFFFFF)
    pRT->FillRectangle(rc, pal.canvas.Get());

    const float headerH = config.ScaleF(15.0f);
    const float railW   = config.ScaleF(15.0f);
    const float drawerW = config.ScaleF(56.0f);
    const float tabH    = config.ScaleF(13.0f);

    // 2. Header Bar (#3D1461)
    D2D1_RECT_F rcHeader = D2D1::RectF(rc.left, rc.top, rc.right, rc.top + headerH);
    pRT->FillRectangle(rcHeader, pal.header.Get());

    // Miniature Logo
    D2D1_RECT_F rcLogo = D2D1::RectF(rc.left + 4.0f, rc.top + 2.5f, rc.left + 12.0f, rc.top + 12.5f);
    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcLogo, 2.0f, 2.0f), pal.accent.Get());

    // Miniature Caption buttons on right
    float capX = rc.right - 18.0f;
    float capY = rc.top + headerH / 2.0f;
    pRT->DrawLine(D2D1::Point2F(capX, capY), D2D1::Point2F(capX + 4.0f, capY), pal.textDark.Get(), 1.0f);
    pRT->DrawRectangle(D2D1::RectF(capX + 6.0f, capY - 2.0f, capX + 10.0f, capY + 2.0f), pal.textDark.Get(), 0.9f);
    pRT->DrawLine(D2D1::Point2F(capX + 12.0f, capY - 2.0f), D2D1::Point2F(capX + 16.0f, capY + 2.0f), pal.textDark.Get(), 0.9f);

    // 3. Activity Rail
    D2D1_RECT_F rcRail = D2D1::RectF(rc.left, rc.top + headerH, rc.left + railW, rc.bottom);
    pRT->FillRectangle(rcRail, pal.rail.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 4.0f, rc.top + headerH + 5.0f, rc.left + 11.0f, rc.top + headerH + 11.0f), pal.textDark.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 4.0f, rc.top + headerH + 15.0f, rc.left + 11.0f, rc.top + headerH + 19.0f), pal.textSec.Get());

    // 4. Drawer Panel (#F3F3F3)
    D2D1_RECT_F rcDrawer = D2D1::RectF(rc.left + railW, rc.top + headerH, rc.left + railW + drawerW, rc.bottom);
    pRT->FillRectangle(rcDrawer, pal.drawer.Get());
    pRT->DrawLine(D2D1::Point2F(rcDrawer.right, rcDrawer.top), D2D1::Point2F(rcDrawer.right, rcDrawer.bottom), pal.border.Get(), 1.0f);

    float treeY = rc.top + headerH + 6.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 6.0f, treeY, rc.left + railW + 28.0f, treeY + 3.0f), pal.textSec.Get());
    treeY += 7.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 10.0f, treeY, rc.left + railW + 42.0f, treeY + 3.0f), pal.folder.Get());
    treeY += 7.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 16.0f, treeY, rc.left + railW + 46.0f, treeY + 3.0f), pal.php.Get());
    treeY += 7.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 16.0f, treeY, rc.left + railW + 40.0f, treeY + 3.0f), pal.js.Get());

    // 5. Editor Tab Bar (#EBEBEB)
    float canvasX = rc.left + railW + drawerW;
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasX, rc.top + headerH, rc.right, rc.top + headerH + tabH);
    pRT->FillRectangle(rcTabBar, pal.tabBar.Get());

    // Active Tab (#FFFFFF) with blue top border
    const float activeTabW = 60.0f;
    D2D1_RECT_F rcActiveTab = D2D1::RectF(canvasX, rc.top + headerH, canvasX + activeTabW, rc.top + headerH + tabH);
    pRT->FillRectangle(rcActiveTab, pal.tabActive.Get());
    pRT->DrawLine(D2D1::Point2F(rcActiveTab.left, rcActiveTab.top + 0.5f), D2D1::Point2F(rcActiveTab.right, rcActiveTab.top + 0.5f), pal.accent.Get(), 1.5f);
    pRT->FillRectangle(D2D1::RectF(rcActiveTab.left + 10.0f, rcActiveTab.top + 5.0f, rcActiveTab.right - 10.0f, rcActiveTab.top + 8.0f), pal.textDark.Get());

    // 6. Code Gutter
    const float gutterW = 20.0f;
    D2D1_RECT_F rcGutter = D2D1::RectF(canvasX, rc.top + headerH + tabH, canvasX + gutterW, rc.bottom);
    pRT->FillRectangle(rcGutter, pal.drawer.Get());
    pRT->DrawLine(D2D1::Point2F(rcGutter.right, rcGutter.top), D2D1::Point2F(rcGutter.right, rcGutter.bottom), pal.border.Get(), 0.8f);

    float numY = rc.top + headerH + tabH + 6.0f;
    for (int l = 0; l < 9; ++l) {
        pRT->FillRectangle(D2D1::RectF(canvasX + 6.0f, numY + 1.0f, canvasX + 14.0f, numY + 3.0f), pal.lineNum.Get());
        numY += 9.5f;
    }

    // 7. Light Syntax Code Lines
    float codeY = rc.top + headerH + tabH + 6.0f;
    float codeX = canvasX + gutterW + 8.0f;

    // Line 1: Blue keyword
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 24.0f, codeY + 3.0f), pal.kw.Get());
    codeY += 9.5f;

    // Line 2: Blue keyword + dark text
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 34.0f, codeY + 3.0f), pal.kw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 38.0f, codeY, codeX + 80.0f, codeY + 3.0f), pal.textDark.Get());
    codeY += 9.5f;

    // Line 3: Teal type
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 20.0f, codeY + 3.0f), pal.type.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 24.0f, codeY, codeX + 60.0f, codeY + 3.0f), pal.textDark.Get());
    codeY += 9.5f;

    // Line 4: Green comment
    pRT->FillRectangle(D2D1::RectF(codeX + 12.0f, codeY, codeX + 85.0f, codeY + 3.0f), pal.comment.Get());
    codeY += 9.5f;

    // Line 5: Red string
    pRT->FillRectangle(D2D1::RectF(codeX + 12.0f, codeY, codeX + 34.0f, codeY + 3.0f), pal.kw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 38.0f, codeY, codeX + 70.0f, codeY + 3.0f), pal.str.Get());
}

void ThemePickerModal::DrawAutoSplitPreview(ID2D1RenderTarget* pRT, const app::AppConfig& config, const D2D1_RECT_F& rc) {
    const float midX = (rc.left + rc.right) / 2.0f;

    // Left Half: Renders the exact left 50% of the IDE in Light Mode!
    D2D1_RECT_F rcLeftClip = D2D1::RectF(rc.left, rc.top, midX, rc.bottom);
    pRT->PushAxisAlignedClip(rcLeftClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DrawLightPreview(pRT, config, rc);
    pRT->PopAxisAlignedClip();

    // Right Half: Renders the exact right 50% of the IDE in Dark Mode!
    D2D1_RECT_F rcRightClip = D2D1::RectF(midX, rc.top, rc.right, rc.bottom);
    pRT->PushAxisAlignedClip(rcRightClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DrawDarkPreview(pRT, config, rc);
    pRT->PopAxisAlignedClip();

    // Center Connected Vertical Dividing Slice Line (Accent Blue / White)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDivider;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f), &brushDivider);
    pRT->DrawLine(
        D2D1::Point2F(midX, rc.top),
        D2D1::Point2F(midX, rc.bottom),
        brushDivider.Get(),
        2.0f
    );
}

void ThemePickerModal::DrawGenericPreview(ID2D1RenderTarget* pRT, const app::AppConfig& config, const D2D1_RECT_F& rc,
                                          D2D1_COLOR_F canvasCol, D2D1_COLOR_F headerCol, D2D1_COLOR_F drawerCol,
                                          D2D1_COLOR_F tabBarCol, D2D1_COLOR_F tabActiveCol, D2D1_COLOR_F accentCol,
                                          D2D1_COLOR_F textCol, D2D1_COLOR_F kwCol, D2D1_COLOR_F typeCol,
                                          D2D1_COLOR_F strCol, D2D1_COLOR_F commentCol, D2D1_COLOR_F lineNumCol,
                                          D2D1_COLOR_F borderCol) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bCanvas, bHeader, bDrawer, bTabBar, bTabActive, bAccent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bText, bKw, bType, bStr, bComment, bLineNum, bBorder;

    pRT->CreateSolidColorBrush(canvasCol, &bCanvas);
    pRT->CreateSolidColorBrush(headerCol, &bHeader);
    pRT->CreateSolidColorBrush(drawerCol, &bDrawer);
    pRT->CreateSolidColorBrush(tabBarCol, &bTabBar);
    pRT->CreateSolidColorBrush(tabActiveCol, &bTabActive);
    pRT->CreateSolidColorBrush(accentCol, &bAccent);
    pRT->CreateSolidColorBrush(textCol, &bText);
    pRT->CreateSolidColorBrush(kwCol, &bKw);
    pRT->CreateSolidColorBrush(typeCol, &bType);
    pRT->CreateSolidColorBrush(strCol, &bStr);
    pRT->CreateSolidColorBrush(commentCol, &bComment);
    pRT->CreateSolidColorBrush(lineNumCol, &bLineNum);
    pRT->CreateSolidColorBrush(borderCol, &bBorder);

    // Canvas Fill
    pRT->FillRectangle(rc, bCanvas.Get());

    const float headerH = config.ScaleF(14.0f);
    const float railW   = config.ScaleF(14.0f);
    const float drawerW = config.ScaleF(44.0f);
    const float tabH    = config.ScaleF(12.0f);

    // Header Bar
    D2D1_RECT_F rcHeader = D2D1::RectF(rc.left, rc.top, rc.right, rc.top + headerH);
    pRT->FillRectangle(rcHeader, bHeader.Get());

    // Logo
    D2D1_RECT_F rcLogo = D2D1::RectF(rc.left + 3.0f, rc.top + 2.5f, rc.left + 9.5f, rc.top + 11.5f);
    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcLogo, 1.5f, 1.5f), bAccent.Get());

    // Caption buttons
    float capX = rc.right - 14.0f;
    float capY = rc.top + headerH / 2.0f;
    pRT->DrawLine(D2D1::Point2F(capX, capY), D2D1::Point2F(capX + 3.0f, capY), bText.Get(), 0.8f);
    pRT->DrawRectangle(D2D1::RectF(capX + 4.5f, capY - 1.5f, capX + 7.5f, capY + 1.5f), bText.Get(), 0.7f);
    pRT->DrawLine(D2D1::Point2F(capX + 9.0f, capY - 1.5f), D2D1::Point2F(capX + 12.0f, capY + 1.5f), bText.Get(), 0.7f);

    // Rail
    D2D1_RECT_F rcRail = D2D1::RectF(rc.left, rc.top + headerH, rc.left + railW, rc.bottom);
    pRT->FillRectangle(rcRail, bHeader.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 3.0f, rc.top + headerH + 4.0f, rc.left + 9.0f, rc.top + headerH + 9.0f), bAccent.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 3.0f, rc.top + headerH + 11.0f, rc.left + 9.0f, rc.top + headerH + 14.0f), bText.Get());
    pRT->FillRectangle(D2D1::RectF(rc.left + 3.0f, rc.top + headerH + 17.0f, rc.left + 9.0f, rc.top + headerH + 20.0f), bText.Get());

    // Drawer
    D2D1_RECT_F rcDrawer = D2D1::RectF(rc.left + railW, rc.top + headerH, rc.left + railW + drawerW, rc.bottom);
    pRT->FillRectangle(rcDrawer, bDrawer.Get());
    pRT->DrawLine(D2D1::Point2F(rcDrawer.right, rcDrawer.top), D2D1::Point2F(rcDrawer.right, rcDrawer.bottom), bBorder.Get(), 0.8f);

    float treeY = rc.top + headerH + 5.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 4.0f, treeY, rc.left + railW + 22.0f, treeY + 2.5f), bText.Get());
    treeY += 6.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 7.0f, treeY, rc.left + railW + 34.0f, treeY + 2.5f), bAccent.Get());
    treeY += 6.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 11.0f, treeY, rc.left + railW + 36.0f, treeY + 2.5f), bKw.Get());
    treeY += 6.0f;
    pRT->FillRectangle(D2D1::RectF(rc.left + railW + 11.0f, treeY, rc.left + railW + 30.0f, treeY + 2.5f), bType.Get());

    // Tab Bar
    float canvasX = rc.left + railW + drawerW;
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasX, rc.top + headerH, rc.right, rc.top + headerH + tabH);
    pRT->FillRectangle(rcTabBar, bTabBar.Get());

    // Active Tab
    const float activeTabW = 44.0f;
    D2D1_RECT_F rcActiveTab = D2D1::RectF(canvasX, rc.top + headerH, canvasX + activeTabW, rc.top + headerH + tabH);
    pRT->FillRectangle(rcActiveTab, bTabActive.Get());
    pRT->DrawLine(D2D1::Point2F(rcActiveTab.left, rcActiveTab.top + 0.5f), D2D1::Point2F(rcActiveTab.right, rcActiveTab.top + 0.5f), bAccent.Get(), 1.5f);
    pRT->FillRectangle(D2D1::RectF(rcActiveTab.left + 6.0f, rcActiveTab.top + 4.0f, rcActiveTab.right - 6.0f, rcActiveTab.top + 7.0f), bText.Get());

    // Gutter
    const float gutterW = 15.0f;
    D2D1_RECT_F rcGutter = D2D1::RectF(canvasX, rc.top + headerH + tabH, canvasX + gutterW, rc.bottom);
    pRT->FillRectangle(rcGutter, bDrawer.Get());
    pRT->DrawLine(D2D1::Point2F(rcGutter.right, rcGutter.top), D2D1::Point2F(rcGutter.right, rcGutter.bottom), bBorder.Get(), 0.6f);

    float numY = rc.top + headerH + tabH + 5.0f;
    for (int l = 0; l < 8; ++l) {
        pRT->FillRectangle(D2D1::RectF(canvasX + 3.0f, numY + 1.0f, canvasX + 10.0f, numY + 2.5f), bLineNum.Get());
        numY += 8.0f;
    }

    // Syntax lines
    float codeY = rc.top + headerH + tabH + 5.0f;
    float codeX = canvasX + gutterW + 5.0f;

    // Line 1: Keyword
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 18.0f, codeY + 2.5f), bKw.Get());
    codeY += 8.0f;

    // Line 2: Keyword + identifier
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 22.0f, codeY + 2.5f), bKw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 25.0f, codeY, codeX + 50.0f, codeY + 2.5f), bText.Get());
    codeY += 8.0f;

    // Line 3: Class / Type
    pRT->FillRectangle(D2D1::RectF(codeX, codeY, codeX + 14.0f, codeY + 2.5f), bKw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 17.0f, codeY, codeX + 40.0f, codeY + 2.5f), bType.Get());
    codeY += 8.0f;

    // Line 4: Comment
    pRT->FillRectangle(D2D1::RectF(codeX + 6.0f, codeY, codeX + 56.0f, codeY + 2.5f), bComment.Get());
    codeY += 8.0f;

    // Line 5: String
    pRT->FillRectangle(D2D1::RectF(codeX + 6.0f, codeY, codeX + 20.0f, codeY + 2.5f), bKw.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 23.0f, codeY, codeX + 60.0f, codeY + 2.5f), bStr.Get());
    codeY += 8.0f;

    // Line 6: Type + Text
    pRT->FillRectangle(D2D1::RectF(codeX + 6.0f, codeY, codeX + 24.0f, codeY + 2.5f), bType.Get());
    pRT->FillRectangle(D2D1::RectF(codeX + 27.0f, codeY, codeX + 48.0f, codeY + 2.5f), bText.Get());
}

void ThemePickerModal::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              float clientW, float clientH) {
    if (!m_isOpen || !pRT) return;

    bool isDark = theme.IsDarkMode();

    // 1. Semi-transparent background scrim
    D2D1_RECT_F rcScrim = D2D1::RectF(0.0f, 0.0f, clientW, clientH);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushScrim;
    pRT->CreateSolidColorBrush(
        isDark ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f),
        &brushScrim
    );
    pRT->FillRectangle(rcScrim, brushScrim.Get());

    // 2. Centered Modal Box (960px wide for wide widescreen cards)
    const float modalW = config.ScaleF(960.0f);
    const float modalH = m_showAllCatalog ? (std::min)(config.ScaleF(700.0f), clientH - config.ScaleF(30.0f))
                                          : config.ScaleF(430.0f);
    const float modalX = (clientW - modalW) / 2.0f;
    const float modalY = (clientH - modalH) / 2.0f;

    m_modalBounds = D2D1::RectF(modalX, modalY, modalX + modalW, modalY + modalH);
    D2D1_ROUNDED_RECT rrcModal = D2D1::RoundedRect(m_modalBounds, 8.0f, 8.0f);

    // Dedicated high-contrast dialog brushes responsive to active theme
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushModalBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushModalBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardHover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardSelect;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushModalTitle;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushModalSub;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushModalMuted;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLabelSelected;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLabelNormal;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBtnBg;

    if (isDark) {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.125f, 0.125f, 0.125f, 0.98f), &brushModalBg);    // #202020
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.27f, 0.27f, 0.27f, 1.0f),       &brushModalBorder);// #454545
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f),    &brushCardBg);     // #181818
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f),       &brushCardBorder); // #333333
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.35f, 0.35f, 1.0f),       &brushCardHover);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f),      &brushCardSelect); // #0078D4
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),          &brushModalTitle); // Pure White
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.65f, 0.65f, 1.0f),       &brushModalSub);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.0f),       &brushModalMuted);
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),          &brushLabelSelected);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.85f, 0.85f, 1.0f),       &brushLabelNormal);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.18f, 1.0f),       &brushBtnBg);
    } else {
        // Light Mode Dialog Palette: Crisp White Dialog with Charcoal/Black Typography
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f),         &brushModalBg);    // #FFFFFF Crisp White
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.82f, 0.85f, 1.0f),       &brushModalBorder);// #D1D1D9
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.965f, 0.965f, 0.970f, 1.0f),    &brushCardBg);     // #F6F6F8 Light Card Surface
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.86f, 0.86f, 0.88f, 1.0f),       &brushCardBorder); // #DCDCE0
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.70f, 0.70f, 0.75f, 1.0f),       &brushCardHover);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f),      &brushCardSelect); // #0078D4
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f),       &brushModalTitle); // #1E1E1E Dark charcoal / black
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.40f, 0.40f, 1.0f),       &brushModalSub);   // #666666
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.50f, 0.50f, 0.50f, 1.0f),       &brushModalMuted); // #808080
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f),      &brushLabelSelected); // Brand blue for selected label
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f),       &brushLabelNormal);   // #1E1E1E Black label text
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.93f, 0.93f, 0.95f, 1.0f),       &brushBtnBg);
    }

    // Modal Background and Border
    pRT->FillRoundedRectangle(rrcModal, brushModalBg.Get());
    pRT->DrawRoundedRectangle(rrcModal, brushModalBorder.Get(), 1.0f);

    // Helper lambda to draw any theme's preview slice
    auto DrawModePreview = [&](ThemeMode m, const D2D1_RECT_F& rcBox) {
        switch (m) {
            case ThemeMode::Dark:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f), // canvas #1E1E1E
                    D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f),   // header #3D1461
                    D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f), // drawer #181818
                    D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f), // tabBar
                    D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f), // tabActive
                    D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f),   // accent #0078D4
                    D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),       // text
                    D2D1::ColorF(0.773f, 0.525f, 0.753f, 1.0f), // kw
                    D2D1::ColorF(0.306f, 0.788f, 0.690f, 1.0f), // type
                    D2D1::ColorF(0.808f, 0.569f, 0.471f, 1.0f), // str
                    D2D1::ColorF(0.416f, 0.600f, 0.333f, 1.0f), // comment
                    D2D1::ColorF(0.522f, 0.522f, 0.522f, 1.0f), // lineNum
                    D2D1::ColorF(0.173f, 0.173f, 0.173f, 1.0f)  // border
                );
                break;
            case ThemeMode::Light:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),       // canvas
                    D2D1::ColorF(0.812f, 0.702f, 0.910f, 1.0f), // header #CFB3E8
                    D2D1::ColorF(0.953f, 0.953f, 0.953f, 1.0f), // drawer #F3F3F3
                    D2D1::ColorF(0.922f, 0.922f, 0.922f, 1.0f), // tabBar #EBEBEB
                    D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),       // tabActive
                    D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f),   // accent
                    D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f),    // text
                    D2D1::ColorF(0.00f, 0.00f, 0.95f, 1.0f),    // kw
                    D2D1::ColorF(0.15f, 0.50f, 0.60f, 1.0f),    // type
                    D2D1::ColorF(0.65f, 0.08f, 0.08f, 1.0f),    // str
                    D2D1::ColorF(0.00f, 0.50f, 0.00f, 1.0f),    // comment
                    D2D1::ColorF(0.43f, 0.46f, 0.51f, 1.0f),    // lineNum
                    D2D1::ColorF(0.88f, 0.88f, 0.88f, 1.0f)     // border
                );
                break;
            case ThemeMode::Auto:
                DrawAutoSplitPreview(pRT, config, rcBox);
                break;
            case ThemeMode::Purple:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.075f, 0.035f, 0.122f, 1.0f), // canvas #13091F
                    D2D1::ColorF(0.118f, 0.047f, 0.188f, 1.0f), // header #1E0C30
                    D2D1::ColorF(0.063f, 0.027f, 0.102f, 1.0f), // drawer #10071A
                    D2D1::ColorF(0.063f, 0.027f, 0.102f, 1.0f), // tabBar
                    D2D1::ColorF(0.075f, 0.035f, 0.122f, 1.0f), // tabActive
                    D2D1::ColorF(0.616f, 0.306f, 0.867f, 1.0f), // accent #9D4EDD
                    D2D1::ColorF(0.953f, 0.910f, 1.0f, 1.0f),   // text #F3E8FF
                    D2D1::ColorF(0.878f, 0.667f, 1.0f, 1.0f),   // kw #E0AAFF
                    D2D1::ColorF(1.0f, 0.439f, 0.651f, 1.0f),   // type #FF70A6
                    D2D1::ColorF(1.0f, 0.718f, 0.012f, 1.0f),   // str #FFB703
                    D2D1::ColorF(0.427f, 0.349f, 0.478f, 1.0f), // comment #6D597A
                    D2D1::ColorF(0.450f, 0.320f, 0.550f, 1.0f), // lineNum #6B4F82
                    D2D1::ColorF(0.196f, 0.078f, 0.310f, 1.0f)  // border #32144F
                );
                break;
            case ThemeMode::Cyan:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.039f, 0.098f, 0.184f, 1.0f), // canvas #0A192F
                    D2D1::ColorF(0.024f, 0.071f, 0.141f, 1.0f), // header #061224
                    D2D1::ColorF(0.024f, 0.063f, 0.125f, 1.0f), // drawer #061020
                    D2D1::ColorF(0.024f, 0.063f, 0.125f, 1.0f), // tabBar
                    D2D1::ColorF(0.039f, 0.098f, 0.184f, 1.0f), // tabActive
                    D2D1::ColorF(0.0f, 0.898f, 1.0f, 1.0f),     // accent #00E5FF
                    D2D1::ColorF(0.878f, 0.969f, 0.980f, 1.0f), // text #E0F7FA
                    D2D1::ColorF(0.0f, 0.898f, 1.0f, 1.0f),     // kw #00E5FF
                    D2D1::ColorF(0.220f, 0.937f, 0.490f, 1.0f), // type #38EF7D
                    D2D1::ColorF(0.565f, 0.878f, 0.937f, 1.0f), // str #90E0EF
                    D2D1::ColorF(0.361f, 0.463f, 0.553f, 1.0f), // comment #5C768D
                    D2D1::ColorF(0.282f, 0.450f, 0.600f, 1.0f), // lineNum
                    D2D1::ColorF(0.055f, 0.165f, 0.278f, 1.0f)  // border
                );
                break;
            case ThemeMode::Turquoise:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.024f, 0.157f, 0.149f, 1.0f), // canvas #062826
                    D2D1::ColorF(0.008f, 0.106f, 0.102f, 1.0f), // header #021B1A
                    D2D1::ColorF(0.008f, 0.102f, 0.098f, 1.0f), // drawer #021A19
                    D2D1::ColorF(0.008f, 0.102f, 0.098f, 1.0f), // tabBar
                    D2D1::ColorF(0.024f, 0.157f, 0.149f, 1.0f), // tabActive
                    D2D1::ColorF(0.114f, 0.914f, 0.714f, 1.0f), // accent #1DE9B6
                    D2D1::ColorF(0.878f, 0.949f, 0.945f, 1.0f), // text #E0F2F1
                    D2D1::ColorF(0.114f, 0.914f, 0.714f, 1.0f), // kw #1DE9B6
                    D2D1::ColorF(0.392f, 1.0f, 0.855f, 1.0f),   // type #64FFDA
                    D2D1::ColorF(0.655f, 1.0f, 0.922f, 1.0f),   // str #A7FFEB
                    D2D1::ColorF(0.306f, 0.502f, 0.490f, 1.0f), // comment #4E807D
                    D2D1::ColorF(0.250f, 0.500f, 0.480f, 1.0f), // lineNum
                    D2D1::ColorF(0.043f, 0.235f, 0.224f, 1.0f)  // border
                );
                break;
            case ThemeMode::Monokai:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.153f, 0.157f, 0.133f, 1.0f), // canvas #272822
                    D2D1::ColorF(0.118f, 0.122f, 0.110f, 1.0f), // header #1E1F1C
                    D2D1::ColorF(0.098f, 0.102f, 0.090f, 1.0f), // drawer #191A17
                    D2D1::ColorF(0.098f, 0.102f, 0.090f, 1.0f), // tabBar
                    D2D1::ColorF(0.153f, 0.157f, 0.133f, 1.0f), // tabActive
                    D2D1::ColorF(0.976f, 0.149f, 0.447f, 1.0f), // accent #F92672
                    D2D1::ColorF(0.973f, 0.973f, 0.949f, 1.0f), // text #F8F8F2
                    D2D1::ColorF(0.976f, 0.149f, 0.447f, 1.0f), // kw #F92672
                    D2D1::ColorF(0.400f, 0.851f, 0.937f, 1.0f), // type #66D9EF
                    D2D1::ColorF(0.902f, 0.859f, 0.455f, 1.0f), // str #E6DB74
                    D2D1::ColorF(0.459f, 0.443f, 0.369f, 1.0f), // comment #75715E
                    D2D1::ColorF(0.550f, 0.530f, 0.440f, 1.0f), // lineNum
                    D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f)  // border
                );
                break;
            case ThemeMode::Dracula:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.157f, 0.165f, 0.212f, 1.0f), // canvas #282A36
                    D2D1::ColorF(0.129f, 0.133f, 0.173f, 1.0f), // header #21222C
                    D2D1::ColorF(0.118f, 0.122f, 0.161f, 1.0f), // drawer #1E1F29
                    D2D1::ColorF(0.118f, 0.122f, 0.161f, 1.0f), // tabBar
                    D2D1::ColorF(0.157f, 0.165f, 0.212f, 1.0f), // tabActive
                    D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f), // accent #BD93F9
                    D2D1::ColorF(0.973f, 0.973f, 0.949f, 1.0f), // text #F8F8F2
                    D2D1::ColorF(1.0f, 0.475f, 0.776f, 1.0f),   // kw #FF79C6
                    D2D1::ColorF(0.545f, 0.914f, 0.992f, 1.0f), // type #8BE9FD
                    D2D1::ColorF(0.945f, 0.980f, 0.549f, 1.0f), // str #F1FA8C
                    D2D1::ColorF(0.384f, 0.447f, 0.643f, 1.0f), // comment #6272A4
                    D2D1::ColorF(0.384f, 0.447f, 0.643f, 1.0f), // lineNum
                    D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f)  // border #44475A
                );
                break;
            case ThemeMode::Nord:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.180f, 0.204f, 0.251f, 1.0f), // canvas #2E3440
                    D2D1::ColorF(0.141f, 0.161f, 0.200f, 1.0f), // header #242933
                    D2D1::ColorF(0.141f, 0.161f, 0.200f, 1.0f), // drawer
                    D2D1::ColorF(0.141f, 0.161f, 0.200f, 1.0f), // tabBar
                    D2D1::ColorF(0.180f, 0.204f, 0.251f, 1.0f), // tabActive
                    D2D1::ColorF(0.533f, 0.753f, 0.816f, 1.0f), // accent #88C0D0
                    D2D1::ColorF(0.925f, 0.937f, 0.957f, 1.0f), // text #ECEFF4
                    D2D1::ColorF(0.506f, 0.631f, 0.757f, 1.0f), // kw #81A1C1
                    D2D1::ColorF(0.561f, 0.737f, 0.733f, 1.0f), // type #8FBCBB
                    D2D1::ColorF(0.639f, 0.745f, 0.549f, 1.0f), // str #A3BE8C
                    D2D1::ColorF(0.380f, 0.431f, 0.533f, 1.0f), // comment #616E88
                    D2D1::ColorF(0.380f, 0.431f, 0.533f, 1.0f), // lineNum
                    D2D1::ColorF(0.298f, 0.337f, 0.416f, 1.0f)  // border #4C566A
                );
                break;
            case ThemeMode::Solarized:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.000f, 0.169f, 0.212f, 1.0f), // canvas #002B36
                    D2D1::ColorF(0.027f, 0.212f, 0.259f, 1.0f), // header #073642
                    D2D1::ColorF(0.027f, 0.212f, 0.259f, 1.0f), // drawer
                    D2D1::ColorF(0.027f, 0.212f, 0.259f, 1.0f), // tabBar
                    D2D1::ColorF(0.000f, 0.169f, 0.212f, 1.0f), // tabActive
                    D2D1::ColorF(0.149f, 0.545f, 0.824f, 1.0f), // accent #268BD2
                    D2D1::ColorF(0.576f, 0.631f, 0.631f, 1.0f), // text #93A1A1
                    D2D1::ColorF(0.522f, 0.600f, 0.000f, 1.0f), // kw #859900
                    D2D1::ColorF(0.710f, 0.537f, 0.000f, 1.0f), // type #B58900
                    D2D1::ColorF(0.165f, 0.631f, 0.596f, 1.0f), // str #2AA198
                    D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f), // comment #586E75
                    D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f), // lineNum
                    D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f)  // border
                );
                break;
            case ThemeMode::GitHubDark:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.051f, 0.067f, 0.090f, 1.0f), // canvas #0D1117
                    D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f), // header #161B22
                    D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f), // drawer
                    D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f), // tabBar
                    D2D1::ColorF(0.051f, 0.067f, 0.090f, 1.0f), // tabActive
                    D2D1::ColorF(0.345f, 0.651f, 1.000f, 1.0f), // accent #58A6FF
                    D2D1::ColorF(0.788f, 0.820f, 0.851f, 1.0f), // text #C9D1D9
                    D2D1::ColorF(1.000f, 0.482f, 0.447f, 1.0f), // kw #FF7B72
                    D2D1::ColorF(1.000f, 0.651f, 0.341f, 1.0f), // type #FFA657
                    D2D1::ColorF(0.647f, 0.839f, 1.000f, 1.0f), // str #A5D6FF
                    D2D1::ColorF(0.545f, 0.580f, 0.620f, 1.0f), // comment #8B949E
                    D2D1::ColorF(0.282f, 0.314f, 0.349f, 1.0f), // lineNum
                    D2D1::ColorF(0.188f, 0.212f, 0.239f, 1.0f)  // border #30363D
                );
                break;
            case ThemeMode::OneDark:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.157f, 0.173f, 0.204f, 1.0f), // canvas #282C34
                    D2D1::ColorF(0.129f, 0.145f, 0.169f, 1.0f), // header #21252B
                    D2D1::ColorF(0.129f, 0.145f, 0.169f, 1.0f), // drawer
                    D2D1::ColorF(0.129f, 0.145f, 0.169f, 1.0f), // tabBar
                    D2D1::ColorF(0.157f, 0.173f, 0.204f, 1.0f), // tabActive
                    D2D1::ColorF(0.380f, 0.686f, 0.937f, 1.0f), // accent #61AFEF
                    D2D1::ColorF(0.671f, 0.698f, 0.749f, 1.0f), // text #ABB2BF
                    D2D1::ColorF(0.776f, 0.471f, 0.867f, 1.0f), // kw #C678DD
                    D2D1::ColorF(0.898f, 0.753f, 0.482f, 1.0f), // type #E5C07B
                    D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), // str #98C379
                    D2D1::ColorF(0.361f, 0.388f, 0.439f, 1.0f), // comment #5C6370
                    D2D1::ColorF(0.361f, 0.388f, 0.439f, 1.0f), // lineNum
                    D2D1::ColorF(0.243f, 0.271f, 0.318f, 1.0f)  // border #3E4451
                );
                break;
            case ThemeMode::SynthWave:
                DrawGenericPreview(pRT, config, rcBox,
                    D2D1::ColorF(0.149f, 0.137f, 0.208f, 1.0f), // canvas #262335
                    D2D1::ColorF(0.141f, 0.106f, 0.184f, 1.0f), // header #241B2F
                    D2D1::ColorF(0.141f, 0.106f, 0.184f, 1.0f), // drawer
                    D2D1::ColorF(0.141f, 0.106f, 0.184f, 1.0f), // tabBar
                    D2D1::ColorF(0.149f, 0.137f, 0.208f, 1.0f), // tabActive
                    D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f), // accent #FF7EDB
                    D2D1::ColorF(0.976f, 0.957f, 0.988f, 1.0f), // text
                    D2D1::ColorF(0.996f, 0.871f, 0.365f, 1.0f), // kw #FEDE5D Neon Gold
                    D2D1::ColorF(0.212f, 0.976f, 0.965f, 1.0f), // type #36F9F6 Neon Cyan
                    D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f), // str #FF7EDB Neon Pink
                    D2D1::ColorF(0.518f, 0.545f, 0.596f, 1.0f), // comment #848B98
                    D2D1::ColorF(0.518f, 0.545f, 0.596f, 1.0f), // lineNum
                    D2D1::ColorF(0.204f, 0.161f, 0.310f, 1.0f)  // border #34294F
                );
                break;
        }
    };

    // Close button (×)
    m_closeBounds = D2D1::RectF(
        modalX + modalW - config.ScaleF(36.0f),
        modalY + config.ScaleF(16.0f),
        modalX + modalW - config.ScaleF(12.0f),
        modalY + config.ScaleF(40.0f)
    );
    if (m_hoveredClose) {
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_closeBounds, 4.0f, 4.0f), brushCardHover.Get());
    }
    float cx = (m_closeBounds.left + m_closeBounds.right) / 2.0f;
    float cy = (m_closeBounds.top + m_closeBounds.bottom) / 2.0f;
    float r = config.ScaleF(4.0f);
    pRT->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), brushModalMuted.Get(), 1.4f);
    pRT->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx - r, cy + r), brushModalMuted.Get(), 1.4f);

    if (!m_showAllCatalog) {
        // Standard 8-Theme View with Wide Widescreen Preview Cards
        D2D1_RECT_F rcTitle = D2D1::RectF(
            modalX + config.ScaleF(24.0f),
            modalY + config.ScaleF(16.0f),
            modalX + modalW - config.ScaleF(60.0f),
            modalY + config.ScaleF(36.0f)
        );
        const wchar_t* titleStr = L"Color Theme";
        pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                      theme.fontButtonCenter.Get(), rcTitle, brushModalTitle.Get());

        D2D1_RECT_F rcSubtitle = D2D1::RectF(
            modalX + config.ScaleF(24.0f),
            modalY + config.ScaleF(38.0f),
            modalX + modalW - config.ScaleF(60.0f),
            modalY + config.ScaleF(56.0f)
        );
        const wchar_t* subStr = L"Choose how phide looks to you";
        pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)),
                      theme.fontMenuShortcut.Get(), rcSubtitle, brushModalSub.Get());

        // 8 Theme Cards arranged in 4 columns x 2 rows
        const float cardW = config.ScaleF(216.0f);
        const float cardH = config.ScaleF(142.0f);
        const float gapX  = config.ScaleF(16.0f);
        const float gapY  = config.ScaleF(14.0f);
        const float row1Y = modalY + config.ScaleF(64.0f);
        const float row2Y = row1Y + cardH + gapY;
        const float startX = modalX + config.ScaleF(24.0f);

        struct CardMeta {
            ThemeMode mode;
            const wchar_t* label;
            float x;
            float y;
        };
        CardMeta metas[8] = {
            { ThemeMode::Dark,      L"Dark Mode",        startX + 0 * (cardW + gapX), row1Y },
            { ThemeMode::Light,     L"Light Mode",       startX + 1 * (cardW + gapX), row1Y },
            { ThemeMode::Auto,      L"Auto (System)",    startX + 2 * (cardW + gapX), row1Y },
            { ThemeMode::Purple,    L"Deep Purple",      startX + 3 * (cardW + gapX), row1Y },
            { ThemeMode::Cyan,      L"Cyan Cyber",       startX + 0 * (cardW + gapX), row2Y },
            { ThemeMode::Turquoise, L"Turquoise Teal",   startX + 1 * (cardW + gapX), row2Y },
            { ThemeMode::Monokai,   L"Monokai Pro",      startX + 2 * (cardW + gapX), row2Y },
            { ThemeMode::Dracula,   L"Dracula",          startX + 3 * (cardW + gapX), row2Y },
        };

        for (int i = 0; i < 8; ++i) {
            float x = metas[i].x;
            float y = metas[i].y;
            m_cardBounds[i] = D2D1::RectF(x, y, x + cardW, y + cardH);

            bool isSelected = (m_selectedMode == metas[i].mode);
            bool isHovered = (m_hoveredCard == i);

            // Card Container
            D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_cardBounds[i], 6.0f, 6.0f);
            pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());

            if (isSelected) {
                pRT->DrawRoundedRectangle(rrcCard, brushCardSelect.Get(), 2.0f);
            } else if (isHovered) {
                pRT->DrawRoundedRectangle(rrcCard, brushCardHover.Get(), 1.5f);
            } else {
                pRT->DrawRoundedRectangle(rrcCard, brushCardBorder.Get(), 1.0f);
            }

            // Top Widescreen Preview Box (ratio ~2.2:1 widescreen)
            const float padBox = config.ScaleF(6.0f);
            const float previewH = config.ScaleF(92.0f);
            m_previewBoxBounds[i] = D2D1::RectF(
                x + padBox,
                y + padBox,
                x + cardW - padBox,
                y + padBox + previewH
            );

            D2D1_ROUNDED_RECT rrcPreview = D2D1::RoundedRect(m_previewBoxBounds[i], 4.0f, 4.0f);
            pRT->PushAxisAlignedClip(m_previewBoxBounds[i], D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            DrawModePreview(metas[i].mode, m_previewBoxBounds[i]);
            pRT->PopAxisAlignedClip();
            pRT->DrawRoundedRectangle(rrcPreview, brushCardBorder.Get(), 1.0f);

            // Bottom: Radio Circle + Label
            float bottomAreaY = m_previewBoxBounds[i].bottom + config.ScaleF(18.0f);
            float radioX = x + config.ScaleF(14.0f);
            float radioRadius = config.ScaleF(5.5f);

            D2D1_ELLIPSE elOuter = D2D1::Ellipse(D2D1::Point2F(radioX, bottomAreaY), radioRadius, radioRadius);
            pRT->DrawEllipse(elOuter, isSelected ? brushCardSelect.Get() : brushModalMuted.Get(), 1.5f);

            if (isSelected) {
                D2D1_ELLIPSE elInner = D2D1::Ellipse(D2D1::Point2F(radioX, bottomAreaY), radioRadius - 2.2f, radioRadius - 2.2f);
                pRT->FillEllipse(elInner, brushCardSelect.Get());
            }

            D2D1_RECT_F rcLabel = D2D1::RectF(
                radioX + radioRadius + config.ScaleF(6.0f),
                bottomAreaY - config.ScaleF(9.0f),
                x + cardW - config.ScaleF(4.0f),
                bottomAreaY + config.ScaleF(11.0f)
            );
            pRT->DrawText(metas[i].label, static_cast<UINT32>(wcslen(metas[i].label)),
                          theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(), rcLabel,
                          isSelected ? brushLabelSelected.Get() : brushLabelNormal.Get());
        }

        // Bottom Right "More Themes... ▸" Button
        const float btnW = config.ScaleF(148.0f);
        const float btnH = config.ScaleF(28.0f);
        m_moreThemesBounds = D2D1::RectF(
            modalX + modalW - config.ScaleF(24.0f) - btnW,
            modalY + modalH - config.ScaleF(14.0f) - btnH,
            modalX + modalW - config.ScaleF(24.0f),
            modalY + modalH - config.ScaleF(14.0f)
        );
        D2D1_ROUNDED_RECT rrcMoreBtn = D2D1::RoundedRect(m_moreThemesBounds, 4.0f, 4.0f);
        if (m_hoveredMoreThemes) {
            pRT->FillRoundedRectangle(rrcMoreBtn, brushCardHover.Get());
        } else {
            pRT->FillRoundedRectangle(rrcMoreBtn, brushBtnBg.Get());
        }
        pRT->DrawRoundedRectangle(rrcMoreBtn, brushCardBorder.Get(), 1.0f);
        const wchar_t* moreText = L"More Themes... ▸";
        pRT->DrawText(moreText, static_cast<UINT32>(wcslen(moreText)),
                      theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                      m_moreThemesBounds, brushModalTitle.Get());
    } else {
        // Big Catalog Pop-up Modal View with All 13 Themes
        // Back Button
        m_backButtonBounds = D2D1::RectF(
            modalX + config.ScaleF(24.0f),
            modalY + config.ScaleF(16.0f),
            modalX + config.ScaleF(180.0f),
            modalY + config.ScaleF(44.0f)
        );
        D2D1_ROUNDED_RECT rrcBackBtn = D2D1::RoundedRect(m_backButtonBounds, 4.0f, 4.0f);
        if (m_hoveredBack) {
            pRT->FillRoundedRectangle(rrcBackBtn, brushCardHover.Get());
        } else {
            pRT->FillRoundedRectangle(rrcBackBtn, brushBtnBg.Get());
        }
        pRT->DrawRoundedRectangle(rrcBackBtn, brushCardBorder.Get(), 1.0f);
        const wchar_t* backText = L"◂ Standard Themes";
        pRT->DrawText(backText, static_cast<UINT32>(wcslen(backText)),
                      theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                      m_backButtonBounds, brushModalTitle.Get());

        // Catalog Title & Subtitle
        D2D1_RECT_F rcCatTitle = D2D1::RectF(
            modalX + config.ScaleF(196.0f),
            modalY + config.ScaleF(14.0f),
            modalX + modalW - config.ScaleF(60.0f),
            modalY + config.ScaleF(34.0f)
        );
        const wchar_t* catTitleStr = L"Color Theme Catalog (13 Themes)";
        pRT->DrawText(catTitleStr, static_cast<UINT32>(wcslen(catTitleStr)),
                      theme.fontButtonCenter.Get(), rcCatTitle, brushModalTitle.Get());

        D2D1_RECT_F rcCatSubtitle = D2D1::RectF(
            modalX + config.ScaleF(196.0f),
            modalY + config.ScaleF(36.0f),
            modalX + modalW - config.ScaleF(60.0f),
            modalY + config.ScaleF(54.0f)
        );
        const wchar_t* catSubStr = L"Select from all 13 built-in themes and color schemes";
        pRT->DrawText(catSubStr, static_cast<UINT32>(wcslen(catSubStr)),
                      theme.fontMenuShortcut.Get(), rcCatSubtitle, brushModalSub.Get());

        // Catalog Clip Area
        D2D1_RECT_F rcCatalogClip = D2D1::RectF(
            modalX + config.ScaleF(16.0f),
            modalY + config.ScaleF(62.0f),
            modalX + modalW - config.ScaleF(16.0f),
            modalY + modalH - config.ScaleF(16.0f)
        );
        pRT->PushAxisAlignedClip(rcCatalogClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        struct CatalogMeta {
            ThemeMode mode;
            const wchar_t* label;
        };
        CatalogMeta catalog[13] = {
            { ThemeMode::Dark,       L"Dark Mode" },
            { ThemeMode::Light,      L"Light Mode" },
            { ThemeMode::Auto,       L"Auto (System)" },
            { ThemeMode::Purple,     L"Deep Purple" },
            { ThemeMode::Cyan,       L"Cyan Cyber" },
            { ThemeMode::Turquoise,  L"Turquoise Teal" },
            { ThemeMode::Monokai,    L"Monokai Pro" },
            { ThemeMode::Dracula,    L"Dracula" },
            { ThemeMode::Nord,       L"Nord Arctic" },
            { ThemeMode::Solarized,  L"Solarized Dark" },
            { ThemeMode::GitHubDark, L"GitHub Dark" },
            { ThemeMode::OneDark,    L"One Dark Pro" },
            { ThemeMode::SynthWave,  L"SynthWave '84" },
        };

        const float catCardW = config.ScaleF(216.0f);
        const float catCardH = config.ScaleF(142.0f);
        const float catGapX  = config.ScaleF(16.0f);
        const float catGapY  = config.ScaleF(14.0f);
        const float catStartX = modalX + config.ScaleF(24.0f);

        for (int i = 0; i < 13; ++i) {
            int col = i % 4;
            int row = i / 4;
            float x = catStartX + col * (catCardW + catGapX);
            float y = rcCatalogClip.top + config.ScaleF(8.0f) + m_catalogScrollY + row * (catCardH + catGapY);
            m_cardBounds[i] = D2D1::RectF(x, y, x + catCardW, y + catCardH);

            bool isSelected = (m_selectedMode == catalog[i].mode);
            bool isHovered = (m_hoveredCard == i);

            D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_cardBounds[i], 6.0f, 6.0f);
            pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());

            if (isSelected) {
                pRT->DrawRoundedRectangle(rrcCard, brushCardSelect.Get(), 2.0f);
            } else if (isHovered) {
                pRT->DrawRoundedRectangle(rrcCard, brushCardHover.Get(), 1.5f);
            } else {
                pRT->DrawRoundedRectangle(rrcCard, brushCardBorder.Get(), 1.0f);
            }

            const float padBox = config.ScaleF(6.0f);
            const float previewH = config.ScaleF(92.0f);
            m_previewBoxBounds[i] = D2D1::RectF(
                x + padBox,
                y + padBox,
                x + catCardW - padBox,
                y + padBox + previewH
            );

            D2D1_ROUNDED_RECT rrcPreview = D2D1::RoundedRect(m_previewBoxBounds[i], 4.0f, 4.0f);
            pRT->PushAxisAlignedClip(m_previewBoxBounds[i], D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            DrawModePreview(catalog[i].mode, m_previewBoxBounds[i]);
            pRT->PopAxisAlignedClip();
            pRT->DrawRoundedRectangle(rrcPreview, brushCardBorder.Get(), 1.0f);

            // Bottom: Radio Circle + Label
            float bottomAreaY = m_previewBoxBounds[i].bottom + config.ScaleF(18.0f);
            float radioX = x + config.ScaleF(14.0f);
            float radioRadius = config.ScaleF(5.5f);

            D2D1_ELLIPSE elOuter = D2D1::Ellipse(D2D1::Point2F(radioX, bottomAreaY), radioRadius, radioRadius);
            pRT->DrawEllipse(elOuter, isSelected ? brushCardSelect.Get() : brushModalMuted.Get(), 1.5f);

            if (isSelected) {
                D2D1_ELLIPSE elInner = D2D1::Ellipse(D2D1::Point2F(radioX, bottomAreaY), radioRadius - 2.2f, radioRadius - 2.2f);
                pRT->FillEllipse(elInner, brushCardSelect.Get());
            }

            D2D1_RECT_F rcLabel = D2D1::RectF(
                radioX + radioRadius + config.ScaleF(6.0f),
                bottomAreaY - config.ScaleF(9.0f),
                x + catCardW - config.ScaleF(4.0f),
                bottomAreaY + config.ScaleF(11.0f)
            );
            pRT->DrawText(catalog[i].label, static_cast<UINT32>(wcslen(catalog[i].label)),
                          theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(), rcLabel,
                          isSelected ? brushLabelSelected.Get() : brushLabelNormal.Get());
        }

        pRT->PopAxisAlignedClip();

        // Optional scrollbar indicator
        const float totalContentH = config.ScaleF(4.0f * (142.0f + 14.0f) + 20.0f);
        const float clipH = rcCatalogClip.bottom - rcCatalogClip.top;
        if (totalContentH > clipH && clipH > 50.0f) {
            float thumbRatio = clipH / totalContentH;
            float thumbH = (std::max)(config.ScaleF(24.0f), clipH * thumbRatio);
            float maxScroll = totalContentH - clipH;
            float scrollFraction = (-m_catalogScrollY) / (maxScroll > 1.0f ? maxScroll : 1.0f);
            float thumbY = rcCatalogClip.top + scrollFraction * (clipH - thumbH);
            D2D1_RECT_F rcThumb = D2D1::RectF(
                rcCatalogClip.right - config.ScaleF(6.0f),
                thumbY,
                rcCatalogClip.right - config.ScaleF(2.0f),
                thumbY + thumbH
            );
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcThumb, 2.0f, 2.0f), brushCardHover.Get());
        }
    }
}

bool ThemePickerModal::HandleMouseWheel(short delta) {
    if (!m_isOpen || !m_showAllCatalog) return false;
    const float scrollStep = 50.0f;
    const float totalContentH = 4.0f * (142.0f + 14.0f) + 20.0f;
    const float clipH = m_modalBounds.bottom - m_modalBounds.top - 80.0f;
    float maxScroll = (std::max)(0.0f, totalContentH - clipH);
    if (delta > 0) {
        m_catalogScrollY = (std::min)(0.0f, m_catalogScrollY + scrollStep);
    } else {
        m_catalogScrollY = (std::max)(-maxScroll, m_catalogScrollY - scrollStep);
    }
    return true;
}

bool ThemePickerModal::HandleMouseMove(POINT clientPt) {
    if (!m_isOpen) return false;

    int oldHover = m_hoveredCard;
    bool oldClose = m_hoveredClose;
    bool oldMore = m_hoveredMoreThemes;
    bool oldBack = m_hoveredBack;

    m_hoveredCard = -1;
    m_hoveredClose = (clientPt.x >= m_closeBounds.left && clientPt.x <= m_closeBounds.right &&
                      clientPt.y >= m_closeBounds.top && clientPt.y <= m_closeBounds.bottom);

    if (!m_showAllCatalog) {
        m_hoveredBack = false;
        m_hoveredMoreThemes = (clientPt.x >= m_moreThemesBounds.left && clientPt.x <= m_moreThemesBounds.right &&
                               clientPt.y >= m_moreThemesBounds.top && clientPt.y <= m_moreThemesBounds.bottom);

        for (int i = 0; i < 8; ++i) {
            if (clientPt.x >= m_cardBounds[i].left && clientPt.x <= m_cardBounds[i].right &&
                clientPt.y >= m_cardBounds[i].top && clientPt.y <= m_cardBounds[i].bottom) {
                m_hoveredCard = i;
                break;
            }
        }
    } else {
        m_hoveredMoreThemes = false;
        m_hoveredBack = (clientPt.x >= m_backButtonBounds.left && clientPt.x <= m_backButtonBounds.right &&
                         clientPt.y >= m_backButtonBounds.top && clientPt.y <= m_backButtonBounds.bottom);

        D2D1_RECT_F rcCatalogClip = D2D1::RectF(
            m_modalBounds.left + 16.0f,
            m_modalBounds.top + 62.0f,
            m_modalBounds.right - 16.0f,
            m_modalBounds.bottom - 16.0f
        );

        if (clientPt.x >= rcCatalogClip.left && clientPt.x <= rcCatalogClip.right &&
            clientPt.y >= rcCatalogClip.top && clientPt.y <= rcCatalogClip.bottom) {
            for (int i = 0; i < 13; ++i) {
                if (clientPt.x >= m_cardBounds[i].left && clientPt.x <= m_cardBounds[i].right &&
                    clientPt.y >= m_cardBounds[i].top && clientPt.y <= m_cardBounds[i].bottom) {
                    m_hoveredCard = i;
                    break;
                }
            }
        }
    }

    return (m_hoveredCard != oldHover || m_hoveredClose != oldClose ||
            m_hoveredMoreThemes != oldMore || m_hoveredBack != oldBack);
}

bool ThemePickerModal::HandleLButtonDown(POINT clientPt, ThemeMode& outChosenMode) {
    if (!m_isOpen) return false;

    // Click on Close button (X) dismisses the dialog without altering the active theme
    if (clientPt.x >= m_closeBounds.left && clientPt.x <= m_closeBounds.right &&
        clientPt.y >= m_closeBounds.top && clientPt.y <= m_closeBounds.bottom) {
        Close();
        return false;
    }

    if (!m_showAllCatalog) {
        // Click on "More Themes... ▸" button switches to big catalog view
        if (clientPt.x >= m_moreThemesBounds.left && clientPt.x <= m_moreThemesBounds.right &&
            clientPt.y >= m_moreThemesBounds.top && clientPt.y <= m_moreThemesBounds.bottom) {
            m_showAllCatalog = true;
            m_hoveredCard = -1;
            m_hoveredMoreThemes = false;
            m_catalogScrollY = 0.0f;
            return false;
        }

        ThemeMode modes[8] = {
            ThemeMode::Dark,
            ThemeMode::Light,
            ThemeMode::Auto,
            ThemeMode::Purple,
            ThemeMode::Cyan,
            ThemeMode::Turquoise,
            ThemeMode::Monokai,
            ThemeMode::Dracula
        };

        // Click on one of the 8 theme preview cards
        for (int i = 0; i < 8; ++i) {
            if (clientPt.x >= m_cardBounds[i].left && clientPt.x <= m_cardBounds[i].right &&
                clientPt.y >= m_cardBounds[i].top && clientPt.y <= m_cardBounds[i].bottom) {
                outChosenMode = modes[i];
                m_selectedMode = outChosenMode;
                Close();
                return true;
            }
        }
    } else {
        // Click on "◂ Standard Themes" back button switches back to standard 8-theme view
        if (clientPt.x >= m_backButtonBounds.left && clientPt.x <= m_backButtonBounds.right &&
            clientPt.y >= m_backButtonBounds.top && clientPt.y <= m_backButtonBounds.bottom) {
            m_showAllCatalog = false;
            m_hoveredCard = -1;
            m_hoveredBack = false;
            return false;
        }

        ThemeMode catModes[13] = {
            ThemeMode::Dark,
            ThemeMode::Light,
            ThemeMode::Auto,
            ThemeMode::Purple,
            ThemeMode::Cyan,
            ThemeMode::Turquoise,
            ThemeMode::Monokai,
            ThemeMode::Dracula,
            ThemeMode::Nord,
            ThemeMode::Solarized,
            ThemeMode::GitHubDark,
            ThemeMode::OneDark,
            ThemeMode::SynthWave
        };

        D2D1_RECT_F rcCatalogClip = D2D1::RectF(
            m_modalBounds.left + 16.0f,
            m_modalBounds.top + 62.0f,
            m_modalBounds.right - 16.0f,
            m_modalBounds.bottom - 16.0f
        );

        if (clientPt.x >= rcCatalogClip.left && clientPt.x <= rcCatalogClip.right &&
            clientPt.y >= rcCatalogClip.top && clientPt.y <= rcCatalogClip.bottom) {
            for (int i = 0; i < 13; ++i) {
                if (clientPt.x >= m_cardBounds[i].left && clientPt.x <= m_cardBounds[i].right &&
                    clientPt.y >= m_cardBounds[i].top && clientPt.y <= m_cardBounds[i].bottom) {
                    outChosenMode = catModes[i];
                    m_selectedMode = outChosenMode;
                    Close();
                    return true;
                }
            }
        }
    }

    // Click outside modal bounds dismisses dialog without altering the active theme
    if (clientPt.x < m_modalBounds.left || clientPt.x > m_modalBounds.right ||
        clientPt.y < m_modalBounds.top || clientPt.y > m_modalBounds.bottom) {
        Close();
        return false;
    }

    return false;
}

} // namespace phide::ui
