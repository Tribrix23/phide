/**
 * @file EditorCanvas.cpp
 * @brief Implementation of dark workspace surface with modern rounded container aesthetics.
 */

#include "phide/ui/EditorCanvas.hpp"

namespace phide::ui {

EditorCanvas::EditorCanvas() = default;

void EditorCanvas::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          const editor::Buffer& buffer, float canvasLeft, float canvasWidth, float clientHeight) {
    (void)buffer;
    if (!pRT) return;

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float canvasRight = canvasLeft + canvasWidth;
    const float cornerRadius = 12.0f;

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

                    // 6. Arc around ONLY the upper-left corner
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

    // 2. Render Centered Clean Dark IDE Watermark & Quick Keybindings
    const float centerY = headerH + (clientHeight - headerH) / 2.0f;
    const float hintW   = config.ScaleF(400.0f);
    const float hintX   = canvasLeft + (canvasWidth - hintW) / 2.0f;

    // Logo / Product Name
    const wchar_t* brandName = L"phide";
    D2D1_RECT_F rcBrand = D2D1::RectF(hintX, centerY - config.ScaleF(60.0f), hintX + hintW, centerY - config.ScaleF(20.0f));
    pRT->DrawText(brandName, static_cast<UINT32>(wcslen(brandName)), theme.fontTitle.Get(), rcBrand, theme.brushTextPrimary.Get());

    // Tagline
    const wchar_t* tagline = L"Modern C++ & PHP Environment";
    D2D1_RECT_F rcTagline = D2D1::RectF(hintX, centerY - config.ScaleF(20.0f), hintX + hintW, centerY + config.ScaleF(10.0f));
    pRT->DrawText(tagline, static_cast<UINT32>(wcslen(tagline)), theme.fontHint.Get(), rcTagline, theme.brushTextSecondary.Get());

    // Shortcut Hints
    const wchar_t* hints = L"Show Explorer \u2014 Click sidebar icon\nOpen Workspace \u2014 Ctrl+O\nCommand Palette \u2014 Ctrl+Shift+P";
    D2D1_RECT_F rcHints = D2D1::RectF(hintX, centerY + config.ScaleF(20.0f), hintX + hintW, centerY + config.ScaleF(100.0f));
    pRT->DrawText(hints, static_cast<UINT32>(wcslen(hints)), theme.fontHint.Get(), rcHints, theme.brushTextMuted.Get());
}

} // namespace phide::ui
