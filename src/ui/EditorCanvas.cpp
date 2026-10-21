/**
 * @file EditorCanvas.cpp
 * @brief Implementation of dark workspace surface with modern rounded container aesthetics.
 */

#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/WatermarkLogo.hpp"
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
                         isRenaming);

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
}

void EditorCanvas::RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    std::vector<editor::OpenDocument>& docs, int docIdx,
                                    float canvasLeft, float canvasWidth, float headerH, float clientHeight,
                                    bool hasLeftDrawer, bool isPaneFocused,
                                    const std::wstring& highlightQuery,
                                    bool highlightMatchCase,
                                    bool highlightWholeWord,
                                    bool highlightUseRegex,
                                    bool isRenaming) {
    if (docs.empty() || docIdx < 0 || docIdx >= static_cast<int>(docs.size())) {
        RenderPlaceholder(pRT, theme, config, canvasLeft, canvasWidth, headerH, clientHeight);
        return;
    }

    const float tabH = config.ScaleF(34.0f);
    RenderTabs(pRT, theme, config, canvasLeft, canvasWidth, headerH, docs, docIdx, hasLeftDrawer, isPaneFocused);

    auto& activeDoc = docs[docIdx];
    const float breadcrumbH = config.ScaleF(24.0f);
    RenderBreadcrumbs(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, headerH + tabH, breadcrumbH);

    float contentTopY = headerH + tabH + breadcrumbH;
    if (activeDoc.isSpecialView && activeDoc.viewType == L"keybindings") {
        m_keybindingsView.Render(pRT, theme, config, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if (activeDoc.isImageView) {
        RenderImageView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
    } else if (activeDoc.isUnsupportedBinary) {
        RenderUnsupportedBinaryView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
    } else {
        RenderCode(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight, isPaneFocused,
                   highlightQuery, highlightMatchCase, highlightWholeWord, highlightUseRegex, isRenaming);
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
                              bool hasLeftDrawer, bool isFocused) {
    const float tabH = config.ScaleF(34.0f);
    const float cornerRadius = hasLeftDrawer ? 0.0f : config.ScaleF(14.0f);

    // 1. Tab Bar Background (#181818 in dark, #ECECEC in light)
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasLeft, headerH, canvasLeft + canvasWidth, headerH + tabH);
    FillUpperLeftRoundedRect(pRT, theme.brushTabBarBg.Get(), rcTabBar, cornerRadius);

    // Bottom border of tab bar
    pRT->DrawLine(
        D2D1::Point2F(canvasLeft, headerH + tabH - 0.5f),
        D2D1::Point2F(canvasLeft + canvasWidth, headerH + tabH - 0.5f),
        theme.brushTabBorder.Get(),
        1.0f
    );

    float curX = canvasLeft;
    for (size_t i = 0; i < docs.size(); ++i) {
        auto& doc = docs[i];
        bool isActive = (static_cast<int>(i) == activeDocIdx);
        bool isFirstTab = (i == 0);
        float tabCurve = (isFirstTab && !hasLeftDrawer) ? cornerRadius : 0.0f;

        // Calculate tab width based on title length
        std::wstring title = doc.fileName.empty() ? L"Untitled-1" : doc.fileName;
        float textW = 0.0f;
        if (theme.dwriteFactory) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    title.c_str(), static_cast<UINT32>(title.length()),
                    theme.fontMenuItem.Get(), 1000.0f, tabH, &pLayout))) {
                DWRITE_TEXT_METRICS m{};
                if (SUCCEEDED(pLayout->GetMetrics(&m))) {
                    textW = m.widthIncludingTrailingWhitespace;
                }
            }
        }
        if (textW <= 0.0f) {
            textW = static_cast<float>(title.length()) * config.ScaleF(7.5f);
        }

        // tab width = icon(16) + pad(16) + text + dirty/close pad(42)
        float tabW = config.ScaleF(16.0f + 16.0f + 42.0f) + textW;
        if (tabCurve > 0.0f) {
            tabW += config.ScaleF(6.0f); // subtle extra breathing room for curved first tab
        }
        tabW = (std::clamp)(tabW, config.ScaleF(100.0f), config.ScaleF(220.0f));

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
                    pRT->DrawLine(
                        D2D1::Point2F(rcTab.left, headerH + 1.0f),
                        D2D1::Point2F(rcTab.right, headerH + 1.0f),
                        theme.brushBrandAccent.Get(),
                        2.0f
                    );
                }
            }
        }

        // Right tab divider line
        pRT->DrawLine(
            D2D1::Point2F(rcTab.right - 0.5f, headerH),
            D2D1::Point2F(rcTab.right - 0.5f, headerH + tabH),
            theme.brushTabBorder.Get(),
            1.0f
        );

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
        ID2D1Brush* pTextBrush = isActive ? 
            (isFocused ? (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextSecondary.Get()) :
            theme.brushTextSecondary.Get();

        pRT->PushAxisAlignedClip(rcTitle, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(title.c_str(), static_cast<UINT32>(title.length()), pTextFmt, rcTitle, pTextBrush);
        pRT->PopAxisAlignedClip();

        // Close Tab Button (×)
        const float closeSize = config.ScaleF(16.0f);
        const float closeX = rcTab.right - config.ScaleF(20.0f);
        const float closeY = headerH + (tabH - closeSize) / 2.0f;
        D2D1_RECT_F rcClose = D2D1::RectF(closeX, closeY, closeX + closeSize, closeY + closeSize);
        doc.closeBounds = rcClose;

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
        pRT->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), theme.brushTextSecondary.Get(), 1.2f);
        pRT->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx - r, cy + r), theme.brushTextSecondary.Get(), 1.2f);

        curX += tabW;
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
    const float lineH   = config.ScaleF(20.0f);

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
    const float canvasRight = canvasLeft + canvasWidth;
    const float codeAreaH = clientHeight - topY;

    m_foldChevrons.clear();

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
    for (size_t b = 0; b < totalBufLines; ++b) {
        size_t foldEnd = 0;
        bool canFold = GetFoldRange(buffer, b, foldEnd);
        bool isFolded = canFold && (doc.foldedLines.count(b) > 0);

        VisualLine vl;
        vl.bufferLine = b;
        vl.isFoldHeader = canFold;
        vl.isFolded = isFolded;
        vl.foldEndLine = foldEnd;
        visualLines.push_back(vl);

        if (isFolded) {
            b = foldEnd; // Skip folded child lines!
        }
    }

    const size_t visibleCount = (lineH > 0.0f) ? static_cast<size_t>(codeAreaH / lineH) : 20;
    const size_t maxScroll = (visualLines.size() > visibleCount) ? (visualLines.size() - visibleCount) : 0;
    if (doc.scrollLine > maxScroll) {
        doc.scrollLine = maxScroll;
    }

    // 2. Current Line Highlight - Only rendered if this editor group is active/focused!
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

    if (isFocused && foundCursorVisual && visualCursorIdx >= doc.scrollLine) {
        float activeLineY = topY + config.ScaleF(6.0f) + (visualCursorIdx - doc.scrollLine) * lineH;
        if (activeLineY >= topY && activeLineY + lineH <= clientHeight) {
            D2D1_RECT_F rcActiveLine = D2D1::RectF(canvasLeft + gutterW, activeLineY, canvasRight - scrollbarW, activeLineY + lineH);
            pRT->FillRectangle(rcActiveLine, theme.brushCurrentLineBg.Get());
        }
    }

    // 3. Line Numbers, Fold Chevrons & Code Lines
    const float codeLeft = canvasLeft + gutterW + config.ScaleF(12.0f);

    D2D1_RECT_F clipCode = D2D1::RectF(canvasLeft, topY, canvasRight - scrollbarW, clientHeight);
    pRT->PushAxisAlignedClip(clipCode, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Analyze rainbow bracket pairs, nesting depths, and active cursor pair
    auto bracketAnalysis = editor::BracketPairAnalyzer::Analyze(buffer, cursor.line, cursor.col);
    std::vector<std::vector<const editor::BracketItem*>> bracketsByLine(totalBufLines);
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
            float guideX = codeLeft + static_cast<float>(pActivePair->guideCol) * charW + charW * 0.5f;
            float openCharX = codeLeft + static_cast<float>(pActivePair->openCol) * charW + charW * 0.5f;
            float closeCharX = codeLeft + static_cast<float>(pActivePair->closeCol) * charW + charW * 0.5f;

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
                float ox = codeLeft + static_cast<float>(pActivePair->openCol) * charW;
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
                float cx = codeLeft + static_cast<float>(pActivePair->closeCol) * charW;
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
                D2D1_RECT_F rcDiffLine = D2D1::RectF(canvasLeft + gutterW, lineY, canvasRight - scrollbarW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRed;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.18f, 0.20f, 0.25f), &brushDiffRed);
                pRT->FillRectangle(rcDiffLine, brushDiffRed.Get());

                D2D1_RECT_F rcGutterBar = D2D1::RectF(canvasLeft + gutterW - config.ScaleF(3.0f), lineY, canvasLeft + gutterW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedBar;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushRedBar);
                pRT->FillRectangle(rcGutterBar, brushRedBar.Get());
            } else if (doc.diffAddedLines.count(vl.bufferLine)) {
                // Green background for added lines
                D2D1_RECT_F rcDiffLine = D2D1::RectF(canvasLeft + gutterW, lineY, canvasRight - scrollbarW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreen;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.20f, 0.25f), &brushDiffGreen);
                pRT->FillRectangle(rcDiffLine, brushDiffGreen.Get());

                D2D1_RECT_F rcGutterBar = D2D1::RectF(canvasLeft + gutterW - config.ScaleF(3.0f), lineY, canvasLeft + gutterW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushGreenBar;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), &brushGreenBar);
                pRT->FillRectangle(rcGutterBar, brushGreenBar.Get());
            }
        }

        // Line Number in Gutter (Right-aligned)
        std::wstring lineNumStr = std::to_wstring(vl.bufferLine + 1);
        D2D1_RECT_F rcNum = D2D1::RectF(
            canvasLeft + config.ScaleF(4.0f),
            lineY,
            canvasLeft + gutterW - config.ScaleF(18.0f),
            lineY + lineH
        );
        ID2D1Brush* numBrush = (isFocused && vl.bufferLine == cursor.line) ? theme.brushTextPrimary.Get() : theme.brushLineNumber.Get();
        pRT->DrawText(
            lineNumStr.c_str(),
            static_cast<UINT32>(lineNumStr.length()),
            theme.fontMenuShortcut.Get(),
            rcNum,
            numBrush
        );

        // Code Folding Chevron in Gutter (Images 2 & 3)
        if (vl.isFoldHeader) {
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

        if (!lineStr.empty()) {
            auto tokens = buffer.GetTokensForLine(vl.bufferLine);
            if (tokens.empty()) {
                D2D1_RECT_F rcLine = D2D1::RectF(codeLeft, lineY, canvasRight - scrollbarW, lineY + lineH);
                pRT->DrawText(
                    lineStr.c_str(),
                    static_cast<UINT32>(lineStr.length()),
                    theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                    rcLine,
                    theme.brushTextPrimary.Get()
                );
                curCodeEndX = codeLeft + lineStr.length() * charW;
            } else {
                for (const auto& tok : tokens) {
                    if (tok.startCol + tok.length > lineStr.length()) continue;

                    float tokenX = codeLeft + tok.startCol * charW;
                    float tokenW = tok.length * charW;
                    D2D1_RECT_F rcTok = D2D1::RectF(tokenX, lineY, tokenX + tokenW + config.ScaleF(4.0f), lineY + lineH);

                    ID2D1Brush* brush = theme.brushTextPrimary.Get();
                    bool isBracketToken = false;
                    if (tok.type == editor::TokenType::Operator && tok.length == 1 && vl.bufferLine < bracketsByLine.size()) {
                        for (const auto* b : bracketsByLine[vl.bufferLine]) {
                            if (b->col == tok.startCol) {
                                brush = theme.GetBracketBrush(b->depth);
                                isBracketToken = true;
                                break;
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
    }

    // 4. Blinking Caret (Image 5: High-contrast in both Dark and Light modes!) - Only in focused group
    if (foundCursorVisual && visualCursorIdx >= doc.scrollLine) {
        float caretX = codeLeft + cursor.col * charW;
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
    }

    pRT->PopAxisAlignedClip();

    // 5. Vertical Scrollbar on Right Edge (Image 1: Smooth scrolling support!)
    doc.scrollbarTrack = D2D1::RectF(canvasRight - scrollbarW, topY, canvasRight, clientHeight);
    if (visualLines.size() > visibleCount) {
        float thumbH = (std::max)(config.ScaleF(28.0f), codeAreaH * (static_cast<float>(visibleCount) / visualLines.size()));
        float scrollRange = codeAreaH - thumbH;
        float scrollFrac = (visualLines.size() > visibleCount)
            ? (static_cast<float>(doc.scrollLine) / static_cast<float>(visualLines.size() - visibleCount))
            : 0.0f;
        float thumbY = topY + scrollRange * scrollFrac;

        doc.scrollbarThumb = D2D1::RectF(
            canvasRight - scrollbarW + config.ScaleF(3.0f),
            thumbY,
            canvasRight - config.ScaleF(3.0f),
            thumbY + thumbH
        );

        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(doc.scrollbarThumb, 3.5f, 3.5f);
        // Scrollbar thumb brush: subtle semi-transparent pill that deepens when dragging
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
    const float lineH   = config.ScaleF(20.0f);
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

    float relX = pt.x - (canvasLeft + gutterW + config.ScaleF(12.0f));
    if (relX < 0.0f) relX = 0.0f;
    size_t col = static_cast<size_t>((relX + charW * 0.5f) / charW);

    if (mappedLine >= buffer.GetLineCount()) {
        mappedLine = buffer.GetLineCount() > 0 ? buffer.GetLineCount() - 1 : 0;
    }
    const auto& lineStr = buffer.GetLine(mappedLine);
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

} // namespace phide::ui
