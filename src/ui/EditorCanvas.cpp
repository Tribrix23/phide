/**
 * @file EditorCanvas.cpp
 * @brief Implementation of glassmorphic dark workspace surface with purple-to-black gradient and rounded container corners.
 */

#include "phide/ui/EditorCanvas.hpp"
#include <cmath>

namespace phide::ui {

using Microsoft::WRL::ComPtr;

EditorCanvas::EditorCanvas() = default;

void EditorCanvas::DiscardDeviceResources() {
    m_pBgGradientBrush.Reset();
    m_pGlassSheenBrush.Reset();
    m_pGlassBorderBrush.Reset();
    m_pPurpleGlowBrush.Reset();
    m_lastLeft   = -1.0f;
    m_lastWidth  = -1.0f;
    m_lastHeight = -1.0f;
}

void EditorCanvas::EnsureGlassBrushes(ID2D1RenderTarget* pRT, float canvasLeft, float canvasWidth, float headerH, float clientHeight) {
    if (m_pBgGradientBrush &&
        std::abs(m_lastLeft - canvasLeft) < 0.5f &&
        std::abs(m_lastWidth - canvasWidth) < 0.5f &&
        std::abs(m_lastHeight - clientHeight) < 0.5f) {
        return;
    }

    m_lastLeft   = canvasLeft;
    m_lastWidth  = canvasWidth;
    m_lastHeight = clientHeight;

    m_pBgGradientBrush.Reset();
    m_pGlassSheenBrush.Reset();
    m_pGlassBorderBrush.Reset();
    m_pPurpleGlowBrush.Reset();

    // 1. Purple-to-Black Linear Gradient (Horizontal from Left to Right)
    // Left side: Rich glass purple; Right side: Deep black (matching user request)
    D2D1_GRADIENT_STOP bgStops[6];
    bgStops[0] = { 0.00f, D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f) }; // #3D1461 - Rich Glass Purple
    bgStops[1] = { 0.18f, D2D1::ColorF(0.17f, 0.06f, 0.29f, 1.0f) }; // #2C0F4A - Deep Royal Violet
    bgStops[2] = { 0.38f, D2D1::ColorF(0.12f, 0.04f, 0.21f, 1.0f) }; // #1F0B36 - Dark Purple-Indigo
    bgStops[3] = { 0.60f, D2D1::ColorF(0.08f, 0.03f, 0.14f, 1.0f) }; // #140824 - Midnight Purple
    bgStops[4] = { 0.82f, D2D1::ColorF(0.05f, 0.04f, 0.07f, 1.0f) }; // #0D0A12 - Fade to Black
    bgStops[5] = { 1.00f, D2D1::ColorF(0.04f, 0.04f, 0.04f, 1.0f) }; // #0A0A0A - Pure Obsidian Black on right side

    ComPtr<ID2D1GradientStopCollection> pBgStopColl;
    if (SUCCEEDED(pRT->CreateGradientStopCollection(bgStops, 6, &pBgStopColl))) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES bgProps = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(canvasLeft, headerH),
            D2D1::Point2F(canvasLeft + canvasWidth, headerH)
        );
        pRT->CreateLinearGradientBrush(bgProps, pBgStopColl.Get(), &m_pBgGradientBrush);
    }

    // 2. Ambient Purple Bloom for Glass Luminance (Soft Radial Gradient on Left)
    D2D1_GRADIENT_STOP glowStops[3];
    glowStops[0] = { 0.00f, D2D1::ColorF(0.72f, 0.28f, 1.0f, 0.15f) }; // Luminous violet bloom
    glowStops[1] = { 0.50f, D2D1::ColorF(0.48f, 0.16f, 0.78f, 0.05f) };
    glowStops[2] = { 1.00f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f) };

    ComPtr<ID2D1GradientStopCollection> pGlowStopColl;
    if (SUCCEEDED(pRT->CreateGradientStopCollection(glowStops, 3, &pGlowStopColl))) {
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES glowProps = D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(canvasLeft + 120.0f, headerH + 130.0f),
            D2D1::Point2F(0.0f, 0.0f),
            320.0f,
            240.0f
        );
        pRT->CreateRadialGradientBrush(glowProps, pGlowStopColl.Get(), &m_pPurpleGlowBrush);
    }

    // 3. Top Specular Glass Reflection Sheen (Top-to-Bottom)
    D2D1_GRADIENT_STOP sheenStops[3];
    sheenStops[0] = { 0.00f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f) }; // Frosted top reflection
    sheenStops[1] = { 0.35f, D2D1::ColorF(0.85f, 0.65f, 1.0f, 0.02f) };
    sheenStops[2] = { 1.00f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f) };

    ComPtr<ID2D1GradientStopCollection> pSheenStopColl;
    if (SUCCEEDED(pRT->CreateGradientStopCollection(sheenStops, 3, &pSheenStopColl))) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES sheenProps = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(canvasLeft, headerH),
            D2D1::Point2F(canvasLeft, headerH + 180.0f)
        );
        pRT->CreateLinearGradientBrush(sheenProps, pSheenStopColl.Get(), &m_pGlassSheenBrush);
    }

    // 4. Frosted Glass Rim Border (1px highlight tracing the rounded perimeter)
    D2D1_GRADIENT_STOP borderStops[4];
    borderStops[0] = { 0.00f, D2D1::ColorF(0.85f, 0.70f, 1.0f, 0.38f) }; // Bright glass rim on top-left
    borderStops[1] = { 0.30f, D2D1::ColorF(0.55f, 0.35f, 0.85f, 0.20f) };
    borderStops[2] = { 0.70f, D2D1::ColorF(0.20f, 0.15f, 0.30f, 0.12f) };
    borderStops[3] = { 1.00f, D2D1::ColorF(0.15f, 0.15f, 0.18f, 0.20f) }; // Subtle rim on black side

    ComPtr<ID2D1GradientStopCollection> pBorderStopColl;
    if (SUCCEEDED(pRT->CreateGradientStopCollection(borderStops, 4, &pBorderStopColl))) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES borderProps = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(canvasLeft, headerH),
            D2D1::Point2F(canvasLeft + canvasWidth, clientHeight)
        );
        pRT->CreateLinearGradientBrush(borderProps, pBorderStopColl.Get(), &m_pGlassBorderBrush);
    }
}

void EditorCanvas::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          const editor::Buffer& buffer, float canvasLeft, float canvasWidth, float clientHeight) {
    (void)buffer;
    if (!pRT) return;

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));

    EnsureGlassBrushes(pRT, canvasLeft, canvasWidth, headerH, clientHeight);

    // 1. Draw Glass Effect Canvas Background with Rounded Corners (as requested in Image 1 & 3)
    D2D1_RECT_F rcCanvas = D2D1::RectF(canvasLeft, headerH, canvasLeft + canvasWidth, clientHeight);
    D2D1_ROUNDED_RECT rrcCanvas = D2D1::RoundedRect(rcCanvas, 12.0f, 12.0f);

    // Fill purple-to-black linear gradient
    if (m_pBgGradientBrush) {
        pRT->FillRoundedRectangle(rrcCanvas, m_pBgGradientBrush.Get());
    } else {
        pRT->FillRoundedRectangle(rrcCanvas, theme.brushWindowBg.Get());
    }

    // Fill ambient purple bloom for luminous depth
    if (m_pPurpleGlowBrush) {
        pRT->FillRoundedRectangle(rrcCanvas, m_pPurpleGlowBrush.Get());
    }

    // Fill top specular glass reflection sheen
    if (m_pGlassSheenBrush) {
        pRT->FillRoundedRectangle(rrcCanvas, m_pGlassSheenBrush.Get());
    }

    // Draw 1px frosted glass rim border
    if (m_pGlassBorderBrush) {
        pRT->DrawRoundedRectangle(rrcCanvas, m_pGlassBorderBrush.Get(), 1.0f);
    } else {
        pRT->DrawRoundedRectangle(rrcCanvas, theme.brushBorderSubtle.Get(), 1.0f);
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
