/**
 * @file SettingsView.cpp
 * @brief Implementation of the modern, categorized, card-based Settings view.
 */

#include "phide/ui/SettingsView.hpp"
#include <algorithm>
#include <cwctype>

namespace phide::ui {

SettingsView::SettingsView() {
    InitializeSettings();
}

void SettingsView::InitializeSettings() {
    m_items.clear();

    // 1. Files: Auto Save (Dropdown)
    {
        SettingItem it;
        it.id = L"files.autoSave";
        it.scope = L"FILES";
        it.title = L"Auto Save";
        it.description = L"Controls auto save of editors that have unsaved changes.";
        it.category = SettingCategory::Files;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Dropdown;
        it.options = { L"afterDelay", L"onFocusChange", L"onWindowChange", L"off" };
        m_items.push_back(std::move(it));
    }

    // 2. Files: Auto Save Delay (Number Stepper)
    {
        SettingItem it;
        it.id = L"files.autoSaveDelay";
        it.scope = L"FILES";
        it.title = L"Auto Save Delay";
        it.description = L"Controls the delay in milliseconds after which an editor is saved automatically.";
        it.category = SettingCategory::Files;
        it.isCommonlyUsed = true;
        it.control = SettingControl::NumberStepper;
        it.minVal = 200.0f;
        it.maxVal = 5000.0f;
        it.stepVal = 200.0f;
        it.unit = L"ms";
        m_items.push_back(std::move(it));
    }

    // 3. Editor: Font Size (Number Stepper)
    {
        SettingItem it;
        it.id = L"editor.fontSize";
        it.scope = L"EDITOR";
        it.title = L"Font Size";
        it.description = L"Controls the font size in pixels for code editors.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::NumberStepper;
        it.minVal = 8.0f;
        it.maxVal = 36.0f;
        it.stepVal = 1.0f;
        it.unit = L"px";
        m_items.push_back(std::move(it));
    }

    // 4. Editor: Font Family (Dropdown / Presets)
    {
        SettingItem it;
        it.id = L"editor.fontFamily";
        it.scope = L"EDITOR";
        it.title = L"Font Family";
        it.description = L"Controls the typeface used in the editor buffer.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Dropdown;
        it.options = { L"Consolas", L"Cascadia Code", L"Courier New", L"Lucida Console" };
        m_items.push_back(std::move(it));
    }

    // 5. Editor: Tab Size (Number Stepper)
    {
        SettingItem it;
        it.id = L"editor.tabSize";
        it.scope = L"EDITOR";
        it.title = L"Tab Size";
        it.description = L"The number of spaces a tab is equal to.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::NumberStepper;
        it.minVal = 2.0f;
        it.maxVal = 8.0f;
        it.stepVal = 2.0f;
        it.unit = L" spaces";
        m_items.push_back(std::move(it));
    }

    // 6. Editor: Word Wrap (Toggle)
    {
        SettingItem it;
        it.id = L"editor.wordWrap";
        it.scope = L"EDITOR";
        it.title = L"Word Wrap";
        it.description = L"Controls whether lines wrap to fit within the editor viewport width.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Toggle;
        m_items.push_back(std::move(it));
    }

    // 7. Editor: Line Numbers (Toggle)
    {
        SettingItem it;
        it.id = L"editor.lineNumbers";
        it.scope = L"EDITOR";
        it.title = L"Line Numbers";
        it.description = L"Controls the display of line numbers in the editor gutter.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Toggle;
        m_items.push_back(std::move(it));
    }

    // 8. Editor: Minimap (Toggle)
    {
        SettingItem it;
        it.id = L"editor.minimap";
        it.scope = L"EDITOR";
        it.title = L"Minimap";
        it.description = L"Controls whether the code overview minimap is shown on the right.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Toggle;
        m_items.push_back(std::move(it));
    }

    // 9. Editor: Render Whitespace (Dropdown)
    {
        SettingItem it;
        it.id = L"editor.renderWhitespace";
        it.scope = L"EDITOR";
        it.title = L"Render Whitespace";
        it.description = L"Controls how the editor should render whitespace characters.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Dropdown;
        it.options = { L"selection", L"boundary", L"trailing", L"all", L"none" };
        m_items.push_back(std::move(it));
    }

    // 10. Editor: Cursor Style (Dropdown)
    {
        SettingItem it;
        it.id = L"editor.cursorStyle";
        it.scope = L"EDITOR";
        it.title = L"Cursor Style";
        it.description = L"Controls the cursor style in insert input mode.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Dropdown;
        it.options = { L"line", L"block", L"underline" };
        m_items.push_back(std::move(it));
    }

    // 11. Editor: Insert Spaces (Toggle)
    {
        SettingItem it;
        it.id = L"editor.insertSpaces";
        it.scope = L"EDITOR";
        it.title = L"Insert Spaces";
        it.description = L"Insert spaces when pressing Tab instead of tab character.";
        it.category = SettingCategory::TextEditor;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Toggle;
        m_items.push_back(std::move(it));
    }

    // 12. Appearance: Breadcrumbs (Toggle)
    {
        SettingItem it;
        it.id = L"workbench.breadcrumbs";
        it.scope = L"APPEARANCE";
        it.title = L"Breadcrumbs";
        it.description = L"Turn file path navigation breadcrumbs on or off above editor.";
        it.category = SettingCategory::Appearance;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Toggle;
        m_items.push_back(std::move(it));
    }

    // 14. Appearance: Status Bar (Toggle)
    {
        SettingItem it;
        it.id = L"workbench.statusBar";
        it.scope = L"APPEARANCE";
        it.title = L"Status Bar";
        it.description = L"Controls the visibility of the bottom status bar.";
        it.category = SettingCategory::Appearance;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Toggle;
        m_items.push_back(std::move(it));
    }

    // 15. Appearance: File Network Graph Colors (Toggle)
    {
        SettingItem it;
        it.id = L"workbench.graphColored";
        it.scope = L"APPEARANCE";
        it.title = L"Graph Node Colors";
        it.description = L"Colors nodes in the File Network graph view by file extension type.";
        it.category = SettingCategory::Appearance;
        it.isCommonlyUsed = true;
        it.control = SettingControl::Toggle;
        m_items.push_back(std::move(it));
    }
}

std::vector<size_t> SettingsView::GetFilteredItemIndices() const {
    std::vector<size_t> result;

    std::wstring qLower = m_searchQuery;
    for (auto& c : qLower) c = towlower(c);
    while (!qLower.empty() && qLower.front() == L' ') qLower.erase(0, 1);
    while (!qLower.empty() && qLower.back() == L' ') qLower.pop_back();

    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto& it = m_items[i];

        // Scope filter
        if (m_activeScope == 1 && it.scope == L"APPEARANCE") continue;
        if (m_activeScope == 2 && it.scope == L"FILES") continue;

        // If search query is non-empty, search across title, description, and id
        if (!qLower.empty()) {
            std::wstring tLower = it.title;
            for (auto& c : tLower) c = towlower(c);
            std::wstring dLower = it.description;
            for (auto& c : dLower) c = towlower(c);
            std::wstring idLower = it.id;
            for (auto& c : idLower) c = towlower(c);

            if (tLower.find(qLower) != std::wstring::npos ||
                dLower.find(qLower) != std::wstring::npos ||
                idLower.find(qLower) != std::wstring::npos) {
                result.push_back(i);
            }
        } else {
            // Category filter
            if (m_activeCategory == SettingCategory::CommonlyUsed) {
                if (it.isCommonlyUsed) result.push_back(i);
            } else if (it.category == m_activeCategory) {
                result.push_back(i);
            }
        }
    }
    return result;
}

void SettingsView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          float left, float width, float top, float height) {
    if (!pRT) return;

    m_viewBounds = D2D1::RectF(left, top, left + width, top + height);

    // Initialize Lucide icons if needed
    ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        m_icons.Initialize(pFactory.Get());
    }

    // Ensure leading-aligned typography formats
    if (!m_pFontDesc && theme.dwriteFactory) {
        theme.dwriteFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_REGULAR,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            config.ScaleF(12.5f),
            L"en-US",
            &m_pFontDesc
        );
        if (m_pFontDesc) {
            m_pFontDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_pFontDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_pFontDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    if (!m_pFontTitle && theme.dwriteFactory) {
        theme.dwriteFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            config.ScaleF(14.0f),
            L"en-US",
            &m_pFontTitle
        );
        if (m_pFontTitle) {
            m_pFontTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_pFontTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_pFontTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    // 1. Fill View Background with refined glass IDE backdrop
    ID2D1Brush* pBgBrush = theme.brushCanvasBg ? theme.brushCanvasBg.Get() : theme.brushRailBg.Get();
    pRT->FillRectangle(m_viewBounds, pBgBrush);

    const float headerH = config.ScaleF(72.0f);
    const float sidebarW = config.ScaleF(210.0f);

    // 2. Render Header Section (Title + Subtitle + Search Bar + Scope Pills)
    D2D1_RECT_F rcHeader = D2D1::RectF(left, top, left + width, top + headerH);

    // Subtle header bottom divider line
    pRT->DrawLine(
        D2D1::Point2F(left, top + headerH),
        D2D1::Point2F(left + width, top + headerH),
        theme.brushBorderSubtle.Get(),
        0.8f
    );

    // Settings Gear Icon Badge in Header
    const float iconBoxSize = config.ScaleF(36.0f);
    const float iconBoxX = left + config.ScaleF(24.0f);
    const float iconBoxY = top + (headerH - iconBoxSize) / 2.0f;
    D2D1_RECT_F rcIconBox = D2D1::RectF(iconBoxX, iconBoxY, iconBoxX + iconBoxSize, iconBoxY + iconBoxSize);
    D2D1_ROUNDED_RECT rrcIconBox = D2D1::RoundedRect(rcIconBox, 8.0f, 8.0f);

    ID2D1Brush* pBadgeBg = theme.brushHomeBadgeBg ? theme.brushHomeBadgeBg.Get() : theme.brushItemHover.Get();
    pRT->FillRoundedRectangle(rrcIconBox, pBadgeBg);
    pRT->DrawRoundedRectangle(rrcIconBox, theme.brushBorderSubtle.Get(), 1.0f);

    D2D1_RECT_F rcGear = D2D1::RectF(iconBoxX + 4.0f, iconBoxY + 4.0f, iconBoxX + iconBoxSize - 4.0f, iconBoxY + iconBoxSize - 4.0f);
    ID2D1Brush* pAccentBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get();
    m_icons.DrawSettings(pRT, rcGear, pAccentBrush, 1.4f);

    // Header Title and Subtitle
    float textX = iconBoxX + iconBoxSize + config.ScaleF(14.0f);
    D2D1_RECT_F rcTitle = D2D1::RectF(textX, top + config.ScaleF(14.0f), textX + config.ScaleF(240.0f), top + config.ScaleF(38.0f));
    const wchar_t* titleStr = L"Settings";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  theme.fontTitle.Get() ? theme.fontTitle.Get() : theme.fontBody.Get(),
                  rcTitle, theme.brushTextPrimary.Get());

    D2D1_RECT_F rcSub = D2D1::RectF(textX, top + config.ScaleF(40.0f), textX + config.ScaleF(360.0f), top + config.ScaleF(60.0f));
    const wchar_t* subStr = L"Manage preferences, editor behavior, and appearance";
    pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)),
                  theme.fontHint.Get() ? theme.fontHint.Get() : theme.fontBody.Get(),
                  rcSub, theme.brushTextSecondary.Get());

    // Search Bar on Right of Header
    const float searchW = config.ScaleF((std::min)(420.0f, width * 0.45f));
    const float searchH = config.ScaleF(34.0f);
    const float searchX = left + width - searchW - config.ScaleF(28.0f);
    const float searchY = top + (headerH - searchH) / 2.0f;
    m_searchBoxBounds = D2D1::RectF(searchX, searchY, searchX + searchW, searchY + searchH);
    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(m_searchBoxBounds, 6.0f, 6.0f);

    // Search background
    ID2D1Brush* pSearchBg = theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushItemHover.Get();
    pRT->FillRoundedRectangle(rrcSearch, pSearchBg);
    ID2D1Brush* pSearchBorder = m_isSearchFocused
        ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get())
        : theme.brushBorderSubtle.Get();
    pRT->DrawRoundedRectangle(rrcSearch, pSearchBorder, m_isSearchFocused ? 1.4f : 1.0f);

    // Search Icon inside Search Bar
    D2D1_RECT_F rcSearchIcon = D2D1::RectF(searchX + config.ScaleF(8.0f), searchY + config.ScaleF(7.0f), searchX + config.ScaleF(24.0f), searchY + searchH - config.ScaleF(7.0f));
    m_icons.DrawSearch(pRT, rcSearchIcon, theme.brushTextSecondary.Get(), 1.4f);

    // Search Text or Placeholder
    D2D1_RECT_F rcSearchText = D2D1::RectF(searchX + config.ScaleF(30.0f), searchY + config.ScaleF(6.0f), searchX + searchW - config.ScaleF(30.0f), searchY + searchH - config.ScaleF(6.0f));
    if (m_searchQuery.empty()) {
        const wchar_t* phStr = L"Search settings (e.g. font, wrap, theme)...";
        pRT->DrawText(phStr, static_cast<UINT32>(wcslen(phStr)),
                      theme.fontHint.Get() ? theme.fontHint.Get() : theme.fontBody.Get(),
                      rcSearchText, theme.brushTextSecondary.Get());
    } else {
        pRT->DrawText(m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                      theme.fontBody.Get(), rcSearchText, theme.brushTextPrimary.Get());
        // Clear 'x' button
        float clearSize = config.ScaleF(16.0f);
        m_searchClearBounds = D2D1::RectF(searchX + searchW - clearSize - config.ScaleF(8.0f), searchY + (searchH - clearSize) / 2.0f,
                                          searchX + searchW - config.ScaleF(8.0f), searchY + (searchH + clearSize) / 2.0f);
        if (m_hoveredSearchClear) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_searchClearBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        float cx = (m_searchClearBounds.left + m_searchClearBounds.right) / 2.0f;
        float cy = (m_searchClearBounds.top + m_searchClearBounds.bottom) / 2.0f;
        float cr = config.ScaleF(3.5f);
        pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr), D2D1::Point2F(cx + cr, cy + cr), theme.brushTextSecondary.Get(), 1.4f);
        pRT->DrawLine(D2D1::Point2F(cx - cr, cy + cr), D2D1::Point2F(cx + cr, cy - cr), theme.brushTextSecondary.Get(), 1.4f);
    }

    // Dynamic Blinking Caret (NO hardcoded '|', authentic Direct2D line blinking at 500ms)
    if (m_isSearchFocused) {
        bool showCaret = ((GetTickCount64() / 500) % 2 == 0);
        if (showCaret) {
            float caretX = rcSearchText.left;
            if (!m_searchQuery.empty() && theme.dwriteFactory) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                    theme.fontBody.Get(), 1000.0f, searchH, &pLayout)) && pLayout) {
                    DWRITE_TEXT_METRICS tm;
                    if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                        caretX += tm.widthIncludingTrailingWhitespace + 1.0f;
                    }
                }
            } else if (!m_searchQuery.empty()) {
                caretX += static_cast<float>(m_searchQuery.length()) * config.ScaleF(8.0f);
            }
            if (caretX > rcSearchText.right - config.ScaleF(4.0f)) {
                caretX = rcSearchText.right - config.ScaleF(4.0f);
            }
            float caretY1 = rcSearchText.top + config.ScaleF(3.0f);
            float caretY2 = rcSearchText.bottom - config.ScaleF(3.0f);
            ID2D1Brush* pCaretBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : pAccentBrush;
            pRT->DrawLine(D2D1::Point2F(caretX, caretY1), D2D1::Point2F(caretX, caretY2), pCaretBrush, 1.5f);
        }
    }

    // 3. Render Left Category Sidebar (Only active categories: Commonly Used, Text Editor, Appearance, Files)
    float bodyTop = top + headerH;
    float bodyH = height - headerH;
    D2D1_RECT_F rcSidebar = D2D1::RectF(left, bodyTop, left + sidebarW, top + height);

    // Vertical Divider
    pRT->DrawLine(
        D2D1::Point2F(left + sidebarW, bodyTop),
        D2D1::Point2F(left + sidebarW, top + height),
        theme.brushBorderSubtle.Get(),
        0.8f
    );

    // Sidebar Category Items (Workbench removed completely as requested)
    struct CatDef {
        SettingCategory cat;
        const wchar_t* name;
        const wchar_t* glyph;
    };
    static const CatDef kCategories[] = {
        { SettingCategory::CommonlyUsed, L"Commonly Used", L"✦" },
        { SettingCategory::TextEditor,   L"Text Editor",   L"✎" },
        { SettingCategory::Appearance,   L"Appearance",    L"🎨" },
        { SettingCategory::Files,        L"Files",         L"📁" }
    };
    const size_t kCatCount = sizeof(kCategories) / sizeof(kCategories[0]);

    m_categoryBounds.clear();
    const float catItemH = config.ScaleF(38.0f);
    const float catPadX = config.ScaleF(14.0f);
    float curCatY = bodyTop + config.ScaleF(16.0f);

    for (size_t i = 0; i < kCatCount; ++i) {
        D2D1_RECT_F rcCat = D2D1::RectF(left + catPadX, curCatY, left + sidebarW - catPadX, curCatY + catItemH);
        m_categoryBounds.push_back(rcCat);

        bool isActive = (m_searchQuery.empty() && m_activeCategory == kCategories[i].cat);
        bool isHovered = (m_hoveredCategoryIdx == static_cast<int>(i));

        D2D1_ROUNDED_RECT rrcCat = D2D1::RoundedRect(rcCat, 6.0f, 6.0f);
        if (isActive) {
            pRT->FillRoundedRectangle(rrcCat, theme.brushHomeBadgeBg ? theme.brushHomeBadgeBg.Get() : theme.brushItemHover.Get());
            // Active left accent indicator bar
            D2D1_RECT_F rcBar = D2D1::RectF(rcCat.left, rcCat.top + config.ScaleF(8.0f), rcCat.left + config.ScaleF(3.5f), rcCat.bottom - config.ScaleF(8.0f));
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcBar, 2.0f, 2.0f), pAccentBrush);
        } else if (isHovered) {
            pRT->FillRoundedRectangle(rrcCat, theme.brushItemHover.Get());
        }

        // Category Icon / Glyph + Text
        D2D1_RECT_F rcCatText = D2D1::RectF(rcCat.left + config.ScaleF(14.0f), rcCat.top, rcCat.right - config.ScaleF(8.0f), rcCat.bottom);
        std::wstring fullLabel = std::wstring(kCategories[i].glyph) + L"  " + kCategories[i].name;
        ID2D1Brush* pTextCol = (isActive || isHovered)
            ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
            : theme.brushTextSecondary.Get();
        pRT->DrawText(fullLabel.c_str(), static_cast<UINT32>(fullLabel.length()),
                      theme.fontBody.Get(), rcCatText, pTextCol);

        curCatY += catItemH + config.ScaleF(4.0f);
    }

    // 4. Render Right Content Area (Wider Cards Grid filling container gracefully)
    float contentLeft = left + sidebarW + config.ScaleF(28.0f);
    float contentW = (std::max)(config.ScaleF(560.0f), width - sidebarW - config.ScaleF(56.0f));
    float contentTop = bodyTop + config.ScaleF(16.0f);
    float contentH = bodyH - config.ScaleF(24.0f);

    D2D1_RECT_F rcContentClip = D2D1::RectF(contentLeft, bodyTop, left + width - config.ScaleF(8.0f), top + height);
    pRT->PushAxisAlignedClip(rcContentClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Filter items
    auto filteredIndices = GetFilteredItemIndices();

    // Section Header Title
    std::wstring sectionTitle;
    if (!m_searchQuery.empty()) {
        sectionTitle = L"Search Results (" + std::to_wstring(filteredIndices.size()) + L" settings found)";
    } else {
        for (const auto& c : kCategories) {
            if (c.cat == m_activeCategory) {
                sectionTitle = std::wstring(c.glyph) + L" " + c.name + L" (" + std::to_wstring(filteredIndices.size()) + L" settings)";
                break;
            }
        }
    }

    float cardY = contentTop - m_scrollOffsetY;
    D2D1_RECT_F rcSecHeader = D2D1::RectF(contentLeft, cardY, contentLeft + contentW, cardY + config.ScaleF(28.0f));
    pRT->DrawText(sectionTitle.c_str(), static_cast<UINT32>(sectionTitle.length()),
                  theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get(),
                  rcSecHeader, theme.brushTextPrimary.Get());
    cardY += config.ScaleF(34.0f);

    // If no matching settings
    if (filteredIndices.empty()) {
        D2D1_RECT_F rcEmpty = D2D1::RectF(contentLeft, cardY + config.ScaleF(30.0f), contentLeft + contentW, cardY + config.ScaleF(80.0f));
        const wchar_t* emptyMsg = L"No settings found matching your search query.";
        pRT->DrawText(emptyMsg, static_cast<UINT32>(wcslen(emptyMsg)),
                      theme.fontBody.Get(), rcEmpty, theme.brushTextSecondary.Get());
    }

    // Comfortable spacious card height and margin
    const float cardH = config.ScaleF(86.0f);
    const float cardSpacing = config.ScaleF(12.0f);

    for (size_t idx : filteredIndices) {
        auto& it = m_items[idx];

        it.cardBounds = D2D1::RectF(contentLeft, cardY, contentLeft + contentW, cardY + cardH);

        bool isCardHovered = (m_hoveredCardIdx == static_cast<int>(idx));

        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(it.cardBounds, 8.0f, 8.0f);

        // Card Background: tactile modern card
        ID2D1Brush* pCardBg = theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushItemHover.Get();
        pRT->FillRoundedRectangle(rrcCard, pCardBg);
        ID2D1Brush* pCardBorder = isCardHovered
            ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get())
            : theme.brushBorderSubtle.Get();
        pRT->DrawRoundedRectangle(rrcCard, pCardBorder, isCardHovered ? 1.2f : 1.0f);

        // Right Control Placement
        const float ctrlRight = contentLeft + contentW - config.ScaleF(24.0f);
        const float ctrlCenterY = cardY + cardH / 2.0f;

        // Scope Tag Badge (e.g. [ EDITOR ], [ FILES ], [ APPEARANCE ])
        float scopeBadgeW = config.ScaleF(static_cast<float>(it.scope.length() * 7.0f + 14.0f));
        float scopeBadgeH = config.ScaleF(18.0f);
        D2D1_RECT_F rcScopeBadge = D2D1::RectF(contentLeft + config.ScaleF(20.0f), cardY + config.ScaleF(16.0f),
                                              contentLeft + config.ScaleF(20.0f) + scopeBadgeW, cardY + config.ScaleF(16.0f) + scopeBadgeH);
        D2D1_ROUNDED_RECT rrcScope = D2D1::RoundedRect(rcScopeBadge, 4.0f, 4.0f);
        pRT->FillRoundedRectangle(rrcScope, theme.brushHomeBadgeBg ? theme.brushHomeBadgeBg.Get() : theme.brushItemHover.Get());
        pRT->DrawText(it.scope.c_str(), static_cast<UINT32>(it.scope.length()),
                      theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontHint.Get(),
                      rcScopeBadge, pAccentBrush);

        // Setting Title
        float titleX = rcScopeBadge.right + config.ScaleF(12.0f);
        D2D1_RECT_F rcCardTitle = D2D1::RectF(titleX, cardY + config.ScaleF(14.0f), ctrlRight - config.ScaleF(180.0f), cardY + config.ScaleF(36.0f));
        pRT->DrawText(it.title.c_str(), static_cast<UINT32>(it.title.length()),
                      m_pFontTitle.Get() ? m_pFontTitle.Get() : theme.fontBody.Get(), rcCardTitle, theme.brushTextPrimary.Get());

        // Setting Description
        D2D1_RECT_F rcDesc = D2D1::RectF(contentLeft + config.ScaleF(20.0f), cardY + config.ScaleF(44.0f),
                                         ctrlRight - config.ScaleF(180.0f), cardY + cardH - config.ScaleF(12.0f));
        pRT->DrawText(it.description.c_str(), static_cast<UINT32>(it.description.length()),
                      m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get(),
                      rcDesc, theme.brushTextSecondary.Get());

        if (it.control == SettingControl::Toggle) {
            const float toggleW = config.ScaleF(46.0f);
            const float toggleH = config.ScaleF(26.0f);
            it.controlBounds = D2D1::RectF(ctrlRight - toggleW, ctrlCenterY - toggleH / 2.0f, ctrlRight, ctrlCenterY + toggleH / 2.0f);

            bool isChecked = false;
            if (it.id == L"editor.wordWrap") isChecked = config.wordWrap;
            else if (it.id == L"editor.minimap") isChecked = config.showMinimap;
            else if (it.id == L"editor.lineNumbers") isChecked = config.showLineNumbers;
            else if (it.id == L"editor.insertSpaces") isChecked = config.insertSpaces;
            else if (it.id == L"workbench.breadcrumbs") isChecked = config.showBreadcrumbs;
            else if (it.id == L"workbench.statusBar") isChecked = config.showStatusBar;
            else if (it.id == L"workbench.graphColored") isChecked = config.isGraphColored;

            DrawToggleSwitch(pRT, theme, config, it.controlBounds, isChecked, isCardHovered);
        } else if (it.control == SettingControl::NumberStepper) {
            const float stepperW = config.ScaleF(126.0f);
            const float stepperH = config.ScaleF(30.0f);
            it.controlBounds = D2D1::RectF(ctrlRight - stepperW, ctrlCenterY - stepperH / 2.0f, ctrlRight, ctrlCenterY + stepperH / 2.0f);

            std::wstring valStr;
            if (it.id == L"editor.fontSize") {
                valStr = std::to_wstring(static_cast<int>(config.fontSize)) + it.unit;
            } else if (it.id == L"editor.tabSize") {
                valStr = std::to_wstring(config.tabSize) + it.unit;
            } else if (it.id == L"files.autoSaveDelay") {
                valStr = std::to_wstring(config.autoSaveDelay) + it.unit;
            }

            bool hMinus = (isCardHovered && m_hoveredControlSubId == "minus");
            bool hPlus  = (isCardHovered && m_hoveredControlSubId == "plus");
            DrawStepper(pRT, theme, config, it, valStr, hMinus, hPlus);
        } else if (it.control == SettingControl::Dropdown) {
            const float dropW = config.ScaleF(160.0f);
            const float dropH = config.ScaleF(32.0f);
            it.controlBounds = D2D1::RectF(ctrlRight - dropW, ctrlCenterY - dropH / 2.0f, ctrlRight, ctrlCenterY + dropH / 2.0f);

            std::wstring valStr;
            if (it.id == L"files.autoSave") {
                valStr = config.autoSave;
            } else if (it.id == L"editor.fontFamily") {
                valStr = config.fontFamily;
            } else if (it.id == L"editor.renderWhitespace") {
                valStr = config.renderWhitespaceMode;
            } else if (it.id == L"editor.cursorStyle") {
                valStr = config.cursorStyle;
            }

            bool isOpen = (m_openDropdownItemIdx == static_cast<int>(idx));
            DrawDropdown(pRT, theme, config, it.controlBounds, valStr, isCardHovered, isOpen);
        }

        cardY += cardH + cardSpacing;
    }

    pRT->PopAxisAlignedClip();

    // 5. Update Max Scroll Offset and draw scrollbar if needed
    float totalContentH = (cardY + m_scrollOffsetY) - contentTop;
    m_maxScrollOffsetY = (std::max)(0.0f, totalContentH - contentH);

    if (m_maxScrollOffsetY > 0.0f) {
        float scrollTrackW = config.ScaleF(6.0f);
        float scrollTrackX = left + width - scrollTrackW - config.ScaleF(4.0f);
        D2D1_RECT_F rcTrack = D2D1::RectF(scrollTrackX, bodyTop + config.ScaleF(8.0f), scrollTrackX + scrollTrackW, top + height - config.ScaleF(8.0f));
        float trackH = rcTrack.bottom - rcTrack.top;
        float thumbH = (std::max)(config.ScaleF(30.0f), trackH * (contentH / totalContentH));
        float thumbY = rcTrack.top + (m_scrollOffsetY / m_maxScrollOffsetY) * (trackH - thumbH);
        D2D1_RECT_F rcThumb = D2D1::RectF(scrollTrackX, thumbY, scrollTrackX + scrollTrackW, thumbY + thumbH);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcThumb, 3.0f, 3.0f), theme.brushTextSecondary.Get());
    }

    // 6. Draw Open Floating Dropdown Menu (Rendered on top of cards outside clipping)
    if (m_openDropdownItemIdx >= 0 && m_openDropdownItemIdx < static_cast<int>(m_items.size())) {
        DrawDropdownPopup(pRT, theme, config);
    }
}

void SettingsView::DrawToggleSwitch(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    const D2D1_RECT_F& bounds, bool isChecked, bool isHovered) {
    const float w = bounds.right - bounds.left;
    const float h = bounds.bottom - bounds.top;
    const float r = h / 2.0f;

    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(bounds, r, r);

    // Pill background: Green/Purple accent when checked, muted grey when unchecked
    if (isChecked) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushActive;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.72f, 0.45f, 1.0f), &brushActive); // Modern active green
        pRT->FillRoundedRectangle(rrcPill, brushActive ? brushActive.Get() : theme.brushBrandAccent.Get());
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushInactive;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.28f, 1.0f), &brushInactive);
        pRT->FillRoundedRectangle(rrcPill, brushInactive ? brushInactive.Get() : theme.brushItemHover.Get());
    }

    pRT->DrawRoundedRectangle(rrcPill, isHovered ? theme.brushTextWhite.Get() : theme.brushBorderSubtle.Get(), 1.0f);

    // Sliding Circular Knob
    const float knobPad = config.ScaleF(2.5f);
    const float knobRadius = r - knobPad;
    float knobCenterX = isChecked ? (bounds.right - r) : (bounds.left + r);
    float knobCenterY = (bounds.top + bounds.bottom) / 2.0f;

    pRT->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(knobCenterX, knobCenterY), knobRadius, knobRadius),
        theme.brushTextWhite.Get()
    );
}

void SettingsView::DrawStepper(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               const SettingItem& item, const std::wstring& valStr, bool isHoveredMinus, bool isHoveredPlus) {
    const float btnW = config.ScaleF(30.0f);
    const float h = item.controlBounds.bottom - item.controlBounds.top;

    auto& itMutable = const_cast<SettingItem&>(item);
    itMutable.minusBounds = D2D1::RectF(item.controlBounds.left, item.controlBounds.top, item.controlBounds.left + btnW, item.controlBounds.bottom);
    itMutable.plusBounds  = D2D1::RectF(item.controlBounds.right - btnW, item.controlBounds.top, item.controlBounds.right, item.controlBounds.bottom);

    // Entire Stepper container frame
    D2D1_ROUNDED_RECT rrcFrame = D2D1::RoundedRect(item.controlBounds, 6.0f, 6.0f);
    pRT->FillRoundedRectangle(rrcFrame, theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushItemHover.Get());
    pRT->DrawRoundedRectangle(rrcFrame, theme.brushBorderSubtle.Get(), 1.0f);

    // Minus Button
    D2D1_ROUNDED_RECT rrcMinus = D2D1::RoundedRect(itMutable.minusBounds, 5.0f, 5.0f);
    if (isHoveredMinus) pRT->FillRoundedRectangle(rrcMinus, theme.brushItemHover.Get());
    float mcx = (itMutable.minusBounds.left + itMutable.minusBounds.right) / 2.0f;
    float mcy = (itMutable.minusBounds.top + itMutable.minusBounds.bottom) / 2.0f;
    pRT->DrawLine(D2D1::Point2F(mcx - 4.5f, mcy), D2D1::Point2F(mcx + 4.5f, mcy), theme.brushTextPrimary.Get(), 1.5f);

    // Value in center
    D2D1_RECT_F rcVal = D2D1::RectF(itMutable.minusBounds.right, item.controlBounds.top, itMutable.plusBounds.left, item.controlBounds.bottom);
    pRT->DrawText(valStr.c_str(), static_cast<UINT32>(valStr.length()),
                  theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                  rcVal, theme.brushTextPrimary.Get());

    // Plus Button
    D2D1_ROUNDED_RECT rrcPlus = D2D1::RoundedRect(itMutable.plusBounds, 5.0f, 5.0f);
    if (isHoveredPlus) pRT->FillRoundedRectangle(rrcPlus, theme.brushItemHover.Get());
    float pcx = (itMutable.plusBounds.left + itMutable.plusBounds.right) / 2.0f;
    float pcy = (itMutable.plusBounds.top + itMutable.plusBounds.bottom) / 2.0f;
    pRT->DrawLine(D2D1::Point2F(pcx - 4.5f, pcy), D2D1::Point2F(pcx + 4.5f, pcy), theme.brushTextPrimary.Get(), 1.5f);
    pRT->DrawLine(D2D1::Point2F(pcx, pcy - 4.5f), D2D1::Point2F(pcx, pcy + 4.5f), theme.brushTextPrimary.Get(), 1.5f);
}

void SettingsView::DrawDropdown(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                const D2D1_RECT_F& bounds, const std::wstring& valStr, bool isHovered, bool isOpen) {
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(bounds, 6.0f, 6.0f);
    ID2D1Brush* pBg = isOpen
        ? (theme.brushItemHover ? theme.brushItemHover.Get() : theme.brushDrawerBg.Get())
        : (theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushItemHover.Get());
    pRT->FillRoundedRectangle(rrc, pBg);

    ID2D1Brush* pBorder = (isOpen || isHovered)
        ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get())
        : theme.brushBorderSubtle.Get();
    pRT->DrawRoundedRectangle(rrc, pBorder, (isOpen || isHovered) ? 1.4f : 1.0f);

    // Selected value text
    D2D1_RECT_F rcText = D2D1::RectF(bounds.left + config.ScaleF(12.0f), bounds.top, bounds.right - config.ScaleF(28.0f), bounds.bottom);
    pRT->DrawText(valStr.c_str(), static_cast<UINT32>(valStr.length()),
                  theme.fontBody.Get(), rcText, theme.brushTextPrimary.Get());

    // Chevron glyph
    float cx = bounds.right - config.ScaleF(16.0f);
    float cy = (bounds.top + bounds.bottom) / 2.0f;
    if (isOpen) {
        // Chevron pointing up ▲
        D2D1_POINT_2F p1 = D2D1::Point2F(cx - 3.5f, cy + 2.0f);
        D2D1_POINT_2F p2 = D2D1::Point2F(cx, cy - 2.5f);
        D2D1_POINT_2F p3 = D2D1::Point2F(cx + 3.5f, cy + 2.0f);
        pRT->DrawLine(p1, p2, theme.brushTextPrimary.Get(), 1.5f);
        pRT->DrawLine(p2, p3, theme.brushTextPrimary.Get(), 1.5f);
    } else {
        // Chevron pointing down ▼
        D2D1_POINT_2F p1 = D2D1::Point2F(cx - 3.5f, cy - 2.5f);
        D2D1_POINT_2F p2 = D2D1::Point2F(cx, cy + 2.0f);
        D2D1_POINT_2F p3 = D2D1::Point2F(cx + 3.5f, cy - 2.5f);
        pRT->DrawLine(p1, p2, theme.brushTextSecondary.Get(), 1.5f);
        pRT->DrawLine(p2, p3, theme.brushTextSecondary.Get(), 1.5f);
    }
}

void SettingsView::DrawDropdownPopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    if (m_openDropdownItemIdx < 0 || m_openDropdownItemIdx >= static_cast<int>(m_items.size())) return;
    const auto& item = m_items[m_openDropdownItemIdx];
    if (item.options.empty()) return;

    // Get current value to display checkmark
    std::wstring curVal;
    if (item.id == L"files.autoSave") curVal = config.autoSave;
    else if (item.id == L"editor.fontFamily") curVal = config.fontFamily;
    else if (item.id == L"editor.renderWhitespace") curVal = config.renderWhitespaceMode;
    else if (item.id == L"editor.cursorStyle") curVal = config.cursorStyle;

    const float rowH = config.ScaleF(32.0f);
    const float padY = config.ScaleF(6.0f);
    const float menuH = item.options.size() * rowH + padY * 2.0f;
    const float menuW = (std::max)(item.controlBounds.right - item.controlBounds.left, config.ScaleF(180.0f));

    // Place menu directly below control button
    float menuLeft = item.controlBounds.right - menuW;
    float menuTop  = item.controlBounds.bottom + config.ScaleF(4.0f);
    // If it would exceed bottom of view, flip above
    if (menuTop + menuH > m_viewBounds.bottom - config.ScaleF(10.0f)) {
        menuTop = item.controlBounds.top - menuH - config.ScaleF(4.0f);
    }
    m_openDropdownBounds = D2D1::RectF(menuLeft, menuTop, menuLeft + menuW, menuTop + menuH);

    // Drop shadow
    D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(
        D2D1::RectF(menuLeft + 2.0f, menuTop + 3.0f, menuLeft + menuW + 2.0f, menuTop + menuH + 3.0f),
        8.0f, 8.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &shadowBrush);
    if (shadowBrush) pRT->FillRoundedRectangle(rrcShadow, shadowBrush.Get());

    // Menu background
    D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_openDropdownBounds, 8.0f, 8.0f);
    ID2D1Brush* pMenuBg = theme.brushMenuBg ? theme.brushMenuBg.Get() :
                          (theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushRailBg.Get());
    pRT->FillRoundedRectangle(rrcMenu, pMenuBg);
    pRT->DrawRoundedRectangle(rrcMenu, theme.brushMenuBorder ? theme.brushMenuBorder.Get() : theme.brushBorderSubtle.Get(), 1.0f);

    // Options rows
    float curRowY = menuTop + padY;
    for (size_t i = 0; i < item.options.size(); ++i) {
        D2D1_RECT_F rcRow = D2D1::RectF(menuLeft + config.ScaleF(6.0f), curRowY, menuLeft + menuW - config.ScaleF(6.0f), curRowY + rowH);
        bool isHovered = (m_hoveredDropdownOption == static_cast<int>(i));
        bool isSelected = (item.options[i] == curVal);

        if (isHovered) {
            D2D1_ROUNDED_RECT rrcRow = D2D1::RoundedRect(rcRow, 5.0f, 5.0f);
            pRT->FillRoundedRectangle(rrcRow, theme.brushItemHover.Get());
        }

        // Checkmark for active selection
        if (isSelected) {
            float checkX = rcRow.left + config.ScaleF(12.0f);
            float checkY = (rcRow.top + rcRow.bottom) / 2.0f;
            D2D1_POINT_2F p1 = D2D1::Point2F(checkX - 3.5f, checkY);
            D2D1_POINT_2F p2 = D2D1::Point2F(checkX - 0.5f, checkY + 3.5f);
            D2D1_POINT_2F p3 = D2D1::Point2F(checkX + 5.0f, checkY - 3.5f);
            ID2D1Brush* pAccent = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get();
            pRT->DrawLine(p1, p2, pAccent, 1.8f);
            pRT->DrawLine(p2, p3, pAccent, 1.8f);
        }

        // Option text
        D2D1_RECT_F rcOptText = D2D1::RectF(rcRow.left + config.ScaleF(26.0f), rcRow.top, rcRow.right - config.ScaleF(8.0f), rcRow.bottom);
        ID2D1Brush* pTextBrush = isSelected
            ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
            : (isHovered ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextPrimary.Get());
        pRT->DrawText(item.options[i].c_str(), static_cast<UINT32>(item.options[i].length()),
                      theme.fontBody.Get(), rcOptText, pTextBrush);

        curRowY += rowH;
    }
}

bool SettingsView::HandleMouseMove(POINT clientPt) {
    int oldCard = m_hoveredCardIdx;
    int oldCat = m_hoveredCategoryIdx;
    bool oldClear = m_hoveredSearchClear;
    std::string oldCtrlSub = m_hoveredControlSubId;
    int oldDropOpt = m_hoveredDropdownOption;

    m_hoveredCardIdx = -1;
    m_hoveredCategoryIdx = -1;
    m_hoveredSearchClear = false;
    m_hoveredControlSubId.clear();
    m_hoveredDropdownOption = -1;

    // Check open dropdown popup options hover first
    if (m_openDropdownItemIdx >= 0 && m_openDropdownItemIdx < static_cast<int>(m_items.size())) {
        if (clientPt.x >= m_openDropdownBounds.left && clientPt.x <= m_openDropdownBounds.right &&
            clientPt.y >= m_openDropdownBounds.top && clientPt.y <= m_openDropdownBounds.bottom) {
            float relY = clientPt.y - (m_openDropdownBounds.top + 6.0f);
            const float totalH = m_openDropdownBounds.bottom - m_openDropdownBounds.top - 12.0f;
            const size_t optCount = m_items[m_openDropdownItemIdx].options.size();
            if (relY >= 0.0f && totalH > 0.0f && optCount > 0) {
                int opt = static_cast<int>(relY / (totalH / static_cast<float>(optCount)));
                if (opt >= 0 && opt < static_cast<int>(optCount)) {
                    m_hoveredDropdownOption = opt;
                }
            }
            return (oldDropOpt != m_hoveredDropdownOption);
        }
    }

    // Check search clear
    if (m_searchClearBounds.right > m_searchClearBounds.left &&
        clientPt.x >= m_searchClearBounds.left && clientPt.x <= m_searchClearBounds.right &&
        clientPt.y >= m_searchClearBounds.top && clientPt.y <= m_searchClearBounds.bottom) {
        m_hoveredSearchClear = true;
    }

    // Check Category sidebar
    for (size_t i = 0; i < m_categoryBounds.size(); ++i) {
        const auto& rc = m_categoryBounds[i];
        if (clientPt.x >= rc.left && clientPt.x <= rc.right &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            m_hoveredCategoryIdx = static_cast<int>(i);
            break;
        }
    }

    // Check visible cards
    auto filtered = GetFilteredItemIndices();
    for (size_t idx : filtered) {
        const auto& it = m_items[idx];
        if (clientPt.x >= it.cardBounds.left && clientPt.x <= it.cardBounds.right &&
            clientPt.y >= it.cardBounds.top && clientPt.y <= it.cardBounds.bottom) {
            m_hoveredCardIdx = static_cast<int>(idx);

            // Check stepper sub controls
            if (it.control == SettingControl::NumberStepper) {
                if (clientPt.x >= it.minusBounds.left && clientPt.x <= it.minusBounds.right &&
                    clientPt.y >= it.minusBounds.top && clientPt.y <= it.minusBounds.bottom) {
                    m_hoveredControlSubId = "minus";
                } else if (clientPt.x >= it.plusBounds.left && clientPt.x <= it.plusBounds.right &&
                           clientPt.y >= it.plusBounds.top && clientPt.y <= it.plusBounds.bottom) {
                    m_hoveredControlSubId = "plus";
                }
            }
            break;
        }
    }

    return (oldCard != m_hoveredCardIdx || oldCat != m_hoveredCategoryIdx ||
            oldClear != m_hoveredSearchClear || oldCtrlSub != m_hoveredControlSubId ||
            oldDropOpt != m_hoveredDropdownOption);
}

bool SettingsView::HandleLButtonDown(POINT clientPt, app::AppConfig& config, Theme& theme, std::wstring& outModifiedSettingId) {
    // 0. Handle Click inside Open Dropdown Popup
    if (m_openDropdownItemIdx >= 0 && m_openDropdownItemIdx < static_cast<int>(m_items.size())) {
        auto& dropItem = m_items[m_openDropdownItemIdx];
        if (clientPt.x >= m_openDropdownBounds.left && clientPt.x <= m_openDropdownBounds.right &&
            clientPt.y >= m_openDropdownBounds.top && clientPt.y <= m_openDropdownBounds.bottom) {
            float relY = clientPt.y - (m_openDropdownBounds.top + 6.0f);
            const float totalH = m_openDropdownBounds.bottom - m_openDropdownBounds.top - 12.0f;
            const size_t optCount = dropItem.options.size();
            if (relY >= 0.0f && totalH > 0.0f && optCount > 0) {
                int optIdx = static_cast<int>(relY / (totalH / static_cast<float>(optCount)));
                if (optIdx >= 0 && optIdx < static_cast<int>(optCount)) {
                    const auto& selectedOpt = dropItem.options[optIdx];
                    if (dropItem.id == L"files.autoSave") {
                        config.autoSave = selectedOpt;
                    } else if (dropItem.id == L"editor.fontFamily") {
                        config.fontFamily = selectedOpt;
                    } else if (dropItem.id == L"editor.renderWhitespace") {
                        config.renderWhitespaceMode = selectedOpt;
                        config.renderWhitespace = (selectedOpt != L"none");
                    } else if (dropItem.id == L"editor.cursorStyle") {
                        config.cursorStyle = selectedOpt;
                    }
                    outModifiedSettingId = dropItem.id;
                }
            }
            m_openDropdownItemIdx = -1;
            m_hoveredDropdownOption = -1;
            return true;
        } else {
            // Click outside closes dropdown
            m_openDropdownItemIdx = -1;
            m_hoveredDropdownOption = -1;
        }
    }

    // 1. Search Box Click
    if (clientPt.x >= m_searchBoxBounds.left && clientPt.x <= m_searchBoxBounds.right &&
        clientPt.y >= m_searchBoxBounds.top && clientPt.y <= m_searchBoxBounds.bottom) {
        // If clicked clear
        if (m_hoveredSearchClear && !m_searchQuery.empty()) {
            m_searchQuery.clear();
            m_scrollOffsetY = 0.0f;
            return true;
        }
        m_isSearchFocused = true;
        return true;
    }
    m_isSearchFocused = false;

    // 2. Category Sidebar Click
    for (size_t i = 0; i < m_categoryBounds.size(); ++i) {
        const auto& rc = m_categoryBounds[i];
        if (clientPt.x >= rc.left && clientPt.x <= rc.right &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            m_activeCategory = static_cast<SettingCategory>(i);
            m_searchQuery.clear();
            m_scrollOffsetY = 0.0f;
            return true;
        }
    }

    // 3. Card Controls Click
    auto filtered = GetFilteredItemIndices();
    for (size_t idx : filtered) {
        auto& it = m_items[idx];
        if (clientPt.x >= it.cardBounds.left && clientPt.x <= it.cardBounds.right &&
            clientPt.y >= it.cardBounds.top && clientPt.y <= it.cardBounds.bottom) {

            // A. Toggle control
            if (it.control == SettingControl::Toggle) {
                if (clientPt.x >= it.controlBounds.left && clientPt.x <= it.controlBounds.right &&
                    clientPt.y >= it.controlBounds.top && clientPt.y <= it.controlBounds.bottom) {
                    if (it.id == L"editor.wordWrap") {
                        config.wordWrap = !config.wordWrap;
                    } else if (it.id == L"editor.minimap") {
                        config.showMinimap = !config.showMinimap;
                    } else if (it.id == L"editor.lineNumbers") {
                        config.showLineNumbers = !config.showLineNumbers;
                    } else if (it.id == L"editor.insertSpaces") {
                        config.insertSpaces = !config.insertSpaces;
                    } else if (it.id == L"workbench.breadcrumbs") {
                        config.showBreadcrumbs = !config.showBreadcrumbs;
                    } else if (it.id == L"workbench.statusBar") {
                        config.showStatusBar = !config.showStatusBar;
                    } else if (it.id == L"workbench.graphColored") {
                        config.isGraphColored = !config.isGraphColored;
                    }
                    outModifiedSettingId = it.id;
                    return true;
                }
            }

            // B. Number Stepper control
            if (it.control == SettingControl::NumberStepper) {
                bool isMinus = (clientPt.x >= it.minusBounds.left && clientPt.x <= it.minusBounds.right &&
                                clientPt.y >= it.minusBounds.top && clientPt.y <= it.minusBounds.bottom);
                bool isPlus  = (clientPt.x >= it.plusBounds.left && clientPt.x <= it.plusBounds.right &&
                                clientPt.y >= it.plusBounds.top && clientPt.y <= it.plusBounds.bottom);

                if (isMinus) {
                    if (it.id == L"editor.fontSize") {
                        config.fontSize = (std::max)(it.minVal, config.fontSize - it.stepVal);
                    } else if (it.id == L"editor.tabSize") {
                        config.tabSize = (std::max)(static_cast<int>(it.minVal), config.tabSize - static_cast<int>(it.stepVal));
                    } else if (it.id == L"files.autoSaveDelay") {
                        config.autoSaveDelay = (std::max)(static_cast<int>(it.minVal), config.autoSaveDelay - static_cast<int>(it.stepVal));
                    }
                    outModifiedSettingId = it.id;
                    return true;
                } else if (isPlus) {
                    if (it.id == L"editor.fontSize") {
                        config.fontSize = (std::min)(it.maxVal, config.fontSize + it.stepVal);
                    } else if (it.id == L"editor.tabSize") {
                        config.tabSize = (std::min)(static_cast<int>(it.maxVal), config.tabSize + static_cast<int>(it.stepVal));
                    } else if (it.id == L"files.autoSaveDelay") {
                        config.autoSaveDelay = (std::min)(static_cast<int>(it.maxVal), config.autoSaveDelay + static_cast<int>(it.stepVal));
                    }
                    outModifiedSettingId = it.id;
                    return true;
                }
            }

            // C. Dropdown Click: Toggle floating popup
            if (it.control == SettingControl::Dropdown) {
                if (clientPt.x >= it.controlBounds.left && clientPt.x <= it.controlBounds.right &&
                    clientPt.y >= it.controlBounds.top && clientPt.y <= it.controlBounds.bottom) {
                    if (m_openDropdownItemIdx == static_cast<int>(idx)) {
                        m_openDropdownItemIdx = -1;
                    } else {
                        m_openDropdownItemIdx = static_cast<int>(idx);
                    }
                    m_hoveredDropdownOption = -1;
                    return true;
                }
            }
        }
    }

    return false;
}

bool SettingsView::HandleMouseWheel(short delta) {
    if (m_maxScrollOffsetY <= 0.0f) return false;

    float oldOffset = m_scrollOffsetY;
    m_scrollOffsetY -= static_cast<float>(delta) * 0.5f;
    if (m_scrollOffsetY < 0.0f) m_scrollOffsetY = 0.0f;
    if (m_scrollOffsetY > m_maxScrollOffsetY) m_scrollOffsetY = m_maxScrollOffsetY;

    return (oldOffset != m_scrollOffsetY);
}

bool SettingsView::HandleChar(wchar_t ch) {
    if (!m_isSearchFocused) return false;

    if (ch >= 32 && ch != 127) {
        m_searchQuery.push_back(ch);
        m_scrollOffsetY = 0.0f;
        return true;
    }
    return false;
}

bool SettingsView::HandleKeyDown(WPARAM key) {
    if (key == VK_ESCAPE) {
        if (m_openDropdownItemIdx >= 0) {
            m_openDropdownItemIdx = -1;
            m_hoveredDropdownOption = -1;
            return true;
        }
        if (m_isSearchFocused) {
            m_searchQuery.clear();
            m_isSearchFocused = false;
            m_scrollOffsetY = 0.0f;
            return true;
        }
    }

    if (!m_isSearchFocused) return false;

    if (key == VK_BACK) {
        if (!m_searchQuery.empty()) {
            m_searchQuery.pop_back();
            m_scrollOffsetY = 0.0f;
            return true;
        }
    }
    return false;
}

LPCWSTR SettingsView::GetCursorForPoint(POINT clientPt) const {
    // 1. If dropdown is open, check dropdown popup menu
    if (m_openDropdownItemIdx >= 0) {
        if (clientPt.x >= m_openDropdownBounds.left && clientPt.x <= m_openDropdownBounds.right &&
            clientPt.y >= m_openDropdownBounds.top && clientPt.y <= m_openDropdownBounds.bottom) {
            return IDC_HAND;
        }
    }

    // 2. Search clear button
    if (m_searchClearBounds.right > m_searchClearBounds.left &&
        clientPt.x >= m_searchClearBounds.left && clientPt.x <= m_searchClearBounds.right &&
        clientPt.y >= m_searchClearBounds.top && clientPt.y <= m_searchClearBounds.bottom) {
        return IDC_HAND;
    }

    // 3. Search input box
    if (clientPt.x >= m_searchBoxBounds.left && clientPt.x <= m_searchBoxBounds.right &&
        clientPt.y >= m_searchBoxBounds.top && clientPt.y <= m_searchBoxBounds.bottom) {
        return IDC_IBEAM;
    }

    // 4. Sidebar categories
    for (const auto& rc : m_categoryBounds) {
        if (clientPt.x >= rc.left && clientPt.x <= rc.right &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            return IDC_HAND;
        }
    }

    // 5. Card interactive controls (toggle switch, stepper minus/plus, dropdown pill)
    auto filtered = GetFilteredItemIndices();
    for (size_t idx : filtered) {
        const auto& it = m_items[idx];
        if (clientPt.x >= it.cardBounds.left && clientPt.x <= it.cardBounds.right &&
            clientPt.y >= it.cardBounds.top && clientPt.y <= it.cardBounds.bottom) {
            if (it.control == SettingControl::Toggle) {
                if (clientPt.x >= it.controlBounds.left && clientPt.x <= it.controlBounds.right &&
                    clientPt.y >= it.controlBounds.top && clientPt.y <= it.controlBounds.bottom) {
                    return IDC_HAND;
                }
            } else if (it.control == SettingControl::NumberStepper) {
                if ((clientPt.x >= it.minusBounds.left && clientPt.x <= it.minusBounds.right &&
                     clientPt.y >= it.minusBounds.top && clientPt.y <= it.minusBounds.bottom) ||
                    (clientPt.x >= it.plusBounds.left && clientPt.x <= it.plusBounds.right &&
                     clientPt.y >= it.plusBounds.top && clientPt.y <= it.plusBounds.bottom)) {
                    return IDC_HAND;
                }
            } else if (it.control == SettingControl::Dropdown) {
                if (clientPt.x >= it.controlBounds.left && clientPt.x <= it.controlBounds.right &&
                    clientPt.y >= it.controlBounds.top && clientPt.y <= it.controlBounds.bottom) {
                    return IDC_HAND;
                }
            }
            break;
        }
    }

    return nullptr;
}

} // namespace phide::ui