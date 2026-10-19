/**
 * @file EditorCanvas.hpp
 * @brief High-performance Direct2D editor canvas, tab bar, line gutter, and syntax rendering.
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
#include "phide/editor/Buffer.hpp"

namespace phide::ui {

/**
 * @struct EditorTab
 * @brief Represents an open buffer tab.
 */
struct EditorTab {
    std::wstring title;
    bool isActive = false;
    D2D1_RECT_F bounds{};
};

/**
 * @class EditorCanvas
 * @brief Renders the editor workspace including tabs, line numbers, and syntax-colored lines.
 */
class EditorCanvas {
public:
    EditorCanvas();
    ~EditorCanvas() = default;

    /**
     * @brief Renders the entire editor canvas.
     * @param pRT Active Direct2D render target.
     * @param theme Theme brushes and fonts.
     * @param config Application configuration.
     * @param buffer Active text buffer.
     * @param canvasLeft Left coordinate of the visible editor surface.
     * @param canvasWidth Available width.
     * @param clientHeight Window client height.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                const editor::Buffer& buffer, float canvasLeft, float canvasWidth, float clientHeight);

    /**
     * @brief Toggles caret blink phase.
     */
    void ToggleCaretBlink() noexcept { m_caretVisible = !m_caretVisible; }

private:
    std::vector<EditorTab> m_tabs;
    bool m_caretVisible = true;

    // Cached path geometry for workspace container (curved ONLY at upper-left corner)
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> m_pCanvasGeometry;
    float m_cachedLeft   = -1.0f;
    float m_cachedTop    = -1.0f;
    float m_cachedRight  = -1.0f;
    float m_cachedBottom = -1.0f;

    void RenderTabs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    float canvasLeft, float canvasWidth, float headerH);

    void RenderCode(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    const editor::Buffer& buffer, float canvasLeft, float canvasWidth,
                    float topY, float clientHeight);
};

} // namespace phide::ui
