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

    /**
     * @brief Displays the theme picker modal dialog and initializes selection.
     * @param currentMode The currently active ThemeMode.
     */
    void Open(ThemeMode currentMode) noexcept {
        m_isOpen = true;
        m_selectedMode = currentMode;
        m_hoveredCard = -1;
        m_hoveredClose = false;
    }

    /**
     * @brief Dismisses the theme picker modal dialog.
     */
    void Close() noexcept {
        m_isOpen = false;
        m_hoveredCard = -1;
    }

    /**
     * @brief Checks if the modal dialog is currently visible.
     * @return True if open.
     */
    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    /**
     * @brief Gets the currently selected theme mode.
     * @return Active ThemeMode.
     */
    [[nodiscard]] ThemeMode GetSelectedMode() const noexcept { return m_selectedMode; }

    /**
     * @brief Renders the semi-transparent scrim and centered modal dialog.
     * @param pRT Active Direct2D render target.
     * @param theme Current visual theme.
     * @param config Application DPI metrics.
     * @param clientW Client window width.
     * @param clientH Client window height.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float clientW, float clientH);

    /**
     * @brief Handles mouse movement over modal cards and buttons.
     * @param clientPt Mouse cursor position in client coordinates.
     * @return True if hover state changed and repaint is required.
     */
    bool HandleMouseMove(POINT clientPt);

    /**
     * @brief Handles mouse click on modal cards or close button.
     * @param clientPt Mouse cursor position in client coordinates.
     * @param outChosenMode Receives the newly chosen ThemeMode ONLY when a card is clicked.
     * @return True if a theme card was selected, false if dialog was dismissed without changes.
     */
    bool HandleLButtonDown(POINT clientPt, ThemeMode& outChosenMode);

private:
    bool m_isOpen = false;
    ThemeMode m_selectedMode = ThemeMode::Dark;
    int m_hoveredCard = -1; // 0: Dark, 1: Light, 2: Auto
    bool m_hoveredClose = false;

    D2D1_RECT_F m_modalBounds{};
    D2D1_RECT_F m_closeBounds{};
    D2D1_RECT_F m_cardBounds[8]{};
    D2D1_RECT_F m_previewBoxBounds[8]{};

    /**
     * @brief Renders an authentic miniature preview illustration of the Dark Mode IDE layout.
     * @param pRT Active Direct2D render target.
     * @param config Application metrics.
     * @param rc Bounding box of the preview box.
     */
    void DrawDarkPreview(ID2D1RenderTarget* pRT, const app::AppConfig& config, const D2D1_RECT_F& rc);

    /**
     * @brief Renders an authentic miniature preview illustration of the Light Mode IDE layout.
     * @param pRT Active Direct2D render target.
     * @param config Application metrics.
     * @param rc Bounding box of the preview box.
     */
    void DrawLightPreview(ID2D1RenderTarget* pRT, const app::AppConfig& config, const D2D1_RECT_F& rc);

    /**
     * @brief Renders an authentic diagonally/vertically sliced connected preview (left half Light, right half Dark).
     * @param pRT Active Direct2D render target.
     * @param config Application metrics.
     * @param rc Bounding box of the preview box.
     */
    void DrawAutoSplitPreview(ID2D1RenderTarget* pRT, const app::AppConfig& config, const D2D1_RECT_F& rc);

    /**
     * @brief Renders a customized miniature preview illustration for any color theme palette.
     * @param pRT Active Direct2D render target.
     * @param config Application metrics.
     * @param rc Bounding box of the preview box.
     */
    void DrawGenericPreview(ID2D1RenderTarget* pRT, const app::AppConfig& config, const D2D1_RECT_F& rc,
                            D2D1_COLOR_F canvasCol, D2D1_COLOR_F headerCol, D2D1_COLOR_F drawerCol,
                            D2D1_COLOR_F tabBarCol, D2D1_COLOR_F tabActiveCol, D2D1_COLOR_F accentCol,
                            D2D1_COLOR_F textCol, D2D1_COLOR_F kwCol, D2D1_COLOR_F typeCol,
                            D2D1_COLOR_F strCol, D2D1_COLOR_F commentCol, D2D1_COLOR_F lineNumCol,
                            D2D1_COLOR_F borderCol);
};

} // namespace phide::ui
