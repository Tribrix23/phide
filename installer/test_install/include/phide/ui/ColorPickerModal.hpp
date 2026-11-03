/**
 * @file ColorPickerModal.hpp
 * @brief Interactive VS Code-style native Direct2D Color Picker popup modal.
 * 
 * Supports 2D Saturation/Value gradient picker, Hue rainbow slider,
 * Alpha opacity slider, format toggling (HEX, RGBA, HSLA), and live in-place code editing.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <functional>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

enum class ColorFormat {
    Hex,
    Rgb,
    Hsl
};

struct ParsedColorToken {
    size_t line = 0;
    size_t col = 0;
    size_t length = 0;
    std::wstring rawText;
    D2D1_COLOR_F color = { 0, 0, 0, 1.0f };
    ColorFormat format = ColorFormat::Hex;
    bool hasAlpha = false;
};

class ColorPickerModal {
public:
    ColorPickerModal();
    ~ColorPickerModal() = default;

    /**
     * @brief Displays the color picker popup anchored near the clicked color swatch.
     */
    void Open(
        float anchorX,
        float anchorY,
        const std::wstring& initialColorStr,
        size_t line,
        size_t col,
        size_t length
    ) noexcept;

    /**
     * @brief Dismisses the color picker popup.
     */
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }
    [[nodiscard]] size_t GetTargetLine() const noexcept { return m_targetLine; }
    [[nodiscard]] size_t GetTargetCol() const noexcept { return m_targetCol; }
    [[nodiscard]] size_t GetTargetLength() const noexcept { return m_targetLength; }
    [[nodiscard]] const std::wstring& GetCurrentColorString() const noexcept { return m_colorString; }

    /**
     * @brief Renders the color picker UI with Direct2D.
     */
    void Render(
        ID2D1RenderTarget* pRT,
        const Theme& theme,
        const app::AppConfig& config,
        float clientW,
        float clientH
    );

    /**
     * @brief Mouse movement handler for interactive slider and gradient dragging.
     */
    bool HandleMouseMove(
        D2D1_POINT_2F pt,
        std::function<void(const std::wstring& newColor, size_t line, size_t col, size_t length)> onUpdate = nullptr
    );

    /**
     * @brief Mouse down handler for clicking sliders or toggling format.
     */
    bool HandleLButtonDown(
        D2D1_POINT_2F pt,
        std::function<void(const std::wstring& newColor, size_t line, size_t col, size_t length)> onUpdate = nullptr
    );

    /**
     * @brief Mouse up handler.
     */
    bool HandleLButtonUp(D2D1_POINT_2F pt);

    /**
     * @brief Key down handler (ESC to close, etc.).
     */
    bool HandleKeyDown(WPARAM wParam);

    // Color conversion utilities
    static bool TryParseColor(const std::wstring& text, D2D1_COLOR_F& outColor, ColorFormat& outFormat, bool& outHasAlpha);
    static std::wstring FormatColor(float r, float g, float b, float a, ColorFormat fmt, bool hasAlpha);
    static void RgbToHsv(float r, float g, float b, float& h, float& s, float& v);
    static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b);
    static void RgbToHsl(float r, float g, float b, float& h, float& s, float& l);
    static void HslToRgb(float h, float s, float l, float& r, float& g, float& b);

private:
    void UpdateColorFromHsvA(
        std::function<void(const std::wstring& newColor, size_t line, size_t col, size_t length)> onUpdate
    );

    bool m_isOpen = false;
    float m_anchorX = 0.0f;
    float m_anchorY = 0.0f;
    size_t m_targetLine = 0;
    size_t m_targetCol = 0;
    size_t m_targetLength = 0;

    // Active color values
    float m_r = 0.125f;
    float m_g = 0.227f;
    float m_b = 0.678f;
    float m_a = 1.0f;

    float m_hue = 229.0f; // 0..360
    float m_sat = 0.81f;  // 0..1
    float m_val = 0.68f;  // 0..1

    ColorFormat m_format = ColorFormat::Hex;
    bool m_hasAlpha = false;
    std::wstring m_colorString = L"#203AAD";

    // Hit-test and layout bounds
    D2D1_RECT_F m_modalBounds{};
    D2D1_RECT_F m_headerBounds{};
    D2D1_RECT_F m_svBounds{};
    D2D1_RECT_F m_hueBounds{};
    D2D1_RECT_F m_alphaBounds{};
    D2D1_RECT_F m_footerBounds{};

    // Drag states
    bool m_draggingSV = false;
    bool m_draggingHue = false;
    bool m_draggingAlpha = false;
};

} // namespace phide::ui
