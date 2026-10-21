/**
 * @file ThemePickerModal.hpp
 * @brief Modal popup dialog for selecting Dark Mode, Light Mode, or Auto (System theme) with connected slice preview.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @class ThemePickerModal
 * @brief Modal dialog presenting visual preview cards for Dark Mode, Light Mode, and Auto.
 */
class ThemePickerModal {
public:
    ThemePickerModal();
    ~ThemePickerModal() = default;

    void Open(ThemeMode currentMode) noexcept {
        m_isOpen = true;
        m_selectedMode = currentMode;
        m_hoveredCard = -1;
        m_hoveredClose = false;
    }

    void Close() noexcept {
        m_isOpen = false;
        m_hoveredCard = -1;
    }

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }
    [[nodiscard]] ThemeMode GetSelectedMode() const noexcept { return m_selectedMode; }

    /**
     * @brief Renders the semi-transparent scrim and centered modal dialog.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float clientW, float clientH);

    /**
     * @brief Handles mouse movement over modal cards and buttons.
     */
    bool HandleMouseMove(POINT clientPt);

    /**
     * @brief Handles mouse click on modal cards or close button.
     * @return Selected ThemeMode, or unchanged.
     */
    bool HandleLButtonDown(POINT clientPt, ThemeMode& outChosenMode);

private:
    bool m_isOpen = false;
    ThemeMode m_selectedMode = ThemeMode::Dark;
    int m_hoveredCard = -1; // 0: Dark, 1: Light, 2: Auto
    bool m_hoveredClose = false;

    D2D1_RECT_F m_modalBounds{};
    D2D1_RECT_F m_closeBounds{};
    D2D1_RECT_F m_cardBounds[3]{};
    D2D1_RECT_F m_previewBoxBounds[3]{};

    void DrawDarkPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, const D2D1_RECT_F& rc);
    void DrawLightPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, const D2D1_RECT_F& rc);
    void DrawAutoSplitPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, const D2D1_RECT_F& rc);
};

} // namespace phide::ui
