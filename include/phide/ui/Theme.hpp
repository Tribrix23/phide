/**
 * @file Theme.hpp
 * @brief Visual theming, color palettes, and Direct2D/DirectWrite rendering resources for Dark IDE.
 * 
 * Provides centralized dark IDE color constants, Direct2D solid color brushes, and
 * DirectWrite text formats. Includes dedicated brushes for red close button hover and
 * customized sidebar tooltips.
 */

#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace phide::ui {

using Microsoft::WRL::ComPtr;

/**
 * @struct ThemeColors
 * @brief Professional Dark IDE color palette (VS Code / JetBrains inspired).
 */
struct ThemeColors {
    // Window & Canvas Backgrounds
    D2D1_COLOR_F windowBg        = D2D1::ColorF(0.078f, 0.078f, 0.078f, 1.0f); // #141414 (Dark Shell Base)
    D2D1_COLOR_F headerBg        = D2D1::ColorF(0.078f, 0.078f, 0.078f, 1.0f); // #141414 (Seamless Dark Title Bar)
    D2D1_COLOR_F railBg          = D2D1::ColorF(0.078f, 0.078f, 0.078f, 1.0f); // #141414 (Seamless Dark Activity Rail)
    D2D1_COLOR_F drawerBg        = D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f); // #181818 (Sidebar Drawer)

    // Borders & Dividers
    D2D1_COLOR_F borderSubtle    = D2D1::ColorF(0.173f, 0.173f, 0.173f, 1.0f); // #2C2C2C
    D2D1_COLOR_F borderHover     = D2D1::ColorF(0.243f, 0.243f, 0.243f, 1.0f); // #3E3E3E

    // Text & Glyphs
    D2D1_COLOR_F textPrimary     = D2D1::ColorF(0.85f, 0.85f, 0.85f, 1.0f);    // #D8D8D8 (High legibility)
    D2D1_COLOR_F textSecondary   = D2D1::ColorF(0.58f, 0.58f, 0.58f, 1.0f);    // #949494
    D2D1_COLOR_F textMuted       = D2D1::ColorF(0.40f, 0.40f, 0.40f, 1.0f);    // #666666
    D2D1_COLOR_F textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);       // Pure White

    // Interactive States & Caption Buttons
    D2D1_COLOR_F itemHover       = D2D1::ColorF(0.165f, 0.176f, 0.180f, 1.0f); // #2A2D2E
    D2D1_COLOR_F itemActive      = D2D1::ColorF(0.216f, 0.216f, 0.239f, 1.0f); // #37373D
    D2D1_COLOR_F captionHover    = D2D1::ColorF(0.200f, 0.200f, 0.200f, 1.0f); // #333333
    D2D1_COLOR_F closeHoverBg    = D2D1::ColorF(0.910f, 0.067f, 0.137f, 1.0f); // #E81123 (Vibrant Red Close Hover)

    // Brand & Accent
    D2D1_COLOR_F brandAccent     = D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f);   // #0078D4 (Accent Blue)
    D2D1_COLOR_F homeBadgeBg     = D2D1::ColorF(0.165f, 0.176f, 0.180f, 1.0f); // #2A2D2E

    // Customized Tooltip Colors
    D2D1_COLOR_F tooltipBg       = D2D1::ColorF(0.145f, 0.145f, 0.149f, 0.98f); // #252526
    D2D1_COLOR_F tooltipBorder   = D2D1::ColorF(0.243f, 0.243f, 0.243f, 1.0f);  // #3E3E3E
};

/**
 * @class Theme
 * @brief Manages Direct2D solid brushes and DirectWrite typography formats.
 */
class Theme {
public:
    Theme();
    ~Theme() = default;

    /**
     * @brief Initializes GPU brushes on the given Direct2D render target.
     * @param pRT Active Direct2D render target.
     * @param pDWriteFactory DirectWrite factory pointer for text formats.
     * @return HRESULT S_OK on success.
     */
    HRESULT CreateDeviceResources(ID2D1RenderTarget* pRT, IDWriteFactory* pDWriteFactory);

    /**
     * @brief Releases device-dependent brushes when render target is recreated.
     */
    void DiscardDeviceResources();

    // Raw color definitions
    ThemeColors colors;

    // Solid Brushes
    ComPtr<ID2D1SolidColorBrush> brushWindowBg;
    ComPtr<ID2D1SolidColorBrush> brushHeaderBg;
    ComPtr<ID2D1SolidColorBrush> brushRailBg;
    ComPtr<ID2D1SolidColorBrush> brushDrawerBg;
    ComPtr<ID2D1SolidColorBrush> brushBorderSubtle;
    ComPtr<ID2D1SolidColorBrush> brushBorderHover;
    ComPtr<ID2D1SolidColorBrush> brushTextPrimary;
    ComPtr<ID2D1SolidColorBrush> brushTextSecondary;
    ComPtr<ID2D1SolidColorBrush> brushTextMuted;
    ComPtr<ID2D1SolidColorBrush> brushTextWhite;
    ComPtr<ID2D1SolidColorBrush> brushItemHover;
    ComPtr<ID2D1SolidColorBrush> brushItemActive;
    ComPtr<ID2D1SolidColorBrush> brushCaptionHover;
    ComPtr<ID2D1SolidColorBrush> brushCloseHoverBg;
    ComPtr<ID2D1SolidColorBrush> brushBrandAccent;
    ComPtr<ID2D1SolidColorBrush> brushHomeBadgeBg;
    ComPtr<ID2D1SolidColorBrush> brushTooltipBg;
    ComPtr<ID2D1SolidColorBrush> brushTooltipBorder;

    // DirectWrite Text Formats
    ComPtr<IDWriteTextFormat> fontTitle;    ///< 12px Semi-bold for title
    ComPtr<IDWriteTextFormat> fontMenu;     ///< 12px Regular for top menus
    ComPtr<IDWriteTextFormat> fontBody;     ///< 13px Regular for UI elements
    ComPtr<IDWriteTextFormat> fontIcon;     ///< 14px Symbol font for glyphs
    ComPtr<IDWriteTextFormat> fontCaption;  ///< 12px Caption button font
    ComPtr<IDWriteTextFormat> fontHint;     ///< 13px Italic/Muted for workspace hints
    ComPtr<IDWriteTextFormat> fontTooltip;  ///< 11.5px Crisp font for customized sidebar tooltips
};

} // namespace phide::ui
