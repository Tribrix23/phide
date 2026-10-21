/**
 * @file PopupMenu.hpp
 * @brief Dark IDE popup dropdown menu system with support for cascading submenus (Appearance).
 * 
 * Provides vector-crisp Direct2D rendering matching modern IDE specifications:
 * - Charcoal background (#1F1F1F) with subtle 1px border (#454545)
 * - Rounded corners and smooth hover highlighting (#0078D4 VS Code blue)
 * - Vector-rendered checkmarks (✓) and submenu chevrons (▸)
 * - Cascading Appearance submenu with side bar, fullscreen, and UI element toggles
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
 * @enum MenuActionId
 * @brief Identifies actionable commands triggered from popup dropdown menus.
 */
enum class MenuActionId {
    None = 0,

    // File Menu Actions
    NewTextFile,
    NewFile,
    NewWindow,
    NewWindowWithProfile,
    OpenFile,
    OpenFolder,
    OpenWorkspaceFromFile,
    OpenRecent,
    AddFolderToWorkspace,
    SaveWorkspaceAs,
    DuplicateWorkspace,
    Save,
    SaveAs,
    SaveAll,
    Share,
    ToggleAutoSave,
    Preferences,
    RevertFile,
    CloseEditor,
    CloseFolder,
    CloseWindow,
    Exit,

    // Submenu Actions for File Menu
    NewWindowDefault,
    NewWindowEmpty,
    RecentItem0,
    RecentItem1,
    RecentItem2,
    RecentItem3,
    ReopenClosedEditor,
    ClearRecent,
    ExportProfile,
    ImportProfile,
    Settings,
    KeyboardShortcuts,
    UserSnippets,
    ThemeSettings,

    // Main View Menu Actions
    CommandPalette,
    OpenView,
    AppearanceCascade,
    EditorLayoutCascade,
    Explorer,
    Search,
    SourceControl,
    Run,
    Extensions,
    Problems,
    Output,
    DebugConsole,
    Terminal,
    WordWrap,

    // Appearance Submenu Actions
    FullScreen,
    ZenMode,
    CenteredLayout,
    ToggleMenuBar,
    TogglePrimarySideBar,
    ToggleSecondarySideBar,
    ToggleStatusBar,
    TogglePanel,
    MoveSideBarRight,
    ActivityBarPosition,
    SecondaryActivityBarPosition,
    PanelPosition,
    AlignPanel,
    ToggleMinimap,
    ToggleBreadcrumbs,
    ToggleStickyScroll,
    ToggleRenderWhitespace,
    ToggleRenderControlChars,
    ZoomIn,
    ZoomOut,
    ResetZoom,

    // Explorer Context Menu Actions (Images 4 & 5)
    CtxOpenToSide,
    CtxOpenWith,
    CtxRevealInExplorer,
    CtxOpenInTerminal,
    CtxCompareWithSelected,
    CtxSelectForCompare,
    CtxOpenTimeline,
    CtxCut,
    CtxCopy,
    CtxPaste,
    CtxCopyPath,
    CtxCopyRelativePath,
    CtxRename,
    CtxDelete,
    CtxNewFile,
    CtxNewFolder,
    CtxFindInFolder
};

/**
 * @struct PopupMenuItem
 * @brief Representation of an individual menu item, separator, or submenu parent.
 */
struct PopupMenuItem {
    MenuActionId actionId = MenuActionId::None;
    std::wstring label;
    std::wstring shortcut;
    bool isSeparator = false;
    bool isChecked = false;
    bool hasSubmenu = false;
    bool isEnabled = true;
    std::vector<PopupMenuItem> submenuItems;

    // Runtime computed bounding box in client coordinates
    D2D1_RECT_F bounds{};
};

/**
 * @class PopupMenu
 * @brief Manages layout, hit-testing, cascading submenus, and Direct2D rendering for dropdown menus.
 */
class PopupMenu {
public:
    PopupMenu();
    ~PopupMenu() = default;

    /**
     * @brief Opens the View dropdown menu at the specified anchor point.
     * @param x Left coordinate of the anchor.
     * @param y Top coordinate of the anchor (e.g. bottom of the header button).
     * @param config Application metrics and DPI scale.
     * @param isSidebarOpen Whether the primary sidebar drawer is currently open.
     * @param isZoomed Whether the window is currently maximized.
     * @param wordWrap Whether editor word wrap is currently active.
     */
    void OpenViewMenu(float x, float y, const app::AppConfig& config,
                      bool isSidebarOpen, bool isZoomed, bool wordWrap = false);

    /**
     * @brief Opens the File dropdown menu at the specified anchor point.
     * @param x Left coordinate of the anchor.
     * @param y Top coordinate of the anchor.
     * @param config Application metrics.
     * @param autoSave Whether auto save is currently toggled on.
     * @param hasOpenFile Whether an editor file is currently open.
     * @param isDirty Whether the active file has unsaved changes.
     * @param hasOpenFolder Whether a folder/project is currently loaded.
     * @param recentItems Optional list of recently opened files or folders.
     */
    void OpenFileMenu(float x, float y, const app::AppConfig& config, bool autoSave,
                      bool hasOpenFile, bool isDirty, bool hasOpenFolder,
                      const std::vector<std::wstring>& recentItems = {});

    /**
     * @brief Opens the custom explorer context menu for a file or folder (matching Images 4 & 5).
     * @param x Left coordinate of the anchor.
     * @param y Top coordinate of the anchor.
     * @param isFolder True if target is a folder, false if a file.
     * @param targetPath Absolute filesystem path of the right-clicked item.
     * @param rootFolder Root workspace path for relative path calculation.
     * @param config Application metrics.
     * @param canPaste True if clipboard contains a path that can be pasted.
     */
    void OpenContextMenu(float x, float y, bool isFolder, const std::wstring& targetPath,
                         const std::wstring& rootFolder, const app::AppConfig& config,
                         bool canPaste = false);

    /**
     * @brief Closes the popup menu and any open submenus.
     */
    void Close();

    /**
     * @brief Returns true if a menu is currently open.
     */
    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    /**
     * @brief Renders the open dropdown menu and any active cascading submenus.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);

    /**
     * @brief Dispatches mouse movement to update hover states and cascading submenu triggers.
     * @param clientPt Mouse point in client coordinates.
     * @param config Application metrics.
     * @param clientW Client window width.
     * @param clientH Client window height.
     * @return True if visual state changed and window should be repainted.
     */
    bool HandleMouseMove(POINT clientPt, const app::AppConfig& config, float clientW, float clientH);

    /**
     * @brief Dispatches mouse click to select an item or trigger submenu action.
     * @param clientPt Mouse point in client coordinates.
     * @return MenuActionId of clicked item, or MenuActionId::None.
     */
    MenuActionId HandleLButtonDown(POINT clientPt);

    /**
     * @brief Tests if the given point is inside the main menu or active submenu bounding rects.
     */
    [[nodiscard]] bool HitTestMenuArea(POINT clientPt) const;

private:
    void BuildViewMenuItems(bool isSidebarOpen, bool isZoomed, bool wordWrap);
    void BuildFileMenuItems(bool autoSave, bool hasOpenFile, bool isDirty, bool hasOpenFolder,
                            const std::vector<std::wstring>& recentItems = {});
    void BuildContextMenuItems(bool isFolder, const std::wstring& targetPath,
                               const std::wstring& rootFolder, bool canPaste);
    void LayoutMenu(std::vector<PopupMenuItem>& items, float startX, float startY, float width,
                    const app::AppConfig& config, D2D1_RECT_F& outBounds);
    void LayoutSubmenu(int parentItemIdx, const app::AppConfig& config, float clientW, float clientH);

    void RenderMenuPanel(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         const std::vector<PopupMenuItem>& items, const D2D1_RECT_F& menuBounds,
                         int hoveredIdx, int activeSubmenuParentIdx);

    void DrawCheckmark(ID2D1RenderTarget* pRT, const D2D1_RECT_F& itemBounds, ID2D1Brush* pBrush);
    void DrawSubmenuChevron(ID2D1RenderTarget* pRT, const D2D1_RECT_F& itemBounds, ID2D1Brush* pBrush);

    bool m_isOpen = false;
    D2D1_RECT_F m_mainMenuBounds{};
    D2D1_RECT_F m_submenuBounds{};

    std::vector<PopupMenuItem> m_items;
    int m_hoveredMainIdx = -1;
    int m_activeSubmenuIdx = -1;
    int m_hoveredSubmenuIdx = -1;
};

} // namespace phide::ui
