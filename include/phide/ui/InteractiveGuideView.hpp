/**
 * @file InteractiveGuideView.hpp
 * @brief Interactive feature guide and walkthrough view for Phide IDE.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @struct InteractiveGuideTask
 * @brief Represents an interactive checkbox item in the guide.
 */
struct InteractiveGuideTask {
    std::wstring label;
    bool checked = false;
    D2D1_RECT_F boxBounds{};
};

/**
 * @class InteractiveGuideView
 * @brief Surface rendering the interactive feature guide with code boxes, swatches, and live controls.
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
     * @brief Handles mouse clicks (e.g. toggling interactive checkboxes).
     */
    bool HandleLButtonDown(POINT clientPt);

    /**
     * @brief Handles mouse wheel scrolling with boundary clamping.
     */
    bool HandleMouseWheel(short delta, float viewHeight);

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

    std::vector<InteractiveGuideTask> m_tasks;
    int m_hoveredTask = -1;

    void DrawKeycapBadge(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         const std::wstring& text, float& curX, float centerY, float badgeH);

    void DrawCodeLine(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                      float x, float y, int lineNum, const std::wstring& code,
                      const std::vector<std::pair<std::wstring, D2D1_COLOR_F>>& tokens,
                      const D2D1_COLOR_F* pSwatchColor = nullptr,
                      const std::wstring* pGhostText = nullptr);

    void DrawCalloutBox(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                        float x, float y, float width, const std::wstring& title, const std::wstring& text,
                        float& outHeight);
};

} // namespace phide::ui
