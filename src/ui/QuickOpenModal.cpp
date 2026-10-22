/**
 * @file QuickOpenModal.cpp
 * @brief Implementation of the Quick Open file search palette matching user reference Image 5.
 */

#include "phide/ui/QuickOpenModal.hpp"
#include <filesystem>
#include <algorithm>
#include <cwctype>

namespace phide::ui {

QuickOpenModal::QuickOpenModal() = default;

void QuickOpenModal::Open(const std::vector<std::wstring>& recentPaths,
                          const std::vector<std::wstring>& openPaths,
                          const std::wstring& workspaceFolder) {
    m_isOpen = true;
    m_query.clear();
    m_selectedIndex = 0;
    m_hoveredIndex = -1;
    m_scrollOffset = 0.0f;
    m_allItems.clear();

    // 1. Quick Navigation Actions (Image 5)
    QuickOpenItem actGoToFile;
    actGoToFile.type = QuickOpenItem::Type::Action;
    actGoToFile.title = L"Go to File";
    actGoToFile.description = L"";
    actGoToFile.shortcut = L"Ctrl + P";
    actGoToFile.iconType = FileIconType::DefaultFile;
    m_allItems.push_back(std::move(actGoToFile));

    QuickOpenItem actCommands;
    actCommands.type = QuickOpenItem::Type::Action;
    actCommands.title = L"Show and Run Commands >";
    actCommands.description = L"";
    actCommands.shortcut = L"Ctrl + Shift + P";
    actCommands.iconType = FileIconType::Terminal;
    m_allItems.push_back(std::move(actCommands));

    QuickOpenItem actSearchText;
    actSearchText.type = QuickOpenItem::Type::Action;
    actSearchText.title = L"Search for Text %";
    actSearchText.description = L"";
    actSearchText.shortcut = L"";
    actSearchText.iconType = FileIconType::DefaultFile;
    m_allItems.push_back(std::move(actSearchText));

    QuickOpenItem actGoToSymbol;
    actGoToSymbol.type = QuickOpenItem::Type::Action;
    actGoToSymbol.title = L"Go to Symbol in Editor @";
    actGoToSymbol.description = L"";
    actGoToSymbol.shortcut = L"Ctrl + Shift + O";
    actGoToSymbol.iconType = FileIconType::DefaultFile;
    m_allItems.push_back(std::move(actGoToSymbol));

    QuickOpenItem actDebug;
    actDebug.type = QuickOpenItem::Type::Action;
    actDebug.title = L"Start Debugging debug";
    actDebug.description = L"";
    actDebug.shortcut = L"";
    actDebug.iconType = FileIconType::DefaultFile;
    m_allItems.push_back(std::move(actDebug));

    QuickOpenItem actTask;
    actTask.type = QuickOpenItem::Type::Action;
    actTask.title = L"Run Task task";
    actTask.description = L"";
    actTask.shortcut = L"";
    actTask.iconType = FileIconType::DefaultFile;
    m_allItems.push_back(std::move(actTask));

    QuickOpenItem actMore;
    actMore.type = QuickOpenItem::Type::Action;
    actMore.title = L"More ?";
    actMore.description = L"";
    actMore.shortcut = L"";
    actMore.iconType = FileIconType::DefaultFile;
    m_allItems.push_back(std::move(actMore));

    // 2. Collect Open and Recent Files
    std::vector<std::wstring> combinedPaths = openPaths;
    for (const auto& rp : recentPaths) {
        if (std::find(combinedPaths.begin(), combinedPaths.end(), rp) == combinedPaths.end()) {
            combinedPaths.push_back(rp);
        }
    }

    for (const auto& path : combinedPaths) {
        if (path.empty()) continue;
        QuickOpenItem item;
        item.type = QuickOpenItem::Type::File;
        item.fullPath = path;
        item.isRecent = true;

        std::filesystem::path p(path);
        item.title = p.filename().wstring();

        // Calculate relative directory
        if (!workspaceFolder.empty()) {
            try {
                auto rel = std::filesystem::relative(p.parent_path(), workspaceFolder);
                item.description = rel.wstring();
            } catch (...) {
                item.description = p.parent_path().wstring();
            }
        } else {
            item.description = p.parent_path().wstring();
        }

        item.iconType = LucideIcons::GetIconTypeForPath(item.title, false, false);
        m_allItems.push_back(std::move(item));
    }

    // 3. Scan Workspace Files (up to 150 files, skipping noise directories)
    if (!workspaceFolder.empty() && std::filesystem::exists(workspaceFolder)) {
        size_t count = 0;
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     workspaceFolder, std::filesystem::directory_options::skip_permission_denied)) {
                if (count >= 150) break;
                if (!entry.is_regular_file()) continue;

                auto pathStr = entry.path().wstring();
                // Skip git, build outputs, node_modules
                if (pathStr.find(L"\\.git\\") != std::wstring::npos ||
                    pathStr.find(L"\\node_modules\\") != std::wstring::npos ||
                    pathStr.find(L"\\bin\\") != std::wstring::npos ||
                    pathStr.find(L"\\obj\\") != std::wstring::npos ||
                    pathStr.find(L"\\.vs\\") != std::wstring::npos) {
                    continue;
                }

                // If not already in list
                if (std::find(combinedPaths.begin(), combinedPaths.end(), pathStr) != combinedPaths.end()) {
                    continue;
                }

                QuickOpenItem item;
                item.type = QuickOpenItem::Type::File;
                item.fullPath = pathStr;
                item.isRecent = false;
                item.title = entry.path().filename().wstring();

                try {
                    auto rel = std::filesystem::relative(entry.path().parent_path(), workspaceFolder);
                    item.description = rel.wstring();
                } catch (...) {
                    item.description = entry.path().parent_path().wstring();
                }

                item.iconType = LucideIcons::GetIconTypeForPath(item.title, false, false);
                m_allItems.push_back(std::move(item));
                count++;
            }
        } catch (...) {}
    }

    FilterItems();
}

void QuickOpenModal::Close() noexcept {
    m_isOpen = false;
    m_query.clear();
    m_selectedIndex = 0;
    m_hoveredIndex = -1;
}

void QuickOpenModal::FilterItems() {
    m_filteredItems.clear();

    if (m_query.empty()) {
        m_filteredItems = m_allItems;
    } else {
        std::wstring qLower = m_query;
        for (auto& ch : qLower) ch = towlower(ch);

        for (const auto& item : m_allItems) {
            std::wstring tLower = item.title;
            std::wstring dLower = item.description;
            for (auto& ch : tLower) ch = towlower(ch);
            for (auto& ch : dLower) ch = towlower(ch);

            if (tLower.find(qLower) != std::wstring::npos || dLower.find(qLower) != std::wstring::npos) {
                m_filteredItems.push_back(item);
            }
        }
    }

    if (m_selectedIndex >= static_cast<int>(m_filteredItems.size())) {
        m_selectedIndex = m_filteredItems.empty() ? 0 : static_cast<int>(m_filteredItems.size() - 1);
    }
}

void QuickOpenModal::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientW, float clientH) {
    if (!m_isOpen) return;

    // 1. Semi-transparent Scrim Backdrop
    D2D1_RECT_F rcBackdrop = D2D1::RectF(0.0f, 0.0f, clientW, clientH);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> scrimBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f), &scrimBrush);
    if (scrimBrush) pRT->FillRectangle(rcBackdrop, scrimBrush.Get());

    // 2. Modal Box Bounds (Centered Horizontally at Top)
    float modalW = (std::min)(config.ScaleF(620.0f), clientW - config.ScaleF(40.0f));
    float modalH = (std::min)(config.ScaleF(440.0f), clientH - config.ScaleF(60.0f));
    float modalX = (clientW - modalW) / 2.0f;
    float modalY = config.ScaleF(static_cast<float>(config.headerHeight)) + config.ScaleF(6.0f);

    m_modalBounds = D2D1::RectF(modalX, modalY, modalX + modalW, modalY + modalH);
    D2D1_ROUNDED_RECT rrcModal = D2D1::RoundedRect(m_modalBounds, 6.0f, 6.0f);

    // Modal Background & Shadow Border
    D2D1_COLOR_F modalBg = theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.13f, 0.98f) : D2D1::ColorF(0.96f, 0.96f, 0.97f, 0.98f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(modalBg, &bgBrush);
    if (bgBrush) pRT->FillRoundedRectangle(&rrcModal, bgBrush.Get());
    pRT->DrawRoundedRectangle(&rrcModal, theme.brushBorderSubtle.Get(), 1.0f);

    // 3. Search Input Box at Top (Image 5)
    float inputPadX = config.ScaleF(10.0f);
    float inputY = modalY + config.ScaleF(10.0f);
    float inputH = config.ScaleF(34.0f);
    m_inputBounds = D2D1::RectF(modalX + inputPadX, inputY, modalX + modalW - inputPadX, inputY + inputH);
    D2D1_ROUNDED_RECT rrcInput = D2D1::RoundedRect(m_inputBounds, 4.0f, 4.0f);

    D2D1_COLOR_F inputBg = theme.IsDarkMode() ? D2D1::ColorF(0.19f, 0.19f, 0.20f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> inputBgBrush;
    pRT->CreateSolidColorBrush(inputBg, &inputBgBrush);
    if (inputBgBrush) pRT->FillRoundedRectangle(&rrcInput, inputBgBrush.Get());

    // Blue focus border (Image 5)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> inputFocusBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 1.0f), &inputFocusBorder);
    if (inputFocusBorder) pRT->DrawRoundedRectangle(&rrcInput, inputFocusBorder.Get(), 1.5f);

    // Text inside Search Box
    D2D1_RECT_F rcText = D2D1::RectF(m_inputBounds.left + config.ScaleF(8.0f), m_inputBounds.top + config.ScaleF(6.0f),
                                     m_inputBounds.right - config.ScaleF(8.0f), m_inputBounds.bottom);
    if (m_query.empty()) {
        const wchar_t* placeholder = L"Search files by name (append : to go to line or @ to go to symbol)";
        pRT->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)),
                      theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get(),
                      rcText, theme.brushTextMuted.Get());
    } else {
        pRT->DrawText(m_query.c_str(), static_cast<UINT32>(m_query.length()),
                      theme.fontBody.Get(), rcText, theme.brushTextPrimary.Get());
    }

    // 4. Results List
    float listTop = inputY + inputH + config.ScaleF(8.0f);
    float listBottom = modalY + modalH - config.ScaleF(8.0f);
    D2D1_RECT_F rcList = D2D1::RectF(modalX, listTop, modalX + modalW, listBottom);

    pRT->PushAxisAlignedClip(rcList, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float itemH = config.ScaleF(28.0f);
    float curY = listTop - m_scrollOffset;
    bool showedRecentHeader = false;

    for (size_t i = 0; i < m_filteredItems.size(); ++i) {
        auto& item = m_filteredItems[i];

        // Section header for "recently opened" (Image 5)
        if (m_query.empty() && item.type == QuickOpenItem::Type::File && !showedRecentHeader) {
            showedRecentHeader = true;
            curY += config.ScaleF(4.0f);
            D2D1_RECT_F rcHeader = D2D1::RectF(modalX + modalW - config.ScaleF(140.0f), curY, modalX + modalW - config.ScaleF(14.0f), curY + config.ScaleF(18.0f));
            const wchar_t* recentLabel = L"recently opened";
            pRT->DrawText(recentLabel, static_cast<UINT32>(wcslen(recentLabel)),
                          theme.fontTooltip ? theme.fontTooltip.Get() : theme.fontHint.Get(),
                          rcHeader, theme.brushTextMuted.Get());
            curY += config.ScaleF(18.0f);
        }

        item.bounds = D2D1::RectF(modalX + config.ScaleF(6.0f), curY, modalX + modalW - config.ScaleF(6.0f), curY + itemH);

        if (curY + itemH >= listTop && curY <= listBottom) {
            bool isSelected = (static_cast<int>(i) == m_selectedIndex);
            bool isHovered = (static_cast<int>(i) == m_hoveredIndex);

            D2D1_ROUNDED_RECT rrcItem = D2D1::RoundedRect(item.bounds, 3.0f, 3.0f);
            if (isSelected) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBg;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.35f), &selBg);
                if (selBg) pRT->FillRoundedRectangle(&rrcItem, selBg.Get());

                // Left active blue bar
                D2D1_RECT_F rcBar = D2D1::RectF(item.bounds.left, item.bounds.top, item.bounds.left + config.ScaleF(3.0f), item.bounds.bottom);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> barBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 1.0f), &barBrush);
                if (barBrush) pRT->FillRectangle(rcBar, barBrush.Get());
            } else if (isHovered) {
                pRT->FillRoundedRectangle(&rrcItem, theme.brushItemHover.Get());
            }

            // Draw File / Action Icon
            float iconSize = config.ScaleF(16.0f);
            float iconX = item.bounds.left + config.ScaleF(10.0f);
            float iconY = curY + (itemH - iconSize) / 2.0f;
            D2D1_RECT_F rcIcon = D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize);

            m_icons.DrawFileIcon(pRT, theme, rcIcon, item.iconType);

            // Title (File Name or Action Name)
            float textX = iconX + iconSize + config.ScaleF(8.0f);
            float textW = static_cast<float>(item.title.length()) * config.ScaleF(7.5f) + config.ScaleF(10.0f);
            D2D1_RECT_F rcTitle = D2D1::RectF(textX, curY + config.ScaleF(4.0f), textX + textW, curY + itemH);
            pRT->DrawText(item.title.c_str(), static_cast<UINT32>(item.title.length()),
                          theme.fontBody.Get(), rcTitle, theme.brushTextPrimary.Get());

            // Directory / Description (Muted Text next to file name)
            if (!item.description.empty()) {
                float descX = textX + textW + config.ScaleF(4.0f);
                float descW = (std::max)(10.0f, item.bounds.right - descX - config.ScaleF(10.0f));
                D2D1_RECT_F rcDesc = D2D1::RectF(descX, curY + config.ScaleF(5.0f), descX + descW, curY + itemH);
                pRT->DrawText(item.description.c_str(), static_cast<UINT32>(item.description.length()),
                              theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get(),
                              rcDesc, theme.brushTextMuted.Get());
            }

            // Shortcut Badge on Right (for Actions e.g. Ctrl + P)
            if (!item.shortcut.empty()) {
                float badgeW = static_cast<float>(item.shortcut.length()) * config.ScaleF(6.5f) + config.ScaleF(12.0f);
                float badgeH = config.ScaleF(18.0f);
                float badgeX = item.bounds.right - badgeW - config.ScaleF(8.0f);
                float badgeY = curY + (itemH - badgeH) / 2.0f;
                D2D1_RECT_F rcBadge = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
                D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcBadge, 3.0f, 3.0f);

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBg;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.2f, 0.22f, 0.7f), &badgeBg);
                if (badgeBg) pRT->FillRoundedRectangle(&rrcBadge, badgeBg.Get());
                pRT->DrawRoundedRectangle(&rrcBadge, theme.brushBorderSubtle.Get(), 0.8f);

                pRT->DrawText(item.shortcut.c_str(), static_cast<UINT32>(item.shortcut.length()),
                              theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontHint.Get(),
                              rcBadge, theme.brushTextMuted.Get());
            }
        }

        curY += itemH;
    }

    pRT->PopAxisAlignedClip();
}

bool QuickOpenModal::HandleMouseMove(POINT pt) {
    if (!m_isOpen) return false;

    int prevHover = m_hoveredIndex;
    m_hoveredIndex = -1;

    for (size_t i = 0; i < m_filteredItems.size(); ++i) {
        const auto& b = m_filteredItems[i].bounds;
        if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
            m_hoveredIndex = static_cast<int>(i);
            break;
        }
    }
    return (m_hoveredIndex != prevHover);
}

bool QuickOpenModal::HandleMouseDown(POINT pt, std::wstring& outSelectedFile) {
    if (!m_isOpen) return false;

    // Check click outside modal dialog
    if (pt.x < m_modalBounds.left || pt.x > m_modalBounds.right ||
        pt.y < m_modalBounds.top || pt.y > m_modalBounds.bottom) {
        Close();
        return true;
    }

    for (size_t i = 0; i < m_filteredItems.size(); ++i) {
        const auto& b = m_filteredItems[i].bounds;
        if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
            m_selectedIndex = static_cast<int>(i);
            if (m_filteredItems[i].type == QuickOpenItem::Type::File) {
                outSelectedFile = m_filteredItems[i].fullPath;
                Close();
                return true;
            } else {
                if (m_filteredItems[i].title.find(L"Show and Run Commands") != std::wstring::npos) {
                    m_query = L">";
                    FilterItems();
                    return true;
                }
            }
            break;
        }
    }
    return true;
}

bool QuickOpenModal::HandleKeyDown(WPARAM wParam, std::wstring& outSelectedFile) {
    if (!m_isOpen) return false;

    if (wParam == VK_ESCAPE) {
        Close();
        return true;
    }

    if (wParam == VK_UP) {
        if (!m_filteredItems.empty()) {
            m_selectedIndex = (m_selectedIndex > 0) ? m_selectedIndex - 1 : static_cast<int>(m_filteredItems.size() - 1);
        }
        return true;
    }

    if (wParam == VK_DOWN) {
        if (!m_filteredItems.empty()) {
            m_selectedIndex = (m_selectedIndex + 1 < static_cast<int>(m_filteredItems.size())) ? m_selectedIndex + 1 : 0;
        }
        return true;
    }

    if (wParam == VK_RETURN) {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredItems.size())) {
            const auto& item = m_filteredItems[m_selectedIndex];
            if (item.type == QuickOpenItem::Type::File) {
                outSelectedFile = item.fullPath;
                Close();
                return true;
            } else if (item.title.find(L"Show and Run Commands") != std::wstring::npos) {
                m_query = L">";
                FilterItems();
                return true;
            }
        }
        return true;
    }

    if (wParam == VK_BACK) {
        if (!m_query.empty()) {
            m_query.pop_back();
            FilterItems();
            return true;
        }
    }

    return false;
}

bool QuickOpenModal::HandleChar(wchar_t ch) {
    if (!m_isOpen) return false;

    if (ch >= 32 && ch != 127) {
        m_query.push_back(ch);
        FilterItems();
        return true;
    }
    return false;
}

bool QuickOpenModal::HandleMouseWheel(short delta) {
    if (!m_isOpen) return false;

    m_scrollOffset -= static_cast<float>(delta) * 0.4f;
    if (m_scrollOffset < 0.0f) m_scrollOffset = 0.0f;
    return true;
}

} // namespace phide::ui
