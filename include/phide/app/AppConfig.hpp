/**
 * @file AppConfig.hpp
 * @brief Global configuration constants, UI geometry parameters, and application state.
 * 
 * This header defines all geometric dimensions, layout metrics, and persistent runtime
 * state variables for phide. Metrics are specified in logical pixels and scaled dynamically
 * according to the active system High-DPI ratio.
 */

#pragma once

#include <string>
#include <cstdint>

namespace phide::app {

/**
 * @struct AppConfig
 * @brief Stores runtime layout metrics, operational flags, and UI states.
 */
struct AppConfig {
    // --- Window Sizing ---
    int initialWidth  = 1280;   ///< Initial window width in logical pixels.
    int initialHeight = 800;    ///< Initial window height in logical pixels.
    int minWidth      = 900;    ///< Minimum allowable window width before layout clipping.
    int minHeight     = 550;    ///< Minimum allowable window height.

    // --- Header Metrics ---
    int headerHeight  = 42;     ///< Height of the integrated frameless title bar.
    int captionBtnWidth = 46;   ///< Width of minimize/maximize/close caption buttons.

    // --- Activity Rail (Left Sidebar) Metrics ---
    int railWidth     = 52;     ///< Fixed width of the collapsed activity rail (matching mockup).
    int railIconSize  = 20;     ///< Size of the vector glyphs within rail buttons.
    int railBtnSize   = 36;     ///< Width and height of the rail button bounding box.

    // --- Drawer Panel Metrics ---
    int drawerWidth   = 280;    ///< Width of the slide-out drawer panel when expanded.
    int rightSidebarWidth = 340; ///< Width of the right sidebar (Runtimes / Extensions panel) when expanded.
    bool isDrawerOpen = false;  ///< True if the contextual sidebar drawer is expanded; false if collapsed.
    int activeRailTab = 0;      ///< Current active rail tab (Default: 0 = Files / Explorer matching Image 2).
    float gitSplitY   = 320.0f; ///< Vertical splitter position between Changes and Graph.

    // --- View and Appearance Settings ---
    bool showMinimap = true;            ///< True if code minimap is rendered on right of editor canvas.
    bool showBreadcrumbs = true;        ///< True if file breadcrumbs hierarchy is visible above editor.
    bool showLineNumbers = true;        ///< True if line numbers gutter is rendered. Default enabled.
    bool showStatusBar = true;          ///< True if bottom status bar is rendered. Default enabled.
    bool wordWrap = false;              ///< True if word wrap is enabled in editor.
    bool renderWhitespace = false;      ///< True if whitespace dots are rendered.
    bool zenMode = false;               ///< True if Zen Mode (distraction-free full screen) is active.
    bool isGraphColored = false;        ///< True if file network graph nodes are colored by file type (Default false per user prompt).
    float fontSize = 14.0f;             ///< Editor font size in points.
    float lineHeightMultiplier = 1.45f; ///< Line height relative to font size.
    float zoomLevel = 1.0f;             ///< Zoom multiplier (1.0 = 100%).

    // --- Floating Prompt Bar Metrics ---
    int promptBarWidth  = 540;  ///< Width of the bottom floating AI prompt pill.
    int promptBarHeight = 44;   ///< Height of the floating prompt pill.
    int promptBarBottomMargin = 20; ///< Distance from the bottom of the window canvas.

    // --- Active Document Info ---
    std::wstring documentTitle = L"PHP IDE Concept Design"; ///< Document/project title shown in header pill.
    std::wstring activeModelName = L"High \u25BE";            ///< Active AI model selector label.

    // --- High-DPI Scaling ---
    float dpiScale = 1.0f;      ///< Current system DPI scale factor (1.0 = 96 DPI, 1.5 = 144 DPI, etc.).

    /**
     * @brief Scales a logical pixel value to physical pixels according to current DPI.
     * @param logicalPx Value in 96-DPI logical coordinates.
     * @return Scaled value in physical screen pixels.
     */
    [[nodiscard]] constexpr int Scale(int logicalPx) const noexcept {
        return static_cast<int>(logicalPx * dpiScale + 0.5f);
    }

    /**
     * @brief Scales a floating-point coordinate according to current DPI.
     * @param logicalVal Coordinate in logical units.
     * @return Scaled coordinate in physical units.
     */
    [[nodiscard]] constexpr float ScaleF(float logicalVal) const noexcept {
        return logicalVal * dpiScale;
    }
};

} // namespace phide::app
