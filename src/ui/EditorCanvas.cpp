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

    // 1. Draw Clean Dark Canvas Background (#1E1E1E) with Sharp Square Corners ("it should stay as quare")
    // Flush against the activity rail on the left and frameless header on the top (no gaps, no disconnected color)
    D2D1_RECT_F rcCanvas = D2D1::RectF(canvasLeft, headerH, canvasRight, clientHeight);
    pRT->FillRectangle(rcCanvas, theme.brushCanvasBg.Get());

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
