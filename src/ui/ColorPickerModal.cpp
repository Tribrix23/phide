/**
 * @file ColorPickerModal.cpp
 * @brief Interactive VS Code-style native Direct2D Color Picker implementation.
 */

#include "phide/ui/ColorPickerModal.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace phide::ui {

ColorPickerModal::ColorPickerModal() = default;

void ColorPickerModal::Open(
    float anchorX,
    float anchorY,
    const std::wstring& initialColorStr,
    size_t line,
    size_t col,
    size_t length
) noexcept {
    m_isOpen = true;
    m_anchorX = anchorX;
    m_anchorY = anchorY;
    m_targetLine = line;
    m_targetCol = col;
    m_targetLength = length;
    m_colorString = initialColorStr;

    D2D1_COLOR_F parsedColor{};
    ColorFormat fmt = ColorFormat::Hex;
    bool hasAlpha = false;
    if (TryParseColor(initialColorStr, parsedColor, fmt, hasAlpha)) {
        m_r = parsedColor.r;
        m_g = parsedColor.g;
        m_b = parsedColor.b;
        m_a = parsedColor.a;
        m_format = fmt;
        m_hasAlpha = hasAlpha;
        RgbToHsv(m_r, m_g, m_b, m_hue, m_sat, m_val);
    } else {
        m_r = 0.125f;
        m_g = 0.227f;
        m_b = 0.678f;
        m_a = 1.0f;
        m_hue = 229.0f;
        m_sat = 0.81f;
        m_val = 0.68f;
        m_format = ColorFormat::Hex;
        m_hasAlpha = false;
    }

    m_draggingSV = false;
    m_draggingHue = false;
    m_draggingAlpha = false;
}

void ColorPickerModal::Close() noexcept {
    m_isOpen = false;
    m_draggingSV = false;
    m_draggingHue = false;
    m_draggingAlpha = false;
}

void ColorPickerModal::Render(
    ID2D1RenderTarget* pRT,
    const Theme& theme,
    const app::AppConfig& config,
    float clientW,
    float clientH
) {
    if (!m_isOpen || !pRT) return;

    float modalW = config.ScaleF(250.0f);
    float modalH = config.ScaleF(215.0f);

    float modalX = m_anchorX;
    float modalY = m_anchorY + config.ScaleF(18.0f);

    // Keep within window viewport boundaries
    if (modalX + modalW > clientW - config.ScaleF(10.0f)) {
        modalX = clientW - modalW - config.ScaleF(10.0f);
    }
    if (modalX < config.ScaleF(10.0f)) {
        modalX = config.ScaleF(10.0f);
    }
    if (modalY + modalH > clientH - config.ScaleF(10.0f)) {
        modalY = m_anchorY - modalH - config.ScaleF(6.0f);
    }
    if (modalY < config.ScaleF(35.0f)) {
        modalY = config.ScaleF(35.0f);
    }

    m_modalBounds = D2D1::RectF(modalX, modalY, modalX + modalW, modalY + modalH);

    // Drop shadow
    D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(
        D2D1::RectF(modalX - 2.0f, modalY - 1.0f, modalX + modalW + 4.0f, modalY + modalH + 5.0f),
        6.0f, 6.0f
    );
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushShadow;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &brushShadow);
    if (brushShadow) pRT->FillRoundedRectangle(rrcShadow, brushShadow.Get());

    // Modal background & crisp border
    D2D1_ROUNDED_RECT rrcModal = D2D1::RoundedRect(m_modalBounds, 5.0f, 5.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0x1e1e1e) : D2D1::ColorF(0xf3f3f3), &brushBg);
    if (brushBg) pRT->FillRoundedRectangle(rrcModal, brushBg.Get());

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBorder;
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0x3c3c3c) : D2D1::ColorF(0xd0d0d0), &brushBorder);
    if (brushBorder) pRT->DrawRoundedRectangle(rrcModal, brushBorder.Get(), 1.0f);

    // 1. Header Bar: Displays current color preview and color text
    float headerH = config.ScaleF(30.0f);
    m_headerBounds = D2D1::RectF(modalX + 1.0f, modalY + 1.0f, modalX + modalW - 1.0f, modalY + headerH);

    // Header background is the current color (with checkerboard underlay if alpha < 1.0)
    if (m_a < 0.999f) {
        float chk = config.ScaleF(6.0f);
        for (float cy = m_headerBounds.top; cy < m_headerBounds.bottom; cy += chk) {
            for (float cx = m_headerBounds.left; cx < m_headerBounds.right; cx += chk) {
                int parity = (static_cast<int>(cx / chk) + static_cast<int>(cy / chk)) % 2;
                D2D1_RECT_F rcChk = D2D1::RectF(cx, cy, (std::min)(cx + chk, m_headerBounds.right), (std::min)(cy + chk, m_headerBounds.bottom));
                brushBorder->SetColor(parity ? D2D1::ColorF(0x2a2a2a) : D2D1::ColorF(0x383838));
                pRT->FillRectangle(rcChk, brushBorder.Get());
            }
        }
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushActiveColor;
    pRT->CreateSolidColorBrush(D2D1::ColorF(m_r, m_g, m_b, m_a), &brushActiveColor);
    if (brushActiveColor) {
        pRT->FillRectangle(m_headerBounds, brushActiveColor.Get());
    }

    // Header text: e.g. "#203AAD" or "rgba(...)"
    float lum = 0.299f * m_r + 0.587f * m_g + 0.114f * m_b;
    D2D1_COLOR_F headerTextColor = (lum > 0.55f && m_a > 0.4f) ? D2D1::ColorF(0x0a0a0a) : D2D1::ColorF(0xffffff);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHeaderText;
    pRT->CreateSolidColorBrush(headerTextColor, &brushHeaderText);

    if (brushHeaderText && theme.dwriteFactory) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> tfHeader;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.0f), L"en-us", &tfHeader))) {
            tfHeader->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            tfHeader->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(m_colorString.c_str(), static_cast<UINT32>(m_colorString.length()), tfHeader.Get(), m_headerBounds, brushHeaderText.Get());
        }
    }

    // 2. 2D Saturation / Value Gradient Picker Area
    float pad = config.ScaleF(8.0f);
    float svW = config.ScaleF(175.0f);
    float svH = config.ScaleF(120.0f);
    float svX = modalX + pad;
    float svY = modalY + headerH + pad;
    m_svBounds = D2D1::RectF(svX, svY, svX + svW, svY + svH);

    // Draw horizontal gradient (White to Pure Hue at current Hue)
    float pureR = 1.0f, pureG = 0.0f, pureB = 0.0f;
    HsvToRgb(m_hue, 1.0f, 1.0f, pureR, pureG, pureB);

    D2D1_GRADIENT_STOP stopsHoriz[2];
    stopsHoriz[0] = { 0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f) };
    stopsHoriz[1] = { 1.0f, D2D1::ColorF(pureR, pureG, pureB, 1.0f) };

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollHoriz;
    if (SUCCEEDED(pRT->CreateGradientStopCollection(stopsHoriz, 2, &stopCollHoriz))) {
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> brushHoriz;
        if (SUCCEEDED(pRT->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(m_svBounds.left, m_svBounds.top), D2D1::Point2F(m_svBounds.right, m_svBounds.top)),
            stopCollHoriz.Get(), &brushHoriz))) {
            pRT->FillRectangle(m_svBounds, brushHoriz.Get());
        }
    }

    // Overlay vertical gradient (Transparent at top to Pure Black at bottom)
    D2D1_GRADIENT_STOP stopsVert[2];
    stopsVert[0] = { 0.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f) };
    stopsVert[1] = { 1.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f) };

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollVert;
    if (SUCCEEDED(pRT->CreateGradientStopCollection(stopsVert, 2, &stopCollVert))) {
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> brushVert;
        if (SUCCEEDED(pRT->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(m_svBounds.left, m_svBounds.top), D2D1::Point2F(m_svBounds.left, m_svBounds.bottom)),
            stopCollVert.Get(), &brushVert))) {
            pRT->FillRectangle(m_svBounds, brushVert.Get());
        }
    }

    // Border around SV box
    pRT->DrawRectangle(m_svBounds, brushBorder.Get(), 1.0f);

    // SV Handle: circle indicator at (m_sat, 1 - m_val)
    float hx = m_svBounds.left + m_sat * (m_svBounds.right - m_svBounds.left);
    float hy = m_svBounds.top + (1.0f - m_val) * (m_svBounds.bottom - m_svBounds.top);
    float hr = config.ScaleF(4.5f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHandle;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0xffffff), &brushHandle);
    if (brushHandle) {
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(hx, hy), hr, hr), brushHandle.Get(), 2.0f);
        brushHandle->SetColor(D2D1::ColorF(0x000000));
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(hx, hy), hr + 1.0f, hr + 1.0f), brushHandle.Get(), 1.0f);
    }

    // 3. Vertical Rainbow Hue Slider
    float sliderW = config.ScaleF(14.0f);
    float hueX = m_svBounds.right + config.ScaleF(8.0f);
    m_hueBounds = D2D1::RectF(hueX, svY, hueX + sliderW, svY + svH);

    D2D1_GRADIENT_STOP stopsHue[7];
    stopsHue[0] = { 0.0f / 6.0f, D2D1::ColorF(1.0f, 0.0f, 0.0f) };
    stopsHue[1] = { 1.0f / 6.0f, D2D1::ColorF(1.0f, 1.0f, 0.0f) };
    stopsHue[2] = { 2.0f / 6.0f, D2D1::ColorF(0.0f, 1.0f, 0.0f) };
    stopsHue[3] = { 3.0f / 6.0f, D2D1::ColorF(0.0f, 1.0f, 1.0f) };
    stopsHue[4] = { 4.0f / 6.0f, D2D1::ColorF(0.0f, 0.0f, 1.0f) };
    stopsHue[5] = { 5.0f / 6.0f, D2D1::ColorF(1.0f, 0.0f, 1.0f) };
    stopsHue[6] = { 6.0f / 6.0f, D2D1::ColorF(1.0f, 0.0f, 0.0f) };

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollHue;
    if (SUCCEEDED(pRT->CreateGradientStopCollection(stopsHue, 7, &stopCollHue))) {
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> brushHue;
        if (SUCCEEDED(pRT->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(m_hueBounds.left, m_hueBounds.top), D2D1::Point2F(m_hueBounds.left, m_hueBounds.bottom)),
            stopCollHue.Get(), &brushHue))) {
            pRT->FillRectangle(m_hueBounds, brushHue.Get());
        }
    }
    pRT->DrawRectangle(m_hueBounds, brushBorder.Get(), 1.0f);

    // Hue handle thumb (horizontal white pill)
    float hueY = m_hueBounds.top + (m_hue / 360.0f) * (m_hueBounds.bottom - m_hueBounds.top);
    D2D1_RECT_F rcHueThumb = D2D1::RectF(m_hueBounds.left - 2.0f, hueY - 3.0f, m_hueBounds.right + 2.0f, hueY + 3.0f);
    if (brushHandle) {
        brushHandle->SetColor(D2D1::ColorF(0xffffff));
        pRT->FillRectangle(rcHueThumb, brushHandle.Get());
        brushHandle->SetColor(D2D1::ColorF(0x1a1a1a));
        pRT->DrawRectangle(rcHueThumb, brushHandle.Get(), 1.0f);
    }

    // 4. Vertical Alpha / Opacity Slider (with Checkerboard background)
    float alphaX = m_hueBounds.right + config.ScaleF(8.0f);
    m_alphaBounds = D2D1::RectF(alphaX, svY, alphaX + sliderW, svY + svH);

    // Checkerboard
    float chkS = config.ScaleF(4.0f);
    for (float cy = m_alphaBounds.top; cy < m_alphaBounds.bottom; cy += chkS) {
        for (float cx = m_alphaBounds.left; cx < m_alphaBounds.right; cx += chkS) {
            int p = (static_cast<int>(cx / chkS) + static_cast<int>(cy / chkS)) % 2;
            D2D1_RECT_F rcC = D2D1::RectF(cx, cy, (std::min)(cx + chkS, m_alphaBounds.right), (std::min)(cy + chkS, m_alphaBounds.bottom));
            brushBorder->SetColor(p ? D2D1::ColorF(0x333333) : D2D1::ColorF(0x666666));
            pRT->FillRectangle(rcC, brushBorder.Get());
        }
    }

    // Alpha gradient overlay (Solid color at top, transparent at bottom)
    D2D1_GRADIENT_STOP stopsAlpha[2];
    stopsAlpha[0] = { 0.0f, D2D1::ColorF(m_r, m_g, m_b, 1.0f) };
    stopsAlpha[1] = { 1.0f, D2D1::ColorF(m_r, m_g, m_b, 0.0f) };

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollAlpha;
    if (SUCCEEDED(pRT->CreateGradientStopCollection(stopsAlpha, 2, &stopCollAlpha))) {
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> brushAlpha;
        if (SUCCEEDED(pRT->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(m_alphaBounds.left, m_alphaBounds.top), D2D1::Point2F(m_alphaBounds.left, m_alphaBounds.bottom)),
            stopCollAlpha.Get(), &brushAlpha))) {
            pRT->FillRectangle(m_alphaBounds, brushAlpha.Get());
        }
    }
    pRT->DrawRectangle(m_alphaBounds, brushBorder.Get(), 1.0f);

    // Alpha handle thumb
    float alphaY = m_alphaBounds.top + (1.0f - m_a) * (m_alphaBounds.bottom - m_alphaBounds.top);
    D2D1_RECT_F rcAlphaThumb = D2D1::RectF(m_alphaBounds.left - 2.0f, alphaY - 3.0f, m_alphaBounds.right + 2.0f, alphaY + 3.0f);
    if (brushHandle) {
        brushHandle->SetColor(D2D1::ColorF(0xffffff));
        pRT->FillRectangle(rcAlphaThumb, brushHandle.Get());
        brushHandle->SetColor(D2D1::ColorF(0x1a1a1a));
        pRT->DrawRectangle(rcAlphaThumb, brushHandle.Get(), 1.0f);
    }

    // 5. Footer Documentation / Info Bar (e.g. "Color of an element's text")
    float footerY = svY + svH + config.ScaleF(8.0f);
    float footerH = modalY + modalH - footerY - config.ScaleF(4.0f);
    m_footerBounds = D2D1::RectF(modalX + pad, footerY, modalX + modalW - pad, footerY + footerH);

    if (theme.brushTextMuted && theme.dwriteFactory) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> tfFooter;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(11.0f), L"en-us", &tfFooter))) {
            tfFooter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            tfFooter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            const wchar_t* infoMsg = L"Click header to switch format (HEX/RGB/HSL)";
            pRT->DrawText(infoMsg, static_cast<UINT32>(wcslen(infoMsg)), tfFooter.Get(), m_footerBounds, theme.brushTextMuted.Get());
        }
    }
}

bool ColorPickerModal::HandleMouseMove(
    D2D1_POINT_2F pt,
    std::function<void(const std::wstring& newColor, size_t line, size_t col, size_t length)> onUpdate
) {
    if (!m_isOpen) return false;

    if (m_draggingSV) {
        float w = (std::max)(1.0f, m_svBounds.right - m_svBounds.left);
        float h = (std::max)(1.0f, m_svBounds.bottom - m_svBounds.top);
        float s = (pt.x - m_svBounds.left) / w;
        float v = 1.0f - (pt.y - m_svBounds.top) / h;
        m_sat = (std::clamp)(s, 0.0f, 1.0f);
        m_val = (std::clamp)(v, 0.0f, 1.0f);
        UpdateColorFromHsvA(onUpdate);
        return true;
    }

    if (m_draggingHue) {
        float h = (std::max)(1.0f, m_hueBounds.bottom - m_hueBounds.top);
        float hueFrac = (pt.y - m_hueBounds.top) / h;
        m_hue = (std::clamp)(hueFrac * 360.0f, 0.0f, 360.0f);
        UpdateColorFromHsvA(onUpdate);
        return true;
    }

    if (m_draggingAlpha) {
        float h = (std::max)(1.0f, m_alphaBounds.bottom - m_alphaBounds.top);
        float alphaFrac = 1.0f - (pt.y - m_alphaBounds.top) / h;
        m_a = (std::clamp)(alphaFrac, 0.0f, 1.0f);
        m_hasAlpha = (m_a < 0.999f);
        UpdateColorFromHsvA(onUpdate);
        return true;
    }

    return false;
}

bool ColorPickerModal::HandleLButtonDown(
    D2D1_POINT_2F pt,
    std::function<void(const std::wstring& newColor, size_t line, size_t col, size_t length)> onUpdate
) {
    if (!m_isOpen) return false;

    // Check if clicked outside modal bounds: dismiss
    if (pt.x < m_modalBounds.left || pt.x > m_modalBounds.right ||
        pt.y < m_modalBounds.top || pt.y > m_modalBounds.bottom) {
        Close();
        return false;
    }

    // 1. Clicked Header: Cycle format (HEX -> RGB -> HSL -> HEX)
    if (pt.x >= m_headerBounds.left && pt.x <= m_headerBounds.right &&
        pt.y >= m_headerBounds.top && pt.y <= m_headerBounds.bottom) {
        if (m_format == ColorFormat::Hex) m_format = ColorFormat::Rgb;
        else if (m_format == ColorFormat::Rgb) m_format = ColorFormat::Hsl;
        else m_format = ColorFormat::Hex;

        m_colorString = FormatColor(m_r, m_g, m_b, m_a, m_format, m_hasAlpha);
        if (onUpdate) {
            onUpdate(m_colorString, m_targetLine, m_targetCol, m_targetLength);
            m_targetLength = m_colorString.length();
        }
        return true;
    }

    // 2. Clicked 2D Saturation / Value Box
    if (pt.x >= m_svBounds.left && pt.x <= m_svBounds.right &&
        pt.y >= m_svBounds.top && pt.y <= m_svBounds.bottom) {
        m_draggingSV = true;
        HandleMouseMove(pt, onUpdate);
        return true;
    }

    // 3. Clicked Hue Slider
    if (pt.x >= m_hueBounds.left && pt.x <= m_hueBounds.right &&
        pt.y >= m_hueBounds.top && pt.y <= m_hueBounds.bottom) {
        m_draggingHue = true;
        HandleMouseMove(pt, onUpdate);
        return true;
    }

    // 4. Clicked Alpha Slider
    if (pt.x >= m_alphaBounds.left && pt.x <= m_alphaBounds.right &&
        pt.y >= m_alphaBounds.top && pt.y <= m_alphaBounds.bottom) {
        m_draggingAlpha = true;
        HandleMouseMove(pt, onUpdate);
        return true;
    }

    return true; // Click consumed by modal
}

bool ColorPickerModal::HandleLButtonUp(D2D1_POINT_2F /*pt*/) {
    if (!m_isOpen) return false;
    bool wasDragging = m_draggingSV || m_draggingHue || m_draggingAlpha;
    m_draggingSV = false;
    m_draggingHue = false;
    m_draggingAlpha = false;
    return wasDragging;
}

bool ColorPickerModal::HandleKeyDown(WPARAM wParam) {
    if (!m_isOpen) return false;
    if (wParam == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

void ColorPickerModal::UpdateColorFromHsvA(
    std::function<void(const std::wstring& newColor, size_t line, size_t col, size_t length)> onUpdate
) {
    HsvToRgb(m_hue, m_sat, m_val, m_r, m_g, m_b);
    m_colorString = FormatColor(m_r, m_g, m_b, m_a, m_format, m_hasAlpha);
    if (onUpdate) {
        onUpdate(m_colorString, m_targetLine, m_targetCol, m_targetLength);
        m_targetLength = m_colorString.length();
    }
}

// Color Math & Conversions
bool ColorPickerModal::TryParseColor(
    const std::wstring& text,
    D2D1_COLOR_F& outColor,
    ColorFormat& outFormat,
    bool& outHasAlpha
) {
    outHasAlpha = false;
    outFormat = ColorFormat::Hex;

    if (text.empty()) return false;

    // Hex (#RGB, #RGBA, #RRGGBB, #RRGGBBAA)
    if (text[0] == L'#') {
        std::wstring h = text.substr(1);
        if (h.length() == 3) {
            unsigned int r, g, b;
            if (swscanf_s(h.c_str(), L"%1x%1x%1x", &r, &g, &b) == 3) {
                outColor = D2D1::ColorF((r * 17) / 255.0f, (g * 17) / 255.0f, (b * 17) / 255.0f, 1.0f);
                outFormat = ColorFormat::Hex;
                return true;
            }
        } else if (h.length() == 4) {
            unsigned int r, g, b, a;
            if (swscanf_s(h.c_str(), L"%1x%1x%1x%1x", &r, &g, &b, &a) == 4) {
                outColor = D2D1::ColorF((r * 17) / 255.0f, (g * 17) / 255.0f, (b * 17) / 255.0f, (a * 17) / 255.0f);
                outFormat = ColorFormat::Hex;
                outHasAlpha = true;
                return true;
            }
        } else if (h.length() == 6) {
            unsigned int rgb;
            if (swscanf_s(h.c_str(), L"%x", &rgb) == 1) {
                outColor = D2D1::ColorF(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, 1.0f);
                outFormat = ColorFormat::Hex;
                return true;
            }
        } else if (h.length() == 8) {
            unsigned int rgba;
            if (swscanf_s(h.c_str(), L"%x", &rgba) == 1) {
                outColor = D2D1::ColorF(((rgba >> 24) & 0xff) / 255.0f, ((rgba >> 16) & 0xff) / 255.0f, ((rgba >> 8) & 0xff) / 255.0f, (rgba & 0xff) / 255.0f);
                outFormat = ColorFormat::Hex;
                outHasAlpha = true;
                return true;
            }
        }
    }

    // rgb(...) / rgba(...)
    if (text.rfind(L"rgba(", 0) == 0 || text.rfind(L"rgb(", 0) == 0) {
        float r = 0, g = 0, b = 0, a = 1.0f;
        int parsed = swscanf_s(text.c_str(), L"rgba(%f,%f,%f,%f)", &r, &g, &b, &a);
        if (parsed < 4) {
            parsed = swscanf_s(text.c_str(), L"rgb(%f,%f,%f)", &r, &g, &b);
            a = 1.0f;
        }
        if (parsed >= 3) {
            outColor = D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
            outFormat = ColorFormat::Rgb;
            outHasAlpha = (parsed == 4 && a < 0.999f);
            return true;
        }
    }

    // hsl(...) / hsla(...)
    if (text.rfind(L"hsla(", 0) == 0 || text.rfind(L"hsl(", 0) == 0) {
        float h = 0, s = 0, l = 0, a = 1.0f;
        int parsed = swscanf_s(text.c_str(), L"hsla(%f,%f%%,%f%%,%f)", &h, &s, &l, &a);
        if (parsed < 4) {
            parsed = swscanf_s(text.c_str(), L"hsl(%f,%f%%,%f%%)", &h, &s, &l);
            a = 1.0f;
        }
        if (parsed >= 3) {
            float r, g, b;
            HslToRgb(h, s / 100.0f, l / 100.0f, r, g, b);
            outColor = D2D1::ColorF(r, g, b, a);
            outFormat = ColorFormat::Hsl;
            outHasAlpha = (parsed == 4 && a < 0.999f);
            return true;
        }
    }

    // Common named colors
    struct NamedColor { const wchar_t* name; unsigned int hex; };
    static const NamedColor namedColors[] = {
        { L"black", 0x000000 }, { L"white", 0xffffff }, { L"red", 0xff0000 },
        { L"green", 0x008000 }, { L"blue", 0x0000ff }, { L"yellow", 0xffff00 },
        { L"azure", 0xf0ffff }, { L"coral", 0xff7f50 }, { L"crimson", 0xdc143c },
        { L"darkgray", 0xa9a9a9 }, { L"gray", 0x808080 }, { L"indigo", 0x4b0082 },
        { L"orange", 0xffa500 }, { L"pink", 0xffc0cb }, { L"purple", 0x800080 },
        { L"teal", 0x008080 }, { L"violet", 0xee82ee }, { L"gold", 0xffd700 }
    };
    for (const auto& nc : namedColors) {
        if (_wcsicmp(text.c_str(), nc.name) == 0) {
            outColor = D2D1::ColorF(((nc.hex >> 16) & 0xff) / 255.0f, ((nc.hex >> 8) & 0xff) / 255.0f, (nc.hex & 0xff) / 255.0f, 1.0f);
            outFormat = ColorFormat::Hex;
            return true;
        }
    }

    return false;
}

std::wstring ColorPickerModal::FormatColor(
    float r, float g, float b, float a, ColorFormat fmt, bool hasAlpha
) {
    int ir = (std::clamp)(static_cast<int>(std::round(r * 255.0f)), 0, 255);
    int ig = (std::clamp)(static_cast<int>(std::round(g * 255.0f)), 0, 255);
    int ib = (std::clamp)(static_cast<int>(std::round(b * 255.0f)), 0, 255);
    int ia = (std::clamp)(static_cast<int>(std::round(a * 255.0f)), 0, 255);

    wchar_t buf[64]{};
    if (fmt == ColorFormat::Hex) {
        if (hasAlpha && a < 0.999f) {
            swprintf_s(buf, L"#%02X%02X%02X%02X", ir, ig, ib, ia);
        } else {
            swprintf_s(buf, L"#%02X%02X%02X", ir, ig, ib);
        }
    } else if (fmt == ColorFormat::Rgb) {
        if (hasAlpha && a < 0.999f) {
            swprintf_s(buf, L"rgba(%d, %d, %d, %.2f)", ir, ig, ib, a);
        } else {
            swprintf_s(buf, L"rgb(%d, %d, %d)", ir, ig, ib);
        }
    } else if (fmt == ColorFormat::Hsl) {
        float h, s, l;
        RgbToHsl(r, g, b, h, s, l);
        int ih = static_cast<int>(std::round(h));
        int is = static_cast<int>(std::round(s * 100.0f));
        int il = static_cast<int>(std::round(l * 100.0f));
        if (hasAlpha && a < 0.999f) {
            swprintf_s(buf, L"hsla(%d, %d%%, %d%%, %.2f)", ih, is, il, a);
        } else {
            swprintf_s(buf, L"hsl(%d, %d%%, %d%%)", ih, is, il);
        }
    }
    return buf;
}

void ColorPickerModal::RgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float maxC = (std::max)({ r, g, b });
    float minC = (std::min)({ r, g, b });
    float delta = maxC - minC;

    v = maxC;
    s = (maxC <= 0.0001f) ? 0.0f : (delta / maxC);

    if (delta <= 0.0001f) {
        h = 0.0f;
    } else if (maxC == r) {
        h = 60.0f * std::fmod((g - b) / delta, 6.0f);
    } else if (maxC == g) {
        h = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        h = 60.0f * (((r - g) / delta) + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;
}

void ColorPickerModal::HsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    if (h < 60.0f) { r = c; g = x; b = 0.0f; }
    else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
    else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
    else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
    else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
    else { r = c; g = 0.0f; b = x; }

    r += m; g += m; b += m;
}

void ColorPickerModal::RgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxC = (std::max)({ r, g, b });
    float minC = (std::min)({ r, g, b });
    float delta = maxC - minC;

    l = (maxC + minC) / 2.0f;

    if (delta <= 0.0001f) {
        h = 0.0f;
        s = 0.0f;
    } else {
        s = l > 0.5f ? (delta / (2.0f - maxC - minC)) : (delta / (maxC + minC));
        if (maxC == r) {
            h = 60.0f * std::fmod((g - b) / delta, 6.0f);
        } else if (maxC == g) {
            h = 60.0f * (((b - r) / delta) + 2.0f);
        } else {
            h = 60.0f * (((r - g) / delta) + 4.0f);
        }
        if (h < 0.0f) h += 360.0f;
    }
}

void ColorPickerModal::HslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    auto hue2rgb = [](float p, float q, float t) {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 1.0f / 2.0f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };

    if (s <= 0.0001f) {
        r = g = b = l;
    } else {
        float q = l < 0.5f ? (l * (1.0f + s)) : (l + s - l * s);
        float p = 2.0f * l - q;
        float hNorm = h / 360.0f;
        r = hue2rgb(p, q, hNorm + 1.0f / 3.0f);
        g = hue2rgb(p, q, hNorm);
        b = hue2rgb(p, q, hNorm - 1.0f / 3.0f);
    }
}

} // namespace phide::ui
