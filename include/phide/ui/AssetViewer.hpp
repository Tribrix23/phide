/**
 * @file AssetViewer.hpp
 * @brief Built-in Image, SVG Split Preview, and Font Asset Viewer (#5).
 *
 * Provides:
 *  - Raster Image Inspector (.png, .jpg, .jpeg, .bmp, .gif, .webp, .ico, .tiff):
 *      WIC decoding, bit-depth & pixel-format inspection, transparency checkerboard,
 *      zoom (Fit / 100% / +/-), quantized dominant color swatches with click-to-copy hex,
 *      and 1-click Copy <img> / Copy Base64 Data URI actions.
 *  - SVG Split Preview (.svg):
 *      Live Direct2D SVG rendering (via ID2D1DeviceContext5::CreateSvgDocument with safe
 *      viewport scaling and checkerboard backdrop) synchronized to the live XML text buffer.
 *  - Font Asset Waterfall Viewer (.ttf, .otf, .woff, .woff2):
 *      DirectWrite isolated in-memory font collection loading (no global system font pollution),
 *      SFNT/WOFF metadata extraction, responsive multi-size waterfall (12px-48px),
 *      customizable sample text input, and 1-click Copy @font-face CSS / <link> actions.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::editor {
struct OpenDocument;
}

namespace phide::ui {

/**
 * @struct DominantColorSwatch
 * @brief Represents one quantized dominant color extracted from a raster image.
 */
struct DominantColorSwatch {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint32_t sampleCount = 0;
    std::wstring hexString;   ///< Uppercase "#RRGGBB"
    D2D1_RECT_F bounds{};     ///< Hit-test rectangle for click-to-copy
};

/**
 * @struct AssetViewerState
 * @brief Per-document state and RAII GPU/DirectWrite caches for Image, SVG, and Font previews.
 *
 * Attached to OpenDocument via std::shared_ptr so non-asset documents incur zero memory overhead.
 * All Direct2D / WIC / DirectWrite COM pointers use ComPtr RAII and are released cleanly on
 * device reset or document destruction.
 */
struct AssetViewerState {
    static constexpr uint64_t kMaxBase64FileBytes = 4u * 1024u * 1024u;
    static constexpr uint64_t kMaxFontFileBytes   = 16u * 1024u * 1024u;
    static constexpr size_t   kMaxSvgChars        = 2u * 1024u * 1024u;

    AssetViewerState() = default;
    ~AssetViewerState();

    AssetViewerState(const AssetViewerState&) = delete;
    AssetViewerState& operator=(const AssetViewerState&) = delete;

    // --- Common Metadata & Feedback ---
    std::wstring loadedFilePath;
    uint64_t fileSizeBytes = 0;
    std::wstring toastMessage;
    ULONGLONG toastExpireTick = 0;

    // --- Raster Image State ---
    uint32_t bitsPerPixel = 0;
    std::wstring pixelFormatName;
    bool hasAlpha = false;
    bool imageMetadataLoaded = false;
    bool showCheckerboard = true;
    float imageZoom = 0.0f; ///< 0.0f = Auto Fit to viewport; >0 = explicit zoom factor (e.g. 1.0f = 100%)
    std::vector<DominantColorSwatch> dominantColors;

    // --- SVG Live Split Preview State ---
    uint64_t cachedSvgBufferVersion = UINT64_MAX;
    ID2D1RenderTarget* cachedSvgRT = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SvgDocument> svgDoc;
    float svgIntrinsicW = 256.0f;
    float svgIntrinsicH = 256.0f;
    std::wstring svgParseError;
    bool svgParseAttempted = false;

    // --- Font Waterfall & Specimen Inspector State ---
    struct FontFeatureToggle {
        std::wstring tag;
        uint32_t tagFourCC = 0;
        bool enabled = false;
        D2D1_RECT_F bounds{};
    };
    struct FontGlyphCell {
        uint32_t codepoint = 0;
        std::wstring glyphChar;
        std::wstring hexCode;
        D2D1_RECT_F bounds{};
    };
    enum class FontViewTab {
        Waterfall = 0,
        Glyphs = 1,
        Specimen = 2,
        Specs = 3
    };

    bool fontLoadAttempted = false;
    bool fontRenderSupported = false;
    std::wstring fontFormatLabel;   ///< "TTF", "OTF", "WOFF", "WOFF2"
    std::wstring fontFamilyName;
    std::wstring fontSubfamilyName;
    std::wstring fontVersionStr;
    std::wstring fontManufacturerStr;
    std::wstring fontLicenseStr;
    uint32_t fontNumGlyphs = 0;
    uint16_t fontUnitsPerEm = 1000;
    bool fontIsFixedPitch = false;
    std::wstring fontStatusNotice;
    std::wstring fontSampleText = L"The quick brown fox jumps over the lazy dog 0123456789 !@#$%^&*()";
    bool fontSampleFocused = false;
    FontViewTab fontTab = FontViewTab::Waterfall;
    float fontScrollY = 0.0f;
    float fontMaxScrollY = 0.0f;
    std::vector<FontFeatureToggle> fontOtFeatures;
    std::vector<FontGlyphCell> fontGlyphCells;
    int hoveredGlyphIdx = -1;
    int hoveredOtFeatureIdx = -1;

    // Isolated DirectWrite in-memory collection (never mutates global system fonts)
    Microsoft::WRL::ComPtr<IDWriteFactory5> dwriteFactory5;
    Microsoft::WRL::ComPtr<IDWriteInMemoryFontFileLoader> memFontLoader;
    Microsoft::WRL::ComPtr<IDWriteFontCollection1> customFontCollection;
    Microsoft::WRL::ComPtr<IDWriteFontFace3> customFontFace;
    std::vector<uint8_t> rawFontBytes;

    // --- Interactive Hit Bounds (recomputed every frame cleanly) ---
    D2D1_RECT_F rcToolbar{};
    D2D1_RECT_F rcBtnZoomOut{};
    D2D1_RECT_F rcBtnZoomFit{};
    D2D1_RECT_F rcBtnZoom100{};
    D2D1_RECT_F rcBtnZoomIn{};
    D2D1_RECT_F rcBtnChecker{};
    D2D1_RECT_F rcBtnCopyImgTag{};
    D2D1_RECT_F rcBtnCopyBase64{};
    D2D1_RECT_F rcBtnOpenHex{};
    D2D1_RECT_F rcBtnCopyFontFace{};
    D2D1_RECT_F rcBtnCopyLinkTag{};
    D2D1_RECT_F rcBtnToggleSvgPreview{};
    D2D1_RECT_F rcFontSampleBox{};
    D2D1_RECT_F rcFontTabWaterfall{};
    D2D1_RECT_F rcFontTabGlyphs{};
    D2D1_RECT_F rcFontTabSpecimen{};
    D2D1_RECT_F rcFontTabSpecs{};
    D2D1_RECT_F rcFontBodyViewport{};
    int hoveredButtonId = -1;
    int hoveredSwatchIdx = -1;

    /**
     * @brief Releases Direct2D device-dependent SVG and bitmap resources cleanly.
     */
    void DiscardDeviceResources() noexcept;

    /**
     * @brief Unregisters any DirectWrite in-memory font loader and clears font buffers.
     */
    void ReleaseFontResources() noexcept;
};

/**
 * @class AssetViewerRenderer
 * @brief Stateless renderer & input handler for Raster Image, SVG Split Preview, and Font Waterfall tabs.
 */
class AssetViewerRenderer {
public:
    /**
     * @brief Returns true if `extLower` (with leading dot, e.g. L".ttf") is a font asset extension.
     */
    [[nodiscard]] static bool IsFontExtension(const std::wstring& extLower) noexcept;

    /**
     * @brief Renders an enhanced Raster Image Viewer inside the active editor canvas rect.
     */
    static void RenderRasterImageView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                      phide::editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                      float topY, float clientHeight);

    /**
     * @brief Renders the live SVG Preview half of an SVG Split View from `doc.buffer`.
     */
    static void RenderSvgPreviewPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     phide::editor::OpenDocument& doc, float paneLeft, float paneWidth,
                                     float topY, float clientHeight);

    /**
     * @brief Renders the Font Asset Waterfall & @font-face snippet inspector for .ttf/.otf/.woff/.woff2 files.
     */
    static void RenderFontAssetView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    phide::editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                    float topY, float clientHeight);

    /**
     * @brief Handles mouse click inside Raster Image, SVG Split Preview, or Font Asset Viewer.
     * @return true if the click was handled.
     */
    static bool HandleLButtonDown(POINT clientPt, HWND hwnd, phide::editor::OpenDocument& doc,
                                  const app::AppConfig& config);

    /**
     * @brief Handles mouse move hover updates inside Asset Viewer toolbars/swatches.
     */
    static bool HandleMouseMove(POINT clientPt, phide::editor::OpenDocument& doc);

    /**
     * @brief Handles mouse wheel zoom (images) or waterfall scroll (fonts).
     */
    static bool HandleMouseWheel(short delta, bool isCtrlDown, phide::editor::OpenDocument& doc,
                                 const app::AppConfig& config);

    /**
     * @brief Handles printable character typing inside the Font Waterfall sample text input.
     */
    static bool HandleChar(wchar_t ch, phide::editor::OpenDocument& doc);

    /**
     * @brief Handles Backspace/Escape inside the Font Waterfall sample text input.
     */
    static bool HandleKeyDown(WPARAM vk, phide::editor::OpenDocument& doc);

    /**
     * @brief Returns the appropriate mouse cursor (IDC_HAND / IDC_IBEAM / nullptr) for `clientPt`.
     */
    [[nodiscard]] static LPCWSTR GetCursorForPoint(POINT clientPt, const phide::editor::OpenDocument& doc) noexcept;
};

} // namespace phide::ui
