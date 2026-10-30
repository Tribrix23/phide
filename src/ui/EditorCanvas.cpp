/**
 * @file EditorCanvas.cpp
 * @brief Implementation of dark workspace surface with modern rounded container aesthetics.
 */

#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/WatermarkLogo.hpp"
#include "phide/ui/ColorPickerModal.hpp"
#include <algorithm>
#include <wincodec.h>
#include <filesystem>
#include <regex>

namespace phide::ui {

static void FillUpperLeftRoundedRect(ID2D1RenderTarget* pRT, ID2D1Brush* pBrush,
                                     const D2D1_RECT_F& rc, float radius) {
    if (!pRT || !pBrush) return;
    if (radius <= 0.0f) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (!pFactory) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
    if (FAILED(pFactory->CreatePathGeometry(&pPath))) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(pPath->Open(&sink))) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }

    const float maxR = (std::min)(rc.right - rc.left, rc.bottom - rc.top);
    const float r = (maxR > 0.0f) ? (std::min)(radius, maxR) : 0.0f;

    sink->BeginFigure(D2D1::Point2F(rc.left + r, rc.top), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(rc.right, rc.top));
    sink->AddLine(D2D1::Point2F(rc.right, rc.bottom));
    sink->AddLine(D2D1::Point2F(rc.left, rc.bottom));
    sink->AddLine(D2D1::Point2F(rc.left, rc.top + r));

    if (r > 0.0f) {
        D2D1_ARC_SEGMENT arc{};
        arc.point = D2D1::Point2F(rc.left + r, rc.top);
        arc.size = D2D1::SizeF(r, r);
        arc.rotationAngle = 0.0f;
        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        arc.arcSize = D2D1_ARC_SIZE_SMALL;
        sink->AddArc(arc);
    }

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    pRT->FillGeometry(pPath.Get(), pBrush);
}

static void DrawKeycap(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       const wchar_t* text, float& curRightX, float centerY, float capH) {
    const size_t len = wcslen(text);
    const float capW = config.ScaleF(static_cast<float>(len * 7.5f + 14.0f));
    const float x = curRightX - capW;
    const float y = centerY - capH / 2.0f;

    D2D1_RECT_F rc = D2D1::RectF(x, y, curRightX, y + capH);
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, 4.0f, 4.0f);

    pRT->FillRoundedRectangle(rrc, theme.brushCaptionHover.Get());
    pRT->DrawRoundedRectangle(rrc, theme.brushBorderSubtle.Get(), 1.0f);

    IDWriteTextFormat* pFmt = theme.fontKeycap ? theme.fontKeycap.Get() : theme.fontMenu.Get();
    pRT->DrawText(text, static_cast<UINT32>(len), pFmt, rc, theme.brushTextPrimary.Get());

    curRightX = x - config.ScaleF(4.0f);
}

static void DrawPlus(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                     float& curRightX, float centerY, float capH) {
    const float plusW = config.ScaleF(12.0f);
    const float x = curRightX - plusW;
    const float y = centerY - capH / 2.0f;

    D2D1_RECT_F rc = D2D1::RectF(x, y, curRightX, y + capH);
    IDWriteTextFormat* pFmt = theme.fontKeycap ? theme.fontKeycap.Get() : theme.fontMenu.Get();
    pRT->DrawText(L"+", 1, pFmt, rc, theme.brushTextMuted.Get());

    curRightX = x - config.ScaleF(4.0f);
}

EditorCanvas::EditorCanvas() = default;

void EditorCanvas::DiscardDeviceResources() {
    m_pWatermarkBitmap.Reset();
    m_pWatermarkBitmapLight.Reset();
    m_pCanvasGeometry.Reset();
    m_learningHubBitmaps.clear();
    m_charWidth = 0.0f;
    m_cachedLeft = -1.0f;
    m_cachedTop = -1.0f;
    m_cachedRight = -1.0f;
    m_cachedBottom = -1.0f;
}

void EditorCanvas::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          std::vector<editor::EditorGroup>& groups, int focusedGroupIdx,
                          float canvasLeft, float canvasWidth, float clientHeight,
                          bool hasLeftDrawer,
                          const std::wstring& highlightQuery,
                          bool highlightMatchCase,
                          bool highlightWholeWord,
                          bool highlightUseRegex,
                          bool isRenaming) {
    if (!pRT) return;
    if (groups.empty()) {
        groups.push_back(editor::EditorGroup{});
    }

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float canvasRight = canvasLeft + canvasWidth;
    const float cornerRadius = hasLeftDrawer ? 0.0f : config.ScaleF(14.0f);

    // Check if cached geometry needs rebuilding (e.g. on resize, layout changes, or drawer open/close)
    if (!m_pCanvasGeometry ||
        m_cachedLeft != canvasLeft ||
        m_cachedTop != headerH ||
        m_cachedRight != canvasRight ||
        m_cachedBottom != clientHeight ||
        m_cachedHasLeftDrawer != hasLeftDrawer)
    {
        m_cachedLeft          = canvasLeft;
        m_cachedTop           = headerH;
        m_cachedRight         = canvasRight;
        m_cachedBottom        = clientHeight;
        m_cachedHasLeftDrawer = hasLeftDrawer;
        m_pCanvasGeometry.Reset();

        Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
        pRT->GetFactory(&pFactory);
        if (pFactory) {
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
            if (SUCCEEDED(pFactory->CreatePathGeometry(&pPath))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pPath->Open(&sink))) {
                    const float maxR = (std::min)(canvasWidth / 2.0f, (clientHeight - headerH) / 2.0f);
                    const float r = (maxR > 0.0f) ? (std::min)(cornerRadius, maxR) : 0.0f;

                    sink->BeginFigure(D2D1::Point2F(canvasLeft + r, headerH), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(canvasRight, headerH));
                    sink->AddLine(D2D1::Point2F(canvasRight, clientHeight));
                    sink->AddLine(D2D1::Point2F(canvasLeft, clientHeight));
                    sink->AddLine(D2D1::Point2F(canvasLeft, headerH + r));

                    if (r > 0.0f) {
                        D2D1_ARC_SEGMENT arc = {};
                        arc.point = D2D1::Point2F(canvasLeft + r, headerH);
                        arc.size = D2D1::SizeF(r, r);
                        arc.rotationAngle = 0.0f;
                        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                        arc.arcSize = D2D1_ARC_SIZE_SMALL;
                        sink->AddArc(arc);
                    }

                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();

                    m_pCanvasGeometry = pPath;
                }
            }
        }
    }

    // 1. Fill dark workspace surface (#1E1E1E) with geometry curved ONLY at upper-left
    if (m_pCanvasGeometry) {
        pRT->FillGeometry(m_pCanvasGeometry.Get(), theme.brushCanvasBg.Get());
    } else {
        D2D1_RECT_F rcFallback = D2D1::RectF(canvasLeft, headerH, canvasRight, clientHeight);
        pRT->FillRectangle(rcFallback, theme.brushCanvasBg.Get());
    }

    // 1b. Render ambient theme visual effects (3D neon perspective grid, stars, matrix rain) across canvas
    RenderCanvasThemeEffects(pRT, theme, config, canvasLeft, canvasWidth, headerH + config.ScaleF(34.0f), clientHeight);

    size_t numGroups = groups.size();
    const float paneGap = 1.0f;
    const float totalGaps = (numGroups > 1) ? static_cast<float>(numGroups - 1) * paneGap : 0.0f;
    const float paneWidth = (canvasWidth - totalGaps) / static_cast<float>(numGroups);

    for (size_t g = 0; g < numGroups; ++g) {
        float pLeft = canvasLeft + g * (paneWidth + paneGap);
        float pWidth = (g == numGroups - 1) ? (canvasRight - pLeft) : paneWidth;
        groups[g].bounds = D2D1::RectF(pLeft, headerH, pLeft + pWidth, clientHeight);

        bool isGroupFocused = (static_cast<int>(g) == focusedGroupIdx);
        bool isFirstPane = (g == 0);

        // Push clip rect for this editor group
        pRT->PushAxisAlignedClip(groups[g].bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        RenderSinglePane(pRT, theme, config, groups[g].docs, groups[g].activeDocIdx,
                         pLeft, pWidth, headerH, clientHeight,
                         (isFirstPane ? hasLeftDrawer : true), isGroupFocused,
                         highlightQuery, highlightMatchCase, highlightWholeWord, highlightUseRegex,
                         isRenaming, groups[g].tabScrollOffset, groups[g].totalTabsWidth, static_cast<int>(g),
                         &groups[g].tabScrollTrack, &groups[g].tabScrollThumb,
                         &groups[g].lastScrolledActiveDocIdx, groups[g].isDraggingTabScroll);

        // Visual drop zone feedback when dragging a tab across split groups (Image 4 / User request)
        if (m_isDraggingTab && m_tabDropTargetGroup == static_cast<int>(g) && m_dragTabGroup != static_cast<int>(g)) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dropZoneBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.08f), &dropZoneBrush);
            if (dropZoneBrush) {
                pRT->FillRectangle(groups[g].bounds, dropZoneBrush.Get());
            }
            if (theme.brushBrandAccent) {
                pRT->DrawRectangle(groups[g].bounds, theme.brushBrandAccent.Get(), 1.5f);
            }
        }

        pRT->PopAxisAlignedClip();

        // 1px Vertical divider line between panes (Image 4)
        if (g < numGroups - 1) {
            float dividerX = pLeft + pWidth + 0.5f;
            pRT->DrawLine(
                D2D1::Point2F(dividerX, headerH),
                D2D1::Point2F(dividerX, clientHeight),
                theme.brushBorderSubtle.Get(),
                1.0f
            );
        }
    }

    // Render floating refactor balloon popover if visible (Image 5)
    if (m_showRefactorPopup) {
        RenderRefactorPopup(pRT, theme, config, clientHeight);
    }

    // Render floating autocomplete suggestions popup (Image 5)
    if (m_showAutocomplete && !m_autocompleteItems.empty()) {
        RenderAutocompletePopup(pRT, theme, config, canvasLeft + canvasWidth, clientHeight);
    }

    // Render floating dragged tab pill on TOP of EVERYTHING (above tabs, breadcrumbs, and editor canvas)
    if (m_isDraggingTab && !m_dragTabTitle.empty() && (m_dragTabPos.x > 0.0f || m_dragTabPos.y > 0.0f)) {
        float pillW = (std::min)(220.0f, (std::max)(130.0f, config.ScaleF(145.0f)));
        float pillH = config.ScaleF(28.0f);
        float pillX = m_dragTabPos.x - pillW / 2.0f;
        float pillY = m_dragTabPos.y - pillH / 2.0f;
        D2D1_RECT_F rcPill = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);
        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, 4.0f, 4.0f);

        // Elevated card drop shadow
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f), &shadowBrush);
        if (shadowBrush) {
            D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(
                D2D1::RectF(pillX + 2.0f, pillY + 3.0f, pillX + pillW + 2.0f, pillY + pillH + 3.0f),
                4.0f, 4.0f
            );
            pRT->FillRoundedRectangle(rrcShadow, shadowBrush.Get());
        }

        // Pill background
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBg;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.98f) : D2D1::ColorF(0.92f, 0.92f, 0.94f, 0.98f),
            &pillBg
        );
        if (pillBg) pRT->FillRoundedRectangle(rrcPill, pillBg.Get());

        // Brand accent border
        if (theme.brushBrandAccent) {
            pRT->DrawRoundedRectangle(rrcPill, theme.brushBrandAccent.Get(), 1.5f);
        }

        // File Icon (Seti or Lucide)
        float iconX = pillX + config.ScaleF(8.0f);
        float iconY = pillY + (pillH - config.ScaleF(16.0f)) / 2.0f;
        D2D1_RECT_F iconBounds = D2D1::RectF(iconX, iconY, iconX + config.ScaleF(16.0f), iconY + config.ScaleF(16.0f));
        auto setiInfo = theme.GetSetiIcon(m_dragTabTitle, false, false);
        if (theme.fontSeti && setiInfo.pBrush) {
            pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), iconBounds, setiInfo.pBrush);
        } else {
            auto iconType = LucideIcons::GetIconTypeForPath(m_dragTabTitle, false, false);
            m_icons.DrawFileIcon(pRT, theme, iconBounds, iconType);
        }

        // File title text
        float textX = iconX + config.ScaleF(18.0f);
        float textW = pillW - config.ScaleF(44.0f);
        D2D1_RECT_F rcText = D2D1::RectF(textX, pillY, textX + textW, pillY + pillH);
        IDWriteTextFormat* pFmt = theme.fontTab ? theme.fontTab.Get() : theme.fontBody.Get();
        ID2D1Brush* pTxtBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
        if (pFmt && pTxtBrush) {
            pRT->DrawText(m_dragTabTitle.c_str(), static_cast<UINT32>(m_dragTabTitle.size()),
                          pFmt, rcText, pTxtBrush);
        }

        // Close 'x' icon
        float closeX = pillX + pillW - config.ScaleF(18.0f);
        float closeY = pillY + (pillH - config.ScaleF(10.0f)) / 2.0f;
        float cx = closeX + config.ScaleF(5.0f);
        float cy = closeY + config.ScaleF(5.0f);
        float cr = config.ScaleF(3.5f);
        if (theme.brushTextSecondary) {
            pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr), D2D1::Point2F(cx + cr, cy + cr), theme.brushTextSecondary.Get(), 1.2f);
            pRT->DrawLine(D2D1::Point2F(cx + cr, cy - cr), D2D1::Point2F(cx - cr, cy + cr), theme.brushTextSecondary.Get(), 1.2f);
        }
    }
}

void EditorCanvas::RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    std::vector<editor::OpenDocument>& docs, int docIdx,
                                    float canvasLeft, float canvasWidth, float headerH, float clientHeight,
                                    bool hasLeftDrawer, bool isPaneFocused,
                                    const std::wstring& highlightQuery,
                                    bool highlightMatchCase,
                                    bool highlightWholeWord,
                                    bool highlightUseRegex,
                                    bool isRenaming,
                                    float& tabScrollOffset, float& totalTabsWidth, int groupIdx,
                                    D2D1_RECT_F* outTrack, D2D1_RECT_F* outThumb,
                                    int* pLastScrolledActiveDocIdx, bool isDraggingTabScroll) {
    if (docs.empty() || docIdx < 0 || docIdx >= static_cast<int>(docs.size())) {
        m_packageManagerView.HideWebView();
        RenderPlaceholder(pRT, theme, config, canvasLeft, canvasWidth, headerH, clientHeight);
        return;
    }

    const float tabH = config.ScaleF(34.0f);
    RenderTabs(pRT, theme, config, canvasLeft, canvasWidth, headerH, docs, docIdx, hasLeftDrawer, isPaneFocused,
               tabScrollOffset, totalTabsWidth, groupIdx, outTrack, outThumb, pLastScrolledActiveDocIdx, isDraggingTabScroll);

    auto& activeDoc = docs[docIdx];
    const float breadcrumbH = config.showBreadcrumbs ? config.ScaleF(24.0f) : 0.0f;
    if (config.showBreadcrumbs) {
        RenderBreadcrumbs(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, headerH + tabH, breadcrumbH);
    }

    float contentTopY = headerH + tabH + breadcrumbH;
    if (activeDoc.isSpecialView && activeDoc.viewType == L"keybindings") {
        m_packageManagerView.HideWebView();
        m_keybindingsView.Render(pRT, theme, config, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"packages") {
        m_packageManagerView.Render(pRT, theme, config, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"notebook") {
        m_packageManagerView.HideWebView();
        m_notebookView.Render(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"student") {
        m_packageManagerView.HideWebView();
        RenderStudentView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
    } else {
        m_packageManagerView.HideWebView();
        if (activeDoc.isImageView) {
            RenderImageView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
        } else if (activeDoc.isUnsupportedBinary) {
            RenderUnsupportedBinaryView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
        } else if (activeDoc.isDiffView) {
            if (activeDoc.isMultiFileDiff) {
                RenderMultiFileDiffView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight, isPaneFocused);
            } else if (activeDoc.isDiffBinary) {
                RenderDiffBinaryView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
            } else {
                RenderDiffView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight, isPaneFocused);
            }
        } else {
            RenderCode(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight, isPaneFocused,
                       highlightQuery, highlightMatchCase, highlightWholeWord, highlightUseRegex, isRenaming);
        }
    }
}

void EditorCanvas::RenderPlaceholder(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     float canvasLeft, float canvasWidth, float headerH, float clientHeight) {
    const float centerY = headerH + (clientHeight - headerH) / 2.0f;

    // Initialize metallic white watermark bitmap for dark mode if needed
    if (!m_pWatermarkBitmap) {
        D2D1_SIZE_U size = D2D1::SizeU(kWatermarkLogoWidth, kWatermarkLogoHeight);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        pRT->CreateBitmap(size, g_watermarkLogoBgra, kWatermarkLogoWidth * 4, props, &m_pWatermarkBitmap);
    }
    // Initialize inverted watermark bitmap for light mode if needed (black outer square, white inside)
    if (!m_pWatermarkBitmapLight) {
        D2D1_SIZE_U size = D2D1::SizeU(kWatermarkLogoWidth, kWatermarkLogoHeight);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        std::vector<uint8_t> lightBgra(sizeof(g_watermarkLogoBgra));
        for (size_t i = 0; i < sizeof(g_watermarkLogoBgra); i += 4) {
            lightBgra[i + 0] = 255 - g_watermarkLogoBgra[i + 0]; // B
            lightBgra[i + 1] = 255 - g_watermarkLogoBgra[i + 1]; // G
            lightBgra[i + 2] = 255 - g_watermarkLogoBgra[i + 2]; // R
            lightBgra[i + 3] = g_watermarkLogoBgra[i + 3];       // A
        }
        pRT->CreateBitmap(size, lightBgra.data(), kWatermarkLogoWidth * 4, props, &m_pWatermarkBitmapLight);
    }

    // Render Logo Watermark in center of canvas (dark logo in light mode, metallic in dark mode)
    const float logoSize = config.ScaleF(84.0f);
    const float logoX = canvasLeft + (canvasWidth - logoSize) / 2.0f;
    const float logoY = centerY - logoSize / 2.0f - config.ScaleF(40.0f);
    D2D1_RECT_F rcLogo = D2D1::RectF(logoX, logoY, logoX + logoSize, logoY + logoSize);

    ID2D1Bitmap* pBmp = theme.IsDarkMode() ? m_pWatermarkBitmap.Get() : m_pWatermarkBitmapLight.Get();
    if (pBmp) {
        pRT->DrawBitmap(pBmp, rcLogo);
    }

    // Render Visually Appealing Keybinding Cheat-sheet Card (No emojis, sleek tactile keycaps)
    const float cardW = config.ScaleF(360.0f);
    const float cardH = config.ScaleF(118.0f);
    const float cardX = canvasLeft + (canvasWidth - cardW) / 2.0f;
    const float cardY = logoY + logoSize + config.ScaleF(24.0f);

    D2D1_RECT_F rcCard = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);
    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 8.0f, 8.0f);

    // Subtle dark glass card surface
    pRT->FillRoundedRectangle(rrcCard, theme.brushDrawerBg.Get());
    pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 1.0f);

    // Card Row Metrics
    const float padX = config.ScaleF(16.0f);
    const float rowH = cardH / 3.0f;
    const float capH = config.ScaleF(20.0f);

    // Row 1: Show Explorer  -->  Ctrl + B
    float row1CenterY = cardY + rowH * 0.5f;
    D2D1_RECT_F rcLabel1 = D2D1::RectF(cardX + padX, row1CenterY - config.ScaleF(10.0f), cardX + cardW / 2.0f, row1CenterY + config.ScaleF(10.0f));
    pRT->DrawText(L"Show Explorer", 13, theme.fontHint.Get(), rcLabel1, theme.brushTextSecondary.Get());
    float rx1 = cardX + cardW - padX;
    DrawKeycap(pRT, theme, config, L"B", rx1, row1CenterY, capH);
    DrawPlus(pRT, theme, config, rx1, row1CenterY, capH);
    DrawKeycap(pRT, theme, config, L"Ctrl", rx1, row1CenterY, capH);

    // Row 2: Open Workspace -->  Ctrl + O
    float row2CenterY = cardY + rowH * 1.5f;
    D2D1_RECT_F rcLabel2 = D2D1::RectF(cardX + padX, row2CenterY - config.ScaleF(10.0f), cardX + cardW / 2.0f, row2CenterY + config.ScaleF(10.0f));
    pRT->DrawText(L"Open Workspace", 14, theme.fontHint.Get(), rcLabel2, theme.brushTextSecondary.Get());
    float rx2 = cardX + cardW - padX;
    DrawKeycap(pRT, theme, config, L"O", rx2, row2CenterY, capH);
    DrawPlus(pRT, theme, config, rx2, row2CenterY, capH);
    DrawKeycap(pRT, theme, config, L"Ctrl", rx2, row2CenterY, capH);

    // Row 3: Command Palette --> Ctrl + Shift + P
    float row3CenterY = cardY + rowH * 2.5f;
    D2D1_RECT_F rcLabel3 = D2D1::RectF(cardX + padX, row3CenterY - config.ScaleF(10.0f), cardX + cardW / 2.0f, row3CenterY + config.ScaleF(10.0f));
    pRT->DrawText(L"Command Palette", 15, theme.fontHint.Get(), rcLabel3, theme.brushTextSecondary.Get());
    float rx3 = cardX + cardW - padX;
    DrawKeycap(pRT, theme, config, L"P", rx3, row3CenterY, capH);
    DrawPlus(pRT, theme, config, rx3, row3CenterY, capH);
    DrawKeycap(pRT, theme, config, L"Shift", rx3, row3CenterY, capH);
    DrawPlus(pRT, theme, config, rx3, row3CenterY, capH);
    DrawKeycap(pRT, theme, config, L"Ctrl", rx3, row3CenterY, capH);
}

void EditorCanvas::RenderTabs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              float canvasLeft, float canvasWidth, float headerH,
                              std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                              bool hasLeftDrawer, bool isFocused,
                              float& tabScrollOffset, float& totalTabsWidth, int groupIdx,
                              D2D1_RECT_F* outTrack, D2D1_RECT_F* outThumb,
                              int* pLastScrolledActiveDocIdx, bool isDraggingTabScroll) {
    const float tabH = config.ScaleF(34.0f);
    const float cornerRadius = hasLeftDrawer ? 0.0f : config.ScaleF(14.0f);

    // 1. Tab Bar Background (#181818 in dark, #ECECEC in light)
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasLeft, headerH, canvasLeft + canvasWidth, headerH + tabH);
    FillUpperLeftRoundedRect(pRT, theme.brushTabBarBg.Get(), rcTabBar, cornerRadius);

    // Bottom border of tab bar
    if (theme.brushTabBorder) {
        pRT->DrawLine(
            D2D1::Point2F(canvasLeft, headerH + tabH - 0.5f),
            D2D1::Point2F(canvasLeft + canvasWidth, headerH + tabH - 0.5f),
            theme.brushTabBorder.Get(),
            1.0f
        );
    }

    // First pass: Precalculate tab widths and total strip width for horizontal scrolling
    std::vector<float> tabWidths(docs.size());
    float sumW = 0.0f;
    for (size_t i = 0; i < docs.size(); ++i) {
        auto& doc = docs[i];
        bool isFirstTab = (i == 0);
        float tabCurve = (isFirstTab && !hasLeftDrawer) ? cornerRadius : 0.0f;

        std::wstring title = doc.fileName.empty() ? L"Untitled-1" : doc.fileName;
        float textW = static_cast<float>(title.length()) * config.ScaleF(7.5f);

        float tabW = config.ScaleF(16.0f + 16.0f + 42.0f) + textW;
        if (tabCurve > 0.0f) {
            tabW += config.ScaleF(6.0f);
        }
        tabW = (std::clamp)(tabW, config.ScaleF(100.0f), config.ScaleF(220.0f));
        tabWidths[i] = tabW;
        sumW += tabW;
    }
    totalTabsWidth = sumW;

    float maxScroll = (std::max)(0.0f, totalTabsWidth - canvasWidth);
    if (!isDraggingTabScroll && activeDocIdx >= 0 && activeDocIdx < static_cast<int>(docs.size())) {
        bool shouldAutoScroll = true;
        if (pLastScrolledActiveDocIdx) {
            if (*pLastScrolledActiveDocIdx == activeDocIdx) {
                shouldAutoScroll = false;
            } else {
                *pLastScrolledActiveDocIdx = activeDocIdx;
            }
        }
        if (shouldAutoScroll) {
            float activeStart = 0.0f;
            for (int i = 0; i < activeDocIdx; ++i) activeStart += tabWidths[i];
            float activeEnd = activeStart + tabWidths[activeDocIdx];

            if (activeStart < tabScrollOffset) {
                tabScrollOffset = activeStart;
            } else if (activeEnd > tabScrollOffset + canvasWidth) {
                tabScrollOffset = activeEnd - canvasWidth;
            }
        }
    }
    tabScrollOffset = (std::clamp)(tabScrollOffset, 0.0f, maxScroll);

    pRT->PushAxisAlignedClip(rcTabBar, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float curX = canvasLeft - tabScrollOffset;
    for (size_t i = 0; i < docs.size(); ++i) {
        auto& doc = docs[i];
        bool isActive = (static_cast<int>(i) == activeDocIdx);
        bool isFirstTab = (i == 0);
        float tabCurve = (isFirstTab && !hasLeftDrawer && tabScrollOffset <= 0.0f) ? cornerRadius : 0.0f;
        float tabW = tabWidths[i];
        std::wstring title = doc.fileName.empty() ? L"Untitled-1" : doc.fileName;

        D2D1_RECT_F rcTab = D2D1::RectF(curX, headerH, curX + tabW, headerH + tabH);
        doc.tabBounds = rcTab;

        // Draw tab surface
        if (isActive) {
            FillUpperLeftRoundedRect(pRT, theme.brushTabActiveBg.Get(), rcTab, tabCurve);
            // Top active tab highlight border (2px Accent Blue) - ONLY on focused editor group!
            if (isFocused) {
                if (tabCurve > 0.0f) {
                    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
                    pRT->GetFactory(&pFactory);
                    if (pFactory) {
                        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pTopPath;
                        if (SUCCEEDED(pFactory->CreatePathGeometry(&pTopPath))) {
                            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sinkTop;
                            if (SUCCEEDED(pTopPath->Open(&sinkTop))) {
                                sinkTop->BeginFigure(D2D1::Point2F(rcTab.left + 0.5f, headerH + tabCurve), D2D1_FIGURE_BEGIN_HOLLOW);
                                D2D1_ARC_SEGMENT arcTop{};
                                arcTop.point = D2D1::Point2F(rcTab.left + tabCurve, headerH + 1.0f);
                                arcTop.size = D2D1::SizeF(tabCurve, tabCurve);
                                arcTop.rotationAngle = 0.0f;
                                arcTop.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                                arcTop.arcSize = D2D1_ARC_SIZE_SMALL;
                                sinkTop->AddArc(arcTop);
                                sinkTop->AddLine(D2D1::Point2F(rcTab.right, headerH + 1.0f));
                                sinkTop->EndFigure(D2D1_FIGURE_END_OPEN);
                                sinkTop->Close();
                                pRT->DrawGeometry(pTopPath.Get(), theme.brushBrandAccent.Get(), 2.0f);
                            }
                        }
                    }
                } else {
                    if (theme.brushBrandAccent) {
                        pRT->DrawLine(
                            D2D1::Point2F(rcTab.left, headerH + 1.0f),
                            D2D1::Point2F(rcTab.right, headerH + 1.0f),
                            theme.brushBrandAccent.Get(),
                            2.0f
                        );
                    }
                }
            }
        }

        // Right tab divider line
        if (theme.brushTabBorder) {
            pRT->DrawLine(
                D2D1::Point2F(rcTab.right - 0.5f, headerH),
                D2D1::Point2F(rcTab.right - 0.5f, headerH + tabH),
                theme.brushTabBorder.Get(),
                1.0f
            );
        }

        // Icon
        const float iconPadX = (tabCurve > 0.0f) ? config.ScaleF(14.0f) : config.ScaleF(10.0f);
        const float iconX = rcTab.left + iconPadX;
        const float iconCenterY = headerH + tabH / 2.0f;
        D2D1_RECT_F iconBounds = D2D1::RectF(
            iconX,
            iconCenterY - config.ScaleF(8.0f),
            iconX + config.ScaleF(16.0f),
            iconCenterY + config.ScaleF(8.0f)
        );

        if (doc.isSpecialView && doc.viewType == L"keybindings") {
            // Draw keyboard glyph / settings
            if (theme.fontSeti && theme.brushSetiJson) {
                wchar_t kbGlyph = 0xE055;
                pRT->DrawText(&kbGlyph, 1, theme.fontSeti.Get(), iconBounds, theme.brushSetiJson.Get());
            } else {
                m_icons.DrawFileIcon(pRT, theme, iconBounds, FileIconType::Json);
            }
        } else if (doc.isSpecialView && doc.viewType == L"packages") {
            m_icons.DrawPackage(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.1f);
        } else if (doc.isSpecialView && doc.viewType == L"student") {
            m_icons.DrawGraduationCap(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.3f);
        } else if ((doc.isSpecialView && doc.viewType == L"notebook") || doc.filePath.ends_with(L".ipynb")) {
            m_icons.DrawTerminalNotebook(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.3f);
        } else {
            auto setiInfo = theme.GetSetiIcon(title, false, false);
            if (theme.fontSeti && setiInfo.pBrush) {
                pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), iconBounds, setiInfo.pBrush);
            } else {
                auto iconType = LucideIcons::GetIconTypeForPath(title, false, false);
                m_icons.DrawFileIcon(pRT, theme, iconBounds, iconType);
            }
        }

        // Title: preview tabs use fontTabPreview (italic), pinned tabs use fontTab (regular)
        float textStartX = rcTab.left + ((tabCurve > 0.0f) ? config.ScaleF(34.0f) : config.ScaleF(30.0f));
        float textMaxW   = (std::max)(10.0f, rcTab.right - config.ScaleF(36.0f) - textStartX);
        D2D1_RECT_F rcTitle = D2D1::RectF(
            textStartX,
            headerH,
            textStartX + textMaxW,
            headerH + tabH
        );
        IDWriteTextFormat* pTextFmt = doc.isPreview ? 
            (theme.fontTabPreview ? theme.fontTabPreview.Get() : theme.fontMenuItem.Get()) :
            (theme.fontTab ? theme.fontTab.Get() : theme.fontMenuItem.Get());
        bool hasErrors = !doc.diagnostics.empty();
        ID2D1Brush* pTextBrush = hasErrors ?
            (theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushTextPrimary.Get()) :
            (isActive ? 
                (isFocused ? (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextSecondary.Get()) :
                theme.brushTextSecondary.Get());

        if (pTextBrush && pTextFmt) {
            pRT->PushAxisAlignedClip(rcTitle, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pRT->DrawText(title.c_str(), static_cast<UINT32>(title.length()), pTextFmt, rcTitle, pTextBrush);
            pRT->PopAxisAlignedClip();
        }

        // Close Tab Button (×)
        const float closeSize = config.ScaleF(16.0f);
        const float closeX = rcTab.right - config.ScaleF(20.0f);
        const float closeY = headerH + (tabH - closeSize) / 2.0f;
        D2D1_RECT_F rcClose = D2D1::RectF(closeX, closeY, closeX + closeSize, closeY + closeSize);
        doc.closeBounds = rcClose;

        // Red Error Indicator on Tab (Image 3)
        if (hasErrors) {
            float errDotX = closeX - config.ScaleF(doc.isDirty ? 18.0f : 9.0f);
            float errDotY = headerH + tabH / 2.0f;
            float errDotR = config.ScaleF(3.5f);
            D2D1_ELLIPSE elErr = D2D1::Ellipse(D2D1::Point2F(errDotX, errDotY), errDotR, errDotR);
            if (theme.brushIconRed) pRT->FillEllipse(elErr, theme.brushIconRed.Get());
        }

        // Dirty Circle Indicator: ONLY if doc.isDirty is true! (7px diameter)
        if (doc.isDirty) {
            float dotX = closeX - config.ScaleF(9.0f);
            float dotY = headerH + tabH / 2.0f;
            float dotRadius = config.ScaleF(3.5f);
            D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotRadius, dotRadius);
            pRT->FillEllipse(dot, theme.brushIconBlue ? theme.brushIconBlue.Get() : theme.brushBrandAccent.Get());
        }

        // Vector '×' mark
        const float cx = closeX + closeSize / 2.0f;
        const float cy = closeY + closeSize / 2.0f;
        const float r = config.ScaleF(3.5f);
        if (theme.brushTextSecondary) {
            pRT->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), theme.brushTextSecondary.Get(), 1.2f);
            pRT->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx - r, cy + r), theme.brushTextSecondary.Get(), 1.2f);
        }

        curX += tabW;
    }

    pRT->PopAxisAlignedClip();

    // 2. Horizontal Scrollbar for Tab Bar (User request: "since i do many tag it many now there should be a scrol bar so i could scroll throught them")
    if (totalTabsWidth > canvasWidth) {
        float trackH = config.ScaleF(5.0f);
        float hitH = config.ScaleF(14.0f);
        D2D1_RECT_F rcTrack = D2D1::RectF(canvasLeft, headerH + tabH - trackH, canvasLeft + canvasWidth, headerH + tabH);
        D2D1_RECT_F rcTrackHit = D2D1::RectF(canvasLeft, headerH + tabH - hitH, canvasLeft + canvasWidth, headerH + tabH + config.ScaleF(4.0f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTrack;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.6f) : D2D1::ColorF(0.85f, 0.85f, 0.85f, 0.6f),
            &brushTrack
        );
        if (brushTrack) pRT->FillRectangle(rcTrack, brushTrack.Get());

        float thumbW = (std::max)(config.ScaleF(36.0f), (canvasWidth / totalTabsWidth) * canvasWidth);
        float maxScroll = totalTabsWidth - canvasWidth;
        float ratio = (maxScroll > 0.0f) ? (tabScrollOffset / maxScroll) : 0.0f;
        float thumbX = canvasLeft + ratio * (canvasWidth - thumbW);
        D2D1_RECT_F rcThumb = D2D1::RectF(thumbX, headerH + tabH - trackH, thumbX + thumbW, headerH + tabH);
        D2D1_RECT_F rcThumbHit = D2D1::RectF(thumbX, headerH + tabH - hitH, thumbX + thumbW, headerH + tabH + config.ScaleF(4.0f));

        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(rcThumb, 2.0f, 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushThumb;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.48f, 0.48f, 0.52f, 0.85f) : D2D1::ColorF(0.52f, 0.52f, 0.56f, 0.85f),
            &brushThumb
        );
        if (brushThumb) pRT->FillRoundedRectangle(rrcThumb, brushThumb.Get());

        if (outTrack) *outTrack = rcTrackHit;
        if (outThumb) *outThumb = rcThumbHit;
    } else {
        if (outTrack) *outTrack = D2D1::RectF(0, 0, 0, 0);
        if (outThumb) *outThumb = D2D1::RectF(0, 0, 0, 0);
    }

    // 3. Tab Drag-and-Drop Insertion Line Indicator & Floating Tab Pill (Image 3: "Buffer.cpp")
    if (m_isDraggingTab && m_tabDropTargetGroup == groupIdx) {
        if (m_tabDropTargetIdx >= 0) {
            float dropX = canvasLeft - tabScrollOffset;
            for (int i = 0; i < m_tabDropTargetIdx && i < static_cast<int>(docs.size()); ++i) {
                dropX += tabWidths[i];
            }
            if (dropX >= canvasLeft && dropX <= canvasLeft + canvasWidth) {
                D2D1_RECT_F rcDropLine = D2D1::RectF(dropX - 1.5f, headerH + 1.0f, dropX + 1.5f, headerH + tabH - 1.0f);
                ID2D1Brush* pDropBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTabBorder.Get();
                if (pDropBrush) {
                    pRT->FillRectangle(rcDropLine, pDropBrush);
                }
            }
        }
    }
}

void EditorCanvas::RenderBreadcrumbs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     const editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                     float barY, float barHeight) {
    if (!pRT || barHeight <= 0.0f) return;
    const float canvasRight = canvasLeft + canvasWidth;
    D2D1_RECT_F rcBar = D2D1::RectF(canvasLeft, barY, canvasRight, barY + barHeight);

    // Subtle surface backdrop
    if (theme.IsDarkMode()) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f), &brushBg); // #1E1E1E
        if (brushBg) pRT->FillRectangle(rcBar, brushBg.Get());
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.97f, 0.97f, 0.97f, 1.0f), &brushBg); // #F7F7F7
        if (brushBg) pRT->FillRectangle(rcBar, brushBg.Get());
    }

    // Bottom subtle 1px divider
    pRT->DrawLine(
        D2D1::Point2F(canvasLeft, barY + barHeight - 0.5f),
        D2D1::Point2F(canvasRight, barY + barHeight - 0.5f),
        theme.brushBorderSubtle.Get(),
        0.8f
    );

    // Diagnostic error count badge on breadcrumb bar (Image 4) - centered text & vector icon
    if (!doc.diagnostics.empty()) {
        std::wstring errBadgeStr = std::to_wstring(doc.diagnostics.size()) +
            (doc.diagnostics.size() == 1 ? L" error" : L" errors");
        float badgeW = config.ScaleF(80.0f);
        float badgeH = config.ScaleF(18.0f);
        float badgeX = canvasRight - config.ScaleF(95.0f);
        float badgeY = barY + (barHeight - badgeH) / 2.0f;
        D2D1_RECT_F rcErrBadge = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
        D2D1_ROUNDED_RECT rrcErrBadge = D2D1::RoundedRect(rcErrBadge, 4.0f, 4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> errBadgeBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> errBadgeBorder;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.22f, 0.22f, 0.18f), &errBadgeBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.30f, 0.30f, 0.40f), &errBadgeBorder);
        if (errBadgeBg) pRT->FillRoundedRectangle(&rrcErrBadge, errBadgeBg.Get());
        if (errBadgeBorder) pRT->DrawRoundedRectangle(&rrcErrBadge, errBadgeBorder.Get(), 0.9f);

        ID2D1Brush* redBrush = theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushTextPrimary.Get();

        // Vector ⊗ symbol
        float iconR = config.ScaleF(4.5f);
        float iconCx = badgeX + config.ScaleF(9.5f);
        float iconCy = badgeY + badgeH / 2.0f;
        D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(iconCx, iconCy), iconR, iconR);
        pRT->DrawEllipse(el, redBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(iconCx - 2.0f, iconCy - 2.0f), D2D1::Point2F(iconCx + 2.0f, iconCy + 2.0f), redBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(iconCx + 2.0f, iconCy - 2.0f), D2D1::Point2F(iconCx - 2.0f, iconCy + 2.0f), redBrush, 1.2f);

        // Perfectly centered text
        D2D1_RECT_F rcErrText = D2D1::RectF(iconCx + config.ScaleF(6.0f), badgeY, badgeX + badgeW - config.ScaleF(2.0f), badgeY + badgeH);
        pRT->DrawText(errBadgeStr.c_str(), static_cast<UINT32>(errBadgeStr.length()),
                      theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(), rcErrText, redBrush);
    }

    // Parse segments from doc.filePath (Image 4)
    std::wstring pathStr = doc.filePath.empty() ? doc.fileName : doc.filePath;
    for (auto& c : pathStr) {
        if (c == L'/') c = L'\\';
    }

    std::vector<std::wstring> segments;
    size_t start = 0;
    while (start < pathStr.length()) {
        size_t end = pathStr.find(L'\\', start);
        if (end == std::wstring::npos) end = pathStr.length();
        if (end > start) {
            std::wstring seg = pathStr.substr(start, end - start);
            // Skip Windows drive letter root if present (e.g. C:)
            if (!(seg.length() == 2 && seg[1] == L':')) {
                segments.push_back(seg);
            }
        }
        start = end + 1;
    }

    // Keep up to 4 segments for a clean breadcrumb hierarchy
    if (segments.size() > 4) {
        segments.erase(segments.begin(), segments.end() - 4);
    }
    if (segments.empty()) {
        segments.push_back(doc.fileName.empty() ? L"Untitled" : doc.fileName);
    }

    float curX = canvasLeft + config.ScaleF(14.0f);
    IDWriteTextFormat* pFmt = theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get();

    for (size_t s = 0; s < segments.size(); ++s) {
        const auto& seg = segments[s];
        bool isLast = (s == segments.size() - 1);

        float textW = static_cast<float>(seg.length()) * config.ScaleF(7.0f) + config.ScaleF(6.0f);
        D2D1_RECT_F rcSeg = D2D1::RectF(curX, barY + config.ScaleF(2.0f), curX + textW, barY + barHeight - config.ScaleF(2.0f));

        ID2D1Brush* segBrush = isLast
            ? theme.brushTextPrimary.Get()
            : theme.brushTextSecondary.Get();

        pRT->DrawText(seg.c_str(), static_cast<UINT32>(seg.length()), pFmt, rcSeg, segBrush);
        curX += textW;

        if (!isLast) {
            // Draw chevron separator › (Unicode \u203A)
            const wchar_t* sep = L"\u203A";
            float sepW = config.ScaleF(14.0f);
            D2D1_RECT_F rcSep = D2D1::RectF(curX, barY + config.ScaleF(2.0f), curX + sepW, barY + barHeight - config.ScaleF(2.0f));
            pRT->DrawText(sep, 1, pFmt, rcSep, theme.brushTextMuted.Get());
            curX += sepW;
        }
    }
}

bool EditorCanvas::GetFoldRange(const editor::Buffer& buffer, size_t startLine, size_t& outEndLine) {
    if (startLine >= buffer.GetLineCount()) return false;
    const std::wstring& line = buffer.GetLine(startLine);

    // 1. Multiline comment /* or /** (Image 2 & 3)
    size_t commentStart = line.find(L"/*");
    if (commentStart != std::wstring::npos) {
        if (line.find(L"*/", commentStart + 2) == std::wstring::npos) {
            for (size_t l = startLine + 1; l < buffer.GetLineCount(); ++l) {
                const std::wstring& nextLine = buffer.GetLine(l);
                if (nextLine.find(L"*/") != std::wstring::npos) {
                    outEndLine = l;
                    return true;
                }
            }
        }
    }

    // 2. Open brace '{' matching block
    int braceBalance = 0;
    bool hasOpenBrace = false;
    for (wchar_t ch : line) {
        if (ch == L'{') { braceBalance++; hasOpenBrace = true; }
        else if (ch == L'}') { braceBalance--; }
    }

    if (hasOpenBrace && braceBalance > 0) {
        int curBalance = braceBalance;
        for (size_t l = startLine + 1; l < buffer.GetLineCount(); ++l) {
            const std::wstring& nextLine = buffer.GetLine(l);
            for (wchar_t ch : nextLine) {
                if (ch == L'{') curBalance++;
                else if (ch == L'}') {
                    curBalance--;
                    if (curBalance == 0) {
                        outEndLine = l;
                        return true;
                    }
                }
            }
        }
    }

    // 3. Indentation-based block folding (Python, YAML, and nested blocks)
    size_t startIndent = 0;
    while (startIndent < line.length() && (line[startIndent] == L' ' || line[startIndent] == L'\t')) {
        startIndent++;
    }
    if (startIndent < line.length()) {
        size_t lastIndented = startLine;
        for (size_t l = startLine + 1; l < buffer.GetLineCount(); ++l) {
            const std::wstring& nextLine = buffer.GetLine(l);
            size_t nextIndent = 0;
            while (nextIndent < nextLine.length() && (nextLine[nextIndent] == L' ' || nextLine[nextIndent] == L'\t')) {
                nextIndent++;
            }
            if (nextIndent == nextLine.length()) continue;
            if (nextIndent > startIndent) {
                lastIndented = l;
            } else {
                break;
            }
        }
        if (lastIndented > startLine) {
            outEndLine = lastIndented;
            return true;
        }
    }

    return false;
}

void EditorCanvas::RenderCode(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                              float topY, float clientHeight, bool isFocused,
                              const std::wstring& highlightQuery,
                              bool highlightMatchCase,
                              bool highlightWholeWord,
                              bool highlightUseRegex,
                              bool isRenaming) {
    auto& buffer = doc.buffer;
    const float gutterW = config.ScaleF(50.0f);
    const float lineH   = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));

    if (m_charWidth <= 0.0f && theme.dwriteFactory && theme.fontCode) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                L"M", 1, theme.fontCode.Get(), 1000.0f, 100.0f, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                m_charWidth = tm.widthIncludingTrailingWhitespace;
            }
        }
    }
    const float charW   = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);
    const float scrollbarW = config.ScaleF(14.0f);
    const float minimapW = config.ScaleF(88.0f);
    const float canvasRight = canvasLeft + canvasWidth;
    const float codeAreaH = clientHeight - topY;
    const float codeRight = config.showMinimap ? (canvasRight - minimapW - config.ScaleF(6.0f)) : (canvasRight - scrollbarW);
    const float swatchSpan = config.ScaleF(14.0f);
    const float swatchSize = config.ScaleF(10.5f);

    m_foldChevrons.clear();
    m_visibleColorSwatches.clear();

    // 1. Gutter Background & Divider
    D2D1_RECT_F rcGutter = D2D1::RectF(canvasLeft, topY, canvasLeft + gutterW, clientHeight);
    pRT->FillRectangle(rcGutter, theme.brushCanvasBg.Get());

    pRT->DrawLine(
        D2D1::Point2F(canvasLeft + gutterW - 0.5f, topY),
        D2D1::Point2F(canvasLeft + gutterW - 0.5f, clientHeight),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    // Build visual lines layout (folding awareness)
    struct VisualLine {
        size_t bufferLine;
        bool isFoldHeader;
        bool isFolded;
        size_t foldEndLine;
    };
    std::vector<VisualLine> visualLines;
    visualLines.reserve(buffer.GetLineCount());

    const size_t totalBufLines = buffer.GetLineCount();
    if (doc.foldedLines.empty()) {
        visualLines.resize(totalBufLines);
        for (size_t b = 0; b < totalBufLines; ++b) {
            visualLines[b] = { b, false, false, 0 };
        }
    } else {
        for (size_t b = 0; b < totalBufLines; ++b) {
            size_t foldEnd = 0;
            bool isFolded = (doc.foldedLines.count(b) > 0);
            if (isFolded) {
                GetFoldRange(buffer, b, foldEnd);
            }

            VisualLine vl;
            vl.bufferLine = b;
            vl.isFoldHeader = isFolded;
            vl.isFolded = isFolded;
            vl.foldEndLine = foldEnd;
            visualLines.push_back(vl);

            if (isFolded && foldEnd > b) {
                b = foldEnd; // Skip folded child lines!
            }
        }
    }

    const size_t visibleCount = (lineH > 0.0f) ? static_cast<size_t>(codeAreaH / lineH) : 20;
    // Scroll beyond last line (VS Code scrollBeyondLastLine feature): allows scrolling closing brackets comfortably up into view
    const size_t maxScroll = visualLines.empty() ? 0 : (visualLines.size() > 3 ? visualLines.size() - 3 : 0);
    if (doc.scrollLine > maxScroll) {
        doc.scrollLine = maxScroll;
    }

    // 2. Current Line Highlight - Rendered if focused or has search target line
    auto cursor = buffer.GetCursor();
    size_t visualCursorIdx = 0;
    bool foundCursorVisual = false;
    for (size_t v = 0; v < visualLines.size(); ++v) {
        if (visualLines[v].bufferLine == cursor.line) {
            visualCursorIdx = v;
            foundCursorVisual = true;
            break;
        }
        if (visualLines[v].isFolded && cursor.line > visualLines[v].bufferLine && cursor.line <= visualLines[v].foldEndLine) {
            visualCursorIdx = v;
            foundCursorVisual = true;
            break;
        }
    }

    if ((isFocused || doc.targetHighlightLine != static_cast<size_t>(-1)) && foundCursorVisual && visualCursorIdx >= doc.scrollLine) {
        float activeLineY = topY + config.ScaleF(6.0f) + (visualCursorIdx - doc.scrollLine) * lineH;
        if (activeLineY >= topY && activeLineY + lineH <= clientHeight) {
            D2D1_RECT_F rcActiveLine = D2D1::RectF(canvasLeft + gutterW, activeLineY, codeRight, activeLineY + lineH);
            pRT->FillRectangle(rcActiveLine, theme.brushCurrentLineBg.Get());
        }
    }

    // 3. Line Numbers, Fold Chevrons & Code Lines
    const float codeLeft = canvasLeft + gutterW + config.ScaleF(12.0f);
    const float scrollX = doc.scrollOffsetX;
    float maxLineWidth = 0.0f;

    D2D1_RECT_F clipCode = D2D1::RectF(canvasLeft + gutterW, topY, codeRight, clientHeight);
    pRT->PushAxisAlignedClip(clipCode, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Analyze rainbow bracket pairs, nesting depths, and active cursor pair with caching
    if (m_cachedBufferVersion != buffer.GetVersion() || m_cachedCursorLine != cursor.line || m_cachedCursorCol != cursor.col) {
        m_cachedBracketAnalysis = editor::BracketPairAnalyzer::Analyze(buffer, cursor.line, cursor.col);
        m_cachedBufferVersion = buffer.GetVersion();
        m_cachedCursorLine = cursor.line;
        m_cachedCursorCol = cursor.col;
    }
    const auto& bracketAnalysis = m_cachedBracketAnalysis;
    std::unordered_map<size_t, std::vector<const editor::BracketItem*>> bracketsByLine;
    for (const auto& b : bracketAnalysis.brackets) {
        if (b.line < totalBufLines) {
            bracketsByLine[b.line].push_back(&b);
        }
    }

    const editor::BracketPair* pActivePair = nullptr;
    float activeOpenLineY = 0.0f;
    float activeCloseLineY = 0.0f;
    bool hasActiveOpenY = false;
    bool hasActiveCloseY = false;

    if (isFocused && bracketAnalysis.activePairIdx >= 0 &&
        bracketAnalysis.activePairIdx < static_cast<int>(bracketAnalysis.pairs.size())) {
        pActivePair = &bracketAnalysis.pairs[bracketAnalysis.activePairIdx];
        for (size_t v = 0; v < visualLines.size(); ++v) {
            if (visualLines[v].bufferLine == pActivePair->openLine) {
                activeOpenLineY = topY + config.ScaleF(6.0f) + (static_cast<float>(v) - static_cast<float>(doc.scrollLine)) * lineH;
                hasActiveOpenY = true;
            }
            if (visualLines[v].bufferLine == pActivePair->closeLine) {
                activeCloseLineY = topY + config.ScaleF(6.0f) + (static_cast<float>(v) - static_cast<float>(doc.scrollLine)) * lineH;
                hasActiveCloseY = true;
            }
            if (hasActiveOpenY && hasActiveCloseY) break;
        }
    }

    // Render active bracket connector guide lines (Images 4 & 5)
    if (pActivePair && hasActiveOpenY && hasActiveCloseY && pActivePair->closeLine > pActivePair->openLine) {
        ID2D1Brush* guideBrush = theme.GetBracketBrush(pActivePair->depth);
        if (guideBrush) {
            float guideX = codeLeft - scrollX + static_cast<float>(pActivePair->guideCol) * charW + charW * 0.5f;
            float openCharX = codeLeft - scrollX + static_cast<float>(pActivePair->openCol) * charW + charW * 0.5f;
            float closeCharX = codeLeft - scrollX + static_cast<float>(pActivePair->closeCol) * charW + charW * 0.5f;

            // Start vertical line at bottom of open line if openCol > guideCol (K&R style),
            // or at center of open line if openCol == guideCol (Allman style)
            float startY = (pActivePair->openCol == pActivePair->guideCol)
                ? (activeOpenLineY + lineH * 0.5f)
                : (activeOpenLineY + lineH);
            float endY = activeCloseLineY + lineH * 0.5f;

            if (endY > startY) {
                // Vertical connector line from opening bracket line down to closing bracket line
                pRT->DrawLine(
                    D2D1::Point2F(guideX, startY),
                    D2D1::Point2F(guideX, endY),
                    guideBrush,
                    1.5f
                );
            }

            // Bottom horizontal hook to closing bracket (if indented)
            if (closeCharX > guideX) {
                pRT->DrawLine(
                    D2D1::Point2F(guideX, endY),
                    D2D1::Point2F(closeCharX, endY),
                    guideBrush,
                    1.5f
                );
            }
        }
    }

    // Render skeletal highlight frames around opening and closing brackets of the active pair (Images 4 & 5)
    if (pActivePair) {
        ID2D1Brush* frameBrush = theme.GetBracketBrush(pActivePair->depth);
        if (frameBrush) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
            auto* solid = static_cast<ID2D1SolidColorBrush*>(frameBrush);
            D2D1_COLOR_F c = solid->GetColor();
            pRT->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, 0.22f), &fillBrush);

            // Opening bracket skeletal frame
            if (hasActiveOpenY && (activeOpenLineY + lineH >= topY) && (activeOpenLineY <= clientHeight)) {
                float ox = codeLeft - scrollX + static_cast<float>(pActivePair->openCol) * charW;
                D2D1_RECT_F rcOpen = D2D1::RectF(
                    ox - config.ScaleF(1.0f),
                    activeOpenLineY + config.ScaleF(2.0f),
                    ox + charW + config.ScaleF(1.0f),
                    activeOpenLineY + lineH - config.ScaleF(2.0f)
                );
                D2D1_ROUNDED_RECT rrcOpen = D2D1::RoundedRect(rcOpen, config.ScaleF(2.5f), config.ScaleF(2.5f));
                if (fillBrush) pRT->FillRoundedRectangle(rrcOpen, fillBrush.Get());
                pRT->DrawRoundedRectangle(rrcOpen, frameBrush, 1.5f);
            }

            // Closing bracket skeletal frame
            if (hasActiveCloseY && (activeCloseLineY + lineH >= topY) && (activeCloseLineY <= clientHeight)) {
                float cx = codeLeft - scrollX + static_cast<float>(pActivePair->closeCol) * charW;
                D2D1_RECT_F rcClose = D2D1::RectF(
                    cx - config.ScaleF(1.0f),
                    activeCloseLineY + config.ScaleF(2.0f),
                    cx + charW + config.ScaleF(1.0f),
                    activeCloseLineY + lineH - config.ScaleF(2.0f)
                );
                D2D1_ROUNDED_RECT rrcClose = D2D1::RoundedRect(rcClose, config.ScaleF(2.5f), config.ScaleF(2.5f));
                if (fillBrush) pRT->FillRoundedRectangle(rrcClose, fillBrush.Get());
                pRT->DrawRoundedRectangle(rrcClose, frameBrush, 1.5f);
            }
        }
    }

    size_t startV = doc.scrollLine;
    size_t endV = (std::min)(visualLines.size(), startV + visibleCount + 2);

    for (size_t v = startV; v < endV; ++v) {
        const auto& vl = visualLines[v];
        float lineY = topY + config.ScaleF(6.0f) + (v - doc.scrollLine) * lineH;
        if (lineY + lineH < topY || lineY > clientHeight) continue;

        // Git Diff Line Highlights (Image 5)
        if (doc.isDiffView || doc.isDeletedInGit) {
            if (doc.isDeletedInGit || doc.diffDeletedLines.count(vl.bufferLine)) {
                // Red background for deleted lines / deleted file
                D2D1_RECT_F rcDiffLine = D2D1::RectF(canvasLeft + gutterW, lineY, codeRight, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRed;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.18f, 0.20f, 0.25f), &brushDiffRed);
                pRT->FillRectangle(rcDiffLine, brushDiffRed.Get());

                D2D1_RECT_F rcGutterBar = D2D1::RectF(canvasLeft + gutterW - config.ScaleF(3.0f), lineY, canvasLeft + gutterW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedBar;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushRedBar);
                pRT->FillRectangle(rcGutterBar, brushRedBar.Get());
            } else if (doc.diffAddedLines.count(vl.bufferLine)) {
                // Green background for added lines
                D2D1_RECT_F rcDiffLine = D2D1::RectF(canvasLeft + gutterW, lineY, codeRight, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreen;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.20f, 0.25f), &brushDiffGreen);
                pRT->FillRectangle(rcDiffLine, brushDiffGreen.Get());

                D2D1_RECT_F rcGutterBar = D2D1::RectF(canvasLeft + gutterW - config.ScaleF(3.0f), lineY, canvasLeft + gutterW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushGreenBar;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), &brushGreenBar);
                pRT->FillRectangle(rcGutterBar, brushGreenBar.Get());
            }
        }

        // Target Line / Search Jump Highlight Banner (Image 1)
        if (vl.bufferLine == doc.targetHighlightLine) {
            D2D1_RECT_F rcTargetLine = D2D1::RectF(canvasLeft + gutterW, lineY, codeRight, lineY + lineH);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTargetBg;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTargetBorder;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.28f), &brushTargetBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.85f), &brushTargetBorder);
            if (brushTargetBg) pRT->FillRectangle(rcTargetLine, brushTargetBg.Get());
            if (brushTargetBorder) {
                pRT->DrawLine(D2D1::Point2F(rcTargetLine.left, rcTargetLine.top), D2D1::Point2F(rcTargetLine.right, rcTargetLine.top), brushTargetBorder.Get(), 1.2f);
                pRT->DrawLine(D2D1::Point2F(rcTargetLine.left, rcTargetLine.bottom), D2D1::Point2F(rcTargetLine.right, rcTargetLine.bottom), brushTargetBorder.Get(), 1.2f);
            }

            // High-contrast keyword box on target line
            if (doc.targetHighlightLen > 0) {
                float hx = codeLeft + doc.targetHighlightCol * charW;
                float hw = doc.targetHighlightLen * charW;
                D2D1_RECT_F rcKeyword = D2D1::RectF(hx - 2.0f, lineY + 1.0f, hx + hw + 2.0f, lineY + lineH - 1.0f);
                D2D1_ROUNDED_RECT rrcKey = D2D1::RoundedRect(rcKeyword, 3.0f, 3.0f);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyFill;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyBorder;
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.82f, 0.0f, 0.50f), &keyFill);
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.82f, 0.0f, 0.95f), &keyBorder);
                if (keyFill) pRT->FillRoundedRectangle(&rrcKey, keyFill.Get());
                if (keyBorder) pRT->DrawRoundedRectangle(&rrcKey, keyBorder.Get(), 1.5f);
            }
        }

        // Line Number in Gutter (Right-aligned)
        bool hasDiagError = false;
        for (const auto& d : doc.diagnostics) {
            if (d.line == vl.bufferLine) {
                hasDiagError = true;
                break;
            }
        }
        std::wstring lineNumStr = std::to_wstring(vl.bufferLine + 1);
        D2D1_RECT_F rcNum = D2D1::RectF(
            canvasLeft + config.ScaleF(4.0f),
            lineY,
            canvasLeft + gutterW - config.ScaleF(18.0f),
            lineY + lineH
        );
        ID2D1Brush* numBrush = hasDiagError ? (theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get()) :
            ((isFocused && vl.bufferLine == cursor.line) ? theme.brushTextPrimary.Get() : theme.brushLineNumber.Get());
        pRT->DrawText(
            lineNumStr.c_str(),
            static_cast<UINT32>(lineNumStr.length()),
            theme.fontMenuShortcut.Get(),
            rcNum,
            numBrush
        );

        // Breakpoint Marker in Gutter (F9 / Debug Menu / Click Gutter)
        bool hasBreakpoint = (std::find(doc.breakpoints.begin(), doc.breakpoints.end(), vl.bufferLine) != doc.breakpoints.end());
        if (hasBreakpoint) {
            float bpX = canvasLeft + config.ScaleF(12.0f);
            float bpY = lineY + lineH / 2.0f;
            float bpR = config.ScaleF(4.5f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bpBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.25f, 0.25f), &bpBrush);
            if (bpBrush) {
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bpX, bpY), bpR, bpR), bpBrush.Get());
            }
        }

        // Gutter Diagnostic Error Indicator Dot (Image 4)
        if (hasDiagError && !hasBreakpoint) {
            float errDotX = canvasLeft + config.ScaleF(8.0f);
            float errDotY = lineY + lineH / 2.0f;
            float errDotR = config.ScaleF(2.5f);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(errDotX, errDotY), errDotR, errDotR), theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get());
        }

        // Code Folding Chevron in Gutter (Images 2 & 3)
        size_t foldEnd = 0;
        bool canFold = vl.isFolded || GetFoldRange(buffer, vl.bufferLine, foldEnd);
        if (canFold) {
            const float chevronSize = config.ScaleF(12.0f);
            const float chevronX = canvasLeft + gutterW - config.ScaleF(16.0f);
            const float chevronY = lineY + (lineH - chevronSize) / 2.0f;
            D2D1_RECT_F rcChevron = D2D1::RectF(chevronX, chevronY, chevronX + chevronSize, chevronY + chevronSize);

            FoldChevronHit hit;
            hit.bounds = rcChevron;
            hit.bufferLine = vl.bufferLine;
            m_foldChevrons.push_back(hit);

            ID2D1Brush* chevronBrush = theme.brushTextMuted.Get();
            if (vl.isFolded) {
                // Collapsed: Right Chevron > (Image 3)
                float cx = rcChevron.left + chevronSize * 0.45f;
                float cy = rcChevron.top + chevronSize * 0.5f;
                float cr = config.ScaleF(3.0f);
                pRT->DrawLine(D2D1::Point2F(cx - cr * 0.6f, cy - cr), D2D1::Point2F(cx + cr * 0.6f, cy), chevronBrush, 1.3f);
                pRT->DrawLine(D2D1::Point2F(cx + cr * 0.6f, cy), D2D1::Point2F(cx - cr * 0.6f, cy + cr), chevronBrush, 1.3f);
            } else {
                // Expanded: Down Chevron v (Image 2)
                float cx = rcChevron.left + chevronSize * 0.5f;
                float cy = rcChevron.top + chevronSize * 0.45f;
                float cr = config.ScaleF(3.0f);
                pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr * 0.6f), D2D1::Point2F(cx, cy + cr * 0.6f), chevronBrush, 1.3f);
                pRT->DrawLine(D2D1::Point2F(cx, cy + cr * 0.6f), D2D1::Point2F(cx + cr, cy - cr * 0.6f), chevronBrush, 1.3f);
            }
        }

        // Code Content
        const auto& lineStr = buffer.GetLine(vl.bufferLine);
        float curCodeEndX = codeLeft;

        /**
         * @brief Highlight all occurrences of the active search query on this line in amber.
         * Rendered before syntax tokens so code text remains crisp and readable on top.
         */
        if (!highlightQuery.empty() && !lineStr.empty()) {
            std::vector<std::pair<size_t, size_t>> matches; // (col, length)
            if (highlightUseRegex) {
                try {
                    std::wregex::flag_type flags = std::regex_constants::ECMAScript;
                    if (!highlightMatchCase) flags |= std::regex_constants::icase;
                    std::wregex re(highlightQuery, flags);
                    auto words_begin = std::wsregex_iterator(lineStr.begin(), lineStr.end(), re);
                    auto words_end = std::wsregex_iterator();
                    for (std::wsregex_iterator it = words_begin; it != words_end; ++it) {
                        std::wsmatch sm = *it;
                        size_t matchPos = static_cast<size_t>(sm.position());
                        size_t matchLen = static_cast<size_t>(sm.length());
                        if (matchLen > 0) {
                            if (highlightWholeWord) {
                                bool leftOk = (matchPos == 0) || !iswalnum(lineStr[matchPos - 1]);
                                bool rightOk = (matchPos + matchLen >= lineStr.length()) || !iswalnum(lineStr[matchPos + matchLen]);
                                if (!leftOk || !rightOk) continue;
                            }
                            matches.push_back({matchPos, matchLen});
                        }
                    }
                } catch (...) {}
            } else {
                std::wstring qLower = highlightQuery;
                std::wstring lLower = lineStr;
                if (!highlightMatchCase) {
                    for (auto& ch : qLower) ch = towlower(ch);
                    for (auto& ch : lLower) ch = towlower(ch);
                }
                const std::wstring& target = highlightMatchCase ? lineStr : lLower;
                const std::wstring& pattern = highlightMatchCase ? highlightQuery : qLower;
                size_t pos = 0;
                while ((pos = target.find(pattern, pos)) != std::wstring::npos) {
                    size_t matchLen = pattern.length();
                    bool keep = true;
                    if (highlightWholeWord) {
                        bool leftOk = (pos == 0) || !iswalnum(lineStr[pos - 1]);
                        bool rightOk = (pos + matchLen >= lineStr.length()) || !iswalnum(lineStr[pos + matchLen]);
                        if (!leftOk || !rightOk) keep = false;
                    }
                    if (keep) {
                        matches.push_back({pos, matchLen});
                    }
                    pos += (std::max)(size_t(1), matchLen);
                }
            }

            if (!matches.empty()) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.58f, 0.12f, 0.38f), &fillBrush);
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.58f, 0.12f, 0.85f), &borderBrush);

                for (const auto& m : matches) {
                    float hx = codeLeft + m.first * charW;
                    float hw = m.second * charW;
                    D2D1_RECT_F rcMatch = D2D1::RectF(hx - 1.0f, lineY + 1.0f, hx + hw + 1.0f, lineY + lineH - 1.0f);
                    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcMatch, 2.0f, 2.0f);
                    if (fillBrush) pRT->FillRoundedRectangle(&rrc, fillBrush.Get());
                    if (borderBrush) pRT->DrawRoundedRectangle(&rrc, borderBrush.Get(), 1.0f);
                }
            }
        }

        // Discover in-context color tokens on line to compute visual column layout
        struct LineColorSpan {
            size_t col;
            size_t len;
            std::wstring colorStr;
            D2D1_COLOR_F color;
            ColorFormat format;
            bool hasAlpha;
        };
        std::vector<LineColorSpan> lineColors;
        size_t scanCIdx = 0;
        while (scanCIdx < lineStr.length()) {
            if (lineStr[scanCIdx] == L'#') {
                size_t hStart = scanCIdx;
                size_t hEnd = hStart + 1;
                while (hEnd < lineStr.length() && iswxdigit(lineStr[hEnd])) hEnd++;
                size_t hLen = hEnd - hStart - 1;
                if (hLen == 3 || hLen == 4 || hLen == 6 || hLen == 8) {
                    std::wstring colorStr = lineStr.substr(hStart, hEnd - hStart);
                    D2D1_COLOR_F c{};
                    ColorFormat fmt = ColorFormat::Hex;
                    bool hasAlpha = false;
                    if (ColorPickerModal::TryParseColor(colorStr, c, fmt, hasAlpha)) {
                        lineColors.push_back({ hStart, colorStr.length(), colorStr, c, fmt, hasAlpha });
                    }
                }
                scanCIdx = hEnd;
                continue;
            }
            if (lineStr.compare(scanCIdx, 4, L"rgb(") == 0 || lineStr.compare(scanCIdx, 5, L"rgba(") == 0 ||
                lineStr.compare(scanCIdx, 4, L"hsl(") == 0 || lineStr.compare(scanCIdx, 5, L"hsla(") == 0) {
                size_t closeParen = lineStr.find(L')', scanCIdx);
                if (closeParen != std::wstring::npos) {
                    std::wstring colorStr = lineStr.substr(scanCIdx, closeParen - scanCIdx + 1);
                    D2D1_COLOR_F c{};
                    ColorFormat fmt = ColorFormat::Hex;
                    bool hasAlpha = false;
                    if (ColorPickerModal::TryParseColor(colorStr, c, fmt, hasAlpha)) {
                        lineColors.push_back({ scanCIdx, colorStr.length(), colorStr, c, fmt, hasAlpha });
                    }
                    scanCIdx = closeParen + 1;
                    continue;
                }
            }
            scanCIdx++;
        }

        const float swatchSpan = config.ScaleF(14.0f);
        const float swatchSize = config.ScaleF(10.5f);

        auto getColVisualX = [&](size_t col) -> float {
            size_t swBefore = 0;
            for (const auto& cs : lineColors) {
                if (col >= cs.col) swBefore++;
            }
            return codeLeft - scrollX + static_cast<float>(col) * charW + static_cast<float>(swBefore) * swatchSpan;
        };

        // Active Text Selection Background on Code Line (Images 3 & 4)
        if (doc.HasSelection()) {
            size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
            doc.GetSelectionRange(sLine, sCol, eLine, eCol);
            if (vl.bufferLine >= sLine && vl.bufferLine <= eLine) {
                float selX1 = codeLeft - scrollX;
                float selX2 = codeLeft - scrollX;
                if (vl.bufferLine == sLine && vl.bufferLine == eLine) {
                    selX1 = getColVisualX(sCol);
                    selX2 = getColVisualX(eCol);
                } else if (vl.bufferLine == sLine) {
                    selX1 = getColVisualX(sCol);
                    selX2 = getColVisualX(lineStr.length()) + config.ScaleF(8.0f);
                } else if (vl.bufferLine == eLine) {
                    selX1 = codeLeft - scrollX;
                    selX2 = getColVisualX(eCol);
                } else {
                    selX1 = codeLeft - scrollX;
                    selX2 = (std::max)(getColVisualX(lineStr.length()) + config.ScaleF(8.0f), codeLeft - scrollX + config.ScaleF(24.0f));
                }

                if (selX2 > selX1) {
                    D2D1_RECT_F rcSel = D2D1::RectF(selX1, lineY + 0.5f, selX2, lineY + lineH - 0.5f);
                    D2D1_ROUNDED_RECT rrcSel = D2D1::RoundedRect(rcSel, 4.0f, 4.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.38f, 0.68f, 0.48f), &selBrush);
                    if (selBrush) pRT->FillRoundedRectangle(&rrcSel, selBrush.Get());
                }
            }
        }

        // Matching Word Occurrences Highlight across lines (Image 4)
        if (!doc.selectedWord.empty() && doc.selectedWord.length() > 1 && !lineStr.empty()) {
            size_t wpos = 0;
            while ((wpos = lineStr.find(doc.selectedWord, wpos)) != std::wstring::npos) {
                bool isCurrentSelection = false;
                if (doc.HasSelection()) {
                    size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                    doc.GetSelectionRange(sLine, sCol, eLine, eCol);
                    if (vl.bufferLine == sLine && vl.bufferLine == eLine && wpos == sCol) {
                        isCurrentSelection = true;
                    }
                }
                if (!isCurrentSelection) {
                    float wx = getColVisualX(wpos);
                    float ww = getColVisualX(wpos + doc.selectedWord.length()) - wx;
                    D2D1_RECT_F rcWord = D2D1::RectF(wx - 1.0f, lineY + 1.0f, wx + ww + 1.0f, lineY + lineH - 1.0f);
                    D2D1_ROUNDED_RECT rrcWord = D2D1::RoundedRect(rcWord, 2.0f, 2.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wordFill;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wordBorder;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.48f, 0.75f, 0.22f), &wordFill);
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.65f, 0.95f, 0.65f), &wordBorder);
                    if (wordFill) pRT->FillRoundedRectangle(&rrcWord, wordFill.Get());
                    if (wordBorder) pRT->DrawRoundedRectangle(&rrcWord, wordBorder.Get(), 1.0f);
                }
                wpos += doc.selectedWord.length();
            }
        }

        if (!lineStr.empty()) {
            // Render whitespace indicators if toggled on in View > Appearance
            if (config.renderWhitespace) {
                for (size_t col = 0; col < lineStr.length(); ++col) {
                    wchar_t ch = lineStr[col];
                    if (ch == L' ') {
                        float dotX = getColVisualX(col) + charW * 0.5f;
                        float dotY = lineY + lineH * 0.5f;
                        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), 1.0f, 1.0f), theme.brushTextMuted.Get());
                    } else if (ch == L'\t') {
                        float arrX = getColVisualX(col) + charW * 0.5f;
                        float arrY = lineY + lineH * 0.5f;
                        pRT->DrawLine(D2D1::Point2F(arrX - 2.0f, arrY), D2D1::Point2F(arrX + 2.0f, arrY), theme.brushTextMuted.Get(), 1.0f);
                    }
                }
            }

            auto tokens = buffer.GetTokensForLine(vl.bufferLine);
            if (tokens.empty()) {
                D2D1_RECT_F rcLine = D2D1::RectF(codeLeft - scrollX, lineY, codeRight, lineY + lineH);
                pRT->DrawText(
                    lineStr.c_str(),
                    static_cast<UINT32>(lineStr.length()),
                    theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                    rcLine,
                    theme.brushTextPrimary.Get()
                );
                curCodeEndX = getColVisualX(lineStr.length());
            } else {
                for (const auto& tok : tokens) {
                    if (tok.startCol + tok.length > lineStr.length()) continue;

                    float tokenX = getColVisualX(tok.startCol);
                    float tokenW = getColVisualX(tok.startCol + tok.length) - tokenX;
                    D2D1_RECT_F rcTok = D2D1::RectF(tokenX, lineY, tokenX + tokenW + config.ScaleF(4.0f), lineY + lineH);

                    ID2D1Brush* brush = theme.brushTextPrimary.Get();
                    bool isBracketToken = false;
                    if (tok.type == editor::TokenType::Operator && tok.length == 1) {
                        auto it = bracketsByLine.find(vl.bufferLine);
                        if (it != bracketsByLine.end()) {
                            for (const auto* b : it->second) {
                                if (b->col == tok.startCol) {
                                    brush = theme.GetBracketBrush(b->depth);
                                    isBracketToken = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (!isBracketToken) {
                        switch (tok.type) {
                            case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
                            case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
                            case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
                            case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
                            case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
                            case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
                            case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
                            case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
                            default: brush = theme.brushTextPrimary.Get(); break;
                        }
                    }

                    pRT->DrawText(
                        lineStr.c_str() + tok.startCol,
                        static_cast<UINT32>(tok.length),
                        theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                        rcTok,
                        brush
                    );
                    curCodeEndX = (std::max)(curCodeEndX, tokenX + tokenW);
                }
            }
        }

        // Render in-context Color Preview Swatches inside dedicated gap (before color code, never replacing characters)
        for (size_t sIdx = 0; sIdx < lineColors.size(); ++sIdx) {
            const auto& cs = lineColors[sIdx];
            float swatchX = codeLeft - scrollX + static_cast<float>(cs.col) * charW + static_cast<float>(sIdx) * swatchSpan + config.ScaleF(1.0f);
            float swatchY = lineY + (lineH - swatchSize) / 2.0f;
            if (swatchX >= codeLeft - swatchSize && swatchX + swatchSize < codeRight) {
                D2D1_RECT_F rcSwatch = D2D1::RectF(swatchX, swatchY, swatchX + swatchSize, swatchY + swatchSize);
                D2D1_ROUNDED_RECT rrcSwatch = D2D1::RoundedRect(rcSwatch, 1.5f, 1.5f);

                if (cs.hasAlpha && cs.color.a < 0.999f) {
                    float half = swatchSize / 2.0f;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bChk;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0x333333), &bChk);
                    if (bChk) {
                        pRT->FillRectangle(D2D1::RectF(swatchX, swatchY, swatchX + half, swatchY + half), bChk.Get());
                        pRT->FillRectangle(D2D1::RectF(swatchX + half, swatchY + half, swatchX + swatchSize, swatchY + swatchSize), bChk.Get());
                        bChk->SetColor(D2D1::ColorF(0x888888));
                        pRT->FillRectangle(D2D1::RectF(swatchX + half, swatchY, swatchX + swatchSize, swatchY + half), bChk.Get());
                        pRT->FillRectangle(D2D1::RectF(swatchX, swatchY + half, swatchX + half, swatchY + swatchSize), bChk.Get());
                    }
                }

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bCol;
                pRT->CreateSolidColorBrush(cs.color, &bCol);
                if (bCol) pRT->FillRoundedRectangle(rrcSwatch, bCol.Get());
                pRT->DrawRoundedRectangle(rrcSwatch, theme.brushBorderSubtle.Get(), 1.0f);

                m_visibleColorSwatches.push_back({ rcSwatch, vl.bufferLine, cs.col, cs.len, cs.colorStr });
            }
        }

        // Track max visual width for horizontal scrolling
        float lineVisualW = getColVisualX(lineStr.length()) - (codeLeft - scrollX);
        maxLineWidth = (std::max)(maxLineWidth, lineVisualW);

        // If line is folded, draw the "..." folded indicator pill (Image 3)
        if (vl.isFolded) {
            float pillX = curCodeEndX + config.ScaleF(8.0f);
            float pillW = config.ScaleF(24.0f);
            float pillH = config.ScaleF(16.0f);
            float pillY = lineY + (lineH - pillH) / 2.0f;
            D2D1_RECT_F rcPill = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);
            D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, 3.0f, 3.0f);

            pRT->FillRoundedRectangle(rrcPill, theme.brushItemHover.Get());
            pRT->DrawRoundedRectangle(rrcPill, theme.brushBorderSubtle.Get(), 0.8f);

            pRT->DrawText(L"...", 3, theme.fontMenuShortcut.Get(), rcPill, theme.brushTextMuted.Get());
        }

        // =====================================================================
        // Inlay Hints & Virtual Type Badges (Images 1, 2, 3, 5)
        // Renders non-editable virtual type/parameter hints without modifying buffer text.
        // =====================================================================
        bool renderedHint = false;
        for (const auto& hint : doc.inlayHints) {
            if (hint.line == vl.bufferLine) {
                float hintX = (hint.col > 0) ? getColVisualX(hint.col) : (curCodeEndX + config.ScaleF(6.0f));
                if (hintX + config.ScaleF(20.0f) < codeRight) {
                    if (hint.isGhostText) {
                        D2D1_RECT_F rcGhost = D2D1::RectF(hintX, lineY, codeRight, lineY + lineH);
                        pRT->DrawText(hint.label.c_str(), static_cast<UINT32>(hint.label.length()),
                                      theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                                      rcGhost, theme.brushTextMuted.Get());
                    } else {
                        float badgeW = static_cast<float>(hint.label.length()) * charW * 0.88f + config.ScaleF(8.0f);
                        float badgeH = lineH - config.ScaleF(5.0f);
                        float badgeY = lineY + config.ScaleF(2.5f);
                        D2D1_RECT_F rcBadge = D2D1::RectF(hintX, badgeY, (std::min)(hintX + badgeW, codeRight), badgeY + badgeH);
                        D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcBadge, 3.0f, 3.0f);

                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBgBrush;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBorderBrush;
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.26f, 0.32f, 0.45f) : D2D1::ColorF(0.88f, 0.90f, 0.94f, 0.65f), &badgeBgBrush);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.40f, 0.44f, 0.52f, 0.55f) : D2D1::ColorF(0.70f, 0.74f, 0.80f, 0.75f), &badgeBorderBrush);
                        if (badgeBgBrush) pRT->FillRoundedRectangle(rrcBadge, badgeBgBrush.Get());
                        if (badgeBorderBrush) pRT->DrawRoundedRectangle(rrcBadge, badgeBorderBrush.Get(), 0.8f);

                        D2D1_RECT_F rcBadgeText = D2D1::RectF(hintX + config.ScaleF(4.0f), badgeY + config.ScaleF(1.0f), rcBadge.right - config.ScaleF(4.0f), badgeY + badgeH);
                        pRT->DrawText(hint.label.c_str(), static_cast<UINT32>(hint.label.length()),
                                      theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                                      rcBadgeText, theme.brushTextSecondary.Get());
                    }
                    renderedHint = true;
                }
            }
        }

        // Render Syntax Error Squiggly Underlines (Image 4)
        for (const auto& diag : doc.diagnostics) {
            if (diag.line == vl.bufferLine) {
                float errX1 = getColVisualX(diag.col);
                size_t errLen = (diag.length > 0) ? diag.length : (std::max)(1ULL, lineStr.length() > diag.col ? lineStr.length() - diag.col : 1);
                float errX2 = (std::max)(getColVisualX(diag.col + errLen), errX1 + config.ScaleF(14.0f));
                if (errX1 < codeRight) {
                    errX2 = (std::min)(errX2, codeRight);
                    float waveY = lineY + lineH - config.ScaleF(2.0f);
                    float waveH = config.ScaleF(2.0f);
                    float step = config.ScaleF(2.5f);
                    ID2D1Brush* pErrBrush = (diag.severity == 1) ?
                        (theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get()) :
                        (theme.brushIconOrange ? theme.brushIconOrange.Get() : theme.brushSyntaxNumber.Get());
                    for (float wx = errX1; wx < errX2; wx += step * 2.0f) {
                        pRT->DrawLine(D2D1::Point2F(wx, waveY), D2D1::Point2F(wx + step, waveY - waveH), pErrBrush, 1.3f);
                        pRT->DrawLine(D2D1::Point2F(wx + step, waveY - waveH), D2D1::Point2F((std::min)(wx + step * 2.0f, errX2), waveY), pErrBrush, 1.3f);
                    }
                }
            }
        }
    }

    // 4. Blinking Caret (Image 5: High-contrast in both Dark and Light modes!) - Only in focused group
    if (foundCursorVisual && visualCursorIdx >= doc.scrollLine) {
        float caretX = codeLeft - scrollX + cursor.col * charW;
        if (cursor.line < buffer.GetLineCount()) {
            const auto& curLineStr = buffer.GetLine(cursor.line);
            size_t swBefore = 0;
            size_t cIdx = 0;
            while (cIdx < curLineStr.length() && cIdx <= cursor.col) {
                if (curLineStr[cIdx] == L'#') {
                    size_t e = cIdx + 1;
                    while (e < curLineStr.length() && iswxdigit(curLineStr[e])) e++;
                    size_t l = e - cIdx - 1;
                    if (l == 3 || l == 4 || l == 6 || l == 8) {
                        D2D1_COLOR_F c{}; ColorFormat f; bool a;
                        if (ColorPickerModal::TryParseColor(curLineStr.substr(cIdx, e - cIdx), c, f, a)) {
                            if (cursor.col >= cIdx) swBefore++;
                        }
                    }
                    cIdx = e; continue;
                }
                cIdx++;
            }
            caretX = codeLeft - scrollX + static_cast<float>(cursor.col) * charW + static_cast<float>(swBefore) * swatchSpan;
        }
        float caretY = topY + config.ScaleF(6.0f) + (visualCursorIdx - doc.scrollLine) * lineH;
        if (isFocused) {
            m_lastCaretX = caretX;
            m_lastCaretY = caretY;
        }
        if (isFocused && !isRenaming && m_caretVisible) {
            if (caretY >= topY && caretY + lineH <= clientHeight) {
                // In Light Mode: #1E1E1E Deep Black/Charcoal. In Dark Mode: #FFFFFF Pure White.
                ID2D1Brush* caretBrush = theme.IsDarkMode()
                    ? theme.brushTextWhite.Get()
                    : (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushHeaderIcon.Get());

                pRT->DrawLine(
                    D2D1::Point2F(caretX, caretY + config.ScaleF(2.0f)),
                    D2D1::Point2F(caretX, caretY + lineH - config.ScaleF(2.0f)),
                    caretBrush,
                    2.0f // Crisp 2.0px stroke
                );
            }
        }

        // 4b. Inline Ghost Text with "Tab to complete" (Image 5)
        std::wstring displayGhost = m_ghostText;

        // ONLY allow inline ghost text if cursor is at the end of the line (no non-whitespace chars follow).
        // This strictly prevents ghost text from double-rendering over existing code (e.g. width=ddevvicee--wwiiddtthh).
        const auto& lineText = buffer.GetLine(cursor.line);
        bool isAtEndOfLine = true;
        for (size_t col = cursor.col; col < lineText.length(); ++col) {
            if (lineText[col] != L' ' && lineText[col] != L'\t' && lineText[col] != L'\r' && lineText[col] != L'\n') {
                isAtEndOfLine = false;
                break;
            }
        }

        if (!isAtEndOfLine) {
            displayGhost.clear();
        }

        if (isFocused && !displayGhost.empty() && caretX + config.ScaleF(40.0f) < codeRight) {
            float ghostX = caretX + config.ScaleF(2.0f);
            float ghostW = static_cast<float>(displayGhost.length()) * charW;
            D2D1_RECT_F rcGhost = D2D1::RectF(ghostX, caretY, (std::min)(ghostX + ghostW, codeRight), caretY + lineH);

            pRT->DrawText(
                displayGhost.c_str(),
                static_cast<UINT32>(displayGhost.length()),
                theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                rcGhost,
                theme.brushTextMuted.Get()
            );
        }
    }


    pRT->PopAxisAlignedClip();

    // 5. Code Minimap (Image 5: scaled miniature code preview with syntax highlighting, search ticks, and viewport slider)
    if (config.showMinimap && !doc.isSpecialView && !doc.isImageView && !doc.isUnsupportedBinary) {
        doc.minimapTrack = D2D1::RectF(canvasRight - minimapW, topY, canvasRight, clientHeight);

        // Minimap background with subtle left border
        D2D1_COLOR_F miniBg = theme.IsDarkMode() ? D2D1::ColorF(0.11f, 0.11f, 0.12f, 0.85f) : D2D1::ColorF(0.96f, 0.96f, 0.97f, 0.85f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> miniBgBrush;
        pRT->CreateSolidColorBrush(miniBg, &miniBgBrush);
        if (miniBgBrush) pRT->FillRectangle(doc.minimapTrack, miniBgBrush.Get());

        pRT->DrawLine(
            D2D1::Point2F(doc.minimapTrack.left, topY),
            D2D1::Point2F(doc.minimapTrack.left, clientHeight),
            theme.brushBorderSubtle.Get(),
            1.0f
        );

        pRT->PushAxisAlignedClip(doc.minimapTrack, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        size_t totalLines = buffer.GetLineCount();
        float miniH = (std::max)(10.0f, clientHeight - topY);
        float stepY = (totalLines * 2.4f > miniH && totalLines > 0) ? (miniH / static_cast<float>(totalLines)) : 2.4f;
        float microH = (std::max)(0.6f, stepY * 0.85f);
        float microCharW = (std::max)(0.5f, stepY * 0.7f);

        // Render micro-lines with syntax colors, selection reflection, and word occurrences
        for (size_t b = 0; b < totalLines; ++b) {
            float my = topY + static_cast<float>(b) * stepY;
            if (my + microH > clientHeight) break;

            const auto& lineStr = buffer.GetLine(b);

            // Active text selection reflection on minimap (Images 3 & 4)
            if (doc.HasSelection()) {
                size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                doc.GetSelectionRange(sLine, sCol, eLine, eCol);
                if (b >= sLine && b <= eLine) {
                    D2D1_RECT_F rcSelMini = D2D1::RectF(
                        doc.minimapTrack.left + 2.0f,
                        my,
                        doc.minimapTrack.right - 2.0f,
                        my + (std::max)(microH, stepY)
                    );
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selMiniBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.85f, 0.45f), &selMiniBrush);
                    if (selMiniBrush) pRT->FillRectangle(rcSelMini, selMiniBrush.Get());
                }
            }

            // Active Caret Line Indicator on minimap (Image 1)
            // Displays a bright blue horizontal indicator line spanning across the minimap track
            if (cursor.line == b) {
                D2D1_RECT_F rcCursorMini = D2D1::RectF(
                    doc.minimapTrack.left + 1.0f,
                    my,
                    doc.minimapTrack.right - 1.0f,
                    my + (std::max)(2.5f, microH + 1.0f)
                );
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cursorMiniBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.00f, 0.48f, 1.00f, 0.95f), &cursorMiniBrush);
                if (cursorMiniBrush) pRT->FillRectangle(rcCursorMini, cursorMiniBrush.Get());
            }

            // Target search jump highlight banner on minimap
            if (doc.targetHighlightLine == b) {
                D2D1_RECT_F rcTargetMini = D2D1::RectF(
                    doc.minimapTrack.left + 1.0f,
                    my - 1.0f,
                    doc.minimapTrack.right - 1.0f,
                    my + microH + 1.0f
                );
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> targetMiniBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.82f, 0.0f, 0.65f), &targetMiniBrush);
                if (targetMiniBrush) pRT->FillRectangle(rcTargetMini, targetMiniBrush.Get());
            }

            // Diagnostic syntax error markers on minimap (Image 4)
            for (const auto& diag : doc.diagnostics) {
                if (diag.line == b) {
                    D2D1_RECT_F rcErrMini = D2D1::RectF(
                        doc.minimapTrack.left + 1.0f,
                        my - 0.5f,
                        doc.minimapTrack.right - 1.0f,
                        my + (std::max)(microH + 1.5f, stepY)
                    );
                    ID2D1Brush* pErrBrush = (diag.severity == 1) ?
                        (theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get()) :
                        (theme.brushIconOrange ? theme.brushIconOrange.Get() : theme.brushSyntaxNumber.Get());
                    pRT->FillRectangle(rcErrMini, pErrBrush);
                    break;
                }
            }

            if (lineStr.empty()) continue;

            size_t indent = lineStr.find_first_not_of(L" \t");
            if (indent == std::wstring::npos) continue;

            auto tokens = buffer.GetTokensForLine(b);
            if (!tokens.empty()) {
                for (const auto& tok : tokens) {
                    if (tok.startCol + tok.length > lineStr.length()) continue;
                    float tx = doc.minimapTrack.left + config.ScaleF(5.0f) + static_cast<float>(tok.startCol) * microCharW;
                    float tw = (std::min)(doc.minimapTrack.right - tx - 2.0f, static_cast<float>(tok.length) * microCharW);
                    if (tw <= 0.5f) continue;

                    ID2D1Brush* brush = theme.brushTextMuted.Get();
                    switch (tok.type) {
                        case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
                        case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
                        case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
                        case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
                        case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
                        case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
                        case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
                        case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
                        default: brush = theme.brushTextPrimary.Get(); break;
                    }
                    pRT->FillRectangle(D2D1::RectF(tx, my, tx + tw, my + microH), brush);
                }
            } else {
                float mx = doc.minimapTrack.left + config.ScaleF(5.0f) + static_cast<float>(indent) * microCharW;
                float tw = (std::min)(doc.minimapTrack.right - mx - 2.0f, static_cast<float>(lineStr.length() - indent) * microCharW);
                if (tw > 0.5f) {
                    pRT->FillRectangle(D2D1::RectF(mx, my, mx + tw, my + microH), theme.brushTextMuted.Get());
                }
            }

            // Highlight matching word occurrences on minimap (Image 4)
            if (!doc.selectedWord.empty() && doc.selectedWord.length() > 1) {
                if (lineStr.find(doc.selectedWord) != std::wstring::npos) {
                    D2D1_RECT_F rcWordTick = D2D1::RectF(
                        doc.minimapTrack.right - config.ScaleF(12.0f),
                        my,
                        doc.minimapTrack.right - config.ScaleF(2.0f),
                        my + (std::max)(2.0f, microH)
                    );
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wordTickBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.70f, 1.0f, 0.90f), &wordTickBrush);
                    if (wordTickBrush) pRT->FillRectangle(rcWordTick, wordTickBrush.Get());
                }
            }

            // Search highlight tick on right side of minimap line
            if (!highlightQuery.empty()) {
                std::wstring qLower = highlightQuery;
                std::wstring lLower = lineStr;
                if (!highlightMatchCase) {
                    for (auto& ch : qLower) ch = towlower(ch);
                    for (auto& ch : lLower) ch = towlower(ch);
                }
                if (lLower.find(qLower) != std::wstring::npos) {
                    D2D1_RECT_F rcTick = D2D1::RectF(
                        doc.minimapTrack.right - config.ScaleF(8.0f),
                        my,
                        doc.minimapTrack.right - config.ScaleF(1.0f),
                        my + (std::max)(2.0f, microH)
                    );
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hlTickBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.58f, 0.12f, 0.90f), &hlTickBrush);
                    if (hlTickBrush) pRT->FillRectangle(rcTick, hlTickBrush.Get());
                }
            }
        }

        // Viewport Slider Box with smooth modern translucency (Images 3 & 4)
        float sliderH = (maxScroll > 0)
            ? (std::max)(config.ScaleF(24.0f), (static_cast<float>(visibleCount) / static_cast<float>(visualLines.size() + visibleCount)) * miniH)
            : miniH;
        if (sliderH > miniH) sliderH = miniH;

        float sliderTop = topY;
        if (maxScroll > 0) {
            float scrollRatio = (std::max)(0.0f, (std::min)(1.0f, static_cast<float>(doc.scrollLine) / static_cast<float>(maxScroll)));
            sliderTop = topY + scrollRatio * (miniH - sliderH);
        }
        float sliderBottom = (std::min)(topY + miniH, sliderTop + sliderH);

        doc.minimapSlider = D2D1::RectF(
            doc.minimapTrack.left + 1.0f,
            sliderTop,
            doc.minimapTrack.right - 1.0f,
            sliderBottom
        );

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sliderBgBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sliderBorderBrush;
        float sliderAlpha = doc.isDraggingMinimap ? 0.32f : 0.18f;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, sliderAlpha), &sliderBgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, sliderAlpha + 0.22f), &sliderBorderBrush);

        D2D1_ROUNDED_RECT rrcSlider = D2D1::RoundedRect(doc.minimapSlider, 2.0f, 2.0f);
        if (sliderBgBrush) pRT->FillRoundedRectangle(&rrcSlider, sliderBgBrush.Get());
        if (sliderBorderBrush) pRT->DrawRoundedRectangle(&rrcSlider, sliderBorderBrush.Get(), 1.0f);

        pRT->PopAxisAlignedClip();
    } else {
        doc.minimapTrack = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        doc.minimapSlider = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);

        // 6. Vertical Scrollbar on Right Edge when Minimap is disabled
        doc.scrollbarTrack = D2D1::RectF(canvasRight - scrollbarW, topY, canvasRight, clientHeight);
        if (maxScroll > 0) {
            float thumbH = (std::max)(config.ScaleF(28.0f), codeAreaH * (static_cast<float>(visibleCount) / (visualLines.size() + visibleCount)));
            float scrollRange = codeAreaH - thumbH;
            float scrollFrac = (maxScroll > 0)
                ? (static_cast<float>(doc.scrollLine) / static_cast<float>(maxScroll))
                : 0.0f;
            float thumbY = topY + scrollRange * scrollFrac;

            doc.scrollbarThumb = D2D1::RectF(
                canvasRight - scrollbarW + config.ScaleF(3.0f),
                thumbY,
                canvasRight - config.ScaleF(3.0f),
                thumbY + thumbH
            );

            D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(doc.scrollbarThumb, 3.5f, 3.5f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushThumb;
            if (theme.IsDarkMode()) {
                float alpha = doc.isDraggingScrollbar ? 0.85f : 0.50f;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.40f, 0.44f, alpha), &brushThumb);
            } else {
                float alpha = doc.isDraggingScrollbar ? 0.85f : 0.50f;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.60f, alpha), &brushThumb);
            }
            if (brushThumb) {
                pRT->FillRoundedRectangle(rrcThumb, brushThumb.Get());
            }
        } else {
            doc.scrollbarThumb = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    // 6b. Horizontal Scrollbar when content overflows horizontally
    doc.maxContentWidth = maxLineWidth;
    float viewportCodeW = codeRight - (canvasLeft + gutterW);
    float maxScrollX = (doc.maxContentWidth > viewportCodeW) ? (doc.maxContentWidth - viewportCodeW + config.ScaleF(40.0f)) : 0.0f;
    if (maxScrollX > 0.0f) {
        float hTrackH = config.ScaleF(10.0f);
        float hTrackY1 = clientHeight - hTrackH;
        float hTrackY2 = clientHeight - config.ScaleF(2.0f);
        float hTrackX1 = canvasLeft + gutterW;
        float hTrackX2 = codeRight;

        doc.hScrollbarTrack = D2D1::RectF(hTrackX1, hTrackY1, hTrackX2, hTrackY2);
        float trackW = hTrackX2 - hTrackX1;
        float thumbW = (std::max)(config.ScaleF(32.0f), (viewportCodeW / (doc.maxContentWidth + config.ScaleF(40.0f))) * trackW);
        float thumbMaxTravel = trackW - thumbW;
        float thumbX = hTrackX1 + (maxScrollX > 0.0f ? ((std::clamp)(doc.scrollOffsetX, 0.0f, maxScrollX) / maxScrollX) * thumbMaxTravel : 0.0f);
        doc.hScrollbarThumb = D2D1::RectF(thumbX, hTrackY1, thumbX + thumbW, hTrackY2);

        D2D1_ROUNDED_RECT rrcHThumb = D2D1::RoundedRect(doc.hScrollbarThumb, 3.5f, 3.5f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHThumb;
        float alpha = doc.isDraggingHScrollbar ? 0.85f : 0.50f;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.40f, 0.40f, 0.44f, alpha) : D2D1::ColorF(0.55f, 0.55f, 0.60f, alpha), &brushHThumb);
        if (brushHThumb) {
            pRT->FillRoundedRectangle(rrcHThumb, brushHThumb.Get());
        }
    } else {
        doc.hScrollbarTrack = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        doc.hScrollbarThumb = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // 7. Diagnostic Error Hover Popover (VS Code / Antigravity style)
    if (doc.hoveredDiagnosticLine != static_cast<size_t>(-1)) {
        const lsp::Diagnostic* pDiag = nullptr;
        for (const auto& d : doc.diagnostics) {
            if (d.line == doc.hoveredDiagnosticLine) {
                pDiag = &d;
                break;
            }
        }
        if (pDiag && pDiag->line >= doc.scrollLine) {
            float popY = topY + config.ScaleF(6.0f) + (pDiag->line - doc.scrollLine + 1) * lineH + config.ScaleF(2.0f);
            float popX = codeLeft + config.ScaleF(20.0f);
            float popW = config.ScaleF(420.0f);
            float popH = config.ScaleF(46.0f);
            if (popY + popH > clientHeight - config.ScaleF(10.0f)) {
                popY = topY + config.ScaleF(6.0f) + (pDiag->line - doc.scrollLine) * lineH - popH - config.ScaleF(2.0f);
            }
            D2D1_RECT_F rcCard = D2D1::RectF(popX, popY, popX + popW, popY + popH);
            D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 4.0f, 4.0f);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBorder;
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.14f, 0.16f, 0.98f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), &cardBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.28f, 0.28f, 1.0f), &cardBorder);
            if (cardBg) pRT->FillRoundedRectangle(&rrcCard, cardBg.Get());
            if (cardBorder) pRT->DrawRoundedRectangle(&rrcCard, cardBorder.Get(), 1.2f);

            // Red Error badge circle (x)
            float badgeX = popX + config.ScaleF(14.0f);
            float badgeY = popY + config.ScaleF(16.0f);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(badgeX, badgeY), config.ScaleF(6.0f), config.ScaleF(6.0f)),
                             theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get());
            // Draw 'x' inside badge
            float xr = config.ScaleF(2.5f);
            pRT->DrawLine(D2D1::Point2F(badgeX - xr, badgeY - xr), D2D1::Point2F(badgeX + xr, badgeY + xr), theme.brushTextWhite.Get(), 1.2f);
            pRT->DrawLine(D2D1::Point2F(badgeX + xr, badgeY - xr), D2D1::Point2F(badgeX - xr, badgeY + xr), theme.brushTextWhite.Get(), 1.2f);

            // Message text
            D2D1_RECT_F rcMsg = D2D1::RectF(popX + config.ScaleF(26.0f), popY + config.ScaleF(6.0f), popX + popW - config.ScaleF(10.0f), popY + config.ScaleF(24.0f));
            pRT->DrawText(pDiag->message.c_str(), static_cast<UINT32>(pDiag->message.length()), theme.fontBody.Get(), rcMsg, theme.brushTextPrimary.Get());

            // Source line/col info
            std::wstring locStr = doc.fileName + L" (" + std::to_wstring(pDiag->line + 1) + L":" + std::to_wstring(pDiag->col + 1) + L")";
            D2D1_RECT_F rcLoc = D2D1::RectF(popX + config.ScaleF(26.0f), popY + config.ScaleF(24.0f), popX + popW - config.ScaleF(10.0f), popY + popH - config.ScaleF(4.0f));
            pRT->DrawText(locStr.c_str(), static_cast<UINT32>(locStr.length()), theme.fontMenuShortcut.Get(), rcLoc, theme.brushTextMuted.Get());
        }
    }
}

int EditorCanvas::HitTestTab(POINT pt, const std::vector<editor::OpenDocument>& docs) const {
    for (size_t i = 0; i < docs.size(); ++i) {
        const auto& b = docs[i].tabBounds;
        if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int EditorCanvas::HitTestTabClose(POINT pt, const std::vector<editor::OpenDocument>& docs) const {
    for (size_t i = 0; i < docs.size(); ++i) {
        const auto& b = docs[i].closeBounds;
        if (pt.x >= b.left - 4 && pt.x <= b.right + 4 && pt.y >= b.top - 4 && pt.y <= b.bottom + 4) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool EditorCanvas::HitTestScrollbar(POINT pt, const editor::OpenDocument& doc, bool& outHitThumb) const {
    outHitThumb = false;
    if (doc.scrollbarTrack.right > doc.scrollbarTrack.left) {
        if (pt.x >= doc.scrollbarTrack.left && pt.x <= doc.scrollbarTrack.right &&
            pt.y >= doc.scrollbarTrack.top && pt.y <= doc.scrollbarTrack.bottom) {
            if (pt.x >= doc.scrollbarThumb.left && pt.x <= doc.scrollbarThumb.right &&
                pt.y >= doc.scrollbarThumb.top && pt.y <= doc.scrollbarThumb.bottom) {
                outHitThumb = true;
            }
            return true;
        }
    }
    return false;
}

bool EditorCanvas::HitTestHScrollbar(POINT pt, const editor::OpenDocument& doc, bool& outHitThumb) const {
    outHitThumb = false;
    if (doc.hScrollbarTrack.right > doc.hScrollbarTrack.left) {
        float px = static_cast<float>(pt.x);
        float py = static_cast<float>(pt.y);
        if (px >= doc.hScrollbarTrack.left && px <= doc.hScrollbarTrack.right &&
            py >= doc.hScrollbarTrack.top && py <= doc.hScrollbarTrack.bottom) {
            if (px >= doc.hScrollbarThumb.left && px <= doc.hScrollbarThumb.right) {
                outHitThumb = true;
            }
            return true;
        }
    }
    return false;
}

bool EditorCanvas::HitTestMinimap(POINT pt, const editor::OpenDocument& doc, bool& outHitSlider) const {
    outHitSlider = false;
    if (doc.minimapTrack.right <= doc.minimapTrack.left) return false;
    if (pt.x >= doc.minimapTrack.left && pt.x <= doc.minimapTrack.right &&
        pt.y >= doc.minimapTrack.top && pt.y <= doc.minimapTrack.bottom) {
        if (pt.x >= doc.minimapSlider.left && pt.x <= doc.minimapSlider.right &&
            pt.y >= doc.minimapSlider.top && pt.y <= doc.minimapSlider.bottom) {
            outHitSlider = true;
        }
        return true;
    }
    return false;
}

int EditorCanvas::HitTestFoldChevron(POINT pt) const {
    for (const auto& hit : m_foldChevrons) {
        if (pt.x >= hit.bounds.left - 4 && pt.x <= hit.bounds.right + 4 &&
            pt.y >= hit.bounds.top - 2 && pt.y <= hit.bounds.bottom + 2) {
            return static_cast<int>(hit.bufferLine);
        }
    }
    return -1;
}

bool EditorCanvas::HitTestCodeArea(POINT pt, const app::AppConfig& config, const editor::OpenDocument& doc,
                                   float canvasLeft, float clientHeight, size_t& outLine, size_t& outCol) const {
    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float tabH    = config.ScaleF(34.0f);
    const float breadcrumbH = config.ScaleF(24.0f);
    const float topY    = headerH + tabH + breadcrumbH;
    const float gutterW = config.ScaleF(50.0f);
    const float lineH   = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));
    const float charW   = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);

    if (pt.x < canvasLeft + gutterW || pt.y < topY || pt.y > clientHeight) {
        return false;
    }

    float relY = pt.y - (topY + config.ScaleF(6.0f));
    if (relY < 0.0f) relY = 0.0f;
    size_t visualSlot = doc.scrollLine + static_cast<size_t>(relY / lineH);

    // Map visual slot back to buffer line taking fold state into account
    auto& buffer = doc.buffer;
    size_t totalBufLines = buffer.GetLineCount();
    size_t curVisual = 0;
    size_t mappedLine = 0;

    for (size_t b = 0; b < totalBufLines; ++b) {
        size_t foldEnd = 0;
        bool canFold = GetFoldRange(buffer, b, foldEnd);
        bool isFolded = canFold && (doc.foldedLines.count(b) > 0);

        if (curVisual == visualSlot) {
            mappedLine = b;
            break;
        }
        curVisual++;
        if (isFolded) {
            b = foldEnd;
        }
        mappedLine = b;
    }

    const auto& lineStr = buffer.GetLine(mappedLine);
    const float swatchSpan = config.ScaleF(14.0f);
    float relX = pt.x - (canvasLeft + gutterW + config.ScaleF(12.0f)) + doc.scrollOffsetX;
    if (relX < 0.0f) relX = 0.0f;

    size_t col = 0;
    float bestDiff = 999999.0f;
    for (size_t c = 0; c <= lineStr.length(); ++c) {
        size_t swBefore = 0;
        for (const auto& sw : m_visibleColorSwatches) {
            if (sw.line == mappedLine && c >= sw.col) swBefore++;
        }
        float vx = static_cast<float>(c) * charW + static_cast<float>(swBefore) * swatchSpan;
        float diff = std::abs(relX - vx);
        if (diff < bestDiff) {
            bestDiff = diff;
            col = c;
        }
    }

    if (mappedLine >= buffer.GetLineCount()) {
        mappedLine = buffer.GetLineCount() > 0 ? buffer.GetLineCount() - 1 : 0;
    }
    if (col > lineStr.length()) {
        col = lineStr.length();
    }

    outLine = mappedLine;
    outCol  = col;
    return true;
}

void EditorCanvas::RenderImageView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                   editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                   float topY, float clientHeight) {
    if (!pRT) return;

    // Load bitmap using WIC if not yet loaded
    if (!doc.pImageBitmap && !doc.filePath.empty()) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> pWicFactory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pWicFactory)
        );
        if (SUCCEEDED(hr) && pWicFactory) {
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
            hr = pWicFactory->CreateDecoderFromFilename(
                doc.filePath.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnDemand,
                &pDecoder
            );
            if (SUCCEEDED(hr) && pDecoder) {
                Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
                hr = pDecoder->GetFrame(0, &pFrame);
                if (SUCCEEDED(hr) && pFrame) {
                    pFrame->GetSize(&doc.imageWidth, &doc.imageHeight);

                    Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
                    hr = pWicFactory->CreateFormatConverter(&pConverter);
                    if (SUCCEEDED(hr) && pConverter) {
                        hr = pConverter->Initialize(
                            pFrame.Get(),
                            GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapDitherTypeNone,
                            nullptr,
                            0.0,
                            WICBitmapPaletteTypeCustom
                        );
                        if (SUCCEEDED(hr)) {
                            pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &doc.pImageBitmap);
                        }
                    }
                }
            }
        }
        if (doc.imageFileSize == 0 && !doc.filePath.empty()) {
            std::error_code ec;
            doc.imageFileSize = std::filesystem::file_size(doc.filePath, ec);
        }
    }

    const float availW = canvasWidth - config.ScaleF(80.0f);
    const float availH = (clientHeight - topY) - config.ScaleF(100.0f);

    if (doc.pImageBitmap && doc.imageWidth > 0 && doc.imageHeight > 0 && availW > 10.0f && availH > 10.0f) {
        float scale = 1.0f;
        if (static_cast<float>(doc.imageWidth) > availW || static_cast<float>(doc.imageHeight) > availH) {
            float scaleX = availW / static_cast<float>(doc.imageWidth);
            float scaleY = availH / static_cast<float>(doc.imageHeight);
            scale = (std::min)(scaleX, scaleY);
        }

        const float drawW = static_cast<float>(doc.imageWidth) * scale;
        const float drawH = static_cast<float>(doc.imageHeight) * scale;
        const float drawX = canvasLeft + (canvasWidth - drawW) / 2.0f;
        const float drawY = topY + ((clientHeight - topY) - drawH) / 2.0f - config.ScaleF(20.0f);

        D2D1_RECT_F imgRect = D2D1::RectF(drawX, drawY, drawX + drawW, drawY + drawH);

        // Subtle frame around image
        D2D1_RECT_F borderRect = D2D1::RectF(drawX - 1.0f, drawY - 1.0f, drawX + drawW + 1.0f, drawY + drawH + 1.0f);
        pRT->DrawRectangle(borderRect, theme.brushBorderSubtle.Get(), 1.0f);

        // Draw image bitmap
        pRT->DrawBitmap(doc.pImageBitmap.Get(), imgRect);

        // Format metadata info
        std::wstring ext;
        auto dotPos = doc.filePath.find_last_of(L".");
        if (dotPos != std::wstring::npos) {
            ext = doc.filePath.substr(dotPos + 1);
            for (auto& ch : ext) ch = towupper(ch);
        } else {
            ext = L"IMAGE";
        }

        std::wstring sizeStr;
        if (doc.imageFileSize < 1024) {
            sizeStr = std::to_wstring(doc.imageFileSize) + L" B";
        } else if (doc.imageFileSize < 1024 * 1024) {
            sizeStr = std::to_wstring(doc.imageFileSize / 1024) + L" KB";
        } else {
            sizeStr = std::to_wstring(doc.imageFileSize / (1024 * 1024)) + L" MB";
        }

        std::wstring infoText = std::to_wstring(doc.imageWidth) + L" \u00D7 " +
                                std::to_wstring(doc.imageHeight) + L" px   |   " +
                                ext + L"   |   " + sizeStr;

        const float badgeH = config.ScaleF(26.0f);
        const float badgeW = config.ScaleF(250.0f);
        const float badgeX = canvasLeft + (canvasWidth - badgeW) / 2.0f;
        const float badgeY = clientHeight - badgeH - config.ScaleF(16.0f);

        D2D1_ROUNDED_RECT rcBadge = D2D1::RoundedRect(
            D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH),
            config.ScaleF(13.0f), config.ScaleF(13.0f)
        );
        pRT->FillRoundedRectangle(rcBadge, theme.brushItemHover.Get());
        pRT->DrawRoundedRectangle(rcBadge, theme.brushBorderSubtle.Get(), 1.0f);

        D2D1_RECT_F rcText = rcBadge.rect;
        IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontCode.Get();
        if (pFmt) {
            DWRITE_TEXT_ALIGNMENT oldAlign = pFmt->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT oldPara = pFmt->GetParagraphAlignment();
            pFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(infoText.c_str(), static_cast<UINT32>(infoText.length()),
                          pFmt, rcText, theme.brushTextSecondary.Get());
            pFmt->SetTextAlignment(oldAlign);
            pFmt->SetParagraphAlignment(oldPara);
        }
    } else {
        std::wstring msg = L"Unable to display image: " + doc.fileName;
        D2D1_RECT_F rcMsg = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight);
        IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontCode.Get();
        if (pFmt) {
            pRT->DrawText(msg.c_str(), static_cast<UINT32>(msg.length()), pFmt, rcMsg, theme.brushTextMuted.Get());
        }
    }
}

/**
 * @brief Renders the unsupported binary file view (red circled X, center text, subheader) matching Image 2.
 */
void EditorCanvas::RenderUnsupportedBinaryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                              editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                              float topY, float clientHeight) {
    if (doc.binaryFileSize == 0 && !doc.filePath.empty()) {
        std::error_code ec;
        doc.binaryFileSize = std::filesystem::file_size(doc.filePath, ec);
    }

    const float availH = clientHeight - topY;
    const float centerY = topY + availH * 0.42f;
    const float centerX = canvasLeft + canvasWidth / 2.0f;

    // 1. Red circled X icon (as requested: "have X incircle red or anything")
    const float circleR = config.ScaleF(28.0f);
    D2D1_ELLIPSE redCircle = D2D1::Ellipse(D2D1::Point2F(centerX, centerY - config.ScaleF(50.0f)), circleR, circleR);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedCross;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.22f, 0.22f, 0.16f), &brushRedBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.22f, 0.22f, 0.90f), &brushRedBorder);
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f) : D2D1::ColorF(0.90f, 0.18f, 0.18f, 1.0f), &brushRedCross);

    if (brushRedBg) pRT->FillEllipse(redCircle, brushRedBg.Get());
    if (brushRedBorder) pRT->DrawEllipse(redCircle, brushRedBorder.Get(), 2.0f);

    if (brushRedCross) {
        float cr = config.ScaleF(10.0f);
        float ccx = redCircle.point.x;
        float ccy = redCircle.point.y;
        pRT->DrawLine(D2D1::Point2F(ccx - cr, ccy - cr), D2D1::Point2F(ccx + cr, ccy + cr), brushRedCross.Get(), 2.4f);
        pRT->DrawLine(D2D1::Point2F(ccx + cr, ccy - cr), D2D1::Point2F(ccx - cr, ccy + cr), brushRedCross.Get(), 2.4f);
    }

    // 2. Bold Headline: "The file cannot be opened in the editor"
    const wchar_t* headline = L"The file cannot be opened in the editor";
    const float headlineW = config.ScaleF(460.0f);
    const float headlineH = config.ScaleF(28.0f);
    D2D1_RECT_F rcHeadline = D2D1::RectF(
        centerX - headlineW / 2.0f,
        centerY - config.ScaleF(6.0f),
        centerX + headlineW / 2.0f,
        centerY + headlineH - config.ScaleF(6.0f)
    );
    IDWriteTextFormat* pHeadFmt = theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get();
    if (pHeadFmt) {
        DWRITE_TEXT_ALIGNMENT oldAlign = pHeadFmt->GetTextAlignment();
        pHeadFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(headline, static_cast<UINT32>(wcslen(headline)), pHeadFmt, rcHeadline, theme.brushTextPrimary.Get());
        pHeadFmt->SetTextAlignment(oldAlign);
    }

    // 3. Sub-header: "This file is unsupported because it is binary or an executable file."
    const wchar_t* subheader = L"This file is unsupported because it is binary or an executable file.";
    const float subH = config.ScaleF(24.0f);
    D2D1_RECT_F rcSub = D2D1::RectF(
        centerX - headlineW / 2.0f,
        rcHeadline.bottom + config.ScaleF(4.0f),
        centerX + headlineW / 2.0f,
        rcHeadline.bottom + config.ScaleF(4.0f) + subH
    );
    if (theme.fontBody) {
        DWRITE_TEXT_ALIGNMENT oldAlign = theme.fontBody->GetTextAlignment();
        theme.fontBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(subheader, static_cast<UINT32>(wcslen(subheader)), theme.fontBody.Get(), rcSub, theme.brushTextSecondary.Get());
        theme.fontBody->SetTextAlignment(oldAlign);
    }

    // 4. File metadata pill (e.g. coordinator.exe | Binary File | 1.42 MB)
    std::wstring sizeStr;
    if (doc.binaryFileSize < 1024) {
        sizeStr = std::to_wstring(doc.binaryFileSize) + L" B";
    } else if (doc.binaryFileSize < 1024 * 1024) {
        sizeStr = std::to_wstring(doc.binaryFileSize / 1024) + L" KB";
    } else {
        sizeStr = std::to_wstring(doc.binaryFileSize / (1024 * 1024)) + L" MB";
    }

    std::wstring typeStr = doc.binaryFileType.empty() ? L"Binary File" : doc.binaryFileType;
    std::wstring pillText = doc.fileName + L"   |   " + typeStr + L"   |   " + sizeStr;

    const float pillH = config.ScaleF(24.0f);
    const float pillW = config.ScaleF(static_cast<float>(pillText.length() * 7.0f + 24.0f));
    const float pillX = centerX - pillW / 2.0f;
    const float pillY = rcSub.bottom + config.ScaleF(14.0f);

    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(
        D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH),
        config.ScaleF(12.0f), config.ScaleF(12.0f)
    );
    pRT->FillRoundedRectangle(rrcPill, theme.brushItemHover.Get());
    pRT->DrawRoundedRectangle(rrcPill, theme.brushBorderSubtle.Get(), 0.8f);

    IDWriteTextFormat* pMenuFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontCode.Get();
    if (pMenuFmt) {
        DWRITE_TEXT_ALIGNMENT oldAlign = pMenuFmt->GetTextAlignment();
        pMenuFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(pillText.c_str(), static_cast<UINT32>(pillText.length()), pMenuFmt, rrcPill.rect, theme.brushTextSecondary.Get());
        pMenuFmt->SetTextAlignment(oldAlign);
    }

    // 5. Action Button: "Reveal in File Explorer"
    const float btnW = config.ScaleF(170.0f);
    const float btnH = config.ScaleF(32.0f);
    const float btnX = centerX - btnW / 2.0f;
    const float btnY = pillY + pillH + config.ScaleF(20.0f);

    doc.unsupportedActionBtn = D2D1::RectF(btnX, btnY, btnX + btnW, btnY + btnH);
    D2D1_ROUNDED_RECT rrcBtn = D2D1::RoundedRect(doc.unsupportedActionBtn, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcBtn, theme.brushBrandAccent.Get());

    const wchar_t* btnLabel = L"Reveal in File Explorer";
    pRT->DrawText(btnLabel, static_cast<UINT32>(wcslen(btnLabel)),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                  doc.unsupportedActionBtn, theme.brushTextWhite.Get());
}

/**
 * @brief Renders the Student Tutorials & Learning Hub view (Student hat 🎓) with modern Academy layout.
 */
void EditorCanvas::InitializeLearningCourses() {
    if (!m_learningCourses.empty()) return;

    // Row 1: Python
    {
        LearningCourseDef c;
        c.id = L"python-1";
        c.category = L"Python";
        c.imageFile = L"course-python-1.png";
        c.creator = L"Koda";
        c.title = L"Python for Beginners";
        c.description = L"Learn Python from scratch with simple examples and hands-on exercises.";
        c.lessons = 24;
        c.hours = 6;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"python-2";
        c.category = L"Python";
        c.imageFile = L"course-python-2.png";
        c.creator = L"Koda";
        c.title = L"DSA with Python";
        c.description = L"Master data structures and algorithms using Python with step-by-step explanations.";
        c.lessons = 32;
        c.hours = 10;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"python-3";
        c.category = L"Python";
        c.imageFile = L"course-python-3.png";
        c.creator = L"Koda";
        c.title = L"Machine Learning with Python";
        c.description = L"Learn machine learning concepts and build real projects using Python.";
        c.lessons = 28;
        c.hours = 12;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"python-4";
        c.category = L"Python";
        c.imageFile = L"course-python-4.png";
        c.creator = L"Koda";
        c.title = L"Introduction to Python";
        c.description = L"Understand Python syntax, variables, functions, and more with practical examples.";
        c.lessons = 20;
        c.hours = 5;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }

    // Row 2: JavaScript
    {
        LearningCourseDef c;
        c.id = L"javascript-1";
        c.category = L"JavaScript";
        c.imageFile = L"course-javascript-1.png";
        c.creator = L"Koda";
        c.title = L"JavaScript Beginner";
        c.description = L"Learn JavaScript fundamentals with interactive examples.";
        c.lessons = 26;
        c.hours = 6;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"javascript-2";
        c.category = L"JavaScript";
        c.imageFile = L"course-javascript-2.png";
        c.creator = L"Koda";
        c.title = L"Introduction to JavaScript";
        c.description = L"Understand the core concepts of JavaScript and how it works in web development.";
        c.lessons = 24;
        c.hours = 6;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"javascript-3";
        c.category = L"JavaScript";
        c.imageFile = L"course-javascript-3.png";
        c.creator = L"Koda";
        c.title = L"Graphics with JavaScript";
        c.description = L"Create interactive graphics and visualizations using JavaScript.";
        c.lessons = 18;
        c.hours = 5;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"javascript-4";
        c.category = L"JavaScript";
        c.imageFile = L"course-javascript-4.png";
        c.creator = L"Koda";
        c.title = L"Advanced JavaScript";
        c.description = L"Deep dive into advanced JavaScript concepts, patterns, and real-world applications.";
        c.lessons = 30;
        c.hours = 10;
        c.difficulty = L"Advanced";
        c.badgeColor = D2D1::ColorF(0.957f, 0.247f, 0.369f, 1.0f);
        m_learningCourses.push_back(c);
    }

    // Row 3: PHP
    {
        LearningCourseDef c;
        c.id = L"php-1";
        c.category = L"PHP";
        c.imageFile = L"course-php-1.png";
        c.creator = L"Koda";
        c.title = L"Beginner PHP";
        c.description = L"Learn PHP basics and build simple dynamic web applications.";
        c.lessons = 24;
        c.hours = 6;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"php-2";
        c.category = L"PHP";
        c.imageFile = L"course-php-2.png";
        c.creator = L"Koda";
        c.title = L"Intermediate PHP";
        c.description = L"Work with databases, sessions, and more advanced PHP features.";
        c.lessons = 28;
        c.hours = 8;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"php-3";
        c.category = L"PHP";
        c.imageFile = L"course-php-3.png";
        c.creator = L"Koda";
        c.title = L"Advanced PHP";
        c.description = L"Master advanced PHP concepts, security, and performance optimization.";
        c.lessons = 32;
        c.hours = 12;
        c.difficulty = L"Advanced";
        c.badgeColor = D2D1::ColorF(0.957f, 0.247f, 0.369f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"php-4";
        c.category = L"PHP";
        c.imageFile = L"course-php-4.png";
        c.creator = L"Koda";
        c.title = L"PHP REST APIs & Microservices";
        c.description = L"Build robust, production-ready RESTful APIs, JWT auth, and microservices architecture.";
        c.lessons = 26;
        c.hours = 8;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
}

ID2D1Bitmap* EditorCanvas::GetLearningHubBitmap(ID2D1RenderTarget* pRT, const std::wstring& filename) {
    if (!pRT || filename.empty()) return nullptr;
    auto it = m_learningHubBitmaps.find(filename);
    if (it != m_learningHubBitmaps.end() && it->second) {
        return it->second.Get();
    }

    std::vector<std::wstring> candidates = {
        L"assets/learning-hub-assets/" + filename,
        L"bin/assets/learning-hub-assets/" + filename,
        L"../assets/learning-hub-assets/" + filename,
        L"assets/" + filename,
        L"bin/assets/" + filename,
        L"../assets/" + filename,
        filename
    };

    wchar_t modPath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, modPath, MAX_PATH)) {
        std::filesystem::path exeDir = std::filesystem::path(modPath).parent_path();
        candidates.push_back((exeDir / L"assets" / L"learning-hub-assets" / filename).wstring());
        candidates.push_back((exeDir / L".." / L"assets" / L"learning-hub-assets" / filename).wstring());
        candidates.push_back((exeDir / L"assets" / filename).wstring());
        candidates.push_back((exeDir / L".." / L"assets" / filename).wstring());
    }

    std::wstring foundPath = L"";
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) {
            foundPath = c;
            break;
        }
    }
    if (foundPath.empty()) return nullptr;

    Microsoft::WRL::ComPtr<IWICImagingFactory> pWicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pWicFactory)
    );
    if (FAILED(hr) || !pWicFactory) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
    hr = pWicFactory->CreateDecoderFromFilename(
        foundPath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &pDecoder
    );
    if (FAILED(hr) || !pDecoder) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr) || !pFrame) return nullptr;

    Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
    hr = pWicFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr) || !pConverter) return nullptr;

    hr = pConverter->Initialize(
        pFrame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> pBmp;
    hr = pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &pBmp);
    if (SUCCEEDED(hr) && pBmp) {
        m_learningHubBitmaps[filename] = pBmp;
        return pBmp.Get();
    }
    return nullptr;
}

/**
 * @brief Renders the Student Tutorials & Learning Hub view (Student hat 🎓) matching the modern Hub design.
 */
void EditorCanvas::RenderStudentView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                     float topY, float clientHeight) {
    if (!pRT) return;

    InitializeLearningCourses();
    m_renderedCourseCards.clear();

    const float viewW = canvasWidth;
    const float viewH = clientHeight - topY;
    if (viewW <= 10.0f || viewH <= 10.0f) return;

    D2D1_RECT_F rcClip = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight);
    pRT->PushAxisAlignedClip(rcClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // 1. Deep charcoal-black background matching design (#0A0D12)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.040f, 0.052f, 0.070f, 1.0f), &bgBrush);
    if (bgBrush) {
        pRT->FillRectangle(rcClip, bgBrush.Get());
    }

    // DirectWrite Factory for typography
    Microsoft::WRL::ComPtr<IDWriteFactory> pDWrite;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &pDWrite);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtTitle;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSub;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSearch;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtPill;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardKoda;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardTitle;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardDesc;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardMeta;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardBadge;

    if (pDWrite) {
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(24.0f), L"en-us", &fmtTitle);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(11.5f), L"en-us", &fmtSub);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(11.0f), L"en-us", &fmtSearch);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(10.5f), L"en-us", &fmtPill);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(9.5f), L"en-us", &fmtCardKoda);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(11.5f), L"en-us", &fmtCardTitle);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(9.2f), L"en-us", &fmtCardDesc);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(9.0f), L"en-us", &fmtCardMeta);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(8.5f), L"en-us", &fmtCardBadge);

        if (fmtCardBadge) {
            fmtCardBadge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtCardBadge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // Color Brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushWhite;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSub;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushMeta;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBorderHover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSearchBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSearchBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSearchBorderActive;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushGreenAccent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillActiveBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillActiveBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillActiveText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillInactiveBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillInactiveBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillInactiveText;

    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brushWhite);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.62f, 0.70f, 1.0f), &brushSub);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.42f, 0.49f, 0.58f, 1.0f), &brushMeta);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.067f, 0.086f, 0.122f, 1.0f), &brushCardBg); // #11161F
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.110f, 0.145f, 0.190f, 1.0f), &brushCardBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.180f, 0.240f, 0.320f, 1.0f), &brushCardBorderHover);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.070f, 0.090f, 0.120f, 1.0f), &brushSearchBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.130f, 0.170f, 0.220f, 1.0f), &brushSearchBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f), &brushSearchBorderActive);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f), &brushGreenAccent);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.043f, 0.188f, 0.137f, 1.0f), &brushPillActiveBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f), &brushPillActiveBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.204f, 0.827f, 0.600f, 1.0f), &brushPillActiveText);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.067f, 0.086f, 0.122f, 1.0f), &brushPillInactiveBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.120f, 0.150f, 0.200f, 1.0f), &brushPillInactiveBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.62f, 0.70f, 1.0f), &brushPillInactiveText);

    const float padX = config.ScaleF(10.0f);
    const float bannerTop = topY - m_learningScrollY + config.ScaleF(10.0f);
    const float bannerH = config.ScaleF(138.0f);
    const float bannerLeft = canvasLeft + padX;
    const float bannerRight = canvasLeft + canvasWidth - padX;
    D2D1_RECT_F rcHeaderBanner = D2D1::RectF(bannerLeft, bannerTop, bannerRight, bannerTop + bannerH);

    // 2. Header Banner: target wide banner with isometric green cubes on right and striped gradient on left
    ID2D1Bitmap* pBmpBanner = GetLearningHubBitmap(pRT, L"header-banner.png");
    if (!pBmpBanner) {
        pBmpBanner = GetLearningHubBitmap(pRT, L"banner-target.png");
    }
    if (pBmpBanner) {
        D2D1_SIZE_F bSize = pBmpBanner->GetSize();
        // Right cubes have source x: 500..783 (width: 283)
        float cubesSrcW = 283.0f;
        float cubesDestW = bannerH * (cubesSrcW / bSize.height);
        float bannerW = bannerRight - bannerLeft;
        if (bannerW > cubesDestW + config.ScaleF(80.0f)) {
            // Draw left background part (0..500)
            D2D1_RECT_F srcLeft = D2D1::RectF(0.0f, 0.0f, 500.0f, bSize.height);
            D2D1_RECT_F destLeft = D2D1::RectF(bannerLeft, bannerTop, bannerRight - cubesDestW, bannerTop + bannerH);
            pRT->DrawBitmap(pBmpBanner, destLeft, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcLeft);

            // Draw right cubes part (500..bSize.width)
            D2D1_RECT_F srcRight = D2D1::RectF(500.0f, 0.0f, bSize.width, bSize.height);
            D2D1_RECT_F destRight = D2D1::RectF(bannerRight - cubesDestW, bannerTop, bannerRight, bannerTop + bannerH);
            pRT->DrawBitmap(pBmpBanner, destRight, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRight);
        } else {
            pRT->DrawBitmap(pBmpBanner, rcHeaderBanner, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    } else {
        // Fallback right image if header-banner is not present
        ID2D1Bitmap* pBmpRight = GetLearningHubBitmap(pRT, L"header-right.png");
        if (pBmpRight) {
            float bmpW = bannerH * (518.0f / 205.0f);
            D2D1_RECT_F rcRightImg = D2D1::RectF(bannerRight - bmpW, bannerTop, bannerRight, bannerTop + bannerH);
            pRT->DrawBitmap(pBmpRight, rcRightImg, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }

    // 3. Header Title: "Learning Hub"
    D2D1_RECT_F rcTitle = D2D1::RectF(bannerLeft + config.ScaleF(18.0f), bannerTop + config.ScaleF(16.0f),
                                     bannerRight - config.ScaleF(260.0f), bannerTop + config.ScaleF(46.0f));
    const wchar_t* titleStr = L"Learning Hub";
    if (fmtTitle && brushWhite) {
        pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)), fmtTitle.Get(), rcTitle, brushWhite.Get());
    }

    // 4. Header Subtitle
    D2D1_RECT_F rcSub = D2D1::RectF(bannerLeft + config.ScaleF(18.0f), rcTitle.bottom + config.ScaleF(2.0f),
                                   bannerRight - config.ScaleF(260.0f), rcTitle.bottom + config.ScaleF(28.0f));
    const wchar_t* subStr = L"Step-by-step programming courses with hands-on examples, projects, and real-world skills.";
    if (fmtSub && brushSub) {
        pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)), fmtSub.Get(), rcSub, brushSub.Get());
    }

    // 5. Header Search Input Box
    const float searchW = (std::min)(config.ScaleF(326.0f), bannerRight - bannerLeft - config.ScaleF(36.0f));
    const float searchH = config.ScaleF(28.0f);
    D2D1_RECT_F rcSearch = D2D1::RectF(bannerLeft + config.ScaleF(18.0f), bannerTop + config.ScaleF(86.0f),
                                       bannerLeft + config.ScaleF(18.0f) + searchW, bannerTop + config.ScaleF(86.0f) + searchH);
    m_learningSearchBounds = rcSearch;
    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(rcSearch, config.ScaleF(5.0f), config.ScaleF(5.0f));

    if (brushSearchBg) pRT->FillRoundedRectangle(rrcSearch, brushSearchBg.Get());
    ID2D1Brush* pSearchBrd = m_isLearningSearchFocused ? brushSearchBorderActive.Get() :
                             (m_hoveredLearningSearch ? brushCardBorderHover.Get() : brushSearchBorder.Get());
    if (pSearchBrd) pRT->DrawRoundedRectangle(rrcSearch, pSearchBrd, 1.0f);

    // Magnifier Search Icon inside input
    const float searchIconSize = config.ScaleF(12.0f);
    D2D1_RECT_F rcSearchIcon = D2D1::RectF(rcSearch.left + config.ScaleF(8.0f),
                                          rcSearch.top + (searchH - searchIconSize) / 2.0f,
                                          rcSearch.left + config.ScaleF(8.0f) + searchIconSize,
                                          rcSearch.top + (searchH + searchIconSize) / 2.0f);
    m_icons.DrawSearch(pRT, rcSearchIcon, brushMeta ? brushMeta.Get() : brushSub.Get(), 1.4f);

    // Search query or placeholder
    D2D1_RECT_F rcSearchText = D2D1::RectF(rcSearchIcon.right + config.ScaleF(6.0f), rcSearch.top + config.ScaleF(5.0f),
                                          rcSearch.right - config.ScaleF(8.0f), rcSearch.bottom - config.ScaleF(5.0f));
    float textWidth = 0.0f;
    if (fmtSearch) {
        if (!m_learningSearchQuery.empty()) {
            pRT->DrawText(m_learningSearchQuery.c_str(), static_cast<UINT32>(m_learningSearchQuery.length()),
                          fmtSearch.Get(), rcSearchText, brushWhite.Get());

            if (pDWrite) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                if (SUCCEEDED(pDWrite->CreateTextLayout(
                    m_learningSearchQuery.c_str(),
                    static_cast<UINT32>(m_learningSearchQuery.length()),
                    fmtSearch.Get(),
                    rcSearchText.right - rcSearchText.left,
                    rcSearchText.bottom - rcSearchText.top,
                    &pLayout))) {
                    DWRITE_TEXT_METRICS metrics;
                    if (SUCCEEDED(pLayout->GetMetrics(&metrics))) {
                        textWidth = metrics.widthIncludingTrailingWhitespace;
                    }
                }
            }
        } else {
            const wchar_t* placeHolder = L"Search courses, e.g. Python, PHP, JavaScript...";
            pRT->DrawText(placeHolder, static_cast<UINT32>(wcslen(placeHolder)), fmtSearch.Get(), rcSearchText, brushMeta.Get());
        }
    }

    // Natural Blinking Caret when focused
    if (m_isLearningSearchFocused && m_caretVisible && brushWhite) {
        float caretX = rcSearchText.left + textWidth + config.ScaleF(1.0f);
        if (caretX <= rcSearch.right - config.ScaleF(4.0f)) {
            pRT->DrawLine(D2D1::Point2F(caretX, rcSearch.top + config.ScaleF(5.0f)),
                          D2D1::Point2F(caretX, rcSearch.bottom - config.ScaleF(5.0f)),
                          brushWhite.Get(), config.ScaleF(1.2f));
        }
    }

    // 6. Filter & Sort Bar
    const float filterY = bannerTop + bannerH + config.ScaleF(14.0f);
    const float filterH = config.ScaleF(24.0f);

    struct FilterBtn {
        const wchar_t* label;
        float width;
    };
    FilterBtn filters[5] = {
        { L"All", config.ScaleF(46.0f) },
        { L"Python", config.ScaleF(72.0f) },
        { L"JavaScript", config.ScaleF(90.0f) },
        { L"PHP", config.ScaleF(62.0f) },
        { L"Others", config.ScaleF(64.0f) }
    };

    float curFilterX = canvasLeft + padX;
    const float pillGap = config.ScaleF(8.0f);

    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F rcPill = D2D1::RectF(curFilterX, filterY, curFilterX + filters[i].width, filterY + filterH);
        m_learningFilterBounds[i] = rcPill;
        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, config.ScaleF(6.0f), config.ScaleF(6.0f));

        bool isActive = (m_learningActiveFilter == i);
        bool isHover = (m_hoveredLearningFilter == i);

        ID2D1Brush* pPillBg = isActive ? brushPillActiveBg.Get() : (isHover ? brushCardBorderHover.Get() : brushPillInactiveBg.Get());
        ID2D1Brush* pPillBorder = isActive ? brushPillActiveBorder.Get() : (isHover ? brushCardBorderHover.Get() : brushPillInactiveBorder.Get());
        ID2D1Brush* pPillText = isActive ? brushPillActiveText.Get() : (isHover ? brushWhite.Get() : brushPillInactiveText.Get());

        if (pPillBg) pRT->FillRoundedRectangle(rrcPill, pPillBg);
        if (pPillBorder) pRT->DrawRoundedRectangle(rrcPill, pPillBorder, isActive ? 1.2f : 1.0f);

        // Icon for category
        float iconBoxW = config.ScaleF(14.0f);
        D2D1_RECT_F rcPillIcon = D2D1::RectF(rcPill.left + config.ScaleF(6.0f), rcPill.top + (filterH - iconBoxW) / 2.0f,
                                            rcPill.left + config.ScaleF(6.0f) + iconBoxW, rcPill.top + (filterH + iconBoxW) / 2.0f);

        if (i == 0) { // All: 2x2 grid
            float sq = config.ScaleF(4.0f);
            float g = config.ScaleF(1.5f);
            float ox = rcPillIcon.left + config.ScaleF(2.0f);
            float oy = rcPillIcon.top + config.ScaleF(2.0f);
            if (pPillText) {
                pRT->FillRectangle(D2D1::RectF(ox, oy, ox + sq, oy + sq), pPillText);
                pRT->FillRectangle(D2D1::RectF(ox + sq + g, oy, ox + sq * 2.0f + g, oy + sq), pPillText);
                pRT->FillRectangle(D2D1::RectF(ox, oy + sq + g, ox + sq, oy + sq * 2.0f + g), pPillText);
                pRT->FillRectangle(D2D1::RectF(ox + sq + g, oy + sq + g, ox + sq * 2.0f + g, oy + sq * 2.0f + g), pPillText);
            }
        } else if (i == 1) { // Python: official icon from assets/python.png
            ID2D1Bitmap* pPyIcon = GetLearningHubBitmap(pRT, L"python.png");
            if (pPyIcon) {
                pRT->DrawBitmap(pPyIcon, rcPillIcon, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else if (i == 2) { // JavaScript: official icon from assets/javascript.png
            ID2D1Bitmap* pJsIcon = GetLearningHubBitmap(pRT, L"javascript.png");
            if (pJsIcon) {
                pRT->DrawBitmap(pJsIcon, rcPillIcon, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else if (i == 3) { // PHP: official icon from assets/php.png
            ID2D1Bitmap* pPhpIcon = GetLearningHubBitmap(pRT, L"php.png");
            if (pPhpIcon) {
                pRT->DrawBitmap(pPhpIcon, rcPillIcon, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else if (i == 4) { // Others: 3 dots
            if (pPillText) {
                float dotY = (rcPillIcon.top + rcPillIcon.bottom) / 2.0f;
                float dotR = config.ScaleF(1.2f);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rcPillIcon.left + config.ScaleF(3.0f), dotY), dotR, dotR), pPillText);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rcPillIcon.left + config.ScaleF(7.0f), dotY), dotR, dotR), pPillText);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rcPillIcon.left + config.ScaleF(11.0f), dotY), dotR, dotR), pPillText);
            }
        }

        // Label
        D2D1_RECT_F rcPillLabel = D2D1::RectF(rcPillIcon.right + config.ScaleF(4.0f), rcPill.top + config.ScaleF(4.0f),
                                             rcPill.right - config.ScaleF(6.0f), rcPill.bottom - config.ScaleF(4.0f));
        if (fmtPill && pPillText) {
            pRT->DrawText(filters[i].label, static_cast<UINT32>(wcslen(filters[i].label)), fmtPill.Get(), rcPillLabel, pPillText);
        }

        curFilterX += filters[i].width + pillGap;
    }

    // Sort Dropdown Pill on Right
    const float sortW = config.ScaleF(112.0f);
    D2D1_RECT_F rcSort = D2D1::RectF(canvasLeft + canvasWidth - padX - sortW, filterY,
                                    canvasLeft + canvasWidth - padX, filterY + filterH);
    m_learningSortBounds = rcSort;
    D2D1_ROUNDED_RECT rrcSort = D2D1::RoundedRect(rcSort, config.ScaleF(6.0f), config.ScaleF(6.0f));

    ID2D1Brush* pSortBg = m_hoveredLearningSort ? brushCardBorderHover.Get() : brushPillInactiveBg.Get();
    ID2D1Brush* pSortBorder = m_hoveredLearningSort ? brushCardBorderHover.Get() : brushPillInactiveBorder.Get();
    if (pSortBg) pRT->FillRoundedRectangle(rrcSort, pSortBg);
    if (pSortBorder) pRT->DrawRoundedRectangle(rrcSort, pSortBorder, 1.0f);

    const wchar_t* sortLabel = (m_learningSortOrder == 1) ? L"⇅ Most Lessons ˅" :
                               (m_learningSortOrder == 2) ? L"⇅ Duration ˅" : L"⇅ Most Popular ˅";
    D2D1_RECT_F rcSortLabel = D2D1::RectF(rcSort.left + config.ScaleF(6.0f), rcSort.top + config.ScaleF(4.0f),
                                         rcSort.right - config.ScaleF(6.0f), rcSort.bottom - config.ScaleF(4.0f));
    if (fmtPill && brushWhite) {
        DWRITE_TEXT_ALIGNMENT oldAlign = fmtPill->GetTextAlignment();
        fmtPill->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(sortLabel, static_cast<UINT32>(wcslen(sortLabel)), fmtPill.Get(), rcSortLabel, brushWhite.Get());
        fmtPill->SetTextAlignment(oldAlign);
    }

    // 7. Course Cards Grid (4 Columns)
    std::vector<size_t> visibleCourses;
    for (size_t i = 0; i < m_learningCourses.size(); ++i) {
        const auto& c = m_learningCourses[i];
        if (m_learningActiveFilter == 1 && c.category != L"Python") continue;
        if (m_learningActiveFilter == 2 && c.category != L"JavaScript") continue;
        if (m_learningActiveFilter == 3 && c.category != L"PHP") continue;
        if (m_learningActiveFilter == 4 && c.category != L"Others") continue;

        if (!m_learningSearchQuery.empty()) {
            auto ToLower = [](const std::wstring& s) {
                std::wstring r = s;
                for (auto& ch : r) ch = towlower(ch);
                return r;
            };
            std::wstring q = ToLower(m_learningSearchQuery);
            if (ToLower(c.title).find(q) == std::wstring::npos &&
                ToLower(c.description).find(q) == std::wstring::npos &&
                ToLower(c.category).find(q) == std::wstring::npos) {
                continue;
            }
        }
        visibleCourses.push_back(i);
    }

    if (m_learningSortOrder == 1) {
        std::stable_sort(visibleCourses.begin(), visibleCourses.end(), [&](size_t a, size_t b) {
            return m_learningCourses[a].lessons > m_learningCourses[b].lessons;
        });
    } else if (m_learningSortOrder == 2) {
        std::stable_sort(visibleCourses.begin(), visibleCourses.end(), [&](size_t a, size_t b) {
            return m_learningCourses[a].hours > m_learningCourses[b].hours;
        });
    }

    const float gapX = config.ScaleF(8.0f);
    const float gapY = config.ScaleF(10.0f);
    const float totalGridW = canvasWidth - padX * 2.0f;

    // Proportional, non-stretching responsive grid (195px base width, 142px base height)
    int numCols = std::clamp(static_cast<int>((totalGridW + gapX) / (config.ScaleF(195.0f) + gapX)), 2, 8);
    const float cardW = (totalGridW - gapX * static_cast<float>(numCols - 1)) / static_cast<float>(numCols);
    const float cardH = cardW * (142.0f / 195.0f);
    const float cardBannerH = cardH * (62.0f / 142.0f);
    const float filterCardGap = config.ScaleF(18.0f);
    const float gridStartY = filterY + filterH + filterCardGap;

    auto DrawBookIcon = [&](const D2D1_RECT_F& r, ID2D1Brush* pBr) {
        if (!pBr) return;
        float midX = (r.left + r.right) / 2.0f;
        float botY = r.bottom - 1.0f;
        float tY = r.top + 1.0f;
        pRT->DrawLine(D2D1::Point2F(midX, botY), D2D1::Point2F(r.left, botY + 0.5f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(r.left, botY + 0.5f), D2D1::Point2F(r.left, tY + 0.5f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(r.left, tY + 0.5f), D2D1::Point2F(midX, tY), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(midX, tY), D2D1::Point2F(r.right, tY + 0.5f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(r.right, tY + 0.5f), D2D1::Point2F(r.right, botY + 0.5f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(r.right, botY + 0.5f), D2D1::Point2F(midX, botY), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(midX, tY), D2D1::Point2F(midX, botY), pBr, 1.1f);
    };

    auto DrawClockIcon = [&](const D2D1_RECT_F& r, ID2D1Brush* pBr) {
        if (!pBr) return;
        float cx = (r.left + r.right) / 2.0f;
        float cy = (r.top + r.bottom) / 2.0f;
        float rad = (std::min)(r.right - r.left, r.bottom - r.top) / 2.0f - 0.5f;
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx, cy - rad * 0.55f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx + rad * 0.50f, cy), pBr, 1.1f);
    };

    ID2D1Bitmap* pBmpKodaLogo = GetLearningHubBitmap(pRT, L"header-logo.png");

    for (size_t vIdx = 0; vIdx < visibleCourses.size(); ++vIdx) {
        size_t origIdx = visibleCourses[vIdx];
        const auto& course = m_learningCourses[origIdx];

        int col = static_cast<int>(vIdx % numCols);
        int row = static_cast<int>(vIdx / numCols);
        float cx = canvasLeft + padX + static_cast<float>(col) * (cardW + gapX);
        float cy = gridStartY + static_cast<float>(row) * (cardH + gapY);

        D2D1_RECT_F rcCard = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);
        m_renderedCourseCards.push_back({ origIdx, rcCard });

        // Backward compatibility bounds for the first 4 cards
        if (origIdx < 4) {
            m_studentCardBounds[origIdx] = rcCard;
            m_studentCardBtnBounds[origIdx] = rcCard;
        }

        bool isHovered = (m_hoveredLearningCard == static_cast<int>(origIdx));
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, config.ScaleF(8.0f), config.ScaleF(8.0f));

        if (brushCardBg) pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());
        ID2D1Brush* pCBorder = isHovered ? brushCardBorderHover.Get() : brushCardBorder.Get();
        if (pCBorder) pRT->DrawRoundedRectangle(rrcCard, pCBorder, isHovered ? 1.2f : 1.0f);

        // Top Banner Image
        D2D1_RECT_F rcBanner = D2D1::RectF(cx + 0.5f, cy + 0.5f, cx + cardW - 0.5f, cy + cardBannerH);
        ID2D1Bitmap* pBannerBmp = GetLearningHubBitmap(pRT, course.imageFile);
        if (pBannerBmp) {
            pRT->DrawBitmap(pBannerBmp, rcBanner, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }

        // Koda Row
        const float kodaY = cy + cardBannerH + config.ScaleF(5.0f);
        const float kodaIconSize = config.ScaleF(10.0f);
        D2D1_RECT_F rcKodaIcon = D2D1::RectF(cx + config.ScaleF(8.0f), kodaY,
                                            cx + config.ScaleF(8.0f) + kodaIconSize, kodaY + kodaIconSize);
        if (pBmpKodaLogo) {
            pRT->DrawBitmap(pBmpKodaLogo, rcKodaIcon);
        } else if (brushGreenAccent) {
            pRT->FillRectangle(rcKodaIcon, brushGreenAccent.Get());
        }

        D2D1_RECT_F rcKodaText = D2D1::RectF(rcKodaIcon.right + config.ScaleF(3.0f), kodaY - config.ScaleF(1.0f),
                                            cx + cardW - config.ScaleF(8.0f), kodaY + config.ScaleF(11.0f));
        if (fmtCardKoda && brushMeta) {
            pRT->DrawText(course.creator.c_str(), static_cast<UINT32>(course.creator.length()),
                          fmtCardKoda.Get(), rcKodaText, brushMeta.Get());
        }

        // Course Title
        const float titleTop = kodaY + config.ScaleF(12.0f);
        D2D1_RECT_F rcCourseTitle = D2D1::RectF(cx + config.ScaleF(8.0f), titleTop,
                                               cx + cardW - config.ScaleF(8.0f), titleTop + config.ScaleF(16.0f));
        if (fmtCardTitle && brushWhite) {
            pRT->DrawText(course.title.c_str(), static_cast<UINT32>(course.title.length()),
                          fmtCardTitle.Get(), rcCourseTitle, brushWhite.Get());
        }

        // Course Description (2 lines wrapped)
        const float descTop = titleTop + config.ScaleF(15.0f);
        D2D1_RECT_F rcCourseDesc = D2D1::RectF(cx + config.ScaleF(8.0f), descTop,
                                              cx + cardW - config.ScaleF(8.0f), descTop + config.ScaleF(28.0f));
        if (fmtCardDesc && brushSub) {
            DWRITE_WORD_WRAPPING oldWrap = fmtCardDesc->GetWordWrapping();
            fmtCardDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            pRT->DrawText(course.description.c_str(), static_cast<UINT32>(course.description.length()),
                          fmtCardDesc.Get(), rcCourseDesc, brushSub.Get());
            fmtCardDesc->SetWordWrapping(oldWrap);
        }

        // Footer Row
        const float footerTop = cy + cardH - config.ScaleF(18.0f);

        // Book icon & lessons
        D2D1_RECT_F rcBook = D2D1::RectF(cx + config.ScaleF(8.0f), footerTop + config.ScaleF(2.0f),
                                         cx + config.ScaleF(17.0f), footerTop + config.ScaleF(11.0f));
        DrawBookIcon(rcBook, brushMeta.Get());

        std::wstring lessonStr = std::to_wstring(course.lessons) + L" lessons";
        D2D1_RECT_F rcLessons = D2D1::RectF(rcBook.right + config.ScaleF(3.0f), footerTop,
                                           rcBook.right + config.ScaleF(56.0f), footerTop + config.ScaleF(14.0f));
        if (fmtCardMeta && brushMeta) {
            pRT->DrawText(lessonStr.c_str(), static_cast<UINT32>(lessonStr.length()), fmtCardMeta.Get(), rcLessons, brushMeta.Get());
        }

        // Clock icon & hours
        D2D1_RECT_F rcClock = D2D1::RectF(rcLessons.right + config.ScaleF(4.0f), footerTop + config.ScaleF(2.0f),
                                          rcLessons.right + config.ScaleF(13.0f), footerTop + config.ScaleF(11.0f));
        DrawClockIcon(rcClock, brushMeta.Get());

        std::wstring hourStr = std::to_wstring(course.hours) + L" hours";
        D2D1_RECT_F rcHours = D2D1::RectF(rcClock.right + config.ScaleF(3.0f), footerTop,
                                         rcClock.right + config.ScaleF(44.0f), footerTop + config.ScaleF(14.0f));
        if (fmtCardMeta && brushMeta) {
            pRT->DrawText(hourStr.c_str(), static_cast<UINT32>(hourStr.length()), fmtCardMeta.Get(), rcHours, brushMeta.Get());
        }

        // Difficulty Badge (Right-aligned)
        const float badgeW = config.ScaleF(56.0f);
        const float badgeH = config.ScaleF(16.0f);
        D2D1_RECT_F rcDiffBadge = D2D1::RectF(cx + cardW - config.ScaleF(8.0f) - badgeW, footerTop - config.ScaleF(1.0f),
                                             cx + cardW - config.ScaleF(8.0f), footerTop - config.ScaleF(1.0f) + badgeH);
        D2D1_ROUNDED_RECT rrcDiffBadge = D2D1::RoundedRect(rcDiffBadge, badgeH / 2.0f, badgeH / 2.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBg, bBrd, bTxt;
        if (course.difficulty == L"Beginner") {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.040f, 0.160f, 0.120f, 1.0f), &bBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.080f, 0.320f, 0.240f, 1.0f), &bBrd);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.125f, 0.796f, 0.482f, 1.0f), &bTxt);
        } else if (course.difficulty == L"Intermediate") {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.050f, 0.140f, 0.250f, 1.0f), &bBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.080f, 0.250f, 0.450f, 1.0f), &bBrd);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f), &bTxt);
        } else { // Advanced
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.200f, 0.080f, 0.090f, 1.0f), &bBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.380f, 0.120f, 0.140f, 1.0f), &bBrd);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.957f, 0.247f, 0.369f, 1.0f), &bTxt);
        }

        if (bBg) pRT->FillRoundedRectangle(rrcDiffBadge, bBg.Get());
        if (bBrd) pRT->DrawRoundedRectangle(rrcDiffBadge, bBrd.Get(), 1.0f);
        if (fmtCardBadge && bTxt) {
            pRT->DrawText(course.difficulty.c_str(), static_cast<UINT32>(course.difficulty.length()),
                          fmtCardBadge.Get(), rcDiffBadge, bTxt.Get());
        }
    }

    // Scroll metrics & Scrollbar
    int numRows = (static_cast<int>(visibleCourses.size()) + numCols - 1) / numCols;
    float contentBottom = gridStartY + static_cast<float>(numRows) * (cardH + gapY) + config.ScaleF(24.0f) + m_learningScrollY;
    float visibleBottom = topY + viewH;
    m_learningMaxScrollY = (std::max)(0.0f, contentBottom - visibleBottom);
    if (m_learningScrollY > m_learningMaxScrollY) m_learningScrollY = m_learningMaxScrollY;

    if (m_learningMaxScrollY > 0.0f && brushCardBorderHover) {
        float trackH = viewH - config.ScaleF(16.0f);
        float thumbH = (std::max)(config.ScaleF(32.0f), (viewH / contentBottom) * trackH);
        float thumbY = topY + config.ScaleF(8.0f) + (m_learningScrollY / m_learningMaxScrollY) * (trackH - thumbH);
        D2D1_RECT_F rcThumb = D2D1::RectF(canvasLeft + canvasWidth - config.ScaleF(6.0f), thumbY,
                                         canvasLeft + canvasWidth - config.ScaleF(2.0f), thumbY + thumbH);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcThumb, 2.0f, 2.0f), brushCardBorderHover.Get());
    }

    pRT->PopAxisAlignedClip();
}

bool EditorCanvas::HandleLearningChar(wchar_t ch) {
    if (!m_isLearningSearchFocused) return false;
    if (ch >= 32 && ch != 127) {
        m_learningSearchQuery.push_back(ch);
        m_learningScrollY = 0.0f;
        return true;
    }
    return false;
}

bool EditorCanvas::HandleLearningKeyDown(WPARAM key) {
    if (!m_isLearningSearchFocused) return false;
    if (key == VK_BACK) {
        if (!m_learningSearchQuery.empty()) {
            m_learningSearchQuery.pop_back();
            m_learningScrollY = 0.0f;
            return true;
        }
    } else if (key == VK_ESCAPE) {
        m_learningSearchQuery.clear();
        m_isLearningSearchFocused = false;
        m_learningScrollY = 0.0f;
        return true;
    }
    return false;
}

bool EditorCanvas::HandleStudentMouseMove(POINT pt) {
    auto PointInRect = [](POINT p, const D2D1_RECT_F& r) -> bool {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };

    int oldHoverCard = m_hoveredLearningCard;
    int oldHoverFilter = m_hoveredLearningFilter;
    bool oldHoverSearch = m_hoveredLearningSearch;
    bool oldHoverSort = m_hoveredLearningSort;

    m_hoveredLearningSearch = PointInRect(pt, m_learningSearchBounds);
    m_hoveredLearningSort = PointInRect(pt, m_learningSortBounds);

    m_hoveredLearningFilter = -1;
    for (int i = 0; i < 5; ++i) {
        if (PointInRect(pt, m_learningFilterBounds[i])) {
            m_hoveredLearningFilter = i;
            break;
        }
    }

    m_hoveredLearningCard = -1;
    for (const auto& card : m_renderedCourseCards) {
        if (PointInRect(pt, card.bounds)) {
            m_hoveredLearningCard = static_cast<int>(card.courseIndex);
            break;
        }
    }

    return (oldHoverCard != m_hoveredLearningCard ||
            oldHoverFilter != m_hoveredLearningFilter ||
            oldHoverSearch != m_hoveredLearningSearch ||
            oldHoverSort != m_hoveredLearningSort);
}

bool EditorCanvas::HandleStudentMouseWheel(short delta) {
    if (m_learningMaxScrollY <= 0.0f) return false;
    float step = 45.0f;
    float oldScroll = m_learningScrollY;
    if (delta > 0) {
        m_learningScrollY = (std::max)(0.0f, m_learningScrollY - step);
    } else {
        m_learningScrollY = (std::min)(m_learningMaxScrollY, m_learningScrollY + step);
    }
    return (m_learningScrollY != oldScroll);
}

int EditorCanvas::HitTestStudentView(POINT pt) const {
    auto PointInRect = [](POINT p, const D2D1_RECT_F& r) -> bool {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };

    if (PointInRect(pt, m_learningSearchBounds)) {
        return 50;
    }

    for (int i = 0; i < 5; ++i) {
        if (PointInRect(pt, m_learningFilterBounds[i])) {
            return 60 + i;
        }
    }

    if (PointInRect(pt, m_learningSortBounds)) {
        return 70;
    }

    for (const auto& card : m_renderedCourseCards) {
        if (PointInRect(pt, card.bounds)) {
            return 100 + static_cast<int>(card.courseIndex);
        }
    }

    // Quick sandbox buttons (backward compatibility if needed)
    if (PointInRect(pt, m_studentQuickNbBtn)) return 10;
    if (PointInRect(pt, m_studentQuickScratchBtn)) return 11;
    if (PointInRect(pt, m_studentQuickDbBtn)) return 12;

    return -1;
}

const LearningCourseDef& EditorCanvas::GetLearningCourse(size_t index) const {
    static LearningCourseDef s_empty{};
    if (index < m_learningCourses.size()) {
        return m_learningCourses[index];
    }
    return s_empty;
}

void EditorCanvas::RenderRefactorPopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight) {
    if (!pRT || m_refactorMessage.empty()) return;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;

    if (theme.IsDarkMode()) {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.145f, 0.145f, 0.155f, 0.98f), &bgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.478f, 0.800f, 1.0f), &borderBrush); // #007ACC
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.88f, 0.88f, 0.88f, 1.0f), &textBrush);
    } else {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.96f, 0.98f), &bgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.478f, 0.800f, 1.0f), &borderBrush); // #007ACC
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &textBrush);
    }

    if (!bgBrush || !borderBrush || !textBrush) return;

    float textW = 160.0f;
    float textH = 18.0f;
    IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get();

    if (theme.dwriteFactory && pFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                m_refactorMessage.c_str(),
                static_cast<UINT32>(m_refactorMessage.length()),
                pFmt, 400.0f, 100.0f, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                textW = tm.widthIncludingTrailingWhitespace;
                textH = tm.height;
            }
        }
    }

    const float padX = config.ScaleF(12.0f);
    const float padY = config.ScaleF(5.0f);
    const float popupW = textW + padX * 2.0f;
    const float popupH = textH + padY * 2.0f;
    const float arrowW = config.ScaleF(10.0f);
    const float arrowH = config.ScaleF(6.0f);

    float tipX = m_lastCaretX;
    if (tipX < 20.0f) tipX = 200.0f;
    float tipY = m_lastCaretY;
    if (tipY < 40.0f) tipY = 150.0f;

    float popupLeft = tipX - popupW * 0.5f;
    if (popupLeft < config.ScaleF(10.0f)) popupLeft = config.ScaleF(10.0f);

    bool flipBelow = (tipY - popupH - arrowH < config.ScaleF(60.0f));
    float popupTop = 0.0f;
    float popupBottom = 0.0f;

    if (!flipBelow) {
        popupBottom = tipY - arrowH;
        popupTop = popupBottom - popupH;
    } else {
        popupTop = tipY + config.ScaleF(20.0f) + arrowH;
        popupBottom = popupTop + popupH;
    }

    D2D1_RECT_F rcBox = D2D1::RectF(popupLeft, popupTop, popupLeft + popupW, popupBottom);
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcBox, 4.0f, 4.0f);

    // Draw main popover balloon body
    pRT->FillRoundedRectangle(rrc, bgBrush.Get());
    pRT->DrawRoundedRectangle(rrc, borderBrush.Get(), 1.2f);

    // Draw pointer triangle
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pTri;
        if (SUCCEEDED(pFactory->CreatePathGeometry(&pTri))) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(pTri->Open(&sink))) {
                if (!flipBelow) {
                    // Downward triangle pointing at caret
                    sink->BeginFigure(D2D1::Point2F(tipX - arrowW * 0.5f, popupBottom - 0.5f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(tipX + arrowW * 0.5f, popupBottom - 0.5f));
                    sink->AddLine(D2D1::Point2F(tipX, tipY));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                } else {
                    // Upward triangle pointing at caret
                    sink->BeginFigure(D2D1::Point2F(tipX - arrowW * 0.5f, popupTop + 0.5f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(tipX + arrowW * 0.5f, popupTop + 0.5f));
                    sink->AddLine(D2D1::Point2F(tipX, tipY + config.ScaleF(20.0f)));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                }
                sink->Close();
                pRT->FillGeometry(pTri.Get(), bgBrush.Get());
                pRT->DrawGeometry(pTri.Get(), borderBrush.Get(), 1.2f);
            }
        }
    }

    // Draw popover message text
    D2D1_RECT_F rcText = D2D1::RectF(popupLeft + padX, popupTop + padY, popupLeft + popupW - padX, popupBottom - padY);
    pRT->DrawText(
        m_refactorMessage.c_str(),
        static_cast<UINT32>(m_refactorMessage.length()),
        pFmt,
        rcText,
        textBrush.Get()
    );
}

void EditorCanvas::ShowAutocomplete(const std::vector<editor::IntelliSenseItem>& items, size_t selectedIndex) {
    m_autocompleteScrollOffset = 0;
    m_autocompleteItems = items;
    m_autocompleteSelectedIdx = (selectedIndex < items.size()) ? selectedIndex : 0;
    m_showAutocomplete = !items.empty();
}

bool EditorCanvas::HandleAutocompleteMouseWheel(short delta) noexcept {
    if (!m_showAutocomplete || m_autocompleteItems.empty()) return false;
    const size_t maxVisible = 8;
    if (m_autocompleteItems.size() <= maxVisible) return false;
    size_t maxScroll = m_autocompleteItems.size() - maxVisible;

    int step = (delta > 0) ? -2 : 2;
    int newOffset = static_cast<int>(m_autocompleteScrollOffset) + step;
    if (newOffset < 0) newOffset = 0;
    if (newOffset > static_cast<int>(maxScroll)) newOffset = static_cast<int>(maxScroll);
    m_autocompleteScrollOffset = static_cast<size_t>(newOffset);
    return true;
}

void EditorCanvas::SetAutocompleteSelectedIndex(size_t idx) noexcept {
    if (idx < m_autocompleteItems.size()) {
        m_autocompleteSelectedIdx = idx;
        const size_t maxVisible = 8;
        if (m_autocompleteSelectedIdx < m_autocompleteScrollOffset) {
            m_autocompleteScrollOffset = m_autocompleteSelectedIdx;
        } else if (m_autocompleteSelectedIdx >= m_autocompleteScrollOffset + maxVisible) {
            m_autocompleteScrollOffset = m_autocompleteSelectedIdx - maxVisible + 1;
        }
    }
}

void EditorCanvas::SelectNextAutocomplete() noexcept {
    if (m_autocompleteItems.empty()) return;
    m_autocompleteSelectedIdx = (m_autocompleteSelectedIdx + 1) % m_autocompleteItems.size();
    const size_t maxVisible = 8;
    if (m_autocompleteSelectedIdx < m_autocompleteScrollOffset) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx;
    } else if (m_autocompleteSelectedIdx >= m_autocompleteScrollOffset + maxVisible) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx - maxVisible + 1;
    }
}

void EditorCanvas::SelectPrevAutocomplete() noexcept {
    if (m_autocompleteItems.empty()) return;
    if (m_autocompleteSelectedIdx == 0) {
        m_autocompleteSelectedIdx = m_autocompleteItems.size() - 1;
    } else {
        m_autocompleteSelectedIdx--;
    }
    const size_t maxVisible = 8;
    if (m_autocompleteSelectedIdx < m_autocompleteScrollOffset) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx;
    } else if (m_autocompleteSelectedIdx >= m_autocompleteScrollOffset + maxVisible) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx - maxVisible + 1;
    }
}

const editor::IntelliSenseItem* EditorCanvas::GetSelectedAutocompleteItem() const noexcept {
    if (m_autocompleteItems.empty() || m_autocompleteSelectedIdx >= m_autocompleteItems.size()) {
        return nullptr;
    }
    return &m_autocompleteItems[m_autocompleteSelectedIdx];
}

bool EditorCanvas::HitTestAutocomplete(POINT pt, int& outIdx) const {
    outIdx = -1;
    if (!m_showAutocomplete || m_autocompleteItems.empty()) return false;
    if (pt.x < m_autocompleteBounds.left || pt.x > m_autocompleteBounds.right ||
        pt.y < m_autocompleteBounds.top || pt.y > m_autocompleteBounds.bottom) {
        return false;
    }

    float itemsTop = m_autocompleteBounds.top + 4.0f;
    float itemsBottom = m_autocompleteBounds.bottom - 26.0f;
    if (pt.y >= itemsTop && pt.y < itemsBottom) {
        size_t visibleCount = (std::min)(m_autocompleteItems.size(), static_cast<size_t>(8));
        if (visibleCount > 0) {
            float itemH = (itemsBottom - itemsTop) / static_cast<float>(visibleCount);
            size_t scrollOffset = m_autocompleteScrollOffset;



            int relIdx = static_cast<int>((pt.y - itemsTop) / itemH);
            if (relIdx >= 0 && relIdx < static_cast<int>(visibleCount)) {
                size_t actualIdx = scrollOffset + static_cast<size_t>(relIdx);
                if (actualIdx < m_autocompleteItems.size()) {
                    outIdx = static_cast<int>(actualIdx);
                }
            }
        }
    }
    return true;
}

void EditorCanvas::RenderAutocompletePopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                           float clientWidth, float clientHeight) {
    if (!pRT || !m_showAutocomplete || m_autocompleteItems.empty()) return;

    const float itemH = config.ScaleF(24.0f);
    const float footerH = config.ScaleF(26.0f);
    const size_t maxVisible = 8;
    const size_t totalItems = m_autocompleteItems.size();
    const size_t visibleCount = (std::min)(totalItems, maxVisible);

    // VS Code IntelliSense card dimensions (Image 5 benchmark: 440px wide for ample breathing room)
    float popupW = config.ScaleF(440.0f);
    if (popupW > clientWidth - config.ScaleF(24.0f)) {
        popupW = (std::max)(config.ScaleF(300.0f), clientWidth - config.ScaleF(24.0f));
    }
    const float popupH = static_cast<float>(visibleCount) * itemH + footerH + config.ScaleF(8.0f);

    float popupX = m_lastCaretX;
    if (popupX + popupW > clientWidth - config.ScaleF(12.0f)) {
        popupX = (std::max)(config.ScaleF(10.0f), clientWidth - popupW - config.ScaleF(12.0f));
    }
    float popupY = m_lastCaretY + config.ScaleF(22.0f);
    if (popupY + popupH > clientHeight - config.ScaleF(10.0f)) {
        popupY = (std::max)(config.ScaleF(35.0f), m_lastCaretY - popupH - config.ScaleF(4.0f));
    }

    m_autocompleteBounds = D2D1::RectF(popupX, popupY, popupX + popupW, popupY + popupH);

    // 1. Elevated soft drop shadow (Image 5)
    for (int s = 1; s <= 3; ++s) {
        float expand = static_cast<float>(s) * config.ScaleF(1.5f);
        float alpha = 0.03f + static_cast<float>(s) * 0.025f;
        D2D1_RECT_F rcShadow = D2D1::RectF(
            popupX - expand, popupY - expand * 0.5f + config.ScaleF(2.0f),
            popupX + popupW + expand, popupY + popupH + expand + config.ScaleF(2.0f)
        );
        D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(rcShadow, 6.0f + expand, 6.0f + expand);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, alpha), &shadowBrush);
        if (shadowBrush) pRT->FillRoundedRectangle(rrcShadow, shadowBrush.Get());
    }

    // 2. VS Code Card Body (#1e1e1e / #252526 dark card with subtle #3c3c3c border)
    D2D1_ROUNDED_RECT rrcPopup = D2D1::RoundedRect(m_autocompleteBounds, 6.0f, 6.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.13f, 0.98f) : D2D1::ColorF(0.97f, 0.97f, 0.98f, 0.98f),
        &bgBrush
    );
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.24f, 0.26f, 1.0f) : D2D1::ColorF(0.80f, 0.82f, 0.85f, 1.0f),
        &borderBrush
    );

    if (bgBrush) pRT->FillRoundedRectangle(rrcPopup, bgBrush.Get());
    if (borderBrush) pRT->DrawRoundedRectangle(rrcPopup, borderBrush.Get(), 1.0f);

    // Compute scroll window
    if (m_autocompleteSelectedIdx < m_autocompleteScrollOffset) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx;
    } else if (m_autocompleteSelectedIdx >= m_autocompleteScrollOffset + visibleCount) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx - visibleCount + 1;
    }
    if (totalItems > visibleCount) {
        if (m_autocompleteScrollOffset > totalItems - visibleCount) {
            m_autocompleteScrollOffset = totalItems - visibleCount;
        }
    } else {
        m_autocompleteScrollOffset = 0;
    }
    size_t scrollOffset = m_autocompleteScrollOffset;

    const bool hasScrollbar = (totalItems > visibleCount);
    float itemY = popupY + config.ScaleF(4.0f);

    for (size_t i = 0; i < visibleCount; ++i) {
        size_t itemIdx = scrollOffset + i;
        if (itemIdx >= totalItems) break;

        const auto& item = m_autocompleteItems[itemIdx];
        bool isSelected = (itemIdx == m_autocompleteSelectedIdx);

        D2D1_RECT_F rcItem = D2D1::RectF(popupX + config.ScaleF(4.0f), itemY, popupX + popupW - config.ScaleF(4.0f), itemY + itemH);

        // 3. Selection row highlight: Subtle VS Code blue #094771 / #264f78 (Image 5)
        if (isSelected) {
            D2D1_ROUNDED_RECT rrcSel = D2D1::RoundedRect(rcItem, 3.5f, 3.5f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.04f, 0.28f, 0.48f, 0.95f) : D2D1::ColorF(0.80f, 0.89f, 0.98f, 0.95f),
                &selBrush
            );
            if (selBrush) pRT->FillRoundedRectangle(rrcSel, selBrush.Get());
        }

        // Strict clipping for this item row: GUARANTEES no text leaks past row bounds
        pRT->PushAxisAlignedClip(rcItem, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // 4. Icon Badge on Left (Image 5)
        float iconSize = config.ScaleF(16.0f);
        float iconX = popupX + config.ScaleF(8.0f);
        float iconY = itemY + (itemH - iconSize) / 2.0f;
        D2D1_RECT_F rcIcon = D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize);

        D2D1_COLOR_F badgeColor = D2D1::ColorF(0.6f, 0.6f, 0.6f);
        const wchar_t* badgeLetter = L"ⓚ";
        switch (item.kind) {
            case editor::CompletionKind::Property:
                badgeColor = D2D1::ColorF(0.75f, 0.52f, 0.99f); badgeLetter = L"ⓟ"; break;
            case editor::CompletionKind::Method:
                badgeColor = D2D1::ColorF(0.51f, 0.55f, 0.97f); badgeLetter = L"ⓜ"; break;
            case editor::CompletionKind::Function:
                badgeColor = D2D1::ColorF(0.65f, 0.54f, 0.98f); badgeLetter = L"ⓕ"; break;
            case editor::CompletionKind::Variable:
                badgeColor = D2D1::ColorF(0.38f, 0.65f, 0.98f); badgeLetter = L"ⓥ"; break;
            case editor::CompletionKind::Keyword:
                badgeColor = D2D1::ColorF(0.96f, 0.45f, 0.71f); badgeLetter = L"ⓚ"; break;
            case editor::CompletionKind::Tag:
                badgeColor = D2D1::ColorF(0.29f, 0.87f, 0.50f); badgeLetter = L"ⓣ"; break;
            case editor::CompletionKind::Attribute:
                badgeColor = D2D1::ColorF(0.98f, 0.57f, 0.24f); badgeLetter = L"ⓐ"; break;
            case editor::CompletionKind::Snippet:
                badgeColor = D2D1::ColorF(0.22f, 0.74f, 0.97f); badgeLetter = L"ⓢ"; break;
            case editor::CompletionKind::Class:
                badgeColor = D2D1::ColorF(0.95f, 0.65f, 0.20f); badgeLetter = L"ⓒ"; break;
            case editor::CompletionKind::Constant:
                badgeColor = D2D1::ColorF(0.40f, 0.75f, 0.95f); badgeLetter = L"ⓒ"; break;
            case editor::CompletionKind::Module:
                badgeColor = D2D1::ColorF(0.85f, 0.45f, 0.85f); badgeLetter = L"ⓜ"; break;
            case editor::CompletionKind::Folder:
            case editor::CompletionKind::File:
                break;
        }

        // Render genuine explorer-style icons for Folders & Files, or badges for symbols (Images 4 & 5)
        if (item.kind == editor::CompletionKind::Folder) {
            auto setiInfo = theme.GetSetiIcon(item.label, true, false);
            if (theme.fontSeti && setiInfo.pBrush) {
                pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), rcIcon, setiInfo.pBrush);
            } else {
                m_icons.DrawFileIcon(pRT, theme, rcIcon, FileIconType::Folder);
            }
        } else if (item.kind == editor::CompletionKind::File) {
            auto setiInfo = theme.GetSetiIcon(item.label, false, false);
            if (theme.fontSeti && setiInfo.pBrush) {
                pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), rcIcon, setiInfo.pBrush);
            } else {
                auto fType = LucideIcons::GetIconTypeForPath(item.label, false, false);
                m_icons.DrawFileIcon(pRT, theme, rcIcon, fType);
            }
        } else {
            // Pill container for symbol badge (ⓟ, ⓜ, ⓕ, ⓥ, ⓚ, ⓣ, ⓐ, ⓢ)
            D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcIcon, 3.0f, 3.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBgBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(badgeColor.r, badgeColor.g, badgeColor.b, 0.15f), &badgeBgBrush);
            if (badgeBgBrush) pRT->FillRoundedRectangle(rrcBadge, badgeBgBrush.Get());

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBrush;
            pRT->CreateSolidColorBrush(badgeColor, &badgeBrush);
            if (badgeBrush) {
                pRT->DrawText(badgeLetter, 1, theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get(), rcIcon, badgeBrush.Get());
            }
        }

        // 5. Origin Tag on Far Right (e.g. "builtins", "snippets", "html", "css", right-aligned)
        float originW = config.ScaleF(76.0f);
        float originRight = popupX + popupW - config.ScaleF(hasScrollbar ? 16.0f : 10.0f);
        float originLeft = originRight - originW;

        std::wstring originText = item.origin;
        if (originText.empty()) {
            switch (item.kind) {
                case editor::CompletionKind::Snippet: originText = L"snippets"; break;
                case editor::CompletionKind::Tag:
                case editor::CompletionKind::Attribute: originText = L"html"; break;
                case editor::CompletionKind::Folder:
                case editor::CompletionKind::File: originText = L"path"; break;
                case editor::CompletionKind::Property: originText = L"css"; break;
                case editor::CompletionKind::Method:
                case editor::CompletionKind::Function:
                case editor::CompletionKind::Variable: originText = L"builtins"; break;
                case editor::CompletionKind::Keyword: originText = L"keyword"; break;
                default: originText = L"code"; break;
            }
        }

        D2D1_RECT_F rcOrigin = D2D1::RectF(originLeft, itemY + config.ScaleF(2.0f), originRight, itemY + itemH);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> originBrush;
        pRT->CreateSolidColorBrush(
            isSelected
                ? (theme.IsDarkMode() ? D2D1::ColorF(0.70f, 0.78f, 0.88f) : D2D1::ColorF(0.35f, 0.45f, 0.60f))
                : (theme.IsDarkMode() ? D2D1::ColorF(0.50f, 0.52f, 0.56f) : D2D1::ColorF(0.55f, 0.57f, 0.62f)),
            &originBrush
        );
        if (originBrush) {
            // fontMenuShortcut has DWRITE_TEXT_ALIGNMENT_TRAILING for right-alignment
            pRT->DrawText(originText.c_str(), static_cast<UINT32>(originText.length()),
                          theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                          rcOrigin, originBrush.Get());
        }

        // 6. Label Text with Measured Width & Highlight (Image 5)
        float labelX = iconX + iconSize + config.ScaleF(8.0f);
        float maxLabelAvailW = originLeft - labelX - config.ScaleF(12.0f);

        float measuredLabelW = 0.0f;
        if (theme.dwriteFactory && theme.fontMenuItem) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pMeasureLayout;
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    item.label.c_str(), static_cast<UINT32>(item.label.length()),
                    theme.fontMenuItem.Get(), (std::max)(100.0f, maxLabelAvailW), itemH, &pMeasureLayout))) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(pMeasureLayout->GetMetrics(&tm))) {
                    measuredLabelW = tm.widthIncludingTrailingWhitespace;
                }
            }
        }
        if (measuredLabelW <= 0.0f) {
            measuredLabelW = static_cast<float>(item.label.length()) * config.ScaleF(7.5f);
        }

        float actualLabelW = (std::min)(measuredLabelW + config.ScaleF(2.0f), maxLabelAvailW * 0.55f);
        D2D1_RECT_F rcLabel = D2D1::RectF(labelX, itemY + config.ScaleF(2.0f), labelX + actualLabelW, itemY + itemH);
        ID2D1Brush* textBrush = isSelected ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();

        // Highlight matched characters within item label
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pTextLayout;
        if (theme.dwriteFactory && theme.fontMenuItem) {
            theme.dwriteFactory->CreateTextLayout(
                item.label.c_str(),
                static_cast<UINT32>(item.label.length()),
                theme.fontMenuItem.Get(),
                actualLabelW + config.ScaleF(4.0f),
                itemH,
                &pTextLayout
            );
        }

        if (pTextLayout) {
            if (item.matchLen > 0 && item.matchStart < item.label.length()) {
                UINT32 mLen = static_cast<UINT32>((std::min)(item.matchLen, item.label.length() - item.matchStart));
                DWRITE_TEXT_RANGE matchRange = { static_cast<UINT32>(item.matchStart), mLen };
                pTextLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, matchRange);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hlBrush;
                if (isSelected) {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &hlBrush); // crisp bold white on selection
                } else {
                    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.22f, 0.62f, 1.0f) : D2D1::ColorF(0.0f, 0.45f, 0.88f), &hlBrush); // subtle VS Code blue
                }
                if (hlBrush) {
                    pTextLayout->SetDrawingEffect(hlBrush.Get(), matchRange);
                }
            }
            pRT->DrawTextLayout(D2D1::Point2F(labelX, itemY + config.ScaleF(2.0f)), pTextLayout.Get(), textBrush);
        } else {
            pRT->DrawText(item.label.c_str(), static_cast<UINT32>(item.label.length()),
                          theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                          rcLabel, textBrush);
        }

        // 7. Detail text with strict clipping (e.g. "(values, sep, end, file, flush)" or "CSSStyleDeclaration.border")
        float detailX = labelX + actualLabelW + config.ScaleF(8.0f);
        float detailRight = originLeft - config.ScaleF(6.0f);
        if (!item.detail.empty() && detailRight > detailX + config.ScaleF(10.0f)) {
            D2D1_RECT_F rcDetail = D2D1::RectF(detailX, itemY + config.ScaleF(2.0f), detailRight, itemY + itemH);
            pRT->PushAxisAlignedClip(rcDetail, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            ID2D1Brush* detailBrush = isSelected
                ? (theme.IsDarkMode() ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get())
                : theme.brushTextMuted.Get();
            pRT->DrawText(item.detail.c_str(), static_cast<UINT32>(item.detail.length()),
                          theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                          rcDetail, detailBrush);
            pRT->PopAxisAlignedClip();
        }

        pRT->PopAxisAlignedClip(); // End item row clip
        itemY += itemH;
    }

    // 8. Vertical scrollbar on right if totalItems > visibleCount
    if (totalItems > visibleCount) {
        float sbW = config.ScaleF(4.0f);
        float sbPad = config.ScaleF(3.0f);
        float sbRight = popupX + popupW - sbPad;
        float sbLeft = sbRight - sbW;
        float sbTop = popupY + config.ScaleF(4.0f);
        float sbH = static_cast<float>(visibleCount) * itemH;

        D2D1_ROUNDED_RECT rrcSbTrack = D2D1::RoundedRect(D2D1::RectF(sbLeft, sbTop, sbRight, sbTop + sbH), 2.0f, 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.15f), &trackBrush);
        if (trackBrush) pRT->FillRoundedRectangle(rrcSbTrack, trackBrush.Get());

        float maxScroll = static_cast<float>(totalItems - visibleCount);
        float thumbH = (std::max)(config.ScaleF(14.0f), sbH * (static_cast<float>(visibleCount) / static_cast<float>(totalItems)));
        float thumbY = sbTop + (static_cast<float>(m_autocompleteScrollOffset) / maxScroll) * (sbH - thumbH);

        D2D1_ROUNDED_RECT rrcSbThumb = D2D1::RoundedRect(D2D1::RectF(sbLeft, thumbY, sbRight, thumbY + thumbH), 2.0f, 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> thumbBrush;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.50f, 0.52f, 0.58f, 0.7f) : D2D1::ColorF(0.60f, 0.62f, 0.68f, 0.8f), &thumbBrush);
        if (thumbBrush) pRT->FillRoundedRectangle(rrcSbThumb, thumbBrush.Get());
    }

    // 9. Bottom Tip Bar (Image 5 benchmark: clipped, clean separator, Next Tip & ⋮ icon)
    float footerY = itemY + config.ScaleF(2.0f);
    pRT->DrawLine(
        D2D1::Point2F(popupX + config.ScaleF(4.0f), footerY),
        D2D1::Point2F(popupX + popupW - config.ScaleF(4.0f), footerY),
        borderBrush.Get(),
        0.8f
    );

    D2D1_RECT_F rcFooter = D2D1::RectF(popupX + config.ScaleF(4.0f), footerY, popupX + popupW - config.ScaleF(4.0f), popupY + popupH);
    pRT->PushAxisAlignedClip(rcFooter, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Left Tip Text
    const wchar_t* tipText = L"Press Tab to insert \u2022 \u2191\u2193 to navigate";
    D2D1_RECT_F rcTip = D2D1::RectF(popupX + config.ScaleF(8.0f), footerY + config.ScaleF(3.0f), popupX + popupW - config.ScaleF(88.0f), popupY + popupH);
    pRT->DrawText(tipText, static_cast<UINT32>(wcslen(tipText)),
                  theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                  rcTip, theme.brushTextMuted.Get());

    // Right "Next Tip" link
    const wchar_t* nextTipText = L"Next Tip";
    D2D1_RECT_F rcNextTip = D2D1::RectF(popupX + popupW - config.ScaleF(84.0f), footerY + config.ScaleF(3.0f), popupX + popupW - config.ScaleF(24.0f), popupY + popupH);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> linkBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.60f, 0.95f), &linkBrush);
    if (linkBrush) {
        pRT->DrawText(nextTipText, static_cast<UINT32>(wcslen(nextTipText)),
                      theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                      rcNextTip, linkBrush.Get());
    }

    // Right "⋮" menu dots
    const wchar_t* dotsText = L"\u22EE";
    D2D1_RECT_F rcDots = D2D1::RectF(popupX + popupW - config.ScaleF(20.0f), footerY + config.ScaleF(3.0f), popupX + popupW - config.ScaleF(6.0f), popupY + popupH);
    pRT->DrawText(dotsText, 1,
                  theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                  rcDots, theme.brushTextMuted.Get());

    pRT->PopAxisAlignedClip(); // End footer clip
}

void EditorCanvas::RenderCanvasThemeEffects(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                            float canvasLeft, float canvasWidth, float topY, float clientHeight) {
    if (!pRT || canvasWidth <= 20.0f || clientHeight <= topY) return;

    ThemeMode mode = theme.GetThemeMode();
    if (mode == ThemeMode::RetroGrid80s) {
        // Synthwave 80s 3D Perspective Wireframe Grid (Image 5)
        const float gridH = (std::min)(350.0f, (clientHeight - topY) * 0.55f);
        const float horizonY = clientHeight - gridH;
        const float centerX = canvasLeft + canvasWidth * 0.5f;

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushNeonPink;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushNeonCyan;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHorizon;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.165f, 0.522f, 0.28f), &brushNeonPink); // #FF2A85
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.941f, 1.0f, 0.22f), &brushNeonCyan);   // #00F0FF
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.165f, 0.522f, 0.45f), &brushHorizon);

        if (brushHorizon) {
            pRT->DrawLine(D2D1::Point2F(canvasLeft, horizonY), D2D1::Point2F(canvasLeft + canvasWidth, horizonY), brushHorizon.Get(), 1.5f);
        }

        // Perspective depth lines converging toward vanishing point
        if (brushNeonPink) {
            const int numLines = 14;
            for (int i = -numLines; i <= numLines; ++i) {
                float bottomX = centerX + static_cast<float>(i) * (canvasWidth / static_cast<float>(numLines * 1.5f));
                float topX = centerX + static_cast<float>(i) * (canvasWidth * 0.05f / static_cast<float>(numLines));
                pRT->DrawLine(D2D1::Point2F(topX, horizonY), D2D1::Point2F(bottomX, clientHeight), brushNeonPink.Get(), 1.0f);
            }
        }

        // Horizontal perspective lines with quadratic spacing
        if (brushNeonCyan) {
            const int numHoriz = 10;
            for (int h = 1; h <= numHoriz; ++h) {
                float t = static_cast<float>(h) / static_cast<float>(numHoriz);
                float nonLinearT = t * t;
                float lineY = horizonY + nonLinearT * gridH;
                float alpha = 0.10f + 0.25f * t;
                brushNeonCyan->SetOpacity(alpha);
                pRT->DrawLine(D2D1::Point2F(canvasLeft, lineY), D2D1::Point2F(canvasLeft + canvasWidth, lineY), brushNeonCyan.Get(), 1.0f);
            }
        }
    } else if (mode == ThemeMode::CosmicStarfield) {
        // Deep Space Cosmic Twinkling Stars & Particles
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushStar;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.92f, 1.0f, 0.6f), &brushStar);
        if (brushStar) {
            const int starCount = 50;
            for (int i = 0; i < starCount; ++i) {
                float seedX = static_cast<float>((i * 1973 + 13) % 997) / 997.0f;
                float seedY = static_cast<float>((i * 4729 + 17) % 991) / 991.0f;
                float seedR = static_cast<float>((i * 7919 + 23) % 100) / 100.0f;

                float starX = canvasLeft + seedX * canvasWidth;
                float starY = topY + seedY * (clientHeight - topY);
                float starRadius = (seedR > 0.85f) ? 1.8f : ((seedR > 0.5f) ? 1.2f : 0.8f);
                float starAlpha = (seedR > 0.85f) ? 0.75f : ((seedR > 0.5f) ? 0.50f : 0.30f);

                if (i % 3 == 0) {
                    brushStar->SetColor(D2D1::ColorF(0.655f, 0.545f, 0.980f, starAlpha)); // Nebula Purple
                } else if (i % 3 == 1) {
                    brushStar->SetColor(D2D1::ColorF(0.380f, 0.750f, 1.000f, starAlpha)); // Cyan Star
                } else {
                    brushStar->SetColor(D2D1::ColorF(1.000f, 0.950f, 0.800f, starAlpha)); // Pale Gold Star
                }
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(starX, starY), starRadius, starRadius), brushStar.Get());
            }
        }
    } else if (mode == ThemeMode::MatrixDigitalRain) {
        // Matrix Green Terminal Code Drops
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushMatrix;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.4f, 0.35f), &brushMatrix);
        if (brushMatrix) {
            const int columnCount = 28;
            for (int c = 0; c < columnCount; ++c) {
                float colX = canvasLeft + (static_cast<float>(c) + 0.5f) * (canvasWidth / static_cast<float>(columnCount));
                float seedY = static_cast<float>((c * 317 + 41) % 997) / 997.0f;
                float streamLen = 80.0f + static_cast<float>((c * 191) % 120);
                float startY = topY + seedY * (clientHeight - topY);

                brushMatrix->SetColor(D2D1::ColorF(0.0f, 1.0f, 0.4f, 0.12f));
                pRT->DrawLine(D2D1::Point2F(colX, startY), D2D1::Point2F(colX, (std::min)(clientHeight, startY + streamLen)), brushMatrix.Get(), 1.5f);

                brushMatrix->SetColor(D2D1::ColorF(0.8f, 1.0f, 0.867f, 0.45f));
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(colX, (std::min)(clientHeight - 2.0f, startY + streamLen)), 1.8f, 1.8f), brushMatrix.Get());
            }
        }
    } else if (mode == ThemeMode::AuroraBorealis) {
        // Shimmering Arctic Aurora Polar Teal Curtains
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushAurora;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.176f, 0.831f, 0.749f, 0.08f), &brushAurora);
        if (brushAurora) {
            float auroraMidY = topY + (clientHeight - topY) * 0.35f;
            for (int w = 0; w < 4; ++w) {
                float yOff = static_cast<float>(w) * 16.0f;
                brushAurora->SetColor(D2D1::ColorF(0.176f, 0.831f, 0.749f, 0.06f - w * 0.012f));
                pRT->DrawLine(D2D1::Point2F(canvasLeft, auroraMidY + yOff), D2D1::Point2F(canvasLeft + canvasWidth, auroraMidY - yOff * 0.5f), brushAurora.Get(), 8.0f);
            }
        }
    } else if (mode == ThemeMode::EmeraldForest) {
        // Glowing Fireflies Particles in deep emerald jungle
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushFirefly;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.639f, 0.902f, 0.208f, 0.5f), &brushFirefly); // #A3E635
        if (brushFirefly) {
            const int fireflyCount = 24;
            for (int f = 0; f < fireflyCount; ++f) {
                float fx = canvasLeft + (static_cast<float>((f * 617 + 11) % 991) / 991.0f) * canvasWidth;
                float fy = topY + (static_cast<float>((f * 883 + 19) % 983) / 983.0f) * (clientHeight - topY);
                float glowR = 3.5f + static_cast<float>((f * 29) % 4);
                brushFirefly->SetColor(D2D1::ColorF(0.639f, 0.902f, 0.208f, 0.15f));
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fx, fy), glowR, glowR), brushFirefly.Get());
                brushFirefly->SetColor(D2D1::ColorF(0.85f, 1.0f, 0.4f, 0.65f));
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fx, fy), 1.2f, 1.2f), brushFirefly.Get());
            }
        }
    } else if (mode == ThemeMode::SunsetGlow) {
        // Sunset Horizon Glow at the bottom
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDusk;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.369f, 0.494f, 0.08f), &brushDusk); // Coral #FF5E7E
        if (brushDusk) {
            float duskH = (clientHeight - topY) * 0.35f;
            float duskY = clientHeight - duskH;
            D2D1_RECT_F rcDusk = D2D1::RectF(canvasLeft, duskY, canvasLeft + canvasWidth, clientHeight);
            pRT->FillRectangle(rcDusk, brushDusk.Get());
            brushDusk->SetColor(D2D1::ColorF(1.0f, 0.459f, 0.337f, 0.18f)); // Amber #FF7556
            pRT->DrawLine(D2D1::Point2F(canvasLeft, duskY), D2D1::Point2F(canvasLeft + canvasWidth, duskY), brushDusk.Get(), 1.0f);
        }
    }
}

void EditorCanvas::RenderSyntaxTokensLine(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                         const editor::Buffer& buffer, size_t lineIdx,
                                         float codeLeft, float lineY, float maxRight,
                                         float lineH, float charW, IDWriteTextFormat* pCodeFmt,
                                         ID2D1Brush* defaultTextBrush) {
    std::wstring lineStr = buffer.GetLine(lineIdx);
    if (lineStr.empty() || codeLeft >= maxRight) return;

    auto tokens = buffer.GetTokensForLine(lineIdx);
    if (tokens.empty()) {
        D2D1_RECT_F rcLine = D2D1::RectF(codeLeft, lineY, maxRight, lineY + lineH);
        pRT->DrawText(lineStr.c_str(), static_cast<UINT32>(lineStr.length()), pCodeFmt, rcLine,
                      defaultTextBrush ? defaultTextBrush : theme.brushTextPrimary.Get());
        return;
    }

    for (const auto& tok : tokens) {
        if (tok.startCol + tok.length > lineStr.length()) continue;
        float tokenX = codeLeft + static_cast<float>(tok.startCol) * charW;
        float tokenW = static_cast<float>(tok.length) * charW;
        if (tokenX >= maxRight) break;

        D2D1_RECT_F rcTok = D2D1::RectF(tokenX, lineY, (std::min)(tokenX + tokenW + config.ScaleF(4.0f), maxRight), lineY + lineH);

        ID2D1Brush* brush = defaultTextBrush ? defaultTextBrush : theme.brushTextPrimary.Get();
        switch (tok.type) {
            case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
            case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
            case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
            case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
            case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
            case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
            case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
            case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
            default: break;
        }
        if (!brush) brush = theme.brushTextPrimary.Get();

        pRT->DrawText(
            lineStr.c_str() + tok.startCol,
            static_cast<UINT32>(tok.length),
            pCodeFmt,
            rcTok,
            brush
        );
    }
}

void EditorCanvas::RenderDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                  float topY, float clientHeight, bool isFocused) {
    const float subHeaderH = config.ScaleF(24.0f);
    const float dividerW = config.ScaleF(32.0f);
    const float scrollbarW = config.ScaleF(12.0f);
    const float halfW = (canvasWidth - dividerW - scrollbarW) / 2.0f;
    const float gutterW = config.ScaleF(46.0f);
    const float lineH = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));
    const float charW = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);

    const float leftX = canvasLeft;
    const float dividerX = leftX + halfW;
    const float rightX = dividerX + dividerW;

    // Brushes for diff styling
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.18f, 0.20f, 0.28f), &brushDiffRedBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedWord;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.22f, 0.25f, 0.50f), &brushDiffRedWord);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedBar;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushDiffRedBar);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.20f, 0.28f), &brushDiffGreenBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenWord;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.65f, 0.25f, 0.50f), &brushDiffGreenWord);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenBar;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), &brushDiffGreenBar);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBridgeModBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.20f, 0.25f, 0.35f), &brushBridgeModBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBridgeModBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.25f, 0.30f, 0.60f), &brushBridgeModBorder);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBridgeAddBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.25f, 0.35f), &brushBridgeAddBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBridgeAddBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.65f, 0.35f, 0.60f), &brushBridgeAddBorder);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDividerBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.12f, 0.85f), &brushDividerBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillRoBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.26f, 0.9f), &brushPillRoBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillEditBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.055f, 0.388f, 0.612f, 0.9f), &brushPillEditBg);

    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pLabelFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();

    // =========================================================================
    // 1. Sub-headers (Left = HEAD / Read-only, Right = Working Tree / Editable)
    // =========================================================================
    D2D1_RECT_F rcLeftSub = D2D1::RectF(leftX, topY, dividerX, topY + subHeaderH);
    pRT->FillRectangle(rcLeftSub, theme.brushHeaderBg.Get());
    pRT->DrawLine(D2D1::Point2F(leftX, topY + subHeaderH), D2D1::Point2F(dividerX, topY + subHeaderH), theme.brushBorderSubtle.Get(), 1.0f);

    std::wstring leftTitle = doc.diffOriginalTitle.empty() ? (doc.fileName + L" (HEAD)") : doc.diffOriginalTitle;
    D2D1_RECT_F rcLeftTitle = D2D1::RectF(leftX + config.ScaleF(12.0f), topY + 2.0f, dividerX - config.ScaleF(80.0f), topY + subHeaderH - 2.0f);
    pRT->DrawText(leftTitle.c_str(), static_cast<UINT32>(leftTitle.length()),
                  pLabelFmt, rcLeftTitle, theme.brushTextPrimary.Get());

    // Left [Read-only] badge
    float roW = config.ScaleF(68.0f);
    float roH = config.ScaleF(16.0f);
    D2D1_ROUNDED_RECT rrcRo = D2D1::RoundedRect(
        D2D1::RectF(dividerX - roW - config.ScaleF(8.0f), topY + (subHeaderH - roH) / 2.0f, dividerX - config.ScaleF(8.0f), topY + (subHeaderH + roH) / 2.0f),
        3.0f, 3.0f
    );
    pRT->FillRoundedRectangle(rrcRo, brushPillRoBg.Get());
    pRT->DrawText(L"Read-only", 9, pLabelFmt,
                  D2D1::RectF(rrcRo.rect.left + 5.0f, rrcRo.rect.top, rrcRo.rect.right, rrcRo.rect.bottom),
                  theme.brushTextMuted.Get());

    // Right Sub-header
    D2D1_RECT_F rcRightSub = D2D1::RectF(rightX, topY, rightX + halfW, topY + subHeaderH);
    pRT->FillRectangle(rcRightSub, theme.brushHeaderBg.Get());
    pRT->DrawLine(D2D1::Point2F(rightX, topY + subHeaderH), D2D1::Point2F(rightX + halfW, topY + subHeaderH), theme.brushBorderSubtle.Get(), 1.0f);

    std::wstring rightTitle = doc.diffWorkingTitle.empty() ? (doc.fileName + L" (Working Tree)") : doc.diffWorkingTitle;
    D2D1_RECT_F rcRightTitle = D2D1::RectF(rightX + config.ScaleF(12.0f), topY + 2.0f, rightX + halfW - config.ScaleF(80.0f), topY + subHeaderH - 2.0f);
    pRT->DrawText(rightTitle.c_str(), static_cast<UINT32>(rightTitle.length()),
                  pLabelFmt, rcRightTitle, theme.brushTextPrimary.Get());

    // Right [Editable] badge (or Read-only if commit diff)
    const wchar_t* rightBadgeText = doc.isDiffRightReadOnly ? L"Read-only" : L"Editable";
    float editW = config.ScaleF(doc.isDiffRightReadOnly ? 68.0f : 56.0f);
    D2D1_ROUNDED_RECT rrcEdit = D2D1::RoundedRect(
        D2D1::RectF(rightX + halfW - editW - config.ScaleF(8.0f), topY + (subHeaderH - roH) / 2.0f, rightX + halfW - config.ScaleF(8.0f), topY + (subHeaderH + roH) / 2.0f),
        3.0f, 3.0f
    );
    pRT->FillRoundedRectangle(rrcEdit, doc.isDiffRightReadOnly ? brushPillRoBg.Get() : brushPillEditBg.Get());
    pRT->DrawText(rightBadgeText, static_cast<UINT32>(wcslen(rightBadgeText)), pLabelFmt,
                  D2D1::RectF(rrcEdit.rect.left + (doc.isDiffRightReadOnly ? 5.0f : 6.0f), rrcEdit.rect.top, rrcEdit.rect.right, rrcEdit.rect.bottom),
                  doc.isDiffRightReadOnly ? theme.brushTextMuted.Get() : theme.brushTextWhite.Get());

    // =========================================================================
    // 2. Code Area Layout & Scrolling
    // =========================================================================
    float codeTopY = topY + subHeaderH;
    float codeAreaH = clientHeight - codeTopY;
    size_t visibleCount = (lineH > 0.0f) ? static_cast<size_t>(codeAreaH / lineH) : 20;

    size_t leftTotal = doc.diffOriginalBuffer.GetLineCount();
    size_t rightTotal = doc.buffer.GetLineCount();
    size_t maxLines = (std::max)(leftTotal, rightTotal);
    size_t maxScroll = (maxLines > visibleCount) ? (maxLines - visibleCount) : 0;
    if (doc.scrollLine > maxScroll) doc.scrollLine = maxScroll;

    // =========================================================================
    // 3. Middle Divider (Connecting bridges between diff hunks) (Images 3 & 4)
    // =========================================================================
    D2D1_RECT_F rcDivider = D2D1::RectF(dividerX, codeTopY, dividerX + dividerW, clientHeight);
    pRT->FillRectangle(rcDivider, brushDividerBg.Get());
    pRT->DrawLine(D2D1::Point2F(dividerX, codeTopY), D2D1::Point2F(dividerX, clientHeight), theme.brushBorderSubtle.Get(), 1.0f);
    pRT->DrawLine(D2D1::Point2F(dividerX + dividerW, codeTopY), D2D1::Point2F(dividerX + dividerW, clientHeight), theme.brushBorderSubtle.Get(), 1.0f);

    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);

    for (const auto& hunk : doc.diffHunks) {
        float yLStart = codeTopY + (static_cast<float>(hunk.leftStart) - static_cast<float>(doc.scrollLine)) * lineH;
        float yLEnd = yLStart + static_cast<float>(hunk.leftCount) * lineH;
        if (hunk.leftCount == 0) yLEnd = yLStart;

        float yRStart = codeTopY + (static_cast<float>(hunk.rightStart) - static_cast<float>(doc.scrollLine)) * lineH;
        float yREnd = yRStart + static_cast<float>(hunk.rightCount) * lineH;
        if (hunk.rightCount == 0) yREnd = yRStart;

        if (pFactory && ((yLEnd >= codeTopY && yLStart <= clientHeight) || (yREnd >= codeTopY && yRStart <= clientHeight))) {
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pBridge;
            if (SUCCEEDED(pFactory->CreatePathGeometry(&pBridge))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pBridge->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(dividerX, yLStart), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddBezier(D2D1::BezierSegment(
                        D2D1::Point2F(dividerX + dividerW * 0.45f, yLStart),
                        D2D1::Point2F(dividerX + dividerW * 0.55f, yRStart),
                        D2D1::Point2F(dividerX + dividerW, yRStart)
                    ));
                    sink->AddLine(D2D1::Point2F(dividerX + dividerW, yREnd));
                    sink->AddBezier(D2D1::BezierSegment(
                        D2D1::Point2F(dividerX + dividerW * 0.55f, yREnd),
                        D2D1::Point2F(dividerX + dividerW * 0.45f, yLEnd),
                        D2D1::Point2F(dividerX, yLEnd)
                    ));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();

                    bool isAdd = (hunk.type == editor::OpenDocument::DiffHunkItem::HunkType::Added);
                    pRT->FillGeometry(pBridge.Get(), isAdd ? brushBridgeAddBg.Get() : brushBridgeModBg.Get());
                    pRT->DrawGeometry(pBridge.Get(), isAdd ? brushBridgeAddBorder.Get() : brushBridgeModBorder.Get(), 1.0f);
                }
            }
        }
    }

    // =========================================================================
    // 4. Left Pane (Original / HEAD - Read Only)
    // =========================================================================
    D2D1_RECT_F rcLeftClip = D2D1::RectF(leftX, codeTopY, dividerX, clientHeight);
    pRT->PushAxisAlignedClip(rcLeftClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Gutter Background
    D2D1_RECT_F rcLeftGutter = D2D1::RectF(leftX, codeTopY, leftX + gutterW, clientHeight);
    pRT->FillRectangle(rcLeftGutter, theme.brushCanvasBg.Get());

    size_t endLeftV = (std::min)(leftTotal, doc.scrollLine + visibleCount + 2);
    for (size_t v = doc.scrollLine; v < endLeftV; ++v) {
        float lineY = codeTopY + (v - doc.scrollLine) * lineH;
        if (lineY + lineH < codeTopY || lineY > clientHeight) continue;

        bool isChanged = doc.diffDeletedLines.count(v) > 0;
        if (isChanged) {
            // Full line red background
            D2D1_RECT_F rcLine = D2D1::RectF(leftX + gutterW, lineY, dividerX, lineY + lineH);
            pRT->FillRectangle(rcLine, brushDiffRedBg.Get());

            // Red vertical bar in gutter
            D2D1_RECT_F rcBar = D2D1::RectF(leftX + gutterW - config.ScaleF(3.0f), lineY, leftX + gutterW, lineY + lineH);
            pRT->FillRectangle(rcBar, brushDiffRedBar.Get());
        }

        // Line number (11- if changed)
        std::wstring numStr = std::to_wstring(v + 1) + (isChanged ? L"-" : L"");
        D2D1_RECT_F rcNum = D2D1::RectF(leftX + config.ScaleF(4.0f), lineY, leftX + gutterW - config.ScaleF(10.0f), lineY + lineH);
        pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()), theme.fontMenuShortcut.Get(),
                      rcNum, isChanged ? brushDiffRedBar.Get() : theme.brushLineNumber.Get());

        // Line text with syntax highlighting
        float textX = leftX + gutterW + config.ScaleF(12.0f);
        RenderSyntaxTokensLine(pRT, theme, config, doc.diffOriginalBuffer, v, textX, lineY, dividerX, lineH, charW, pCodeFmt, isChanged ? brushDiffRedBar.Get() : theme.brushTextPrimary.Get());
    }
    pRT->PopAxisAlignedClip();

    // =========================================================================
    // 5. Right Pane (Working Tree - EDITABLE) (Images 2, 3, 4)
    // =========================================================================
    D2D1_RECT_F rcRightClip = D2D1::RectF(rightX, codeTopY, rightX + halfW, clientHeight);
    pRT->PushAxisAlignedClip(rcRightClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Gutter Background
    D2D1_RECT_F rcRightGutter = D2D1::RectF(rightX, codeTopY, rightX + gutterW, clientHeight);
    pRT->FillRectangle(rcRightGutter, theme.brushCanvasBg.Get());

    auto cursor = doc.buffer.GetCursor();
    bool isRightActive = (doc.diffActivePane == 1 && isFocused);

    size_t endRightV = (std::min)(rightTotal, doc.scrollLine + visibleCount + 2);
    for (size_t v = doc.scrollLine; v < endRightV; ++v) {
        float lineY = codeTopY + (v - doc.scrollLine) * lineH;
        if (lineY + lineH < codeTopY || lineY > clientHeight) continue;

        // Active line highlight
        if (isRightActive && v == cursor.line) {
            D2D1_RECT_F rcActive = D2D1::RectF(rightX + gutterW, lineY, rightX + halfW, lineY + lineH);
            pRT->FillRectangle(rcActive, theme.brushCurrentLineBg.Get());
        }

        bool isChanged = doc.diffAddedLines.count(v) > 0;
        if (isChanged) {
            // Full line green background
            D2D1_RECT_F rcLine = D2D1::RectF(rightX + gutterW, lineY, rightX + halfW, lineY + lineH);
            pRT->FillRectangle(rcLine, brushDiffGreenBg.Get());

            // Green vertical bar in gutter
            D2D1_RECT_F rcBar = D2D1::RectF(rightX + gutterW - config.ScaleF(3.0f), lineY, rightX + gutterW, lineY + lineH);
            pRT->FillRectangle(rcBar, brushDiffGreenBar.Get());
        }

        // Line number (11+ if changed)
        std::wstring numStr = std::to_wstring(v + 1) + (isChanged ? L"+" : L"");
        D2D1_RECT_F rcNum = D2D1::RectF(rightX + config.ScaleF(4.0f), lineY, rightX + gutterW - config.ScaleF(10.0f), lineY + lineH);
        pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()), theme.fontMenuShortcut.Get(),
                      rcNum, isChanged ? brushDiffGreenBar.Get() : ((isRightActive && v == cursor.line) ? theme.brushTextPrimary.Get() : theme.brushLineNumber.Get()));

        // Line text with syntax highlighting
        float textX = rightX + gutterW + config.ScaleF(12.0f);
        RenderSyntaxTokensLine(pRT, theme, config, doc.buffer, v, textX, lineY, rightX + halfW, lineH, charW, pCodeFmt, isChanged ? brushDiffGreenBar.Get() : theme.brushTextPrimary.Get());
    }

    // Thin Deletion Line Indicators on Right Pane (Image 4 Request)
    // Instead of skipping a line to show changes, draw a thin horizontal line marker!
    for (const auto& hunk : doc.diffHunks) {
        if (hunk.type == editor::OpenDocument::DiffHunkItem::HunkType::Deleted && hunk.rightCount == 0) {
            if (hunk.rightStart >= doc.scrollLine && hunk.rightStart <= doc.scrollLine + visibleCount + 1) {
                float thinY = codeTopY + (static_cast<float>(hunk.rightStart) - static_cast<float>(doc.scrollLine)) * lineH;
                // Thin horizontal red deletion marker
                pRT->DrawLine(
                    D2D1::Point2F(rightX + gutterW, thinY),
                    D2D1::Point2F(rightX + halfW, thinY),
                    brushDiffRedBar.Get(), 2.2f
                );
                // Small indicator triangle in gutter
                float triX = rightX + gutterW - config.ScaleF(10.0f);
                pRT->DrawLine(D2D1::Point2F(triX, thinY - 3.0f), D2D1::Point2F(triX + 4.5f, thinY), brushDiffRedBar.Get(), 1.6f);
                pRT->DrawLine(D2D1::Point2F(triX + 4.5f, thinY), D2D1::Point2F(triX, thinY + 3.0f), brushDiffRedBar.Get(), 1.6f);
            }
        }
    }

    // Blinking Caret '|' on Editable Right Pane (Image 2)
    if (isRightActive && !doc.isDiffRightReadOnly && cursor.line >= doc.scrollLine && cursor.line <= doc.scrollLine + visibleCount) {
        float caretY = codeTopY + (cursor.line - doc.scrollLine) * lineH;
        float caretX = rightX + gutterW + config.ScaleF(12.0f) + static_cast<float>(cursor.col) * charW;
        m_lastCaretX = caretX;
        m_lastCaretY = caretY;
        if (m_caretVisible && caretX < rightX + halfW) {
            pRT->DrawLine(
                D2D1::Point2F(caretX, caretY + config.ScaleF(2.0f)),
                D2D1::Point2F(caretX, caretY + lineH - config.ScaleF(2.0f)),
                theme.brushTextPrimary.Get(), 1.5f
            );
        }
    }

    pRT->PopAxisAlignedClip();

    // =========================================================================
    // 6. Vertical Scrollbar on Far Right
    // =========================================================================
    float sbLeft = rightX + halfW;
    D2D1_RECT_F rcTrack = D2D1::RectF(sbLeft, codeTopY, sbLeft + scrollbarW, clientHeight);
    doc.scrollbarTrack = rcTrack;
    pRT->FillRectangle(rcTrack, theme.brushCanvasBg.Get());

    if (maxLines > visibleCount) {
        float thumbRatio = (static_cast<float>(visibleCount) / static_cast<float>(maxLines));
        float thumbH = (std::max)(config.ScaleF(24.0f), codeAreaH * thumbRatio);
        float scrollPct = (maxScroll > 0) ? (static_cast<float>(doc.scrollLine) / static_cast<float>(maxScroll)) : 0.0f;
        float thumbY = codeTopY + (codeAreaH - thumbH) * scrollPct;
        doc.scrollbarThumb = D2D1::RectF(sbLeft + 2.0f, thumbY, sbLeft + scrollbarW - 2.0f, thumbY + thumbH);
        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(doc.scrollbarThumb, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrcThumb, theme.brushBorderSubtle.Get());
    }
}

void EditorCanvas::RenderDiffBinaryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                        editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                        float topY, float clientHeight) {
    const float subHeaderH = config.ScaleF(24.0f);
    const float dividerW = config.ScaleF(32.0f);
    const float halfW = (canvasWidth - dividerW) / 2.0f;
    const float leftX = canvasLeft;
    const float dividerX = leftX + halfW;
    const float rightX = dividerX + dividerW;

    IDWriteTextFormat* pLabelFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();

    // Sub-headers
    D2D1_RECT_F rcLeftSub = D2D1::RectF(leftX, topY, dividerX, topY + subHeaderH);
    pRT->FillRectangle(rcLeftSub, theme.brushHeaderBg.Get());
    pRT->DrawLine(D2D1::Point2F(leftX, topY + subHeaderH), D2D1::Point2F(dividerX, topY + subHeaderH), theme.brushBorderSubtle.Get(), 1.0f);
    std::wstring leftTitle = doc.diffOriginalTitle.empty() ? (doc.fileName + L" (HEAD)") : doc.diffOriginalTitle;
    pRT->DrawText(leftTitle.c_str(), static_cast<UINT32>(leftTitle.length()), pLabelFmt,
                  D2D1::RectF(leftX + config.ScaleF(12.0f), topY + 2.0f, dividerX - config.ScaleF(80.0f), topY + subHeaderH - 2.0f),
                  theme.brushTextPrimary.Get());

    D2D1_RECT_F rcRightSub = D2D1::RectF(rightX, topY, rightX + halfW, topY + subHeaderH);
    pRT->FillRectangle(rcRightSub, theme.brushHeaderBg.Get());
    pRT->DrawLine(D2D1::Point2F(rightX, topY + subHeaderH), D2D1::Point2F(rightX + halfW, topY + subHeaderH), theme.brushBorderSubtle.Get(), 1.0f);
    std::wstring rightTitle = doc.diffWorkingTitle.empty() ? (doc.fileName + L" (Working Tree)") : doc.diffWorkingTitle;
    pRT->DrawText(rightTitle.c_str(), static_cast<UINT32>(rightTitle.length()), pLabelFmt,
                  D2D1::RectF(rightX + config.ScaleF(12.0f), topY + 2.0f, rightX + halfW - config.ScaleF(80.0f), topY + subHeaderH - 2.0f),
                  theme.brushTextPrimary.Get());

    // Center Banner: Binary File Not Shown
    float contentTop = topY + subHeaderH;
    float contentH = clientHeight - contentTop;
    float centerY = contentTop + contentH * 0.40f;
    float centerX = canvasLeft + canvasWidth * 0.5f;

    // Card background
    float cardW = config.ScaleF(460.0f);
    float cardH = config.ScaleF(130.0f);
    D2D1_RECT_F rcCard = D2D1::RectF(centerX - cardW * 0.5f, centerY - cardH * 0.5f, centerX + cardW * 0.5f, centerY + cardH * 0.5f);
    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 8.0f, 8.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.95f), &brushCardBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadge;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.45f, 0.95f, 1.0f), &brushBadge);

    pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());
    pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 1.0f);

    // Pill badge: [BINARY]
    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(
        D2D1::RectF(centerX - config.ScaleF(36.0f), rcCard.top + config.ScaleF(14.0f), centerX + config.ScaleF(36.0f), rcCard.top + config.ScaleF(32.0f)),
        3.0f, 3.0f
    );
    pRT->FillRoundedRectangle(rrcPill, brushBadge.Get());
    const wchar_t* pillText = L"BINARY";
    pRT->DrawText(pillText, 6, theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : pLabelFmt,
                  rrcPill.rect, theme.brushTextWhite.Get());

    // Headline: "Binary file not shown"
    const wchar_t* headline = L"Binary file not shown";
    D2D1_RECT_F rcHead = D2D1::RectF(rcCard.left, rcCard.top + config.ScaleF(38.0f), rcCard.right, rcCard.top + config.ScaleF(66.0f));
    pRT->DrawText(headline, static_cast<UINT32>(wcslen(headline)),
                  theme.fontTitle.Get() ? theme.fontTitle.Get() : theme.fontBody.Get(),
                  rcHead, theme.brushTextPrimary.Get());

    // Subtitle
    std::wstring targetName = doc.diffBinaryFileName.empty() ? doc.fileName : doc.diffBinaryFileName;
    std::wstring sub = L"The file '" + targetName + L"' is binary or an executable and cannot be compared as text.";
    D2D1_RECT_F rcSub = D2D1::RectF(rcCard.left + config.ScaleF(16.0f), rcCard.top + config.ScaleF(72.0f), rcCard.right - config.ScaleF(16.0f), rcCard.bottom - config.ScaleF(10.0f));
    pRT->DrawText(sub.c_str(), static_cast<UINT32>(sub.length()),
                  theme.fontBody.Get(), rcSub, theme.brushTextSecondary.Get());
}

void EditorCanvas::RenderMultiFileDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                           editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                           float topY, float clientHeight, bool isPaneFocused) {
    (void)isPaneFocused;
    const float headerBarH = config.ScaleF(28.0f);
    const float lineH = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));
    const float gutterW = config.ScaleF(46.0f);
    const float scrollbarW = config.ScaleF(12.0f);
    const float dividerW = config.ScaleF(24.0f);
    const float usableW = canvasWidth - scrollbarW;
    const float halfW = (usableW - dividerW) * 0.5f;

    const float leftX = canvasLeft;
    const float dividerX = leftX + halfW;
    const float rightX = dividerX + dividerW;

    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pLabelFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();

    // Clip rendering to content area
    pRT->PushAxisAlignedClip(D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Calculate total visual rows for scrollbar
    size_t totalVisualRows = 0;
    for (const auto& entry : doc.multiFileDiffs) {
        totalVisualRows += 2; // header bar (~1.4 rows)
        if (!entry.isCollapsed) {
            if (entry.isBinary) {
                totalVisualRows += 3;
            } else {
                size_t nLeft = entry.originalBuffer.GetLineCount();
                size_t nRight = entry.workingBuffer.GetLineCount();
                totalVisualRows += (std::max)(nLeft, nRight);
            }
        }
    }
    if (totalVisualRows == 0) totalVisualRows = 1;

    // Viewport row capacity
    float viewH = clientHeight - topY;
    size_t visibleRows = static_cast<size_t>(viewH / lineH);
    if (visibleRows < 2) visibleRows = 2;
    if (doc.scrollLine > totalVisualRows) doc.scrollLine = (totalVisualRows > visibleRows) ? (totalVisualRows - visibleRows) : 0;

    // Virtual Y start based on scrollLine
    float curY = topY - static_cast<float>(doc.scrollLine) * lineH;

    // Common brushes for diff highlights
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.18f, 0.20f, 0.28f), &brushDiffRedBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedBar;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushDiffRedBar);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.20f, 0.28f), &brushDiffGreenBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenBar;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.78f, 0.45f, 1.0f), &brushDiffGreenBar);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHeaderBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.14f, 0.16f, 1.0f), &brushHeaderBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHeaderBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.26f, 1.0f), &brushHeaderBorder);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHiddenLinesBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.16f, 0.18f, 0.85f), &brushHiddenLinesBg);

    for (size_t fIdx = 0; fIdx < doc.multiFileDiffs.size(); ++fIdx) {
        auto& entry = doc.multiFileDiffs[fIdx];

        // 1. Render Collapsible File Header Banner (Image 4)
        entry.headerBounds = D2D1::RectF(canvasLeft, curY, canvasLeft + usableW, curY + headerBarH);

        if (curY + headerBarH >= topY && curY <= clientHeight) {
            pRT->FillRectangle(entry.headerBounds, brushHeaderBg.Get());
            pRT->DrawLine(D2D1::Point2F(canvasLeft, curY), D2D1::Point2F(canvasLeft + usableW, curY), brushHeaderBorder.Get(), 1.0f);
            pRT->DrawLine(D2D1::Point2F(canvasLeft, curY + headerBarH), D2D1::Point2F(canvasLeft + usableW, curY + headerBarH), brushHeaderBorder.Get(), 1.0f);

            // Expand/collapse chevron
            const wchar_t* chv = entry.isCollapsed ? L"›" : L"⌄";
            D2D1_RECT_F rcChv = D2D1::RectF(canvasLeft + config.ScaleF(10.0f), curY + config.ScaleF(2.0f), canvasLeft + config.ScaleF(24.0f), curY + headerBarH);
            pRT->DrawText(chv, 1, pLabelFmt, rcChv, theme.brushTextMuted.Get());

            // File icon representation (e.g. C- for C++, js for JS, etc.)
            float textX = canvasLeft + config.ScaleF(28.0f);
            float iconW = config.ScaleF(18.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushIcon;
            if (entry.fileName.ends_with(L".hpp") || entry.fileName.ends_with(L".h")) {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.65f, 0.95f, 1.0f), &brushIcon);
            } else if (entry.fileName.ends_with(L".cpp") || entry.fileName.ends_with(L".c")) {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.50f, 0.90f, 1.0f), &brushIcon);
            } else if (entry.fileName.ends_with(L".json") || entry.fileName.ends_with(L".yml") || entry.fileName.ends_with(L".yaml")) {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.75f, 0.25f, 1.0f), &brushIcon);
            } else {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.70f, 0.70f, 0.75f, 1.0f), &brushIcon);
            }
            D2D1_RECT_F rcIcon = D2D1::RectF(textX, curY + config.ScaleF(5.0f), textX + iconW, curY + config.ScaleF(23.0f));
            pRT->DrawRectangle(rcIcon, brushIcon.Get(), 1.2f);
            textX += iconW + config.ScaleF(8.0f);

            // File Name (bold white)
            float nameW = static_cast<float>(entry.fileName.length()) * config.ScaleF(8.0f);
            D2D1_RECT_F rcName = D2D1::RectF(textX, curY + config.ScaleF(4.0f), textX + nameW, curY + headerBarH);
            pRT->DrawText(entry.fileName.c_str(), static_cast<UINT32>(entry.fileName.length()), pLabelFmt, rcName, theme.brushTextPrimary.Get());
            textX += nameW + config.ScaleF(10.0f);

            // Relative directory (dimmed gray, matching Image 4)
            if (!entry.relDir.empty()) {
                D2D1_RECT_F rcDir = D2D1::RectF(textX, curY + config.ScaleF(4.0f), canvasLeft + usableW - config.ScaleF(120.0f), curY + headerBarH);
                pRT->DrawText(entry.relDir.c_str(), static_cast<UINT32>(entry.relDir.length()), pLabelFmt, rcDir, theme.brushTextMuted.Get());
            }

            // Right-side stats/pills
            if (entry.isBinary) {
                float badgeW = config.ScaleF(56.0f);
                float badgeH = config.ScaleF(16.0f);
                D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(
                    D2D1::RectF(canvasLeft + usableW - badgeW - config.ScaleF(12.0f), curY + (headerBarH - badgeH) * 0.5f,
                                canvasLeft + usableW - config.ScaleF(12.0f), curY + (headerBarH + badgeH) * 0.5f),
                    3.0f, 3.0f
                );
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadge;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.45f, 0.95f, 1.0f), &brushBadge);
                pRT->FillRoundedRectangle(rrcPill, brushBadge.Get());
                const wchar_t* bText = L"BINARY";
                pRT->DrawText(bText, 6, pLabelFmt, rrcPill.rect, theme.brushTextWhite.Get());
            } else {
                size_t nAdds = entry.addedLines.size();
                size_t nDels = entry.deletedLines.size();
                std::wstring statStr = L"+" + std::to_wstring(nAdds) + L" -" + std::to_wstring(nDels);
                D2D1_RECT_F rcStat = D2D1::RectF(canvasLeft + usableW - config.ScaleF(110.0f), curY + config.ScaleF(4.0f),
                                                 canvasLeft + usableW - config.ScaleF(14.0f), curY + headerBarH);
                pRT->DrawText(statStr.c_str(), static_cast<UINT32>(statStr.length()), pLabelFmt, rcStat, brushDiffGreenBar.Get());
            }
        }
        curY += headerBarH;

        // 2. Render File Diff Body (if expanded)
        if (!entry.isCollapsed) {
            if (entry.isBinary) {
                // Binary placeholder box
                float boxH = config.ScaleF(44.0f);
                if (curY + boxH >= topY && curY <= clientHeight) {
                    D2D1_RECT_F rcBox = D2D1::RectF(canvasLeft, curY, canvasLeft + usableW, curY + boxH);
                    pRT->FillRectangle(rcBox, theme.brushCanvasBg.Get());
                    const wchar_t* binNotice = L"Binary file not shown";
                    D2D1_RECT_F rcNotice = D2D1::RectF(canvasLeft + config.ScaleF(40.0f), curY + config.ScaleF(12.0f), canvasLeft + usableW, curY + boxH);
                    pRT->DrawText(binNotice, static_cast<UINT32>(wcslen(binNotice)), pLabelFmt, rcNotice, theme.brushTextMuted.Get());
                }
                curY += boxH;
            } else {
                size_t nLeft = entry.originalBuffer.GetLineCount();
                size_t nRight = entry.workingBuffer.GetLineCount();
                size_t maxLines = (std::max)(nLeft, nRight);

                // Render side-by-side lines
                size_t l = 0;
                while (l < maxLines) {
                    // Check for consecutive unchanged lines to show hidden lines banner (Image 4)
                    bool isLeftDel = (l < nLeft && entry.deletedLines.contains(l));
                    bool isRightAdd = (l < nRight && entry.addedLines.contains(l));

                    if (!isLeftDel && !isRightAdd) {
                        size_t unchangedCount = 0;
                        size_t peek = l;
                        while (peek < maxLines) {
                            bool pd = (peek < nLeft && entry.deletedLines.contains(peek));
                            bool pa = (peek < nRight && entry.addedLines.contains(peek));
                            if (pd || pa) break;
                            unchangedCount++;
                            peek++;
                        }

                        if (unchangedCount > 8) {
                            // Render Hidden Lines Bar (Image 4)
                            float barH = lineH;
                            if (curY + barH >= topY && curY <= clientHeight) {
                                D2D1_RECT_F rcBar = D2D1::RectF(canvasLeft, curY, canvasLeft + usableW, curY + barH);
                                pRT->FillRectangle(rcBar, brushHiddenLinesBg.Get());
                                pRT->DrawLine(D2D1::Point2F(canvasLeft, curY), D2D1::Point2F(canvasLeft + usableW, curY), brushHeaderBorder.Get(), 0.8f);
                                pRT->DrawLine(D2D1::Point2F(canvasLeft, curY + barH), D2D1::Point2F(canvasLeft + usableW, curY + barH), brushHeaderBorder.Get(), 0.8f);

                                std::wstring hidText = L"❊  " + std::to_wstring(unchangedCount) + L" hidden lines  ❊";
                                D2D1_RECT_F rcHidText = D2D1::RectF(canvasLeft + gutterW, curY + 1.0f, canvasLeft + usableW, curY + barH);
                                pRT->DrawText(hidText.c_str(), static_cast<UINT32>(hidText.length()), pLabelFmt, rcHidText, theme.brushTextMuted.Get());
                            }
                            curY += barH;
                            l += unchangedCount;
                            continue;
                        }
                    }

                    if (curY + lineH >= topY && curY <= clientHeight) {
                        // Left pane line (Parent commit)
                        if (l < nLeft) {
                            D2D1_RECT_F rcLeftLine = D2D1::RectF(leftX, curY, dividerX, curY + lineH);
                            if (isLeftDel) {
                                pRT->FillRectangle(rcLeftLine, brushDiffRedBg.Get());
                                pRT->FillRectangle(D2D1::RectF(leftX, curY, leftX + 3.0f, curY + lineH), brushDiffRedBar.Get());
                            }
                            // Gutter number
                            std::wstring num = std::to_wstring(l + 1);
                            D2D1_RECT_F rcGutter = D2D1::RectF(leftX + 2.0f, curY + 1.0f, leftX + gutterW - 6.0f, curY + lineH);
                            pRT->DrawText(num.c_str(), static_cast<UINT32>(num.length()), pCodeFmt, rcGutter, theme.brushLineNumber.Get());

                            // Text with syntax highlighting
                            float textX = leftX + gutterW + config.ScaleF(8.0f);
                            RenderSyntaxTokensLine(pRT, theme, config, entry.originalBuffer, l, textX, curY + 1.0f, dividerX, lineH, config.ScaleF(7.8f), pCodeFmt, isLeftDel ? brushDiffRedBar.Get() : theme.brushTextPrimary.Get());
                        }

                        // Right pane line (This commit)
                        if (l < nRight) {
                            D2D1_RECT_F rcRightLine = D2D1::RectF(rightX, curY, rightX + halfW, curY + lineH);
                            if (isRightAdd) {
                                pRT->FillRectangle(rcRightLine, brushDiffGreenBg.Get());
                                pRT->FillRectangle(D2D1::RectF(rightX, curY, rightX + 3.0f, curY + lineH), brushDiffGreenBar.Get());
                            }
                            // Gutter number
                            std::wstring num = std::to_wstring(l + 1);
                            D2D1_RECT_F rcGutter = D2D1::RectF(rightX + 2.0f, curY + 1.0f, rightX + gutterW - 6.0f, curY + lineH);
                            pRT->DrawText(num.c_str(), static_cast<UINT32>(num.length()), pCodeFmt, rcGutter, theme.brushLineNumber.Get());

                            // Text with syntax highlighting
                            float textX = rightX + gutterW + config.ScaleF(8.0f);
                            RenderSyntaxTokensLine(pRT, theme, config, entry.workingBuffer, l, textX, curY + 1.0f, rightX + halfW, lineH, config.ScaleF(7.8f), pCodeFmt, isRightAdd ? brushDiffGreenBar.Get() : theme.brushTextPrimary.Get());
                        }

                        // Divider between left and right
                        pRT->DrawLine(D2D1::Point2F(dividerX + dividerW * 0.5f, curY), D2D1::Point2F(dividerX + dividerW * 0.5f, curY + lineH), brushHeaderBorder.Get(), 0.5f);
                    }
                    curY += lineH;
                    l++;
                }
            }
        }
    }

    // Scrollbar Track & Thumb
    float trackLeft = canvasLeft + canvasWidth - scrollbarW;
    doc.scrollbarTrack = D2D1::RectF(trackLeft, topY, canvasLeft + canvasWidth, clientHeight);
    pRT->FillRectangle(doc.scrollbarTrack, theme.brushHeaderBg.Get());

    if (totalVisualRows > visibleRows) {
        float ratio = static_cast<float>(visibleRows) / static_cast<float>(totalVisualRows);
        float thumbH = (std::max)(viewH * ratio, config.ScaleF(24.0f));
        float scrollRange = viewH - thumbH;
        float progress = static_cast<float>(doc.scrollLine) / static_cast<float>(totalVisualRows - visibleRows);
        float thumbY = topY + progress * scrollRange;

        doc.scrollbarThumb = D2D1::RectF(trackLeft + 2.0f, thumbY, trackLeft + scrollbarW - 2.0f, thumbY + thumbH);
        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(doc.scrollbarThumb, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrcThumb, theme.brushBorderSubtle.Get());
    }

    pRT->PopAxisAlignedClip();
}

const EditorCanvas::ColorSwatchHit* EditorCanvas::HitTestColorSwatch(POINT pt) const {
    float x = static_cast<float>(pt.x);
    float y = static_cast<float>(pt.y);
    for (const auto& s : m_visibleColorSwatches) {
        if (x >= s.bounds.left - 3.0f && x <= s.bounds.right + 3.0f &&
            y >= s.bounds.top - 3.0f && y <= s.bounds.bottom + 3.0f) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace phide::ui
