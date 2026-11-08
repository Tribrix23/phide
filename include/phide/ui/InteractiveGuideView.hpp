/**
 * @file InteractiveGuideView.hpp
 * @brief Interactive feature guide with embedded live editable code blocks for Phide IDE.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/IntelliSenseEngine.hpp"

namespace phide::ui {

/**
 * @struct GuideEditorBlock
 * @brief Represents an embedded live editable mini-editor within the guide.
 */
struct GuideEditorBlock {
    int id = 0;
    std::wstring languageExt; // L".css", L".js", L".php"
    editor::OpenDocument doc;
    D2D1_RECT_F bounds{};
    D2D1_RECT_F codeAreaBounds{};
    D2D1_RECT_F gutterBounds{};
    bool isFocused = false;

    // Real IntelliSense state (Editor 1)
    bool showIntelliSense = false;
    std::vector<editor::IntelliSenseItem> intelliSenseItems;
    size_t intelliSenseSelectedIdx = 0;
    size_t intelliSenseScrollOffset = 0;
    D2D1_RECT_F intelliSensePopupBounds{};

    // Predictive ghost text state (Editor 2)
    std::wstring ghostText;
    size_t ghostLine = static_cast<size_t>(-1);
    size_t ghostCol = static_cast<size_t>(-1);
};

/**
 * @class InteractiveGuideView
 * @brief Surface rendering the interactive feature guide with real embedded mini-editors and live controls.
 */
class InteractiveGuideView {
public:
    InteractiveGuideView();
    ~InteractiveGuideView() = default;

    /**
     * @brief Renders the complete interactive guide surface.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float left, float width, float top, float height);

    /**
     * @brief Handles mouse movements and hover detection.
     */
    bool HandleMouseMove(POINT clientPt);

    /**
     * @brief Handles mouse clicks inside the view or embedded editors.
     */
    bool HandleLButtonDown(POINT clientPt, bool altPressed = false);

    /**
     * @brief Handles mouse up (completes selection drag).
     */
    bool HandleLButtonUp(POINT clientPt);

    /**
     * @brief Handles character typing in the focused embedded editor.
     */
    bool HandleChar(wchar_t ch);

    /**
     * @brief Handles special keys (Backspace, Delete, Enter, Arrows, Ctrl+Space, Tab, Esc).
     */
    bool HandleKeyDown(WPARAM key);

    /**
     * @brief Handles mouse wheel scrolling with boundary clamping.
     */
    bool HandleMouseWheel(short delta, float viewHeight);

    /**
     * @brief Toggles caret blink phase for active editor cursors.
     */
    void ToggleCaretBlink() noexcept { m_caretBlink = !m_caretBlink; }

    /**
     * @brief Determines the appropriate cursor icon for mouse coordinates.
     */
    [[nodiscard]] LPCWSTR GetCursorForPoint(POINT pt) const;

    /**
     * @brief Resets the vertical scroll position back to top.
     */
    void ResetScroll() noexcept { m_scrollY = 0.0f; }

private:
    float m_scrollY = 0.0f;
    float m_maxScrollY = 0.0f;
    float m_contentTotalHeight = 0.0f;
    D2D1_RECT_F m_viewBounds{};
    D2D1_RECT_F m_scrollTrackBounds{};
    D2D1_RECT_F m_scrollThumbBounds{};

    std::vector<GuideEditorBlock> m_editors; // 0: CSS, 1: JS, 2: PHP, 3: Tasks
    int m_focusedEditorIdx = -1;
    bool m_caretBlink = true;
    mutable float m_cachedCharWidth = 0.0f;

    editor::IntelliSenseEngine m_intelliSense;

    void InitializeEditors();
    void UpdateIntelliSense(GuideEditorBlock& ed);
    void ApplyCompletion(GuideEditorBlock& ed, size_t idx);

    void DrawKeycapBadge(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         const std::wstring& text, float& curX, float centerY, float badgeH);

    void DrawCalloutBox(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                        float x, float y, float width, const std::wstring& title, const std::wstring& text,
                        float& outHeight);

    void RenderEditorBlock(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           GuideEditorBlock& ed, float x, float y, float width, float& outHeight);
};

} // namespace phide::ui
