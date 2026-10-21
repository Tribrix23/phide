/**
 * @file EditorCanvas.cpp
 * @brief Implementation of dark workspace surface with modern rounded container aesthetics.
 */

#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/WatermarkLogo.hpp"
#include <algorithm>

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
    m_cachedLeft = -1.0f;
    m_cachedTop = -1.0f;
    m_cachedRight = -1.0f;
    m_cachedBottom = -1.0f;
}

void EditorCanvas::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                          float canvasLeft, float canvasWidth, float clientHeight,
                          bool hasLeftDrawer) {
    if (!pRT) return;

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

                    // 1. Begin at the top edge right after the upper-left rounded corner (or top-left if sharp)
                    sink->BeginFigure(D2D1::Point2F(canvasLeft + r, headerH), D2D1_FIGURE_BEGIN_FILLED);

                    // 2. Top edge straight to top-right corner (SHARP - not curved)
                    sink->AddLine(D2D1::Point2F(canvasRight, headerH));

                    // 3. Right edge straight down to bottom-right corner (SHARP - not curved)
                    sink->AddLine(D2D1::Point2F(canvasRight, clientHeight));

                    // 4. Bottom edge straight to bottom-left corner (SHARP - not curved)
                    sink->AddLine(D2D1::Point2F(canvasLeft, clientHeight));

                    // 5. Left edge straight up to start of the upper-left curve
                    sink->AddLine(D2D1::Point2F(canvasLeft, headerH + r));

                    // 6. Arc around ONLY the upper-left corner when drawer is closed
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

    if (docs.empty() || activeDocIdx < 0 || activeDocIdx >= static_cast<int>(docs.size())) {
        // When no file is open: render metallic watermark logo & keybinding cheat-sheet
        RenderPlaceholder(pRT, theme, config, canvasLeft, canvasWidth, headerH, clientHeight);
    } else {
        // When files are open: render multi-tab bar and active editor view
        const float tabH = config.ScaleF(34.0f);
        RenderTabs(pRT, theme, config, canvasLeft, canvasWidth, headerH, docs, activeDocIdx, hasLeftDrawer);

        auto& activeDoc = docs[activeDocIdx];
        if (activeDoc.isSpecialView && activeDoc.viewType == L"keybindings") {
            m_keybindingsView.Render(pRT, theme, config, canvasLeft, canvasWidth, headerH + tabH, clientHeight - (headerH + tabH));
        } else {
            RenderCode(pRT, theme, config, activeDoc.buffer, canvasLeft, canvasWidth, headerH + tabH, clientHeight);
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
                              bool hasLeftDrawer) {
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
            // Top active tab highlight border (2px Accent Blue)
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
            (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) :
            theme.brushTextSecondary.Get();

        pRT->PushAxisAlignedClip(rcTitle, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(title.c_str(), static_cast<UINT32>(title.length()), pTextFmt, rcTitle, pTextBrush);
        pRT->PopAxisAlignedClip();

        // Close Tab Button (×)
        const float closeSize = config.ScaleF(16.0f);
        const float closeX = rcTab.right - config.ScaleF(20.0f);
        const float closeY = headerH + (tabH - closeSize) / 2.0f;
        doc.closeBounds = D2D1::RectF(closeX, closeY, closeX + closeSize, closeY + closeSize);

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

void EditorCanvas::RenderCode(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              const editor::Buffer& buffer, float canvasLeft, float canvasWidth,
                              float topY, float clientHeight) {
    const float gutterW = config.ScaleF(50.0f);
    const float lineH   = config.ScaleF(20.0f);
    const float charW   = config.ScaleF(7.8f);
    const float canvasRight = canvasLeft + canvasWidth;

    // 1. Gutter Background & Divider
    D2D1_RECT_F rcGutter = D2D1::RectF(canvasLeft, topY, canvasLeft + gutterW, clientHeight);
    pRT->FillRectangle(rcGutter, theme.brushCanvasBg.Get());

    pRT->DrawLine(
        D2D1::Point2F(canvasLeft + gutterW - 0.5f, topY),
        D2D1::Point2F(canvasLeft + gutterW - 0.5f, clientHeight),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    // 2. Current Line Highlight
    auto cursor = buffer.GetCursor();
    float activeLineY = topY + config.ScaleF(6.0f) + cursor.line * lineH;
    if (activeLineY >= topY && activeLineY + lineH <= clientHeight) {
        D2D1_RECT_F rcActiveLine = D2D1::RectF(canvasLeft + gutterW, activeLineY, canvasRight, activeLineY + lineH);
        pRT->FillRectangle(rcActiveLine, theme.brushCurrentLineBg.Get());
    }

    // 3. Line Numbers & Code Lines
    const size_t totalLines = buffer.GetLineCount();
    const float codeLeft = canvasLeft + gutterW + config.ScaleF(12.0f);

    // Clip code to editor boundaries
    D2D1_RECT_F clipCode = D2D1::RectF(canvasLeft, topY, canvasRight, clientHeight);
    pRT->PushAxisAlignedClip(clipCode, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    for (size_t i = 0; i < totalLines; ++i) {
        float lineY = topY + config.ScaleF(6.0f) + i * lineH;
        if (lineY + lineH < topY || lineY > clientHeight) continue;

        // Line Number in Gutter (Right-aligned)
        std::wstring lineNumStr = std::to_wstring(i + 1);
        D2D1_RECT_F rcNum = D2D1::RectF(
            canvasLeft + config.ScaleF(4.0f),
            lineY,
            canvasLeft + gutterW - config.ScaleF(8.0f),
            lineY + lineH
        );
        ID2D1Brush* numBrush = (i == cursor.line) ? theme.brushTextPrimary.Get() : theme.brushLineNumber.Get();
        pRT->DrawText(
            lineNumStr.c_str(),
            static_cast<UINT32>(lineNumStr.length()),
            theme.fontMenuShortcut.Get(),
            rcNum,
            numBrush
        );

        // Code Content
        const auto& lineStr = buffer.GetLine(i);
        if (lineStr.empty()) continue;

        auto tokens = buffer.GetTokensForLine(i);
        if (tokens.empty()) {
            D2D1_RECT_F rcLine = D2D1::RectF(codeLeft, lineY, canvasRight, lineY + lineH);
            pRT->DrawText(
                lineStr.c_str(),
                static_cast<UINT32>(lineStr.length()),
                theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                rcLine,
                theme.brushTextPrimary.Get()
            );
        } else {
            // Draw syntax tokens
            for (const auto& tok : tokens) {
                if (tok.startCol + tok.length > lineStr.length()) continue;

                float tokenX = codeLeft + tok.startCol * charW;
                float tokenW = tok.length * charW;
                D2D1_RECT_F rcTok = D2D1::RectF(tokenX, lineY, tokenX + tokenW, lineY + lineH);

                ID2D1Brush* brush = theme.brushTextPrimary.Get();
                switch (tok.type) {
                    case editor::TokenType::Keyword:
                        brush = theme.brushSyntaxKeyword.Get();
                        break;
                    case editor::TokenType::Type:
                        brush = theme.brushSyntaxType.Get();
                        break;
                    case editor::TokenType::String:
                        brush = theme.brushSyntaxString.Get();
                        break;
                    case editor::TokenType::Comment:
                        brush = theme.brushSyntaxComment.Get();
                        break;
                    case editor::TokenType::Number:
                        brush = theme.brushSyntaxNumber.Get();
                        break;
                    case editor::TokenType::Operator:
                        brush = theme.brushSyntaxOperator.Get();
                        break;
                    default:
                        brush = theme.brushTextPrimary.Get();
                        break;
                }

                pRT->DrawText(
                    lineStr.c_str() + tok.startCol,
                    static_cast<UINT32>(tok.length),
                    theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                    rcTok,
                    brush
                );
            }
        }
    }

    // 4. Blinking Caret
    if (m_caretVisible) {
        float caretX = codeLeft + cursor.col * charW;
        float caretY = topY + config.ScaleF(6.0f) + cursor.line * lineH;
        if (caretY >= topY && caretY + lineH <= clientHeight) {
            pRT->DrawLine(
                D2D1::Point2F(caretX, caretY + config.ScaleF(2.0f)),
                D2D1::Point2F(caretX, caretY + lineH - config.ScaleF(2.0f)),
                theme.brushTextWhite.Get(),
                1.5f
            );
        }
    }

    pRT->PopAxisAlignedClip();
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

bool EditorCanvas::HitTestCodeArea(POINT pt, const app::AppConfig& config, const editor::Buffer& buffer,
                                   float canvasLeft, float clientHeight, size_t& outLine, size_t& outCol) const {
    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float tabH    = config.ScaleF(34.0f);
    const float topY    = headerH + tabH;
    const float gutterW = config.ScaleF(50.0f);
    const float lineH   = config.ScaleF(20.0f);
    const float charW   = config.ScaleF(7.8f);

    if (pt.x < canvasLeft + gutterW || pt.y < topY || pt.y > clientHeight) {
        return false;
    }

    float relY = pt.y - (topY + config.ScaleF(6.0f));
    if (relY < 0.0f) relY = 0.0f;
    size_t line = static_cast<size_t>(relY / lineH);

    float relX = pt.x - (canvasLeft + gutterW + config.ScaleF(12.0f));
    if (relX < 0.0f) relX = 0.0f;
    size_t col = static_cast<size_t>((relX + charW * 0.5f) / charW);

    if (line >= buffer.GetLineCount()) {
        line = buffer.GetLineCount() > 0 ? buffer.GetLineCount() - 1 : 0;
    }
    const auto& lineStr = buffer.GetLine(line);
    if (col > lineStr.length()) {
        col = lineStr.length();
    }

    outLine = line;
    outCol  = col;
    return true;
}

} // namespace phide::ui
