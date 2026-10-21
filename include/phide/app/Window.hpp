/**
 * @file Window.hpp
 * @brief Native Win32 frameless window implementation with Direct2D GPU rendering.
 * 
 * Coordinates the entire UI tree: TitleHeader, ActivityRail, DrawerPanel, EditorCanvas,
 * PopupMenu, and ThemePickerModal. Handles Windows 11 DWM message routing, DPI scaling,
 * and hardware acceleration.
 */

#pragma once

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <vector>
#include <string>

#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/TitleHeader.hpp"
#include "phide/ui/PopupMenu.hpp"
#include "phide/ui/ActivityRail.hpp"
#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/ThemePickerModal.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/workspace/WorkspaceManager.hpp"
#include "phide/lsp/LspClient.hpp"

namespace phide::app {

using Microsoft::WRL::ComPtr;

/**
 * @class Window
 * @brief Encapsulates the application window, message loop dispatch, and Direct2D pipeline.
 */
class Window {
public:
    Window();
    ~Window();

    /**
     * @brief Creates the window and initializes Direct2D GPU infrastructure.
     * @param hInstance Win32 application instance handle.
     * @param nCmdShow Initial display mode.
     * @return True if window creation succeeded.
     */
    bool Create(HINSTANCE hInstance, int nCmdShow);

    /**
     * @brief Executes the message loop until WM_QUIT is received.
     * @return Process exit code.
     */
    int Run();

    /**
     * @brief Static window procedure routing messages to the Window instance.
     */
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    void OnPaint();
    void OnResize(UINT width, UINT height);
    void OnDpiChanged(int newDpi, const RECT* pNewRect);

    // Hit-testing and Mouse Events
    LRESULT HandleNCHitTest(POINT screenPt);
    void HandleMouseMove(POINT clientPt);
    void HandleLButtonDown(POINT clientPt);
    void HandleLButtonUp(POINT clientPt);

    /**
     * @brief Toggles the sidebar drawer panel with smooth sliding animation.
     */
    void ToggleSidebar();

    // Multi-Document Life-cycle & Preview Tab Management
    editor::OpenDocument* GetActiveDoc() noexcept {
        if (m_activeDocIdx >= 0 && m_activeDocIdx < static_cast<int>(m_openDocs.size())) {
            return &m_openDocs[m_activeDocIdx];
        }
        return nullptr;
    }

    const editor::OpenDocument* GetActiveDoc() const noexcept {
        if (m_activeDocIdx >= 0 && m_activeDocIdx < static_cast<int>(m_openDocs.size())) {
            return &m_openDocs[m_activeDocIdx];
        }
        return nullptr;
    }

    [[nodiscard]] bool HasOpenFile() const noexcept {
        return (GetActiveDoc() != nullptr);
    }

    void OpenFileDocument(const std::wstring& path, bool isPreview = false);
    void OpenVirtualDocument(const std::wstring& name, const std::wstring& content, bool isPreview = false);
    void OpenSpecialView(const std::wstring& viewType, const std::wstring& title);
    void CloseDocument(int index);

    // File Action Handlers
    void DoNewTextFile();
    void DoNewFile();
    void DoNewWindow();
    void DoOpenFile();
    void DoOpenFolder();
    void DoOpenFolderFromPath(const std::wstring& path);
    void DoOpenWorkspaceFromFile();
    void DoAddFolderToWorkspace();
    void DoSaveWorkspaceAs();
    void DoDuplicateWorkspace();
    void DoSaveFile();
    void DoSaveFileAs();
    void DoSaveAll();
    void DoExportProfile();
    void DoImportProfile();
    void DoOpenSettings();
    void DoOpenKeyboardShortcuts();
    void DoOpenUserSnippets();
    void DoToggleThemeModal();
    void DoToggleTheme();
    void DoRevertFile();
    void DoCloseEditor();
    void DoCloseFolder();
    void DoReopenClosedEditor();
    void DoCut();
    void DoCopy();
    void DoPaste();
    void DoUndo();
    void DoRedo();
    void DoSelectAll();
    void DoToggleWordWrap();
    void DoShowExplorer();
    void DoShowSearch();
    void DoShowSourceControl();
    void DoCreateTerminal();
    void OpenFileByName(const std::wstring& name);
    void LoadFileFromPath(const std::wstring& path);
    void SaveFileToPath(const std::wstring& path);

    // Session State Persistence
    void SaveSessionState();
    void LoadSessionState();
    std::wstring GetSessionFilePath() const;

    // Win32 & D2D handles
    HWND m_hwnd = nullptr;
    ComPtr<ID2D1Factory> m_pD2DFactory;
    ComPtr<ID2D1HwndRenderTarget> m_pRenderTarget;
    ComPtr<IDWriteFactory> m_pDWriteFactory;

    // Subsystems
    AppConfig m_config;
    ui::Theme m_theme;
    ui::TitleHeader m_header;
    ui::PopupMenu m_popupMenu;
    ui::ActivityRail m_rail;
    ui::DrawerPanel m_drawer;
    ui::EditorCanvas m_editor;
    ui::ThemePickerModal m_themePicker;
    workspace::WorkspaceManager m_workspace;
    lsp::LspClient m_lsp;

    // Open Documents Collection
    std::vector<editor::OpenDocument> m_openDocs;
    int m_activeDocIdx = -1;
    int m_untitledCounter = 0;

    bool m_trackingMouse = false;
    bool m_wordWrap = false;
    bool m_autoSave = true;
    bool m_chordCtrlK = false;
    std::vector<std::wstring> m_recentPaths;
    std::wstring m_lastClosedFilePath;

    UINT_PTR m_caretTimerId = 1001;
    UINT_PTR m_animTimerId  = 1002;

    float m_drawerAnimProgress   = 0.0f;
    float m_targetDrawerProgress = 0.0f;
    bool  m_isDrawerAnimating    = false;
};

} // namespace phide::app
