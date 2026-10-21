/**
 * @file DrawerPanel.cpp
 * @brief Implementation of the slide-out sidebar drawer view with real filesystem browsing.
 */

#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/ActivityRail.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace phide::ui {

DrawerPanel::DrawerPanel() {
    // Starts with no folder opened - mock tree is completely removed
    m_hasOpenFolder = false;
    m_visibleItems.clear();
}

void DrawerPanel::OpenFolder(const std::wstring& folderPath, const std::set<std::wstring>* pInitialExpanded) {
    if (folderPath.empty()) return;

    std::error_code ec;
    std::filesystem::path fsPath(folderPath);
    if (!std::filesystem::exists(fsPath, ec) || !std::filesystem::is_directory(fsPath, ec)) {
        return;
    }

    m_openFolderPath = std::filesystem::canonical(fsPath, ec).wstring();
    if (m_openFolderPath.empty()) m_openFolderPath = folderPath;

    // Extract folder name
    std::filesystem::path canon(m_openFolderPath);
    m_rootName = canon.filename().wstring();
    if (m_rootName.empty()) {
        m_rootName = canon.parent_path().filename().wstring();
    }
    if (m_rootName.empty()) {
        m_rootName = m_openFolderPath;
    }

    m_hasOpenFolder = true;
    m_expandedDirs.clear();
    if (pInitialExpanded && !pInitialExpanded->empty()) {
        m_expandedDirs = *pInitialExpanded;
        m_expandedDirs.insert(m_openFolderPath);
    } else {
        m_expandedDirs.insert(m_openFolderPath);
    }

    RebuildVisibleItems();
}

void DrawerPanel::SetExpandedDirs(const std::set<std::wstring>& dirs) {
    m_expandedDirs = dirs;
    if (m_hasOpenFolder && !m_openFolderPath.empty()) {
        m_expandedDirs.insert(m_openFolderPath);
    }
    RebuildVisibleItems();
}

void DrawerPanel::CloseFolder() {
    m_hasOpenFolder = false;
    m_openFolderPath.clear();
    m_rootName.clear();
    m_expandedDirs.clear();
    m_visibleItems.clear();
    m_hoveredIndex = -1;
}

void DrawerPanel::RefreshTree() {
    if (m_hasOpenFolder) {
        RebuildVisibleItems();
    }
}

void DrawerPanel::ToggleFolder(int index) {
    if (index >= 0 && index < static_cast<int>(m_visibleItems.size())) {
        auto& item = m_visibleItems[index];
        if (item.isFolder) {
            if (m_expandedDirs.count(item.fullPath) > 0) {
                m_expandedDirs.erase(item.fullPath);
            } else {
                m_expandedDirs.insert(item.fullPath);
            }
            RebuildVisibleItems();
        }
    }
}

std::wstring DrawerPanel::GetIconForFile(const std::wstring& filename, bool isDir, bool isExpanded) const {
    if (isDir) {
        return isExpanded ? L"\u25BE \u25A4" : L"\u25B8 \u25A4";
    }

    // Extract extension in lowercase
    auto dot = filename.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = filename.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

        if (ext == L".cpp" || ext == L".cxx" || ext == L".cc" || ext == L".c") return L"\u2699";
        if (ext == L".hpp" || ext == L".hxx" || ext == L".hh" || ext == L".h") return L"\u25A1";
        if (ext == L".php" || ext == L".phtml") return L"\u272A";
        if (ext == L".json" || ext == L".yaml" || ext == L".yml" || ext == L".xml" || ext == L".toml" || ext == L".ini") return L"\u2261";
        if (ext == L".md" || ext == L".txt" || ext == L".rst" || ext == L".log") return L"\u2261";
        if (ext == L".bat" || ext == L".cmd" || ext == L".sh" || ext == L".ps1") return L"\u26A1";
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" || ext == L".ico" || ext == L".svg") return L"\u25A3";
        if (ext == L".py") return L"\u26A1";
        if (ext == L".js" || ext == L".ts" || ext == L".html" || ext == L".css") return L"\u25CE";
    }

    if (filename == L"CMakeLists.txt" || filename == L"Makefile") return L"\u2699";
    if (filename == L"composer.json" || filename == L"package.json") return L"\u2261";

    return L"\u25AB";
}

void DrawerPanel::RebuildVisibleItems() {
    m_visibleItems.clear();
    if (!m_hasOpenFolder || m_openFolderPath.empty()) return;

    // Root folder item
    DrawerTreeItem rootItem;
    rootItem.label = m_rootName;
    rootItem.fullPath = m_openFolderPath;
    rootItem.depth = 0;
    rootItem.isFolder = true;
    rootItem.isExpanded = (m_expandedDirs.count(m_openFolderPath) > 0);
    rootItem.iconGlyph = GetIconForFile(m_rootName, true, rootItem.isExpanded);
    m_visibleItems.push_back(rootItem);

    if (rootItem.isExpanded) {
        AddDirectoryChildren(m_openFolderPath, 1);
    }
}

void DrawerPanel::AddDirectoryChildren(const std::wstring& dirPath, int depth) {
    std::error_code ec;
    std::filesystem::path fsPath(dirPath);
    if (!std::filesystem::exists(fsPath, ec) || !std::filesystem::is_directory(fsPath, ec)) {
        return;
    }

    struct EntryInfo {
        std::wstring name;
        std::wstring fullPath;
        bool isDir;
    };
    std::vector<EntryInfo> folders;
    std::vector<EntryInfo> files;

    for (const auto& entry : std::filesystem::directory_iterator(fsPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
        std::wstring name = entry.path().filename().wstring();
        if (name.empty() || name[0] == L'.') continue; // Skip hidden
        if (name == L"bin" || name == L"obj" || name == L"build" || name == L"node_modules" || name == L".git" || name == L".vs") {
            continue;
        }

        bool isDir = entry.is_directory(ec);
        if (isDir) {
            folders.push_back({ name, entry.path().wstring(), true });
        } else {
            files.push_back({ name, entry.path().wstring(), false });
        }
    }

    auto cmp = [](const EntryInfo& a, const EntryInfo& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    };
    std::sort(folders.begin(), folders.end(), cmp);
    std::sort(files.begin(), files.end(), cmp);

    // Add folders first
    for (const auto& f : folders) {
        DrawerTreeItem item;
        item.label = f.name;
        item.fullPath = f.fullPath;
        item.depth = depth;
        item.isFolder = true;
        item.isExpanded = (m_expandedDirs.count(f.fullPath) > 0);
        item.iconGlyph = GetIconForFile(f.name, true, item.isExpanded);
        m_visibleItems.push_back(item);

        if (item.isExpanded) {
            AddDirectoryChildren(f.fullPath, depth + 1);
        }
    }

    // Add files next
    for (const auto& f : files) {
        DrawerTreeItem item;
        item.label = f.name;
        item.fullPath = f.fullPath;
        item.depth = depth;
        item.isFolder = false;
        item.isExpanded = false;
        item.iconGlyph = GetIconForFile(f.name, false, false);
        m_visibleItems.push_back(item);
    }
}

void DrawerPanel::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW) {
    if (!pRT || currentDrawerW <= 0.0f) return;

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float cornerRadius = config.ScaleF(14.0f);

    // Check if cached geometry needs rebuilding
    if (!m_pDrawerBgGeometry ||
        m_cachedRailW != railW ||
        m_cachedHeaderH != headerH ||
        m_cachedDrawerW != currentDrawerW ||
        m_cachedClientH != clientHeight)
    {
        m_cachedRailW   = railW;
        m_cachedHeaderH = headerH;
        m_cachedDrawerW = currentDrawerW;
        m_cachedClientH = clientHeight;
        m_pDrawerBgGeometry.Reset();

        Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
        pRT->GetFactory(&pFactory);
        if (pFactory) {
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
            if (SUCCEEDED(pFactory->CreatePathGeometry(&pPath))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pPath->Open(&sink))) {
                    const float maxR = (std::min)(currentDrawerW, (clientHeight - headerH) / 2.0f);
                    const float r = (maxR > 0.0f) ? (std::min)(cornerRadius, maxR) : 0.0f;

                    sink->BeginFigure(D2D1::Point2F(railW + r, headerH), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(railW + currentDrawerW, headerH));
                    sink->AddLine(D2D1::Point2F(railW + currentDrawerW, clientHeight));
                    sink->AddLine(D2D1::Point2F(railW, clientHeight));
                    sink->AddLine(D2D1::Point2F(railW, headerH + r));

                    if (r > 0.0f) {
                        D2D1_ARC_SEGMENT arc = {};
                        arc.point = D2D1::Point2F(railW + r, headerH);
                        arc.size = D2D1::SizeF(r, r);
                        arc.rotationAngle = 0.0f;
                        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                        arc.arcSize = D2D1_ARC_SIZE_SMALL;
                        sink->AddArc(arc);
                    }

                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();

                    m_pDrawerBgGeometry = pPath;
                }
            }
        }
    }

    // 1. Fill Drawer Background with upper-left curve
    if (m_pDrawerBgGeometry) {
        pRT->FillGeometry(m_pDrawerBgGeometry.Get(), theme.brushDrawerBg.Get());
    } else {
        D2D1_RECT_F rcDrawer = D2D1::RectF(railW, headerH, railW + currentDrawerW, clientHeight);
        pRT->FillRectangle(rcDrawer, theme.brushDrawerBg.Get());
    }

    // Right border divider
    pRT->DrawLine(
        D2D1::Point2F(railW + currentDrawerW - 0.5f, headerH),
        D2D1::Point2F(railW + currentDrawerW - 0.5f, clientHeight),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    // 2. Render Drawer Contents (Explorer or Code Search)
    const float minContentW = config.ScaleF(40.0f);

    if (currentDrawerW >= minContentW) {
        D2D1_RECT_F clipRect = D2D1::RectF(railW, headerH, railW + currentDrawerW, clientHeight);
        pRT->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (config.activeRailTab == 1) {
            RenderSearchPane(pRT, theme, config, clientHeight, currentDrawerW);
        } else {
            RenderExplorerPane(pRT, theme, config, clientHeight, currentDrawerW);
        }

        pRT->PopAxisAlignedClip();
    }
}

void DrawerPanel::RenderExplorerPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW) {
    (void)currentDrawerW;
    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float fullDrawerW = config.ScaleF(static_cast<float>(config.drawerWidth));
    float curY = headerH + config.ScaleF(14.0f);
    const float paddingX = config.ScaleF(16.0f);

        if (!m_hasOpenFolder) {
            // === NO FOLDER OPENED STATE ===
            D2D1_RECT_F rcTitle = D2D1::RectF(
                railW + paddingX,
                curY,
                railW + fullDrawerW - paddingX,
                curY + config.ScaleF(20.0f)
            );
            const wchar_t* title = L"EXPLORER";
            pRT->DrawText(title, static_cast<UINT32>(wcslen(title)),
                          theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get(),
                          rcTitle, theme.brushTextPrimary.Get());

            curY += config.ScaleF(32.0f);

            // Subtitle
            D2D1_RECT_F rcSubtitle = D2D1::RectF(
                railW + paddingX,
                curY,
                railW + fullDrawerW - paddingX,
                curY + config.ScaleF(36.0f)
            );
            const wchar_t* subText = L"You have not yet opened a folder.";
            pRT->DrawText(subText, static_cast<UINT32>(wcslen(subText)),
                          theme.fontBody.Get(),
                          rcSubtitle, theme.brushTextSecondary.Get());

            curY += config.ScaleF(46.0f);

            // "Open Folder" Action Button
            const float btnW = fullDrawerW - paddingX * 2.0f;
            const float btnH = config.ScaleF(32.0f);
            m_rcOpenFolderBtn = D2D1::RectF(
                railW + paddingX,
                curY,
                railW + paddingX + btnW,
                curY + btnH
            );

            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_rcOpenFolderBtn, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrc, m_hoveredOpenFolderBtn ? theme.brushItemHover.Get() : theme.brushMenuHover.Get());
            pRT->DrawRoundedRectangle(rrc, theme.brushMenuHover.Get(), 1.0f);

            // Perfectly centered text inside button
            const wchar_t* btnLabel = L"Open Folder";
            pRT->DrawText(btnLabel, static_cast<UINT32>(wcslen(btnLabel)),
                          theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                          m_rcOpenFolderBtn, theme.brushTextWhite.Get());

        } else {
            // === FOLDER OPENED STATE ===
            std::wstring title = L"EXPLORER";
            if (!m_rootName.empty()) {
                title += L": " + m_rootName;
            }

            D2D1_RECT_F rcTitle = D2D1::RectF(
                railW + paddingX,
                curY,
                railW + fullDrawerW - paddingX,
                curY + config.ScaleF(20.0f)
            );
            pRT->DrawText(title.c_str(), static_cast<UINT32>(title.length()),
                          theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get(),
                          rcTitle, theme.brushTextPrimary.Get());

            curY += config.ScaleF(30.0f);

            // Clamp vertical scroll index to visible items bounds
            const float itemH = config.ScaleF(24.0f);
            const float availableTreeH = clientHeight - curY;
            const size_t visibleSlots = (itemH > 0.0f) ? static_cast<size_t>(availableTreeH / itemH) : 10;
            if (m_visibleItems.size() <= visibleSlots) {
                m_scrollIndex = 0;
            } else if (m_scrollIndex > m_visibleItems.size() - visibleSlots) {
                m_scrollIndex = m_visibleItems.size() - visibleSlots;
            }

            // Invalidate bounds of scrolled-out items above viewport
            for (size_t i = 0; i < m_scrollIndex; ++i) {
                m_visibleItems[i].bounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
            }

            // Tree Items with Seti/Material Icons & Indentation Guidelines
            for (size_t i = m_scrollIndex; i < m_visibleItems.size(); ++i) {
                auto& item = m_visibleItems[i];
                if (curY + itemH > clientHeight) {
                    item.bounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
                    continue;
                }
                item.bounds = D2D1::RectF(railW + config.ScaleF(4.0f), curY, railW + fullDrawerW - config.ScaleF(4.0f), curY + itemH);

                bool isHovered = (m_hoveredIndex == static_cast<int>(i));
                if (isHovered) {
                    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 3.5f, 3.5f);
                    pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
                }

                float indent = config.ScaleF(static_cast<float>(item.depth * 14));

                // 1. Draw vertical tree indent guidelines for depth > 0
                for (int d = 1; d <= item.depth; ++d) {
                    float guideX = railW + paddingX + config.ScaleF(static_cast<float>((d - 1) * 14)) + config.ScaleF(2.0f);
                    pRT->DrawLine(
                        D2D1::Point2F(guideX, curY),
                        D2D1::Point2F(guideX, curY + itemH),
                        theme.brushBorderSubtle.Get(),
                        1.0f
                    );
                }

                float cy = curY + itemH / 2.0f;
                float chevronX = railW + config.ScaleF(14.0f) + indent;

                // 2. Folder expand/collapse chevron
                if (item.isFolder) {
                    if (item.isExpanded) {
                        // Down chevron (v)
                        pRT->DrawLine(
                            D2D1::Point2F(chevronX - config.ScaleF(3.5f), cy - config.ScaleF(1.5f)),
                            D2D1::Point2F(chevronX, cy + config.ScaleF(2.0f)),
                            theme.brushTextSecondary.Get(), 1.3f
                        );
                        pRT->DrawLine(
                            D2D1::Point2F(chevronX, cy + config.ScaleF(2.0f)),
                            D2D1::Point2F(chevronX + config.ScaleF(3.5f), cy - config.ScaleF(1.5f)),
                            theme.brushTextSecondary.Get(), 1.3f
                        );
                    } else {
                        // Right chevron (>)
                        pRT->DrawLine(
                            D2D1::Point2F(chevronX - config.ScaleF(2.0f), cy - config.ScaleF(3.5f)),
                            D2D1::Point2F(chevronX + config.ScaleF(2.0f), cy),
                            theme.brushTextSecondary.Get(), 1.3f
                        );
                        pRT->DrawLine(
                            D2D1::Point2F(chevronX + config.ScaleF(2.0f), cy),
                            D2D1::Point2F(chevronX - config.ScaleF(2.0f), cy + config.ScaleF(3.5f)),
                            theme.brushTextSecondary.Get(), 1.3f
                        );
                    }
                }

                // 3. Official VS Code Seti File/Folder Icon
                float iconX = chevronX + config.ScaleF(10.0f);
                D2D1_RECT_F iconBounds = D2D1::RectF(
                    iconX,
                    cy - config.ScaleF(8.0f),
                    iconX + config.ScaleF(16.0f),
                    cy + config.ScaleF(8.0f)
                );
                auto setiInfo = theme.GetSetiIcon(item.label, item.isFolder, item.isExpanded);
                if (theme.fontSeti && setiInfo.pBrush) {
                    pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), iconBounds, setiInfo.pBrush);
                } else {
                    auto iconType = LucideIcons::GetIconTypeForPath(item.label, item.isFolder, item.isExpanded);
                    m_icons.DrawFileIcon(pRT, theme, iconBounds, iconType);
                }

                // 4. Draw Item Text Label
                float labelX = iconX + config.ScaleF(22.0f);
                D2D1_RECT_F rcLabel = D2D1::RectF(
                    labelX,
                    curY,
                    railW + fullDrawerW - config.ScaleF(10.0f),
                    curY + itemH
                );
                ID2D1Brush* pTextBrush = theme.IsDarkMode() ? 
                    (isHovered ? theme.brushTextWhite.Get() : (item.isFolder ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())) :
                    theme.brushTextPrimary.Get();
                pRT->DrawText(
                    item.label.c_str(),
                    static_cast<UINT32>(item.label.length()),
                    theme.fontBody.Get(),
                    rcLabel,
                    pTextBrush
                );

                curY += itemH;
            }
        }
    }

void DrawerPanel::RenderSearchPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW) {
    (void)currentDrawerW;
    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float fullDrawerW = config.ScaleF(static_cast<float>(config.drawerWidth));
    const float paddingX = config.ScaleF(14.0f);

    float curY = headerH + config.ScaleF(14.0f);

    // 1. Header: "Code Search" (matching Image 2) + Refresh (↻) and Clear (✕)
    D2D1_RECT_F rcTitle = D2D1::RectF(
        railW + paddingX,
        curY,
        railW + fullDrawerW - config.ScaleF(60.0f),
        curY + config.ScaleF(20.0f)
    );
    const wchar_t* title = L"Code Search";
    pRT->DrawText(title, static_cast<UINT32>(wcslen(title)),
                  theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get(),
                  rcTitle, theme.brushTextPrimary.Get());

    // Refresh icon button (↻)
    m_rcRefreshSearchBtn = D2D1::RectF(
        railW + fullDrawerW - config.ScaleF(48.0f),
        curY - config.ScaleF(1.0f),
        railW + fullDrawerW - config.ScaleF(28.0f),
        curY + config.ScaleF(19.0f)
    );
    const wchar_t* refreshGlyph = L"\u21BB";
    pRT->DrawText(refreshGlyph, 1, theme.fontBody.Get(), m_rcRefreshSearchBtn, theme.brushTextSecondary.Get());

    // Clear search button (✕)
    m_rcClearSearchBtn = D2D1::RectF(
        railW + fullDrawerW - config.ScaleF(26.0f),
        curY - config.ScaleF(1.0f),
        railW + fullDrawerW - config.ScaleF(8.0f),
        curY + config.ScaleF(19.0f)
    );
    const wchar_t* clearGlyph = L"\u2715";
    pRT->DrawText(clearGlyph, 1, theme.fontBody.Get(), m_rcClearSearchBtn, theme.brushTextSecondary.Get());

    curY += config.ScaleF(28.0f);

    // 2. Search Box Row
    const float inputH = config.ScaleF(26.0f);
    m_rcSearchBox = D2D1::RectF(
        railW + paddingX,
        curY,
        railW + fullDrawerW - paddingX,
        curY + inputH
    );

    // Background of Search Box
    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(m_rcSearchBox, 3.5f, 3.5f);
    pRT->FillRoundedRectangle(rrcSearch, theme.brushCanvasBg.Get());

    // Border: Blue/BrandAccent when focused (1.5px), else subtle border (1px)
    if (m_focusedSearchInput == 1) {
        pRT->DrawRoundedRectangle(rrcSearch, theme.brushBrandAccent.Get(), 1.5f);
    } else {
        pRT->DrawRoundedRectangle(rrcSearch, theme.brushBorderSubtle.Get(), 1.0f);
    }

    // Toggle Buttons on Right inside Search Box: [Aa] [ab] [.*]
    const float btnW = config.ScaleF(20.0f);
    const float btnH = config.ScaleF(18.0f);
    const float btnY = curY + (inputH - btnH) / 2.0f;
    float rightBtnX = m_rcSearchBox.right - config.ScaleF(4.0f);

    // [.*] Regex Button
    rightBtnX -= btnW;
    m_rcUseRegexBtn = D2D1::RectF(rightBtnX, btnY, rightBtnX + btnW, btnY + btnH);
    if (m_useRegex) {
        D2D1_ROUNDED_RECT rrcBtn = D2D1::RoundedRect(m_rcUseRegexBtn, 2.5f, 2.5f);
        pRT->FillRoundedRectangle(rrcBtn, theme.brushItemActive.Get());
        pRT->DrawRoundedRectangle(rrcBtn, theme.brushBrandAccent.Get(), 1.0f);
    }

    // [ab] Match Whole Word Button
    rightBtnX -= (btnW + config.ScaleF(2.0f));
    m_rcMatchWholeWordBtn = D2D1::RectF(rightBtnX, btnY, rightBtnX + btnW, btnY + btnH);
    if (m_matchWholeWord) {
        D2D1_ROUNDED_RECT rrcBtn = D2D1::RoundedRect(m_rcMatchWholeWordBtn, 2.5f, 2.5f);
        pRT->FillRoundedRectangle(rrcBtn, theme.brushItemActive.Get());
        pRT->DrawRoundedRectangle(rrcBtn, theme.brushBrandAccent.Get(), 1.0f);
    }

    // [Aa] Match Case Button
    rightBtnX -= (btnW + config.ScaleF(2.0f));
    m_rcMatchCaseBtn = D2D1::RectF(rightBtnX, btnY, rightBtnX + btnW, btnY + btnH);
    if (m_matchCase) {
        D2D1_ROUNDED_RECT rrcBtn = D2D1::RoundedRect(m_rcMatchCaseBtn, 2.5f, 2.5f);
        pRT->FillRoundedRectangle(rrcBtn, theme.brushItemActive.Get());
        pRT->DrawRoundedRectangle(rrcBtn, theme.brushBrandAccent.Get(), 1.0f);
    }

    // Draw Labels for Toggle Buttons
    IDWriteTextFormat* pBtnFmt = theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get();
    pRT->DrawText(L"Aa", 2, pBtnFmt, m_rcMatchCaseBtn, m_matchCase ? theme.brushTextPrimary.Get() : theme.brushTextMuted.Get());
    pRT->DrawText(L"ab", 2, pBtnFmt, m_rcMatchWholeWordBtn, m_matchWholeWord ? theme.brushTextPrimary.Get() : theme.brushTextMuted.Get());
    pRT->DrawText(L".*", 2, pBtnFmt, m_rcUseRegexBtn, m_useRegex ? theme.brushTextPrimary.Get() : theme.brushTextMuted.Get());

    // Draw Search Input Query Text or Placeholder
    D2D1_RECT_F rcSearchText = D2D1::RectF(
        m_rcSearchBox.left + config.ScaleF(6.0f),
        curY + config.ScaleF(3.0f),
        rightBtnX - config.ScaleF(4.0f),
        curY + inputH - config.ScaleF(3.0f)
    );

    float textWidth = 0.0f;
    if (m_searchQuery.empty()) {
        const wchar_t* placeholder = L"Search";
        pRT->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)),
                      theme.fontBody.Get(), rcSearchText, theme.brushTextMuted.Get());
    } else {
        // High contrast text in both Light Mode (#1E1E1E) and Dark Mode (#D8D8D8 / #FFFFFF)
        pRT->DrawText(m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                      theme.fontBody.Get(), rcSearchText, theme.brushTextPrimary.Get());

        if (theme.dwriteFactory) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                    theme.fontBody.Get(), 1000.0f, inputH, &pLayout))) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                    textWidth = tm.width;
                }
            }
        }
    }

    // Caret '|' in Search Box
    if (m_focusedSearchInput == 1) {
        float caretX = rcSearchText.left + textWidth + 1.0f;
        if (caretX < rcSearchText.right) {
            pRT->DrawLine(
                D2D1::Point2F(caretX, curY + config.ScaleF(4.0f)),
                D2D1::Point2F(caretX, curY + inputH - config.ScaleF(4.0f)),
                theme.brushTextPrimary.Get(), 1.5f
            );
        }
    }

    curY += inputH + config.ScaleF(6.0f);

    // 3. Chevron toggle on left + Replace Row
    const float chevronW = config.ScaleF(14.0f);
    m_rcToggleReplaceChevron = D2D1::RectF(
        railW + config.ScaleF(1.0f),
        curY,
        railW + config.ScaleF(15.0f),
        curY + inputH
    );
    float chCenterY = curY + inputH / 2.0f;
    float chCenterX = m_rcToggleReplaceChevron.left + chevronW / 2.0f;
    if (m_isReplaceOpen) {
        // Down chevron 'v'
        pRT->DrawLine(
            D2D1::Point2F(chCenterX - config.ScaleF(3.5f), chCenterY - config.ScaleF(1.5f)),
            D2D1::Point2F(chCenterX, chCenterY + config.ScaleF(2.0f)),
            theme.brushTextSecondary.Get(), 1.4f
        );
        pRT->DrawLine(
            D2D1::Point2F(chCenterX, chCenterY + config.ScaleF(2.0f)),
            D2D1::Point2F(chCenterX + config.ScaleF(3.5f), chCenterY - config.ScaleF(1.5f)),
            theme.brushTextSecondary.Get(), 1.4f
        );
    } else {
        // Right chevron '>'
        pRT->DrawLine(
            D2D1::Point2F(chCenterX - config.ScaleF(2.0f), chCenterY - config.ScaleF(3.5f)),
            D2D1::Point2F(chCenterX + config.ScaleF(2.0f), chCenterY),
            theme.brushTextSecondary.Get(), 1.4f
        );
        pRT->DrawLine(
            D2D1::Point2F(chCenterX + config.ScaleF(2.0f), chCenterY),
            D2D1::Point2F(chCenterX - config.ScaleF(2.0f), chCenterY + config.ScaleF(3.5f)),
            theme.brushTextSecondary.Get(), 1.4f
        );
    }

    if (m_isReplaceOpen) {
        const float replaceAllW = config.ScaleF(24.0f);
        m_rcReplaceBox = D2D1::RectF(
            railW + paddingX,
            curY,
            railW + fullDrawerW - paddingX - replaceAllW - config.ScaleF(4.0f),
            curY + inputH
        );

        // Background of Replace Box
        D2D1_ROUNDED_RECT rrcReplace = D2D1::RoundedRect(m_rcReplaceBox, 3.5f, 3.5f);
        pRT->FillRoundedRectangle(rrcReplace, theme.brushCanvasBg.Get());

        if (m_focusedSearchInput == 2) {
            pRT->DrawRoundedRectangle(rrcReplace, theme.brushBrandAccent.Get(), 1.5f);
        } else {
            pRT->DrawRoundedRectangle(rrcReplace, theme.brushBorderSubtle.Get(), 1.0f);
        }

        // [AB] Preserve Case Button inside Replace Box
        float abBtnW = config.ScaleF(22.0f);
        float abBtnH = config.ScaleF(18.0f);
        float abBtnX = m_rcReplaceBox.right - abBtnW - config.ScaleF(4.0f);
        float abBtnY = curY + (inputH - abBtnH) / 2.0f;
        m_rcPreserveCaseBtn = D2D1::RectF(abBtnX, abBtnY, abBtnX + abBtnW, abBtnY + abBtnH);
        if (m_preserveCase) {
            D2D1_ROUNDED_RECT rrcAB = D2D1::RoundedRect(m_rcPreserveCaseBtn, 2.5f, 2.5f);
            pRT->FillRoundedRectangle(rrcAB, theme.brushItemActive.Get());
            pRT->DrawRoundedRectangle(rrcAB, theme.brushBrandAccent.Get(), 1.0f);
        }
        pRT->DrawText(L"AB", 2, pBtnFmt, m_rcPreserveCaseBtn, m_preserveCase ? theme.brushTextPrimary.Get() : theme.brushTextMuted.Get());

        // Replace Box Query Text or Placeholder
        D2D1_RECT_F rcReplaceText = D2D1::RectF(
            m_rcReplaceBox.left + config.ScaleF(6.0f),
            curY + config.ScaleF(3.0f),
            abBtnX - config.ScaleF(4.0f),
            curY + inputH - config.ScaleF(3.0f)
        );

        float repTextWidth = 0.0f;
        if (m_replaceQuery.empty()) {
            const wchar_t* placeholder = L"Replace";
            pRT->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)),
                      theme.fontBody.Get(), rcReplaceText, theme.brushTextMuted.Get());
        } else {
            pRT->DrawText(m_replaceQuery.c_str(), static_cast<UINT32>(m_replaceQuery.length()),
                          theme.fontBody.Get(), rcReplaceText, theme.brushTextPrimary.Get());

            if (theme.dwriteFactory) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                        m_replaceQuery.c_str(), static_cast<UINT32>(m_replaceQuery.length()),
                        theme.fontBody.Get(), 1000.0f, inputH, &pLayout))) {
                    DWRITE_TEXT_METRICS tm{};
                    if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                        repTextWidth = tm.width;
                    }
                }
            }
        }

        // Caret '|' in Replace Box
        if (m_focusedSearchInput == 2) {
            float caretX = rcReplaceText.left + repTextWidth + 1.0f;
            if (caretX < rcReplaceText.right) {
                pRT->DrawLine(
                    D2D1::Point2F(caretX, curY + config.ScaleF(4.0f)),
                    D2D1::Point2F(caretX, curY + inputH - config.ScaleF(4.0f)),
                    theme.brushTextPrimary.Get(), 1.5f
                );
            }
        }

        // Replace All Action Button outside Replace Box (to the right)
        m_rcReplaceAllBtn = D2D1::RectF(
            railW + fullDrawerW - paddingX - replaceAllW,
            curY,
            railW + fullDrawerW - paddingX,
            curY + inputH
        );
        D2D1_ROUNDED_RECT rrcRepAll = D2D1::RoundedRect(m_rcReplaceAllBtn, 3.5f, 3.5f);
        pRT->FillRoundedRectangle(rrcRepAll, theme.brushItemHover.Get());
        pRT->DrawRoundedRectangle(rrcRepAll, theme.brushBorderSubtle.Get(), 1.0f);
        const wchar_t* repAllGlyph = L"\u21C9";
        pRT->DrawText(repAllGlyph, 1, theme.fontBody.Get(), m_rcReplaceAllBtn, theme.brushTextSecondary.Get());

        curY += inputH + config.ScaleF(8.0f);
    } else {
        curY += config.ScaleF(6.0f);
    }

    // 4. Results List
    if (m_searchQuery.empty()) {
        D2D1_RECT_F rcHint = D2D1::RectF(
            railW + paddingX,
            curY + config.ScaleF(10.0f),
            railW + fullDrawerW - paddingX,
            curY + config.ScaleF(34.0f)
        );
        const wchar_t* hint = L"Type a keyword to search files...";
        pRT->DrawText(hint, static_cast<UINT32>(wcslen(hint)),
                      theme.fontBody.Get(), rcHint, theme.brushTextMuted.Get());
        return;
    }

    // Summary line
    std::wstring summary;
    if (m_totalMatches == 0) {
        summary = L"No results found.";
    } else {
        summary = std::to_wstring(m_totalMatches) + L" results in " +
                  std::to_wstring(m_searchGroups.size()) +
                  (m_searchGroups.size() == 1 ? L" file" : L" files");
    }

    D2D1_RECT_F rcSummary = D2D1::RectF(
        railW + paddingX,
        curY,
        railW + fullDrawerW - paddingX,
        curY + config.ScaleF(18.0f)
    );
    pRT->DrawText(summary.c_str(), static_cast<UINT32>(summary.length()),
                  theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                  rcSummary, theme.brushTextSecondary.Get());

    curY += config.ScaleF(24.0f);

    // Render Search File Groups and Matches
    const float rowH = config.ScaleF(22.0f);
    size_t curRowIdx = 0;

    for (size_t g = 0; g < m_searchGroups.size(); ++g) {
        auto& group = m_searchGroups[g];

        // File Header Row
        if (curRowIdx >= m_searchScrollIdx) {
            if (curY + rowH > clientHeight) {
                group.headerBounds = D2D1::RectF(0, 0, 0, 0);
            } else {
                group.headerBounds = D2D1::RectF(
                    railW + config.ScaleF(4.0f),
                    curY,
                    railW + fullDrawerW - config.ScaleF(4.0f),
                    curY + rowH
                );

                // File Chevron
                float gChevronX = railW + paddingX;
                float gCenterY = curY + rowH / 2.0f;
                if (group.isExpanded) {
                    pRT->DrawLine(
                        D2D1::Point2F(gChevronX - config.ScaleF(3.0f), gCenterY - config.ScaleF(1.5f)),
                        D2D1::Point2F(gChevronX, gCenterY + config.ScaleF(1.5f)),
                        theme.brushTextSecondary.Get(), 1.2f
                    );
                    pRT->DrawLine(
                        D2D1::Point2F(gChevronX, gCenterY + config.ScaleF(1.5f)),
                        D2D1::Point2F(gChevronX + config.ScaleF(3.0f), gCenterY - config.ScaleF(1.5f)),
                        theme.brushTextSecondary.Get(), 1.2f
                    );
                } else {
                    pRT->DrawLine(
                        D2D1::Point2F(gChevronX - config.ScaleF(1.5f), gCenterY - config.ScaleF(3.0f)),
                        D2D1::Point2F(gChevronX + config.ScaleF(1.5f), gCenterY),
                        theme.brushTextSecondary.Get(), 1.2f
                    );
                    pRT->DrawLine(
                        D2D1::Point2F(gChevronX + config.ScaleF(1.5f), gCenterY),
                        D2D1::Point2F(gChevronX - config.ScaleF(1.5f), gCenterY + config.ScaleF(3.0f)),
                        theme.brushTextSecondary.Get(), 1.2f
                    );
                }

                // File Seti Icon
                float iconX = gChevronX + config.ScaleF(10.0f);
                D2D1_RECT_F rcIcon = D2D1::RectF(
                    iconX, gCenterY - config.ScaleF(7.0f),
                    iconX + config.ScaleF(14.0f), gCenterY + config.ScaleF(7.0f)
                );
                auto setiInfo = theme.GetSetiIcon(group.fileName, false, false);
                if (theme.fontSeti && setiInfo.pBrush) {
                    pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), rcIcon, setiInfo.pBrush);
                }

                // File Name
                float nameX = iconX + config.ScaleF(18.0f);
                D2D1_RECT_F rcName = D2D1::RectF(
                    nameX, curY,
                    railW + fullDrawerW - config.ScaleF(46.0f), curY + rowH
                );
                std::wstring fileLabel = group.fileName;
                if (!group.relDir.empty()) {
                    fileLabel += L"  " + group.relDir;
                }
                pRT->DrawText(fileLabel.c_str(), static_cast<UINT32>(fileLabel.length()),
                              theme.fontBody.Get(), rcName, theme.brushTextPrimary.Get());

                // Badge count pill
                std::wstring countStr = std::to_wstring(group.matches.size());
                float badgeH = config.ScaleF(16.0f);
                D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(
                    D2D1::RectF(
                        railW + fullDrawerW - config.ScaleF(32.0f),
                        curY + (rowH - badgeH) / 2.0f,
                        railW + fullDrawerW - config.ScaleF(12.0f),
                        curY + (rowH + badgeH) / 2.0f
                    ),
                    config.ScaleF(8.0f), config.ScaleF(8.0f)
                );
                pRT->FillRoundedRectangle(rrcBadge, theme.brushItemHover.Get());
                pRT->DrawText(countStr.c_str(), static_cast<UINT32>(countStr.length()),
                              theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                              rrcBadge.rect, theme.brushTextSecondary.Get());

                curY += rowH;
            }
        }
        curRowIdx++;

        // Child Matches under File
        if (group.isExpanded) {
            for (size_t m = 0; m < group.matches.size(); ++m) {
                auto& match = group.matches[m];
                if (curRowIdx >= m_searchScrollIdx) {
                    if (curY + rowH > clientHeight) {
                        match.bounds = D2D1::RectF(0, 0, 0, 0);
                    } else {
                        match.bounds = D2D1::RectF(
                            railW + config.ScaleF(4.0f),
                            curY,
                            railW + fullDrawerW - config.ScaleF(4.0f),
                            curY + rowH
                        );

                        // Match line number (e.g. "14")
                        std::wstring lineNum = std::to_wstring(match.lineIndex + 1);
                        D2D1_RECT_F rcLineNum = D2D1::RectF(
                            railW + paddingX + config.ScaleF(10.0f),
                            curY,
                            railW + paddingX + config.ScaleF(34.0f),
                            curY + rowH
                        );
                        pRT->DrawText(lineNum.c_str(), static_cast<UINT32>(lineNum.length()),
                                      theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                                      rcLineNum, theme.brushTextMuted.Get());

                        // Match snippet
                        float snipX = rcLineNum.right + config.ScaleF(4.0f);
                        D2D1_RECT_F rcSnip = D2D1::RectF(
                            snipX, curY,
                            railW + fullDrawerW - config.ScaleF(10.0f), curY + rowH
                        );

                        std::wstring trimmed = match.lineText;
                        size_t firstNonSpace = trimmed.find_first_not_of(L" \t");
                        if (firstNonSpace != std::wstring::npos && firstNonSpace > 0) {
                            trimmed = trimmed.substr(firstNonSpace);
                        }

                        pRT->DrawText(trimmed.c_str(), static_cast<UINT32>(trimmed.length()),
                                      theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                                      rcSnip, theme.brushTextPrimary.Get());

                        curY += rowH;
                    }
                }
                curRowIdx++;
            }
        }
    }
}

int DrawerPanel::HitTest(POINT pt, const app::AppConfig& config) const {
    (void)config;
    if (!m_hasOpenFolder) return -1;

    for (size_t i = 0; i < m_visibleItems.size(); ++i) {
        const auto& item = m_visibleItems[i];
        if (pt.x >= item.bounds.left && pt.x < item.bounds.right &&
            pt.y >= item.bounds.top && pt.y < item.bounds.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool DrawerPanel::HitTestOpenFolderButton(POINT pt, const app::AppConfig& config) const {
    (void)config;
    if (!m_hasOpenFolder) {
        return (pt.x >= m_rcOpenFolderBtn.left && pt.x <= m_rcOpenFolderBtn.right &&
                pt.y >= m_rcOpenFolderBtn.top && pt.y <= m_rcOpenFolderBtn.bottom);
    }
    return false;
}

bool DrawerPanel::HitTestSearch(POINT pt, const app::AppConfig& config, std::wstring& outJumpFile, size_t& outJumpLine) {
    (void)config;
    outJumpFile.clear();
    outJumpLine = 0;

    // 1. Refresh button
    if (pt.x >= m_rcRefreshSearchBtn.left && pt.x <= m_rcRefreshSearchBtn.right &&
        pt.y >= m_rcRefreshSearchBtn.top && pt.y <= m_rcRefreshSearchBtn.bottom) {
        ExecuteSearch();
        return true;
    }

    // 2. Clear button
    if (pt.x >= m_rcClearSearchBtn.left && pt.x <= m_rcClearSearchBtn.right &&
        pt.y >= m_rcClearSearchBtn.top && pt.y <= m_rcClearSearchBtn.bottom) {
        m_searchQuery.clear();
        m_searchGroups.clear();
        m_totalMatches = 0;
        return true;
    }

    // 3. Match Case (Aa) button
    if (pt.x >= m_rcMatchCaseBtn.left && pt.x <= m_rcMatchCaseBtn.right &&
        pt.y >= m_rcMatchCaseBtn.top && pt.y <= m_rcMatchCaseBtn.bottom) {
        m_matchCase = !m_matchCase;
        ExecuteSearch();
        return true;
    }

    // 4. Match Whole Word (ab) button
    if (pt.x >= m_rcMatchWholeWordBtn.left && pt.x <= m_rcMatchWholeWordBtn.right &&
        pt.y >= m_rcMatchWholeWordBtn.top && pt.y <= m_rcMatchWholeWordBtn.bottom) {
        m_matchWholeWord = !m_matchWholeWord;
        ExecuteSearch();
        return true;
    }

    // 5. Use Regex (.*) button
    if (pt.x >= m_rcUseRegexBtn.left && pt.x <= m_rcUseRegexBtn.right &&
        pt.y >= m_rcUseRegexBtn.top && pt.y <= m_rcUseRegexBtn.bottom) {
        m_useRegex = !m_useRegex;
        ExecuteSearch();
        return true;
    }

    // 6. Chevron toggle Replace box
    if (pt.x >= m_rcToggleReplaceChevron.left && pt.x <= m_rcToggleReplaceChevron.right &&
        pt.y >= m_rcToggleReplaceChevron.top && pt.y <= m_rcToggleReplaceChevron.bottom) {
        m_isReplaceOpen = !m_isReplaceOpen;
        return true;
    }

    // 7. Preserve Case (AB) button
    if (m_isReplaceOpen &&
        pt.x >= m_rcPreserveCaseBtn.left && pt.x <= m_rcPreserveCaseBtn.right &&
        pt.y >= m_rcPreserveCaseBtn.top && pt.y <= m_rcPreserveCaseBtn.bottom) {
        m_preserveCase = !m_preserveCase;
        return true;
    }

    // 8. Replace All button
    if (m_isReplaceOpen &&
        pt.x >= m_rcReplaceAllBtn.left && pt.x <= m_rcReplaceAllBtn.right &&
        pt.y >= m_rcReplaceAllBtn.top && pt.y <= m_rcReplaceAllBtn.bottom) {
        ExecuteReplaceAll();
        return true;
    }

    // 9. Search Input Box
    if (pt.x >= m_rcSearchBox.left && pt.x <= m_rcSearchBox.right &&
        pt.y >= m_rcSearchBox.top && pt.y <= m_rcSearchBox.bottom) {
        m_focusedSearchInput = 1;
        return true;
    }

    // 10. Replace Input Box
    if (m_isReplaceOpen &&
        pt.x >= m_rcReplaceBox.left && pt.x <= m_rcReplaceBox.right &&
        pt.y >= m_rcReplaceBox.top && pt.y <= m_rcReplaceBox.bottom) {
        m_focusedSearchInput = 2;
        return true;
    }

    // 11. Search Results: File Header toggling & Match clicking
    for (auto& group : m_searchGroups) {
        if (group.headerBounds.right > group.headerBounds.left &&
            pt.x >= group.headerBounds.left && pt.x <= group.headerBounds.right &&
            pt.y >= group.headerBounds.top && pt.y <= group.headerBounds.bottom) {
            group.isExpanded = !group.isExpanded;
            return true;
        }

        if (group.isExpanded) {
            for (const auto& match : group.matches) {
                if (match.bounds.right > match.bounds.left &&
                    pt.x >= match.bounds.left && pt.x <= match.bounds.right &&
                    pt.y >= match.bounds.top && pt.y <= match.bounds.bottom) {
                    outJumpFile = group.filePath;
                    outJumpLine = match.lineIndex;
                    return true;
                }
            }
        }
    }

    return false;
}

void DrawerPanel::HandleSearchChar(wchar_t ch) {
    if (m_focusedSearchInput == 1) {
        if (ch >= 32) {
            m_searchQuery.push_back(ch);
            ExecuteSearch();
        }
    } else if (m_focusedSearchInput == 2) {
        if (ch >= 32) {
            m_replaceQuery.push_back(ch);
        }
    }
}

void DrawerPanel::HandleSearchKeyDown(WPARAM wParam) {
    if (wParam == VK_BACK) {
        if (m_focusedSearchInput == 1 && !m_searchQuery.empty()) {
            m_searchQuery.pop_back();
            ExecuteSearch();
        } else if (m_focusedSearchInput == 2 && !m_replaceQuery.empty()) {
            m_replaceQuery.pop_back();
        }
    } else if (wParam == VK_TAB) {
        if (m_isReplaceOpen) {
            m_focusedSearchInput = (m_focusedSearchInput == 1) ? 2 : 1;
        }
    } else if (wParam == VK_ESCAPE) {
        m_focusedSearchInput = 0;
    } else if (wParam == VK_RETURN) {
        ExecuteSearch();
    }
}

void DrawerPanel::ExecuteSearch() {
    m_searchGroups.clear();
    m_totalMatches = 0;
    m_searchScrollIdx = 0;
    if (m_searchQuery.empty() || !m_hasOpenFolder || m_openFolderPath.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::path root(m_openFolderPath);
    if (!std::filesystem::exists(root, ec)) return;

    std::wregex regexPattern;
    bool regexValid = false;
    if (m_useRegex) {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!m_matchCase) {
                flags |= std::regex_constants::icase;
            }
            regexPattern = std::wregex(m_searchQuery, flags);
            regexValid = true;
        } catch (...) {
            regexValid = false;
        }
    }

    std::wstring lowerQuery = m_searchQuery;
    if (!m_matchCase) {
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);
    }

    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        const auto& entry = *it;
        auto filename = entry.path().filename().wstring();

        // Skip hidden files/directories and common heavy folders
        if (!filename.empty() && filename[0] == L'.') {
            if (entry.is_directory(ec)) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (entry.is_directory(ec)) {
            if (filename == L"node_modules" || filename == L"build" || filename == L"bin" ||
                filename == L"obj" || filename == L"target" || filename == L".git") {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!entry.is_regular_file(ec)) continue;

        // Skip large files (> 2MB)
        auto fileSize = entry.file_size(ec);
        if (ec || fileSize > 2 * 1024 * 1024) {
            ec.clear();
            continue;
        }

        // Open and read file lines
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open()) continue;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // Skip binary files
        if (content.find('\0') != std::string::npos) continue;

        int wlen = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), nullptr, 0);
        if (wlen <= 0) continue;
        std::wstring wcontent(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), &wcontent[0], wlen);

        SearchFileGroup group;
        group.filePath = entry.path().wstring();
        group.fileName = filename;
        std::filesystem::path rel = std::filesystem::relative(entry.path().parent_path(), root, ec);
        group.relDir = ec ? L"" : rel.wstring();
        if (group.relDir == L".") group.relDir.clear();
        group.isExpanded = true;

        size_t lineIdx = 0;
        size_t pos = 0;
        while (pos < wcontent.size()) {
            size_t nextNL = wcontent.find(L'\n', pos);
            if (nextNL == std::wstring::npos) nextNL = wcontent.size();

            std::wstring lineStr = wcontent.substr(pos, nextNL - pos);
            if (!lineStr.empty() && lineStr.back() == L'\r') {
                lineStr.pop_back();
            }

            if (m_useRegex && regexValid) {
                std::wsregex_iterator regIt(lineStr.begin(), lineStr.end(), regexPattern);
                std::wsregex_iterator regEnd;
                for (; regIt != regEnd; ++regIt) {
                    SearchMatch match;
                    match.lineIndex = lineIdx;
                    match.lineText = lineStr;
                    match.matchCol = static_cast<size_t>(regIt->position());
                    match.matchLen = static_cast<size_t>(regIt->length());
                    group.matches.push_back(match);
                }
            } else {
                std::wstring searchLine = lineStr;
                if (!m_matchCase) {
                    std::transform(searchLine.begin(), searchLine.end(), searchLine.begin(), ::towlower);
                }

                size_t findPos = 0;
                while ((findPos = searchLine.find(lowerQuery, findPos)) != std::wstring::npos) {
                    bool valid = true;
                    if (m_matchWholeWord) {
                        if (findPos > 0) {
                            wchar_t prev = searchLine[findPos - 1];
                            if (iswalnum(prev) || prev == L'_') valid = false;
                        }
                        if (valid && (findPos + lowerQuery.size() < searchLine.size())) {
                            wchar_t next = searchLine[findPos + lowerQuery.size()];
                            if (iswalnum(next) || next == L'_') valid = false;
                        }
                    }

                    if (valid) {
                        SearchMatch match;
                        match.lineIndex = lineIdx;
                        match.lineText = lineStr;
                        match.matchCol = findPos;
                        match.matchLen = m_searchQuery.size();
                        group.matches.push_back(match);
                    }
                    findPos += (std::max)(size_t(1), lowerQuery.size());
                }
            }

            pos = nextNL + 1;
            lineIdx++;
        }

        if (!group.matches.empty()) {
            m_totalMatches += group.matches.size();
            m_searchGroups.push_back(std::move(group));
        }

        if (m_totalMatches >= 1000) break;
    }
}

void DrawerPanel::ExecuteReplaceAll() {
    if (m_searchQuery.empty() || m_searchGroups.empty()) return;

    for (const auto& group : m_searchGroups) {
        std::ifstream file(group.filePath, std::ios::binary);
        if (!file.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        int wlen = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), nullptr, 0);
        if (wlen <= 0) continue;
        std::wstring wcontent(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), &wcontent[0], wlen);

        std::wstring newContent;
        size_t lastPos = 0;
        size_t pos = 0;
        std::wstring lowerContent = wcontent;
        std::wstring lowerQuery = m_searchQuery;
        if (!m_matchCase) {
            std::transform(lowerContent.begin(), lowerContent.end(), lowerContent.begin(), ::towlower);
            std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);
        }

        while ((pos = lowerContent.find(lowerQuery, lastPos)) != std::wstring::npos) {
            newContent.append(wcontent, lastPos, pos - lastPos);
            newContent.append(m_replaceQuery);
            lastPos = pos + m_searchQuery.length();
        }
        newContent.append(wcontent, lastPos, wcontent.length() - lastPos);

        int u8len = WideCharToMultiByte(CP_UTF8, 0, newContent.c_str(), static_cast<int>(newContent.size()), nullptr, 0, nullptr, nullptr);
        if (u8len > 0) {
            std::string u8(u8len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, newContent.c_str(), static_cast<int>(newContent.size()), &u8[0], u8len, nullptr, nullptr);
            std::ofstream out(group.filePath, std::ios::binary | std::ios::trunc);
            if (out.is_open()) {
                out.write(u8.data(), u8.size());
                out.close();
            }
        }
    }

    ExecuteSearch();
}

bool DrawerPanel::SetHoveredIndex(int index) {
    if (m_hoveredIndex != index) {
        m_hoveredIndex = index;
        return true;
    }
    return false;
}

bool DrawerPanel::SetHoveredOpenFolderBtn(bool hovered) {
    if (m_hoveredOpenFolderBtn != hovered) {
        m_hoveredOpenFolderBtn = hovered;
        return true;
    }
    return false;
}

bool DrawerPanel::HandleMouseWheel(short delta, int activeRailTab) {
    if (activeRailTab == 1) {
        if (m_searchGroups.empty()) return false;
        size_t oldScroll = m_searchScrollIdx;
        if (delta > 0) {
            if (m_searchScrollIdx >= 2) {
                m_searchScrollIdx -= 2;
            } else {
                m_searchScrollIdx = 0;
            }
        } else {
            m_searchScrollIdx += 2;
        }
        return m_searchScrollIdx != oldScroll;
    }

    if (m_visibleItems.empty()) return false;
    size_t oldScroll = m_scrollIndex;
    if (delta > 0) {
        if (m_scrollIndex >= 2) {
            m_scrollIndex -= 2;
        } else {
            m_scrollIndex = 0;
        }
    } else {
        if (m_scrollIndex + 2 < m_visibleItems.size()) {
            m_scrollIndex += 2;
        }
    }
    return m_scrollIndex != oldScroll;
}

} // namespace phide::ui
