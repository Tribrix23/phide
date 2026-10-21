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
#include <set>
#include <filesystem>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

/**
 * @struct DrawerTreeItem
 * @brief Visual entry in the file tree or perspective list.
 */
struct DrawerTreeItem {
    std::wstring label;
    std::wstring iconGlyph;
    std::wstring fullPath;
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
     * @brief Paints the drawer background, header, and contents with animated width and upper-left curve.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme.
     * @param config Application configuration.
     * @param clientHeight Height of the client window.
     * @param currentDrawerW Current animated drawer width.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW);

    /**
     * @brief Performs hit-testing on drawer items.
     * @param pt Mouse point in client coordinates.
     * @param config Application configuration.
     * @return Index of clicked tree item, or -1.
     */
    int HitTest(POINT pt, const app::AppConfig& config) const;

    /**
     * @brief Performs hit-testing on the "Open Folder" button shown when no folder is open.
     */
    bool HitTestOpenFolderButton(POINT pt, const app::AppConfig& config) const;

    /**
     * @brief Sets the hovered tree item.
     * @param index Hovered item index.
     * @return True if state changed.
     */
    bool SetHoveredIndex(int index);

    /**
     * @brief Sets the hovered state of the "Open Folder" button.
     * @return True if state changed.
     */
    bool SetHoveredOpenFolderBtn(bool hovered);

    /**
     * @brief Gets a pointer to the tree item at the specified index.
     */
    [[nodiscard]] const DrawerTreeItem* GetItem(int index) const {
        if (index >= 0 && index < static_cast<int>(m_visibleItems.size())) {
            return &m_visibleItems[index];
        }
        return nullptr;
    }

    /**
     * @brief Toggles folder open/closed state.
     */
    void ToggleFolder(int index);

    /**
     * @brief Opens a folder and populates the explorer tree with real filesystem contents.
     */
    void OpenFolder(const std::wstring& folderPath, const std::set<std::wstring>* pInitialExpanded = nullptr);

    /**
     * @brief Gets the currently expanded directories in the tree.
     */
    [[nodiscard]] const std::set<std::wstring>& GetExpandedDirs() const noexcept { return m_expandedDirs; }

    /**
     * @brief Restores the expanded directories and refreshes visible tree items.
     */
    void SetExpandedDirs(const std::set<std::wstring>& dirs);

    /**
     * @brief Closes the currently opened folder, returning the explorer to the "NO FOLDER OPENED" state.
     */
    void CloseFolder();

    /**
     * @brief Refreshes the tree contents from disk.
     */
    void RefreshTree();

    [[nodiscard]] bool HasOpenFolder() const noexcept { return m_hasOpenFolder; }
    [[nodiscard]] const std::wstring& GetOpenFolderPath() const noexcept { return m_openFolderPath; }
    [[nodiscard]] const std::wstring& GetRootName() const noexcept { return m_rootName; }

private:
    void RebuildVisibleItems();
    void AddDirectoryChildren(const std::wstring& dirPath, int depth);
    std::wstring GetIconForFile(const std::wstring& filename, bool isDir, bool isExpanded) const;

    bool m_hasOpenFolder = false;
    std::wstring m_openFolderPath;
    std::wstring m_rootName;

    std::set<std::wstring> m_expandedDirs;
    std::vector<DrawerTreeItem> m_visibleItems;

    int m_hoveredIndex = -1;
    bool m_hoveredOpenFolderBtn = false;
    D2D1_RECT_F m_rcOpenFolderBtn{};

    LucideIcons m_icons;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> m_pDrawerBgGeometry;
    float m_cachedRailW = -1.0f;
    float m_cachedHeaderH = -1.0f;
    float m_cachedDrawerW = -1.0f;
    float m_cachedClientH = -1.0f;
};

} // namespace phide::ui
