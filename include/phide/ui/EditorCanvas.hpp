/**
 * @file EditorCanvas.hpp
 * @brief High-performance Direct2D editor canvas, multi-tab bar, line gutter, and syntax rendering.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/ui/KeyboardShortcutsView.hpp"

namespace phide::ui {

/**
 * @class EditorCanvas
 * @brief Renders the editor workspace including multi-tabs, line numbers, syntax-colored lines, and interactive views.
 */
class EditorCanvas {
public:
    EditorCanvas();
    ~EditorCanvas() = default;

    /**
     * @brief Renders the entire editor canvas (multi-tabs, active document or placeholder watermark).
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                float canvasLeft, float canvasWidth, float clientHeight,
                bool hasLeftDrawer = false);

    /**
     * @brief Toggles caret blink phase.
     */
    void ToggleCaretBlink() noexcept { m_caretVisible = !m_caretVisible; }

    /**
     * @brief Tests if mouse clicked on any tab header.
     * @return Tab index (0-based) or -1 if no tab hit.
     */
    [[nodiscard]] int HitTestTab(POINT pt, const std::vector<editor::OpenDocument>& docs) const;

    /**
     * @brief Tests if mouse clicked any tab's close button (×).
     * @return Tab index (0-based) or -1 if no close button hit.
     */
    [[nodiscard]] int HitTestTabClose(POINT pt, const std::vector<editor::OpenDocument>& docs) const;

    /**
     * @brief Tests if mouse clicked inside the code area to position cursor.
     */
    bool HitTestCodeArea(POINT pt, const app::AppConfig& config, const editor::Buffer& buffer,
                         float canvasLeft, float clientHeight, size_t& outLine, size_t& outCol) const;

    /**
     * @brief Accesses the embedded keyboard shortcuts interactive view.
     * @return Reference to the KeyboardShortcutsView component.
     */
    KeyboardShortcutsView& GetKeyboardShortcutsView() noexcept { return m_keybindingsView; }

    /**
     * @brief Releases device-dependent Direct2D resources (e.g. path geometries, watermark bitmaps).
     */
    void DiscardDeviceResources();

private:
    bool m_caretVisible = true;
    KeyboardShortcutsView m_keybindingsView;

    // Cached path geometry for workspace container (curved ONLY at upper-left corner when drawer is closed)
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> m_pCanvasGeometry;
    float m_cachedLeft          = -1.0f;
    float m_cachedTop           = -1.0f;
    float m_cachedRight         = -1.0f;
    float m_cachedBottom        = -1.0f;
    bool  m_cachedHasLeftDrawer = false;

    // Watermark logo in center of canvas (dark mode + light mode inverted)
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_pWatermarkBitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_pWatermarkBitmapLight;
    LucideIcons m_icons;

    /**
     * @brief Renders the empty workspace placeholder with brand watermark and key shortcut hints.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark/light brushes.
     * @param config Application DPI metrics and layout configuration.
     * @param canvasLeft Left coordinate of the canvas.
     * @param canvasWidth Width of the canvas.
     * @param headerH Height of the top title header.
     * @param clientHeight Total client window height.
     */
    void RenderPlaceholder(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           float canvasLeft, float canvasWidth, float headerH, float clientHeight);

    /**
     * @brief Renders the multi-document tab bar with Seti file icons, preview italics, dirty dots, and close buttons.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing tab styling brushes.
     * @param config Application metrics.
     * @param canvasLeft Left boundary coordinate of the canvas.
     * @param canvasWidth Available horizontal width for the tab strip.
     * @param headerH Height offset of the top title bar.
     * @param docs Collection of open documents.
     * @param activeDocIdx Index of the currently active document.
     * @param hasLeftDrawer True if the sidebar drawer is open (influences upper-left tab rounding).
     */
    void RenderTabs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    float canvasLeft, float canvasWidth, float headerH,
                    std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                    bool hasLeftDrawer);

    /**
     * @brief Renders active buffer source lines, line gutter numbers, and syntax highlights.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme for syntax colors.
     * @param config Application metrics.
     * @param buffer In-memory document buffer to render.
     * @param canvasLeft Left boundary coordinate of the canvas.
     * @param canvasWidth Width of the canvas.
     * @param topY Vertical starting coordinate below the tab bar.
     * @param clientHeight Total client height.
     */
    void RenderCode(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    const editor::Buffer& buffer, float canvasLeft, float canvasWidth,
                    float topY, float clientHeight);
};

} // namespace phide::ui
