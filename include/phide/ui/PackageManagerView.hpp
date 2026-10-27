/**
 * @file PackageManagerView.hpp
 * @brief PyCharm-styled dedicated Package Manager tab with language runtime grid first,
 *        debounced fuzzy search, lazy-loaded package list with split Install|▾ buttons,
 *        right-side GitHub preview, and Supabase integration.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;

namespace phide::ui {

/**
 * @enum RuntimeLanguage
 * @brief Supported development runtimes for package management.
 */
enum class RuntimeLanguage {
    Python = 0,
    Php = 1,
    Node = 2
};

/**
 * @enum PkgViewState
 * @brief Current presentation state in the package manager tab.
 */
enum class PkgViewState {
    LanguageGrid = 0,   ///< Initial screen: choose runtime / environment on flat rounded grid
    PackageSearch = 1   ///< Active screen: search, output list with install buttons, GitHub preview
};

/**
 * @enum PkgInstallScope
 * @brief Installation target scope for package execution.
 */
enum class PkgInstallScope {
    Project = 0,    ///< Default: install in current project
    Global = 1      ///< Install globally on the system
};

/**
 * @struct LanguageCard
 * @brief Modern flat grid card representing an installed or detected runtime.
 */
struct LanguageCard {
    RuntimeLanguage lang;
    std::wstring name;          ///< "Python", "PHP", "Node.js"
    std::wstring version;       ///< "v3.14.7", "v8.5.12", "v22.20.0"
    std::wstring managerName;   ///< "pip", "composer", "npm"
    std::wstring executable;    ///< Path to detected executable
    bool isInstalled = true;
    bool isHovered = false;
    D2D1_RECT_F bounds{};
};

/**
 * @struct PackageEntry
 * @brief Package item data matching Supabase python_package table fields (name, download, github).
 */
struct PackageEntry {
    std::wstring name;          ///< e.g. "matplotlib"
    std::wstring download;      ///< e.g. "pip install matplotlib"
    std::wstring github;        ///< e.g. "https://github.com/matplotlib/matplotlib"
    std::wstring version;       ///< e.g. "3.9.2"
    std::wstring summary;       ///< Description
    std::wstring license;       ///< e.g. "BSD", "MIT", "PSF"
    std::wstring stars;         ///< e.g. "20.5k"
    std::wstring topics;        ///< e.g. "#visualization #plotting #python"
    bool isRemote = false;      ///< True if loaded from Supabase REST API
    int searchScore = 0;        ///< Computed relevance score
    PkgInstallScope scope = PkgInstallScope::Project;
    bool isInstalled = false;
    bool isInstalling = false;

    // Card interactive regions
    D2D1_RECT_F bounds{};
    D2D1_RECT_F rcInstallBtn{};     ///< Main install button (e.g. "Install")
    D2D1_RECT_F rcSplitArrowBtn{};  ///< Split dropdown button ("▾")
};

/**
 * @class PackageManagerView
 * @brief PyCharm-style Package Manager tab view.
 */
class PackageManagerView {
public:
    PackageManagerView();
    ~PackageManagerView();

    /**
     * @brief Discards Direct2D cached resources on resize/theme change.
     */
    void DiscardDeviceResources();

    /**
     * @brief Paints the Package Manager tab UI.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float left, float width, float top, float height);

    /**
     * @brief Mouse movement handler for hover highlights and button states.
     */
    bool HandleMouseMove(POINT clientPt);

    /**
     * @brief Mouse click handler for selecting packages, cards, buttons.
     */
    bool HandleLButtonDown(POINT clientPt);

    /**
     * @brief Mouse wheel scrolling handler for package list and preview.
     */
    bool HandleMouseWheel(short delta, float mouseY);

    /**
     * @brief Character input for search box.
     */
    bool HandleChar(wchar_t ch);

    /**
     * @brief Key handling for backspace, arrow navigation, escape.
     */
    bool HandleKeyDown(WPARAM key);

    [[nodiscard]] bool IsSearchFocused() const noexcept { return m_isSearchFocused; }
    void SetSearchFocused(bool focused) noexcept { m_isSearchFocused = focused; }

    [[nodiscard]] const std::wstring& GetSearchQuery() const noexcept { return m_searchQuery; }
    void SetSearchQuery(const std::wstring& q);
    [[nodiscard]] size_t GetSearchCaretPos() const noexcept { return m_searchCaretPos; }
    void SetSearchCaretPos(size_t pos) noexcept { m_searchCaretPos = pos; }

    /**
     * @brief Sets callback triggered when user clicks "Install Package".
     */
    void SetOnInstallCallback(std::function<void(const std::wstring&, const std::wstring&, PkgInstallScope)> cb) {
        m_onInstallCallback = std::move(cb);
    }

    /**
     * @brief Returns cursor type (e.g. IDC_HAND, IDC_IBEAM, IDC_ARROW) for point.
     */
    [[nodiscard]] LPCWSTR GetCursorForPoint(POINT clientPt) const noexcept;

    /**
     * @brief Marks a package as successfully installed in catalog.
     */
    void MarkPackageInstalled(const std::wstring& pkgName);

    /**
     * @brief Marks a package as uninstalled in catalog.
     */
    void MarkPackageUninstalled(const std::wstring& pkgName);

    /**
     * @brief Scans workspace virtual environment and system site-packages to reflect installed status.
     */
    void ScanInstalledPackages(const std::wstring& wsFolder = L"");

    /**
     * @brief Fetches packages from Supabase remote REST API asynchronously.
     */
    void FetchSupabasePackagesAsync(const std::wstring& query = L"");

    [[nodiscard]] size_t GetPackageCount() const noexcept { return m_packages.size(); }
    [[nodiscard]] size_t GetFilteredPackageCount() const noexcept { return m_filteredIndices.size(); }
    [[nodiscard]] const std::vector<size_t>& GetFilteredIndices() const noexcept { return m_filteredIndices; }
    [[nodiscard]] const std::vector<PackageEntry>& GetPackages() const noexcept { return m_packages; }
    [[nodiscard]] const std::vector<PackageEntry>& GetPhpPackages() const noexcept { return m_phpPackages; }
    [[nodiscard]] int GetSelectedIndex() const noexcept { return m_selectedIndex; }
    void SetSelectedIndex(int idx);

    [[nodiscard]] RuntimeLanguage GetActiveLanguage() const noexcept { return m_activeLanguage; }
    void SetActiveLanguage(RuntimeLanguage lang);

    [[nodiscard]] PkgViewState GetViewState() const noexcept { return m_viewState; }
    void SetViewState(PkgViewState state) noexcept { m_viewState = state; }

    void SelectLanguageAndOpenSearch(RuntimeLanguage lang);
    void NavigateBackToLanguages();

    void SetHwnd(HWND hwnd);
    void InitWebView2(HWND hwnd);
    void UpdateWebViewBounds();
    void HideWebView();
    void NavigateToPackage(const std::wstring& url);

private:
    void DetectInstalledRuntimes();
    void InitializeCatalog();
    void ApplyFilter();
    int ComputeRelevance(const std::wstring& text, const std::wstring& query) const;
    void Populate500PythonPackages();
    void Populate500PhpPackages();
    void LoadAssetBitmaps(ID2D1RenderTarget* pRT);
    void RenderBrandedIcon(ID2D1RenderTarget* pRT, Theme& theme, RuntimeLanguage lang, const D2D1_RECT_F& bounds);
    void RenderGitHubWebPage(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, const PackageEntry& pkg, const D2D1_RECT_F& bounds);

    // View state
    PkgViewState m_viewState = PkgViewState::LanguageGrid; ///< Default initial screen is the Language Grid!
    RuntimeLanguage m_activeLanguage = RuntimeLanguage::Python;

    // Search input state
    std::wstring m_searchQuery;
    size_t m_searchCaretPos = 0;
    bool m_isSearchFocused = true;
    ULONGLONG m_lastSearchKeystrokeTime = 0;
    bool m_searchDebouncePending = false;

    // WebView2 Live Embedded GitHub Browser
    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_webViewEnvironment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_webViewController;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webView;
    bool m_webViewInitializing = false;
    bool m_webViewReady = false;
    std::wstring m_pendingUrl;
    std::wstring m_currentLoadedUrl;

    // Runtime cards
    std::vector<LanguageCard> m_languageCards;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_bmpPython;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_bmpPhp;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_bmpNode;
    bool m_bitmapsLoaded = false;

    // Package catalog
    std::vector<PackageEntry> m_packages;
    std::vector<PackageEntry> m_phpPackages;
    std::vector<size_t> m_filteredIndices;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    size_t m_scrollIndex = 0;
    size_t m_visibleLimit = 16; ///< Virtualized lazy-loading chunk size
    float m_previewScrollY = 0.0f;

    // Dropdown state for Install | ▾
    int m_activeDropdownPkgIdx = -1;
    D2D1_RECT_F m_rcDropdownProject{};
    D2D1_RECT_F m_rcDropdownGlobal{};
    bool m_isHoveringDropdownProject = false;
    bool m_isHoveringDropdownGlobal = false;

    // UI Bounds
    D2D1_RECT_F m_rcBackBtn{};
    D2D1_RECT_F m_rcSearchBox{};
    D2D1_RECT_F m_rcClearSearch{};
    D2D1_RECT_F m_rcPackageList{};
    D2D1_RECT_F m_rcRightPreview{};
    D2D1_RECT_F m_rcGithubHeaderLink{};
    D2D1_RECT_F m_rcGithubOpenBtn{};

    bool m_isHoveringBack = false;
    bool m_isHoveringGithubLink = false;
    bool m_isHoveringGithubOpen = false;

    // Supabase REST API state
    std::atomic<bool> m_isFetchingRemote{ false };
    std::atomic<bool> m_remoteFetchSuccess{ false };
    std::mutex m_packageMutex;

    std::function<void(const std::wstring&, const std::wstring&, PkgInstallScope)> m_onInstallCallback;
    LucideIcons m_icons;
};

} // namespace phide::ui
