/**
 * @file EditorCanvas.cpp
 * @brief Implementation of dark workspace surface with modern rounded container aesthetics.
 */

#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/WatermarkLogo.hpp"
#include <algorithm>

namespace phide::ui {

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

    pRT->DrawText(text, static_cast<UINT32>(len), theme.fontMenu.Get(), rc, theme.brushTextPrimary.Get());

    curRightX = x - config.ScaleF(4.0f);
}

static void DrawPlus(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                     float& curRightX, float centerY, float capH) {
    const float plusW = config.ScaleF(12.0f);
    const float x = curRightX - plusW;
    const float y = centerY - capH / 2.0f;

    D2D1_RECT_F rc = D2D1::RectF(x, y, curRightX, y + capH);
    pRT->DrawText(L"+", 1, theme.fontMenu.Get(), rc, theme.brushTextMuted.Get());

    curRightX = x - config.ScaleF(4.0f);
}

EditorCanvas::EditorCanvas() = default;

void EditorCanvas::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          const editor::Buffer& buffer, float canvasLeft, float canvasWidth, float clientHeight,
                          bool hasLeftDrawer, bool hasOpenFile,
                          const std::wstring& currentFileName, bool isDirty) {
    if (!pRT) return;

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float canvasRight = canvasLeft + canvasWidth;
    const float cornerRadius = hasLeftDrawer ? 0.0f : 14.0f;

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

    if (!hasOpenFile) {
        // When no file is open: render metallic watermark logo & keybinding cheat-sheet
        m_tabCloseBounds = {};
        RenderPlaceholder(pRT, theme, config, canvasLeft, canvasWidth, headerH, clientHeight);
    } else {
        // When a file is open: REMOVE placeholder! Put actual tabs and code editor surface there!
        const float tabH = config.ScaleF(34.0f);
        RenderTabs(pRT, theme, config, canvasLeft, canvasWidth, headerH, currentFileName, isDirty);
        RenderCode(pRT, theme, config, buffer, canvasLeft, canvasWidth, headerH + tabH, clientHeight);
    }
}

void EditorCanvas::RenderPlaceholder(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     float canvasLeft, float canvasWidth, float headerH, float clientHeight) {
    const float centerY = headerH + (clientHeight - headerH) / 2.0f;

    // Initialize metallic white watermark bitmap if needed
    if (!m_pWatermarkBitmap) {
        D2D1_SIZE_U size = D2D1::SizeU(kWatermarkLogoWidth, kWatermarkLogoHeight);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        pRT->CreateBitmap(size, g_watermarkLogoBgra, kWatermarkLogoWidth * 4, props, &m_pWatermarkBitmap);
    }

    // Render Metallic White / Platinum Inverted Logo Watermark in center of canvas
    const float logoSize = config.ScaleF(84.0f);
    const float logoX = canvasLeft + (canvasWidth - logoSize) / 2.0f;
    const float logoY = centerY - logoSize / 2.0f - config.ScaleF(40.0f);
    D2D1_RECT_F rcLogo = D2D1::RectF(logoX, logoY, logoX + logoSize, logoY + logoSize);

    if (m_pWatermarkBitmap) {
        pRT->DrawBitmap(m_pWatermarkBitmap.Get(), rcLogo);
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
                              const std::wstring& currentFileName, bool isDirty) {
    const float tabH = config.ScaleF(34.0f);

    // 1. Tab Bar Background (#181818)
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasLeft, headerH, canvasLeft + canvasWidth, headerH + tabH);
    pRT->FillRectangle(rcTabBar, theme.brushTabBarBg.Get());

    // Bottom border of tab bar
    pRT->DrawLine(
        D2D1::Point2F(canvasLeft, headerH + tabH - 0.5f),
        D2D1::Point2F(canvasLeft + canvasWidth, headerH + tabH - 0.5f),
        theme.brushTabBorder.Get(),
        1.0f
    );

    // 2. Active Tab
    const float tabW = config.ScaleF(160.0f);
    D2D1_RECT_F rcTab = D2D1::RectF(canvasLeft, headerH, canvasLeft + tabW, headerH + tabH);
    pRT->FillRectangle(rcTab, theme.brushTabActiveBg.Get());

    // Top active tab highlight border (2px Accent Blue)
    pRT->DrawLine(
        D2D1::Point2F(rcTab.left, headerH + 1.0f),
        D2D1::Point2F(rcTab.right, headerH + 1.0f),
        theme.brushBrandAccent.Get(),
        2.0f
    );

    // Right tab divider line
    pRT->DrawLine(
        D2D1::Point2F(rcTab.right - 0.5f, headerH),
        D2D1::Point2F(rcTab.right - 0.5f, headerH + tabH),
        theme.brushTabBorder.Get(),
        1.0f
    );

    // 3. File Type Icon (Vector Lucide/Seti Icon)
    const float iconX = rcTab.left + config.ScaleF(10.0f);
    const float iconCenterY = headerH + tabH / 2.0f;
    D2D1_RECT_F iconBounds = D2D1::RectF(
        iconX,
        iconCenterY - config.ScaleF(8.0f),
        iconX + config.ScaleF(16.0f),
        iconCenterY + config.ScaleF(8.0f)
    );
    auto iconType = LucideIcons::GetIconTypeForPath(currentFileName, false, false);
    m_icons.DrawFileIcon(pRT, theme, iconBounds, iconType);

    // 4. File Title
    std::wstring title = currentFileName.empty() ? L"Untitled-1" : currentFileName;
    float textStartX = rcTab.left + config.ScaleF(32.0f);
    float textMaxW   = rcTab.right - config.ScaleF(40.0f) - textStartX;
    D2D1_RECT_F rcTitle = D2D1::RectF(
        textStartX,
        headerH,
        textStartX + textMaxW,
        headerH + tabH
    );
    pRT->DrawText(title.c_str(), static_cast<UINT32>(title.length()), theme.fontMenuItem.Get(), rcTitle, theme.brushTextWhite.Get());

    // 5. Large Blue Dirty Indicator Circle (8px diameter)
    if (isDirty) {
        float dotX = textStartX + config.ScaleF(60.0f);
        if (theme.dwriteFactory) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    title.c_str(), static_cast<UINT32>(title.length()),
                    theme.fontMenuItem.Get(), 1000.0f, tabH, &pLayout))) {
                DWRITE_TEXT_METRICS m{};
                if (SUCCEEDED(pLayout->GetMetrics(&m))) {
                    dotX = textStartX + m.widthIncludingTrailingWhitespace + config.ScaleF(8.0f);
                }
            }
        }
        dotX = (std::min)(dotX, rcTab.right - config.ScaleF(28.0f));
        float dotY = headerH + tabH / 2.0f;
        float dotRadius = config.ScaleF(4.0f); // 8px diameter circle!
        D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotRadius, dotRadius);
        pRT->FillEllipse(dot, theme.brushIconBlue ? theme.brushIconBlue.Get() : theme.brushBrandAccent.Get());
    }

    // Close Tab Button (×)
    const float closeSize = config.ScaleF(16.0f);
    const float closeX = rcTab.right - config.ScaleF(20.0f);
    const float closeY = headerH + (tabH - closeSize) / 2.0f;
    m_tabCloseBounds = D2D1::RectF(closeX, closeY, closeX + closeSize, closeY + closeSize);

    // Vector '×' mark
    const float cx = closeX + closeSize / 2.0f;
    const float cy = closeY + closeSize / 2.0f;
    const float r = config.ScaleF(3.5f);
    pRT->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), theme.brushTextSecondary.Get(), 1.2f);
    pRT->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx - r, cy + r), theme.brushTextSecondary.Get(), 1.2f);
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

bool EditorCanvas::HitTestTabClose(POINT pt) const {
    return (pt.x >= m_tabCloseBounds.left && pt.x <= m_tabCloseBounds.right &&
            pt.y >= m_tabCloseBounds.top && pt.y <= m_tabCloseBounds.bottom);
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
