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
    std::wstring name;          ///< "Python", "PHP (Composer)", "Node.js (npm)"
    std::wstring version;       ///< e.g. "3.13", "8.5", "20.x"
    std::wstring managerName;   ///< "pip", "composer", "npm"
    std::wstring executable;    ///< Path to detected executable
    bool isInstalled = false;   ///< True if runtime binary detected
    D2D1_RECT_F bounds{};
    D2D1_RECT_F browseBtnBounds{};
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
    ~PackageManagerView() = default;

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

    /**
     * @brief Sets callback triggered when user clicks "Install Package".
     */
    void SetOnInstallCallback(std::function<void(const std::wstring&, PkgInstallScope)> cb) {
        m_onInstallCallback = std::move(cb);
    }

    /**
     * @brief Fetches packages from Supabase remote REST API asynchronously.
     */
    void FetchSupabasePackagesAsync(const std::wstring& query = L"");

    [[nodiscard]] size_t GetPackageCount() const noexcept { return m_packages.size(); }
    [[nodiscard]] size_t GetFilteredPackageCount() const noexcept { return m_filteredIndices.size(); }
    [[nodiscard]] int GetSelectedIndex() const noexcept { return m_selectedIndex; }
    void SetSelectedIndex(int idx);

    [[nodiscard]] RuntimeLanguage GetActiveLanguage() const noexcept { return m_activeLanguage; }
    void SetActiveLanguage(RuntimeLanguage lang);

    [[nodiscard]] PkgViewState GetViewState() const noexcept { return m_viewState; }
    void SetViewState(PkgViewState state) noexcept { m_viewState = state; }

    void SelectLanguageAndOpenSearch(RuntimeLanguage lang);
    void NavigateBackToLanguages();

private:
    void DetectInstalledRuntimes();
    void InitializeCatalog();
    void ApplyFilter();
    int ComputeRelevance(const std::wstring& text, const std::wstring& query) const;
    void Populate500PythonPackages();
    void Populate500PhpPackages();

    // View state
    PkgViewState m_viewState = PkgViewState::LanguageGrid; ///< Default initial screen is the Language Grid!
    RuntimeLanguage m_activeLanguage = RuntimeLanguage::Python;

    // Search input state
    std::wstring m_searchQuery;
    bool m_isSearchFocused = true;
    ULONGLONG m_lastSearchKeystrokeTime = 0;
    bool m_searchDebouncePending = false;

    // Runtime cards
    std::vector<LanguageCard> m_languageCards;

    // Package catalog
    std::vector<PackageEntry> m_packages;
    std::vector<PackageEntry> m_phpPackages;
    std::vector<size_t> m_filteredIndices;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    size_t m_scrollIndex = 0;
    size_t m_visibleLimit = 12; ///< Virtualized lazy-loading chunk size

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

    std::function<void(const std::wstring&, PkgInstallScope)> m_onInstallCallback;
    LucideIcons m_icons;
};

} // namespace phide::ui
