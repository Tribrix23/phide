/**
 * @file DrawerPanel.hpp
 * @brief Slide-out contextual sidebar drawer for project explorer, AI assistant, and git history.
 * 
 * Expands when an icon on the activity rail is clicked, and collapses back to 0 width
 * when toggled off, restoring full canvas width.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @struct DrawerTreeItem
 * @brief Visual entry in the file tree or perspective list.
 */
struct DrawerTreeItem {
    std::wstring label;
    std::wstring iconGlyph;
    int depth = 0;
    bool isFolder = false;
    bool isExpanded = true;
    D2D1_RECT_F bounds{};
};

/**
 * @class DrawerPanel
 * @brief Renders the expandable sidebar drawer when active.
 */
class DrawerPanel {
public:
    DrawerPanel();
    ~DrawerPanel() = default;

    /**
     * @brief Paints the drawer background, header, and contents.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme.
     * @param config Application configuration.
     * @param clientHeight Height of the client window.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);

    /**
     * @brief Performs hit-testing on drawer items or close button.
     * @param pt Mouse point in client coordinates.
     * @param config Application configuration.
     * @return Index of clicked tree item, or -1.
     */
    int HitTest(POINT pt, const app::AppConfig& config) const;

    /**
     * @brief Tests if the mouse is over the drawer close button.
     * @param pt Mouse point.
     * @return True if clicking close button.
     */
    bool HitTestCloseButton(POINT pt) const;

    /**
     * @brief Sets the hovered tree item.
     * @param index Hovered item index.
     * @return True if state changed.
     */
    bool SetHoveredIndex(int index);

private:
    int m_hoveredIndex = -1;
    D2D1_RECT_F m_rcCloseBtn{};
    std::vector<DrawerTreeItem> m_mockFileTree;
};

} // namespace phide::ui
