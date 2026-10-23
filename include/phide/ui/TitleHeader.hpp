/**
 * @file TitleHeader.hpp
 * @brief Dark IDE frameless header containing menu bar, title, and Lucide vector caption buttons.
 * 
 * Features crisp vector-drawn window controls (minimize, maximize, close) with red hover
 * background on the Close button, seamless title dragging, and no horizontal divider line.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

/**
 * @enum HeaderMenuId
 * @brief Identifies menu bar items in the header.
 */
enum class HeaderMenuId {
    None = -1,
    Sidebar = 0,
    File = 1,
    Edit = 2,
    View = 3,
    Debug = 4,
    Code = 5,
    Tools = 6,
    Window = 7,
    Help = 8
};

/**
 * @enum HeaderExecId
 * @brief Identifies top-right execution widget buttons (Run, Debug, Terminal).
 */
enum class HeaderExecId {
    None = -1,
    Run = 0,        ///< Run / Execute application (vibrant green)
    Debug = 1,      ///< Debug application (vibrant green)
    Terminal = 2    ///< Terminal toggle (neutral/white text, explicitly not green)
};

/**
 * @enum CaptionButtonType
 * @brief Identifies top-right window control buttons.
 */
enum class CaptionButtonType {
    None,
    Minimize,   ///< Minimize window to taskbar
    Maximize,   ///< Maximize / Restore window
    Close       ///< Close / Exit application
};

/**
 * @struct MenuItem
 * @brief Visual descriptor for a top menu item.
 */
struct MenuItem {
    HeaderMenuId id;
    const wchar_t* label;
    D2D1_RECT_F bounds{};
};

/**
 * @struct ExecButton
 * @brief Visual descriptor for a top-right execution action button.
 */
struct ExecButton {
    HeaderExecId id;
    const wchar_t* label;
    D2D1_RECT_F bounds{};
};

/**
 * @class TitleHeader
 * @brief Renders the dark IDE title bar, menus, and top-right caption controls.
 */
class TitleHeader {
public:
    TitleHeader();
    ~TitleHeader() = default;

    /**
     * @brief Releases device-dependent Direct2D resources.
     */
    void DiscardDeviceResources() {
        m_pHeaderGradientBrush.Reset();
        m_lastWidth = -1.0f;
    }

    /**
     * @brief Paints the header background, menus, centered title, and caption controls.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark brushes and text formats.
     * @param config Application configuration and DPI metrics.
     * @param clientWidth Width of the client window in physical pixels.
     * @param isZoomed True if window is currently maximized.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float clientWidth, bool isZoomed);

    /**
     * @brief Tests if mouse is over a menu item.
     * @param pt Mouse point in client coordinates.
     * @return HeaderMenuId or HeaderMenuId::None.
     */
    HeaderMenuId HitTestMenu(POINT pt) const;

    /**
     * @brief Tests if mouse is over an execution button (Run, Debug, Terminal).
     * @param pt Mouse point in client coordinates.
     * @return HeaderExecId or HeaderExecId::None.
     */
    HeaderExecId HitTestExecButton(POINT pt) const;

    /**
     * @brief Tests if mouse is over a caption button (min, max, close).
     * @param pt Mouse point in client coordinates.
     * @return CaptionButtonType under the cursor.
     */
    CaptionButtonType HitTestCaptionButton(POINT pt) const;

    /**
     * @brief Determines whether the cursor is in a draggable title bar zone or over interactive buttons.
     * @param pt Mouse point in client coordinates.
     * @param config Application configuration.
     * @return LRESULT HTCAPTION if draggable, HTCLIENT if over a button.
     */
    LRESULT HitTestNCHitTest(POINT pt, const app::AppConfig& config);

    /**
     * @brief Updates hover state for menus, caption buttons, and execution buttons.
     * @param hoveredMenu Currently hovered menu.
     * @param hoveredCaption Currently hovered caption button.
     * @param hoveredExec Currently hovered execution button.
     * @return True if state changed and repaint is needed.
     */
    bool UpdateHoverState(HeaderMenuId hoveredMenu, CaptionButtonType hoveredCaption, HeaderExecId hoveredExec = HeaderExecId::None);

    [[nodiscard]] HeaderMenuId GetHoveredMenu() const noexcept { return m_hoveredMenu; }
    [[nodiscard]] CaptionButtonType GetHoveredCaption() const noexcept { return m_hoveredCaption; }
    [[nodiscard]] HeaderExecId GetHoveredExec() const noexcept { return m_hoveredExec; }

    /**
     * @brief Checks if mouse is over the Split Editor button.
     * @param pt Mouse point in client coordinates.
     * @return True if cursor is over the Split Editor button.
     */
    [[nodiscard]] bool HitTestSplitEditor(POINT pt) const noexcept;

    /**
     * @brief Updates hover state for the Split Editor button.
     * @return True if hover state changed and repaint is needed.
     */
    bool SetHoveredSplitEditor(bool hovered) noexcept;

    [[nodiscard]] D2D1_RECT_F GetSplitEditorBounds() const noexcept { return m_rcSplitEditor; }

    /**
     * @brief Checks if mouse is over the Quick Open (magnifying glass) button.
     * @param pt Mouse point in client coordinates.
     * @return True if cursor is over the Quick Open button.
     */
    [[nodiscard]] bool HitTestQuickOpen(POINT pt) const noexcept;

    /**
     * @brief Updates hover state for the Quick Open button.
     * @return True if hover state changed and repaint is needed.
     */
    bool SetHoveredQuickOpen(bool hovered) noexcept;

    [[nodiscard]] D2D1_RECT_F GetQuickOpenBounds() const noexcept { return m_rcQuickOpen; }

    // Zoom In / Zoom Out Controls (Image 3)
    [[nodiscard]] bool HitTestZoomIn(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestZoomOut(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestZoomReset(POINT pt) const noexcept;
    bool SetHoveredZoom(int hovered) noexcept; // 0=none, 1=out, 2=reset, 3=in
    [[nodiscard]] int GetHoveredZoom() const noexcept { return m_hoveredZoom; }
    [[nodiscard]] D2D1_RECT_F GetZoomContainerBounds() const noexcept { return m_rcZoomContainer; }

    /**
     * @brief Gets the bounding rectangle of a menu button.
     * @param id HeaderMenuId to query.
     * @return D2D1_RECT_F bounds of the button.
     */
    D2D1_RECT_F GetMenuItemBounds(HeaderMenuId id) const;

    /**
     * @brief Sets which menu is currently open with an active dropdown.
     */
    void SetActiveMenu(HeaderMenuId id) { m_activeMenu = id; }
    [[nodiscard]] HeaderMenuId GetActiveMenu() const noexcept { return m_activeMenu; }

    // Run & Debug Execution state
    void SetRunEnabled(bool enabled) noexcept { m_runEnabled = enabled; }
    [[nodiscard]] bool IsRunEnabled() const noexcept { return m_runEnabled; }
    void SetDebugEnabled(bool enabled) noexcept { m_debugEnabled = enabled; }
    [[nodiscard]] bool IsDebugEnabled() const noexcept { return m_debugEnabled; }

private:
    bool m_runEnabled = true;
    bool m_debugEnabled = true;

    HeaderMenuId m_hoveredMenu = HeaderMenuId::None;
    HeaderMenuId m_activeMenu = HeaderMenuId::None;
    CaptionButtonType m_hoveredCaption = CaptionButtonType::None;
    HeaderExecId m_hoveredExec = HeaderExecId::None;

    std::vector<MenuItem> m_menus;
    std::vector<ExecButton> m_execButtons;
    LucideIcons m_icons;

    // Bounding rectangles for caption controls
    D2D1_RECT_F m_rcMinimize{};
    D2D1_RECT_F m_rcMaximize{};
    D2D1_RECT_F m_rcClose{};

    // Split Editor control button (Image 3)
    D2D1_RECT_F m_rcSplitEditor{};
    bool m_hoveredSplitEditor = false;

    // Quick Open (magnifying glass) button (Image 5)
    D2D1_RECT_F m_rcQuickOpen{};
    bool m_hoveredQuickOpen = false;

    // Zoom In / Zoom Out controls widget (Image 3)
    D2D1_RECT_F m_rcZoomContainer{};
    D2D1_RECT_F m_rcZoomOut{};
    D2D1_RECT_F m_rcZoomReset{};
    D2D1_RECT_F m_rcZoomIn{};
    int m_hoveredZoom = 0; // 0=none, 1=out, 2=reset, 3=in

    // Direct2D Purple Glass Gradient Brush and theme state cache
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_pHeaderGradientBrush;
    float m_lastWidth = -1.0f;
    bool  m_lastIsDark = true; ///< Tracks theme transitions to invalidate gradient cache
    ThemeMode m_lastThemeMode = static_cast<ThemeMode>(-1);
};

} // namespace phide::ui
