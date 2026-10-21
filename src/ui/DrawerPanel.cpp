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

namespace {

bool MatchesWildcard(const std::wstring& str, const std::wstring& pattern) {
    size_t s = 0, p = 0, match = 0, starIdx = std::wstring::npos;
    while (s < str.length()) {
        if (p < pattern.length() && (pattern[p] == L'?' || towlower(pattern[p]) == towlower(str[s]))) {
            ++s;
            ++p;
        } else if (p < pattern.length() && pattern[p] == L'*') {
            starIdx = p;
            match = s;
            ++p;
        } else if (starIdx != std::wstring::npos) {
            p = starIdx + 1;
            ++match;
            s = match;
        } else {
            return false;
        }
    }
    while (p < pattern.length() && pattern[p] == L'*') {
        ++p;
    }
    return p == pattern.length();
}

bool MatchesPatternList(const std::wstring& filename, const std::wstring& fullPath, const std::wstring& patternList) {
    if (patternList.empty()) return true;
    size_t start = 0;
    while (start < patternList.length()) {
        while (start < patternList.length() && iswspace(patternList[start])) ++start;
        size_t comma = patternList.find(L',', start);
        size_t end = (comma == std::wstring::npos) ? patternList.length() : comma;
        std::wstring pat = patternList.substr(start, end - start);
        while (!pat.empty() && iswspace(pat.back())) pat.pop_back();

        if (!pat.empty()) {
            if (pat.find(L'*') == std::wstring::npos && pat.find(L'?') == std::wstring::npos) {
                std::wstring lPath = fullPath;
                std::wstring lPat = pat;
                for (auto& ch : lPath) ch = towlower(ch);
                for (auto& ch : lPat) ch = towlower(ch);
                if (lPath.find(lPat) != std::wstring::npos) return true;
            } else {
                if (MatchesWildcard(filename, pat) || MatchesWildcard(fullPath, pat)) {
                    return true;
                }
            }
        }
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return false;
}

} // anonymous namespace

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
        } else if (config.activeRailTab == 2) {
            RenderSourceControlPane(pRT, theme, config, clientHeight, currentDrawerW);
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

                bool isActive = (!m_activeFilePath.empty() && item.fullPath == m_activeFilePath);
                bool isHovered = (m_hoveredIndex == static_cast<int>(i));

                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(item.bounds, 3.5f, 3.5f);
                if (isActive) {
                    // Active selection highlight bar matching Images 2 & 3
                    if (theme.brushTreeSelected) {
                        pRT->FillRoundedRectangle(rrc, theme.brushTreeSelected.Get());
                    } else {
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushActive;
                        pRT->CreateSolidColorBrush(
                            theme.IsDarkMode() ? D2D1::ColorF(0.04f, 0.38f, 0.65f, 0.40f) : D2D1::ColorF(0.04f, 0.45f, 0.85f, 0.25f),
                            &brushActive
                        );
                        if (brushActive) pRT->FillRoundedRectangle(rrc, brushActive.Get());
                    }
                } else if (isHovered) {
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

                // 4. Draw Item Text Label OR Inline Rename Input Box (VS Code style inline rename)
                float labelX = iconX + config.ScaleF(22.0f);
                if (m_isRenaming && static_cast<int>(i) == m_renamingItemIdx) {
                    m_rcRenameBox = D2D1::RectF(
                        labelX - config.ScaleF(4.0f),
                        curY + config.ScaleF(1.0f),
                        railW + fullDrawerW - config.ScaleF(10.0f),
                        curY + itemH - config.ScaleF(1.0f)
                    );
                    D2D1_ROUNDED_RECT rrcBox = D2D1::RoundedRect(m_rcRenameBox, 3.0f, 3.0f);
                    pRT->FillRoundedRectangle(rrcBox, theme.brushCanvasBg.Get());
                    pRT->DrawRoundedRectangle(rrcBox, theme.brushBrandAccent.Get(), 1.5f);

                    D2D1_RECT_F rcRenameText = D2D1::RectF(
                        m_rcRenameBox.left + config.ScaleF(4.0f),
                        m_rcRenameBox.top + config.ScaleF(1.0f),
                        m_rcRenameBox.right - config.ScaleF(4.0f),
                        m_rcRenameBox.bottom - config.ScaleF(1.0f)
                    );

                    // Pre-selection highlight (name selected, excluding extension)
                    if (m_renameSelEnd > m_renameSelStart && theme.dwriteFactory && !m_renameText.empty()) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                m_renameText.c_str(), static_cast<UINT32>(m_renameText.length()),
                                theme.fontBody.Get(), 1000.0f, itemH, &pLayout))) {
                            DWRITE_HIT_TEST_METRICS htm[16];
                            UINT32 actualCount = 0;
                            if (SUCCEEDED(pLayout->HitTestTextRange(
                                    static_cast<UINT32>(m_renameSelStart),
                                    static_cast<UINT32>(m_renameSelEnd - m_renameSelStart),
                                    0.0f, 0.0f, htm, 16, &actualCount))) {
                                for (UINT32 h = 0; h < actualCount; ++h) {
                                    D2D1_RECT_F rcSel = D2D1::RectF(
                                        rcRenameText.left + htm[h].left,
                                        rcRenameText.top + config.ScaleF(1.5f),
                                        rcRenameText.left + htm[h].left + htm[h].width,
                                        rcRenameText.bottom - config.ScaleF(1.5f)
                                    );
                                    ID2D1Brush* pSelBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushItemHover.Get();
                                    pRT->FillRectangle(rcSel, pSelBrush);
                                }
                            }
                        }
                    }

                    pRT->DrawText(
                        m_renameText.c_str(),
                        static_cast<UINT32>(m_renameText.length()),
                        theme.fontBody.Get(),
                        rcRenameText,
                        theme.brushTextPrimary.Get()
                    );

                    // Caret inside rename box at m_renameCaretPos
                    float caretOffset = 0.0f;
                    if (theme.dwriteFactory && m_renameCaretPos > 0 && !m_renameText.empty()) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                m_renameText.c_str(), static_cast<UINT32>(m_renameText.length()),
                                theme.fontBody.Get(), 1000.0f, itemH, &pLayout))) {
                            FLOAT hx = 0.0f, hy = 0.0f;
                            DWRITE_HIT_TEST_METRICS htm{};
                            if (SUCCEEDED(pLayout->HitTestTextPosition(
                                    static_cast<UINT32>(m_renameCaretPos), FALSE, &hx, &hy, &htm))) {
                                caretOffset = hx;
                            }
                        }
                    }
                    float caretX = rcRenameText.left + caretOffset + 1.0f;
                    if (caretX < rcRenameText.right) {
                        pRT->DrawLine(
                            D2D1::Point2F(caretX, m_rcRenameBox.top + config.ScaleF(3.0f)),
                            D2D1::Point2F(caretX, m_rcRenameBox.bottom - config.ScaleF(3.0f)),
                            theme.brushTextPrimary.Get(), 1.5f
                        );
                    }
                } else {
                    D2D1_RECT_F rcLabel = D2D1::RectF(
                        labelX,
                        curY,
                        railW + fullDrawerW - config.ScaleF(10.0f),
                        curY + itemH
                    );
                    ID2D1Brush* pTextBrush = isActive ?
                        (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) :
                        (theme.IsDarkMode() ? 
                            (isHovered ? theme.brushTextWhite.Get() : (item.isFolder ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())) :
                            theme.brushTextPrimary.Get());
                    pRT->DrawText(
                        item.label.c_str(),
                        static_cast<UINT32>(item.label.length()),
                        theme.fontBody.Get(),
                        rcLabel,
                        pTextBrush
                    );
                }

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

    // 1. Header: "Code Search" + Refresh icon button (↻) (Clear '✕' removed as requested)
    D2D1_RECT_F rcTitle = D2D1::RectF(
        railW + paddingX,
        curY,
        railW + fullDrawerW - config.ScaleF(40.0f),
        curY + config.ScaleF(20.0f)
    );
    const wchar_t* title = L"Code Search";
    pRT->DrawText(title, static_cast<UINT32>(wcslen(title)),
                  theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get(),
                  rcTitle, theme.brushTextPrimary.Get());

    // Refresh icon button (↻)
    m_rcRefreshSearchBtn = D2D1::RectF(
        railW + fullDrawerW - config.ScaleF(32.0f),
        curY - config.ScaleF(1.0f),
        railW + fullDrawerW - config.ScaleF(10.0f),
        curY + config.ScaleF(19.0f)
    );
    const wchar_t* refreshGlyph = L"\u21BB";
    pRT->DrawText(refreshGlyph, 1, theme.fontBody.Get(), m_rcRefreshSearchBtn, theme.brushTextSecondary.Get());

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

        // Clean vector Replace All icon: right-pointing arrow with vertical bar
        float rcx = (m_rcReplaceAllBtn.left + m_rcReplaceAllBtn.right) * 0.5f;
        float rcy = (m_rcReplaceAllBtn.top + m_rcReplaceAllBtn.bottom) * 0.5f;
        ID2D1Brush* repBrush = theme.brushTextSecondary.Get();
        float arrHalfW = config.ScaleF(4.5f);
        pRT->DrawLine(D2D1::Point2F(rcx - arrHalfW, rcy), D2D1::Point2F(rcx + arrHalfW - config.ScaleF(1.0f), rcy), repBrush, 1.4f);
        pRT->DrawLine(D2D1::Point2F(rcx + arrHalfW - config.ScaleF(3.5f), rcy - config.ScaleF(3.0f)), D2D1::Point2F(rcx + arrHalfW - config.ScaleF(1.0f), rcy), repBrush, 1.4f);
        pRT->DrawLine(D2D1::Point2F(rcx + arrHalfW - config.ScaleF(3.5f), rcy + config.ScaleF(3.0f)), D2D1::Point2F(rcx + arrHalfW - config.ScaleF(1.0f), rcy), repBrush, 1.4f);
        pRT->DrawLine(D2D1::Point2F(rcx + arrHalfW + config.ScaleF(1.5f), rcy - config.ScaleF(4.0f)), D2D1::Point2F(rcx + arrHalfW + config.ScaleF(1.5f), rcy + config.ScaleF(4.0f)), repBrush, 1.5f);

        curY += inputH + config.ScaleF(4.0f);
    } else {
        curY += config.ScaleF(4.0f);
    }

    // 3b. Toggle Search Details (...) Button Row (Images 2, 3, 4)
    const float detailsBtnW = config.ScaleF(24.0f);
    const float detailsBtnH = config.ScaleF(18.0f);
    m_rcToggleSearchDetailsBtn = D2D1::RectF(
        railW + fullDrawerW - paddingX - detailsBtnW,
        curY,
        railW + fullDrawerW - paddingX,
        curY + detailsBtnH
    );
    D2D1_ROUNDED_RECT rrcDetails = D2D1::RoundedRect(m_rcToggleSearchDetailsBtn, 3.0f, 3.0f);
    if (m_isSearchDetailsOpen) {
        pRT->FillRoundedRectangle(rrcDetails, theme.brushItemActive.Get());
        pRT->DrawRoundedRectangle(rrcDetails, theme.brushBrandAccent.Get(), 1.0f);
    } else {
        pRT->FillRoundedRectangle(rrcDetails, theme.brushItemHover.Get());
        pRT->DrawRoundedRectangle(rrcDetails, theme.brushBorderSubtle.Get(), 0.8f);
    }

    // Sleek vector 3-dot horizontal ellipsis (...)
    float dotY = (m_rcToggleSearchDetailsBtn.top + m_rcToggleSearchDetailsBtn.bottom) * 0.5f;
    float dotMidX = (m_rcToggleSearchDetailsBtn.left + m_rcToggleSearchDetailsBtn.right) * 0.5f;
    float dotSpacing = config.ScaleF(4.0f);
    float dotR = config.ScaleF(1.2f);
    ID2D1Brush* dotBrush = m_isSearchDetailsOpen ? theme.brushBrandAccent.Get() : theme.brushTextSecondary.Get();
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotMidX - dotSpacing, dotY), dotR, dotR), dotBrush);
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotMidX, dotY), dotR, dotR), dotBrush);
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotMidX + dotSpacing, dotY), dotR, dotR), dotBrush);

    curY += detailsBtnH + config.ScaleF(4.0f);

    // If Search Details (...) is expanded, render "files to include" and "files to exclude" (Image 3 & 4)
    if (m_isSearchDetailsOpen) {
        // --- A. "files to include" ---
        D2D1_RECT_F rcIncludeLabel = D2D1::RectF(
            railW + paddingX,
            curY,
            railW + fullDrawerW - paddingX,
            curY + config.ScaleF(16.0f)
        );
        pRT->DrawText(L"files to include", 16,
                      theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                      rcIncludeLabel, theme.brushTextMuted.Get());
        curY += config.ScaleF(16.0f);

        m_rcFilesToIncludeBox = D2D1::RectF(
            railW + paddingX,
            curY,
            railW + fullDrawerW - paddingX,
            curY + inputH
        );
        D2D1_ROUNDED_RECT rrcInc = D2D1::RoundedRect(m_rcFilesToIncludeBox, 3.5f, 3.5f);
        pRT->FillRoundedRectangle(rrcInc, theme.brushCanvasBg.Get());
        if (m_focusedSearchInput == 3) {
            pRT->DrawRoundedRectangle(rrcInc, theme.brushBrandAccent.Get(), 1.5f);
        } else {
            pRT->DrawRoundedRectangle(rrcInc, theme.brushBorderSubtle.Get(), 1.0f);
        }

        // Open Editors only toggle button inside Include box on right (Book icon, Image 3 & 4)
        float incBtnW = config.ScaleF(22.0f);
        float incBtnH = config.ScaleF(18.0f);
        float incBtnX = m_rcFilesToIncludeBox.right - incBtnW - config.ScaleF(4.0f);
        float incBtnY = curY + (inputH - incBtnH) / 2.0f;
        m_rcOpenEditorsOnlyBtn = D2D1::RectF(incBtnX, incBtnY, incBtnX + incBtnW, incBtnY + incBtnH);
        D2D1_ROUNDED_RECT rrcBook = D2D1::RoundedRect(m_rcOpenEditorsOnlyBtn, 2.5f, 2.5f);
        if (m_searchOnlyOpenEditors) {
            pRT->FillRoundedRectangle(rrcBook, theme.brushItemActive.Get());
            pRT->DrawRoundedRectangle(rrcBook, theme.brushBrandAccent.Get(), 1.0f);
        }

        // Draw crisp vector Open Book icon
        ID2D1Brush* bookBrush = m_searchOnlyOpenEditors ? theme.brushBrandAccent.Get() : theme.brushTextSecondary.Get();
        float bkMidX = (m_rcOpenEditorsOnlyBtn.left + m_rcOpenEditorsOnlyBtn.right) * 0.5f;
        float bkMidY = (m_rcOpenEditorsOnlyBtn.top + m_rcOpenEditorsOnlyBtn.bottom) * 0.5f;
        float bkw = config.ScaleF(11.0f);
        float bkh = config.ScaleF(8.5f);
        float hW = bkw * 0.5f;
        float hH = bkh * 0.5f;
        // Center spine
        pRT->DrawLine(D2D1::Point2F(bkMidX, bkMidY - hH), D2D1::Point2F(bkMidX, bkMidY + hH), bookBrush, 1.3f);
        // Left page outline
        pRT->DrawLine(D2D1::Point2F(bkMidX, bkMidY - hH), D2D1::Point2F(bkMidX - hW, bkMidY - hH * 0.6f), bookBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(bkMidX - hW, bkMidY - hH * 0.6f), D2D1::Point2F(bkMidX - hW, bkMidY + hH * 0.8f), bookBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(bkMidX - hW, bkMidY + hH * 0.8f), D2D1::Point2F(bkMidX, bkMidY + hH), bookBrush, 1.2f);
        // Right page outline
        pRT->DrawLine(D2D1::Point2F(bkMidX, bkMidY - hH), D2D1::Point2F(bkMidX + hW, bkMidY - hH * 0.6f), bookBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(bkMidX + hW, bkMidY - hH * 0.6f), D2D1::Point2F(bkMidX + hW, bkMidY + hH * 0.8f), bookBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(bkMidX + hW, bkMidY + hH * 0.8f), D2D1::Point2F(bkMidX, bkMidY + hH), bookBrush, 1.2f);

        // Include text or placeholder
        D2D1_RECT_F rcIncText = D2D1::RectF(
            m_rcFilesToIncludeBox.left + config.ScaleF(6.0f),
            curY + config.ScaleF(3.0f),
            incBtnX - config.ScaleF(4.0f),
            curY + inputH - config.ScaleF(3.0f)
        );
        float incTextWidth = 0.0f;
        if (m_filesToInclude.empty()) {
            pRT->DrawText(L"e.g. *.ts, src/**/include", 24, theme.fontBody.Get(), rcIncText, theme.brushTextMuted.Get());
        } else {
            pRT->DrawText(m_filesToInclude.c_str(), static_cast<UINT32>(m_filesToInclude.length()),
                          theme.fontBody.Get(), rcIncText, theme.brushTextPrimary.Get());
            if (theme.dwriteFactory) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                        m_filesToInclude.c_str(), static_cast<UINT32>(m_filesToInclude.length()),
                        theme.fontBody.Get(), 1000.0f, inputH, &pLayout))) {
                    DWRITE_TEXT_METRICS tm{};
                    if (SUCCEEDED(pLayout->GetMetrics(&tm))) incTextWidth = tm.width;
                }
            }
        }
        if (m_focusedSearchInput == 3) {
            float caretX = rcIncText.left + incTextWidth + 1.0f;
            if (caretX < rcIncText.right) {
                pRT->DrawLine(D2D1::Point2F(caretX, curY + config.ScaleF(4.0f)),
                              D2D1::Point2F(caretX, curY + inputH - config.ScaleF(4.0f)),
                              theme.brushTextPrimary.Get(), 1.5f);
            }
        }
        curY += inputH + config.ScaleF(6.0f);

        // --- B. "files to exclude" ---
        D2D1_RECT_F rcExcludeLabel = D2D1::RectF(
            railW + paddingX,
            curY,
            railW + fullDrawerW - paddingX,
            curY + config.ScaleF(16.0f)
        );
        pRT->DrawText(L"files to exclude", 16,
                      theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                      rcExcludeLabel, theme.brushTextMuted.Get());
        curY += config.ScaleF(16.0f);

        m_rcFilesToExcludeBox = D2D1::RectF(
            railW + paddingX,
            curY,
            railW + fullDrawerW - paddingX,
            curY + inputH
        );
        D2D1_ROUNDED_RECT rrcExc = D2D1::RoundedRect(m_rcFilesToExcludeBox, 3.5f, 3.5f);
        pRT->FillRoundedRectangle(rrcExc, theme.brushCanvasBg.Get());
        if (m_focusedSearchInput == 4) {
            pRT->DrawRoundedRectangle(rrcExc, theme.brushBrandAccent.Get(), 1.5f);
        } else {
            pRT->DrawRoundedRectangle(rrcExc, theme.brushBorderSubtle.Get(), 1.0f);
        }

        // Use Exclude Settings toggle button inside Exclude box on right (Gear icon, Image 3 & 4)
        float excBtnW = config.ScaleF(22.0f);
        float excBtnH = config.ScaleF(18.0f);
        float excBtnX = m_rcFilesToExcludeBox.right - excBtnW - config.ScaleF(4.0f);
        float excBtnY = curY + (inputH - excBtnH) / 2.0f;
        m_rcUseExcludeSettingsBtn = D2D1::RectF(excBtnX, excBtnY, excBtnX + excBtnW, excBtnY + excBtnH);
        D2D1_ROUNDED_RECT rrcGear = D2D1::RoundedRect(m_rcUseExcludeSettingsBtn, 2.5f, 2.5f);
        if (m_useExcludeSettings) {
            pRT->FillRoundedRectangle(rrcGear, theme.brushItemActive.Get());
            pRT->DrawRoundedRectangle(rrcGear, theme.brushBrandAccent.Get(), 1.0f);
        }

        // Draw crisp vector Gear icon (Settings / Exclude Settings)
        ID2D1Brush* gearBrush = m_useExcludeSettings ? theme.brushBrandAccent.Get() : theme.brushTextSecondary.Get();
        float gx = (m_rcUseExcludeSettingsBtn.left + m_rcUseExcludeSettingsBtn.right) * 0.5f;
        float gy = (m_rcUseExcludeSettingsBtn.top + m_rcUseExcludeSettingsBtn.bottom) * 0.5f;
        float gr = config.ScaleF(4.0f);
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(gx, gy), gr, gr), gearBrush, 1.2f);
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(gx, gy), gr * 0.45f, gr * 0.45f), gearBrush, 1.0f);
        for (int a = 0; a < 6; ++a) {
            float angle = static_cast<float>(a) * 3.14159265f / 3.0f;
            float x1 = gx + cosf(angle) * (gr - config.ScaleF(0.4f));
            float y1 = gy + sinf(angle) * (gr - config.ScaleF(0.4f));
            float x2 = gx + cosf(angle) * (gr + config.ScaleF(1.8f));
            float y2 = gy + sinf(angle) * (gr + config.ScaleF(1.8f));
            pRT->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), gearBrush, 1.6f);
        }

        // Exclude text or placeholder
        D2D1_RECT_F rcExcText = D2D1::RectF(
            m_rcFilesToExcludeBox.left + config.ScaleF(6.0f),
            curY + config.ScaleF(3.0f),
            excBtnX - config.ScaleF(4.0f),
            curY + inputH - config.ScaleF(3.0f)
        );
        float excTextWidth = 0.0f;
        if (!m_filesToExclude.empty()) {
            pRT->DrawText(m_filesToExclude.c_str(), static_cast<UINT32>(m_filesToExclude.length()),
                          theme.fontBody.Get(), rcExcText, theme.brushTextPrimary.Get());
            if (theme.dwriteFactory) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                        m_filesToExclude.c_str(), static_cast<UINT32>(m_filesToExclude.length()),
                        theme.fontBody.Get(), 1000.0f, inputH, &pLayout))) {
                    DWRITE_TEXT_METRICS tm{};
                    if (SUCCEEDED(pLayout->GetMetrics(&tm))) excTextWidth = tm.width;
                }
            }
        }
        if (m_focusedSearchInput == 4) {
            float caretX = rcExcText.left + excTextWidth + 1.0f;
            if (caretX < rcExcText.right) {
                pRT->DrawLine(D2D1::Point2F(caretX, curY + config.ScaleF(4.0f)),
                              D2D1::Point2F(caretX, curY + inputH - config.ScaleF(4.0f)),
                              theme.brushTextPrimary.Get(), 1.5f);
            }
        }
        curY += inputH + config.ScaleF(8.0f);
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

                        // Match snippet with matched keyword highlight
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

                        if (theme.dwriteFactory) {
                            Microsoft::WRL::ComPtr<IDWriteTextLayout> pSnippetLayout;
                            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                    trimmed.c_str(), static_cast<UINT32>(trimmed.length()),
                                    theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                                    rcSnip.right - rcSnip.left, rowH, &pSnippetLayout))) {

                                // Find matches in trimmed snippet
                                std::wstring qLower = m_searchQuery;
                                std::wstring sLower = trimmed;
                                if (!m_matchCase) {
                                    for (auto& ch : qLower) ch = towlower(ch);
                                    for (auto& ch : sLower) ch = towlower(ch);
                                }
                                const std::wstring& target = m_matchCase ? trimmed : sLower;
                                const std::wstring& pattern = m_matchCase ? m_searchQuery : qLower;

                                size_t pos = 0;
                                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hlFill;
                                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hlBorder;
                                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.58f, 0.12f, 0.40f), &hlFill);
                                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.58f, 0.12f, 0.85f), &hlBorder);

                                while (!pattern.empty() && (pos = target.find(pattern, pos)) != std::wstring::npos) {
                                    DWRITE_HIT_TEST_METRICS htm{};
                                    UINT32 count = 0;
                                    if (SUCCEEDED(pSnippetLayout->HitTestTextRange(
                                            static_cast<UINT32>(pos), static_cast<UINT32>(pattern.length()),
                                            0.0f, 0.0f, &htm, 1, &count)) && count > 0) {
                                        D2D1_RECT_F rcHL = D2D1::RectF(
                                            snipX + htm.left - 1.0f,
                                            curY + htm.top + 1.0f,
                                            snipX + htm.left + htm.width + 1.0f,
                                            curY + htm.top + htm.height - 1.0f
                                        );
                                        D2D1_ROUNDED_RECT rrcHL = D2D1::RoundedRect(rcHL, 2.0f, 2.0f);
                                        if (hlFill) pRT->FillRoundedRectangle(&rrcHL, hlFill.Get());
                                        if (hlBorder) pRT->DrawRoundedRectangle(&rrcHL, hlBorder.Get(), 1.0f);
                                    }
                                    pos += (std::max)(size_t(1), pattern.length());
                                }

                                pRT->DrawTextLayout(D2D1::Point2F(snipX, curY), pSnippetLayout.Get(), theme.brushTextPrimary.Get());
                            }
                        } else {
                            pRT->DrawText(trimmed.c_str(), static_cast<UINT32>(trimmed.length()),
                                          theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                                          rcSnip, theme.brushTextPrimary.Get());
                        }

                        curY += rowH;
                    }
                }
                curRowIdx++;
            }
        }
    }

    // 5. Floating Tooltip for Hovered Search Button
    if (!m_searchTooltipText.empty()) {
        const float tipH = config.ScaleF(22.0f);
        const float tipPadX = config.ScaleF(8.0f);
        size_t textLen = m_searchTooltipText.length();
        float textW = config.ScaleF(static_cast<float>(textLen * 6.5f + 12.0f));
        float tipW = textW + tipPadX * 2.0f;

        float tipX = m_searchTooltipAnchor.left + (m_searchTooltipAnchor.right - m_searchTooltipAnchor.left - tipW) / 2.0f;
        float tipY = m_searchTooltipAnchor.bottom + config.ScaleF(4.0f);

        // Keep inside drawer viewport
        if (tipX < railW + config.ScaleF(6.0f)) tipX = railW + config.ScaleF(6.0f);
        if (tipX + tipW > railW + fullDrawerW - config.ScaleF(6.0f)) tipX = railW + fullDrawerW - tipW - config.ScaleF(6.0f);

        D2D1_RECT_F rcTip = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
        D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 4.0f, 4.0f);

        pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg.Get());
        pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder.Get(), 1.0f);

        pRT->DrawText(
            m_searchTooltipText.c_str(),
            static_cast<UINT32>(textLen),
            theme.fontTooltip ? theme.fontTooltip.Get() : theme.fontHint.Get(),
            rcTip,
            theme.brushTooltipText ? theme.brushTooltipText.Get() : theme.brushTextPrimary.Get()
        );
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

    // 2. Match Case (Aa) button
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

    // 10b. Toggle Search Details (...) button
    if (pt.x >= m_rcToggleSearchDetailsBtn.left && pt.x <= m_rcToggleSearchDetailsBtn.right &&
        pt.y >= m_rcToggleSearchDetailsBtn.top && pt.y <= m_rcToggleSearchDetailsBtn.bottom) {
        m_isSearchDetailsOpen = !m_isSearchDetailsOpen;
        return true;
    }

    // 10c. Include / Exclude Inputs & Toggles (when Search Details is open)
    if (m_isSearchDetailsOpen) {
        // Book / Search Open Tabs Only icon button (Images 3 & 4)
        if (pt.x >= m_rcOpenEditorsOnlyBtn.left && pt.x <= m_rcOpenEditorsOnlyBtn.right &&
            pt.y >= m_rcOpenEditorsOnlyBtn.top && pt.y <= m_rcOpenEditorsOnlyBtn.bottom) {
            m_searchOnlyOpenEditors = !m_searchOnlyOpenEditors;
            ExecuteSearch();
            return true;
        }

        // Gear / Use Exclude Settings icon button (Images 3 & 4)
        if (pt.x >= m_rcUseExcludeSettingsBtn.left && pt.x <= m_rcUseExcludeSettingsBtn.right &&
            pt.y >= m_rcUseExcludeSettingsBtn.top && pt.y <= m_rcUseExcludeSettingsBtn.bottom) {
            m_useExcludeSettings = !m_useExcludeSettings;
            ExecuteSearch();
            return true;
        }

        // Files to Include Box
        if (pt.x >= m_rcFilesToIncludeBox.left && pt.x <= m_rcFilesToIncludeBox.right &&
            pt.y >= m_rcFilesToIncludeBox.top && pt.y <= m_rcFilesToIncludeBox.bottom) {
            m_focusedSearchInput = 3;
            return true;
        }

        // Files to Exclude Box
        if (pt.x >= m_rcFilesToExcludeBox.left && pt.x <= m_rcFilesToExcludeBox.right &&
            pt.y >= m_rcFilesToExcludeBox.top && pt.y <= m_rcFilesToExcludeBox.bottom) {
            m_focusedSearchInput = 4;
            return true;
        }
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

void DrawerPanel::ClearSearchResults() {
    for (auto& g : m_searchGroups) {
        g.matches.clear();
        g.matches.shrink_to_fit();
    }
    m_searchGroups.clear();
    m_searchGroups.shrink_to_fit();
    m_totalMatches = 0;
    m_searchCandidateFiles.clear();
    m_searchCandidateFileIndex = 0;
    m_searchHasMore = false;
    m_isBatchAppend = false;
}

void DrawerPanel::TriggerSearchThrottled(ULONGLONG nowMs) {
    (void)nowMs;
    m_searchPending = true;
    m_lastKeystrokeTime = GetTickCount64();
}

bool DrawerPanel::PollSearchThrottle(ULONGLONG nowMs) {
    bool needRepaint = false;

    // 1. Process pending throttled keystroke: only fire if user paused typing for 150ms and throttle interval elapsed
    if (m_searchPending) {
        if ((nowMs - m_lastKeystrokeTime >= 150) && (nowMs - m_lastSearchExecutionTime >= m_throttleIntervalMs)) {
            m_searchPending = false;
            m_lastSearchExecutionTime = nowMs;
            LaunchAsyncSearch();
        }
    }

    // 2. Consume completed background results (thread-safe swap or incremental append)
    if (m_hasPendingResults) {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        if (m_isBatchAppend) {
            for (auto& g : m_pendingSearchGroups) {
                m_searchGroups.push_back(std::move(g));
            }
            m_totalMatches += m_pendingTotalMatches;
            m_pendingSearchGroups.clear();
            m_pendingTotalMatches = 0;
            m_isBatchAppend = false;
        } else {
            for (auto& g : m_searchGroups) {
                g.matches.clear();
                g.matches.shrink_to_fit();
            }
            m_searchGroups.clear();
            m_searchGroups.shrink_to_fit();
            m_searchGroups = std::move(m_pendingSearchGroups);
            m_totalMatches = m_pendingTotalMatches;
        }
        m_hasPendingResults = false;
        needRepaint = true;
    }

    return needRepaint;
}

void DrawerPanel::HandleSearchChar(wchar_t ch) {
    if (ch < 32) return;

    if (m_focusedSearchInput == 1) {
        m_searchQuery.push_back(ch);
        m_searchPending = true;
        m_lastKeystrokeTime = GetTickCount64();
    } else if (m_focusedSearchInput == 2) {
        m_replaceQuery.push_back(ch);
    } else if (m_focusedSearchInput == 3) {
        m_filesToInclude.push_back(ch);
        m_searchPending = true;
        m_lastKeystrokeTime = GetTickCount64();
    } else if (m_focusedSearchInput == 4) {
        m_filesToExclude.push_back(ch);
        m_searchPending = true;
        m_lastKeystrokeTime = GetTickCount64();
    }
}

void DrawerPanel::HandleSearchKeyDown(WPARAM wParam) {
    if (wParam == VK_BACK) {
        if (m_focusedSearchInput == 1 && !m_searchQuery.empty()) {
            m_searchQuery.pop_back();
            m_searchPending = true;
            m_lastKeystrokeTime = GetTickCount64();
            if (m_searchQuery.empty()) {
                ClearSearchResults();
            }
        } else if (m_focusedSearchInput == 2 && !m_replaceQuery.empty()) {
            m_replaceQuery.pop_back();
        } else if (m_focusedSearchInput == 3 && !m_filesToInclude.empty()) {
            m_filesToInclude.pop_back();
            m_searchPending = true;
            m_lastKeystrokeTime = GetTickCount64();
        } else if (m_focusedSearchInput == 4 && !m_filesToExclude.empty()) {
            m_filesToExclude.pop_back();
            m_searchPending = true;
            m_lastKeystrokeTime = GetTickCount64();
        }
    } else if (wParam == VK_TAB) {
        if (m_focusedSearchInput == 1) {
            m_focusedSearchInput = m_isReplaceOpen ? 2 : (m_isSearchDetailsOpen ? 3 : 1);
        } else if (m_focusedSearchInput == 2) {
            m_focusedSearchInput = m_isSearchDetailsOpen ? 3 : 1;
        } else if (m_focusedSearchInput == 3) {
            m_focusedSearchInput = 4;
        } else if (m_focusedSearchInput == 4) {
            m_focusedSearchInput = 1;
        }
    } else if (wParam == VK_ESCAPE) {
        m_focusedSearchInput = 0;
    } else if (wParam == VK_RETURN) {
        m_searchPending = false;
        m_lastSearchExecutionTime = GetTickCount64();
        LaunchAsyncSearch();
    }
}

/**
 * @brief Searches an individual file for occurrences of query or regex.
 */
static bool SearchSingleFile(
    const std::filesystem::path& filePath,
    const std::filesystem::path& root,
    const std::wstring& query,
    const std::wstring& lowerQuery,
    bool matchCase,
    bool matchWord,
    bool useRegex,
    bool regexValid,
    const std::wregex& regexPattern,
    SearchFileGroup& outGroup,
    size_t& inOutTotalMatches,
    size_t maxMatchesPerFile,
    size_t maxTotalMatches
) {
    std::error_code ec;
    auto filename = filePath.filename().wstring();

    // Skip binary extensions (video, images, executables, libraries, archives, audio, fonts)
    auto ext = filePath.extension().wstring();
    for (auto& ch : ext) ch = towlower(ch);
    if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" ||
        ext == L".bmp" || ext == L".ico" || ext == L".webp" || ext == L".tiff" ||
        ext == L".tif" || ext == L".psd" || ext == L".raw"  ||
        ext == L".mp4" || ext == L".mkv" || ext == L".avi"  || ext == L".mov" ||
        ext == L".wmv" || ext == L".flv" || ext == L".webm" || ext == L".mpg" ||
        ext == L".mpeg"|| ext == L".m4v" || ext == L".3gp"  ||
        ext == L".mp3" || ext == L".wav" || ext == L".ogg"  || ext == L".flac" ||
        ext == L".aac" || ext == L".m4a" || ext == L".wma"  ||
        ext == L".exe" || ext == L".dll" || ext == L".obj"  || ext == L".bin" ||
        ext == L".lib" || ext == L".pdb" || ext == L".so"   || ext == L".dylib" ||
        ext == L".sys" || ext == L".o"   || ext == L".a"    || ext == L".class" ||
        ext == L".pyc" || ext == L".wasm"|| ext == L".zip"  || ext == L".rar" ||
        ext == L".7z"  || ext == L".tar" || ext == L".gz"   || ext == L".iso" ||
        ext == L".ttf" || ext == L".otf" || ext == L".woff" || ext == L".woff2") {
        return false;
    }

    // Skip files > 2MB
    auto fileSize = std::filesystem::file_size(filePath, ec);
    if (ec || fileSize > 2 * 1024 * 1024) {
        return false;
    }

    // Inspect initial bytes for null characters (binary file check)
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    char headerBuf[512];
    file.read(headerBuf, sizeof(headerBuf));
    std::streamsize bytesRead = file.gcount();
    for (std::streamsize b = 0; b < bytesRead; ++b) {
        if (headerBuf[b] == '\0') {
            file.close();
            return false;
        }
    }

    file.clear();
    file.seekg(0, std::ios::beg);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (content.find('\0') != std::string::npos) return false;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wcontent(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), &wcontent[0], wlen);

    outGroup.filePath = filePath.wstring();
    outGroup.fileName = filename;
    std::filesystem::path rel = std::filesystem::relative(filePath.parent_path(), root, ec);
    outGroup.relDir = ec ? L"" : rel.wstring();
    if (outGroup.relDir == L".") outGroup.relDir.clear();
    outGroup.isExpanded = true;

    size_t lineIdx = 0;
    size_t pos = 0;
    while (pos < wcontent.size() && outGroup.matches.size() < maxMatchesPerFile && inOutTotalMatches < maxTotalMatches) {
        size_t nextNL = wcontent.find(L'\n', pos);
        if (nextNL == std::wstring::npos) nextNL = wcontent.size();

        std::wstring lineStr = wcontent.substr(pos, nextNL - pos);
        if (!lineStr.empty() && lineStr.back() == L'\r') lineStr.pop_back();

        // Skip extremely long single-line files (e.g. minified single-line bundle > 30,000 chars)
        if (lineStr.size() > 30000) {
            pos = nextNL + 1;
            lineIdx++;
            continue;
        }

        if (useRegex && regexValid) {
            std::wsregex_iterator regIt(lineStr.begin(), lineStr.end(), regexPattern);
            std::wsregex_iterator regEnd;
            for (; regIt != regEnd && outGroup.matches.size() < maxMatchesPerFile && inOutTotalMatches < maxTotalMatches; ++regIt) {
                size_t matchCol = static_cast<size_t>(regIt->position());
                size_t matchLen = static_cast<size_t>(regIt->length());

                size_t snipStart = (matchCol > 40) ? (matchCol - 40) : 0;
                size_t snipEnd = (std::min)(lineStr.size(), matchCol + matchLen + 80);

                SearchMatch match;
                match.lineIndex = lineIdx;
                match.lineText = lineStr.substr(snipStart, snipEnd - snipStart);
                match.matchCol = matchCol - snipStart;
                match.matchLen = matchLen;
                outGroup.matches.push_back(std::move(match));
                inOutTotalMatches++;
            }
        } else {
            std::wstring searchLine = lineStr;
            if (!matchCase) {
                std::transform(searchLine.begin(), searchLine.end(), searchLine.begin(), ::towlower);
            }

            size_t findPos = 0;
            while ((findPos = searchLine.find(lowerQuery, findPos)) != std::wstring::npos &&
                   outGroup.matches.size() < maxMatchesPerFile && inOutTotalMatches < maxTotalMatches) {
                bool valid = true;
                if (matchWord) {
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
                    size_t snipStart = (findPos > 40) ? (findPos - 40) : 0;
                    size_t snipEnd = (std::min)(lineStr.size(), findPos + lowerQuery.size() + 80);

                    SearchMatch match;
                    match.lineIndex = lineIdx;
                    match.lineText = lineStr.substr(snipStart, snipEnd - snipStart);
                    match.matchCol = findPos - snipStart;
                    match.matchLen = query.size();
                    outGroup.matches.push_back(std::move(match));
                    inOutTotalMatches++;
                }
                findPos += (std::max)(size_t(1), lowerQuery.size());
            }
        }

        pos = nextNL + 1;
        lineIdx++;
    }

    return !outGroup.matches.empty();
}

void DrawerPanel::LaunchAsyncSearch() {
    uint32_t thisGen = ++m_searchGeneration;

    std::wstring query = m_searchQuery;
    std::wstring rootFolder = m_openFolderPath;
    bool matchCase = m_matchCase;
    bool matchWord = m_matchWholeWord;
    bool useRegex = m_useRegex;
    bool searchOnlyOpenEditors = m_searchOnlyOpenEditors;
    std::vector<std::wstring> openDocPaths = m_openDocPaths;
    std::wstring filesToInclude = m_filesToInclude;
    std::wstring filesToExclude = m_filesToExclude;
    bool useExcludeSettings = m_useExcludeSettings;

    if (query.empty() || (!searchOnlyOpenEditors && (rootFolder.empty() || !m_hasOpenFolder))) {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        for (auto& g : m_pendingSearchGroups) {
            g.matches.clear();
            g.matches.shrink_to_fit();
        }
        m_pendingSearchGroups.clear();
        m_pendingSearchGroups.shrink_to_fit();
        m_pendingTotalMatches = 0;
        m_searchCandidateFiles.clear();
        m_searchCandidateFileIndex = 0;
        m_searchHasMore = false;
        m_isBatchAppend = false;
        m_hasPendingResults = true;
        m_isSearching = false;
        return;
    }

    m_isSearching = true;

    // Run file traversal in worker thread to prevent freezing the GUI thread!
    std::thread([this, thisGen, query, rootFolder, matchCase, matchWord, useRegex,
                 searchOnlyOpenEditors, openDocPaths, filesToInclude, filesToExclude, useExcludeSettings]() {
        try {
            std::error_code ec;
            std::filesystem::path root(rootFolder);

            std::wregex regexPattern;
            bool regexValid = false;
            if (useRegex) {
                try {
                    auto flags = std::regex_constants::ECMAScript;
                    if (!matchCase) flags |= std::regex_constants::icase;
                    regexPattern = std::wregex(query, flags);
                    regexValid = true;
                } catch (...) {
                    regexValid = false;
                }
            }

            std::wstring lowerQuery = query;
            if (!matchCase) {
                std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);
            }

            // Collect files to search based on "Search Open Tabs Only" toggle and include/exclude patterns
            std::vector<std::filesystem::path> filesToProcess;
            if (searchOnlyOpenEditors) {
                for (const auto& op : openDocPaths) {
                    std::filesystem::path fp(op);
                    std::error_code fec;
                    if (std::filesystem::exists(fp, fec) && std::filesystem::is_regular_file(fp, fec)) {
                        auto fname = fp.filename().wstring();
                        auto fstr = fp.wstring();
                        if (!filesToInclude.empty() && !MatchesPatternList(fname, fstr, filesToInclude)) continue;
                        if (!filesToExclude.empty() && MatchesPatternList(fname, fstr, filesToExclude)) continue;
                        filesToProcess.push_back(fp);
                    }
                }
            } else {
                for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
                     it != end; it.increment(ec)) {
                    if (m_searchGeneration.load() != thisGen) {
                        m_isSearching = false;
                        return;
                    }
                    if (ec) { ec.clear(); continue; }

                    const auto& entry = *it;
                    auto filename = entry.path().filename().wstring();

                    if (!filename.empty() && filename[0] == L'.') {
                        if (entry.is_directory(ec)) it.disable_recursion_pending();
                        continue;
                    }

                    if (entry.is_directory(ec)) {
                        if (useExcludeSettings && (filename == L"node_modules" || filename == L"build" || filename == L"bin" ||
                            filename == L"obj" || filename == L"target" || filename == L".git" ||
                            filename == L"dist" || filename == L"out" || filename == L"vendor" ||
                            filename == L".cache" || filename == L"tmp" || filename == L"temp" ||
                            filename == L"coverage" || filename == L".next" || filename == L".nuxt")) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }

                    if (!entry.is_regular_file(ec)) continue;

                    auto fstr = entry.path().wstring();
                    if (!filesToInclude.empty() && !MatchesPatternList(filename, fstr, filesToInclude)) continue;
                    if (!filesToExclude.empty() && MatchesPatternList(filename, fstr, filesToExclude)) continue;

                    filesToProcess.push_back(entry.path());
                }
            }

            // Search first visible batch: target up to 12 files with matches or 35 matches, or 150 scanned files
            std::vector<SearchFileGroup> initialGroups;
            size_t batchMatches = 0;
            size_t processedIdx = 0;
            const size_t BATCH_FILE_LIMIT = 12;
            const size_t BATCH_MATCH_LIMIT = 35;
            const size_t MAX_SCAN_PER_CHUNK = 150;

            for (; processedIdx < filesToProcess.size(); ++processedIdx) {
                if (m_searchGeneration.load() != thisGen) {
                    m_isSearching = false;
                    return;
                }
                if (initialGroups.size() >= BATCH_FILE_LIMIT || batchMatches >= BATCH_MATCH_LIMIT || processedIdx >= MAX_SCAN_PER_CHUNK) {
                    break;
                }
                SearchFileGroup group;
                if (SearchSingleFile(filesToProcess[processedIdx], root, query, lowerQuery,
                                     matchCase, matchWord, useRegex, regexValid, regexPattern,
                                     group, batchMatches, 50, 1000)) {
                    initialGroups.push_back(std::move(group));
                }
            }

            if (m_searchGeneration.load() == thisGen) {
                std::lock_guard<std::mutex> lock(m_searchMutex);
                m_pendingSearchGroups = std::move(initialGroups);
                m_pendingTotalMatches = batchMatches;
                m_searchCandidateFiles = std::move(filesToProcess);
                m_searchCandidateFileIndex = processedIdx;
                m_searchHasMore = (m_searchCandidateFileIndex < m_searchCandidateFiles.size());
                m_isBatchAppend = false;
                m_hasPendingResults = true;
            }
        } catch (...) {
        }
        m_isSearching = false;
    }).detach();
}

void DrawerPanel::FetchNextSearchBatch() {
    if (!m_searchHasMore || m_isFetchingMore.load() || m_isSearching.load()) return;
    m_isFetchingMore = true;

    uint32_t thisGen = m_searchGeneration.load();
    std::wstring query = m_searchQuery;
    std::wstring rootFolder = m_openFolderPath;
    bool matchCase = m_matchCase;
    bool matchWord = m_matchWholeWord;
    bool useRegex = m_useRegex;

    std::thread([this, thisGen, query, rootFolder, matchCase, matchWord, useRegex]() {
        try {
            std::filesystem::path root(rootFolder);
            std::wregex regexPattern;
            bool regexValid = false;
            if (useRegex) {
                try {
                    auto flags = std::regex_constants::ECMAScript;
                    if (!matchCase) flags |= std::regex_constants::icase;
                    regexPattern = std::wregex(query, flags);
                    regexValid = true;
                } catch (...) {
                    regexValid = false;
                }
            }

            std::wstring lowerQuery = query;
            if (!matchCase) {
                std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);
            }

            std::vector<SearchFileGroup> nextGroups;
            size_t batchMatches = 0;
            const size_t BATCH_FILE_LIMIT = 12;
            const size_t BATCH_MATCH_LIMIT = 35;
            const size_t MAX_SCAN_PER_CHUNK = 150;

            size_t startIdx = m_searchCandidateFileIndex;
            size_t endIdx = startIdx;

            for (; endIdx < m_searchCandidateFiles.size(); ++endIdx) {
                if (m_searchGeneration.load() != thisGen) {
                    m_isFetchingMore = false;
                    return;
                }
                if (nextGroups.size() >= BATCH_FILE_LIMIT || batchMatches >= BATCH_MATCH_LIMIT || (endIdx - startIdx) >= MAX_SCAN_PER_CHUNK) {
                    break;
                }
                SearchFileGroup group;
                if (SearchSingleFile(m_searchCandidateFiles[endIdx], root, query, lowerQuery,
                                     matchCase, matchWord, useRegex, regexValid, regexPattern,
                                     group, batchMatches, 50, 1000)) {
                    nextGroups.push_back(std::move(group));
                }
            }

            if (m_searchGeneration.load() == thisGen) {
                std::lock_guard<std::mutex> lock(m_searchMutex);
                m_pendingSearchGroups = std::move(nextGroups);
                m_pendingTotalMatches = batchMatches;
                m_searchCandidateFileIndex = endIdx;
                m_searchHasMore = (m_searchCandidateFileIndex < m_searchCandidateFiles.size());
                m_isBatchAppend = true;
                m_hasPendingResults = true;
            }
        } catch (...) {
        }
        m_isFetchingMore = false;
    }).detach();
}

void DrawerPanel::ExecuteSearch(bool async) {
    if (async) {
        LaunchAsyncSearch();
        return;
    }

    ClearSearchResults();
    m_searchScrollIdx = 0;
    if (m_searchQuery.empty() || (!m_searchOnlyOpenEditors && (!m_hasOpenFolder || m_openFolderPath.empty()))) {
        return;
    }

    std::error_code ec;
    std::filesystem::path root(m_openFolderPath);
    if (!m_searchOnlyOpenEditors && !std::filesystem::exists(root, ec)) return;

    std::wregex regexPattern;
    bool regexValid = false;
    if (m_useRegex) {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!m_matchCase) flags |= std::regex_constants::icase;
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

    const size_t MAX_MATCHES_PER_FILE = 50;
    const size_t MAX_TOTAL_MATCHES = 1000;

    std::vector<std::filesystem::path> filesToProcess;
    if (m_searchOnlyOpenEditors) {
        for (const auto& op : m_openDocPaths) {
            std::filesystem::path fp(op);
            std::error_code fec;
            if (std::filesystem::exists(fp, fec) && std::filesystem::is_regular_file(fp, fec)) {
                auto fname = fp.filename().wstring();
                auto fstr = fp.wstring();
                if (!m_filesToInclude.empty() && !MatchesPatternList(fname, fstr, m_filesToInclude)) continue;
                if (!m_filesToExclude.empty() && MatchesPatternList(fname, fstr, m_filesToExclude)) continue;
                filesToProcess.push_back(fp);
            }
        }
    } else {
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
                if (entry.is_directory(ec)) it.disable_recursion_pending();
                continue;
            }

            if (entry.is_directory(ec)) {
                if (m_useExcludeSettings && (filename == L"node_modules" || filename == L"build" || filename == L"bin" ||
                    filename == L"obj" || filename == L"target" || filename == L".git" ||
                    filename == L"dist" || filename == L"out" || filename == L"vendor" ||
                    filename == L".cache" || filename == L"tmp" || filename == L"temp" ||
                    filename == L"coverage" || filename == L".next" || filename == L".nuxt")) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            if (!entry.is_regular_file(ec)) continue;

            auto fstr = entry.path().wstring();
            if (!m_filesToInclude.empty() && !MatchesPatternList(filename, fstr, m_filesToInclude)) continue;
            if (!m_filesToExclude.empty() && MatchesPatternList(filename, fstr, m_filesToExclude)) continue;

            filesToProcess.push_back(entry.path());
        }
    }

    for (const auto& filePath : filesToProcess) {
        auto filename = filePath.filename().wstring();

        // Skip binary file extensions
        auto ext = filePath.extension().wstring();
        for (auto& ch : ext) ch = towlower(ch);
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" ||
            ext == L".bmp" || ext == L".ico" || ext == L".webp" || ext == L".tiff" ||
            ext == L".tif" || ext == L".psd" || ext == L".raw"  ||
            ext == L".mp4" || ext == L".mkv" || ext == L".avi"  || ext == L".mov" ||
            ext == L".wmv" || ext == L".flv" || ext == L".webm" || ext == L".mpg" ||
            ext == L".mpeg"|| ext == L".m4v" || ext == L".3gp"  ||
            ext == L".mp3" || ext == L".wav" || ext == L".ogg"  || ext == L".flac" ||
            ext == L".aac" || ext == L".m4a" || ext == L".wma"  ||
            ext == L".exe" || ext == L".dll" || ext == L".obj"  || ext == L".bin" ||
            ext == L".lib" || ext == L".pdb" || ext == L".so"   || ext == L".dylib" ||
            ext == L".sys" || ext == L".o"   || ext == L".a"    || ext == L".class" ||
            ext == L".pyc" || ext == L".wasm"|| ext == L".zip"  || ext == L".rar" ||
            ext == L".7z"  || ext == L".tar" || ext == L".gz"   || ext == L".iso" ||
            ext == L".ttf" || ext == L".otf" || ext == L".woff" || ext == L".woff2") {
            continue;
        }

        auto fileSize = std::filesystem::file_size(filePath, ec);
        if (ec || fileSize > 2 * 1024 * 1024) {
            ec.clear();
            continue;
        }

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) continue;

        char headerBuf[512];
        file.read(headerBuf, sizeof(headerBuf));
        std::streamsize bytesRead = file.gcount();
        bool isBinary = false;
        for (std::streamsize b = 0; b < bytesRead; ++b) {
            if (headerBuf[b] == '\0') {
                isBinary = true;
                break;
            }
        }
        if (isBinary) {
            file.close();
            continue;
        }

        file.clear();
        file.seekg(0, std::ios::beg);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        if (content.find('\0') != std::string::npos) continue;

        int wlen = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), nullptr, 0);
        if (wlen <= 0) continue;
        std::wstring wcontent(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), &wcontent[0], wlen);

        SearchFileGroup group;
        group.filePath = filePath.wstring();
        group.fileName = filename;
        std::filesystem::path rel = std::filesystem::relative(filePath.parent_path(), root, ec);
        group.relDir = ec ? L"" : rel.wstring();
        if (group.relDir == L".") group.relDir.clear();
        group.isExpanded = true;

        size_t lineIdx = 0;
        size_t pos = 0;
        while (pos < wcontent.size() && group.matches.size() < MAX_MATCHES_PER_FILE && m_totalMatches < MAX_TOTAL_MATCHES) {
            size_t nextNL = wcontent.find(L'\n', pos);
            if (nextNL == std::wstring::npos) nextNL = wcontent.size();

            std::wstring lineStr = wcontent.substr(pos, nextNL - pos);
            if (!lineStr.empty() && lineStr.back() == L'\r') lineStr.pop_back();

            if (lineStr.size() > 30000) {
                pos = nextNL + 1;
                lineIdx++;
                continue;
            }

            if (m_useRegex && regexValid) {
                std::wsregex_iterator regIt(lineStr.begin(), lineStr.end(), regexPattern);
                std::wsregex_iterator regEnd;
                for (; regIt != regEnd && group.matches.size() < MAX_MATCHES_PER_FILE && m_totalMatches < MAX_TOTAL_MATCHES; ++regIt) {
                    size_t matchCol = static_cast<size_t>(regIt->position());
                    size_t matchLen = static_cast<size_t>(regIt->length());

                    size_t snipStart = (matchCol > 40) ? (matchCol - 40) : 0;
                    size_t snipEnd = (std::min)(lineStr.size(), matchCol + matchLen + 80);

                    SearchMatch match;
                    match.lineIndex = lineIdx;
                    match.lineText = lineStr.substr(snipStart, snipEnd - snipStart);
                    match.matchCol = matchCol - snipStart;
                    match.matchLen = matchLen;
                    group.matches.push_back(std::move(match));
                    m_totalMatches++;
                }
            } else {
                std::wstring searchLine = lineStr;
                if (!m_matchCase) {
                    std::transform(searchLine.begin(), searchLine.end(), searchLine.begin(), ::towlower);
                }

                size_t findPos = 0;
                while ((findPos = searchLine.find(lowerQuery, findPos)) != std::wstring::npos &&
                       group.matches.size() < MAX_MATCHES_PER_FILE && m_totalMatches < MAX_TOTAL_MATCHES) {
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
                        size_t snipStart = (findPos > 40) ? (findPos - 40) : 0;
                        size_t snipEnd = (std::min)(lineStr.size(), findPos + lowerQuery.size() + 80);

                        SearchMatch match;
                        match.lineIndex = lineIdx;
                        match.lineText = lineStr.substr(snipStart, snipEnd - snipStart);
                        match.matchCol = findPos - snipStart;
                        match.matchLen = m_searchQuery.size();
                        group.matches.push_back(std::move(match));
                        m_totalMatches++;
                    }
                    findPos += (std::max)(size_t(1), lowerQuery.size());
                }
            }

            pos = nextNL + 1;
            lineIdx++;
        }

        if (!group.matches.empty()) {
            m_searchGroups.push_back(std::move(group));
        }

        if (m_totalMatches >= MAX_TOTAL_MATCHES) break;
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

bool DrawerPanel::HitTestGitSplitter(POINT pt) const noexcept {
    if (!m_showChangesSection || !m_showGraphSection) return false;
    return (pt.x >= m_cachedRailW && pt.x <= m_cachedRailW + m_cachedDrawerW &&
            std::abs(static_cast<float>(pt.y) - m_gitSplitY) <= 5.0f);
}

void DrawerPanel::SetGitSplitY(float y, float minY, float maxY) noexcept {
    m_gitSplitY = (std::max)(minY, (std::min)(maxY, y));
}

bool DrawerPanel::HandleMouseWheel(short delta, int activeRailTab, float mouseY) {
    if (activeRailTab == 2) {
        if (m_gitCommits.empty() && m_gitChanges.empty()) return false;
        if (m_showChangesSection && m_showGraphSection && mouseY > 0.0f && mouseY < m_gitSplitY) {
            // Scroll Changes list
            size_t oldScroll = m_gitChangesScrollIdx;
            if (delta > 0) {
                if (m_gitChangesScrollIdx >= 2) m_gitChangesScrollIdx -= 2;
                else m_gitChangesScrollIdx = 0;
            } else {
                if (m_gitChangesScrollIdx + 2 < m_gitChanges.size()) m_gitChangesScrollIdx += 2;
            }
            return m_gitChangesScrollIdx != oldScroll;
        } else {
            // Scroll Graph list
            size_t oldScroll = m_gitGraphScrollIdx;
            if (delta > 0) {
                if (m_gitGraphScrollIdx >= 2) m_gitGraphScrollIdx -= 2;
                else m_gitGraphScrollIdx = 0;
            } else {
                if (!m_gitCommits.empty() && m_gitGraphScrollIdx + 2 < m_gitCommits.size()) m_gitGraphScrollIdx += 2;
            }
            return m_gitGraphScrollIdx != oldScroll;
        }
    }

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
            // Incremental lazy loading: if scrolling down and more candidate files remain, fetch next batch
            if (m_searchHasMore && !m_isFetchingMore.load()) {
                FetchNextSearchBatch();
            }
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

/**
 * @brief Initiates inline rename mode on a visible tree item by its index.
 * @param itemIdx Index in m_visibleItems to rename.
 */
void DrawerPanel::StartInlineRename(int itemIdx) {
    if (itemIdx < 0 || itemIdx >= static_cast<int>(m_visibleItems.size())) return;
    m_isRenaming = true;
    m_renamingItemIdx = itemIdx;
    m_renameOriginalPath = m_visibleItems[itemIdx].fullPath;
    m_renameText = m_visibleItems[itemIdx].label;

    // Pre-select filename excluding extension (matching VS Code / Windows Explorer standard)
    if (!m_visibleItems[itemIdx].isFolder) {
        size_t dotPos = m_renameText.find_last_of(L'.');
        if (dotPos != std::wstring::npos && dotPos > 0) {
            m_renameSelStart = 0;
            m_renameSelEnd = dotPos;
            m_renameCaretPos = dotPos;
        } else {
            m_renameSelStart = 0;
            m_renameSelEnd = m_renameText.length();
            m_renameCaretPos = m_renameText.length();
        }
    } else {
        m_renameSelStart = 0;
        m_renameSelEnd = m_renameText.length();
        m_renameCaretPos = m_renameText.length();
    }
}

/**
 * @brief Initiates inline rename mode on a tree item matching full filesystem path.
 * @param fullPath Canonical filesystem path of the item to rename.
 */
void DrawerPanel::StartInlineRenameByPath(const std::wstring& fullPath) {
    if (fullPath.empty()) return;
    for (size_t i = 0; i < m_visibleItems.size(); ++i) {
        if (m_visibleItems[i].fullPath == fullPath) {
            StartInlineRename(static_cast<int>(i));
            return;
        }
    }
    std::filesystem::path p(fullPath);
    for (size_t i = 0; i < m_visibleItems.size(); ++i) {
        if (m_visibleItems[i].label == p.filename().wstring()) {
            StartInlineRename(static_cast<int>(i));
            return;
        }
    }
}

/**
 * @brief Cancels inline rename mode and discards user input.
 */
void DrawerPanel::CancelInlineRename() {
    m_isRenaming = false;
    m_renamingItemIdx = -1;
    m_renameText.clear();
    m_renameOriginalPath.clear();
    m_renameSelStart = 0;
    m_renameSelEnd = 0;
    m_renameCaretPos = 0;
    m_rcRenameBox = D2D1::RectF(0, 0, 0, 0);
}

/**
 * @brief Commits inline rename, renaming the target file/folder on disk.
 * @param outOldPath Receives the original filesystem path.
 * @param outNewPath Receives the newly renamed filesystem path.
 * @return True if the rename operation succeeded on disk.
 */
bool DrawerPanel::CommitInlineRename(std::wstring& outOldPath, std::wstring& outNewPath) {
    outOldPath.clear();
    outNewPath.clear();
    if (!m_isRenaming || m_renameOriginalPath.empty() || m_renameText.empty()) {
        CancelInlineRename();
        return false;
    }

    std::filesystem::path oldP(m_renameOriginalPath);
    if (m_renameText == oldP.filename().wstring()) {
        CancelInlineRename();
        return false;
    }

    std::filesystem::path newP = oldP.parent_path() / m_renameText;
    std::error_code ec;
    std::filesystem::rename(oldP, newP, ec);
    if (!ec) {
        outOldPath = oldP.wstring();
        outNewPath = newP.wstring();
        CancelInlineRename();
        RefreshTree();
        return true;
    }

    CancelInlineRename();
    return false;
}

/**
 * @brief Handles character typed during inline rename, filtering illegal filename characters.
 * @param ch Wide character code.
 */
void DrawerPanel::HandleRenameChar(wchar_t ch) {
    if (!m_isRenaming) return;
    if (ch >= 32 && ch != L'\\' && ch != L'/' && ch != L':' && ch != L'*' &&
        ch != L'?' && ch != L'\"' && ch != L'<' && ch != L'>' && ch != L'|') {
        if (m_renameSelEnd > m_renameSelStart && m_renameSelEnd <= m_renameText.length()) {
            m_renameText.erase(m_renameSelStart, m_renameSelEnd - m_renameSelStart);
            m_renameText.insert(m_renameSelStart, 1, ch);
            m_renameCaretPos = m_renameSelStart + 1;
            m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
        } else {
            if (m_renameCaretPos > m_renameText.length()) m_renameCaretPos = m_renameText.length();
            m_renameText.insert(m_renameCaretPos, 1, ch);
            m_renameCaretPos++;
            m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
        }
    }
}

/**
 * @brief Handles special keys during inline rename (Backspace, Escape).
 * @param wParam Virtual key code.
 */
void DrawerPanel::HandleRenameKeyDown(WPARAM wParam) {
    if (!m_isRenaming) return;
    if (wParam == VK_BACK) {
        if (m_renameSelEnd > m_renameSelStart && m_renameSelEnd <= m_renameText.length()) {
            m_renameText.erase(m_renameSelStart, m_renameSelEnd - m_renameSelStart);
            m_renameCaretPos = m_renameSelStart;
            m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
        } else if (m_renameCaretPos > 0 && m_renameCaretPos <= m_renameText.length()) {
            m_renameText.erase(m_renameCaretPos - 1, 1);
            m_renameCaretPos--;
            m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
        }
    } else if (wParam == VK_DELETE) {
        if (m_renameSelEnd > m_renameSelStart && m_renameSelEnd <= m_renameText.length()) {
            m_renameText.erase(m_renameSelStart, m_renameSelEnd - m_renameSelStart);
            m_renameCaretPos = m_renameSelStart;
            m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
        } else if (m_renameCaretPos < m_renameText.length()) {
            m_renameText.erase(m_renameCaretPos, 1);
            m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
        }
    } else if (wParam == VK_LEFT) {
        if (m_renameCaretPos > 0) m_renameCaretPos--;
        m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
    } else if (wParam == VK_RIGHT) {
        if (m_renameCaretPos < m_renameText.length()) m_renameCaretPos++;
        m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
    } else if (wParam == VK_HOME) {
        m_renameCaretPos = 0;
        m_renameSelStart = m_renameSelEnd = 0;
    } else if (wParam == VK_END) {
        m_renameCaretPos = m_renameText.length();
        m_renameSelStart = m_renameSelEnd = m_renameCaretPos;
    } else if (wParam == VK_ESCAPE) {
        CancelInlineRename();
    }
}

/**
 * @brief Handles mouse movement over Code Search buttons to update floating tooltips.
 * @param pt Cursor point in client coordinates.
 * @return True if tooltip state changed and requires repaint.
 */
bool DrawerPanel::HandleSearchMouseMove(POINT pt) {
    std::wstring oldTip = m_searchTooltipText;
    m_searchTooltipText.clear();

    auto checkBtn = [&](const D2D1_RECT_F& rc, const wchar_t* text) {
        if (pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom) {
            m_searchTooltipText = text;
            m_searchTooltipAnchor = rc;
            return true;
        }
        return false;
    };

    if (checkBtn(m_rcRefreshSearchBtn, L"Refresh")) {}
    else if (checkBtn(m_rcToggleReplaceChevron, L"Toggle Replace")) {}
    else if (checkBtn(m_rcMatchCaseBtn, L"Match Case (Alt+C)")) {}
    else if (checkBtn(m_rcMatchWholeWordBtn, L"Match Whole Word (Alt+W)")) {}
    else if (checkBtn(m_rcUseRegexBtn, L"Use Regular Expression (Alt+R)")) {}
    else if (m_isReplaceOpen && checkBtn(m_rcPreserveCaseBtn, L"Preserve Case (Alt+P)")) {}
    else if (m_isReplaceOpen && checkBtn(m_rcReplaceAllBtn, L"Replace All (Ctrl+Alt+Enter)")) {}
    else if (checkBtn(m_rcToggleSearchDetailsBtn, L"Toggle Search Details")) {}
    else if (m_isSearchDetailsOpen && checkBtn(m_rcOpenEditorsOnlyBtn, L"Search Open Tabs Only (Alt+L)")) {}
    else if (m_isSearchDetailsOpen && checkBtn(m_rcUseExcludeSettingsBtn, L"Use Exclude Settings and Ignore Files")) {}

    return (m_searchTooltipText != oldTip);
}

// =========================================================================
// --- Source Control (Git) Implementation (Images 2, 3, 4, 5) ---
// =========================================================================

namespace {

static std::wstring GitUtf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return L"";
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &result[0], sizeNeeded);
    return result;
}

struct GitCmdResult {
    int exitCode = -1;
    std::wstring output;
};

static GitCmdResult RunGitCmd(const std::wstring& cmd, const std::wstring& workDir) {
    GitCmdResult res;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return res;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    PROCESS_INFORMATION pi{ 0 };
    std::wstring fullCmd = L"git " + cmd;
    std::vector<wchar_t> cmdBuf(fullCmd.begin(), fullCmd.end());
    cmdBuf.push_back(L'\0');

    LPCWSTR pDir = workDir.empty() ? nullptr : workDir.c_str();

    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, pDir, &si, &pi);
    CloseHandle(hWritePipe);

    if (!ok) {
        CloseHandle(hReadPipe);
        return res;
    }

    std::string rawOut;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        rawOut.append(buffer, bytesRead);
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, 6000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    res.exitCode = static_cast<int>(exitCode);
    res.output = GitUtf8ToWide(rawOut);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return res;
}

} // anonymous namespace

void DrawerPanel::OpenSourceControl() {
    RefreshGitState(true);
}

void DrawerPanel::RefreshGitState(bool async) {
    auto task = [this]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;

        // 1. Check if inside git repository
        auto resCheck = RunGitCmd(L"rev-parse --is-inside-work-tree", dir);
        if (resCheck.exitCode != 0) {
            std::lock_guard<std::mutex> lock(m_gitMutex);
            m_isGitRepo = false;
            m_gitChanges.clear();
            m_gitCommits.clear();
            m_isGitLoading = false;
            return;
        }

        // 2. Query active branch
        auto resBranch = RunGitCmd(L"branch --show-current", dir);
        std::wstring branch = L"master";
        if (resBranch.exitCode == 0 && !resBranch.output.empty()) {
            branch = resBranch.output;
            while (!branch.empty() && (branch.back() == L'\r' || branch.back() == L'\n' || branch.back() == L' ')) {
                branch.pop_back();
            }
        }

        // 3. Query working tree changes
        auto resStatus = RunGitCmd(L"status --porcelain=v1", dir);
        std::vector<GitChangeItem> changes;
        if (resStatus.exitCode == 0) {
            std::wstringstream ss(resStatus.output);
            std::wstring line;
            while (std::getline(ss, line)) {
                if (line.size() < 3) continue;
                std::wstring statusStr = line.substr(0, 2);
                size_t first = statusStr.find_first_not_of(L" ");
                if (first != std::wstring::npos) statusStr = statusStr.substr(first);
                size_t last = statusStr.find_last_not_of(L" ");
                if (last != std::wstring::npos) statusStr = statusStr.substr(0, last + 1);

                std::wstring filePath = line.substr(3);
                if (filePath.size() >= 2 && filePath.front() == L'"' && filePath.back() == L'"') {
                    filePath = filePath.substr(1, filePath.size() - 2);
                }
                for (auto& c : filePath) if (c == L'/') c = L'\\';

                std::filesystem::path p(filePath);
                GitChangeItem item;
                item.path = filePath;
                item.fileName = p.filename().wstring();
                item.relDir = p.parent_path().wstring();
                item.status = statusStr.empty() ? L"M" : statusStr;
                changes.push_back(item);
            }
        }

        // 4. Query commit history graph
        auto resLog = RunGitCmd(L"log -n 40 --pretty=format:\"%h|%p|%s|%an|%ar|%d\"", dir);
        std::vector<GitCommitItem> commits;
        if (resLog.exitCode == 0) {
            std::wstringstream ss(resLog.output);
            std::wstring line;
            bool firstCommit = true;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                std::vector<std::wstring> tokens;
                size_t start = 0;
                while (start < line.size()) {
                    size_t pipe = line.find(L'|', start);
                    if (pipe == std::wstring::npos) {
                        tokens.push_back(line.substr(start));
                        break;
                    }
                    tokens.push_back(line.substr(start, pipe - start));
                    start = pipe + 1;
                }

                if (tokens.size() >= 3) {
                    GitCommitItem ci;
                    ci.hash = tokens[0];
                    ci.parentHash = tokens.size() > 1 ? tokens[1] : L"";
                    ci.message = tokens.size() > 2 ? tokens[2] : L"";
                    ci.author = tokens.size() > 3 ? tokens[3] : L"";
                    ci.relativeTime = tokens.size() > 4 ? tokens[4] : L"";
                    ci.refNames = tokens.size() > 5 ? tokens[5] : L"";
                    ci.isHead = firstCommit || (ci.refNames.find(L"HEAD") != std::wstring::npos);
                    firstCommit = false;
                    commits.push_back(ci);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_gitMutex);
            m_isGitRepo = true;
            m_gitBranch = branch;
            m_gitChanges = std::move(changes);
            m_gitCommits = std::move(commits);
            m_isGitLoading = false;
        }
    };

    if (async) {
        std::thread(task).detach();
    } else {
        task();
    }
}

void DrawerPanel::StageAllChangesAsync() {
    std::thread([this]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        RunGitCmd(L"add -A", dir);
        RefreshGitState(false);
    }).detach();
}

void DrawerPanel::DiscardAllChangesAsync() {
    std::thread([this]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        RunGitCmd(L"restore .", dir);
        RunGitCmd(L"clean -fd", dir);
        RefreshGitState(false);
    }).detach();
}

void DrawerPanel::StageFileAsync(const std::wstring& relPath) {
    std::thread([this, relPath]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        RunGitCmd(L"add \"" + relPath + L"\"", dir);
        RefreshGitState(false);
    }).detach();
}

void DrawerPanel::DiscardFileAsync(const std::wstring& relPath) {
    std::thread([this, relPath]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        RunGitCmd(L"restore \"" + relPath + L"\"", dir);
        RunGitCmd(L"clean -f \"" + relPath + L"\"", dir);
        RefreshGitState(false);
    }).detach();
}

void DrawerPanel::CommitChangesAsync(const std::wstring& message) {
    std::wstring msg = message;
    if (msg.empty()) {
        GenerateAiCommitMessage();
        msg = m_commitMessage;
    }
    if (msg.empty()) msg = L"chore: update workspace";

    std::thread([this, msg]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        RunGitCmd(L"add -A", dir);
        RunGitCmd(L"commit -m \"" + msg + L"\"", dir);
        {
            std::lock_guard<std::mutex> lock(m_gitMutex);
            m_commitMessage.clear();
        }
        RefreshGitState(false);
    }).detach();
}

void DrawerPanel::PushCommitsAsync() {
    std::thread([this]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        RunGitCmd(L"push", dir);
        RefreshGitState(false);
    }).detach();
}

void DrawerPanel::GenerateAiCommitMessage() {
    std::lock_guard<std::mutex> lock(m_gitMutex);
    if (m_gitChanges.empty()) {
        m_commitMessage = L"chore: update repository files";
        return;
    }

    bool hasUi = false;
    bool hasTests = false;
    bool hasDocs = false;
    std::wstring mainFileName;

    for (const auto& c : m_gitChanges) {
        if (mainFileName.empty()) mainFileName = c.fileName;
        std::wstring p = c.path;
        for (auto& ch : p) ch = towlower(ch);
        if (p.find(L"ui") != std::wstring::npos || p.find(L"theme") != std::wstring::npos || p.find(L"panel") != std::wstring::npos) hasUi = true;
        if (p.find(L"test") != std::wstring::npos) hasTests = true;
        if (p.find(L".md") != std::wstring::npos || p.find(L"doc") != std::wstring::npos) hasDocs = true;
    }

    if (hasUi) {
        m_commitMessage = L"feat(ui): update source control panel and user interface";
    } else if (hasTests) {
        m_commitMessage = L"test: add verification tests for source control";
    } else if (hasDocs) {
        m_commitMessage = L"docs: update documentation and specifications";
    } else if (!mainFileName.empty()) {
        m_commitMessage = L"feat: update " + mainFileName;
    } else {
        m_commitMessage = L"chore: update workspace files";
    }
}

bool DrawerPanel::HitTestGit(POINT pt, const app::AppConfig& config, std::wstring& outOpenFile) {
    (void)config;
    outOpenFile.clear();

    // 1. Top Popup Menus (Header Views, Changes More, Graph More)
    if (m_activeGitMenu != GitMenuType::None) {
        if (pt.x >= m_gitMenuBounds.left && pt.x <= m_gitMenuBounds.right &&
            pt.y >= m_gitMenuBounds.top && pt.y <= m_gitMenuBounds.bottom) {
            for (const auto& item : m_gitMenuItems) {
                if (item.isHeader || item.isSeparator) continue;
                if (pt.x >= item.bounds.left && pt.x <= item.bounds.right &&
                    pt.y >= item.bounds.top && pt.y <= item.bounds.bottom) {
                    switch (item.id) {
                        case 2: // Toggle Changes
                            m_showChangesSection = !m_showChangesSection;
                            break;
                        case 3: // Toggle Graph
                            m_showGraphSection = !m_showGraphSection;
                            break;
                        case 5: // Refresh All
                        case 14: // Refresh Status
                        case 20: // Refresh Graph
                            RefreshGitState(true);
                            break;
                        case 10: // Stage All Changes
                            StageAllChangesAsync();
                            break;
                        case 11: // Discard All Changes
                            DiscardAllChangesAsync();
                            break;
                        case 12: // Stash Changes
                            RunGitCmd(L"git stash", false);
                            RefreshGitState(true);
                            break;
                        case 21: // Push Commits
                            PushCommitsAsync();
                            break;
                        case 22: // Pull Commits
                            RunGitCmd(L"git pull", false);
                            RefreshGitState(true);
                            break;
                        case 23: // Fetch All Remotes
                            RunGitCmd(L"git fetch --all", false);
                            RefreshGitState(true);
                            break;
                        default:
                            break;
                    }
                    m_activeGitMenu = GitMenuType::None;
                    m_showGitViewMenu = false;
                    return true;
                }
            }
        }
        // Outside menu closes menu
        m_activeGitMenu = GitMenuType::None;
        m_showGitViewMenu = false;
        return true;
    }

    // Legacy fallback for tests
    if (m_showGitViewMenu) {
        if (pt.x >= m_gitViewMenuChangesBounds.left && pt.x <= m_gitViewMenuChangesBounds.right &&
            pt.y >= m_gitViewMenuChangesBounds.top && pt.y <= m_gitViewMenuChangesBounds.bottom) {
            m_showChangesSection = !m_showChangesSection;
            m_showGitViewMenu = false;
            return true;
        }
        if (pt.x >= m_gitViewMenuGraphBounds.left && pt.x <= m_gitViewMenuGraphBounds.right &&
            pt.y >= m_gitViewMenuGraphBounds.top && pt.y <= m_gitViewMenuGraphBounds.bottom) {
            m_showGraphSection = !m_showGraphSection;
            m_showGitViewMenu = false;
            return true;
        }
        m_showGitViewMenu = false;
        return true;
    }

    // 2. Header More Button (Title bar Views menu)
    if (pt.x >= m_gitHeaderMoreBounds.left && pt.x <= m_gitHeaderMoreBounds.right &&
        pt.y >= m_gitHeaderMoreBounds.top && pt.y <= m_gitHeaderMoreBounds.bottom) {
        m_activeGitMenu = (m_activeGitMenu == GitMenuType::HeaderViews) ? GitMenuType::None : GitMenuType::HeaderViews;
        m_showGitViewMenu = (m_activeGitMenu == GitMenuType::HeaderViews);
        return true;
    }

    // 3. Changes Section Header & Buttons (Image 5)
    if (m_showChangesSection) {
        if (pt.x >= m_gitStageAllBounds.left && pt.x <= m_gitStageAllBounds.right &&
            pt.y >= m_gitStageAllBounds.top && pt.y <= m_gitStageAllBounds.bottom) {
            StageAllChangesAsync();
            return true;
        }
        if (pt.x >= m_gitDiscardAllBounds.left && pt.x <= m_gitDiscardAllBounds.right &&
            pt.y >= m_gitDiscardAllBounds.top && pt.y <= m_gitDiscardAllBounds.bottom) {
            DiscardAllChangesAsync();
            return true;
        }
        if (pt.x >= m_gitChangesMoreBounds.left && pt.x <= m_gitChangesMoreBounds.right &&
            pt.y >= m_gitChangesMoreBounds.top && pt.y <= m_gitChangesMoreBounds.bottom) {
            m_activeGitMenu = (m_activeGitMenu == GitMenuType::ChangesMore) ? GitMenuType::None : GitMenuType::ChangesMore;
            return true;
        }
        if (pt.x >= m_gitChangesHeaderBounds.left && pt.x <= m_gitChangesHeaderBounds.right &&
            pt.y >= m_gitChangesHeaderBounds.top && pt.y <= m_gitChangesHeaderBounds.bottom) {
            m_changesExpanded = !m_changesExpanded;
            return true;
        }

        if (m_changesExpanded) {
            // AI Generate Commit Message button (compact sparkle icon)
            if (pt.x >= m_gitGenerateAiBounds.left && pt.x <= m_gitGenerateAiBounds.right &&
                pt.y >= m_gitGenerateAiBounds.top && pt.y <= m_gitGenerateAiBounds.bottom) {
                GenerateAiCommitMessage();
                m_focusedGitInput = 1;
                return true;
            }

            // Commit Message input box focus
            if (pt.x >= m_gitMsgBoxBounds.left && pt.x <= m_gitMsgBoxBounds.right &&
                pt.y >= m_gitMsgBoxBounds.top && pt.y <= m_gitMsgBoxBounds.bottom) {
                m_focusedGitInput = 1;
                return true;
            } else {
                m_focusedGitInput = 0;
            }

            // Commit button
            if (pt.x >= m_gitCommitBtnBounds.left && pt.x <= m_gitCommitBtnBounds.right &&
                pt.y >= m_gitCommitBtnBounds.top && pt.y <= m_gitCommitBtnBounds.bottom) {
                CommitChangesAsync(m_commitMessage);
                return true;
            }

            // Changed file rows
            for (const auto& item : m_gitChanges) {
                if (pt.x >= item.bounds.left && pt.x <= item.bounds.right &&
                    pt.y >= item.bounds.top && pt.y <= item.bounds.bottom) {
                    if (pt.x >= item.stageBtnBounds.left && pt.x <= item.stageBtnBounds.right &&
                        pt.y >= item.stageBtnBounds.top && pt.y <= item.stageBtnBounds.bottom) {
                        StageFileAsync(item.path);
                        return true;
                    }
                    if (pt.x >= item.discardBtnBounds.left && pt.x <= item.discardBtnBounds.right &&
                        pt.y >= item.discardBtnBounds.top && pt.y <= item.discardBtnBounds.bottom) {
                        DiscardFileAsync(item.path);
                        return true;
                    }
                    outOpenFile = m_openFolderPath.empty() ? item.path : (m_openFolderPath + L"\\" + item.path);
                    return true;
                }
            }
        }
    }

    // 4. Graph Section Header & Toolbar (Image 2 & 3)
    if (m_showGraphSection) {
        if (pt.x >= m_gitGraphAutoBounds.left && pt.x <= m_gitGraphAutoBounds.right &&
            pt.y >= m_gitGraphAutoBounds.top && pt.y <= m_gitGraphAutoBounds.bottom) {
            m_gitAutoRefresh = !m_gitAutoRefresh;
            return true;
        }
        if (pt.x >= m_gitGraphBullseyeBounds.left && pt.x <= m_gitGraphBullseyeBounds.right &&
            pt.y >= m_gitGraphBullseyeBounds.top && pt.y <= m_gitGraphBullseyeBounds.bottom) {
            m_gitGraphScrollIdx = 0;
            return true;
        }
        if (pt.x >= m_gitGraphSplitBounds.left && pt.x <= m_gitGraphSplitBounds.right &&
            pt.y >= m_gitGraphSplitBounds.top && pt.y <= m_gitGraphSplitBounds.bottom) {
            RefreshGitState(true);
            return true;
        }
        if (pt.x >= m_gitGraphMergeBounds.left && pt.x <= m_gitGraphMergeBounds.right &&
            pt.y >= m_gitGraphMergeBounds.top && pt.y <= m_gitGraphMergeBounds.bottom) {
            RefreshGitState(true);
            return true;
        }
        if (pt.x >= m_gitGraphPushBounds.left && pt.x <= m_gitGraphPushBounds.right &&
            pt.y >= m_gitGraphPushBounds.top && pt.y <= m_gitGraphPushBounds.bottom) {
            PushCommitsAsync();
            return true;
        }
        if (pt.x >= m_gitGraphRefreshBounds.left && pt.x <= m_gitGraphRefreshBounds.right &&
            pt.y >= m_gitGraphRefreshBounds.top && pt.y <= m_gitGraphRefreshBounds.bottom) {
            RefreshGitState(true);
            return true;
        }
        if (pt.x >= m_gitGraphMoreBounds.left && pt.x <= m_gitGraphMoreBounds.right &&
            pt.y >= m_gitGraphMoreBounds.top && pt.y <= m_gitGraphMoreBounds.bottom) {
            m_activeGitMenu = (m_activeGitMenu == GitMenuType::GraphMore) ? GitMenuType::None : GitMenuType::GraphMore;
            return true;
        }
        if (pt.x >= m_gitGraphHeaderBounds.left && pt.x <= m_gitGraphHeaderBounds.right &&
            pt.y >= m_gitGraphHeaderBounds.top && pt.y <= m_gitGraphHeaderBounds.bottom) {
            m_graphExpanded = !m_graphExpanded;
            return true;
        }
    }

    return false;
}

bool DrawerPanel::HandleGitMouseMove(POINT pt) {
    std::wstring oldTip = m_gitTooltipText;
    m_gitTooltipText.clear();
    m_showGitTooltip = false;

    // 0. If a menu is open, handle item hover highlights
    if (m_activeGitMenu != GitMenuType::None) {
        int oldHover = m_hoveredGitMenuItem;
        m_hoveredGitMenuItem = -1;
        if (pt.x >= m_gitMenuBounds.left && pt.x <= m_gitMenuBounds.right &&
            pt.y >= m_gitMenuBounds.top && pt.y <= m_gitMenuBounds.bottom) {
            for (size_t i = 0; i < m_gitMenuItems.size(); ++i) {
                const auto& item = m_gitMenuItems[i];
                if (item.isHeader || item.isSeparator) continue;
                if (pt.x >= item.bounds.left && pt.x <= item.bounds.right &&
                    pt.y >= item.bounds.top && pt.y <= item.bounds.bottom) {
                    m_hoveredGitMenuItem = static_cast<int>(i);
                    break;
                }
            }
        }
        return m_hoveredGitMenuItem != oldHover;
    }

    auto checkBtn = [&](const D2D1_RECT_F& rc, const wchar_t* text) {
        if (pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom) {
            m_gitTooltipText = text;
            m_gitTooltipTarget = rc;
            m_showGitTooltip = true;
            return true;
        }
        return false;
    };

    if (checkBtn(m_gitHeaderMoreBounds, L"Views and More Actions...")) {}
    else if (checkBtn(m_gitStageAllBounds, L"Stage All Changes (✓)")) {}
    else if (checkBtn(m_gitDiscardAllBounds, L"Discard All Changes (🔄)")) {}
    else if (checkBtn(m_gitChangesMoreBounds, L"More Actions...")) {}
    else if (checkBtn(m_gitGenerateAiBounds, L"Generate Commit Message with AI")) {}
    else if (checkBtn(m_gitCommitBtnBounds, L"Commit Changes (Ctrl+Enter)")) {}
    else if (checkBtn(m_gitGraphAutoBounds, L"Toggle Auto Fetch / Refresh")) {}
    else if (checkBtn(m_gitGraphBullseyeBounds, L"Scroll to Current HEAD Commit (◎)")) {}
    else if (checkBtn(m_gitGraphSplitBounds, L"View Branch Divergence (⑂)")) {}
    else if (checkBtn(m_gitGraphMergeBounds, L"View Branch Merges (⑃)")) {}
    else if (checkBtn(m_gitGraphPushBounds, L"Push Commits to Remote (↑)")) {}
    else if (checkBtn(m_gitGraphRefreshBounds, L"Refresh Git History & Status (🔄)")) {}
    else if (checkBtn(m_gitGraphMoreBounds, L"More Graph Actions...")) {}

    // Check changed file rows
    if (m_changesExpanded) {
        for (const auto& item : m_gitChanges) {
            if (checkBtn(item.stageBtnBounds, L"Stage Changes (+)")) break;
            if (checkBtn(item.discardBtnBounds, L"Discard Changes (↩)")) break;
        }
    }

    // Check commit rows
    if (m_graphExpanded && !m_showGitTooltip) {
        for (const auto& ci : m_gitCommits) {
            if (pt.x >= ci.bounds.left && pt.x <= ci.bounds.right &&
                pt.y >= ci.bounds.top && pt.y <= ci.bounds.bottom) {
                m_gitTooltipText = ci.message + L" (" + ci.hash + L")\n" + ci.author + L" • " + ci.relativeTime;
                m_gitTooltipTarget = ci.bounds;
                m_showGitTooltip = true;
                break;
            }
        }
    }

    return (m_gitTooltipText != oldTip);
}

void DrawerPanel::HandleGitChar(wchar_t ch) {
    if (m_focusedGitInput != 1) return;
    if (ch >= 32) {
        m_commitMessage.push_back(ch);
    }
}

void DrawerPanel::HandleGitKeyDown(WPARAM wParam) {
    if (m_focusedGitInput != 1) return;

    if (wParam == VK_BACK) {
        if (!m_commitMessage.empty()) {
            m_commitMessage.pop_back();
        }
    } else if (wParam == VK_RETURN) {
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            CommitChangesAsync(m_commitMessage);
        }
    } else if (wParam == VK_ESCAPE) {
        m_focusedGitInput = 0;
    }
}

void DrawerPanel::RenderSourceControlPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW) {
    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float paddingX = config.ScaleF(16.0f);

    // 1. Pane Title: "SOURCE CONTROL"
    D2D1_RECT_F rcTitle = D2D1::RectF(
        railW + paddingX,
        headerH + config.ScaleF(10.0f),
        railW + currentDrawerW - config.ScaleF(40.0f),
        headerH + config.ScaleF(30.0f)
    );
    const wchar_t* titleStr = L"Source Control";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                  rcTitle, theme.brushTextPrimary.Get());

    // 2. Top-Right "..." Button (Image 2 & 3)
    m_gitHeaderMoreBounds = D2D1::RectF(
        railW + currentDrawerW - config.ScaleF(32.0f),
        headerH + config.ScaleF(10.0f),
        railW + currentDrawerW - config.ScaleF(12.0f),
        headerH + config.ScaleF(30.0f)
    );
    float dotY = (m_gitHeaderMoreBounds.top + m_gitHeaderMoreBounds.bottom) / 2.0f;
    float dotMidX = (m_gitHeaderMoreBounds.left + m_gitHeaderMoreBounds.right) / 2.0f;
    float dotOffset = config.ScaleF(5.0f);
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotMidX - dotOffset, dotY), 1.4f, 1.4f), theme.brushTextMuted.Get());
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotMidX, dotY), 1.4f, 1.4f), theme.brushTextMuted.Get());
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotMidX + dotOffset, dotY), 1.4f, 1.4f), theme.brushTextMuted.Get());

    float curY = headerH + config.ScaleF(38.0f);

    // Non-Git Workspace Fallback
    if (!m_isGitRepo) {
        D2D1_RECT_F rcNoGit = D2D1::RectF(
            railW + paddingX,
            curY + config.ScaleF(20.0f),
            railW + currentDrawerW - paddingX,
            curY + config.ScaleF(100.0f)
        );
        const wchar_t* noGitMsg = L"No Git repository detected in the current workspace.\n\nOpen a workspace with a Git repository to view Changes and Graph.";
        pRT->DrawText(noGitMsg, static_cast<UINT32>(wcslen(noGitMsg)),
                      theme.fontMenuShortcut.Get(), rcNoGit, theme.brushTextMuted.Get());
        return;
    }

    // =========================================================================
    // 3. Section 1: "Changes" (collapsible)
    // =========================================================================
    if (m_showChangesSection) {
        float secH = config.ScaleF(24.0f);
        m_gitChangesHeaderBounds = D2D1::RectF(railW, curY, railW + currentDrawerW, curY + secH);

        // Chevron (v or >)
        float chevX = railW + config.ScaleF(14.0f);
        float chevY = curY + secH / 2.0f;
        m_gitChangesChevronBounds = D2D1::RectF(chevX - 6.0f, chevY - 6.0f, chevX + 6.0f, chevY + 6.0f);

        if (m_changesExpanded) {
            // Downward triangle
            pRT->DrawLine(D2D1::Point2F(chevX - 3.5f, chevY - 2.0f), D2D1::Point2F(chevX, chevY + 2.0f), theme.brushTextPrimary.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(chevX, chevY + 2.0f), D2D1::Point2F(chevX + 3.5f, chevY - 2.0f), theme.brushTextPrimary.Get(), 1.4f);
        } else {
            // Rightward triangle
            pRT->DrawLine(D2D1::Point2F(chevX - 2.0f, chevY - 3.5f), D2D1::Point2F(chevX + 2.0f, chevY), theme.brushTextPrimary.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(chevX + 2.0f, chevY), D2D1::Point2F(chevX - 2.0f, chevY + 3.5f), theme.brushTextPrimary.Get(), 1.4f);
        }

        // Title: Changes (N)
        std::wstring changesLabel = L"Changes";
        if (!m_gitChanges.empty()) {
            changesLabel += L" " + std::to_wstring(m_gitChanges.size());
        }
        D2D1_RECT_F rcChangesText = D2D1::RectF(chevX + config.ScaleF(12.0f), curY, railW + currentDrawerW - config.ScaleF(80.0f), curY + secH);
        pRT->DrawText(changesLabel.c_str(), static_cast<UINT32>(changesLabel.length()),
                      theme.fontMenuShortcut.Get(), rcChangesText, theme.brushTextPrimary.Get());

        // Header Action Buttons on right (Image 5): ✓ (Stage All), 🔄 (Discard All), ... (More)
        float btnW = config.ScaleF(18.0f);
        float btnGap = config.ScaleF(4.0f);
        float rightX = railW + currentDrawerW - config.ScaleF(12.0f);

        m_gitChangesMoreBounds = D2D1::RectF(rightX - btnW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (btnW + btnGap);

        m_gitDiscardAllBounds = D2D1::RectF(rightX - btnW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (btnW + btnGap);

        m_gitStageAllBounds = D2D1::RectF(rightX - btnW, curY + 2.0f, rightX, curY + secH - 2.0f);

        // Draw ✓ icon on m_gitStageAllBounds
        float ckX = (m_gitStageAllBounds.left + m_gitStageAllBounds.right) / 2.0f;
        float ckY = (m_gitStageAllBounds.top + m_gitStageAllBounds.bottom) / 2.0f;
        pRT->DrawLine(D2D1::Point2F(ckX - 3.5f, ckY), D2D1::Point2F(ckX - 1.0f, ckY + 3.0f), theme.brushTextMuted.Get(), 1.4f);
        pRT->DrawLine(D2D1::Point2F(ckX - 1.0f, ckY + 3.0f), D2D1::Point2F(ckX + 4.0f, ckY - 3.0f), theme.brushTextMuted.Get(), 1.4f);

        // Draw 🔄 circular arrow on m_gitDiscardAllBounds
        float rcX = (m_gitDiscardAllBounds.left + m_gitDiscardAllBounds.right) / 2.0f;
        float rcY = (m_gitDiscardAllBounds.top + m_gitDiscardAllBounds.bottom) / 2.0f;
        D2D1_ELLIPSE elDisc = D2D1::Ellipse(D2D1::Point2F(rcX, rcY), 4.2f, 4.2f);
        pRT->DrawEllipse(elDisc, theme.brushTextMuted.Get(), 1.3f);
        pRT->DrawLine(D2D1::Point2F(rcX + 4.0f, rcY - 2.5f), D2D1::Point2F(rcX + 4.0f, rcY), theme.brushTextMuted.Get(), 1.3f);

        // Draw ... on m_gitChangesMoreBounds
        float mx = (m_gitChangesMoreBounds.left + m_gitChangesMoreBounds.right) / 2.0f;
        float my = (m_gitChangesMoreBounds.top + m_gitChangesMoreBounds.bottom) / 2.0f;
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(mx - 3.5f, my), 1.2f, 1.2f), theme.brushTextMuted.Get());
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(mx, my), 1.2f, 1.2f), theme.brushTextMuted.Get());
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(mx + 3.5f, my), 1.2f, 1.2f), theme.brushTextMuted.Get());

        curY += secH + config.ScaleF(6.0f);

        if (m_changesExpanded) {
            // Commit Message Input Box
            float msgH = config.ScaleF(28.0f);
            m_gitMsgBoxBounds = D2D1::RectF(
                railW + paddingX,
                curY,
                railW + currentDrawerW - paddingX,
                curY + msgH
            );

            D2D1_ROUNDED_RECT rrcMsg = D2D1::RoundedRect(m_gitMsgBoxBounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrcMsg, theme.brushCanvasBg.Get());
            pRT->DrawRoundedRectangle(rrcMsg,
                m_focusedGitInput == 1 ? theme.brushBorderHover.Get() : theme.brushBorderSubtle.Get(),
                m_focusedGitInput == 1 ? 1.4f : 1.0f);

            // AI Generate Button inside message box on right
            float genW = config.ScaleF(78.0f);
            m_gitGenerateAiBounds = D2D1::RectF(
                m_gitMsgBoxBounds.right - genW - config.ScaleF(3.0f),
                m_gitMsgBoxBounds.top + config.ScaleF(2.5f),
                m_gitMsgBoxBounds.right - config.ScaleF(3.0f),
                m_gitMsgBoxBounds.bottom - config.ScaleF(2.5f)
            );
            D2D1_ROUNDED_RECT rrcGen = D2D1::RoundedRect(m_gitGenerateAiBounds, 3.0f, 3.0f);
            pRT->FillRoundedRectangle(rrcGen, theme.brushItemActive.Get());
            const wchar_t* genText = L"Generate 🪄";
            pRT->DrawText(genText, static_cast<UINT32>(wcslen(genText)),
                          theme.fontMenuShortcut.Get(), m_gitGenerateAiBounds, theme.brushBrandAccent.Get());

            // Message text or placeholder
            D2D1_RECT_F rcMsgText = D2D1::RectF(
                m_gitMsgBoxBounds.left + config.ScaleF(6.0f),
                m_gitMsgBoxBounds.top,
                m_gitGenerateAiBounds.left - config.ScaleF(4.0f),
                m_gitMsgBoxBounds.bottom
            );
            if (m_commitMessage.empty()) {
                const wchar_t* phStr = L"Message (Ctrl+Enter to co...";
                pRT->DrawText(phStr, static_cast<UINT32>(wcslen(phStr)),
                              theme.fontMenuShortcut.Get(), rcMsgText, theme.brushTextMuted.Get());
            } else {
                pRT->DrawText(m_commitMessage.c_str(), static_cast<UINT32>(m_commitMessage.length()),
                              theme.fontMenuShortcut.Get(), rcMsgText, theme.brushTextPrimary.Get());
            }

            curY += msgH + config.ScaleF(6.0f);

            // Primary Commit Button
            float commitH = config.ScaleF(28.0f);
            m_gitCommitBtnBounds = D2D1::RectF(
                railW + paddingX,
                curY,
                railW + currentDrawerW - paddingX,
                curY + commitH
            );
            D2D1_ROUNDED_RECT rrcCommit = D2D1::RoundedRect(m_gitCommitBtnBounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrcCommit, theme.brushBrandAccent.Get());

            // Checkmark and "Commit" text
            D2D1_RECT_F rcCommitLabel = D2D1::RectF(
                m_gitCommitBtnBounds.left,
                curY,
                m_gitCommitBtnBounds.right - config.ScaleF(26.0f),
                curY + commitH
            );
            const wchar_t* commitText = L"✓ Commit";
            pRT->DrawText(commitText, static_cast<UINT32>(wcslen(commitText)),
                          theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                          rcCommitLabel, theme.brushTextWhite.Get());

            // Vertical divider and chevron dropdown
            float divX = m_gitCommitBtnBounds.right - config.ScaleF(26.0f);
            pRT->DrawLine(D2D1::Point2F(divX, curY + 4.0f), D2D1::Point2F(divX, curY + commitH - 4.0f),
                          theme.brushBorderSubtle.Get(), 1.0f);
            float dchX = (divX + m_gitCommitBtnBounds.right) / 2.0f;
            float dchY = curY + commitH / 2.0f;
            pRT->DrawLine(D2D1::Point2F(dchX - 3.0f, dchY - 1.5f), D2D1::Point2F(dchX, dchY + 2.0f), theme.brushTextWhite.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(dchX, dchY + 2.0f), D2D1::Point2F(dchX + 3.0f, dchY - 1.5f), theme.brushTextWhite.Get(), 1.4f);

            curY += commitH + config.ScaleF(10.0f);

            // Changed Files Rows
            if (m_gitChanges.empty()) {
                D2D1_RECT_F rcNoCh = D2D1::RectF(railW + paddingX, curY, railW + currentDrawerW - paddingX, curY + 20.0f);
                const wchar_t* noChStr = L"No changes detected";
                pRT->DrawText(noChStr, static_cast<UINT32>(wcslen(noChStr)),
                              theme.fontMenuShortcut.Get(), rcNoCh, theme.brushTextMuted.Get());
                curY += config.ScaleF(24.0f);
            } else {
                float rowH = config.ScaleF(24.0f);
                size_t displayCount = (std::min)(m_gitChanges.size(), static_cast<size_t>(6));
                for (size_t i = 0; i < displayCount; ++i) {
                    auto& item = m_gitChanges[i];
                    item.bounds = D2D1::RectF(railW, curY, railW + currentDrawerW, curY + rowH);

                    // File icon glyph / extension
                    D2D1_RECT_F rcIcon = D2D1::RectF(railW + config.ScaleF(16.0f), curY + 3.0f, railW + config.ScaleF(32.0f), curY + rowH - 3.0f);
                    pRT->DrawText(L"📄", 2, theme.fontMenuShortcut.Get(), rcIcon, theme.brushTextMuted.Get());

                    // File name and relative directory
                    D2D1_RECT_F rcName = D2D1::RectF(
                        railW + config.ScaleF(34.0f),
                        curY,
                        railW + currentDrawerW - config.ScaleF(48.0f),
                        curY + rowH
                    );
                    pRT->DrawText(item.fileName.c_str(), static_cast<UINT32>(item.fileName.length()),
                                  theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                                  rcName, theme.brushTextPrimary.Get());

                    // Status letter (M = yellow, U/A = green, D = red)
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushStatus;
                    if (item.status.find(L"M") != std::wstring::npos) {
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.898f, 0.753f, 0.482f, 1.0f), &brushStatus); // #E5C07B
                    } else if (item.status.find(L"D") != std::wstring::npos) {
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushStatus); // #E06C75
                    } else {
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), &brushStatus); // #98C379
                    }

                    D2D1_RECT_F rcStatus = D2D1::RectF(
                        railW + currentDrawerW - config.ScaleF(28.0f),
                        curY,
                        railW + currentDrawerW - config.ScaleF(12.0f),
                        curY + rowH
                    );
                    const wchar_t* statChar = item.status.empty() ? L"M" : item.status.c_str();
                    pRT->DrawText(statChar, 1, theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                                  rcStatus, brushStatus.Get());

                    // Stage (+) and Discard (↩) buttons on row
                    item.stageBtnBounds = D2D1::RectF(railW + currentDrawerW - config.ScaleF(46.0f), curY + 3.0f, railW + currentDrawerW - config.ScaleF(32.0f), curY + rowH - 3.0f);
                    item.discardBtnBounds = D2D1::RectF(railW + currentDrawerW - config.ScaleF(30.0f), curY + 3.0f, railW + currentDrawerW - config.ScaleF(16.0f), curY + rowH - 3.0f);

                    curY += rowH;
                }
            }
        }
    }

    // =========================================================================
    // 4. Section 2: "Graph" (Images 2 & 3)
    // =========================================================================
    if (m_showGraphSection) {
        float secH = config.ScaleF(24.0f);
        m_gitGraphHeaderBounds = D2D1::RectF(railW, curY, railW + currentDrawerW, curY + secH);

        // Chevron (v or >)
        float chevX = railW + config.ScaleF(14.0f);
        float chevY = curY + secH / 2.0f;
        m_gitGraphChevronBounds = D2D1::RectF(chevX - 6.0f, chevY - 6.0f, chevX + 6.0f, chevY + 6.0f);

        if (m_graphExpanded) {
            pRT->DrawLine(D2D1::Point2F(chevX - 3.5f, chevY - 2.0f), D2D1::Point2F(chevX, chevY + 2.0f), theme.brushTextPrimary.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(chevX, chevY + 2.0f), D2D1::Point2F(chevX + 3.5f, chevY - 2.0f), theme.brushTextPrimary.Get(), 1.4f);
        } else {
            pRT->DrawLine(D2D1::Point2F(chevX - 2.0f, chevY - 3.5f), D2D1::Point2F(chevX + 2.0f, chevY), theme.brushTextPrimary.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(chevX + 2.0f, chevY), D2D1::Point2F(chevX - 2.0f, chevY + 3.5f), theme.brushTextPrimary.Get(), 1.4f);
        }

        // Title: Graph
        D2D1_RECT_F rcGraphTitle = D2D1::RectF(chevX + config.ScaleF(12.0f), curY, railW + config.ScaleF(80.0f), curY + secH);
        const wchar_t* graphTitleStr = L"Graph";
        pRT->DrawText(graphTitleStr, static_cast<UINT32>(wcslen(graphTitleStr)),
                      theme.fontMenuShortcut.Get(), rcGraphTitle, theme.brushTextPrimary.Get());

        // Header Action Toolbar on right (Images 2 & 3): Auto, ◎, ⑂, ⑃, ↑, 🔄, ...
        float tbW = config.ScaleF(18.0f);
        float tbGap = config.ScaleF(3.0f);
        float rightX = railW + currentDrawerW - config.ScaleF(10.0f);

        // ... More
        m_gitGraphMoreBounds = D2D1::RectF(rightX - tbW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (tbW + tbGap);
        float gmx = (m_gitGraphMoreBounds.left + m_gitGraphMoreBounds.right) / 2.0f;
        float gmy = (m_gitGraphMoreBounds.top + m_gitGraphMoreBounds.bottom) / 2.0f;
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(gmx - 3.5f, gmy), 1.2f, 1.2f), theme.brushTextMuted.Get());
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(gmx, gmy), 1.2f, 1.2f), theme.brushTextMuted.Get());
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(gmx + 3.5f, gmy), 1.2f, 1.2f), theme.brushTextMuted.Get());

        // 🔄 Refresh
        m_gitGraphRefreshBounds = D2D1::RectF(rightX - tbW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (tbW + tbGap);
        float grx = (m_gitGraphRefreshBounds.left + m_gitGraphRefreshBounds.right) / 2.0f;
        float gry = (m_gitGraphRefreshBounds.top + m_gitGraphRefreshBounds.bottom) / 2.0f;
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(grx, gry), 4.2f, 4.2f), theme.brushTextMuted.Get(), 1.3f);
        pRT->DrawLine(D2D1::Point2F(grx + 4.0f, gry - 2.5f), D2D1::Point2F(grx + 4.0f, gry), theme.brushTextMuted.Get(), 1.3f);

        // ↑ Push
        m_gitGraphPushBounds = D2D1::RectF(rightX - tbW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (tbW + tbGap);
        float gpx = (m_gitGraphPushBounds.left + m_gitGraphPushBounds.right) / 2.0f;
        float gpy = (m_gitGraphPushBounds.top + m_gitGraphPushBounds.bottom) / 2.0f;
        pRT->DrawLine(D2D1::Point2F(gpx, gpy + 4.0f), D2D1::Point2F(gpx, gpy - 4.0f), theme.brushTextMuted.Get(), 1.3f);
        pRT->DrawLine(D2D1::Point2F(gpx - 3.0f, gpy - 1.0f), D2D1::Point2F(gpx, gpy - 4.0f), theme.brushTextMuted.Get(), 1.3f);
        pRT->DrawLine(D2D1::Point2F(gpx + 3.0f, gpy - 1.0f), D2D1::Point2F(gpx, gpy - 4.0f), theme.brushTextMuted.Get(), 1.3f);
        pRT->DrawLine(D2D1::Point2F(gpx - 4.0f, gpy + 4.5f), D2D1::Point2F(gpx + 4.0f, gpy + 4.5f), theme.brushTextMuted.Get(), 1.3f);

        // ⑃ Merge
        m_gitGraphMergeBounds = D2D1::RectF(rightX - tbW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (tbW + tbGap);
        float gmgx = (m_gitGraphMergeBounds.left + m_gitGraphMergeBounds.right) / 2.0f;
        float gmgy = (m_gitGraphMergeBounds.top + m_gitGraphMergeBounds.bottom) / 2.0f;
        pRT->DrawLine(D2D1::Point2F(gmgx - 3.0f, gmgy - 4.0f), D2D1::Point2F(gmgx, gmgy), theme.brushTextMuted.Get(), 1.2f);
        pRT->DrawLine(D2D1::Point2F(gmgx + 3.0f, gmgy - 4.0f), D2D1::Point2F(gmgx, gmgy), theme.brushTextMuted.Get(), 1.2f);
        pRT->DrawLine(D2D1::Point2F(gmgx, gmgy), D2D1::Point2F(gmgx, gmgy + 4.0f), theme.brushTextMuted.Get(), 1.2f);

        // ⑂ Split
        m_gitGraphSplitBounds = D2D1::RectF(rightX - tbW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (tbW + tbGap);
        float gsx = (m_gitGraphSplitBounds.left + m_gitGraphSplitBounds.right) / 2.0f;
        float gsy = (m_gitGraphSplitBounds.top + m_gitGraphSplitBounds.bottom) / 2.0f;
        pRT->DrawLine(D2D1::Point2F(gsx, gsy + 4.0f), D2D1::Point2F(gsx, gsy), theme.brushTextMuted.Get(), 1.2f);
        pRT->DrawLine(D2D1::Point2F(gsx, gsy), D2D1::Point2F(gsx - 3.0f, gsy - 4.0f), theme.brushTextMuted.Get(), 1.2f);
        pRT->DrawLine(D2D1::Point2F(gsx, gsy), D2D1::Point2F(gsx + 3.0f, gsy - 4.0f), theme.brushTextMuted.Get(), 1.2f);

        // ◎ Bullseye
        m_gitGraphBullseyeBounds = D2D1::RectF(rightX - tbW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (tbW + tbGap);
        float gbx = (m_gitGraphBullseyeBounds.left + m_gitGraphBullseyeBounds.right) / 2.0f;
        float gby = (m_gitGraphBullseyeBounds.top + m_gitGraphBullseyeBounds.bottom) / 2.0f;
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(gbx, gby), 4.5f, 4.5f), theme.brushTextMuted.Get(), 1.2f);
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(gbx, gby), 1.8f, 1.8f), theme.brushTextMuted.Get());

        // Auto pill
        float autoW = config.ScaleF(32.0f);
        m_gitGraphAutoBounds = D2D1::RectF(rightX - autoW, curY + 2.0f, rightX, curY + secH - 2.0f);
        const wchar_t* autoStr = L"Auto";
        pRT->DrawText(autoStr, 4, theme.fontMenuShortcut.Get(), m_gitGraphAutoBounds,
                      m_gitAutoRefresh ? theme.brushBrandAccent.Get() : theme.brushTextMuted.Get());

        curY += secH + config.ScaleF(4.0f);

        if (m_graphExpanded) {
            // Clip to Graph scrollable area
            D2D1_RECT_F rcGraphClip = D2D1::RectF(railW, curY, railW + currentDrawerW, clientHeight);
            pRT->PushAxisAlignedClip(rcGraphClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            float rowH = config.ScaleF(26.0f);
            float timelineX = railW + config.ScaleF(24.0f);

            // Connective graph line brush
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLine;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 0.75f), &brushLine);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHeadNode;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.345f, 0.651f, 1.000f, 1.0f), &brushHeadNode);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushNode;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.149f, 0.545f, 0.824f, 1.0f), &brushNode);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBranchBadge;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.122f, 0.231f, 0.373f, 0.85f), &brushBranchBadge);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCloud;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f), &brushCloud); // purple cloud #BD93F9

            size_t startIdx = (std::min)(m_gitGraphScrollIdx, m_gitCommits.empty() ? size_t(0) : m_gitCommits.size() - 1);
            for (size_t i = startIdx; i < m_gitCommits.size() && curY < clientHeight; ++i) {
                auto& ci = m_gitCommits[i];
                ci.bounds = D2D1::RectF(railW, curY, railW + currentDrawerW, curY + rowH);

                // Vertical timeline connector line
                pRT->DrawLine(
                    D2D1::Point2F(timelineX, curY),
                    D2D1::Point2F(timelineX, curY + rowH),
                    brushLine.Get(), 1.6f
                );

                // Commit circle node
                float nodeY = curY + rowH / 2.0f;
                if (ci.isHead) {
                    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(timelineX, nodeY), 4.5f, 4.5f), brushHeadNode.Get(), 1.6f);
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(timelineX, nodeY), 2.2f, 2.2f), brushHeadNode.Get());
                } else {
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(timelineX, nodeY), 2.8f, 2.8f), brushNode.Get());
                }

                float textLeft = timelineX + config.ScaleF(14.0f);

                // Branch pill badge (e.g. master + cloud) if HEAD or has refNames
                if (ci.isHead || !ci.refNames.empty()) {
                    std::wstring branchBadgeText = L"master";
                    if (!m_gitBranch.empty()) branchBadgeText = m_gitBranch;
                    float bW = config.ScaleF(56.0f);
                    float bH = config.ScaleF(17.0f);
                    D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(
                        D2D1::RectF(textLeft, nodeY - bH / 2.0f, textLeft + bW, nodeY + bH / 2.0f),
                        3.0f, 3.0f
                    );
                    pRT->FillRoundedRectangle(rrcBadge, brushBranchBadge.Get());
                    pRT->DrawRoundedRectangle(rrcBadge, brushHeadNode.Get(), 1.0f);

                    // Badge text
                    pRT->DrawText(branchBadgeText.c_str(), static_cast<UINT32>(branchBadgeText.length()),
                                  theme.fontMenuShortcut.Get(),
                                  D2D1::RectF(textLeft + 3.0f, nodeY - bH / 2.0f, textLeft + bW - 14.0f, nodeY + bH / 2.0f),
                                  brushHeadNode.Get());

                    // Cloud icon on right edge of pill
                    float cloudX = textLeft + bW - 8.0f;
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cloudX - 3.0f, nodeY + 1.0f), 2.2f, 2.2f), brushCloud.Get());
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cloudX, nodeY - 1.0f), 3.0f, 3.0f), brushCloud.Get());
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cloudX + 3.0f, nodeY + 1.0f), 2.2f, 2.2f), brushCloud.Get());

                    textLeft += bW + config.ScaleF(8.0f);
                }

                // Commit message text
                D2D1_RECT_F rcMsg = D2D1::RectF(
                    textLeft,
                    curY,
                    railW + currentDrawerW - config.ScaleF(12.0f),
                    curY + rowH
                );
                pRT->DrawText(ci.message.c_str(), static_cast<UINT32>(ci.message.length()),
                              theme.fontMenuShortcut.Get(), rcMsg, theme.brushTextPrimary.Get());

                curY += rowH;
            }

            pRT->PopAxisAlignedClip();
        }
    }

    // =========================================================================
    // 5. Top View Dropdown Menu (Image 3: Repositories, ✓ Changes, ✓ Graph)
    // =========================================================================
    if (m_showGitViewMenu) {
        float menuW = config.ScaleF(138.0f);
        float menuH = config.ScaleF(78.0f);
        m_gitViewMenuBounds = D2D1::RectF(
            m_gitHeaderMoreBounds.right - menuW,
            m_gitHeaderMoreBounds.bottom + config.ScaleF(2.0f),
            m_gitHeaderMoreBounds.right,
            m_gitHeaderMoreBounds.bottom + config.ScaleF(2.0f) + menuH
        );

        D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_gitViewMenuBounds, 4.0f, 4.0f);
        pRT->FillRoundedRectangle(rrcMenu, theme.brushMenuBg.Get());
        pRT->DrawRoundedRectangle(rrcMenu, theme.brushMenuBorder.Get(), 1.0f);

        float itemH = menuH / 3.0f;
        float itemY = m_gitViewMenuBounds.top;

        // Row 1: "Repositories" (disabled title)
        D2D1_RECT_F rcRep = D2D1::RectF(m_gitViewMenuBounds.left + config.ScaleF(14.0f), itemY, m_gitViewMenuBounds.right, itemY + itemH);
        const wchar_t* repStr = L"Repositories";
        pRT->DrawText(repStr, static_cast<UINT32>(wcslen(repStr)),
                      theme.fontMenuShortcut.Get(), rcRep, theme.brushTextMuted.Get());
        itemY += itemH;

        // Row 2: "✓ Changes"
        m_gitViewMenuChangesBounds = D2D1::RectF(m_gitViewMenuBounds.left, itemY, m_gitViewMenuBounds.right, itemY + itemH);
        std::wstring chText = (m_showChangesSection ? L"✓  Changes" : L"    Changes");
        pRT->DrawText(chText.c_str(), static_cast<UINT32>(chText.length()),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      D2D1::RectF(m_gitViewMenuBounds.left + config.ScaleF(10.0f), itemY, m_gitViewMenuBounds.right, itemY + itemH),
                      theme.brushTextPrimary.Get());
        itemY += itemH;

        // Row 3: "✓ Graph"
        m_gitViewMenuGraphBounds = D2D1::RectF(m_gitViewMenuBounds.left, itemY, m_gitViewMenuBounds.right, itemY + itemH);
        std::wstring grText = (m_showGraphSection ? L"✓  Graph" : L"    Graph");
        pRT->DrawText(grText.c_str(), static_cast<UINT32>(grText.length()),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      D2D1::RectF(m_gitViewMenuBounds.left + config.ScaleF(10.0f), itemY, m_gitViewMenuBounds.right, itemY + itemH),
                      theme.brushTextPrimary.Get());
    }

    // =========================================================================
    // 6. Floating Tooltip Rendering
    // =========================================================================
    if (m_showGitTooltip && !m_gitTooltipText.empty()) {
        float tipPadX = config.ScaleF(8.0f);
        float tipPadY = config.ScaleF(4.0f);
        float tipW = (std::min)(config.ScaleF(260.0f), config.ScaleF(static_cast<float>(m_gitTooltipText.length() * 7 + 16)));
        float tipH = config.ScaleF(m_gitTooltipText.find(L'\n') != std::wstring::npos ? 38.0f : 24.0f);

        float tipX = m_gitTooltipTarget.left;
        if (tipX + tipW > railW + currentDrawerW) tipX = railW + currentDrawerW - tipW - 4.0f;
        float tipY = m_gitTooltipTarget.bottom + config.ScaleF(4.0f);
        if (tipY + tipH > clientHeight) tipY = m_gitTooltipTarget.top - tipH - config.ScaleF(4.0f);

        D2D1_RECT_F rcTipBox = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
        D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTipBox, 3.5f, 3.5f);
        pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg.Get());
        pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder.Get(), 1.0f);

        D2D1_RECT_F rcTipText = D2D1::RectF(tipX + tipPadX, tipY + tipPadY, tipX + tipW - tipPadX, tipY + tipH - tipPadY);
        pRT->DrawText(m_gitTooltipText.c_str(), static_cast<UINT32>(m_gitTooltipText.length()),
                      theme.fontMenuShortcut.Get(), rcTipText, theme.brushTooltipText.Get());
    }
}

} // namespace phide::ui
