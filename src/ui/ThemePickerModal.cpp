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

    // 2. Centered Modal Box (740px x 510px) - Large High-Fidelity Dialog
    const float modalW = config.ScaleF(740.0f);
    const float modalH = config.ScaleF(510.0f);
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
    }

    // Modal Background and Border
    pRT->FillRoundedRectangle(rrcModal, brushModalBg.Get());
    pRT->DrawRoundedRectangle(rrcModal, brushModalBorder.Get(), 1.0f);

    // 3. Header: "Color Theme" and Close (×) Button
    D2D1_RECT_F rcTitle = D2D1::RectF(
        modalX + config.ScaleF(28.0f),
        modalY + config.ScaleF(18.0f),
        modalX + modalW - config.ScaleF(60.0f),
        modalY + config.ScaleF(40.0f)
    );
    const wchar_t* titleStr = L"Color Theme";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  theme.fontButtonCenter.Get(), rcTitle, brushModalTitle.Get());

    D2D1_RECT_F rcSubtitle = D2D1::RectF(
        modalX + config.ScaleF(28.0f),
        modalY + config.ScaleF(42.0f),
        modalX + modalW - config.ScaleF(60.0f),
        modalY + config.ScaleF(60.0f)
    );
    const wchar_t* subStr = L"Choose how phide looks to you";
    pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)),
                  theme.fontMenuShortcut.Get(), rcSubtitle, brushModalSub.Get());

    // Close button (×)
    const float closeSize = config.ScaleF(24.0f);
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

    // 4. Large Theme Cards (2 on Top Row + 1 Centered on Bottom Row)
    const float cardW = config.ScaleF(330.0f);
    const float cardH = config.ScaleF(196.0f);
    const float gapX  = config.ScaleF(26.0f);
    const float row1Y = modalY + config.ScaleF(68.0f);
    const float row2Y = row1Y + cardH + config.ScaleF(16.0f);

    struct CardMeta {
        ThemeMode mode;
        const wchar_t* label;
        float x;
        float y;
    };
    CardMeta metas[3] = {
        { ThemeMode::Dark,  L"Dark Mode",            modalX + config.ScaleF(27.0f),                row1Y },
        { ThemeMode::Light, L"Light Mode",           modalX + config.ScaleF(27.0f) + cardW + gapX, row1Y },
        { ThemeMode::Auto,  L"Auto (System Theme)",  modalX + (modalW - cardW) / 2.0f,            row2Y }
    };

    for (int i = 0; i < 3; ++i) {
        float x = metas[i].x;
        float y = metas[i].y;
        m_cardBounds[i] = D2D1::RectF(x, y, x + cardW, y + cardH);

        bool isSelected = (m_selectedMode == metas[i].mode);
        bool isHovered = (m_hoveredCard == i);

        // Card Container (#181818)
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_cardBounds[i], 6.0f, 6.0f);
        pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());

        if (isSelected) {
            pRT->DrawRoundedRectangle(rrcCard, brushCardSelect.Get(), 2.0f);
        } else if (isHovered) {
            pRT->DrawRoundedRectangle(rrcCard, brushCardHover.Get(), 1.5f);
        } else {
            pRT->DrawRoundedRectangle(rrcCard, brushCardBorder.Get(), 1.0f);
        }

        // Top Preview Box (314px x 142px) - BIG Detailed Preview!
        const float padBox = config.ScaleF(8.0f);
        const float previewH = config.ScaleF(142.0f);
        m_previewBoxBounds[i] = D2D1::RectF(
            x + padBox,
            y + padBox,
            x + cardW - padBox,
            y + padBox + previewH
        );

        D2D1_ROUNDED_RECT rrcPreview = D2D1::RoundedRect(m_previewBoxBounds[i], 4.0f, 4.0f);
        pRT->PushAxisAlignedClip(m_previewBoxBounds[i], D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (i == 0) {
            DrawDarkPreview(pRT, config, m_previewBoxBounds[i]);
        } else if (i == 1) {
            DrawLightPreview(pRT, config, m_previewBoxBounds[i]);
        } else {
            DrawAutoSplitPreview(pRT, config, m_previewBoxBounds[i]);
        }

        pRT->PopAxisAlignedClip();
        pRT->DrawRoundedRectangle(rrcPreview, brushCardBorder.Get(), 1.0f);

        // Bottom: Radio Circle + Label
        float bottomAreaY = m_previewBoxBounds[i].bottom + config.ScaleF(21.0f);
        float radioX = x + config.ScaleF(16.0f);
        float radioRadius = config.ScaleF(6.0f);

        // Radio circle outline
        D2D1_ELLIPSE elOuter = D2D1::Ellipse(D2D1::Point2F(radioX, bottomAreaY), radioRadius, radioRadius);
        pRT->DrawEllipse(elOuter, isSelected ? brushCardSelect.Get() : brushModalMuted.Get(), 1.5f);

        // Radio center dot if selected
        if (isSelected) {
            D2D1_ELLIPSE elInner = D2D1::Ellipse(D2D1::Point2F(radioX, bottomAreaY), radioRadius - 2.5f, radioRadius - 2.5f);
            pRT->FillEllipse(elInner, brushCardSelect.Get());
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
                      isSelected ? brushLabelSelected.Get() : brushLabelNormal.Get());
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

/**
 * @brief Handles left mouse button click on theme cards or the Close (×) button.
 * @param clientPt Mouse cursor position in client coordinates.
 * @param outChosenMode Populated with the newly chosen ThemeMode ONLY when a card is selected.
 * @return True if a theme card was selected (theme should change), or false if dismissed/closed without changing theme.
 */
bool ThemePickerModal::HandleLButtonDown(POINT clientPt, ThemeMode& outChosenMode) {
    if (!m_isOpen) return false;

    // Click on Close button (X) dismisses the dialog without altering the active theme
    if (clientPt.x >= m_closeBounds.left && clientPt.x <= m_closeBounds.right &&
        clientPt.y >= m_closeBounds.top && clientPt.y <= m_closeBounds.bottom) {
        Close();
        return false;
    }

    // Click on one of the theme preview cards
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

    // Click outside modal bounds dismisses dialog without altering the active theme
    if (clientPt.x < m_modalBounds.left || clientPt.x > m_modalBounds.right ||
        clientPt.y < m_modalBounds.top || clientPt.y > m_modalBounds.bottom) {
        Close();
        return false;
    }

    return false;
}

} // namespace phide::ui
