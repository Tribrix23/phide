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
#include <dwrite_3.h>
#include <wrl/client.h>
#include <string>

namespace phide::ui {

using Microsoft::WRL::ComPtr;

/**
 * @enum ThemeMode
 * @brief Theme appearance mode: Dark, Light, or Auto (synchronized with Windows system theme).
 */
enum class ThemeMode {
    Dark,
    Light,
    Auto
};

/**
 * @struct ThemeColors
 * @brief Professional IDE color palette (VS Code / JetBrains inspired).
 */
struct ThemeColors {
    // Window & Canvas Backgrounds
    D2D1_COLOR_F windowBg        = D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f);   // #3D1461 (Shell Glass Purple Base)
    D2D1_COLOR_F canvasBg        = D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f); // #1E1E1E (Dark IDE Workspace)
    D2D1_COLOR_F headerBg        = D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f);   // #3D1461 (Glass Purple)
    D2D1_COLOR_F railBg          = D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f);   // #3D1461 (Glass Purple)
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
    D2D1_COLOR_F runGreen        = D2D1::ColorF(0.24f, 0.84f, 0.52f, 1.0f);   // #3DD684 (Vibrant Emerald / IDE Run Green)
    D2D1_COLOR_F runGreenHover   = D2D1::ColorF(0.24f, 0.84f, 0.52f, 0.16f);  // Soft green hover glow

    // Customized Tooltip Colors
    D2D1_COLOR_F tooltipBg       = D2D1::ColorF(0.145f, 0.145f, 0.149f, 0.98f); // #252526
    D2D1_COLOR_F tooltipBorder   = D2D1::ColorF(0.243f, 0.243f, 0.243f, 1.0f);  // #3E3E3E

    // Dropdown / Popup Menu Colors
    D2D1_COLOR_F menuBg          = D2D1::ColorF(0.122f, 0.122f, 0.122f, 0.98f); // #1F1F1F
    D2D1_COLOR_F menuBorder      = D2D1::ColorF(0.271f, 0.271f, 0.271f, 1.0f);  // #454545
    D2D1_COLOR_F menuSeparator   = D2D1::ColorF(0.235f, 0.235f, 0.235f, 1.0f);  // #3C3C3C
    D2D1_COLOR_F menuHover       = D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f);   // #0078D4 (VS Code Selection Blue)
    D2D1_COLOR_F menuText        = D2D1::ColorF(0.800f, 0.800f, 0.800f, 1.0f);  // #CCCCCC
    D2D1_COLOR_F menuShortcut    = D2D1::ColorF(0.550f, 0.550f, 0.550f, 1.0f);  // #8C8C8C

    // Syntax Highlighting & Code Surface Colors
    D2D1_COLOR_F syntaxKeyword   = D2D1::ColorF(0.773f, 0.525f, 0.753f, 1.0f);  // #C586C0 (VS Code Keyword Purple)
    D2D1_COLOR_F syntaxType      = D2D1::ColorF(0.306f, 0.788f, 0.690f, 1.0f);  // #4EC9B0 (Turquoise)
    D2D1_COLOR_F syntaxString    = D2D1::ColorF(0.808f, 0.569f, 0.471f, 1.0f);  // #CE9178 (Orange-Brown)
    D2D1_COLOR_F syntaxComment   = D2D1::ColorF(0.416f, 0.600f, 0.333f, 1.0f);  // #6A9955 (Green)
    D2D1_COLOR_F syntaxNumber    = D2D1::ColorF(0.710f, 0.808f, 0.659f, 1.0f);  // #B5CEA8 (Light Mint)
    D2D1_COLOR_F syntaxOperator  = D2D1::ColorF(0.831f, 0.831f, 0.831f, 1.0f);  // #D4D4D4 (Light Neutral)
    D2D1_COLOR_F lineNumber      = D2D1::ColorF(0.522f, 0.522f, 0.522f, 1.0f);  // #858585 (Gutter Line Numbers)
    D2D1_COLOR_F currentLineBg   = D2D1::ColorF(0.145f, 0.145f, 0.145f, 1.0f);  // #252525 (Current Line Highlight)
    D2D1_COLOR_F tabBarBg        = D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f);  // #181818 (Tab Bar Surface)
    D2D1_COLOR_F tabActiveBg     = D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f);  // #1E1E1E (Active Tab Surface)
    D2D1_COLOR_F tabBorder       = D2D1::ColorF(0.173f, 0.173f, 0.173f, 1.0f);  // #2C2C2C
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

    /**
     * @brief Sets the active theme mode and updates the color palette.
     */
    void SetThemeMode(ThemeMode mode);

    [[nodiscard]] ThemeMode GetThemeMode() const noexcept { return m_mode; }
    [[nodiscard]] bool IsDarkMode() const noexcept { return m_isDarkActive; }

    // Raw color definitions
    ThemeColors colors;

    // Solid Brushes
    ComPtr<ID2D1SolidColorBrush> brushWindowBg;
    ComPtr<ID2D1SolidColorBrush> brushCanvasBg;
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
    ComPtr<ID2D1SolidColorBrush> brushRunGreen;
    ComPtr<ID2D1SolidColorBrush> brushRunGreenHover;
    ComPtr<ID2D1SolidColorBrush> brushTooltipBg;
    ComPtr<ID2D1SolidColorBrush> brushTooltipBorder;
    ComPtr<ID2D1SolidColorBrush> brushMenuBg;
    ComPtr<ID2D1SolidColorBrush> brushMenuBorder;
    ComPtr<ID2D1SolidColorBrush> brushMenuSeparator;
    ComPtr<ID2D1SolidColorBrush> brushMenuHover;
    ComPtr<ID2D1SolidColorBrush> brushMenuText;
    ComPtr<ID2D1SolidColorBrush> brushMenuShortcut;
    ComPtr<ID2D1SolidColorBrush> brushSyntaxKeyword;
    ComPtr<ID2D1SolidColorBrush> brushSyntaxType;
    ComPtr<ID2D1SolidColorBrush> brushSyntaxString;
    ComPtr<ID2D1SolidColorBrush> brushSyntaxComment;
    ComPtr<ID2D1SolidColorBrush> brushSyntaxNumber;
    ComPtr<ID2D1SolidColorBrush> brushSyntaxOperator;
    ComPtr<ID2D1SolidColorBrush> brushLineNumber;
    ComPtr<ID2D1SolidColorBrush> brushCurrentLineBg;
    ComPtr<ID2D1SolidColorBrush> brushTabBarBg;
    ComPtr<ID2D1SolidColorBrush> brushTabActiveBg;
    ComPtr<ID2D1SolidColorBrush> brushTabBorder;

    // Colorful File & Folder Tree Icon Brushes
    ComPtr<ID2D1SolidColorBrush> brushIconBlue;
    ComPtr<ID2D1SolidColorBrush> brushIconPurple;
    ComPtr<ID2D1SolidColorBrush> brushIconYellow;
    ComPtr<ID2D1SolidColorBrush> brushIconGreen;
    ComPtr<ID2D1SolidColorBrush> brushIconRed;
    ComPtr<ID2D1SolidColorBrush> brushIconOrange;
    ComPtr<ID2D1SolidColorBrush> brushIconFolder;
    ComPtr<ID2D1SolidColorBrush> brushIconFolderOpen;
    ComPtr<ID2D1SolidColorBrush> brushIconGray;

    // Seti Official Brand Palette Brushes
    ComPtr<ID2D1SolidColorBrush> brushSetiPhp;       ///< PHP (#777BB4 / #8892BF)
    ComPtr<ID2D1SolidColorBrush> brushSetiJs;        ///< JavaScript (#F7DF1E)
    ComPtr<ID2D1SolidColorBrush> brushSetiTs;        ///< TypeScript (#519ABA)
    ComPtr<ID2D1SolidColorBrush> brushSetiCpp;       ///< C / C++ (#519ABA)
    ComPtr<ID2D1SolidColorBrush> brushSetiJson;      ///< JSON (#CBCB41)
    ComPtr<ID2D1SolidColorBrush> brushSetiHtml;      ///< HTML5 (#E34C26)
    ComPtr<ID2D1SolidColorBrush> brushSetiCss;       ///< CSS3 (#42A5F5)
    ComPtr<ID2D1SolidColorBrush> brushSetiPython;    ///< Python (#3572A5)
    ComPtr<ID2D1SolidColorBrush> brushSetiRust;      ///< Rust (#DEA584)
    ComPtr<ID2D1SolidColorBrush> brushSetiJava;      ///< Java (#CC3E44)
    ComPtr<ID2D1SolidColorBrush> brushSetiGit;       ///< Git (#F14E32)
    ComPtr<ID2D1SolidColorBrush> brushSetiShell;     ///< Terminal / Shell / Make (#4D9375)
    ComPtr<ID2D1SolidColorBrush> brushSetiYaml;      ///< YAML (#CB171E)
    ComPtr<ID2D1SolidColorBrush> brushSetiImage;     ///< Images / SVG (#8A52BE)
    ComPtr<ID2D1SolidColorBrush> brushSetiFolder;    ///< Folder Closed (#7998B5)
    ComPtr<ID2D1SolidColorBrush> brushSetiFolderOpen;///< Folder Open (#8CAECF)
    ComPtr<ID2D1SolidColorBrush> brushSetiDefault;   ///< Default Document (#9E9E9E)

    // DirectWrite Text Formats and Factory
    ComPtr<IDWriteFactory> dwriteFactory;
    ComPtr<IDWriteFontCollection1> customFontCollection; ///< Process font collection for Seti font
    ComPtr<IDWriteTextFormat> fontSeti;          ///< 14.5px Centered Seti UI icon font format
    ComPtr<IDWriteTextFormat> fontTitle;         ///< 12px Semi-bold for title
    ComPtr<IDWriteTextFormat> fontMenu;          ///< 12px Regular for top menus
    ComPtr<IDWriteTextFormat> fontMenuItem;      ///< 12px Regular Left-aligned for dropdown items
    ComPtr<IDWriteTextFormat> fontMenuShortcut;  ///< 11.5px Regular Right-aligned for shortcuts
    ComPtr<IDWriteTextFormat> fontBody;          ///< 13px Regular for UI elements
    ComPtr<IDWriteTextFormat> fontButtonCenter;  ///< 13px Centered for buttons
    ComPtr<IDWriteTextFormat> fontBadge;         ///< 8.5px Bold Centered for file badges (PHP, JS, TS, C++)
    ComPtr<IDWriteTextFormat> fontCode;          ///< 13px Consolas Monospace for code lines
    ComPtr<IDWriteTextFormat> fontSectionHeader; ///< 11px Semi-bold Leading for section titles (EXPLORER)
    ComPtr<IDWriteTextFormat> fontIcon;          ///< 14px Symbol font for glyphs
    ComPtr<IDWriteTextFormat> fontCaption;       ///< 12px Caption button font
    ComPtr<IDWriteTextFormat> fontHint;          ///< 13px Italic/Muted for workspace hints
    ComPtr<IDWriteTextFormat> fontTooltip;       ///< 11.5px Crisp font for customized sidebar tooltips
    ComPtr<IDWriteTextFormat> fontTab;           ///< 12px Regular Leading-aligned for editor tabs
    ComPtr<IDWriteTextFormat> fontTabPreview;    ///< 12px Italic Leading-aligned for preview tabs

    struct SetiIconInfo {
        wchar_t glyph = 0xE023;
        ID2D1Brush* pBrush = nullptr;
    };

    /**
     * @brief Maps a filename and folder state to an authentic Seti UI icon glyph & brand brush.
     */
    [[nodiscard]] SetiIconInfo GetSetiIcon(const std::wstring& filename, bool isFolder, bool isExpanded) const;

private:
    ThemeMode m_mode = ThemeMode::Dark;
    bool m_isDarkActive = true;
};

} // namespace phide::ui
