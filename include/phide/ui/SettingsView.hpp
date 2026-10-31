/**
 * @file SettingsView.hpp
 * @brief Interactive modern Settings editor tab view with categorized card layout, searchable preferences,
 *        tactile switches, numeric steppers, and dropdown selectors.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include <functional>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

/**
 * @enum SettingCategory
 * @brief Logical grouping of configuration preferences.
 */
enum class SettingCategory {
    CommonlyUsed = 0,
    TextEditor   = 1,
    Appearance   = 2,
    Files        = 3
};

/**
 * @enum SettingControl
 * @brief UI control type used for interactive editing in a setting card.
 */
enum class SettingControl {
    Toggle,        ///< Modern iOS/Fluent rounded toggle switch
    NumberStepper, ///< [-] [ value ] [+] tactile buttons
    Dropdown,      ///< Clickable cycle pill with chevron
    TextEdit       ///< Text / preset selector
};

/**
 * @struct SettingItem
 * @brief Defines a single configurable application preference card.
 */
struct SettingItem {
    std::wstring id;          ///< Unique identifier, e.g. "editor.fontSize"
    std::wstring scope;       ///< Tag badge, e.g. "Editor", "Files", "Appearance"
    std::wstring title;       ///< Display name, e.g. "Font Size"
    std::wstring description; ///< Explanatory subtitle
    SettingCategory category = SettingCategory::TextEditor;
    bool isCommonlyUsed      = true;
    SettingControl control   = SettingControl::Toggle;

    // Dropdown / Preset values
    std::vector<std::wstring> options;

    // Number stepper bounds
    float minVal  = 1.0f;
    float maxVal  = 100.0f;
    float stepVal = 1.0f;
    std::wstring unit; ///< e.g. "px", "ms", "sp"

    // Dynamic layout bounds computed during Render
    D2D1_RECT_F cardBounds{};
    D2D1_RECT_F controlBounds{};
    D2D1_RECT_F minusBounds{};
    D2D1_RECT_F plusBounds{};
};

/**
 * @class SettingsView
 * @brief Dedicated editor surface rendering the modern settings view.
 */
class SettingsView {
public:
    SettingsView();
    ~SettingsView() = default;

    /**
     * @brief Releases device-dependent resources.
     */
    void DiscardDeviceResources() {}

    /**
     * @brief Renders the settings view with sidebar categories, modern card grid, and search bar.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float left, float width, float top, float height);

    /**
     * @brief Handles mouse movement and hover over cards and controls.
     */
    bool HandleMouseMove(POINT clientPt);

    /**
     * @brief Handles mouse clicks on category tabs, search bar, toggle switches, steppers, and dropdowns.
     * @param clientPt Mouse click coordinate.
     * @param config Application configuration to read/write.
     * @param theme Theme to query or update.
     * @param outModifiedSettingId Receives the ID of the setting modified, if any.
     * @return True if an action was handled and repaint is needed.
     */
    bool HandleLButtonDown(POINT clientPt, app::AppConfig& config, Theme& theme, std::wstring& outModifiedSettingId);

    /**
     * @brief Handles mouse wheel scrolling across cards.
     */
    bool HandleMouseWheel(short delta);

    /**
     * @brief Handles character typed into the search bar.
     */
    bool HandleChar(wchar_t ch);

    /**
     * @brief Handles special keys (Backspace, Delete, Escape) in search bar.
     */
    bool HandleKeyDown(WPARAM key);

    [[nodiscard]] bool IsSearchFocused() const noexcept { return m_isSearchFocused; }
    void SetSearchFocused(bool focused) noexcept { m_isSearchFocused = focused; }
    [[nodiscard]] const std::wstring& GetSearchQuery() const noexcept { return m_searchQuery; }
    void SetSearchQuery(const std::wstring& q) { m_searchQuery = q; m_scrollOffsetY = 0.0f; }

    [[nodiscard]] SettingCategory GetActiveCategory() const noexcept { return m_activeCategory; }
    void SetActiveCategory(SettingCategory cat) noexcept { m_activeCategory = cat; m_scrollOffsetY = 0.0f; }
    void ResetScroll() noexcept { m_scrollOffsetY = 0.0f; }

    /**
     * @brief Queries the appropriate Windows mouse cursor for the given client coordinate.
     * @return IDC_HAND for interactive buttons, steppers, toggles, and dropdowns; IDC_IBEAM for search input; nullptr for default.
     */
    [[nodiscard]] LPCWSTR GetCursorForPoint(POINT clientPt) const;

    [[nodiscard]] bool IsDropdownOpen() const noexcept { return m_openDropdownItemIdx >= 0; }
    void CloseDropdown() noexcept { m_openDropdownItemIdx = -1; m_hoveredDropdownOption = -1; }

private:
    std::vector<SettingItem> m_items;
    SettingCategory m_activeCategory = SettingCategory::CommonlyUsed;
    int m_activeScope = 0; ///< 0 = All Settings, 1 = User, 2 = Workspace
    std::wstring m_searchQuery;
    bool m_isSearchFocused = false;
    bool m_cursorVisible = true;
    float m_scrollOffsetY = 0.0f;
    float m_maxScrollOffsetY = 0.0f;

    // Hover state
    int m_hoveredCardIdx = -1;
    int m_hoveredCategoryIdx = -1;
    int m_hoveredScopeIdx = -1;
    bool m_hoveredSearchClear = false;
    std::string m_hoveredControlSubId; ///< "toggle", "minus", "plus", "dropdown"

    // Dropdown popup state
    int m_openDropdownItemIdx = -1;
    D2D1_RECT_F m_openDropdownBounds{};
    int m_hoveredDropdownOption = -1;

    // Layout cache
    D2D1_RECT_F m_viewBounds{};
    D2D1_RECT_F m_searchBoxBounds{};
    D2D1_RECT_F m_searchClearBounds{};
    std::vector<D2D1_RECT_F> m_categoryBounds;
    std::vector<D2D1_RECT_F> m_scopeBounds;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pFontDesc;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pFontTitle;

    LucideIcons m_icons;

    void InitializeSettings();
    [[nodiscard]] std::vector<size_t> GetFilteredItemIndices() const;
    void DrawToggleSwitch(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          const D2D1_RECT_F& bounds, bool isChecked, bool isHovered);
    void DrawStepper(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                     const SettingItem& item, const std::wstring& valStr, bool isHoveredMinus, bool isHoveredPlus);
    void DrawDropdown(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                      const D2D1_RECT_F& bounds, const std::wstring& valStr, bool isHovered, bool isOpen);
    void DrawDropdownPopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);
};

} // namespace phide::ui
