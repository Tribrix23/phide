#pragma once

#include "phide/ui/Theme.hpp"
#include "phide/app/AppConfig.hpp"
#include "phide/ui/LucideIcons.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>

namespace phide::ui {

/**
 * @struct EnvironmentOption
 * @brief Represents an executable environment/interpreter option the user can select as default.
 */
struct EnvironmentOption {
    std::wstring name;                   ///< Display title (e.g. "Python 3.13.9 (Global System)")
    std::wstring path;                   ///< Binary executable path
    std::wstring type;                   ///< "System PATH", "Virtualenv", "Custom"
    bool isDefault = false;              ///< True if chosen as default environment
    D2D1_RECT_F bounds{};                ///< Hit bounds for selection
    bool isHovered = false;              ///< Hover state
};

/**
 * @struct ProcessTask
 * @brief Represents an active background task in the modern Processes card (Image 3).
 */
struct ProcessTask {
    std::wstring title;                  ///< e.g. "Updating Python interpreter"
    std::wstring subtitle;               ///< e.g. "Launching skeleton generator..."
    float progress = 0.0f;               ///< 0.0f to 1.0f
    bool isCompleted = false;
    float completionTimer = 3500.0f;
    D2D1_RECT_F cancelBtnBounds{};
    bool isCancelHovered = false;
};

/**
 * @struct ExtensionItem
 * @brief Represents an environment runtime extension (Python, PHP, Node.js).
 */
struct ExtensionItem {
    std::wstring id;                     ///< "python", "php", "node"
    std::wstring name;                   ///< "Python", "PHP", "Node.js"
    std::wstring tagline;                ///< Short, crisp summary tagline
    std::wstring publisher;              ///< Publisher / ecosystem info
    std::wstring description;            ///< Informative description
    std::vector<std::pair<std::wstring, std::wstring>> features; ///< (Feature name, detail)
    std::wstring assetFile;              ///< Image filename in assets (e.g. "python.png")
    bool isInstalled = false;            ///< True if detected on the user's PC
    std::wstring installedVersion;       ///< Genuine version detected on PC (e.g. "3.13.9")
    std::wstring installPath;            ///< Detected binary path
    std::vector<std::wstring> versions;  ///< Available versions to choose
    std::vector<std::wstring> versionDownloads; ///< Download URLs mapped to versions (e.g. from Supabase)
    std::vector<std::wstring> modernTags;       ///< Sleek Bento capability tags (e.g. "Scripting", "APIs", "Automation")
    int selectedVersionIdx = 0;          ///< Index of chosen version
    bool isSpecialAllVersionsUninstall = false; ///< True for Node.js (all versions show uninstall if node is installed)

    // System Environment choices (User requirement: "USER CAN CHOOSE THE SYSTEM ENVIROMENT THATS THE DEFAULT")
    std::vector<EnvironmentOption> environments; ///< Selectable environment options
    int defaultEnvIdx = 0;                       ///< Index of default chosen environment
    D2D1_RECT_F browseBtnBounds{};               ///< "Browse Custom Path..." button hit bounds
    bool isHoverBrowseBtn = false;

    // Cached Direct2D bitmap loaded from assets/<assetFile> if present
    Microsoft::WRL::ComPtr<ID2D1Bitmap> pImageBitmap;
    bool attemptedImageLoad = false;

    // Layout hit bounds
    D2D1_RECT_F cardBounds{};
    bool isHovered = false;
};

/**
 * @class ExtensionsPanel
 * @brief Renders the right sidebar extensions & environment runtimes panel (Python, PHP, Node.js).
 */
class ExtensionsPanel {
public:
    enum class ViewMode {
        List,   ///< Grid of square cards with rounded corners
        Detail  ///< Extension details with skeleton loader, version dropdown, Install/Uninstall button
    };

    ExtensionsPanel();
    ~ExtensionsPanel() = default;

    void Open() noexcept;
    void Close() noexcept { m_isOpen = false; m_showVersionDropdown = false; }
    void Toggle() noexcept { if (m_isOpen) Close(); else Open(); }
    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    [[nodiscard]] float GetWidth() const noexcept { return m_width; }
    void SetWidth(float w) noexcept { m_width = w; }

    void SetOnDataChanged(std::function<void()> cb) noexcept { m_onDataChanged = std::move(cb); }

    /**
     * @brief Detects genuinely installed runtimes on the user's PC (python, node, php).
     */
    void RefreshPcStatus();

    /**
     * @brief Queries Supabase REST endpoints for Python, PHP, and Node runtime versions and download links.
     * Endpoints:
     *   https://lzwtgcbvuoogpzcgujdb.supabase.co/rest/v1/python
     *   https://lzwtgcbvuoogpzcgujdb.supabase.co/rest/v1/php
     *   https://lzwtgcbvuoogpzcgujdb.supabase.co/rest/v1/node
     * ApiKey: sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry
     * Columns: version, download
     */
    bool FetchSupabaseVersions(bool async = false);
    bool FetchSupabasePythonVersions(bool async = false) { return FetchSupabaseVersions(async); }

    /**
     * @brief Updates animation progress for skeleton loader and hover effects.
     */
    void UpdateAnim(float deltaMs) noexcept;

    /**
     * @brief Renders the right sidebar extensions panel.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float x, float y, float w, float h);

    // Mouse hit-testing and event handling
    bool HitTest(POINT pt) const noexcept;
    bool HandleMouseMove(POINT pt) noexcept;
    bool HandleLButtonDown(POINT pt, std::function<void(const std::wstring&)> notifyCb = nullptr) noexcept;
    bool HandleMouseWheel(short delta) noexcept;

    [[nodiscard]] ViewMode GetViewMode() const noexcept { return m_viewMode; }
    void SetViewMode(ViewMode mode) noexcept { m_viewMode = mode; }
    [[nodiscard]] int GetSelectedExtensionIdx() const noexcept { return m_selectedExtensionIdx; }
    [[nodiscard]] const D2D1_RECT_F& GetBackBtnBounds() const noexcept { return m_backBtnBounds; }
    [[nodiscard]] const D2D1_RECT_F& GetActionBtnBounds() const noexcept { return m_actionBtnBounds; }
    [[nodiscard]] const D2D1_RECT_F& GetDropdownBtnBounds() const noexcept { return m_dropdownBtnBounds; }

    // Direct access to items for verification testing
    [[nodiscard]] const std::vector<ExtensionItem>& GetItems() const noexcept { return m_items; }
    [[nodiscard]] std::vector<ExtensionItem>& GetItems() noexcept { return m_items; }

    bool SelectDefaultEnvironment(int itemIdx, int envIdx);
    bool SetCustomEnvironment(int itemIdx, const std::wstring& customPath);

    // Processes management (Images 2 & 3)
    void StartProcessTask(const std::wstring& title, const std::wstring& subtitle);
    void UpdateProcessTask(const std::wstring& title, float progress, const std::wstring& subtitle);
    void CompleteProcessTask(const std::wstring& title, const std::wstring& completionMsg);
    void DismissProcessTask(size_t index);
    [[nodiscard]] const std::vector<ProcessTask>& GetProcessTasks() const noexcept { return m_processTasks; }
    [[nodiscard]] bool HasActiveProcesses() const noexcept { return !m_processTasks.empty(); }
    [[nodiscard]] bool HitTestProcesses(POINT pt) const noexcept;
    void RenderProcessesCard(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h);

    void CreateWorkspaceVenv(std::function<void(const std::wstring&)> notifyCb = nullptr);
    void UninstallRuntime(int itemIdx, const std::wstring& version, std::function<void(const std::wstring&)> notifyCb = nullptr);
    void InstallRuntime(int itemIdx, const std::wstring& version, std::function<void(const std::wstring&)> notifyCb = nullptr);

private:
    void LoadAssetImages(ID2D1RenderTarget* pRT);
    void RenderListView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h);
    void RenderDetailView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h);
    void RenderBrandedIcon(ID2D1RenderTarget* pRT, Theme& theme, const ExtensionItem& item, const D2D1_RECT_F& bounds);
    void RenderSkeletonLoader(ID2D1RenderTarget* pRT, Theme& theme, const D2D1_RECT_F& bounds);

    bool m_isOpen = false;
    float m_width = 340.0f;
    ViewMode m_viewMode = ViewMode::List;
    int m_selectedExtensionIdx = 0;

    std::vector<ExtensionItem> m_items;
    LucideIcons m_icons;

    // Detail View controls
    bool m_isLoading = false;
    float m_loadingTime = 0.0f;
    float m_shimmerOffset = 0.0f;
    bool m_showVersionDropdown = false;

    // Bounds for mouse hit testing
    D2D1_RECT_F m_panelBounds{};
    D2D1_RECT_F m_backBtnBounds{};
    D2D1_RECT_F m_actionBtnBounds{};
    D2D1_RECT_F m_dropdownBtnBounds{};
    D2D1_RECT_F m_dropdownMenuBounds{};
    std::vector<D2D1_RECT_F> m_versionItemBounds;

    bool m_hoverBack = false;
    bool m_hoverAction = false;
    bool m_hoverDropdown = false;
    int m_hoveredVersionItem = -1;

    float m_scrollOffset = 0.0f;
    float m_versionDropdownScrollY = 0.0f;
    std::function<void()> m_onDataChanged;
    std::mutex m_itemsMutex;
    std::wstring m_statusBannerMessage;
    float m_statusBannerTimer = 0.0f;
    std::vector<ProcessTask> m_processTasks;
    bool m_processesMinimized = false;
    D2D1_RECT_F m_processesCardBounds{};
    D2D1_RECT_F m_processesMinBtnBounds{};
    bool m_isMinBtnHovered = false;
    float m_processAnimProgress = 0.0f;
    bool m_attemptedSupabaseFetch = false;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pHeaderFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pBoldFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pDescFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pTagFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pPathFormat;
};

} // namespace phide::ui
