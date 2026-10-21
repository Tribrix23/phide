/**
 * @file PopupMenu.cpp
 * @brief Implementation of the Dark IDE popup dropdown menu system.
 */

#include "phide/ui/PopupMenu.hpp"
#include <algorithm>

namespace phide::ui {

static bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) {
    return (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom);
}

PopupMenu::PopupMenu() = default;

void PopupMenu::OpenViewMenu(float x, float y, const app::AppConfig& config,
                             bool isSidebarOpen, bool isZoomed, bool wordWrap) {
    m_isOpen = true;
    m_hoveredMainIdx = -1;
    m_activeSubmenuIdx = -1;
    m_hoveredSubmenuIdx = -1;

    BuildViewMenuItems(isSidebarOpen, isZoomed, wordWrap);

    const float menuW = config.ScaleF(230.0f);
    LayoutMenu(m_items, x, y, menuW, config, m_mainMenuBounds);
}

void PopupMenu::OpenFileMenu(float x, float y, const app::AppConfig& config, bool autoSave,
                             bool hasOpenFile, bool isDirty, bool hasOpenFolder,
                             const std::vector<std::wstring>& recentItems) {
    m_isOpen = true;
    m_hoveredMainIdx = -1;
    m_activeSubmenuIdx = -1;
    m_hoveredSubmenuIdx = -1;

    BuildFileMenuItems(autoSave, hasOpenFile, isDirty, hasOpenFolder, recentItems);

    const float menuW = config.ScaleF(245.0f);
    LayoutMenu(m_items, x, y, menuW, config, m_mainMenuBounds);
}

void PopupMenu::OpenContextMenu(float x, float y, bool isFolder, const std::wstring& targetPath,
                                const std::wstring& rootFolder, const app::AppConfig& config,
                                bool canPaste) {
    m_isOpen = true;
    m_hoveredMainIdx = -1;
    m_activeSubmenuIdx = -1;
    m_hoveredSubmenuIdx = -1;

    BuildContextMenuItems(isFolder, targetPath, rootFolder, canPaste);

    const float menuW = config.ScaleF(240.0f);
    LayoutMenu(m_items, x, y, menuW, config, m_mainMenuBounds);
}

void PopupMenu::OpenEditorContextMenu(float x, float y, const app::AppConfig& config,
                                     bool hasSelection, bool canPaste) {
    (void)hasSelection;
    m_isOpen = true;
    m_hoveredMainIdx = -1;
    m_activeSubmenuIdx = -1;
    m_hoveredSubmenuIdx = -1;
    m_items.clear();

    // 1. Change All Occurrences (Ctrl+F2)
    m_items.push_back({ MenuActionId::EditorChangeAllOccurrences, L"Change All Occurrences", L"Ctrl+F2", false, false, false, true, {}, {} });
    // 2. Refactor... (Ctrl+Shift+R)
    m_items.push_back({ MenuActionId::EditorRefactor, L"Refactor...", L"Ctrl+Shift+R", false, false, false, true, {}, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 3. Cut (Ctrl+X)
    m_items.push_back({ MenuActionId::EditorCut, L"Cut", L"Ctrl+X", false, false, false, true, {}, {} });
    // 4. Copy (Ctrl+C)
    m_items.push_back({ MenuActionId::EditorCopy, L"Copy", L"Ctrl+C", false, false, false, true, {}, {} });
    // 5. Paste (Ctrl+V)
    m_items.push_back({ MenuActionId::EditorPaste, L"Paste", L"Ctrl+V", false, false, false, canPaste, {}, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 6. Command Palette... (Ctrl+Shift+P)
    m_items.push_back({ MenuActionId::EditorCommandPalette, L"Command Palette...", L"Ctrl+Shift+P", false, false, false, true, {}, {} });

    const float menuW = config.ScaleF(240.0f);
    LayoutMenu(m_items, x, y, menuW, config, m_mainMenuBounds);
}

void PopupMenu::Close() {
    m_isOpen = false;
    m_hoveredMainIdx = -1;
    m_activeSubmenuIdx = -1;
    m_hoveredSubmenuIdx = -1;
    m_mainMenuBounds = {};
    m_submenuBounds = {};
}

void PopupMenu::BuildViewMenuItems(bool isSidebarOpen, bool isZoomed, bool wordWrap) {
    m_items.clear();

    // 1. Command Palette... (Ctrl+Shift+P)
    m_items.push_back({ MenuActionId::CommandPalette, L"Command Palette...", L"Ctrl+Shift+P", false, false, false, {} });
    // 2. Open View...
    m_items.push_back({ MenuActionId::OpenView,       L"Open View...",       L"",             false, false, false, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None,           L"",                   L"",             true,  false, false, {} });

    // 3. Appearance ▸ (Cascading Submenu matching user reference image)
    PopupMenuItem appearanceItem;
    appearanceItem.actionId = MenuActionId::AppearanceCascade;
    appearanceItem.label = L"Appearance";
    appearanceItem.hasSubmenu = true;

    // Appearance Submenu Items
    appearanceItem.submenuItems.push_back({ MenuActionId::FullScreen,                L"Full Screen",                  L"F11",          false, isZoomed,       false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ZenMode,                   L"Zen Mode",                     L"Ctrl+K Z",     false, false,          false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::CenteredLayout,            L"Centered Layout",              L"",             false, false,          false, {} });
    // --- Submenu Separator ---
    appearanceItem.submenuItems.push_back({ MenuActionId::None,                      L"",                             L"",             true,  false,          false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ToggleMenuBar,             L"Menu Bar",                     L"",             false, true,           false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::TogglePrimarySideBar,      L"Primary Side Bar",             L"Ctrl+B",       false, isSidebarOpen,  false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ToggleSecondarySideBar,    L"Secondary Side Bar",           L"",             false, false,          false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ToggleStatusBar,          L"Status Bar",                   L"",             false, true,           false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::TogglePanel,              L"Panel",                        L"Ctrl+J",       false, true,           false, {} });
    // --- Submenu Separator ---
    appearanceItem.submenuItems.push_back({ MenuActionId::MoveSideBarRight,          L"Move Primary Side Bar Right",  L"",             false, false,          false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ActivityBarPosition,       L"Activity Bar Position",        L"",             false, false,          true,  {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::SecondaryActivityBarPosition, L"Secondary Activity Bar Position", L"",      false, false,          true,  {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::PanelPosition,             L"Panel Position",               L"",             false, false,          true,  {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::AlignPanel,                L"Align Panel",                  L"",             false, false,          true,  {} });
    // --- Submenu Separator ---
    appearanceItem.submenuItems.push_back({ MenuActionId::ToggleMinimap,             L"Minimap",                      L"",             false, true,           false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ToggleBreadcrumbs,         L"Breadcrumbs",                  L"",             false, true,           false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ToggleStickyScroll,        L"Sticky Scroll",                L"",             false, true,           false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ToggleRenderWhitespace,    L"Render Whitespace",            L"",             false, false,          false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ToggleRenderControlChars, L"Render Control Characters",     L"",             false, false,          false, {} });
    // --- Submenu Separator ---
    appearanceItem.submenuItems.push_back({ MenuActionId::ZoomIn,                    L"Zoom In",                      L"Ctrl+=",       false, false,          false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ZoomOut,                   L"Zoom Out",                     L"Ctrl+-",       false, false,          false, {} });
    appearanceItem.submenuItems.push_back({ MenuActionId::ResetZoom,                 L"Reset Zoom",                   L"Ctrl+Num0",    false, false,          false, {} });

    m_items.push_back(std::move(appearanceItem));

    // 4. Editor Layout ▸
    m_items.push_back({ MenuActionId::EditorLayoutCascade, L"Editor Layout", L"", false, false, true, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None,           L"",                   L"",             true,  false, false, {} });

    // 5. Explorer, Search, Source Control, Run, Extensions
    m_items.push_back({ MenuActionId::Explorer,       L"Explorer",           L"Ctrl+Shift+E", false, false, false, {} });
    m_items.push_back({ MenuActionId::Search,         L"Search",             L"Ctrl+Shift+F", false, false, false, {} });
    m_items.push_back({ MenuActionId::SourceControl,  L"Source Control",     L"Ctrl+Shift+G", false, false, false, {} });
    m_items.push_back({ MenuActionId::Run,            L"Run",                L"Ctrl+Shift+D", false, false, false, {} });
    m_items.push_back({ MenuActionId::Extensions,     L"Extensions",         L"Ctrl+Shift+X", false, false, false, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None,           L"",                   L"",             true,  false, false, {} });

    // 6. Problems, Output, Debug Console, Terminal
    m_items.push_back({ MenuActionId::Problems,       L"Problems",           L"Ctrl+Shift+M", false, false, false, {} });
    m_items.push_back({ MenuActionId::Output,         L"Output",             L"Ctrl+Shift+U", false, false, false, {} });
    m_items.push_back({ MenuActionId::DebugConsole,   L"Debug Console",      L"Ctrl+Shift+Y", false, false, false, {} });
    m_items.push_back({ MenuActionId::Terminal,       L"Terminal",           L"Ctrl+`",       false, false, false, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None,           L"",                   L"",             true,  false, false, {} });

    // 7. Word Wrap (Alt+Z)
    m_items.push_back({ MenuActionId::WordWrap,       L"Word Wrap",          L"Alt+Z",        false, wordWrap, false, {} });
}

void PopupMenu::BuildFileMenuItems(bool autoSave, bool hasOpenFile, bool isDirty, bool hasOpenFolder,
                                  const std::vector<std::wstring>& recentItems) {
    m_items.clear();

    // 1. New Text File (Ctrl+N)
    m_items.push_back({ MenuActionId::NewTextFile, L"New Text File", L"Ctrl+N", false, false, false, true, {}, {} });
    // 2. New File... (Ctrl+Alt+Windows+N)
    m_items.push_back({ MenuActionId::NewFile, L"New File...", L"Ctrl+Alt+Windows+N", false, false, false, true, {}, {} });
    // 3. New Window (Ctrl+Shift+N)
    m_items.push_back({ MenuActionId::NewWindow, L"New Window", L"Ctrl+Shift+N", false, false, false, true, {}, {} });

    // 4. New Window with Profile ▸
    PopupMenuItem winProfile;
    winProfile.actionId = MenuActionId::NewWindowWithProfile;
    winProfile.label = L"New Window with Profile";
    winProfile.hasSubmenu = true;
    winProfile.isEnabled = true;
    winProfile.submenuItems.push_back({ MenuActionId::NewWindowDefault, L"Default", L"", false, true, false, true, {}, {} });
    winProfile.submenuItems.push_back({ MenuActionId::NewWindowEmpty, L"Empty Profile", L"", false, false, false, true, {}, {} });
    m_items.push_back(std::move(winProfile));

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 5. Open File... (Ctrl+O)
    m_items.push_back({ MenuActionId::OpenFile, L"Open File...", L"Ctrl+O", false, false, false, true, {}, {} });
    // 6. Open Folder... (Ctrl+K Ctrl+O)
    m_items.push_back({ MenuActionId::OpenFolder, L"Open Folder...", L"Ctrl+K Ctrl+O", false, false, false, true, {}, {} });
    // 7. Open Workspace from File...
    m_items.push_back({ MenuActionId::OpenWorkspaceFromFile, L"Open Workspace from File...", L"", false, false, false, true, {}, {} });

    // 8. Open Recent ▸
    PopupMenuItem openRecent;
    openRecent.actionId = MenuActionId::OpenRecent;
    openRecent.label = L"Open Recent";
    openRecent.hasSubmenu = true;
    openRecent.isEnabled = true;
    if (recentItems.empty()) {
        openRecent.submenuItems.push_back({ MenuActionId::RecentItem0, L"phide", L"", false, false, false, true, {}, {} });
    } else {
        const MenuActionId recentIds[] = { MenuActionId::RecentItem0, MenuActionId::RecentItem1, MenuActionId::RecentItem2, MenuActionId::RecentItem3 };
        for (size_t r = 0; r < recentItems.size() && r < 4; ++r) {
            auto pos = recentItems[r].find_last_of(L"\\/");
            std::wstring disp = (pos != std::wstring::npos) ? recentItems[r].substr(pos + 1) : recentItems[r];
            openRecent.submenuItems.push_back({ recentIds[r], disp, L"", false, false, false, true, {}, {} });
        }
    }
    openRecent.submenuItems.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });
    openRecent.submenuItems.push_back({ MenuActionId::ReopenClosedEditor, L"Reopen Closed Editor", L"Ctrl+Shift+T", false, false, false, true, {}, {} });
    openRecent.submenuItems.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });
    openRecent.submenuItems.push_back({ MenuActionId::ClearRecent, L"Clear Recently Opened", L"", false, false, false, true, {}, {} });
    m_items.push_back(std::move(openRecent));

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 9. Add Folder to Workspace...
    m_items.push_back({ MenuActionId::AddFolderToWorkspace, L"Add Folder to Workspace...", L"", false, false, false, true, {}, {} });
    // 10. Save Workspace As... (disabled if no folder open)
    m_items.push_back({ MenuActionId::SaveWorkspaceAs, L"Save Workspace As...", L"", false, false, false, hasOpenFolder, {}, {} });
    // 11. Duplicate Workspace (disabled if no folder open)
    m_items.push_back({ MenuActionId::DuplicateWorkspace, L"Duplicate Workspace", L"", false, false, false, hasOpenFolder, {}, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 12. Save (Ctrl+S) - DISABLED if no file open, or if not dirty!
    m_items.push_back({ MenuActionId::Save, L"Save", L"Ctrl+S", false, false, false, hasOpenFile && isDirty, {}, {} });
    // 13. Save As... (Ctrl+Shift+S) - DISABLED if no file open!
    m_items.push_back({ MenuActionId::SaveAs, L"Save As...", L"Ctrl+Shift+S", false, false, false, hasOpenFile, {}, {} });
    // 14. Save All (Ctrl+K S) - DISABLED if no file open, or if not dirty!
    m_items.push_back({ MenuActionId::SaveAll, L"Save All", L"Ctrl+K S", false, false, false, hasOpenFile && isDirty, {}, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 15. Share ▸
    PopupMenuItem shareItem;
    shareItem.actionId = MenuActionId::Share;
    shareItem.label = L"Share";
    shareItem.hasSubmenu = true;
    shareItem.isEnabled = true;
    shareItem.submenuItems.push_back({ MenuActionId::ExportProfile, L"Export Profile...", L"", false, false, false, true, {}, {} });
    shareItem.submenuItems.push_back({ MenuActionId::ImportProfile, L"Import Profile...", L"", false, false, false, true, {}, {} });
    m_items.push_back(std::move(shareItem));

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 16. Auto Save (✓)
    m_items.push_back({ MenuActionId::ToggleAutoSave, L"Auto Save", L"", false, autoSave, false, true, {}, {} });

    // 17. Preferences ▸
    PopupMenuItem prefItem;
    prefItem.actionId = MenuActionId::Preferences;
    prefItem.label = L"Preferences";
    prefItem.hasSubmenu = true;
    prefItem.isEnabled = true;
    prefItem.submenuItems.push_back({ MenuActionId::Settings, L"Settings", L"Ctrl+,", false, false, false, true, {}, {} });
    prefItem.submenuItems.push_back({ MenuActionId::KeyboardShortcuts, L"Keyboard Shortcuts", L"Ctrl+K Ctrl+S", false, false, false, true, {}, {} });
    prefItem.submenuItems.push_back({ MenuActionId::UserSnippets, L"User Snippets", L"", false, false, false, true, {}, {} });
    prefItem.submenuItems.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });
    prefItem.submenuItems.push_back({ MenuActionId::ThemeSettings, L"Theme", L"Ctrl+K Ctrl+T", false, false, false, true, {}, {} });
    m_items.push_back(std::move(prefItem));

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 18. Revert File - DISABLED if no file open, or if not dirty!
    m_items.push_back({ MenuActionId::RevertFile, L"Revert File", L"", false, false, false, hasOpenFile && isDirty, {}, {} });
    // 19. Close Editor (Ctrl+F4) - DISABLED if no file open!
    m_items.push_back({ MenuActionId::CloseEditor, L"Close Editor", L"Ctrl+F4", false, false, false, hasOpenFile, {}, {} });
    // 20. Close Folder (Ctrl+K F) - DISABLED if no folder open!
    m_items.push_back({ MenuActionId::CloseFolder, L"Close Folder", L"Ctrl+K F", false, false, false, hasOpenFolder, {}, {} });
    // 21. Close Window (Alt+F4)
    m_items.push_back({ MenuActionId::CloseWindow, L"Close Window", L"Alt+F4", false, false, false, true, {}, {} });

    // --- Separator ---
    m_items.push_back({ MenuActionId::None, L"", L"", true, false, false, false, {}, {} });

    // 22. Exit
    m_items.push_back({ MenuActionId::Exit, L"Exit", L"", false, false, false, true, {}, {} });
}

void PopupMenu::BuildContextMenuItems(bool isFolder, const std::wstring& targetPath,
                                      const std::wstring& rootFolder, bool canPaste) {
    m_items.clear();

    if (isFolder) {
        // --- Folder Context Menu (Matching Image 5) ---
        // 1. New File...
        m_items.push_back({ MenuActionId::CtxNewFile,           L"New File...",                  L"",                       false, false, false, true, {}, {} });
        // 2. New Folder...
        m_items.push_back({ MenuActionId::CtxNewFolder,         L"New Folder...",                L"",                       false, false, false, true, {}, {} });
        // 3. Reveal in File Explorer (Shift+Alt+R)
        m_items.push_back({ MenuActionId::CtxRevealInExplorer,  L"Reveal in File Explorer",      L"Shift+Alt+R",            false, false, false, true, {}, {} });
        // 4. Open in Integrated Terminal
        m_items.push_back({ MenuActionId::CtxOpenInTerminal,    L"Open in Integrated Terminal",  L"",                       false, false, false, true, {}, {} });

        // --- Separator ---
        m_items.push_back({ MenuActionId::None,                 L"",                             L"",                       true,  false, false, false, {}, {} });

        // 5. Find in Folder... (Shift+Alt+F)
        m_items.push_back({ MenuActionId::CtxFindInFolder,      L"Find in Folder...",            L"Shift+Alt+F",            false, false, false, true, {}, {} });

        // --- Separator ---
        m_items.push_back({ MenuActionId::None,                 L"",                             L"",                       true,  false, false, false, {}, {} });

        // 6. Cut (Ctrl+X)
        m_items.push_back({ MenuActionId::CtxCut,               L"Cut",                          L"Ctrl+X",                 false, false, false, true, {}, {} });
        // 7. Copy (Ctrl+C)
        m_items.push_back({ MenuActionId::CtxCopy,              L"Copy",                         L"Ctrl+C",                 false, false, false, true, {}, {} });
        // 8. Paste (Ctrl+V)
        m_items.push_back({ MenuActionId::CtxPaste,             L"Paste",                        L"Ctrl+V",                 false, false, false, canPaste, {}, {} });

        // --- Separator ---
        m_items.push_back({ MenuActionId::None,                 L"",                             L"",                       true,  false, false, false, {}, {} });

        // 9. Copy Path (Shift+Alt+C)
        m_items.push_back({ MenuActionId::CtxCopyPath,          L"Copy Path",                    L"Shift+Alt+C",            false, false, false, true, {}, {} });
        // 10. Copy Relative Path (Ctrl+K Ctrl+Shift+C)
        m_items.push_back({ MenuActionId::CtxCopyRelativePath,  L"Copy Relative Path",           L"Ctrl+K Ctrl+Shift+C",    false, false, false, true, {}, {} });

        // --- Separator ---
        m_items.push_back({ MenuActionId::None,                 L"",                             L"",                       true,  false, false, false, {}, {} });

        // 11. Rename... (F2)
        m_items.push_back({ MenuActionId::CtxRename,            L"Rename...",                    L"F2",                     false, false, false, true, {}, {} });
        // 12. Delete (Delete)
        m_items.push_back({ MenuActionId::CtxDelete,            L"Delete",                       L"Delete",                 false, false, false, true, {}, {} });
    } else {
        // --- File Context Menu (Matching Image 4) ---
        // 1. Open to the Side
        m_items.push_back({ MenuActionId::CtxOpenToSide,        L"Open to the Side",             L"",                       false, false, false, true, {}, {} });
        // 2. Open With...
        m_items.push_back({ MenuActionId::CtxOpenWith,          L"Open With...",                 L"",                       false, false, false, true, {}, {} });
        // 3. Reveal in File Explorer (Shift+Alt+R)
        m_items.push_back({ MenuActionId::CtxRevealInExplorer,  L"Reveal in File Explorer",      L"Shift+Alt+R",            false, false, false, true, {}, {} });
        // 4. Open in Integrated Terminal
        m_items.push_back({ MenuActionId::CtxOpenInTerminal,    L"Open in Integrated Terminal",  L"",                       false, false, false, true, {}, {} });

        // --- Separator ---
        m_items.push_back({ MenuActionId::None,                 L"",                             L"",                       true,  false, false, false, {}, {} });

        // 5. Compare with Selected
        m_items.push_back({ MenuActionId::CtxCompareWithSelected, L"Compare with Selected",      L"",                       false, false, false, true, {}, {} });
        // 6. Select for Compare
        m_items.push_back({ MenuActionId::CtxSelectForCompare,    L"Select for Compare",         L"",                       false, false, false, true, {}, {} });
        // 7. Open Timeline
        m_items.push_back({ MenuActionId::CtxOpenTimeline,      L"Open Timeline",                L"",                       false, false, false, true, {}, {} });

        // --- Separator ---
        m_items.push_back({ MenuActionId::None,                 L"",                             L"",                       true,  false, false, false, {}, {} });

        // 8. Cut (Ctrl+X)
        m_items.push_back({ MenuActionId::CtxCut,               L"Cut",                          L"Ctrl+X",                 false, false, false, true, {}, {} });
        // 9. Copy (Ctrl+C)
        m_items.push_back({ MenuActionId::CtxCopy,              L"Copy",                         L"Ctrl+C",                 false, false, false, true, {}, {} });

        // --- Separator ---
        m_items.push_back({ MenuActionId::None,                 L"",                             L"",                       true,  false, false, false, {}, {} });

        // 10. Copy Path (Shift+Alt+C)
        m_items.push_back({ MenuActionId::CtxCopyPath,          L"Copy Path",                    L"Shift+Alt+C",            false, false, false, true, {}, {} });
        // 11. Copy Relative Path (Ctrl+K Ctrl+Shift+C)
        m_items.push_back({ MenuActionId::CtxCopyRelativePath,  L"Copy Relative Path",           L"Ctrl+K Ctrl+Shift+C",    false, false, false, true, {}, {} });

        // --- Separator ---
        m_items.push_back({ MenuActionId::None,                 L"",                             L"",                       true,  false, false, false, {}, {} });

        // 12. Rename... (F2)
        m_items.push_back({ MenuActionId::CtxRename,            L"Rename...",                    L"F2",                     false, false, false, true, {}, {} });
        // 13. Delete (Delete)
        m_items.push_back({ MenuActionId::CtxDelete,            L"Delete",                       L"Delete",                 false, false, false, true, {}, {} });
    }
}

void PopupMenu::LayoutMenu(std::vector<PopupMenuItem>& items, float startX, float startY, float width,
                          const app::AppConfig& config, D2D1_RECT_F& outBounds) {
    const float padY   = config.ScaleF(4.0f);
    const float itemH  = config.ScaleF(25.0f);
    const float sepH   = config.ScaleF(7.0f);

    float curY = startY + padY;

    for (auto& item : items) {
        if (item.isSeparator) {
            item.bounds = D2D1::RectF(startX, curY, startX + width, curY + sepH);
            curY += sepH;
        } else {
            item.bounds = D2D1::RectF(startX, curY, startX + width, curY + itemH);
            curY += itemH;
        }
    }

    outBounds = D2D1::RectF(startX, startY, startX + width, curY + padY);
}

void PopupMenu::LayoutSubmenu(int parentItemIdx, const app::AppConfig& config, float clientW, float clientH) {
    if (parentItemIdx < 0 || parentItemIdx >= static_cast<int>(m_items.size())) {
        m_activeSubmenuIdx = -1;
        m_submenuBounds = {};
        return;
    }

    auto& parent = m_items[parentItemIdx];
    if (!parent.hasSubmenu || parent.submenuItems.empty()) {
        m_activeSubmenuIdx = -1;
        m_submenuBounds = {};
        return;
    }

    m_activeSubmenuIdx = parentItemIdx;
    const float subW = config.ScaleF(245.0f);
    float subX = m_mainMenuBounds.right - config.ScaleF(2.0f);
    float subY = parent.bounds.top - config.ScaleF(4.0f);

    // Calculate approximate submenu height
    const float padY  = config.ScaleF(4.0f);
    const float itemH = config.ScaleF(23.5f);
    const float sepH  = config.ScaleF(7.0f);
    float totalH = padY * 2.0f;
    for (const auto& sub : parent.submenuItems) {
        totalH += sub.isSeparator ? sepH : itemH;
    }

    // Clamp vertically to stay entirely within the window client area
    const float topMargin = config.ScaleF(static_cast<float>(config.headerHeight)) + 2.0f;
    const float bottomMargin = config.ScaleF(10.0f);

    if (subY + totalH > clientH - bottomMargin) {
        subY = (std::max)(topMargin, clientH - totalH - bottomMargin);
    }
    if (subX + subW > clientW - config.ScaleF(8.0f)) {
        subX = m_mainMenuBounds.left - subW + config.ScaleF(2.0f);
    }

    LayoutMenu(parent.submenuItems, subX, subY, subW, config, m_submenuBounds);
}

void PopupMenu::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    if (!m_isOpen || !pRT) return;

    // 1. Render Main Menu
    RenderMenuPanel(pRT, theme, config, m_items, m_mainMenuBounds, m_hoveredMainIdx, m_activeSubmenuIdx);

    // 2. Render Cascading Submenu (Appearance) if active
    if (m_activeSubmenuIdx >= 0 && m_activeSubmenuIdx < static_cast<int>(m_items.size())) {
        const auto& parent = m_items[m_activeSubmenuIdx];
        if (parent.hasSubmenu && !parent.submenuItems.empty()) {
            RenderMenuPanel(pRT, theme, config, parent.submenuItems, m_submenuBounds, m_hoveredSubmenuIdx, -1);
        }
    }
}

void PopupMenu::RenderMenuPanel(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               const std::vector<PopupMenuItem>& items, const D2D1_RECT_F& menuBounds,
                               int hoveredIdx, int activeSubmenuParentIdx) {
    // Dropdown Panel Background with Rounded Corners (5px)
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(menuBounds, 5.0f, 5.0f);
    pRT->FillRoundedRectangle(rrc, theme.brushMenuBg.Get());

    // 1px Subtle Menu Border (#454545)
    pRT->DrawRoundedRectangle(rrc, theme.brushMenuBorder.Get(), 1.0f);

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];

        if (item.isSeparator) {
            float sepY = (item.bounds.top + item.bounds.bottom) / 2.0f;
            pRT->DrawLine(
                D2D1::Point2F(item.bounds.left + config.ScaleF(10.0f), sepY),
                D2D1::Point2F(item.bounds.right - config.ScaleF(10.0f), sepY),
                theme.brushMenuSeparator.Get(),
                1.0f
            );
            continue;
        }

        bool isHovered = (static_cast<int>(i) == hoveredIdx || static_cast<int>(i) == activeSubmenuParentIdx);

        if (isHovered && item.isEnabled) {
            // Hover background pill with 3.5px rounded corners matching VS Code
            D2D1_RECT_F rcHover = D2D1::RectF(
                item.bounds.left + config.ScaleF(4.0f),
                item.bounds.top,
                item.bounds.right - config.ScaleF(4.0f),
                item.bounds.bottom
            );
            D2D1_ROUNDED_RECT rrcHover = D2D1::RoundedRect(rcHover, 3.5f, 3.5f);
            pRT->FillRoundedRectangle(rrcHover, theme.brushMenuHover.Get());
        }

        ID2D1Brush* textBrush = !item.isEnabled ? theme.brushTextMuted.Get() : (isHovered ? theme.brushTextWhite.Get() : theme.brushMenuText.Get());
        ID2D1Brush* shortcutBrush = !item.isEnabled ? theme.brushTextMuted.Get() : (isHovered ? theme.brushTextWhite.Get() : theme.brushMenuShortcut.Get());
        ID2D1Brush* glyphBrush = !item.isEnabled ? theme.brushTextMuted.Get() : (isHovered ? theme.brushTextWhite.Get() : theme.brushMenuText.Get());

        // 1. Checkmark (✓)
        if (item.isChecked) {
            DrawCheckmark(pRT, item.bounds, glyphBrush);
        }

        // 2. Menu Item Text Label
        D2D1_RECT_F rcLabel = D2D1::RectF(
            item.bounds.left + config.ScaleF(28.0f),
            item.bounds.top,
            item.bounds.right - config.ScaleF(60.0f),
            item.bounds.bottom
        );
        pRT->DrawText(
            item.label.c_str(),
            static_cast<UINT32>(item.label.length()),
            theme.fontMenuItem.Get(),
            rcLabel,
            textBrush
        );

        // 3. Right-Aligned Keyboard Shortcut
        if (!item.shortcut.empty()) {
            D2D1_RECT_F rcShortcut = D2D1::RectF(
                item.bounds.left + config.ScaleF(60.0f),
                item.bounds.top,
                item.bounds.right - (item.hasSubmenu ? config.ScaleF(24.0f) : config.ScaleF(14.0f)),
                item.bounds.bottom
            );
            pRT->DrawText(
                item.shortcut.c_str(),
                static_cast<UINT32>(item.shortcut.length()),
                theme.fontMenuShortcut.Get(),
                rcShortcut,
                shortcutBrush
            );
        }

        // 4. Submenu Right Chevron (▸)
        if (item.hasSubmenu) {
            DrawSubmenuChevron(pRT, item.bounds, glyphBrush);
        }
    }
}

void PopupMenu::DrawCheckmark(ID2D1RenderTarget* pRT, const D2D1_RECT_F& itemBounds, ID2D1Brush* pBrush) {
    const float cx = itemBounds.left + 14.0f;
    const float cy = (itemBounds.top + itemBounds.bottom) / 2.0f;

    // Vector 2-segment checkmark
    pRT->DrawLine(
        D2D1::Point2F(cx - 4.0f, cy),
        D2D1::Point2F(cx - 1.0f, cy + 3.0f),
        pBrush,
        1.4f
    );
    pRT->DrawLine(
        D2D1::Point2F(cx - 1.0f, cy + 3.0f),
        D2D1::Point2F(cx + 4.5f, cy - 3.5f),
        pBrush,
        1.4f
    );
}

void PopupMenu::DrawSubmenuChevron(ID2D1RenderTarget* pRT, const D2D1_RECT_F& itemBounds, ID2D1Brush* pBrush) {
    const float cx = itemBounds.right - 14.0f;
    const float cy = (itemBounds.top + itemBounds.bottom) / 2.0f;

    // Crisp angled chevron (>)
    pRT->DrawLine(
        D2D1::Point2F(cx - 2.5f, cy - 3.5f),
        D2D1::Point2F(cx + 1.5f, cy),
        pBrush,
        1.3f
    );
    pRT->DrawLine(
        D2D1::Point2F(cx + 1.5f, cy),
        D2D1::Point2F(cx - 2.5f, cy + 3.5f),
        pBrush,
        1.3f
    );
}

bool PopupMenu::HandleMouseMove(POINT clientPt, const app::AppConfig& config, float clientW, float clientH) {
    if (!m_isOpen) return false;

    bool stateChanged = false;

    // 1. Check if hovering inside active Cascading Submenu
    if (m_activeSubmenuIdx >= 0 && PtInRectF(clientPt, m_submenuBounds)) {
        const auto& parent = m_items[m_activeSubmenuIdx];
        int newHover = -1;
        for (size_t i = 0; i < parent.submenuItems.size(); ++i) {
            if (!parent.submenuItems[i].isSeparator && parent.submenuItems[i].isEnabled && PtInRectF(clientPt, parent.submenuItems[i].bounds)) {
                newHover = static_cast<int>(i);
                break;
            }
        }
        if (m_hoveredSubmenuIdx != newHover) {
            m_hoveredSubmenuIdx = newHover;
            stateChanged = true;
        }
        return stateChanged;
    }

    // 2. Check if hovering inside Main Menu
    if (PtInRectF(clientPt, m_mainMenuBounds)) {
        int newMainHover = -1;
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (!m_items[i].isSeparator && PtInRectF(clientPt, m_items[i].bounds)) {
                if (m_items[i].isEnabled || m_items[i].hasSubmenu) {
                    newMainHover = static_cast<int>(i);
                }
                break;
            }
        }

        if (m_hoveredMainIdx != newMainHover) {
            m_hoveredMainIdx = newMainHover;
            stateChanged = true;
        }

        // If hovering over an item with a submenu (e.g. Appearance), cascade it open
        if (newMainHover >= 0 && m_items[newMainHover].hasSubmenu) {
            if (m_activeSubmenuIdx != newMainHover) {
                LayoutSubmenu(newMainHover, config, clientW, clientH);
                m_hoveredSubmenuIdx = -1;
                stateChanged = true;
            }
        } else if (newMainHover >= 0 && !m_items[newMainHover].hasSubmenu) {
            // Hovering another main item closes the submenu
            if (m_activeSubmenuIdx != -1) {
                m_activeSubmenuIdx = -1;
                m_hoveredSubmenuIdx = -1;
                m_submenuBounds = {};
                stateChanged = true;
            }
        }

        return stateChanged;
    }

    // 3. Outside both menus: reset hover highlights
    if (m_hoveredMainIdx != -1 || m_hoveredSubmenuIdx != -1) {
        m_hoveredMainIdx = -1;
        m_hoveredSubmenuIdx = -1;
        stateChanged = true;
    }

    return stateChanged;
}

MenuActionId PopupMenu::HandleLButtonDown(POINT clientPt) {
    if (!m_isOpen) return MenuActionId::None;

    // 1. Click inside Cascading Submenu
    if (m_activeSubmenuIdx >= 0 && PtInRectF(clientPt, m_submenuBounds)) {
        const auto& parent = m_items[m_activeSubmenuIdx];
        for (const auto& item : parent.submenuItems) {
            if (!item.isSeparator && PtInRectF(clientPt, item.bounds)) {
                if (!item.isEnabled) return MenuActionId::None;
                return item.actionId;
            }
        }
        return MenuActionId::None;
    }

    // 2. Click inside Main Menu
    if (PtInRectF(clientPt, m_mainMenuBounds)) {
        for (const auto& item : m_items) {
            if (!item.isSeparator && PtInRectF(clientPt, item.bounds)) {
                if (item.hasSubmenu) {
                    // Clicking parent with submenu keeps/toggles submenu
                    return MenuActionId::None;
                }
                if (!item.isEnabled) return MenuActionId::None;
                return item.actionId;
            }
        }
        return MenuActionId::None;
    }

    return MenuActionId::None;
}

bool PopupMenu::HitTestMenuArea(POINT clientPt) const {
    if (!m_isOpen) return false;
    if (PtInRectF(clientPt, m_mainMenuBounds)) return true;
    if (m_activeSubmenuIdx >= 0 && PtInRectF(clientPt, m_submenuBounds)) return true;
    return false;
}

} // namespace phide::ui
