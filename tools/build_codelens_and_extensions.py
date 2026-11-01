import os
import sys

# Script to inject CodeLens and Extensions features into the phide codebase
print("Starting implementation of CodeLens and VS Code Extensions...")

# -----------------------------------------------------------------------------
# 1. Update DrawerPanel.hpp
# -----------------------------------------------------------------------------
drawer_hpp_path = 'include/phide/ui/DrawerPanel.hpp'
with open(drawer_hpp_path, 'r', encoding='utf-8') as f:
    d_hpp = f.read()

target_d_render = 'void RenderSourceControlPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW);'
replace_d_render = '''void RenderSourceControlPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW);
    void RenderExtensionsPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW);'''

if target_d_render in d_hpp and 'RenderExtensionsPane' not in d_hpp:
    d_hpp = d_hpp.replace(target_d_render, replace_d_render, 1)

target_d_public = 'void SetSearchFocus(int inputId) noexcept { m_focusedSearchInput = inputId; }'
replace_d_public = '''void SetSearchFocus(int inputId) noexcept { m_focusedSearchInput = inputId; }

    struct ExtensionItemEntry {
        std::wstring id;
        std::wstring displayName;
        std::wstring publisher;
        std::wstring description;
        std::wstring installsStr;
        std::wstring ratingStr;
        std::wstring iconFile;
        bool isInstalled = true;
        D2D1_RECT_F bounds{};
    };

    void SetOnOpenExtensionDetail(std::function<void(const std::wstring&)> cb) { m_onOpenExtensionDetail = cb; }
    void InitializeExtensions();
    bool HandleExtensionsMouseDown(POINT pt);
    bool HandleExtensionsMouseWheel(short delta);
    bool HandleExtensionsMouseMove(POINT pt);'''

if target_d_public in d_hpp and 'SetOnOpenExtensionDetail' not in d_hpp:
    d_hpp = d_hpp.replace(target_d_public, replace_d_public, 1)

target_d_private = 'std::wstring m_gitBranch = L"master";'
replace_d_private = '''std::wstring m_gitBranch = L"master";
    std::vector<ExtensionItemEntry> m_extensionEntries;
    std::wstring m_extensionSearchQuery;
    bool m_isExtensionSearchFocused = false;
    float m_extensionsScrollY = 0.0f;
    std::function<void(const std::wstring&)> m_onOpenExtensionDetail;
    D2D1_RECT_F m_extensionSearchBounds{};
    int m_hoveredExtensionIdx = -1;'''

if target_d_private in d_hpp and 'm_extensionEntries' not in d_hpp:
    d_hpp = d_hpp.replace(target_d_private, replace_d_private, 1)

with open(drawer_hpp_path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(d_hpp)
print("Updated DrawerPanel.hpp")

# -----------------------------------------------------------------------------
# 2. Update DrawerPanel.cpp
# -----------------------------------------------------------------------------
drawer_cpp_path = 'src/ui/DrawerPanel.cpp'
with open(drawer_cpp_path, 'r', encoding='utf-8') as f:
    d_cpp = f.read()

# Call InitializeExtensions in DrawerPanel::DrawerPanel()
target_d_ctor = 'm_gitSplitY = 380.0f;'
replace_d_ctor = '''m_gitSplitY = 380.0f;
    InitializeExtensions();'''

if target_d_ctor in d_cpp and 'InitializeExtensions();' not in d_cpp:
    d_cpp = d_cpp.replace(target_d_ctor, replace_d_ctor, 1)

# In Render(): handle config.activeRailTab == 3 (Extensions)
target_d_tabs = '''        if (config.activeRailTab == 1) {
            RenderSearchPane(pRT, theme, config, clientHeight, currentDrawerW);
        } else if (config.activeRailTab == 2) {
            RenderSourceControlPane(pRT, theme, config, clientHeight, currentDrawerW);
        } else {
            RenderExplorerPane(pRT, theme, config, clientHeight, currentDrawerW);
        }'''

replace_d_tabs = '''        if (config.activeRailTab == 1) {
            RenderSearchPane(pRT, theme, config, clientHeight, currentDrawerW);
        } else if (config.activeRailTab == 2) {
            RenderSourceControlPane(pRT, theme, config, clientHeight, currentDrawerW);
        } else if (config.activeRailTab == 3) {
            RenderExtensionsPane(pRT, theme, config, clientHeight, currentDrawerW);
        } else {
            RenderExplorerPane(pRT, theme, config, clientHeight, currentDrawerW);
        }'''

if target_d_tabs in d_cpp:
    d_cpp = d_cpp.replace(target_d_tabs, replace_d_tabs, 1)

# In HandleMouseWheel: handle activeRailTab == 3
target_d_wheel = '''    if (activeRailTab == 1) {'''
replace_d_wheel = '''    if (activeRailTab == 3) {
        return HandleExtensionsMouseWheel(delta);
    }
    if (activeRailTab == 1) {'''

if target_d_wheel in d_cpp and 'activeRailTab == 3' not in d_cpp:
    d_cpp = d_cpp.replace(target_d_wheel, replace_d_wheel, 1)

# In HandleLButtonDown: check if activeRailTab == 3
target_d_click = '''    if (m_focusedSearchInput > 0) {
        m_focusedSearchInput = 0;
    }'''

replace_d_click = '''    if (m_config.activeRailTab == 3) {
        if (HandleExtensionsMouseDown(pt)) return true;
    }
    if (m_focusedSearchInput > 0) {
        m_focusedSearchInput = 0;
    }'''

if target_d_click in d_cpp and 'm_config.activeRailTab == 3' not in d_cpp:
    d_cpp = d_cpp.replace(target_d_click, replace_d_click, 1)

# Append InitializeExtensions, RenderExtensionsPane, HandleExtensionsMouseDown, HandleExtensionsMouseWheel
extensions_impl = '''
void DrawerPanel::InitializeExtensions() {
    m_extensionEntries.clear();

    // 1. ES7+ React/Redux/React-Native snippets (Image 2)
    m_extensionEntries.push_back({
        L"dsznajder.es7-react-js-snippets",
        L"ES7+ React/Redux/React-Native snippets",
        L"dsznajder",
        L"Extensions for React, React-Native and Redux in JS/TS with ES7+ syntax.",
        L"17.8M",
        L"4.5",
        L"icon.png",
        true,
        {}
    });

    // 2. Tailwind CSS IntelliSense (Image 3)
    m_extensionEntries.push_back({
        L"bradlc.vscode-tailwindcss",
        L"Tailwind CSS IntelliSense",
        L"Tailwind Labs",
        L"Intelligent Tailwind CSS tooling for VS Code",
        L"14.8M",
        L"5.0",
        L"icon.png",
        true,
        {}
    });

    // 3. YAML Language Support (Image 4)
    m_extensionEntries.push_back({
        L"redhat.vscode-yaml",
        L"YAML",
        L"Red Hat",
        L"YAML Language Support by Red Hat, with built-in Kubernetes syntax support",
        L"29.8M",
        L"4.5",
        L"icon.png",
        true,
        {}
    });

    // 4. GitLens (Image 5)
    m_extensionEntries.push_back({
        L"eamodio.gitlens",
        L"GitLens \u2014 Git supercharged",
        L"GitKraken",
        L"Supercharge Git within VS Code \u2014 Visualize code authorship at a glance via Git blame...",
        L"53.6M",
        L"4.5",
        L"icon.png",
        true,
        {}
    });
}

void DrawerPanel::RenderExtensionsPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW) {
    const float railW = config.ScaleF(static_cast<float>(config.railWidth));
    const float left = railW;
    const float headerTop = config.ScaleF(static_cast<float>(config.headerHeight)) + config.ScaleF(12.0f);

    // Section title: EXTENSIONS
    D2D1_RECT_F rcTitle = D2D1::RectF(left + config.ScaleF(14.0f), headerTop, left + currentDrawerW - config.ScaleF(14.0f), headerTop + config.ScaleF(20.0f));
    const wchar_t* titleStr = L"EXTENSIONS";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  theme.fontHint.Get() ? theme.fontHint.Get() : theme.fontBody.Get(),
                  rcTitle, theme.brushTextSecondary.Get());

    // Search Box (matching Image 2: "Search Extensions in Marketplace")
    float searchY = headerTop + config.ScaleF(24.0f);
    float searchH = config.ScaleF(28.0f);
    m_extensionSearchBounds = D2D1::RectF(left + config.ScaleF(12.0f), searchY, left + currentDrawerW - config.ScaleF(12.0f), searchY + searchH);
    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(m_extensionSearchBounds, 4.0f, 4.0f);

    pRT->FillRoundedRectangle(rrcSearch, theme.brushItemHover.Get());
    pRT->DrawRoundedRectangle(rrcSearch, m_isExtensionSearchFocused ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get()) : theme.brushBorderSubtle.Get(), 1.0f);

    // Search Icon
    D2D1_RECT_F rcIcon = D2D1::RectF(m_extensionSearchBounds.left + config.ScaleF(6.0f), searchY + config.ScaleF(5.0f),
                                     m_extensionSearchBounds.left + config.ScaleF(22.0f), searchY + searchH - config.ScaleF(5.0f));
    m_icons.DrawSearch(pRT, rcIcon, theme.brushTextSecondary.Get(), 1.2f);

    // Search Text / Placeholder
    D2D1_RECT_F rcSearchText = D2D1::RectF(m_extensionSearchBounds.left + config.ScaleF(26.0f), searchY + config.ScaleF(4.0f),
                                          m_extensionSearchBounds.right - config.ScaleF(6.0f), searchY + searchH - config.ScaleF(4.0f));
    if (m_extensionSearchQuery.empty()) {
        const wchar_t* phStr = L"Search Extensions in Marketplace...";
        pRT->DrawText(phStr, static_cast<UINT32>(wcslen(phStr)),
                      theme.fontHint.Get() ? theme.fontHint.Get() : theme.fontBody.Get(),
                      rcSearchText, theme.brushTextSecondary.Get());
    } else {
        pRT->DrawText(m_extensionSearchQuery.c_str(), static_cast<UINT32>(m_extensionSearchQuery.length()),
                      theme.fontBody.Get() ? theme.fontBody.Get() : theme.fontHint.Get(),
                      rcSearchText, theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
    }

    // Installed Header & Count
    float listY = searchY + searchH + config.ScaleF(12.0f) - m_extensionsScrollY;
    D2D1_RECT_F rcInstalledHdr = D2D1::RectF(left + config.ScaleF(14.0f), listY, left + currentDrawerW - config.ScaleF(14.0f), listY + config.ScaleF(18.0f));
    std::wstring hdrStr = L"INSTALLED (" + std::to_wstring(m_extensionEntries.size()) + L")";
    pRT->DrawText(hdrStr.c_str(), static_cast<UINT32>(hdrStr.length()),
                  theme.fontHint.Get() ? theme.fontHint.Get() : theme.fontBody.Get(),
                  rcInstalledHdr, theme.brushTextSecondary.Get());
    listY += config.ScaleF(22.0f);

    // Extension Cards List (matching Image 2)
    const float cardH = config.ScaleF(66.0f);
    for (size_t i = 0; i < m_extensionEntries.size(); ++i) {
        auto& entry = m_extensionEntries[i];
        if (!m_extensionSearchQuery.empty()) {
            std::wstring qLower = m_extensionSearchQuery;
            std::wstring nLower = entry.displayName;
            for (auto& c : qLower) c = towlower(c);
            for (auto& c : nLower) c = towlower(c);
            if (nLower.find(qLower) == std::wstring::npos) continue;
        }

        entry.bounds = D2D1::RectF(left + config.ScaleF(6.0f), listY, left + currentDrawerW - config.ScaleF(6.0f), listY + cardH);

        bool isHovered = (static_cast<int>(i) == m_hoveredExtensionIdx);
        if (isHovered) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(entry.bounds, 5.0f, 5.0f), theme.brushItemHover.Get());
        }

        // Left Icon Box (38x38)
        float iconBoxSize = config.ScaleF(38.0f);
        float iconX = entry.bounds.left + config.ScaleF(8.0f);
        float iconY = entry.bounds.top + (cardH - iconBoxSize) / 2.0f;
        D2D1_RECT_F rcIconBox = D2D1::RectF(iconX, iconY, iconX + iconBoxSize, iconY + iconBoxSize);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcIconBox, 4.0f, 4.0f), theme.brushCurrentLineBg ? theme.brushCurrentLineBg.Get() : theme.brushItemHover.Get());
        m_icons.DrawExtensionsGrid(pRT, D2D1::RectF(iconX + config.ScaleF(8.0f), iconY + config.ScaleF(8.0f), iconX + iconBoxSize - config.ScaleF(8.0f), iconY + iconBoxSize - config.ScaleF(8.0f)),
                                  theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextSecondary.Get(), 1.3f);

        // Text Info (Right of Icon)
        float textX = iconX + iconBoxSize + config.ScaleF(10.0f);
        float textW = entry.bounds.right - textX - config.ScaleF(8.0f);

        // Display Name (12.5px, bold)
        D2D1_RECT_F rcName = D2D1::RectF(textX, entry.bounds.top + config.ScaleF(6.0f), textX + textW, entry.bounds.top + config.ScaleF(24.0f));
        pRT->DrawText(entry.displayName.c_str(), static_cast<UINT32>(entry.displayName.length()),
                      theme.fontTitle.Get() ? theme.fontTitle.Get() : theme.fontBody.Get(),
                      rcName, theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get(),
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);

        // Description (11px, secondary)
        D2D1_RECT_F rcDesc = D2D1::RectF(textX, entry.bounds.top + config.ScaleF(25.0f), textX + textW, entry.bounds.top + config.ScaleF(42.0f));
        pRT->DrawText(entry.description.c_str(), static_cast<UINT32>(entry.description.length()),
                      theme.fontHint.Get() ? theme.fontHint.Get() : theme.fontBody.Get(),
                      rcDesc, theme.brushTextSecondary.Get(),
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);

        // Publisher & Stats: e.g. "dsznajder \u2022 17.8M \u2022 \u2605 4.5"
        std::wstring statStr = entry.publisher + L"  \u2022  \u2913 " + entry.installsStr + L"  \u2022  \u2605 " + entry.ratingStr;
        D2D1_RECT_F rcStats = D2D1::RectF(textX, entry.bounds.top + config.ScaleF(44.0f), textX + textW, entry.bounds.top + config.ScaleF(60.0f));
        pRT->DrawText(statStr.c_str(), static_cast<UINT32>(statStr.length()),
                      theme.fontHint.Get() ? theme.fontHint.Get() : theme.fontBody.Get(),
                      rcStats, theme.brushTextSecondary.Get(),
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);

        listY += cardH + config.ScaleF(4.0f);
    }
}

bool DrawerPanel::HandleExtensionsMouseDown(POINT pt) {
    if (pt.x >= m_extensionSearchBounds.left && pt.x <= m_extensionSearchBounds.right &&
        pt.y >= m_extensionSearchBounds.top && pt.y <= m_extensionSearchBounds.bottom) {
        m_isExtensionSearchFocused = true;
        return true;
    }
    m_isExtensionSearchFocused = false;

    for (const auto& entry : m_extensionEntries) {
        if (pt.x >= entry.bounds.left && pt.x <= entry.bounds.right &&
            pt.y >= entry.bounds.top && pt.y <= entry.bounds.bottom) {
            if (m_onOpenExtensionDetail) {
                m_onOpenExtensionDetail(entry.id);
            }
            return true;
        }
    }
    return false;
}

bool DrawerPanel::HandleExtensionsMouseMove(POINT pt) {
    int oldHover = m_hoveredExtensionIdx;
    m_hoveredExtensionIdx = -1;
    for (size_t i = 0; i < m_extensionEntries.size(); ++i) {
        const auto& b = m_extensionEntries[i].bounds;
        if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
            m_hoveredExtensionIdx = static_cast<int>(i);
            break;
        }
    }
    return (m_hoveredExtensionIdx != oldHover);
}

bool DrawerPanel::HandleExtensionsMouseWheel(short delta) {
    if (delta > 0) {
        m_extensionsScrollY = (std::max)(0.0f, m_extensionsScrollY - 30.0f);
    } else {
        m_extensionsScrollY += 30.0f;
    }
    return true;
}
'''

if 'void DrawerPanel::RenderExtensionsPane' not in d_cpp:
    d_cpp += extensions_impl

with open(drawer_cpp_path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(d_cpp)
print("Updated DrawerPanel.cpp")

print("Drawer updates complete.")
