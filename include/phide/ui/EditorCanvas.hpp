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
#include "phide/editor/BracketPairAnalyzer.hpp"
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
     * @brief Renders the entire editor canvas (multi-tabs, active document, split editor side-by-side panes, or placeholder watermark).
     * @param highlightQuery Search query string to highlight across visible lines.
     * @param highlightMatchCase Whether search query match is case-sensitive.
     * @param highlightWholeWord Whether search query matches whole words only.
     * @param highlightUseRegex Whether search query is treated as a regular expression.
     * @param isSplitView Whether side-by-side split editor mode is active (Image 4).
     * @param splitDocIdx Document index displayed in the secondary (right) split pane.
     * @param focusedPane Active pane receiving keyboard input (0 = Left, 1 = Right).
     */
    /**
     * @brief Renders the entire editor canvas supporting multi-column editor groups.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme.
     * @param config Application config.
     * @param groups List of active side-by-side editor groups.
     * @param focusedGroupIdx 0-based index of the currently focused editor group.
     * @param canvasLeft X coordinate where the editor canvas begins.
     * @param canvasWidth Total width allocated for all editor groups.
     * @param clientHeight Total client area height.
     * @param hasLeftDrawer True if sidebar drawer is open.
     * @param highlightQuery Search query for occurrences.
     * @param highlightMatchCase Case sensitive.
     * @param highlightWholeWord Whole word.
     * @param highlightUseRegex Regex.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                std::vector<editor::EditorGroup>& groups, int focusedGroupIdx,
                float canvasLeft, float canvasWidth, float clientHeight,
                bool hasLeftDrawer = false,
                const std::wstring& highlightQuery = L"",
                bool highlightMatchCase = false,
                bool highlightWholeWord = false,
                bool highlightUseRegex = false,
                bool isRenaming = false);

    /**
     * @brief Convenience overload for single-pane callers and unit tests.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                float canvasLeft, float canvasWidth, float clientHeight,
                bool hasLeftDrawer = false,
                const std::wstring& highlightQuery = L"",
                bool highlightMatchCase = false,
                bool highlightWholeWord = false,
                bool highlightUseRegex = false,
                bool isRenaming = false) {
        editor::EditorGroup g;
        g.docs = docs;
        g.activeDocIdx = activeDocIdx;
        std::vector<editor::EditorGroup> groups;
        groups.push_back(std::move(g));
        Render(pRT, theme, config, groups, 0, canvasLeft, canvasWidth, clientHeight, hasLeftDrawer,
               highlightQuery, highlightMatchCase, highlightWholeWord, highlightUseRegex, isRenaming);
        docs = std::move(groups[0].docs);
    }

    /**
     * @brief Displays the floating refactor action / warning popup matching Image 5.
     * @param message Text to display (e.g. L"No refactorings available").
     */
    void ShowRefactorPopup(const std::wstring& message = L"No refactorings available") {
        m_showRefactorPopup = true;
        m_refactorMessage = message;
    }
    void DismissRefactorPopup() noexcept { m_showRefactorPopup = false; }
    [[nodiscard]] bool IsRefactorPopupVisible() const noexcept { return m_showRefactorPopup; }

    /**
     * @brief Toggles caret blink phase.
     */
    void ToggleCaretBlink() noexcept { m_caretVisible = !m_caretVisible; }

    /**
     * @brief Tests if mouse clicked on any tab header.
     * @param pt Mouse point in client coordinates.
     * @param docs Collection of open documents in the target pane.
     * @return Tab index (0-based) or -1 if no tab hit.
     */
    [[nodiscard]] int HitTestTab(POINT pt, const std::vector<editor::OpenDocument>& docs) const;

    /**
     * @brief Tests if mouse clicked any tab's close button (×).
     * @param pt Mouse point in client coordinates.
     * @param docs Collection of open documents in the target pane.
     * @return Tab index (0-based) or -1 if no close button hit.
     */
    [[nodiscard]] int HitTestTabClose(POINT pt, const std::vector<editor::OpenDocument>& docs) const;

    /**
     * @brief Tests if mouse clicked on the vertical scrollbar track or thumb.
     * @param pt Mouse point in client coordinates.
     * @param doc Active document containing scrollbar geometry.
     * @param outHitThumb Set to true if click was directly over the thumb pill.
     * @return True if scrollbar was clicked.
     */
    [[nodiscard]] bool HitTestScrollbar(POINT pt, const editor::OpenDocument& doc, bool& outHitThumb) const;

    /**
     * @brief Tests if mouse clicked on the code minimap track or viewport slider.
     * @param pt Mouse point in client coordinates.
     * @param doc Active document containing minimap geometry.
     * @param outHitSlider Set to true if click was directly over the minimap slider box.
     * @return True if minimap was clicked.
     */
    [[nodiscard]] bool HitTestMinimap(POINT pt, const editor::OpenDocument& doc, bool& outHitSlider) const;

    /**
     * @brief Tests if mouse clicked on a code folding chevron in the line number gutter.
     * @param pt Mouse point in client coordinates.
     * @return Buffer line index (0-based) of the fold toggle, or -1 if none hit.
     */
    [[nodiscard]] int HitTestFoldChevron(POINT pt) const;

    /**
     * @brief Tests if mouse clicked inside the code area to position cursor.
     */
    bool HitTestCodeArea(POINT pt, const app::AppConfig& config, const editor::OpenDocument& doc,
                         float canvasLeft, float clientHeight, size_t& outLine, size_t& outCol) const;

    /**
     * @brief Computes whether a buffer line can be folded and returns its terminating line.
     * @param buffer Active text buffer.
     * @param startLine 0-based buffer line index to check.
     * @param outEndLine Output line where the foldable region ends.
     * @return True if startLine begins a foldable block.
     */
    static bool GetFoldRange(const editor::Buffer& buffer, size_t startLine, size_t& outEndLine);

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
    mutable float m_charWidth = 0.0f;
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
                    bool hasLeftDrawer, bool isFocused);

    /**
     * @brief Renders the breadcrumbs navigation bar displaying file hierarchy and icon (Image 4).
     */
    void RenderBreadcrumbs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           const editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                           float barY, float barHeight);

    /**
     * @brief Renders the native picture viewer for images (PNG, JPG, BMP, GIF, ICO, WEBP) matching Image 2.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark/light brushes.
     * @param config Application metrics.
     * @param doc Open document whose image bitmap and dimensions are displayed.
     * @param canvasLeft Left boundary coordinate of the canvas.
     * @param canvasWidth Width of the canvas.
     * @param topY Vertical starting coordinate below the tab bar and breadcrumbs.
     * @param clientHeight Total client height.
     */
    void RenderImageView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                         float topY, float clientHeight);

    /**
     * @brief Renders the unsupported binary file view (red circled X, center text, subheader) matching Image 2.
     */
    void RenderUnsupportedBinaryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                    float topY, float clientHeight);

    /**
     * @brief Renders placeholder view when a binary/executable file is opened in diff view.
     */
    void RenderDiffBinaryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                              float topY, float clientHeight);

    /**
     * @brief Renders the multi-file commit diff view (matching Image 4) showing all files changed in a commit.
     */
    void RenderMultiFileDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                 float topY, float clientHeight, bool isPaneFocused);

    /**
     * @brief Renders syntax-highlighted tokens for a single line of code with fallback plain text.
     */
    void RenderSyntaxTokensLine(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                const editor::Buffer& buffer, size_t lineIdx,
                                float codeLeft, float lineY, float maxRight,
                                float lineH, float charW, IDWriteTextFormat* pCodeFmt,
                                ID2D1Brush* defaultTextBrush = nullptr);

    /**
     * @brief Renders a single editor pane (tabs, breadcrumbs, content).
     */
    void RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                          float canvasLeft, float canvasWidth, float headerH, float clientHeight,
                          bool hasLeftDrawer, bool isPaneFocused,
                          const std::wstring& highlightQuery = L"",
                          bool highlightMatchCase = false,
                          bool highlightWholeWord = false,
                          bool highlightUseRegex = false,
                          bool isRenaming = false);

    /**
     * @brief Renders active buffer source lines, line gutter numbers, fold chevrons, and syntax highlights.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme for syntax colors.
     * @param config Application metrics.
     * @param doc Open document whose buffer and scroll/fold state are rendered.
     * @param canvasLeft Left boundary coordinate of the canvas.
     * @param canvasWidth Width of the canvas.
     * @param topY Vertical starting coordinate below the tab bar and breadcrumbs.
     * @param clientHeight Total client height.
     */
    void RenderCode(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                    float topY, float clientHeight, bool isFocused,
                    const std::wstring& highlightQuery = L"",
                    bool highlightMatchCase = false,
                    bool highlightWholeWord = false,
                    bool highlightUseRegex = false,
                    bool isRenaming = false);

    /**
     * @brief Renders side-by-side diff comparison view with read-only left pane and editable right pane (Images 2, 3, 4, 5).
     */
    void RenderDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                        editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                        float topY, float clientHeight, bool isFocused);

    /**
     * @brief Renders ambient background theme effects (3D perspective grid, particles, starfields) for unique themes.
     */
    void RenderCanvasThemeEffects(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  float canvasLeft, float canvasWidth, float topY, float clientHeight);

    void RenderRefactorPopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);

    bool m_showRefactorPopup = false;
    std::wstring m_refactorMessage;
    float m_lastCaretX = 0.0f;
    float m_lastCaretY = 0.0f;

    struct FoldChevronHit {
        D2D1_RECT_F bounds{};
        size_t bufferLine = 0;
    };
    std::vector<FoldChevronHit> m_foldChevrons;
};

} // namespace phide::ui
