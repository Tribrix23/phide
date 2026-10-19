/**
 * @file EditorCanvas.cpp
 * @brief Implementation of dark workspace surface with modern rounded container aesthetics.
 */

#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/WatermarkLogo.hpp"
#include <algorithm>

namespace phide::ui {

EditorCanvas::EditorCanvas() = default;

void EditorCanvas::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          const editor::Buffer& buffer, float canvasLeft, float canvasWidth, float clientHeight) {
    (void)buffer;
    if (!pRT) return;

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float canvasRight = canvasLeft + canvasWidth;
    const float cornerRadius = 14.0f;

    // Check if cached geometry needs rebuilding (e.g. on resize or layout changes)
    if (!m_pCanvasGeometry ||
        m_cachedLeft != canvasLeft ||
        m_cachedTop != headerH ||
        m_cachedRight != canvasRight ||
        m_cachedBottom != clientHeight)
    {
        m_cachedLeft   = canvasLeft;
        m_cachedTop    = headerH;
        m_cachedRight  = canvasRight;
        m_cachedBottom = clientHeight;
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

                    // 1. Begin at the top edge right after the upper-left rounded corner
                    sink->BeginFigure(D2D1::Point2F(canvasLeft + r, headerH), D2D1_FIGURE_BEGIN_FILLED);

                    // 2. Top edge straight to top-right corner (SHARP - not curved)
                    sink->AddLine(D2D1::Point2F(canvasRight, headerH));

                    // 3. Right edge straight down to bottom-right corner (SHARP - not curved)
                    sink->AddLine(D2D1::Point2F(canvasRight, clientHeight));

                    // 4. Bottom edge straight to bottom-left corner (SHARP - not curved)
                    sink->AddLine(D2D1::Point2F(canvasLeft, clientHeight));

                    // 5. Left edge straight up to start of the upper-left curve
                    sink->AddLine(D2D1::Point2F(canvasLeft, headerH + r));

                    // 6. Arc around ONLY the upper-left corner as requested ("this should be rounded right upper left only")
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

    // 2. Render Centered Metallic White Brand Logo & Quick Keybindings (word "phide" removed as requested)
    const float centerY = headerH + (clientHeight - headerH) / 2.0f;
    const float hintW   = config.ScaleF(400.0f);
    const float hintX   = canvasLeft + (canvasWidth - hintW) / 2.0f;

    // Initialize metallic white watermark bitmap if needed
    if (!m_pWatermarkBitmap) {
        D2D1_SIZE_U size = D2D1::SizeU(kWatermarkLogoWidth, kWatermarkLogoHeight);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        pRT->CreateBitmap(size, g_watermarkLogoBgra, kWatermarkLogoWidth * 4, props, &m_pWatermarkBitmap);
    }

    // Render Metallic White / Platinum Inverted Logo Watermark in center of canvas
    const float logoSize = config.ScaleF(88.0f);
    const float logoX = canvasLeft + (canvasWidth - logoSize) / 2.0f;
    const float logoY = centerY - logoSize / 2.0f - config.ScaleF(28.0f);
    D2D1_RECT_F rcLogo = D2D1::RectF(logoX, logoY, logoX + logoSize, logoY + logoSize);

    if (m_pWatermarkBitmap) {
        pRT->DrawBitmap(m_pWatermarkBitmap.Get(), rcLogo);
    }

    // Shortcut Hints centered below the logo
    const wchar_t* hints = L"Show Explorer \u2014 Click sidebar icon\nOpen Workspace \u2014 Ctrl+O\nCommand Palette \u2014 Ctrl+Shift+P";
    D2D1_RECT_F rcHints = D2D1::RectF(hintX, logoY + logoSize + config.ScaleF(20.0f), hintX + hintW, logoY + logoSize + config.ScaleF(100.0f));
    pRT->DrawText(hints, static_cast<UINT32>(wcslen(hints)), theme.fontHint.Get(), rcHints, theme.brushTextMuted.Get());
}

} // namespace phide::ui
