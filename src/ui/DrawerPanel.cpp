/**
 * @file DrawerPanel.cpp
 * @brief Implementation of the slide-out sidebar drawer view with real filesystem browsing.
 */

#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/ActivityRail.hpp"
#include "phide/editor/Buffer.hpp"
#include <algorithm>
#include <iostream>
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
    if (m_openFolderPath.size() >= 4 && m_openFolderPath[0] == L'\\' && m_openFolderPath[1] == L'\\' && m_openFolderPath[2] == L'?' && m_openFolderPath[3] == L'\\') {
        m_openFolderPath = m_openFolderPath.substr(4);
    }

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

            bool createRowDrawn = false;
            auto drawCreateRow = [&]() {
                if (!m_isCreating || createRowDrawn || curY + itemH > clientHeight) return;
                createRowDrawn = true;

                float indent = config.ScaleF(static_cast<float>(m_createDepth * 14));
                // Indent guides
                for (int d = 1; d <= m_createDepth; ++d) {
                    float guideX = railW + paddingX + config.ScaleF(static_cast<float>((d - 1) * 14)) + config.ScaleF(2.0f);
                    pRT->DrawLine(D2D1::Point2F(guideX, curY), D2D1::Point2F(guideX, curY + itemH), theme.brushBorderSubtle.Get(), 1.0f);
                }

                float cy = curY + itemH / 2.0f;
                float chevronX = railW + config.ScaleF(14.0f) + indent;

                if (m_createIsFolder) {
                    // Closed right chevron
                    pRT->DrawLine(D2D1::Point2F(chevronX - config.ScaleF(2.0f), cy - config.ScaleF(3.5f)),
                                  D2D1::Point2F(chevronX + config.ScaleF(2.0f), cy), theme.brushTextSecondary.Get(), 1.3f);
                    pRT->DrawLine(D2D1::Point2F(chevronX + config.ScaleF(2.0f), cy),
                                  D2D1::Point2F(chevronX - config.ScaleF(2.0f), cy + config.ScaleF(3.5f)), theme.brushTextSecondary.Get(), 1.3f);
                }

                float iconX = chevronX + config.ScaleF(10.0f);
                D2D1_RECT_F iconBounds = D2D1::RectF(iconX, cy - config.ScaleF(8.0f), iconX + config.ScaleF(16.0f), cy + config.ScaleF(8.0f));
                if (m_createIsFolder) {
                    auto setiInfo = theme.GetSetiIcon(L"folder", true, false);
                    if (theme.fontSeti && setiInfo.pBrush) {
                        pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), iconBounds, setiInfo.pBrush);
                    } else {
                        m_icons.DrawFileIcon(pRT, theme, iconBounds, FileIconType::Folder);
                    }
                } else {
                    auto setiInfo = theme.GetSetiIcon(m_createText.empty() ? L"file.txt" : m_createText, false, false);
                    if (theme.fontSeti && setiInfo.pBrush) {
                        pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), iconBounds, setiInfo.pBrush);
                    } else {
                        auto fType = LucideIcons::GetIconTypeForPath(m_createText.empty() ? L"file.txt" : m_createText, false, false);
                        m_icons.DrawFileIcon(pRT, theme, iconBounds, fType);
                    }
                }

                float labelX = iconX + config.ScaleF(22.0f);
                m_rcCreateBox = D2D1::RectF(
                    labelX - config.ScaleF(4.0f),
                    curY + config.ScaleF(1.0f),
                    railW + fullDrawerW - config.ScaleF(10.0f),
                    curY + itemH - config.ScaleF(1.0f)
                );

                // 1. Draw inline input box matching Image 2 & 3
                D2D1_ROUNDED_RECT rrcBox = D2D1::RoundedRect(m_rcCreateBox, 3.5f, 3.5f);
                pRT->FillRoundedRectangle(rrcBox, theme.brushCanvasBg.Get());
                pRT->DrawRoundedRectangle(rrcBox, theme.brushBrandAccent.Get(), 1.5f);

                D2D1_RECT_F rcInputText = D2D1::RectF(
                    m_rcCreateBox.left + config.ScaleF(6.0f),
                    m_rcCreateBox.top + config.ScaleF(1.0f),
                    m_rcCreateBox.right - config.ScaleF(4.0f),
                    m_rcCreateBox.bottom - config.ScaleF(1.0f)
                );

                pRT->DrawText(m_createText.c_str(), static_cast<UINT32>(m_createText.length()),
                              theme.fontBody.Get(), rcInputText, theme.brushTextPrimary.Get());

                // Blinking caret
                float caretX = rcInputText.left;
                if (theme.dwriteFactory && !m_createText.empty()) {
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                    if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                            m_createText.c_str(), static_cast<UINT32>(m_createText.length()),
                            theme.fontBody.Get(), 1000.0f, itemH, &pLayout))) {
                        FLOAT hx = 0.0f, hy = 0.0f;
                        DWRITE_HIT_TEST_METRICS htm{};
                        pLayout->HitTestTextPosition(static_cast<UINT32>(m_createCaretPos), FALSE, &hx, &hy, &htm);
                        caretX += hx;
                    }
                }
                pRT->DrawLine(D2D1::Point2F(caretX, m_rcCreateBox.top + config.ScaleF(3.0f)),
                              D2D1::Point2F(caretX, m_rcCreateBox.bottom - config.ScaleF(3.0f)),
                              theme.brushTextPrimary.Get(), 1.5f);

                curY += itemH;
            };

            if (m_isCreating && m_createInsertAfterIdx == -1) {
                drawCreateRow();
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
                if (m_isCreating && static_cast<int>(i) == m_createInsertAfterIdx) {
                    drawCreateRow();
                }
            }

            if (m_isCreating && !createRowDrawn) {
                drawCreateRow();
            }

            // Draw floating path pill card below input box (Images 2 & 3)
            if (m_isCreating && m_rcCreateBox.right > m_rcCreateBox.left) {
                std::wstring displayPath = m_createParentDir;
                if (!displayPath.empty() && displayPath.back() != L'\\' && displayPath.back() != L'/') {
                    displayPath += L"\\";
                }

                float pillH = config.ScaleF(20.0f);
                float pillY = m_rcCreateBox.bottom + config.ScaleF(2.0f);
                float maxPillW = (railW + fullDrawerW - config.ScaleF(8.0f)) - m_rcCreateBox.left;
                float idealPillW = static_cast<float>(displayPath.length()) * 6.5f + config.ScaleF(16.0f);
                float pillW = (std::min)(maxPillW, (std::max)(config.ScaleF(140.0f), idealPillW));
                m_rcCreatePill = D2D1::RectF(m_rcCreateBox.left, pillY, m_rcCreateBox.left + pillW, pillY + pillH);

                // Drop shadow
                D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(
                    D2D1::RectF(m_rcCreatePill.left + 1.0f, m_rcCreatePill.top + 2.0f, m_rcCreatePill.right + 1.0f, m_rcCreatePill.bottom + 2.0f),
                    3.5f, 3.5f
                );
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f), &shadowBrush);
                if (shadowBrush) pRT->FillRoundedRectangle(rrcShadow, shadowBrush.Get());

                D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(m_rcCreatePill, 3.5f, 3.5f);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBg;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.14f, 0.16f, 1.0f), &pillBg);
                if (pillBg) pRT->FillRoundedRectangle(rrcPill, pillBg.Get());

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBorder;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.28f, 0.28f, 0.32f, 1.0f), &pillBorder);
                if (pillBorder) pRT->DrawRoundedRectangle(rrcPill, pillBorder.Get(), 1.0f);

                D2D1_RECT_F rcPillText = D2D1::RectF(
                    m_rcCreatePill.left + config.ScaleF(8.0f),
                    m_rcCreatePill.top,
                    m_rcCreatePill.right - config.ScaleF(4.0f),
                    m_rcCreatePill.bottom
                );

                pRT->PushAxisAlignedClip(m_rcCreatePill, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                pRT->DrawText(displayPath.c_str(), static_cast<UINT32>(displayPath.length()),
                              theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                              rcPillText, theme.brushTextSecondary.Get());
                pRT->PopAxisAlignedClip();
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
        if (m_isSearching.load()) {
            summary = L"Searching...";
        } else {
            summary = L"No results found.";
        }
    } else {
        summary = std::to_wstring(m_totalMatches) + L" results in " +
                  std::to_wstring(m_searchGroups.size()) +
                  (m_searchGroups.size() == 1 ? L" file" : L" files");
        if (m_isSearching.load()) {
            summary += L" (searching...)";
        }
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

                bool isGroupHovered = (static_cast<int>(g) == m_hoveredSearchGroupIdx);
                if (isGroupHovered) {
                    pRT->FillRectangle(group.headerBounds, theme.brushItemHover.Get());
                }

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
                float maxTextRight = railW + fullDrawerW - config.ScaleF(50.0f);
                float availW = (std::max)(10.0f, maxTextRight - nameX);
                std::wstring fileLabel = group.fileName;
                if (!group.relDir.empty()) {
                    fileLabel += L"  " + group.relDir;
                }

                if (theme.dwriteFactory) {
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                    if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                            fileLabel.c_str(), static_cast<UINT32>(fileLabel.length()),
                            theme.fontBody.Get(), availW, rowH, &pLayout))) {
                        pLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                        pLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                        Microsoft::WRL::ComPtr<IDWriteInlineObject> pEllipsis;
                        if (SUCCEEDED(theme.dwriteFactory->CreateEllipsisTrimmingSign(theme.fontBody.Get(), &pEllipsis))) {
                            pLayout->SetTrimming(&trimming, pEllipsis.Get());
                        }

                        if (theme.brushTextMuted && !group.relDir.empty() && fileLabel.length() > group.fileName.length() + 2) {
                            DWRITE_TEXT_RANGE dirRange{
                                static_cast<UINT32>(group.fileName.length() + 2),
                                static_cast<UINT32>(group.relDir.length())
                            };
                            pLayout->SetDrawingEffect(theme.brushTextMuted.Get(), dirRange);
                        }

                        D2D1_RECT_F clipRect = D2D1::RectF(nameX, curY, maxTextRight, curY + rowH);
                        pRT->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                        pRT->DrawTextLayout(D2D1::Point2F(nameX, curY), pLayout.Get(), theme.brushTextPrimary.Get());
                        pRT->PopAxisAlignedClip();
                    }
                } else {
                    D2D1_RECT_F clipRect = D2D1::RectF(nameX, curY, maxTextRight, curY + rowH);
                    pRT->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    pRT->DrawText(fileLabel.c_str(), static_cast<UINT32>(fileLabel.length()),
                                  theme.fontBody.Get(), clipRect, theme.brushTextPrimary.Get());
                    pRT->PopAxisAlignedClip();
                }

                if (isGroupHovered) {
                    float btnSize = config.ScaleF(16.0f);
                    float rightX = railW + fullDrawerW - config.ScaleF(8.0f);
                    group.dismissBtnBounds = D2D1::RectF(rightX - btnSize, curY + (rowH - btnSize) * 0.5f, rightX, curY + (rowH + btnSize) * 0.5f);
                    rightX -= (btnSize + config.ScaleF(3.0f));
                    group.replaceAllBtnBounds = D2D1::RectF(rightX - btnSize, curY + (rowH - btnSize) * 0.5f, rightX, curY + (rowH + btnSize) * 0.5f);

                    bool isDismissHovered = (m_searchMousePos.x >= group.dismissBtnBounds.left && m_searchMousePos.x <= group.dismissBtnBounds.right &&
                                             m_searchMousePos.y >= group.dismissBtnBounds.top && m_searchMousePos.y <= group.dismissBtnBounds.bottom);
                    bool isReplaceHovered = (m_searchMousePos.x >= group.replaceAllBtnBounds.left && m_searchMousePos.x <= group.replaceAllBtnBounds.right &&
                                             m_searchMousePos.y >= group.replaceAllBtnBounds.top && m_searchMousePos.y <= group.replaceAllBtnBounds.bottom);

                    if (isDismissHovered) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(group.dismissBtnBounds, 3.0f, 3.0f);
                        pRT->FillRoundedRectangle(&rrc, theme.brushItemActive ? theme.brushItemActive.Get() : theme.brushItemHover.Get());
                    }
                    if (isReplaceHovered) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(group.replaceAllBtnBounds, 3.0f, 3.0f);
                        pRT->FillRoundedRectangle(&rrc, theme.brushItemActive ? theme.brushItemActive.Get() : theme.brushItemHover.Get());
                    }

                    // Draw Dismiss (✕) Button
                    float dmx = (group.dismissBtnBounds.left + group.dismissBtnBounds.right) * 0.5f;
                    float dmy = (group.dismissBtnBounds.top + group.dismissBtnBounds.bottom) * 0.5f;
                    float dr = config.ScaleF(3.5f);
                    pRT->DrawLine(D2D1::Point2F(dmx - dr, dmy - dr), D2D1::Point2F(dmx + dr, dmy + dr),
                                  isDismissHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);
                    pRT->DrawLine(D2D1::Point2F(dmx + dr, dmy - dr), D2D1::Point2F(dmx - dr, dmy + dr),
                                  isDismissHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);

                    // Draw Replace All in File Button (document with arrow)
                    float rpx = (group.replaceAllBtnBounds.left + group.replaceAllBtnBounds.right) * 0.5f;
                    float rpy = (group.replaceAllBtnBounds.top + group.replaceAllBtnBounds.bottom) * 0.5f;
                    float rw = config.ScaleF(4.0f);
                    float rh = config.ScaleF(4.5f);
                    pRT->DrawRectangle(D2D1::RectF(rpx - rw, rpy - rh, rpx + rw, rpy + rh),
                                       isReplaceHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.0f);
                    pRT->DrawLine(D2D1::Point2F(rpx - rw + 1.5f, rpy), D2D1::Point2F(rpx + rw - 1.5f, rpy),
                                  isReplaceHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);
                    pRT->DrawLine(D2D1::Point2F(rpx + rw - 3.0f, rpy - 1.5f), D2D1::Point2F(rpx + rw - 1.5f, rpy),
                                  isReplaceHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);
                    pRT->DrawLine(D2D1::Point2F(rpx + rw - 3.0f, rpy + 1.5f), D2D1::Point2F(rpx + rw - 1.5f, rpy),
                                  isReplaceHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);
                } else {
                    group.replaceAllBtnBounds = D2D1::RectF(0, 0, 0, 0);
                    group.dismissBtnBounds = D2D1::RectF(0, 0, 0, 0);

                    // Badge count pill - perfectly centered horizontally and vertically
                    std::wstring countStr = std::to_wstring(group.matches.size());
                    float badgeH = config.ScaleF(16.0f);
                    float badgeW = config.ScaleF(countStr.length() > 2 ? 30.0f : (countStr.length() > 1 ? 24.0f : 18.0f));
                    float badgeRight = railW + fullDrawerW - config.ScaleF(12.0f);
                    float badgeLeft = badgeRight - badgeW;
                    D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(
                        D2D1::RectF(
                            badgeLeft,
                            curY + (rowH - badgeH) * 0.5f,
                            badgeRight,
                            curY + (rowH + badgeH) * 0.5f
                        ),
                        config.ScaleF(8.0f), config.ScaleF(8.0f)
                    );
                    pRT->FillRoundedRectangle(rrcBadge, theme.brushItemHover.Get());
                    IDWriteTextFormat* pBadgeFmt = theme.fontCounterBadge ? theme.fontCounterBadge.Get() :
                                                   (theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get());
                    pRT->DrawText(countStr.c_str(), static_cast<UINT32>(countStr.length()),
                                  pBadgeFmt, rrcBadge.rect, theme.brushTextSecondary.Get());
                }

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

                        bool isMatchHovered = (static_cast<int>(g) == m_hoveredSearchGroupIdx && static_cast<int>(m) == m_hoveredSearchMatchIdx);
                        if (isMatchHovered) {
                            pRT->FillRectangle(match.bounds, theme.brushItemHover.Get());
                        }

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
                        float maxSnipRight = railW + fullDrawerW - config.ScaleF(50.0f);
                        float snipW = (std::max)(10.0f, maxSnipRight - snipX);
                        D2D1_RECT_F rcSnip = D2D1::RectF(snipX, curY, maxSnipRight, curY + rowH);

                        std::wstring trimmed = match.lineText;
                        size_t firstNonSpace = trimmed.find_first_not_of(L" \t");
                        if (firstNonSpace != std::wstring::npos && firstNonSpace > 0) {
                            trimmed = trimmed.substr(firstNonSpace);
                        }

                        D2D1_RECT_F snipClip = D2D1::RectF(snipX, curY, maxSnipRight, curY + rowH);
                        pRT->PushAxisAlignedClip(snipClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                        if (theme.dwriteFactory) {
                            Microsoft::WRL::ComPtr<IDWriteTextLayout> pSnippetLayout;
                            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                    trimmed.c_str(), static_cast<UINT32>(trimmed.length()),
                                    theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                                    snipW, rowH, &pSnippetLayout))) {
                                pSnippetLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                                pSnippetLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                                DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                                Microsoft::WRL::ComPtr<IDWriteInlineObject> pEllipsis;
                                if (SUCCEEDED(theme.dwriteFactory->CreateEllipsisTrimmingSign(theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(), &pEllipsis))) {
                                    pSnippetLayout->SetTrimming(&trimming, pEllipsis.Get());
                                }

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

                        pRT->PopAxisAlignedClip();

                        if (isMatchHovered) {
                            float btnSize = config.ScaleF(16.0f);
                            float rightX = railW + fullDrawerW - config.ScaleF(8.0f);
                            match.dismissBtnBounds = D2D1::RectF(rightX - btnSize, curY + (rowH - btnSize) * 0.5f, rightX, curY + (rowH + btnSize) * 0.5f);
                            rightX -= (btnSize + config.ScaleF(3.0f));
                            match.replaceBtnBounds = D2D1::RectF(rightX - btnSize, curY + (rowH - btnSize) * 0.5f, rightX, curY + (rowH + btnSize) * 0.5f);

                            bool isDismissHovered = (m_searchMousePos.x >= match.dismissBtnBounds.left && m_searchMousePos.x <= match.dismissBtnBounds.right &&
                                                     m_searchMousePos.y >= match.dismissBtnBounds.top && m_searchMousePos.y <= match.dismissBtnBounds.bottom);
                            bool isReplaceHovered = (m_searchMousePos.x >= match.replaceBtnBounds.left && m_searchMousePos.x <= match.replaceBtnBounds.right &&
                                                     m_searchMousePos.y >= match.replaceBtnBounds.top && m_searchMousePos.y <= match.replaceBtnBounds.bottom);

                            if (isDismissHovered) {
                                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(match.dismissBtnBounds, 3.0f, 3.0f);
                                pRT->FillRoundedRectangle(&rrc, theme.brushItemActive ? theme.brushItemActive.Get() : theme.brushItemHover.Get());
                            }
                            if (isReplaceHovered) {
                                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(match.replaceBtnBounds, 3.0f, 3.0f);
                                pRT->FillRoundedRectangle(&rrc, theme.brushItemActive ? theme.brushItemActive.Get() : theme.brushItemHover.Get());
                            }

                            // Draw Dismiss (✕) Button
                            float dmx = (match.dismissBtnBounds.left + match.dismissBtnBounds.right) * 0.5f;
                            float dmy = (match.dismissBtnBounds.top + match.dismissBtnBounds.bottom) * 0.5f;
                            float dr = config.ScaleF(3.5f);
                            pRT->DrawLine(D2D1::Point2F(dmx - dr, dmy - dr), D2D1::Point2F(dmx + dr, dmy + dr),
                                          isDismissHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);
                            pRT->DrawLine(D2D1::Point2F(dmx + dr, dmy - dr), D2D1::Point2F(dmx - dr, dmy + dr),
                                          isDismissHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);

                            // Draw Replace Occurrence Button (pen/replace icon)
                            float rpx = (match.replaceBtnBounds.left + match.replaceBtnBounds.right) * 0.5f;
                            float rpy = (match.replaceBtnBounds.top + match.replaceBtnBounds.bottom) * 0.5f;
                            pRT->DrawLine(D2D1::Point2F(rpx - 3.5f, rpy + 3.5f), D2D1::Point2F(rpx + 2.5f, rpy - 2.5f),
                                          isReplaceHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);
                            pRT->DrawLine(D2D1::Point2F(rpx + 0.5f, rpy - 4.0f), D2D1::Point2F(rpx + 4.0f, rpy - 0.5f),
                                          isReplaceHovered ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(), 1.2f);
                        } else {
                            match.replaceBtnBounds = D2D1::RectF(0, 0, 0, 0);
                            match.dismissBtnBounds = D2D1::RectF(0, 0, 0, 0);
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

bool DrawerPanel::HitTestSearch(POINT pt, const app::AppConfig& config, std::wstring& outJumpFile, size_t& outJumpLine, size_t& outMatchCol, size_t& outMatchLen) {
    (void)config;
    outJumpFile.clear();
    outJumpLine = 0;
    outMatchCol = 0;
    outMatchLen = 0;

    // 1. Refresh button
    if (pt.x >= m_rcRefreshSearchBtn.left && pt.x <= m_rcRefreshSearchBtn.right &&
        pt.y >= m_rcRefreshSearchBtn.top && pt.y <= m_rcRefreshSearchBtn.bottom) {
        LaunchAsyncSearch();
        return true;
    }

    // 2. Match Case (Aa) button
    if (pt.x >= m_rcMatchCaseBtn.left && pt.x <= m_rcMatchCaseBtn.right &&
        pt.y >= m_rcMatchCaseBtn.top && pt.y <= m_rcMatchCaseBtn.bottom) {
        m_matchCase = !m_matchCase;
        LaunchAsyncSearch();
        return true;
    }

    // 4. Match Whole Word (ab) button
    if (pt.x >= m_rcMatchWholeWordBtn.left && pt.x <= m_rcMatchWholeWordBtn.right &&
        pt.y >= m_rcMatchWholeWordBtn.top && pt.y <= m_rcMatchWholeWordBtn.bottom) {
        m_matchWholeWord = !m_matchWholeWord;
        LaunchAsyncSearch();
        return true;
    }

    // 5. Use Regex (.*) button
    if (pt.x >= m_rcUseRegexBtn.left && pt.x <= m_rcUseRegexBtn.right &&
        pt.y >= m_rcUseRegexBtn.top && pt.y <= m_rcUseRegexBtn.bottom) {
        m_useRegex = !m_useRegex;
        LaunchAsyncSearch();
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
            LaunchAsyncSearch();
            return true;
        }

        // Gear / Use Exclude Settings icon button (Images 3 & 4)
        if (pt.x >= m_rcUseExcludeSettingsBtn.left && pt.x <= m_rcUseExcludeSettingsBtn.right &&
            pt.y >= m_rcUseExcludeSettingsBtn.top && pt.y <= m_rcUseExcludeSettingsBtn.bottom) {
            m_useExcludeSettings = !m_useExcludeSettings;
            LaunchAsyncSearch();
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

    // 11. Search Results: File Header buttons & Match buttons
    for (size_t g = 0; g < m_searchGroups.size(); ++g) {
        auto& group = m_searchGroups[g];

        // Replace all in file
        if (group.replaceAllBtnBounds.right > group.replaceAllBtnBounds.left &&
            pt.x >= group.replaceAllBtnBounds.left && pt.x <= group.replaceAllBtnBounds.right &&
            pt.y >= group.replaceAllBtnBounds.top && pt.y <= group.replaceAllBtnBounds.bottom) {
            ExecuteReplaceInFile(group.filePath);
            return true;
        }

        // Dismiss file
        if (group.dismissBtnBounds.right > group.dismissBtnBounds.left &&
            pt.x >= group.dismissBtnBounds.left && pt.x <= group.dismissBtnBounds.right &&
            pt.y >= group.dismissBtnBounds.top && pt.y <= group.dismissBtnBounds.bottom) {
            DismissSearchGroup(g);
            return true;
        }

        // Click on file header to expand/collapse
        if (group.headerBounds.right > group.headerBounds.left &&
            pt.x >= group.headerBounds.left && pt.x <= group.headerBounds.right &&
            pt.y >= group.headerBounds.top && pt.y <= group.headerBounds.bottom) {
            group.isExpanded = !group.isExpanded;
            return true;
        }

        if (group.isExpanded) {
            for (size_t m = 0; m < group.matches.size(); ++m) {
                auto& match = group.matches[m];

                // Replace single match
                if (match.replaceBtnBounds.right > match.replaceBtnBounds.left &&
                    pt.x >= match.replaceBtnBounds.left && pt.x <= match.replaceBtnBounds.right &&
                    pt.y >= match.replaceBtnBounds.top && pt.y <= match.replaceBtnBounds.bottom) {
                    ExecuteReplaceSingleMatch(group.filePath, match);
                    return true;
                }

                // Dismiss single match
                if (match.dismissBtnBounds.right > match.dismissBtnBounds.left &&
                    pt.x >= match.dismissBtnBounds.left && pt.x <= match.dismissBtnBounds.right &&
                    pt.y >= match.dismissBtnBounds.top && pt.y <= match.dismissBtnBounds.bottom) {
                    DismissSearchMatch(g, m);
                    return true;
                }

                // Click on match row to jump to code
                if (match.bounds.right > match.bounds.left &&
                    pt.x >= match.bounds.left && pt.x <= match.bounds.right &&
                    pt.y >= match.bounds.top && pt.y <= match.bounds.bottom) {
                    outJumpFile = group.filePath;
                    outJumpLine = match.lineIndex;
                    outMatchCol = match.matchCol;
                    outMatchLen = match.matchLen;
                    m_focusedSearchInput = 0;
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
    } else if (wParam == 'L' || wParam == 'l') {
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            m_searchOnlyOpenEditors = !m_searchOnlyOpenEditors;
            LaunchAsyncSearch();
        }
    } else if (wParam == 'C' || wParam == 'c') {
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            m_matchCase = !m_matchCase;
            LaunchAsyncSearch();
        }
    } else if (wParam == 'W' || wParam == 'w') {
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            m_matchWholeWord = !m_matchWholeWord;
            LaunchAsyncSearch();
        }
    } else if (wParam == 'R' || wParam == 'r') {
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            m_useRegex = !m_useRegex;
            LaunchAsyncSearch();
        }
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

    if (m_onRefreshOpenDocs) {
        m_onRefreshOpenDocs();
    }

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
                    if (!std::filesystem::exists(fp, fec) && !rootFolder.empty()) {
                        std::filesystem::path cand = std::filesystem::path(rootFolder) / fp;
                        if (std::filesystem::exists(cand, fec)) fp = cand;
                    }
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
                        bool excludeDir = false;
                        if (useExcludeSettings) {
                            std::wstring lowerDir = filename;
                            for (auto& c : lowerDir) c = towlower(c);
                            if (lowerDir == L"node_modules" || lowerDir == L"build" || lowerDir == L"bin" ||
                                lowerDir == L"obj" || lowerDir == L"target" || lowerDir == L".git" ||
                                lowerDir == L"dist" || lowerDir == L"out" || lowerDir == L"vendor" ||
                                lowerDir == L".cache" || lowerDir == L"tmp" || lowerDir == L"temp" ||
                                lowerDir == L"coverage" || lowerDir == L".next" || lowerDir == L".nuxt" ||
                                lowerDir == L"_internal" || lowerDir == L"venv" || lowerDir == L".venv" ||
                                lowerDir == L"__pycache__" || lowerDir == L".vscode") {
                                excludeDir = true;
                            }
                        }
                        if (!excludeDir && !filesToExclude.empty()) {
                            auto dstr = entry.path().wstring();
                            if (MatchesPatternList(filename, dstr, filesToExclude)) {
                                excludeDir = true;
                            }
                        }
                        if (excludeDir) {
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

            // Stream results continuously in worker thread in responsive batches
            std::vector<SearchFileGroup> currentBatch;
            size_t batchMatches = 0;
            bool isFirstBatch = true;
            const size_t BATCH_FILE_LIMIT = 12;
            const size_t BATCH_MATCH_LIMIT = 35;
            const size_t MAX_TOTAL_MATCHES = 1000;
            size_t totalFound = 0;

            for (size_t i = 0; i < filesToProcess.size(); ++i) {
                if (m_searchGeneration.load() != thisGen) {
                    m_isSearching = false;
                    return;
                }
                if (totalFound >= MAX_TOTAL_MATCHES) {
                    break;
                }

                SearchFileGroup group;
                if (SearchSingleFile(filesToProcess[i], root, query, lowerQuery,
                                     matchCase, matchWord, useRegex, regexValid, regexPattern,
                                     group, batchMatches, 50, MAX_TOTAL_MATCHES)) {
                    totalFound += group.matches.size();
                    currentBatch.push_back(std::move(group));
                }

                bool batchReady = (currentBatch.size() >= BATCH_FILE_LIMIT || batchMatches >= BATCH_MATCH_LIMIT);
                bool isLast = (i + 1 == filesToProcess.size()) || (totalFound >= MAX_TOTAL_MATCHES);

                if ((batchReady || isLast) && (!currentBatch.empty() || isLast)) {
                    if (m_searchGeneration.load() != thisGen) {
                        m_isSearching = false;
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_searchMutex);
                        if (isFirstBatch) {
                            m_pendingSearchGroups = std::move(currentBatch);
                            m_pendingTotalMatches = totalFound;
                            m_isBatchAppend = false;
                            isFirstBatch = false;
                        } else {
                            for (auto& g : currentBatch) {
                                m_pendingSearchGroups.push_back(std::move(g));
                            }
                            m_pendingTotalMatches = totalFound;
                            m_isBatchAppend = true;
                        }
                        m_searchCandidateFiles.clear();
                        m_searchCandidateFileIndex = i + 1;
                        m_searchHasMore = !isLast;
                        m_hasPendingResults = true;
                    }
                    currentBatch.clear();
                    batchMatches = 0;

                    if (!isLast) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
            }

            if (isFirstBatch) {
                std::lock_guard<std::mutex> lock(m_searchMutex);
                m_pendingSearchGroups.clear();
                m_pendingTotalMatches = 0;
                m_searchCandidateFiles.clear();
                m_searchCandidateFileIndex = 0;
                m_searchHasMore = false;
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

    if (m_onRefreshOpenDocs) {
        m_onRefreshOpenDocs();
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
            if (!std::filesystem::exists(fp, fec) && !m_openFolderPath.empty()) {
                std::filesystem::path cand = std::filesystem::path(m_openFolderPath) / fp;
                if (std::filesystem::exists(cand, fec)) fp = cand;
            }
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
                bool excludeDir = false;
                if (m_useExcludeSettings) {
                    std::wstring lowerDir = filename;
                    for (auto& c : lowerDir) c = towlower(c);
                    if (lowerDir == L"node_modules" || lowerDir == L"build" || lowerDir == L"bin" ||
                        lowerDir == L"obj" || lowerDir == L"target" || lowerDir == L".git" ||
                        lowerDir == L"dist" || lowerDir == L"out" || lowerDir == L"vendor" ||
                        lowerDir == L".cache" || lowerDir == L"tmp" || lowerDir == L"temp" ||
                        lowerDir == L"coverage" || lowerDir == L".next" || lowerDir == L".nuxt" ||
                        lowerDir == L"_internal" || lowerDir == L"venv" || lowerDir == L".venv" ||
                        lowerDir == L"__pycache__" || lowerDir == L".vscode") {
                        excludeDir = true;
                    }
                }
                if (!excludeDir && !m_filesToExclude.empty()) {
                    auto dstr = entry.path().wstring();
                    if (MatchesPatternList(filename, dstr, m_filesToExclude)) {
                        excludeDir = true;
                    }
                }
                if (excludeDir) {
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

// --- Inline Tree Item Create (Images 2 & 3: VS Code style new file / new folder) ---

void DrawerPanel::StartInlineCreate(const std::wstring& parentPath, bool isFolder) {
    CancelInlineRename();
    m_isCreating = true;
    m_createIsFolder = isFolder;
    m_createText.clear();
    m_createCaretPos = 0;
    m_rcCreateBox = D2D1::RectF(0, 0, 0, 0);
    m_rcCreatePill = D2D1::RectF(0, 0, 0, 0);

    std::wstring dir = parentPath;
    if (dir.empty()) {
        dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
    } else {
        std::error_code ec;
        if (std::filesystem::exists(dir, ec) && !std::filesystem::is_directory(dir, ec)) {
            dir = std::filesystem::path(dir).parent_path().wstring();
        }
    }
    m_createParentDir = dir;

    // Ensure parent folder is expanded
    if (dir != m_openFolderPath && !dir.empty()) {
        m_expandedDirs.insert(dir);
        RefreshTree();
    }

    // Determine depth and position in m_visibleItems
    m_createDepth = 0;
    m_createInsertAfterIdx = -1;

    std::wstring cleanDir = dir;
    while (cleanDir.size() > 3 && (cleanDir.back() == L'\\' || cleanDir.back() == L'/')) {
        cleanDir.pop_back();
    }

    std::error_code ec;
    std::wstring canonDir = std::filesystem::canonical(cleanDir, ec).wstring();
    if (canonDir.size() >= 4 && canonDir.rfind(L"\\\\?\\", 0) == 0) canonDir = canonDir.substr(4);
    if (canonDir.empty()) canonDir = cleanDir;

    for (size_t i = 0; i < m_visibleItems.size(); ++i) {
        std::wstring itemPath = m_visibleItems[i].fullPath;
        while (itemPath.size() > 3 && (itemPath.back() == L'\\' || itemPath.back() == L'/')) itemPath.pop_back();
        if (_wcsicmp(itemPath.c_str(), cleanDir.c_str()) == 0 ||
            _wcsicmp(itemPath.c_str(), canonDir.c_str()) == 0) {
            m_createInsertAfterIdx = static_cast<int>(i);
            m_createDepth = m_visibleItems[i].depth + 1;
            break;
        }
    }
}

void DrawerPanel::CancelInlineCreate() {
    m_isCreating = false;
    m_createIsFolder = false;
    m_createParentDir.clear();
    m_createText.clear();
    m_createCaretPos = 0;
    m_createInsertAfterIdx = -1;
    m_createDepth = 0;
    m_rcCreateBox = D2D1::RectF(0, 0, 0, 0);
    m_rcCreatePill = D2D1::RectF(0, 0, 0, 0);
}

bool DrawerPanel::CommitInlineCreate(std::wstring& outCreatedPath) {
    outCreatedPath.clear();
    if (!m_isCreating || m_createParentDir.empty() || m_createText.empty()) {
        CancelInlineCreate();
        return false;
    }

    std::filesystem::path target = std::filesystem::path(m_createParentDir) / m_createText;
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        CancelInlineCreate();
        return false;
    }

    if (m_createIsFolder) {
        std::filesystem::create_directories(target, ec);
        if (ec) {
            CancelInlineCreate();
            return false;
        }
        m_expandedDirs.insert(target.wstring());
    } else {
        std::filesystem::create_directories(target.parent_path(), ec);
        std::ofstream ofs(target);
        if (!ofs.is_open()) {
            CancelInlineCreate();
            return false;
        }
        ofs.close();
    }

    outCreatedPath = target.wstring();
    CancelInlineCreate();
    RefreshTree();
    return true;
}

void DrawerPanel::HandleCreateChar(wchar_t ch) {
    if (!m_isCreating) return;
    if (ch >= 32 && ch != L'\\' && ch != L'/' && ch != L':' && ch != L'*' &&
        ch != L'?' && ch != L'\"' && ch != L'<' && ch != L'>' && ch != L'|') {
        if (m_createCaretPos > m_createText.length()) m_createCaretPos = m_createText.length();
        m_createText.insert(m_createCaretPos, 1, ch);
        m_createCaretPos++;
    }
}

void DrawerPanel::HandleCreateKeyDown(WPARAM wParam) {
    if (!m_isCreating) return;
    if (wParam == VK_BACK) {
        if (m_createCaretPos > 0 && !m_createText.empty()) {
            m_createText.erase(m_createCaretPos - 1, 1);
            m_createCaretPos--;
        }
    } else if (wParam == VK_DELETE) {
        if (m_createCaretPos < m_createText.length()) {
            m_createText.erase(m_createCaretPos, 1);
        }
    } else if (wParam == VK_LEFT) {
        if (m_createCaretPos > 0) m_createCaretPos--;
    } else if (wParam == VK_RIGHT) {
        if (m_createCaretPos < m_createText.length()) m_createCaretPos++;
    } else if (wParam == VK_HOME) {
        m_createCaretPos = 0;
    } else if (wParam == VK_END) {
        m_createCaretPos = m_createText.length();
    } else if (wParam == VK_ESCAPE) {
        CancelInlineCreate();
    }
}

/**
 * @brief Handles mouse movement over Code Search buttons to update floating tooltips.
 * @param pt Cursor point in client coordinates.
 * @return True if tooltip state changed and requires repaint.
 */
bool DrawerPanel::HandleSearchMouseMove(POINT pt) {
    m_searchMousePos = pt;
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

    int oldHoverGroup = m_hoveredSearchGroupIdx;
    int oldHoverMatch = m_hoveredSearchMatchIdx;
    m_hoveredSearchGroupIdx = -1;
    m_hoveredSearchMatchIdx = -1;

    for (size_t g = 0; g < m_searchGroups.size(); ++g) {
        auto& group = m_searchGroups[g];
        if (group.headerBounds.right > group.headerBounds.left &&
            pt.x >= group.headerBounds.left && pt.x <= group.headerBounds.right &&
            pt.y >= group.headerBounds.top && pt.y <= group.headerBounds.bottom) {
            m_hoveredSearchGroupIdx = static_cast<int>(g);
            m_hoveredSearchMatchIdx = -1;
            if (checkBtn(group.replaceAllBtnBounds, L"Replace All (Ctrl+Shift+1)")) {}
            else if (checkBtn(group.dismissBtnBounds, L"Dismiss (Exclude from results)")) {}
            break;
        }

        if (group.isExpanded) {
            for (size_t m = 0; m < group.matches.size(); ++m) {
                auto& match = group.matches[m];
                if (match.bounds.right > match.bounds.left &&
                    pt.x >= match.bounds.left && pt.x <= match.bounds.right &&
                    pt.y >= match.bounds.top && pt.y <= match.bounds.bottom) {
                    m_hoveredSearchGroupIdx = static_cast<int>(g);
                    m_hoveredSearchMatchIdx = static_cast<int>(m);
                    if (checkBtn(match.replaceBtnBounds, L"Replace (Ctrl+Shift+1)")) {}
                    else if (checkBtn(match.dismissBtnBounds, L"Dismiss (Remove from results)")) {}
                    break;
                }
            }
            if (m_hoveredSearchGroupIdx >= 0) break;
        }
    }

    return (m_searchTooltipText != oldTip || m_hoveredSearchGroupIdx != oldHoverGroup || m_hoveredSearchMatchIdx != oldHoverMatch);
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

} // anonymous namespace

GitCmdResult RunGitCmd(const std::wstring& cmd, const std::wstring& workDir) {
    static std::mutex s_gitCmdMutex;
    std::lock_guard<std::mutex> cmdLock(s_gitCmdMutex);

    GitCmdResult res;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return res;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nullptr;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    PROCESS_INFORMATION pi{ 0 };
    std::wstring fullCmd = L"git " + cmd;
    std::vector<wchar_t> cmdBuf(fullCmd.begin(), fullCmd.end());
    cmdBuf.push_back(L'\0');

    std::wstring cleanWorkDir = workDir;
    if (cleanWorkDir.size() >= 4 && cleanWorkDir[0] == L'\\' && cleanWorkDir[1] == L'\\' && cleanWorkDir[2] == L'?' && cleanWorkDir[3] == L'\\') {
        cleanWorkDir = cleanWorkDir.substr(4);
    }
    LPCWSTR pDir = cleanWorkDir.empty() ? nullptr : cleanWorkDir.c_str();

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

void DrawerPanel::OpenSourceControl() {
    RefreshGitState(true, true);
}

void DrawerPanel::RefreshGitState(bool async, bool showLoading) {
    if (async) {
        bool expected = false;
        if (!m_isGitRefreshing.compare_exchange_strong(expected, true)) {
            // Already a git refresh operation running, drop redundant trigger
            return;
        }
    } else {
        m_isGitRefreshing.store(true);
    }

    auto task = [this, showLoading, async]() {
        struct RefreshFinisher {
            std::atomic<bool>& flag;
            std::atomic<bool>& loadingFlag;
            bool wasLoading;
            RefreshFinisher(std::atomic<bool>& f, std::atomic<bool>& lf, bool wl)
                : flag(f), loadingFlag(lf), wasLoading(wl) {}
            ~RefreshFinisher() {
                flag.store(false);
                if (wasLoading) loadingFlag.store(false);
            }
        } finisher(m_isGitRefreshing, m_isGitLoading, showLoading);

        if (showLoading) {
            m_isGitLoading = true;
        }
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;

        // 1. Check if inside git repository
        auto resCheck = RunGitCmd(L"rev-parse --is-inside-work-tree", dir);
        if (resCheck.exitCode != 0) {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            m_isGitRepo = false;
            m_gitChanges.clear();
            m_gitCommits.clear();
            if (showLoading) m_isGitLoading = false;
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
        RunGitCmd(L"update-index -q --refresh", dir);
        auto resStatus = RunGitCmd(L"status --porcelain=v1", dir);
        auto resDiff = RunGitCmd(L"diff --name-only", dir);
        auto resCached = RunGitCmd(L"diff --cached --name-only", dir);
        std::unordered_set<std::wstring> filesWithRealDiff;
        auto addDiffLines = [&](const std::wstring& out) {
            std::wstringstream dss(out);
            std::wstring dl;
            while (std::getline(dss, dl)) {
                while (!dl.empty() && (dl.back() == L'\r' || dl.back() == L'\n' || dl.back() == L' ')) dl.pop_back();
                if (!dl.empty()) {
                    for (auto& c : dl) if (c == L'/') c = L'\\';
                    filesWithRealDiff.insert(dl);
                }
            }
        };
        addDiffLines(resDiff.output);
        addDiffLines(resCached.output);

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

                // Check for rename destination "old -> new"
                size_t arrowPos = filePath.find(L" -> ");
                if (arrowPos != std::wstring::npos) {
                    filePath = filePath.substr(arrowPos + 4);
                    if (filePath.size() >= 2 && filePath.front() == L'"' && filePath.back() == L'"') {
                        filePath = filePath.substr(1, filePath.size() - 2);
                    }
                    for (auto& c : filePath) if (c == L'/') c = L'\\';
                    statusStr = L"R";
                }

                // Normalize porcelain status letters (Images 3 & 4)
                if (statusStr.find(L"?") != std::wstring::npos) {
                    statusStr = L"U"; // Untracked
                } else if (statusStr.find(L"R") != std::wstring::npos) {
                    statusStr = L"R"; // Renamed
                } else if (statusStr.find(L"M") != std::wstring::npos) {
                    statusStr = L"M"; // Modified
                } else if (statusStr.find(L"D") != std::wstring::npos) {
                    statusStr = L"D"; // Deleted
                } else if (statusStr.find(L"A") != std::wstring::npos) {
                    statusStr = L"A"; // Added
                }

                // Filter out false positives (e.g. electron-builder.yml unchanged files)
                if (statusStr == L"M") {
                    std::wstring cleanRel = filePath;
                    for (auto& c : cleanRel) if (c == L'/') c = L'\\';

                    bool isDirtyInMemory = false;
                    {
                        std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
                        for (const auto& df : m_dirtyFilePaths) {
                            std::wstring dfClean = df;
                            for (auto& c : dfClean) if (c == L'/') c = L'\\';
                            if (dfClean.ends_with(cleanRel) || cleanRel.ends_with(dfClean)) {
                                isDirtyInMemory = true;
                                break;
                            }
                        }
                    }

                    if (!isDirtyInMemory && filesWithRealDiff.count(cleanRel) == 0) {
                        continue; // File has ZERO diff! Prune false positive!
                    }
                }

                std::filesystem::path p(filePath);
                GitChangeItem item;
                item.path = filePath;
                item.fileName = p.filename().wstring();
                item.relDir = p.parent_path().wstring();
                item.status = statusStr.empty() ? L"M" : statusStr;
                changes.push_back(item);
            }
        }

        // Incorporate in-memory dirty documents into changes list
        {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            for (const auto& fp : m_dirtyFilePaths) {
                std::wstring rel = fp;
                if (!dir.empty() && rel.rfind(dir, 0) == 0) {
                    rel = rel.substr(dir.length());
                    while (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/')) {
                        rel.erase(0, 1);
                    }
                }
                for (auto& c : rel) if (c == L'/') c = L'\\';
                if (rel.empty()) continue;

                bool exists = false;
                for (const auto& chg : changes) {
                    if (_wcsicmp(chg.path.c_str(), rel.c_str()) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    GitChangeItem item;
                    item.path = rel;
                    item.fileName = std::filesystem::path(rel).filename().wstring();
                    item.relDir = std::filesystem::path(rel).parent_path().wstring();
                    item.status = L"M";
                    changes.push_back(std::move(item));
                }
            }
        }

        // Snapshot existing commits to preserve expanded states and cached files across background polling (Complaint 2)
        std::map<std::wstring, GitCommitItem> prevMap;
        {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            for (auto& item : m_gitCommits) {
                prevMap[item.hash] = std::move(item);
            }
        }

        // 4. Query commit history graph with formatted date
        auto resLog = RunGitCmd(L"log -n 40 --pretty=format:\"%h|%p|%s|%an|%ar|%d|%ad\" --date=format:\"%B %d, %Y at %I:%M %p\"", dir);
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
                    ci.dateFormatted = tokens.size() > 6 ? tokens[6] : L"";
                    ci.isHead = firstCommit || (ci.refNames.find(L"HEAD") != std::wstring::npos);
                    firstCommit = false;

                    auto it = prevMap.find(ci.hash);
                    if (it != prevMap.end()) {
                        ci.statSummary = it->second.statSummary;
                    }

                    // Restore expanded state and cached files if in m_expandedCommitHashes
                    if (m_expandedCommitHashes.count(ci.hash)) {
                        ci.isExpanded = true;
                        if (it != prevMap.end() && it->second.filesLoaded) {
                            ci.files = std::move(it->second.files);
                            ci.filesLoaded = true;
                        } else {
                            LoadCommitFiles(ci);
                        }
                    }

                    commits.push_back(std::move(ci));
                }
            }
            ComputeGitGraphLanes(commits);
        }

        // 5. Query unpushed commits count (rev-list @{u}..HEAD or ahead in status -sb)
        int unpushedCount = 0;
        auto resRevList = RunGitCmd(L"rev-list --count @{u}..HEAD", dir);
        if (resRevList.exitCode == 0 && !resRevList.output.empty()) {
            try {
                unpushedCount = std::stoi(resRevList.output);
            } catch (...) {}
        } else {
            auto resSb = RunGitCmd(L"status -sb", dir);
            if (resSb.exitCode == 0) {
                size_t aheadPos = resSb.output.find(L"ahead ");
                if (aheadPos != std::wstring::npos) {
                    try {
                        unpushedCount = std::stoi(resSb.output.substr(aheadPos + 6));
                    } catch (...) {}
                }
            }
        }

        {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            m_isGitRepo = true;
            m_gitBranch = branch;
            m_gitChanges = std::move(changes);
            m_gitCommits = std::move(commits);
            m_unpushedCommitsCount = unpushedCount;
            if (showLoading) {
                m_isGitLoading = false;
            }
        }

        if (m_onGitStateChanged) {
            m_onGitStateChanged();
        }
    };

    if (async) {
        std::thread(task).detach();
    } else {
        task();
    }
}


static bool PerformLineReplacement(std::wstring& text, const std::wstring& searchQ, const std::wstring& replaceQ, bool matchCase, bool matchWord) {
    if (searchQ.empty()) return false;
    bool anyReplaced = false;

    std::wstring lowerText = text;
    std::wstring lowerQ = searchQ;
    if (!matchCase) {
        for (auto& c : lowerText) c = towlower(c);
        for (auto& c : lowerQ) c = towlower(c);
    }

    std::wstring result;
    size_t lastPos = 0;
    size_t pos = 0;

    auto isWordBoundary = [](wchar_t c) {
        return !iswalnum(c) && c != L'_';
    };

    while ((pos = lowerText.find(lowerQ, lastPos)) != std::wstring::npos) {
        if (matchWord) {
            bool leftOk = (pos == 0) || isWordBoundary(text[pos - 1]);
            bool rightOk = (pos + searchQ.length() >= text.length()) || isWordBoundary(text[pos + searchQ.length()]);
            if (!leftOk || !rightOk) {
                result.append(text, lastPos, pos - lastPos + searchQ.length());
                lastPos = pos + searchQ.length();
                continue;
            }
        }
        result.append(text, lastPos, pos - lastPos);
        result.append(replaceQ);
        lastPos = pos + searchQ.length();
        anyReplaced = true;
    }
    result.append(text, lastPos, text.length() - lastPos);
    if (anyReplaced) {
        text = result;
    }
    return anyReplaced;
}

void DrawerPanel::ExecuteReplaceSingleMatch(const std::wstring& filePath, const SearchMatch& match) {
    if (m_searchQuery.empty()) return;
    phide::editor::Buffer buf;
    if (!buf.LoadFromFile(filePath)) return;

    if (match.lineIndex < buf.GetLineCount()) {
        std::wstring line = buf.GetLine(match.lineIndex);
        bool modified = false;
        if (match.matchCol + match.matchLen <= line.length()) {
            std::wstring sub = line.substr(match.matchCol, match.matchLen);
            bool matchesSub = m_matchCase ? (sub == m_searchQuery) : (_wcsicmp(sub.c_str(), m_searchQuery.c_str()) == 0);
            if (matchesSub) {
                line.replace(match.matchCol, match.matchLen, m_replaceQuery);
                modified = true;
            }
        }
        if (!modified) {
            modified = PerformLineReplacement(line, m_searchQuery, m_replaceQuery, m_matchCase, m_matchWholeWord);
        }
        if (modified) {
            buf.SetLine(match.lineIndex, line);
            buf.SaveToFile(filePath);
            if (m_onFilesReplaced) {
                m_onFilesReplaced({ filePath });
            }
        }
    }

    // Remove this match from search results
    for (size_t g = 0; g < m_searchGroups.size(); ++g) {
        if (m_searchGroups[g].filePath == filePath) {
            for (size_t m = 0; m < m_searchGroups[g].matches.size(); ++m) {
                if (m_searchGroups[g].matches[m].lineIndex == match.lineIndex &&
                    m_searchGroups[g].matches[m].matchCol == match.matchCol) {
                    DismissSearchMatch(g, m);
                    return;
                }
            }
            for (size_t m = 0; m < m_searchGroups[g].matches.size(); ++m) {
                if (m_searchGroups[g].matches[m].lineIndex == match.lineIndex) {
                    DismissSearchMatch(g, m);
                    return;
                }
            }
            break;
        }
    }
}

void DrawerPanel::ExecuteReplaceInFile(const std::wstring& filePath) {
    if (m_searchQuery.empty()) return;
    phide::editor::Buffer buf;
    if (!buf.LoadFromFile(filePath)) return;

    bool anyModified = false;
    for (size_t l = 0; l < buf.GetLineCount(); ++l) {
        std::wstring line = buf.GetLine(l);
        if (PerformLineReplacement(line, m_searchQuery, m_replaceQuery, m_matchCase, m_matchWholeWord)) {
            buf.SetLine(l, line);
            anyModified = true;
        }
    }
    if (anyModified) {
        buf.SaveToFile(filePath);
        if (m_onFilesReplaced) {
            m_onFilesReplaced({ filePath });
        }
    }

    // Dismiss this file group from search results
    for (size_t g = 0; g < m_searchGroups.size(); ++g) {
        if (m_searchGroups[g].filePath == filePath) {
            DismissSearchGroup(g);
            return;
        }
    }
}

void DrawerPanel::ExecuteReplaceAll() {
    if (m_searchQuery.empty() || m_searchGroups.empty()) return;

    std::vector<std::wstring> replacedFiles;
    for (const auto& group : m_searchGroups) {
        phide::editor::Buffer buf;
        if (!buf.LoadFromFile(group.filePath)) continue;

        bool anyModified = false;
        for (size_t l = 0; l < buf.GetLineCount(); ++l) {
            std::wstring line = buf.GetLine(l);
            if (PerformLineReplacement(line, m_searchQuery, m_replaceQuery, m_matchCase, m_matchWholeWord)) {
                buf.SetLine(l, line);
                anyModified = true;
            }
        }
        if (anyModified) {
            buf.SaveToFile(group.filePath);
            replacedFiles.push_back(group.filePath);
        }
    }

    ClearSearchResults();
    if (m_onFilesReplaced && !replacedFiles.empty()) {
        m_onFilesReplaced(replacedFiles);
    }
}

void DrawerPanel::DismissSearchMatch(size_t groupIdx, size_t matchIdx) {
    if (groupIdx >= m_searchGroups.size()) return;
    auto& group = m_searchGroups[groupIdx];
    if (matchIdx >= group.matches.size()) return;

    group.matches.erase(group.matches.begin() + matchIdx);
    if (m_totalMatches > 0) m_totalMatches--;

    if (group.matches.empty()) {
        m_searchGroups.erase(m_searchGroups.begin() + groupIdx);
    }
}

void DrawerPanel::DismissSearchGroup(size_t groupIdx) {
    if (groupIdx >= m_searchGroups.size()) return;
    m_totalMatches -= (std::min)(m_totalMatches, m_searchGroups[groupIdx].matches.size());
    m_searchGroups.erase(m_searchGroups.begin() + groupIdx);
}

void DrawerPanel::RefreshGitStatusOnly(bool async) {
    auto task = [this]() {
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;

        // 1. Check if git repo
        auto resCheck = RunGitCmd(L"rev-parse --is-inside-work-tree", dir);
        if (resCheck.exitCode != 0) {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            m_isGitRepo = false;
            m_gitChanges.clear();
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
        RunGitCmd(L"update-index -q --refresh", dir);
        auto resStatus = RunGitCmd(L"status --porcelain=v1", dir);
        auto resDiff = RunGitCmd(L"diff --name-only", dir);
        auto resCached = RunGitCmd(L"diff --cached --name-only", dir);
        std::unordered_set<std::wstring> filesWithRealDiff;
        auto addDiffLines = [&](const std::wstring& out) {
            std::wstringstream dss(out);
            std::wstring dl;
            while (std::getline(dss, dl)) {
                while (!dl.empty() && (dl.back() == L'\r' || dl.back() == L'\n' || dl.back() == L' ')) dl.pop_back();
                if (!dl.empty()) {
                    for (auto& c : dl) if (c == L'/') c = L'\\';
                    filesWithRealDiff.insert(dl);
                }
            }
        };
        addDiffLines(resDiff.output);
        addDiffLines(resCached.output);

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

                // Check for rename destination "old -> new"
                size_t arrowPos = filePath.find(L" -> ");
                if (arrowPos != std::wstring::npos) {
                    filePath = filePath.substr(arrowPos + 4);
                    if (filePath.size() >= 2 && filePath.front() == L'"' && filePath.back() == L'"') {
                        filePath = filePath.substr(1, filePath.size() - 2);
                    }
                    for (auto& c : filePath) if (c == L'/') c = L'\\';
                    statusStr = L"R";
                }

                // Normalize porcelain status letters (Images 3 & 4)
                if (statusStr.find(L"?") != std::wstring::npos) {
                    statusStr = L"U"; // Untracked
                } else if (statusStr.find(L"R") != std::wstring::npos) {
                    statusStr = L"R"; // Renamed
                } else if (statusStr.find(L"M") != std::wstring::npos) {
                    statusStr = L"M"; // Modified
                } else if (statusStr.find(L"D") != std::wstring::npos) {
                    statusStr = L"D"; // Deleted
                } else if (statusStr.find(L"A") != std::wstring::npos) {
                    statusStr = L"A"; // Added
                }

                // Filter out false positives (e.g. electron-builder.yml unchanged files)
                if (statusStr == L"M") {
                    std::wstring cleanRel = filePath;
                    for (auto& c : cleanRel) if (c == L'/') c = L'\\';

                    bool isDirtyInMemory = false;
                    {
                        std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
                        for (const auto& df : m_dirtyFilePaths) {
                            std::wstring dfClean = df;
                            for (auto& c : dfClean) if (c == L'/') c = L'\\';
                            if (dfClean.ends_with(cleanRel) || cleanRel.ends_with(dfClean)) {
                                isDirtyInMemory = true;
                                break;
                            }
                        }
                    }

                    if (!isDirtyInMemory && filesWithRealDiff.count(cleanRel) == 0) {
                        continue; // File has ZERO diff! Prune false positive!
                    }
                }

                std::filesystem::path p(filePath);
                GitChangeItem item;
                item.path = filePath;
                item.fileName = p.filename().wstring();
                item.relDir = p.parent_path().wstring();
                item.status = statusStr.empty() ? L"M" : statusStr;
                changes.push_back(item);
            }
        }

        // Incorporate in-memory dirty documents into changes list
        {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            for (const auto& fp : m_dirtyFilePaths) {
                std::wstring rel = fp;
                if (!dir.empty() && rel.rfind(dir, 0) == 0) {
                    rel = rel.substr(dir.length());
                    while (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/')) {
                        rel.erase(0, 1);
                    }
                }
                for (auto& c : rel) if (c == L'/') c = L'\\';
                if (rel.empty()) continue;

                bool exists = false;
                for (const auto& chg : changes) {
                    if (_wcsicmp(chg.path.c_str(), rel.c_str()) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    GitChangeItem item;
                    item.path = rel;
                    item.fileName = std::filesystem::path(rel).filename().wstring();
                    item.relDir = std::filesystem::path(rel).parent_path().wstring();
                    item.status = L"M";
                    changes.push_back(std::move(item));
                }
            }
        }

        // 4. Query unpushed commits count
        int unpushedCount = 0;
        auto resRevList = RunGitCmd(L"rev-list --count @{u}..HEAD", dir);
        if (resRevList.exitCode == 0 && !resRevList.output.empty()) {
            try { unpushedCount = std::stoi(resRevList.output); } catch (...) {}
        } else {
            auto resSb = RunGitCmd(L"status -sb", dir);
            if (resSb.exitCode == 0) {
                size_t aheadPos = resSb.output.find(L"ahead ");
                if (aheadPos != std::wstring::npos) {
                    try { unpushedCount = std::stoi(resSb.output.substr(aheadPos + 6)); } catch (...) {}
                }
            }
        }

        bool stateChanged = false;
        {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            if (!m_isGitRepo || m_gitBranch != branch || m_unpushedCommitsCount != unpushedCount ||
                m_gitChanges.size() != changes.size()) {
                stateChanged = true;
            } else {
                for (size_t i = 0; i < changes.size(); ++i) {
                    if (m_gitChanges[i].path != changes[i].path || m_gitChanges[i].status != changes[i].status) {
                        stateChanged = true;
                        break;
                    }
                }
            }

            m_isGitRepo = true;
            m_gitBranch = branch;
            m_gitChanges = std::move(changes);
            m_unpushedCommitsCount = unpushedCount;
        }

        if (stateChanged && m_onGitStateChanged) {
            m_onGitStateChanged();
        }
    };

    if (async) {
        std::thread(task).detach();
    } else {
        task();
    }
}

void DrawerPanel::LoadCommitFiles(GitCommitItem& ci) {
    if (ci.hash.empty()) return;
    std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
    if (!ci.filesLoaded) {
        auto res = RunGitCmd(L"show --name-status --pretty=\"\" " + ci.hash, dir);
        ci.files.clear();
        if (res.exitCode == 0) {
            std::wstringstream ss(res.output);
            std::wstring line;
            while (std::getline(ss, line)) {
                while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ')) line.pop_back();
                if (line.empty()) continue;
                size_t tab = line.find(L'\t');
                std::wstring status = L"M";
                std::wstring filePath;
                if (tab != std::wstring::npos) {
                    status = line.substr(0, tab);
                    filePath = line.substr(tab + 1);
                } else {
                    filePath = line;
                }
                if (filePath.size() >= 2 && filePath.front() == L'"' && filePath.back() == L'"') {
                    filePath = filePath.substr(1, filePath.size() - 2);
                }
                for (auto& c : filePath) if (c == L'/') c = L'\\';

                std::filesystem::path p(filePath);
                GitCommitFile cf;
                cf.path = filePath;
                cf.fileName = p.filename().wstring();
                cf.relDir = p.parent_path().wstring();
                cf.status = status.empty() ? L"M" : status;
                ci.files.push_back(std::move(cf));
            }
        }
        ci.filesLoaded = true;
    }

    if (ci.statSummary.empty()) {
        auto resStat = RunGitCmd(L"show --shortstat --format=\"\" " + ci.hash, dir);
        if (resStat.exitCode == 0 && !resStat.output.empty()) {
            std::wstring st = resStat.output;
            size_t f = st.find_first_not_of(L" \r\n\t");
            if (f != std::wstring::npos) st = st.substr(f);
            while (!st.empty() && (st.back() == L'\r' || st.back() == L'\n' || st.back() == L' ')) st.pop_back();
            ci.statSummary = st;
        }
    }
}

void DrawerPanel::ComputeGitGraphLanes(std::vector<GitCommitItem>& commits) {
    if (commits.empty()) return;

    std::vector<std::wstring> activeLanes;

    for (size_t i = 0; i < commits.size(); ++i) {
        auto& ci = commits[i];

        // 1. Locate lane for this commit if assigned by child
        int assignedLane = -1;
        for (size_t l = 0; l < activeLanes.size(); ++l) {
            if (!activeLanes[l].empty() && activeLanes[l] == ci.hash) {
                assignedLane = static_cast<int>(l);
                break;
            }
        }

        if (assignedLane == -1) {
            for (size_t l = 0; l < activeLanes.size(); ++l) {
                if (activeLanes[l].empty()) {
                    assignedLane = static_cast<int>(l);
                    break;
                }
            }
        }

        if (assignedLane == -1) {
            assignedLane = static_cast<int>(activeLanes.size());
            activeLanes.push_back(ci.hash);
        } else {
            activeLanes[assignedLane] = ci.hash;
        }

        ci.lane = assignedLane;
        ci.colorIdx = assignedLane % 8;

        // 2. Tokenize parent hashes
        std::vector<std::wstring> parents;
        std::wstringstream pss(ci.parentHash);
        std::wstring ph;
        while (pss >> ph) {
            if (!ph.empty()) parents.push_back(ph);
        }

        // 3. Mark pass-through lanes for vertical continuation
        for (size_t l = 0; l < activeLanes.size(); ++l) {
            if (static_cast<int>(l) != ci.lane && !activeLanes[l].empty()) {
                ci.passThroughLanes.push_back(static_cast<int>(l));
            }
        }

        // 4. Update active lanes with parents
        if (parents.empty()) {
            activeLanes[ci.lane].clear();
        } else {
            activeLanes[ci.lane] = parents[0];
            ci.parentLanes.push_back(ci.lane);

            for (size_t p = 1; p < parents.size(); ++p) {
                const auto& parentHash = parents[p];
                int pLane = -1;
                for (size_t l = 0; l < activeLanes.size(); ++l) {
                    if (activeLanes[l] == parentHash) {
                        pLane = static_cast<int>(l);
                        break;
                    }
                }
                if (pLane == -1) {
                    for (size_t l = 0; l < activeLanes.size(); ++l) {
                        if (activeLanes[l].empty()) {
                            pLane = static_cast<int>(l);
                            activeLanes[l] = parentHash;
                            break;
                        }
                    }
                }
                if (pLane == -1) {
                    pLane = static_cast<int>(activeLanes.size());
                    activeLanes.push_back(parentHash);
                }
                ci.parentLanes.push_back(pLane);
            }
        }
    }
}

void DrawerPanel::SetDirtyFiles(const std::vector<std::wstring>& dirtyFilePaths) {
    std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
    m_dirtyFilePaths = dirtyFilePaths;
    if (!m_isGitRepo) return;

    std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;

    bool changed = false;
    for (const auto& fp : m_dirtyFilePaths) {
        std::wstring rel = fp;
        if (!dir.empty() && rel.rfind(dir, 0) == 0) {
            rel = rel.substr(dir.length());
            while (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/')) {
                rel.erase(0, 1);
            }
        }
        for (auto& c : rel) if (c == L'/') c = L'\\';
        if (rel.empty()) continue;

        bool found = false;
        for (const auto& chg : m_gitChanges) {
            if (_wcsicmp(chg.path.c_str(), rel.c_str()) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            GitChangeItem item;
            item.path = rel;
            item.fileName = std::filesystem::path(rel).filename().wstring();
            item.relDir = std::filesystem::path(rel).parent_path().wstring();
            item.status = L"M";
            m_gitChanges.push_back(std::move(item));
            changed = true;
        }
    }

    if (changed && m_onGitStateChanged) {
        m_onGitStateChanged();
    }
}

void DrawerPanel::StageAllChangesAsync() {
    std::thread([this]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        RunGitCmd(L"add -A", dir);
        RefreshGitStatusOnly(false);
        m_isGitLoading = false;
    }).detach();
}

bool DrawerPanel::DiscardAllChanges() {
    m_isGitLoading = true;
    std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
    RunGitCmd(L"restore --staged --worktree .", dir);
    RunGitCmd(L"checkout HEAD -- .", dir);
    RunGitCmd(L"clean -fd", dir);
    RefreshGitStatusOnly(false);
    m_isGitLoading = false;
    return true;
}

void DrawerPanel::DiscardAllChangesAsync() {
    std::thread([this]() {
        DiscardAllChanges();
    }).detach();
}

void DrawerPanel::StageFileAsync(const std::wstring& relPath) {
    std::thread([this, relPath]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        std::wstring cleanRel = relPath;
        for (auto& c : cleanRel) if (c == L'\\') c = L'/';
        RunGitCmd(L"add \"" + cleanRel + L"\"", dir);
        RefreshGitStatusOnly(false);
        m_isGitLoading = false;
    }).detach();
}

bool DrawerPanel::DiscardFile(const std::wstring& relPath) {
    m_isGitLoading = true;
    std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
    std::wstring cleanRel = relPath;
    for (auto& c : cleanRel) if (c == L'\\') c = L'/';
    RunGitCmd(L"restore --staged --worktree -- \"" + cleanRel + L"\"", dir);
    RunGitCmd(L"checkout HEAD -- \"" + cleanRel + L"\"", dir);
    RunGitCmd(L"clean -fd -- \"" + cleanRel + L"\"", dir);
    RefreshGitStatusOnly(false);
    m_isGitLoading = false;
    return true;
}

void DrawerPanel::DiscardFileAsync(const std::wstring& relPath) {
    std::thread([this, relPath]() {
        DiscardFile(relPath);
    }).detach();
}

std::wstring DrawerPanel::GetCommitFileContent(const std::wstring& commitHash, const std::wstring& relPath) {
    std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
    std::wstring cleanRel = relPath;
    for (auto& c : cleanRel) if (c == L'\\') c = L'/';
    auto res = RunGitCmd(L"show " + commitHash + L":\"" + cleanRel + L"\"", dir);
    if (res.exitCode == 0) return res.output;
    return L"";
}

void DrawerPanel::CommitChangesAsync(const std::wstring& message) {
    std::wstring msg = message;
    // Trim whitespace
    size_t first = msg.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        // Refuse to commit without message (Complaint 5 & 7)
        m_focusedGitInput = 1;
        m_gitTooltipText = L"Please provide a commit message before committing.";
        m_gitTooltipTarget = m_gitMsgBoxBounds;
        m_showGitTooltip = true;
        return;
    }
    msg = msg.substr(first);
    size_t last = msg.find_last_not_of(L" \t\r\n");
    if (last != std::wstring::npos) msg = msg.substr(0, last + 1);

    std::thread([this, msg]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        RunGitCmd(L"add -A", dir);
        std::wstring escapedMsg;
        for (wchar_t c : msg) {
            if (c == L'"') escapedMsg += L"\\\"";
            else escapedMsg += c;
        }
        RunGitCmd(L"commit -m \"" + escapedMsg + L"\"", dir);
        {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            m_commitMessage.clear();
        }
        RefreshGitState(false);
    }).detach();
}

void DrawerPanel::UncommitLastCommitAsync() {
    std::thread([this]() {
        m_isGitLoading = true;
        std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
        // 1. Get the last commit message before undoing
        auto resMsg = RunGitCmd(L"log -1 --pretty=%B", dir);
        std::wstring lastMsg = resMsg.output;
        while (!lastMsg.empty() && (lastMsg.back() == L'\r' || lastMsg.back() == L'\n' || lastMsg.back() == L' ')) {
            lastMsg.pop_back();
        }
        // 2. Soft reset HEAD~1 (keeps all modified/staged files in working directory)
        auto resReset = RunGitCmd(L"reset --soft HEAD~1", dir);
        if (resReset.exitCode == 0) {
            std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
            if (!lastMsg.empty()) {
                m_commitMessage = lastMsg;
            }
            m_focusedGitInput = 1;
        }
        RefreshGitState(false);
    }).detach();
}

void DrawerPanel::PushCommitsAsync() {
    std::thread([this]() {
        try {
            m_isGitLoading = true;
            std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
            RunGitCmd(L"push", dir);
            RefreshGitState(false);
        } catch (...) {}
    }).detach();
}

void DrawerPanel::PullCommitsAsync() {
    std::thread([this]() {
        try {
            m_isGitLoading = true;
            std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
            RunGitCmd(L"pull", dir);
            RefreshGitState(false);
        } catch (...) {}
    }).detach();
}

void DrawerPanel::FetchAllRemotesAsync() {
    std::thread([this]() {
        try {
            m_isGitLoading = true;
            std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
            RunGitCmd(L"fetch --all", dir);
            RefreshGitState(false);
        } catch (...) {}
    }).detach();
}

void DrawerPanel::StashChangesAsync() {
    std::thread([this]() {
        try {
            m_isGitLoading = true;
            std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
            RunGitCmd(L"stash", dir);
            RefreshGitState(false);
        } catch (...) {}
    }).detach();
}

void DrawerPanel::RunGarbageCollectionAsync() {
    std::thread([this]() {
        try {
            m_isGitLoading = true;
            std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
            // 1. Run git garbage collection & prune loose unreachable objects
            RunGitCmd(L"gc --prune=now --auto", dir);
            // 2. Perform in-memory IDE garbage collection
            {
                std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
                m_gitCommits.shrink_to_fit();
                m_gitChanges.shrink_to_fit();
                m_expandedCommitHashes.clear();
            }
            // 3. Trim process working set
            SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
            RefreshGitState(false);
        } catch (...) {}
    }).detach();
}

std::wstring DrawerPanel::GetGitFileContent(const std::wstring& relPath) {
    std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
    std::wstring cleanRel = relPath;
    for (auto& c : cleanRel) if (c == L'\\') c = L'/';
    auto res = RunGitCmd(L"show HEAD:\"" + cleanRel + L"\"", dir);
    if (res.exitCode == 0) {
        return res.output;
    }
    return L"";
}

void DrawerPanel::ComputeGitDiffLines(const std::wstring& relPath, std::set<size_t>& outAdded, std::set<size_t>& outDeleted) {
    outAdded.clear();
    outDeleted.clear();
    std::wstring dir = m_openFolderPath.empty() ? L"." : m_openFolderPath;
    std::wstring cleanRel = relPath;
    for (auto& c : cleanRel) if (c == L'\\') c = L'/';
    auto res = RunGitCmd(L"diff -U0 HEAD -- \"" + cleanRel + L"\"", dir);
    if (res.exitCode == 0 && !res.output.empty()) {
        std::wstringstream ss(res.output);
        std::wstring line;
        while (std::getline(ss, line)) {
            if (line.rfind(L"@@", 0) == 0) {
                // e.g. @@ -10,3 +10,5 @@ or @@ -10 +10,5 @@
                size_t plusPos = line.find(L'+');
                if (plusPos != std::wstring::npos) {
                    size_t endPos = line.find(L' ', plusPos);
                    std::wstring hunk = (endPos != std::wstring::npos) ?
                        line.substr(plusPos + 1, endPos - plusPos - 1) :
                        line.substr(plusPos + 1);
                    size_t commaPos = hunk.find(L',');
                    int startLine = 1;
                    int count = 1;
                    if (commaPos != std::wstring::npos) {
                        try {
                            startLine = std::stoi(hunk.substr(0, commaPos));
                            count = std::stoi(hunk.substr(commaPos + 1));
                        } catch (...) {}
                    } else {
                        try {
                            startLine = std::stoi(hunk);
                            count = 1;
                        } catch (...) {}
                    }
                    if (count > 0 && startLine > 0) {
                        for (int l = 0; l < count; ++l) {
                            outAdded.insert(static_cast<size_t>(startLine - 1 + l));
                        }
                    } else if (count == 0 && startLine > 0) {
                        outDeleted.insert(static_cast<size_t>(startLine > 1 ? startLine - 1 : 0));
                    }
                }
            }
        }
    }
}

void DrawerPanel::GenerateAiCommitMessage() {
    std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
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

bool DrawerPanel::HitTestGit(POINT pt, const app::AppConfig& config,
                            std::wstring& outOpenFile,
                            std::wstring& outDiscardedFile,
                            bool& outIsDiff,
                            std::wstring& outOpenCommitDiff,
                            std::wstring& outOpenUrl) {
    (void)config;
    std::lock_guard<std::recursive_mutex> lock(m_gitMutex);
    outOpenFile.clear();
    outDiscardedFile.clear();
    outIsDiff = false;
    outOpenCommitDiff.clear();
    outOpenUrl.clear();

    // 0. Check Commit Hover Card buttons first (Image 5)
    if (m_hoveredCommitIdx >= 0 || m_selectedCommitIdx >= 0) {
        if (pt.x >= m_commitCardCloseBounds.left && pt.x <= m_commitCardCloseBounds.right &&
            pt.y >= m_commitCardCloseBounds.top && pt.y <= m_commitCardCloseBounds.bottom) {
            DismissCommitCard();
            return true;
        }
        if (pt.x >= m_commitCardOpenChangesBounds.left && pt.x <= m_commitCardOpenChangesBounds.right &&
            pt.y >= m_commitCardOpenChangesBounds.top && pt.y <= m_commitCardOpenChangesBounds.bottom) {
            outOpenCommitDiff = m_commitCardHash;
            outIsDiff = true;
            DismissCommitCard();
            return true;
        }
        if (pt.x >= m_commitCardGitHubBounds.left && pt.x <= m_commitCardGitHubBounds.right &&
            pt.y >= m_commitCardGitHubBounds.top && pt.y <= m_commitCardGitHubBounds.bottom) {
            outOpenUrl = L"https://github.com/Tribrix23/phide/commit/" + m_commitCardHash;
            DismissCommitCard();
            return true;
        }
        if (pt.x >= m_commitCardBounds.left && pt.x <= m_commitCardBounds.right &&
            pt.y >= m_commitCardBounds.top && pt.y <= m_commitCardBounds.bottom) {
            return true; // Click inside card body is absorbed
        }
    }

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
                            RefreshGitState(true, true);
                            break;
                        case 10: // Stage All Changes
                            StageAllChangesAsync();
                            break;
                        case 11: // Discard All Changes
                            DiscardAllChangesAsync();
                            outDiscardedFile = L"*";
                            break;
                        case 13: // Undo Last Commit (Uncommit)
                            UncommitLastCommitAsync();
                            break;
                        case 12: // Stash Changes
                            StashChangesAsync();
                            break;
                        case 21: // Push Commits
                            PushCommitsAsync();
                            break;
                        case 22: // Pull Commits
                            PullCommitsAsync();
                            break;
                        case 23: // Fetch All Remotes
                            FetchAllRemotesAsync();
                            break;
                        case 24: // Run Garbage Collection (git gc)
                            RunGarbageCollectionAsync();
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
            outDiscardedFile = L"*";
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
            } else if (pt.y >= m_gitMsgBoxBounds.top && pt.y <= m_gitMsgBoxBounds.bottom) {
                m_focusedGitInput = 0;
            }

            // Primary Commit / Sync Changes Button (Image 1 & 5)
            if (pt.x >= m_gitCommitBtnBounds.left && pt.x <= m_gitCommitBtnBounds.right &&
                pt.y >= m_gitCommitBtnBounds.top && pt.y <= m_gitCommitBtnBounds.bottom) {
                if (!m_gitChanges.empty()) {
                    std::wstring trimmed = m_commitMessage;
                    size_t f = trimmed.find_first_not_of(L" \t\r\n");
                    if (f == std::wstring::npos) {
                        m_focusedGitInput = 1;
                        m_gitTooltipText = L"Please provide a commit message before committing.";
                        m_gitTooltipTarget = m_gitMsgBoxBounds;
                        m_showGitTooltip = true;
                        return true;
                    }
                    CommitChangesAsync(m_commitMessage);
                    return true;
                } else if (m_unpushedCommitsCount > 0) {
                    PushCommitsAsync();
                    return true;
                }
                return true; // Disabled when 0 changes and 0 unpushed
            }

            // Changed file rows (Image 4 & 5)
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
                        outDiscardedFile = item.path;
                        return true;
                    }
                    outOpenFile = item.path;
                    outIsDiff = true; // Opens side-by-side diff comparison! (Image 2)
                    return true;
                }
            }
        }
    }

    // 4. Graph Section Header, Toolbar & Commits (Image 2 & 3)
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
            RefreshGitState(true, true);
            return true;
        }
        if (pt.x >= m_gitGraphMergeBounds.left && pt.x <= m_gitGraphMergeBounds.right &&
            pt.y >= m_gitGraphMergeBounds.top && pt.y <= m_gitGraphMergeBounds.bottom) {
            RefreshGitState(true, true);
            return true;
        }
        if (pt.x >= m_gitGraphPushBounds.left && pt.x <= m_gitGraphPushBounds.right &&
            pt.y >= m_gitGraphPushBounds.top && pt.y <= m_gitGraphPushBounds.bottom) {
            PushCommitsAsync();
            return true;
        }
        if (pt.x >= m_gitGraphRefreshBounds.left && pt.x <= m_gitGraphRefreshBounds.right &&
            pt.y >= m_gitGraphRefreshBounds.top && pt.y <= m_gitGraphRefreshBounds.bottom) {
            RefreshGitState(true, true);
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

        // Commit Rows & Expanded File Details (Image 2 & 3)
        if (m_graphExpanded) {
            for (size_t i = 0; i < m_gitCommits.size(); ++i) {
                auto& ci = m_gitCommits[i];
                // Check if Open Changes icon on the commit row was clicked (Image 3)
                if (ci.openChangesBtnBounds.right > ci.openChangesBtnBounds.left &&
                    pt.x >= ci.openChangesBtnBounds.left && pt.x <= ci.openChangesBtnBounds.right &&
                    pt.y >= ci.openChangesBtnBounds.top && pt.y <= ci.openChangesBtnBounds.bottom) {
                    outOpenCommitDiff = ci.hash;
                    outIsDiff = true;
                    DismissCommitCard();
                    return true;
                }
                // If commit is expanded, check clicking any of its changed files to open diff in editor
                if (ci.isExpanded) {
                    for (const auto& cf : ci.files) {
                        if (pt.x >= cf.bounds.left && pt.x <= cf.bounds.right &&
                            pt.y >= cf.bounds.top && pt.y <= cf.bounds.bottom) {
                            outOpenCommitDiff = ci.hash;
                            outOpenFile = cf.path;
                            outIsDiff = true;
                            return true;
                        }
                    }
                }
                // Clicking commit row selects it to show card & toggles expansion
                if (pt.x >= ci.bounds.left && pt.x <= ci.bounds.right &&
                    pt.y >= ci.bounds.top && pt.y <= ci.bounds.bottom) {
                    m_selectedCommitIdx = static_cast<int>(i);
                    m_hoveredCommitIdx = static_cast<int>(i);
                    m_commitCardHash = ci.hash;
                    if (ci.statSummary.empty()) {
                        LoadCommitFiles(ci);
                    }
                    ci.isExpanded = !ci.isExpanded;
                    if (ci.isExpanded) {
                        m_expandedCommitHashes.insert(ci.hash);
                        if (!ci.filesLoaded) {
                            LoadCommitFiles(ci);
                        }
                    } else {
                        m_expandedCommitHashes.erase(ci.hash);
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

bool DrawerPanel::HandleGitMouseMove(POINT pt) {
    std::wstring oldTip = m_gitTooltipText;
    int oldHoverChange = m_hoveredGitChangeIdx;
    m_hoveredGitChangeIdx = -1;
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
    else if (pt.x >= m_gitCommitBtnBounds.left && pt.x <= m_gitCommitBtnBounds.right &&
             pt.y >= m_gitCommitBtnBounds.top && pt.y <= m_gitCommitBtnBounds.bottom) {
        if (!m_gitChanges.empty()) {
            m_gitTooltipText = L"Commit Changes (Ctrl+Enter)";
        } else if (m_unpushedCommitsCount > 0) {
            m_gitTooltipText = L"Sync Changes (" + std::to_wstring(m_unpushedCommitsCount) + L") with Remote / GitHub";
        } else {
            m_gitTooltipText = L"No changes to commit";
        }
        m_gitTooltipTarget = m_gitCommitBtnBounds;
        m_showGitTooltip = true;
    }
    else if (checkBtn(m_gitGraphAutoBounds, L"Toggle Auto Fetch / Refresh")) {}
    else if (checkBtn(m_gitGraphBullseyeBounds, L"Scroll to Current HEAD Commit (◎)")) {}
    else if (checkBtn(m_gitGraphSplitBounds, L"View Branch Divergence (⑂)")) {}
    else if (checkBtn(m_gitGraphMergeBounds, L"View Branch Merges (⑃)")) {}
    else if (checkBtn(m_gitGraphPushBounds, L"Push Commits to Remote (↑)")) {}
    else if (checkBtn(m_gitGraphRefreshBounds, L"Refresh Git History & Status (🔄)")) {}
    else if (checkBtn(m_gitGraphMoreBounds, L"More Graph Actions...")) {}

    // Check changed file rows (Image 4 & 5)
    if (m_changesExpanded) {
        for (size_t i = 0; i < m_gitChanges.size(); ++i) {
            const auto& item = m_gitChanges[i];
            if (pt.x >= item.bounds.left && pt.x <= item.bounds.right &&
                pt.y >= item.bounds.top && pt.y <= item.bounds.bottom) {
                m_hoveredGitChangeIdx = static_cast<int>(i);

                if (checkBtn(item.discardBtnBounds, L"Discard Changes (↩)")) {
                    break;
                }
                if (checkBtn(item.stageBtnBounds, L"Stage Changes (+)")) {
                    break;
                }
                std::wstring desc = (item.status == L"U") ? L"Untracked" :
                                    (item.status == L"D") ? L"Deleted" :
                                    (item.status == L"R") ? L"Renamed" :
                                    (item.status == L"A") ? L"Added" : L"Modified";
                m_gitTooltipText = item.path + L" • " + desc;
                m_gitTooltipTarget = item.bounds;
                m_showGitTooltip = true;
                break;
            }
        }
    }

    // Check commit rows and expanded files (Images 2, 3, 5)
    int oldHoverCommit = m_hoveredCommitIdx;
    if (m_graphExpanded && !m_showGitTooltip) {
        bool inCard = (m_commitCardBounds.right > m_commitCardBounds.left &&
                       pt.x >= m_commitCardBounds.left && pt.x <= m_commitCardBounds.right &&
                       pt.y >= m_commitCardBounds.top && pt.y <= m_commitCardBounds.bottom);
        // Bridge between drawer right edge and floating card
        bool inBridge = (m_commitCardBounds.right > m_commitCardBounds.left &&
                         pt.x >= m_commitCardBounds.left - 24.0f && pt.x <= m_commitCardBounds.right &&
                         pt.y >= m_commitCardBounds.top - 8.0f && pt.y <= m_commitCardBounds.bottom + 8.0f);
        int hitCommitIdx = -1;
        for (size_t i = 0; i < m_gitCommits.size(); ++i) {
            const auto& ci = m_gitCommits[i];
            if (pt.x >= ci.bounds.left && pt.x <= ci.bounds.right &&
                pt.y >= ci.bounds.top && pt.y <= ci.bounds.bottom) {
                hitCommitIdx = static_cast<int>(i);
                break;
            }
        }

        if (hitCommitIdx >= 0) {
            m_hoveredCommitIdx = hitCommitIdx;
            m_commitCardHash = m_gitCommits[hitCommitIdx].hash;
        } else if (inCard || inBridge) {
            // Keep card alive while hovering inside the card bounds or transitioning from drawer
        } else if (m_selectedCommitIdx >= 0) {
            // Keep card alive when a commit row was explicitly clicked/selected
            m_hoveredCommitIdx = -1;
        } else {
            // Mouse is outside both commit rows and card: dismiss card
            m_hoveredCommitIdx = -1;
            m_commitCardBounds = D2D1::RectF(0, 0, 0, 0);
        }

        for (const auto& ci : m_gitCommits) {
            if (ci.isExpanded) {
                for (const auto& cf : ci.files) {
                    if (pt.x >= cf.bounds.left && pt.x <= cf.bounds.right &&
                        pt.y >= cf.bounds.top && pt.y <= cf.bounds.bottom) {
                        m_gitTooltipText = cf.path + L" [" + cf.status + L"]\nClick to view diff";
                        m_gitTooltipTarget = cf.bounds;
                        m_showGitTooltip = true;
                        break;
                    }
                }
                if (m_showGitTooltip) break;
            }
        }
    }

    return (m_gitTooltipText != oldTip || m_hoveredGitChangeIdx != oldHoverChange || m_hoveredCommitIdx != oldHoverCommit);
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
            std::wstring trimmed = m_commitMessage;
            size_t f = trimmed.find_first_not_of(L" \t\r\n");
            if (f != std::wstring::npos) {
                CommitChangesAsync(m_commitMessage);
            } else {
                m_gitTooltipText = L"Please provide a commit message before committing.";
                m_gitTooltipTarget = m_gitMsgBoxBounds;
                m_showGitTooltip = true;
            }
        }
    } else if (wParam == VK_ESCAPE) {
        m_focusedGitInput = 0;
    }
}

void DrawerPanel::RenderSourceControlPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight, float currentDrawerW) {
    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float railW   = config.ScaleF(static_cast<float>(config.railWidth));
    const float paddingX = config.ScaleF(14.0f);

    // 1. Pane Title: "Source Control"
    D2D1_RECT_F rcTitle = D2D1::RectF(
        railW + paddingX,
        headerH + config.ScaleF(10.0f),
        railW + currentDrawerW - config.ScaleF(40.0f),
        headerH + config.ScaleF(30.0f)
    );
    const wchar_t* titleStr = L"Source Control";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                  rcTitle, theme.brushTextPrimary.Get());

    // 2. Top-Right "..." Button (Header Views & More Actions)
    m_gitHeaderMoreBounds = D2D1::RectF(
        railW + currentDrawerW - config.ScaleF(30.0f),
        headerH + config.ScaleF(10.0f),
        railW + currentDrawerW - config.ScaleF(10.0f),
        headerH + config.ScaleF(30.0f)
    );
    float dotY = (m_gitHeaderMoreBounds.top + m_gitHeaderMoreBounds.bottom) / 2.0f;
    float dotMidX = (m_gitHeaderMoreBounds.left + m_gitHeaderMoreBounds.right) / 2.0f;
    float dotOffset = config.ScaleF(4.5f);
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
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontMenuShortcut.Get(),
                      rcNoGit, theme.brushTextMuted.Get());
        return;
    }

    float secH = config.ScaleF(24.0f);

    // Clamp vertical splitter between Changes and Graph with persistence (Complaint 7)
    float minSplitY = curY + config.ScaleF(110.0f);
    float maxSplitY = clientHeight - config.ScaleF(70.0f);
    if (m_gitSplitY <= 0.0f) {
        m_gitSplitY = curY + (clientHeight - curY) * 0.46f;
    } else {
        if (m_gitSplitY < minSplitY) m_gitSplitY = minSplitY;
        if (m_gitSplitY > maxSplitY) m_gitSplitY = maxSplitY;
    }

    // =========================================================================
    // 3. Section 1: "Changes" (collapsible)
    // =========================================================================
    if (m_showChangesSection) {
        m_gitChangesHeaderBounds = D2D1::RectF(railW, curY, railW + currentDrawerW, curY + secH);

        // Chevron (v or >)
        float chevX = railW + config.ScaleF(14.0f);
        float chevY = curY + secH / 2.0f;
        m_gitChangesChevronBounds = D2D1::RectF(chevX - 6.0f, chevY - 6.0f, chevX + 6.0f, chevY + 6.0f);

        if (m_changesExpanded) {
            pRT->DrawLine(D2D1::Point2F(chevX - 3.5f, chevY - 2.0f), D2D1::Point2F(chevX, chevY + 2.0f), theme.brushTextPrimary.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(chevX, chevY + 2.0f), D2D1::Point2F(chevX + 3.5f, chevY - 2.0f), theme.brushTextPrimary.Get(), 1.4f);
        } else {
            pRT->DrawLine(D2D1::Point2F(chevX - 2.0f, chevY - 3.5f), D2D1::Point2F(chevX + 2.0f, chevY), theme.brushTextPrimary.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(chevX + 2.0f, chevY), D2D1::Point2F(chevX - 2.0f, chevY + 3.5f), theme.brushTextPrimary.Get(), 1.4f);
        }

        // Title: Changes (LEFT-ALIGNED right next to chevron, NOT on the right!)
        D2D1_RECT_F rcChangesText = D2D1::RectF(chevX + config.ScaleF(12.0f), curY, chevX + config.ScaleF(70.0f), curY + secH);
        pRT->DrawText(L"Changes", 7,
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcChangesText, theme.brushTextPrimary.Get());

        // Header Action Buttons on right: ✓ (Stage All), 🔄 (Discard All), ... (More)
        float btnW = config.ScaleF(18.0f);
        float btnGap = config.ScaleF(4.0f);
        float rightX = railW + currentDrawerW - config.ScaleF(10.0f);

        m_gitChangesMoreBounds = D2D1::RectF(rightX - btnW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (btnW + btnGap);

        m_gitDiscardAllBounds = D2D1::RectF(rightX - btnW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (btnW + btnGap);

        m_gitStageAllBounds = D2D1::RectF(rightX - btnW, curY + 2.0f, rightX, curY + secH - 2.0f);
        rightX -= (btnW + btnGap);

        // Circular count pill badge [ N ] (matching VS Code Image 4)
        if (!m_gitChanges.empty()) {
            float badgeW = config.ScaleF(18.0f);
            float badgeH = config.ScaleF(16.0f);
            D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(
                D2D1::RectF(rightX - badgeW, curY + (secH - badgeH) / 2.0f, rightX, curY + (secH + badgeH) / 2.0f),
                8.0f, 8.0f
            );
            pRT->FillRoundedRectangle(rrcBadge, theme.brushItemHover.Get());
            std::wstring cntStr = std::to_wstring(m_gitChanges.size());
            pRT->DrawText(cntStr.c_str(), static_cast<UINT32>(cntStr.length()),
                          theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuShortcut.Get(),
                          rrcBadge.rect, theme.brushTextPrimary.Get());
        }

        // Draw ✓ icon on m_gitStageAllBounds
        float ckX = (m_gitStageAllBounds.left + m_gitStageAllBounds.right) / 2.0f;
        float ckY = (m_gitStageAllBounds.top + m_gitStageAllBounds.bottom) / 2.0f;
        pRT->DrawLine(D2D1::Point2F(ckX - 3.5f, ckY), D2D1::Point2F(ckX - 1.0f, ckY + 3.0f), theme.brushTextMuted.Get(), 1.4f);
        pRT->DrawLine(D2D1::Point2F(ckX - 1.0f, ckY + 3.0f), D2D1::Point2F(ckX + 4.0f, ckY - 3.0f), theme.brushTextMuted.Get(), 1.4f);

        // Draw 🔄 circular arrow on m_gitDiscardAllBounds
        float rcX = (m_gitDiscardAllBounds.left + m_gitDiscardAllBounds.right) / 2.0f;
        float rcY = (m_gitDiscardAllBounds.top + m_gitDiscardAllBounds.bottom) / 2.0f;
        D2D1_ELLIPSE elDisc = D2D1::Ellipse(D2D1::Point2F(rcX, rcY), 4.0f, 4.0f);
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

            // AI Commit Message Sparkle Button (compact square with ✦ sparkle icon, NO "Generate" text!)
            float btnSize = config.ScaleF(22.0f);
            m_gitGenerateAiBounds = D2D1::RectF(
                m_gitMsgBoxBounds.right - btnSize - config.ScaleF(3.0f),
                m_gitMsgBoxBounds.top + (msgH - btnSize) / 2.0f,
                m_gitMsgBoxBounds.right - config.ScaleF(3.0f),
                m_gitMsgBoxBounds.top + (msgH - btnSize) / 2.0f + btnSize
            );
            D2D1_ROUNDED_RECT rrcGen = D2D1::RoundedRect(m_gitGenerateAiBounds, 3.0f, 3.0f);
            pRT->FillRoundedRectangle(rrcGen, theme.brushBrandAccent.Get());

            // Vector 4-pointed Sparkle (✦) icon in crisp white
            float scx = (m_gitGenerateAiBounds.left + m_gitGenerateAiBounds.right) / 2.0f;
            float scy = (m_gitGenerateAiBounds.top + m_gitGenerateAiBounds.bottom) / 2.0f;
            float sr = config.ScaleF(5.0f);
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pSparkle;
            Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
            pRT->GetFactory(&pFactory);
            if (pFactory && SUCCEEDED(pFactory->CreatePathGeometry(&pSparkle))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pSparkle->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(scx, scy - sr), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(scx + sr * 0.28f, scy - sr * 0.28f));
                    sink->AddLine(D2D1::Point2F(scx + sr, scy));
                    sink->AddLine(D2D1::Point2F(scx + sr * 0.28f, scy + sr * 0.28f));
                    sink->AddLine(D2D1::Point2F(scx, scy + sr));
                    sink->AddLine(D2D1::Point2F(scx - sr * 0.28f, scy + sr * 0.28f));
                    sink->AddLine(D2D1::Point2F(scx - sr, scy));
                    sink->AddLine(D2D1::Point2F(scx - sr * 0.28f, scy - sr * 0.28f));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    pRT->FillGeometry(pSparkle.Get(), theme.brushTextWhite.Get());
                }
            }

            // Message text or placeholder (LEFT-ALIGNED)
            D2D1_RECT_F rcMsgText = D2D1::RectF(
                m_gitMsgBoxBounds.left + config.ScaleF(8.0f),
                m_gitMsgBoxBounds.top,
                m_gitGenerateAiBounds.left - config.ScaleF(6.0f),
                m_gitMsgBoxBounds.bottom
            );
            float msgTextWidth = 0.0f;
            if (m_commitMessage.empty()) {
                const wchar_t* phStr = L"Message (Ctrl+Enter to commit)";
                pRT->DrawText(phStr, static_cast<UINT32>(wcslen(phStr)),
                              theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                              rcMsgText, theme.brushTextMuted.Get());
            } else {
                pRT->DrawText(m_commitMessage.c_str(), static_cast<UINT32>(m_commitMessage.length()),
                              theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                              rcMsgText, theme.brushTextPrimary.Get());

                if (theme.dwriteFactory) {
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                    if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                            m_commitMessage.c_str(), static_cast<UINT32>(m_commitMessage.length()),
                            theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                            1000.0f, msgH, &pLayout))) {
                        DWRITE_TEXT_METRICS tm{};
                        if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                            msgTextWidth = tm.width;
                        }
                    }
                }
            }

            // Blinking Caret '|' in Commit Message Box (Image 1)
            if (m_focusedGitInput == 1 && m_gitCaretVisible) {
                float caretX = rcMsgText.left + msgTextWidth + 1.0f;
                if (caretX < rcMsgText.right) {
                    pRT->DrawLine(
                        D2D1::Point2F(caretX, m_gitMsgBoxBounds.top + config.ScaleF(5.0f)),
                        D2D1::Point2F(caretX, m_gitMsgBoxBounds.bottom - config.ScaleF(5.0f)),
                        theme.brushTextPrimary.Get(), 1.5f
                    );
                }
            }

            curY += msgH + config.ScaleF(6.0f);

            // Primary Commit / Sync Changes Button (Images 1 & 5)
            float commitH = config.ScaleF(28.0f);
            m_gitCommitBtnBounds = D2D1::RectF(
                railW + paddingX,
                curY,
                railW + currentDrawerW - paddingX,
                curY + commitH
            );
            D2D1_ROUNDED_RECT rrcCommit = D2D1::RoundedRect(m_gitCommitBtnBounds, 4.0f, 4.0f);

            bool hasChanges = !m_gitChanges.empty();
            bool hasUnpushed = (m_unpushedCommitsCount > 0);

            if (hasChanges) {
                // Active Commit Button (Blue)
                pRT->FillRoundedRectangle(rrcCommit, theme.brushBrandAccent.Get());

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
            } else if (hasUnpushed) {
                // Active Sync Changes Button (Blue, with unpushed count matching Image 5)
                pRT->FillRoundedRectangle(rrcCommit, theme.brushBrandAccent.Get());

                D2D1_RECT_F rcSyncLabel = D2D1::RectF(
                    m_gitCommitBtnBounds.left,
                    curY,
                    m_gitCommitBtnBounds.right - config.ScaleF(26.0f),
                    curY + commitH
                );
                std::wstring syncText = L"🔄 Sync Changes " + std::to_wstring(m_unpushedCommitsCount);
                pRT->DrawText(syncText.c_str(), static_cast<UINT32>(syncText.length()),
                              theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                              rcSyncLabel, theme.brushTextWhite.Get());

                // Vertical divider and chevron dropdown
                float divX = m_gitCommitBtnBounds.right - config.ScaleF(26.0f);
                pRT->DrawLine(D2D1::Point2F(divX, curY + 4.0f), D2D1::Point2F(divX, curY + commitH - 4.0f),
                              theme.brushBorderSubtle.Get(), 1.0f);
                float dchX = (divX + m_gitCommitBtnBounds.right) / 2.0f;
                float dchY = curY + commitH / 2.0f;
                pRT->DrawLine(D2D1::Point2F(dchX - 3.0f, dchY - 1.5f), D2D1::Point2F(dchX, dchY + 2.0f), theme.brushTextWhite.Get(), 1.4f);
                pRT->DrawLine(D2D1::Point2F(dchX, dchY + 2.0f), D2D1::Point2F(dchX + 3.0f, dchY - 1.5f), theme.brushTextWhite.Get(), 1.4f);
            } else {
                // Disabled Muted Commit Button (0 changes and 0 unpushed commits)
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDisabled;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.22f, 0.22f, 0.6f), &brushDisabled);
                pRT->FillRoundedRectangle(rrcCommit, brushDisabled.Get() ? brushDisabled.Get() : theme.brushBorderSubtle.Get());

                D2D1_RECT_F rcCommitLabel = D2D1::RectF(
                    m_gitCommitBtnBounds.left,
                    curY,
                    m_gitCommitBtnBounds.right - config.ScaleF(26.0f),
                    curY + commitH
                );
                const wchar_t* commitText = L"✓ Commit";
                pRT->DrawText(commitText, static_cast<UINT32>(wcslen(commitText)),
                              theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                              rcCommitLabel, theme.brushTextMuted.Get());

                float divX = m_gitCommitBtnBounds.right - config.ScaleF(26.0f);
                pRT->DrawLine(D2D1::Point2F(divX, curY + 4.0f), D2D1::Point2F(divX, curY + commitH - 4.0f),
                              theme.brushBorderSubtle.Get(), 1.0f);
                float dchX = (divX + m_gitCommitBtnBounds.right) / 2.0f;
                float dchY = curY + commitH / 2.0f;
                pRT->DrawLine(D2D1::Point2F(dchX - 3.0f, dchY - 1.5f), D2D1::Point2F(dchX, dchY + 2.0f), theme.brushTextMuted.Get(), 1.4f);
                pRT->DrawLine(D2D1::Point2F(dchX, dchY + 2.0f), D2D1::Point2F(dchX + 3.0f, dchY - 1.5f), theme.brushTextMuted.Get(), 1.4f);
            }

            curY += commitH + config.ScaleF(8.0f);

            // Changed Files Scrollable List (Bounded above draggable splitter)
            float fileListTopY = curY;
            float fileListBottomY = m_showGraphSection ? (m_gitSplitY - config.ScaleF(4.0f)) : clientHeight;

            if (fileListBottomY > fileListTopY) {
                D2D1_RECT_F rcChangesClip = D2D1::RectF(railW, fileListTopY, railW + currentDrawerW, fileListBottomY);
                pRT->PushAxisAlignedClip(rcChangesClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                if (m_gitChanges.empty()) {
                    D2D1_RECT_F rcNoCh = D2D1::RectF(railW + paddingX, fileListTopY, railW + currentDrawerW - paddingX, fileListTopY + 20.0f);
                    const wchar_t* noChStr = L"No changes detected";
                    pRT->DrawText(noChStr, static_cast<UINT32>(wcslen(noChStr)),
                                  theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontMenuShortcut.Get(),
                                  rcNoCh, theme.brushTextMuted.Get());
                } else {
                    float rowH = config.ScaleF(24.0f);
                    float itemY = fileListTopY;
                    size_t startCh = (std::min)(m_gitChangesScrollIdx, m_gitChanges.empty() ? size_t(0) : m_gitChanges.size() - 1);
                    for (size_t i = startCh; i < m_gitChanges.size() && itemY < fileListBottomY; ++i) {
                        auto& item = m_gitChanges[i];
                        item.bounds = D2D1::RectF(railW, itemY, railW + currentDrawerW, itemY + rowH);

                        bool isRowHovered = (static_cast<int>(i) == m_hoveredGitChangeIdx);

                        // Row hover background
                        if (isRowHovered) {
                            pRT->FillRectangle(item.bounds, theme.brushItemHover.Get());
                        }

                        // 1. Authentic Seti UI File Icon with brand color (Image 4 & 5)
                        float iconX = railW + config.ScaleF(14.0f);
                        D2D1_RECT_F rcIcon = D2D1::RectF(
                            iconX,
                            itemY + config.ScaleF(4.0f),
                            iconX + config.ScaleF(16.0f),
                            itemY + rowH - config.ScaleF(4.0f)
                        );
                        auto setiInfo = theme.GetSetiIcon(item.fileName, false, false);
                        if (theme.fontSeti && setiInfo.pBrush) {
                            pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), rcIcon, setiInfo.pBrush);
                        } else {
                            auto iconType = LucideIcons::GetIconTypeForPath(item.fileName, false, false);
                            m_icons.DrawFileIcon(pRT, theme, rcIcon, iconType);
                        }

                        // 2. File Name (LEFT-ALIGNED)
                        float nameX = iconX + config.ScaleF(20.0f);
                        D2D1_RECT_F rcName = D2D1::RectF(
                            nameX,
                            itemY,
                            railW + currentDrawerW - config.ScaleF(50.0f),
                            itemY + rowH
                        );
                        pRT->DrawText(item.fileName.c_str(), static_cast<UINT32>(item.fileName.length()),
                                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                                      rcName, theme.brushTextPrimary.Get());

                        bool isBin = false;
                        auto dot = item.path.rfind(L'.');
                        if (dot != std::wstring::npos) {
                            std::wstring ext = item.path.substr(dot);
                            for (auto& c : ext) c = towlower(c);
                            if (ext == L".exe" || ext == L".dll" || ext == L".bin" || ext == L".obj" || ext == L".lib" || ext == L".node") isBin = true;
                        }

                        // 3. Status letter (M = yellow #E5C07B, D = red #E06C75, U/A = green #98C379, R = blue #61AFEF, BIN = purple)
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushStatus;
                        if (isBin) {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.45f, 0.95f, 1.0f), &brushStatus);
                        } else if (item.status.find(L"M") != std::wstring::npos) {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.898f, 0.753f, 0.482f, 1.0f), &brushStatus);
                        } else if (item.status.find(L"D") != std::wstring::npos) {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushStatus);
                        } else if (item.status.find(L"R") != std::wstring::npos) {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.380f, 0.686f, 0.937f, 1.0f), &brushStatus);
                        } else {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), &brushStatus);
                        }

                        D2D1_RECT_F rcStatus = D2D1::RectF(
                            railW + currentDrawerW - config.ScaleF(24.0f),
                            itemY,
                            railW + currentDrawerW - config.ScaleF(8.0f),
                            itemY + rowH
                        );
                        const wchar_t* statChar = isBin ? L"B" : (item.status.empty() ? L"M" : item.status.c_str());

                        // 4. Stage (+) and Discard (↩) buttons on row hover (Image 4 & 5)
                        item.discardBtnBounds = D2D1::RectF(railW + currentDrawerW - config.ScaleF(26.0f), itemY + 2.0f, railW + currentDrawerW - config.ScaleF(10.0f), itemY + rowH - 2.0f);
                        item.stageBtnBounds = D2D1::RectF(railW + currentDrawerW - config.ScaleF(46.0f), itemY + 2.0f, railW + currentDrawerW - config.ScaleF(30.0f), itemY + rowH - 2.0f);

                        if (isRowHovered) {
                            // Stage (+) button
                            float sx = (item.stageBtnBounds.left + item.stageBtnBounds.right) / 2.0f;
                            float sy = (item.stageBtnBounds.top + item.stageBtnBounds.bottom) / 2.0f;
                            pRT->DrawLine(D2D1::Point2F(sx - 3.5f, sy), D2D1::Point2F(sx + 3.5f, sy), theme.brushTextPrimary.Get(), 1.3f);
                            pRT->DrawLine(D2D1::Point2F(sx, sy - 3.5f), D2D1::Point2F(sx, sy + 3.5f), theme.brushTextPrimary.Get(), 1.3f);

                            // Discard (↩) button (curved undo arrow)
                            float dx = (item.discardBtnBounds.left + item.discardBtnBounds.right) / 2.0f;
                            float dy = (item.discardBtnBounds.top + item.discardBtnBounds.bottom) / 2.0f;
                            pRT->DrawLine(D2D1::Point2F(dx - 1.5f, dy + 2.0f), D2D1::Point2F(dx + 3.0f, dy + 2.0f), theme.brushTextPrimary.Get(), 1.3f);
                            pRT->DrawLine(D2D1::Point2F(dx + 3.0f, dy + 2.0f), D2D1::Point2F(dx + 3.0f, dy - 2.0f), theme.brushTextPrimary.Get(), 1.3f);
                            pRT->DrawLine(D2D1::Point2F(dx + 3.0f, dy - 2.0f), D2D1::Point2F(dx - 3.0f, dy - 2.0f), theme.brushTextPrimary.Get(), 1.3f);
                            pRT->DrawLine(D2D1::Point2F(dx - 3.0f, dy - 2.0f), D2D1::Point2F(dx - 0.5f, dy - 4.5f), theme.brushTextPrimary.Get(), 1.3f);
                            pRT->DrawLine(D2D1::Point2F(dx - 3.0f, dy - 2.0f), D2D1::Point2F(dx - 0.5f, dy + 0.5f), theme.brushTextPrimary.Get(), 1.3f);
                        } else {
                            pRT->DrawText(statChar, 1,
                                          theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                                          rcStatus, brushStatus.Get());
                        }

                        itemY += rowH;
                    }
                }

                pRT->PopAxisAlignedClip();
            }
        }
    }

    // =========================================================================
    // 4. Draggable Splitter & Section 2: "Graph" (Images 2 & 3)
    // =========================================================================
    if (m_showGraphSection) {
        if (m_showChangesSection) {
            curY = m_gitSplitY;

            // Draw horizontal splitter divider line
            pRT->DrawLine(
                D2D1::Point2F(railW, m_gitSplitY),
                D2D1::Point2F(railW + currentDrawerW, m_gitSplitY),
                theme.brushBorderSubtle.Get(), 1.0f
            );
            m_gitSplitterBounds = D2D1::RectF(railW, m_gitSplitY - 4.0f, railW + currentDrawerW, m_gitSplitY + 4.0f);
        }

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

        // Title: Graph (LEFT-ALIGNED right next to chevron)
        D2D1_RECT_F rcGraphTitle = D2D1::RectF(chevX + config.ScaleF(12.0f), curY, chevX + config.ScaleF(60.0f), curY + secH);
        const wchar_t* graphTitleStr = L"Graph";
        pRT->DrawText(graphTitleStr, static_cast<UINT32>(wcslen(graphTitleStr)),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcGraphTitle, theme.brushTextPrimary.Get());

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
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(grx, gry), 4.0f, 4.0f), theme.brushTextMuted.Get(), 1.3f);
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
        pRT->DrawText(autoStr, 4,
                      theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                      m_gitGraphAutoBounds,
                      m_gitAutoRefresh ? theme.brushBrandAccent.Get() : theme.brushTextMuted.Get());

        curY += secH + config.ScaleF(4.0f);

        if (m_graphExpanded) {
            // Clip to Graph scrollable area (cleanly bounded, never spills over)
            D2D1_RECT_F rcGraphClip = D2D1::RectF(railW, curY, railW + currentDrawerW, clientHeight);
            pRT->PushAxisAlignedClip(rcGraphClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            float rowH = config.ScaleF(26.0f);
            float timelineX = railW + config.ScaleF(24.0f);

            // VS Code Git Graph multi-lane color palette (Image 2)
            static const D2D1_COLOR_F s_gitLaneColors[] = {
                D2D1::ColorF(0.23f, 0.51f, 0.96f), // 0: Blue #3B82F6
                D2D1::ColorF(0.93f, 0.28f, 0.60f), // 1: Pink #EC4899
                D2D1::ColorF(0.58f, 0.64f, 0.72f), // 2: Grey #94A3B8
                D2D1::ColorF(0.13f, 0.77f, 0.37f), // 3: Green #22C55E
                D2D1::ColorF(0.92f, 0.70f, 0.03f), // 4: Amber/Gold #EAB308
                D2D1::ColorF(0.08f, 0.72f, 0.65f), // 5: Teal #14B8A6
                D2D1::ColorF(0.66f, 0.33f, 0.97f), // 6: Purple #A855F7
                D2D1::ColorF(0.98f, 0.45f, 0.09f), // 7: Orange #F97316
            };

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> laneBrushes[8];
            for (int k = 0; k < 8; ++k) {
                pRT->CreateSolidColorBrush(s_gitLaneColors[k], &laneBrushes[k]);
            }

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBranchBadge;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.122f, 0.231f, 0.373f, 0.85f), &brushBranchBadge);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCloud;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f), &brushCloud);

            size_t startIdx = (std::min)(m_gitGraphScrollIdx, m_gitCommits.empty() ? size_t(0) : m_gitCommits.size() - 1);

            int maxLaneVisible = 0;
            for (size_t k = startIdx; k < m_gitCommits.size(); ++k) {
                maxLaneVisible = (std::max)(maxLaneVisible, m_gitCommits[k].lane);
                for (int pl : m_gitCommits[k].passThroughLanes) {
                    maxLaneVisible = (std::max)(maxLaneVisible, pl);
                }
            }
            const float laneSpacing = config.ScaleF(12.0f);
            const float originTimelineX = railW + config.ScaleF(16.0f);

            for (size_t i = startIdx; i < m_gitCommits.size() && curY < clientHeight; ++i) {
                auto& ci = m_gitCommits[i];
                ci.bounds = D2D1::RectF(railW, curY, railW + currentDrawerW, curY + rowH);
                bool isHovered = (static_cast<int>(i) == m_hoveredCommitIdx);
                bool isSelected = (static_cast<int>(i) == m_selectedCommitIdx);
                if (isHovered || isSelected) {
                    pRT->FillRectangle(ci.bounds, isSelected ? theme.brushTreeSelected.Get() : theme.brushItemHover.Get());
                }

                float nodeY = curY + rowH / 2.0f;
                float nodeX = originTimelineX + ci.lane * laneSpacing;
                ID2D1SolidColorBrush* pCommitBrush = laneBrushes[ci.colorIdx % 8].Get();

                // 1. Pass-through vertical lines for other active lanes
                for (int passLane : ci.passThroughLanes) {
                    float plx = originTimelineX + passLane * laneSpacing;
                    pRT->DrawLine(
                        D2D1::Point2F(plx, curY),
                        D2D1::Point2F(plx, curY + rowH),
                        laneBrushes[passLane % 8].Get(), 1.6f
                    );
                }

                // 2. Incoming vertical line from parent commit above
                if (i > 0) {
                    pRT->DrawLine(
                        D2D1::Point2F(nodeX, curY),
                        D2D1::Point2F(nodeX, nodeY),
                        pCommitBrush, 1.6f
                    );
                }

                // 3. Outgoing connector lines to parent commits below
                for (int pLane : ci.parentLanes) {
                    float x1 = nodeX;
                    float x2 = originTimelineX + pLane * laneSpacing;
                    float y1 = nodeY;
                    float y2 = curY + rowH;
                    if (std::abs(x1 - x2) < 0.5f) {
                        pRT->DrawLine(
                            D2D1::Point2F(x1, y1),
                            D2D1::Point2F(x2, y2),
                            pCommitBrush, 1.6f
                        );
                    } else {
                        // Smooth Bézier S-curve matching VS Code Git Graph (Image 2)
                        Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
                        pRT->GetFactory(&pFactory);
                        if (pFactory) {
                            Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
                            if (SUCCEEDED(pFactory->CreatePathGeometry(&path)) && path) {
                                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                                if (SUCCEEDED(path->Open(&sink)) && sink) {
                                    sink->BeginFigure(D2D1::Point2F(x1, y1), D2D1_FIGURE_BEGIN_HOLLOW);
                                    float midY = (y1 + y2) * 0.5f;
                                    sink->AddBezier(D2D1::BezierSegment(
                                        D2D1::Point2F(x1, midY),
                                        D2D1::Point2F(x2, midY),
                                        D2D1::Point2F(x2, y2)
                                    ));
                                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                                    sink->Close();
                                    pRT->DrawGeometry(path.Get(), laneBrushes[pLane % 8].Get(), 1.6f);
                                }
                            }
                        }
                    }
                }

                // Chevron expander for commit details
                float chX = railW + config.ScaleF(8.0f);
                ci.chevronBounds = D2D1::RectF(chX - 4.0f, nodeY - 6.0f, chX + 4.0f, nodeY + 6.0f);
                if (ci.isExpanded) {
                    pRT->DrawLine(D2D1::Point2F(chX - 2.5f, nodeY - 1.5f), D2D1::Point2F(chX, nodeY + 1.5f), theme.brushTextMuted.Get(), 1.2f);
                    pRT->DrawLine(D2D1::Point2F(chX, nodeY + 1.5f), D2D1::Point2F(chX + 2.5f, nodeY - 1.5f), theme.brushTextMuted.Get(), 1.2f);
                } else {
                    pRT->DrawLine(D2D1::Point2F(chX - 1.5f, nodeY - 2.5f), D2D1::Point2F(chX + 1.5f, nodeY), theme.brushTextMuted.Get(), 1.2f);
                    pRT->DrawLine(D2D1::Point2F(chX + 1.5f, nodeY), D2D1::Point2F(chX - 1.5f, nodeY + 2.5f), theme.brushTextMuted.Get(), 1.2f);
                }

                // 4. Commit circle node
                if (ci.isHead) {
                    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(nodeX, nodeY), 4.8f, 4.8f), pCommitBrush, 1.8f);
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nodeX, nodeY), 2.2f, 2.2f), pCommitBrush);
                } else {
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nodeX, nodeY), 3.2f, 3.2f), pCommitBrush);
                }

                float textLeft = originTimelineX + (maxLaneVisible + 1) * laneSpacing + config.ScaleF(8.0f);

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
                    pRT->DrawRoundedRectangle(rrcBadge, pCommitBrush, 1.0f);

                    // Badge text
                    pRT->DrawText(branchBadgeText.c_str(), static_cast<UINT32>(branchBadgeText.length()),
                                  theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                                  D2D1::RectF(textLeft + 3.0f, nodeY - bH / 2.0f, textLeft + bW - 14.0f, nodeY + bH / 2.0f),
                                  pCommitBrush);

                    // Cloud icon on right edge of pill
                    float cloudX = textLeft + bW - 8.0f;
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cloudX - 3.0f, nodeY + 1.0f), 2.2f, 2.2f), brushCloud.Get());
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cloudX, nodeY - 1.0f), 3.0f, 3.0f), brushCloud.Get());
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cloudX + 3.0f, nodeY + 1.0f), 2.2f, 2.2f), brushCloud.Get());

                    textLeft += bW + config.ScaleF(8.0f);
                }

                // Right-edge Open Changes icon button (Image 3)
                float btnSize = config.ScaleF(16.0f);
                float btnRight = railW + currentDrawerW - config.ScaleF(8.0f);
                float btnLeft = btnRight - btnSize;
                ci.openChangesBtnBounds = D2D1::RectF(btnLeft, curY + (rowH - btnSize) * 0.5f, btnRight, curY + (rowH + btnSize) * 0.5f);

                if (isHovered || isSelected) {
                    D2D1_ROUNDED_RECT rrcBtn = D2D1::RoundedRect(ci.openChangesBtnBounds, 3.0f, 3.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBtnBg;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.22f, 0.25f, 0.9f), &brushBtnBg);
                    pRT->FillRoundedRectangle(rrcBtn, brushBtnBg.Get());

                    // File diff outline icon
                    D2D1_RECT_F rcIcon = D2D1::RectF(btnLeft + 3.0f, curY + (rowH - btnSize) * 0.5f + 2.0f, btnRight - 3.0f, curY + (rowH + btnSize) * 0.5f - 2.0f);
                    pRT->DrawRectangle(rcIcon, theme.brushTextPrimary.Get(), 1.0f);
                    pRT->DrawLine(D2D1::Point2F(btnLeft + 5.0f, curY + rowH * 0.5f), D2D1::Point2F(btnRight - 5.0f, curY + rowH * 0.5f), theme.brushTextMuted.Get(), 1.0f);
                } else {
                    ci.openChangesBtnBounds = D2D1::RectF(0, 0, 0, 0);
                }

                // Commit message text with genuine ellipsis '...' before truncation (Image 3)
                float availW = (btnLeft - config.ScaleF(4.0f)) - textLeft;
                if (availW > config.ScaleF(20.0f)) {
                    IDWriteTextFormat* pFmt = theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get();
                    bool drawn = false;
                    if (theme.dwriteFactory && pFmt) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                        HRESULT hr = theme.dwriteFactory->CreateTextLayout(
                            ci.message.c_str(),
                            static_cast<UINT32>(ci.message.length()),
                            pFmt,
                            availW,
                            rowH,
                            &pLayout
                        );
                        if (SUCCEEDED(hr) && pLayout) {
                            DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                            Microsoft::WRL::ComPtr<IDWriteInlineObject> pEllipsis;
                            theme.dwriteFactory->CreateEllipsisTrimmingSign(pFmt, &pEllipsis);
                            pLayout->SetTrimming(&trimming, pEllipsis.Get());
                            pLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                            pRT->DrawTextLayout(D2D1::Point2F(textLeft, curY), pLayout.Get(), theme.brushTextPrimary.Get());
                            drawn = true;
                        }
                    }
                    if (!drawn) {
                        D2D1_RECT_F rcMsg = D2D1::RectF(textLeft, curY, textLeft + availW, curY + rowH);
                        pRT->DrawText(ci.message.c_str(), static_cast<UINT32>(ci.message.length()),
                                      pFmt, rcMsg, theme.brushTextPrimary.Get());
                    }
                }

                curY += rowH;

                // Render expanded commit changed files (Image 3)
                if (ci.isExpanded) {
                    float fileRowH = config.ScaleF(22.0f);
                    float fileIndent = timelineX + config.ScaleF(8.0f);

                    for (auto& cf : ci.files) {
                        if (curY + fileRowH > clientHeight) break;
                        cf.bounds = D2D1::RectF(railW, curY, railW + currentDrawerW, curY + fileRowH);

                        // File row vertical timeline connector
                        pRT->DrawLine(
                            D2D1::Point2F(timelineX, curY),
                            D2D1::Point2F(timelineX, curY + fileRowH),
                            pCommitBrush, 1.0f
                        );
                        // Small branch connecting timeline to file
                        pRT->DrawLine(
                            D2D1::Point2F(timelineX, curY + fileRowH / 2.0f),
                            D2D1::Point2F(fileIndent, curY + fileRowH / 2.0f),
                            pCommitBrush, 1.0f
                        );

                        // Seti UI File Icon
                        D2D1_RECT_F rcFileIcon = D2D1::RectF(
                            fileIndent + config.ScaleF(4.0f),
                            curY + config.ScaleF(3.0f),
                            fileIndent + config.ScaleF(18.0f),
                            curY + fileRowH - config.ScaleF(3.0f)
                        );
                        auto setiInfo = theme.GetSetiIcon(cf.fileName, false, false);
                        if (theme.fontSeti && setiInfo.pBrush) {
                            pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), rcFileIcon, setiInfo.pBrush);
                        } else {
                            auto iconType = LucideIcons::GetIconTypeForPath(cf.fileName, false, false);
                            m_icons.DrawFileIcon(pRT, theme, rcFileIcon, iconType);
                        }

                        // File Name
                        D2D1_RECT_F rcFileName = D2D1::RectF(
                            fileIndent + config.ScaleF(22.0f),
                            curY,
                            railW + currentDrawerW - config.ScaleF(26.0f),
                            curY + fileRowH
                        );
                        pRT->DrawText(cf.fileName.c_str(), static_cast<UINT32>(cf.fileName.length()),
                                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                                      rcFileName, theme.brushTextPrimary.Get());

                        // Status Badge (M, A, D, B)
                        bool isBin = false;
                        auto dot = cf.path.rfind(L'.');
                        if (dot != std::wstring::npos) {
                            std::wstring ext = cf.path.substr(dot);
                            for (auto& c : ext) c = towlower(c);
                            if (ext == L".exe" || ext == L".dll" || ext == L".bin" || ext == L".obj" || ext == L".lib" || ext == L".node") isBin = true;
                        }

                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushStatus;
                        if (isBin) {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.45f, 0.95f, 1.0f), &brushStatus);
                        } else if (cf.status.find(L"M") != std::wstring::npos) {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.898f, 0.753f, 0.482f, 1.0f), &brushStatus);
                        } else if (cf.status.find(L"D") != std::wstring::npos) {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushStatus);
                        } else {
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), &brushStatus);
                        }
                        D2D1_RECT_F rcStatus = D2D1::RectF(
                            railW + currentDrawerW - config.ScaleF(24.0f),
                            curY,
                            railW + currentDrawerW - config.ScaleF(8.0f),
                            curY + fileRowH
                        );
                        const wchar_t* sChar = isBin ? L"B" : (cf.status.empty() ? L"M" : cf.status.c_str());
                        pRT->DrawText(sChar, 1,
                                      theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                                      rcStatus, brushStatus.Get());

                        curY += fileRowH;
                    }
                }
            }

            pRT->PopAxisAlignedClip();
        }
    }

    // =========================================================================
    // 4.5 Floating Commit Details Card (Image 5)
    // =========================================================================
    int cardCommitIdx = (m_selectedCommitIdx >= 0) ? m_selectedCommitIdx : m_hoveredCommitIdx;
    if (m_showGraphSection && cardCommitIdx >= 0 && cardCommitIdx < static_cast<int>(m_gitCommits.size())) {
        auto& ci = m_gitCommits[cardCommitIdx];
        m_commitCardHash = ci.hash;

        // 4.5 Floating Commit Details Card OUTSIDE the sidebar drawer (Image 3)
        float cardW = config.ScaleF(430.0f);
        float cardH = config.ScaleF(142.0f);
        float cardLeft = railW + currentDrawerW + config.ScaleF(8.0f);
        float cardTop = (ci.bounds.top + ci.bounds.bottom) * 0.5f - cardH * 0.5f;
        float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
        float minTop = headerH + config.ScaleF(6.0f);
        float maxTop = clientHeight - cardH - config.ScaleF(6.0f);
        if (cardTop < minTop) cardTop = minTop;
        if (cardTop > maxTop) cardTop = (std::max)(minTop, maxTop);

        m_commitCardBounds = D2D1::RectF(cardLeft, cardTop, cardLeft + cardW, cardTop + cardH);

        // Card shadow & background
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_commitCardBounds, 6.0f, 6.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.98f), &brushCardBg);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBorder;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.28f, 0.28f, 0.32f, 1.0f), &brushCardBorder);

        pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());
        pRT->DrawRoundedRectangle(rrcCard, brushCardBorder.Get(), 1.0f);

        // Close '×' button on top-right of commit card (Issue 1)
        float closeBtnSize = config.ScaleF(18.0f);
        m_commitCardCloseBounds = D2D1::RectF(
            cardLeft + cardW - closeBtnSize - config.ScaleF(8.0f),
            cardTop + config.ScaleF(8.0f),
            cardLeft + cardW - config.ScaleF(8.0f),
            cardTop + config.ScaleF(8.0f) + closeBtnSize
        );
        float cx = (m_commitCardCloseBounds.left + m_commitCardCloseBounds.right) * 0.5f;
        float cy = (m_commitCardCloseBounds.top + m_commitCardCloseBounds.bottom) * 0.5f;
        float cr = config.ScaleF(3.5f);
        pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr), D2D1::Point2F(cx + cr, cy + cr), theme.brushTextMuted.Get(), 1.4f);
        pRT->DrawLine(D2D1::Point2F(cx + cr, cy - cr), D2D1::Point2F(cx - cr, cy + cr), theme.brushTextMuted.Get(), 1.4f);

        // Header: Author Avatar circle + Author Name (bold) + relative time & date (Image 3)
        float curCardY = cardTop + config.ScaleF(10.0f);
        float avatarR = config.ScaleF(7.5f);
        float avatarX = cardLeft + config.ScaleF(16.0f);
        float avatarY = curCardY + avatarR;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushAvatar;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.055f, 0.388f, 0.612f, 1.0f), &brushAvatar);
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(avatarX, avatarY), avatarR, avatarR), brushAvatar.Get());

        wchar_t initialChar[2] = { L'C', L'\0' };
        if (!ci.author.empty()) initialChar[0] = static_cast<wchar_t>(towupper(ci.author[0]));
        D2D1_RECT_F rcInitial = D2D1::RectF(avatarX - avatarR, avatarY - avatarR, avatarX + avatarR, avatarY + avatarR);
        pRT->DrawText(initialChar, 1, theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(), rcInitial, theme.brushTextWhite.Get());

        IDWriteTextFormat* pCardFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();

        float textStartX = avatarX + avatarR + config.ScaleF(8.0f);
        std::wstring authorName = ci.author.empty() ? L"Commit" : ci.author;
        float authorW = static_cast<float>(authorName.length()) * config.ScaleF(7.5f);
        D2D1_RECT_F rcAuthor = D2D1::RectF(textStartX, curCardY - config.ScaleF(1.0f), textStartX + authorW, curCardY + config.ScaleF(18.0f));
        pRT->DrawText(authorName.c_str(), static_cast<UINT32>(authorName.length()),
                      pCardFmt, rcAuthor, theme.brushTextPrimary.Get());

        std::wstring timeLine = ci.relativeTime;
        if (!ci.dateFormatted.empty()) timeLine += L" (" + ci.dateFormatted + L")";
        D2D1_RECT_F rcTime = D2D1::RectF(textStartX + authorW + config.ScaleF(8.0f), curCardY, cardLeft + cardW - closeBtnSize - config.ScaleF(10.0f), curCardY + config.ScaleF(18.0f));
        pRT->DrawText(timeLine.c_str(), static_cast<UINT32>(timeLine.length()),
                      pCardFmt, rcTime, theme.brushTextMuted.Get());

        curCardY += config.ScaleF(20.0f);

        // Commit message (wrapped, up to 2 lines)
        if (theme.dwriteFactory && pCardFmt) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pMsgLayout;
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(ci.message.c_str(), static_cast<UINT32>(ci.message.length()), pCardFmt, cardW - config.ScaleF(28.0f), config.ScaleF(36.0f), &pMsgLayout)) && pMsgLayout) {
                pMsgLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                pRT->DrawTextLayout(D2D1::Point2F(cardLeft + config.ScaleF(14.0f), curCardY), pMsgLayout.Get(), theme.brushTextPrimary.Get());
            }
        }
        curCardY += config.ScaleF(38.0f);

        // Stat line (e.g. 10 files changed, 1174 insertions(+), 83 deletions(-))
        std::wstring statText = ci.statSummary;
        if (statText.empty()) {
            statText = (ci.files.empty() ? L"1 file changed" : (std::to_wstring(ci.files.size()) + L" files changed"));
        }
        D2D1_RECT_F rcStat = D2D1::RectF(cardLeft + config.ScaleF(14.0f), curCardY, cardLeft + cardW - config.ScaleF(14.0f), curCardY + config.ScaleF(18.0f));

        // Syntax highlight insertions(+) in green, deletions(-) in red
        size_t insPos = statText.find(L"insertion");
        size_t delPos = statText.find(L"deletion");
        if (insPos != std::wstring::npos || delPos != std::wstring::npos) {
            float sX = cardLeft + config.ScaleF(14.0f);
            size_t c1 = statText.find(L',');
            if (c1 != std::wstring::npos) {
                std::wstring part1 = statText.substr(0, c1 + 2); // e.g. "10 files changed, "
                float w1 = static_cast<float>(part1.length()) * config.ScaleF(7.0f);
                pRT->DrawText(part1.c_str(), static_cast<UINT32>(part1.length()), pCardFmt, D2D1::RectF(sX, curCardY, sX + w1, curCardY + config.ScaleF(18.0f)), theme.brushTextMuted.Get());
                sX += w1;

                size_t c2 = statText.find(L',', c1 + 1);
                if (c2 != std::wstring::npos) {
                    std::wstring part2 = statText.substr(c1 + 2, c2 - (c1 + 2)); // e.g. "1174 insertions(+)"
                    float w2 = static_cast<float>(part2.length()) * config.ScaleF(7.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushIns;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.78f, 0.40f, 1.0f), &brushIns);
                    pRT->DrawText(part2.c_str(), static_cast<UINT32>(part2.length()), pCardFmt, D2D1::RectF(sX, curCardY, sX + w2, curCardY + config.ScaleF(18.0f)), brushIns.Get());
                    sX += w2;

                    std::wstring commaSep = L", ";
                    pRT->DrawText(commaSep.c_str(), 2, pCardFmt, D2D1::RectF(sX, curCardY, sX + config.ScaleF(14.0f), curCardY + config.ScaleF(18.0f)), theme.brushTextMuted.Get());
                    sX += config.ScaleF(14.0f);

                    std::wstring part3 = statText.substr(c2 + 2); // e.g. "83 deletions(-)"
                    float w3 = static_cast<float>(part3.length()) * config.ScaleF(7.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDel;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.35f, 0.35f, 1.0f), &brushDel);
                    pRT->DrawText(part3.c_str(), static_cast<UINT32>(part3.length()), pCardFmt, D2D1::RectF(sX, curCardY, sX + w3, curCardY + config.ScaleF(18.0f)), brushDel.Get());
                } else {
                    std::wstring part2 = statText.substr(c1 + 2);
                    pRT->DrawText(part2.c_str(), static_cast<UINT32>(part2.length()), pCardFmt, D2D1::RectF(sX, curCardY, cardLeft + cardW - config.ScaleF(14.0f), curCardY + config.ScaleF(18.0f)), theme.brushTextMuted.Get());
                }
            } else {
                pRT->DrawText(statText.c_str(), static_cast<UINT32>(statText.length()), pCardFmt, rcStat, theme.brushTextMuted.Get());
            }
        } else {
            pRT->DrawText(statText.c_str(), static_cast<UINT32>(statText.length()), pCardFmt, rcStat, theme.brushTextMuted.Get());
        }

        curCardY += config.ScaleF(20.0f);

        // Action buttons row: [hash pill] [↗ Open on GitHub] [Open Changes] (Image 3)
        float hashW = config.ScaleF(62.0f);
        float btnH = config.ScaleF(24.0f);
        D2D1_ROUNDED_RECT rrcHash = D2D1::RoundedRect(
            D2D1::RectF(cardLeft + config.ScaleF(14.0f), curCardY, cardLeft + config.ScaleF(14.0f) + hashW, curCardY + btnH),
            3.0f, 3.0f
        );
        pRT->FillRoundedRectangle(rrcHash, theme.brushHeaderBg.Get());
        pRT->DrawRoundedRectangle(rrcHash, brushCardBorder.Get(), 1.0f);
        std::wstring shortH = ci.hash.substr(0, (ci.hash.length() < 7) ? ci.hash.length() : 7);
        pRT->DrawText(shortH.c_str(), static_cast<UINT32>(shortH.length()),
                      pCardFmt,
                      D2D1::RectF(cardLeft + config.ScaleF(18.0f), curCardY + config.ScaleF(3.0f), cardLeft + config.ScaleF(14.0f) + hashW, curCardY + btnH),
                      theme.brushBrandAccent.Get());

        // Open on GitHub link
        float ghLeft = cardLeft + config.ScaleF(84.0f);
        float ghW = config.ScaleF(110.0f);
        m_commitCardGitHubBounds = D2D1::RectF(ghLeft, curCardY, ghLeft + ghW, curCardY + btnH);
        const wchar_t* ghText = L"↗ Open on GitHub";
        pRT->DrawText(ghText, static_cast<UINT32>(wcslen(ghText)),
                      pCardFmt,
                      D2D1::RectF(ghLeft, curCardY + config.ScaleF(3.0f), ghLeft + ghW, curCardY + btnH),
                      theme.brushBrandAccent.Get());

        // Open Changes Button (Right-aligned, matching Image 5 & 3)
        float chW = config.ScaleF(108.0f);
        float chLeft = cardLeft + cardW - chW - config.ScaleF(12.0f);
        m_commitCardOpenChangesBounds = D2D1::RectF(chLeft, curCardY, chLeft + chW, curCardY + btnH);
        D2D1_ROUNDED_RECT rrcOpenChanges = D2D1::RoundedRect(m_commitCardOpenChangesBounds, 3.5f, 3.5f);
        pRT->FillRoundedRectangle(rrcOpenChanges, theme.brushBrandAccent.Get());
        const wchar_t* ocText = L"Open Changes";
        pRT->DrawText(ocText, static_cast<UINT32>(wcslen(ocText)),
                      pCardFmt,
                      D2D1::RectF(chLeft + config.ScaleF(10.0f), curCardY + config.ScaleF(3.0f), chLeft + chW, curCardY + btnH),
                      theme.brushTextWhite.Get());
    } else {
        m_commitCardBounds = D2D1::RectF(0, 0, 0, 0);
        m_commitCardOpenChangesBounds = D2D1::RectF(0, 0, 0, 0);
        m_commitCardGitHubBounds = D2D1::RectF(0, 0, 0, 0);
    }

    // =========================================================================
    // 5. Floating Popup Menus (Header Views, Changes More, Graph More)
    // =========================================================================
    if (m_activeGitMenu != GitMenuType::None || m_showGitViewMenu) {
        m_gitMenuItems.clear();
        D2D1_RECT_F anchor = m_gitHeaderMoreBounds;
        float menuW = config.ScaleF(150.0f);

        if (m_activeGitMenu == GitMenuType::ChangesMore) {
            anchor = m_gitChangesMoreBounds;
            menuW = config.ScaleF(190.0f);
            m_gitMenuItems.push_back({ 10, L"✓  Stage All Changes" });
            m_gitMenuItems.push_back({ 11, L"🔄 Discard All Changes" });
            m_gitMenuItems.push_back({ 13, L"↩  Undo Last Commit" });
            m_gitMenuItems.push_back({ 12, L"📦 Stash Changes" });
            m_gitMenuItems.push_back({ 0,  L"", false, true }); // separator
            m_gitMenuItems.push_back({ 24, L"🧹 Run Garbage Collection" });
            m_gitMenuItems.push_back({ 14, L"🔄 Refresh Status" });
        } else if (m_activeGitMenu == GitMenuType::GraphMore) {
            anchor = m_gitGraphMoreBounds;
            menuW = config.ScaleF(190.0f);
            m_gitMenuItems.push_back({ 13, L"↩  Undo Last Commit" });
            m_gitMenuItems.push_back({ 20, L"🔄 Refresh Graph" });
            m_gitMenuItems.push_back({ 21, L"↑  Push Commits" });
            m_gitMenuItems.push_back({ 22, L"↓  Pull Commits" });
            m_gitMenuItems.push_back({ 23, L"☁  Fetch All Remotes" });
            m_gitMenuItems.push_back({ 0,  L"", false, true }); // separator
            m_gitMenuItems.push_back({ 24, L"🧹 Run Garbage Collection" });
        } else {
            // Header Views
            anchor = m_gitHeaderMoreBounds;
            menuW = config.ScaleF(180.0f);
            m_gitMenuItems.push_back({ 1, L"Repositories", true }); // header
            m_gitMenuItems.push_back({ 2, m_showChangesSection ? L"✓  Changes" : L"    Changes", false, false, m_showChangesSection });
            m_gitMenuItems.push_back({ 3, m_showGraphSection ? L"✓  Graph" : L"    Graph", false, false, m_showGraphSection });
            m_gitMenuItems.push_back({ 0, L"", false, true }); // separator
            m_gitMenuItems.push_back({ 13, L"↩  Undo Last Commit" });
            m_gitMenuItems.push_back({ 24, L"🧹 Run Garbage Collection" });
            m_gitMenuItems.push_back({ 5, L"🔄 Refresh All" });
        }

        float itemH = config.ScaleF(24.0f);
        float menuH = config.ScaleF(8.0f);
        for (const auto& itm : m_gitMenuItems) {
            menuH += itm.isSeparator ? config.ScaleF(6.0f) : itemH;
        }

        float menuRight = (std::min)(anchor.right, railW + currentDrawerW - config.ScaleF(4.0f));
        float menuLeft = menuRight - menuW;
        if (menuLeft < railW + config.ScaleF(4.0f)) menuLeft = railW + config.ScaleF(4.0f);
        float menuTop = anchor.bottom + config.ScaleF(2.0f);
        if (menuTop + menuH > clientHeight) menuTop = anchor.top - menuH - config.ScaleF(2.0f);

        m_gitMenuBounds = D2D1::RectF(menuLeft, menuTop, menuLeft + menuW, menuTop + menuH);
        m_gitViewMenuBounds = m_gitMenuBounds; // Backwards compatibility for tests

        D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_gitMenuBounds, 4.0f, 4.0f);
        pRT->FillRoundedRectangle(rrcMenu, theme.brushMenuBg.Get());
        pRT->DrawRoundedRectangle(rrcMenu, theme.brushMenuBorder.Get(), 1.0f);

        float itemY = menuTop + config.ScaleF(4.0f);
        for (size_t i = 0; i < m_gitMenuItems.size(); ++i) {
            auto& itm = m_gitMenuItems[i];
            if (itm.isSeparator) {
                float lineY = itemY + config.ScaleF(3.0f);
                pRT->DrawLine(D2D1::Point2F(menuLeft + 6.0f, lineY), D2D1::Point2F(menuLeft + menuW - 6.0f, lineY),
                              theme.brushBorderSubtle.Get(), 1.0f);
                itemY += config.ScaleF(6.0f);
            } else {
                itm.bounds = D2D1::RectF(menuLeft + 3.0f, itemY, menuLeft + menuW - 3.0f, itemY + itemH);
                if (itm.id == 2) m_gitViewMenuChangesBounds = itm.bounds;
                if (itm.id == 3) m_gitViewMenuGraphBounds = itm.bounds;

                if (!itm.isHeader && static_cast<int>(i) == m_hoveredGitMenuItem) {
                    D2D1_ROUNDED_RECT rrcHover = D2D1::RoundedRect(itm.bounds, 3.0f, 3.0f);
                    pRT->FillRoundedRectangle(rrcHover, theme.brushItemHover.Get());
                }

                D2D1_RECT_F rcItemText = D2D1::RectF(itm.bounds.left + config.ScaleF(8.0f), itm.bounds.top,
                                                     itm.bounds.right - config.ScaleF(6.0f), itm.bounds.bottom);
                pRT->DrawText(itm.label.c_str(), static_cast<UINT32>(itm.label.length()),
                              theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                              rcItemText, itm.isHeader ? theme.brushTextMuted.Get() : theme.brushTextPrimary.Get());
                itemY += itemH;
            }
        }
    }

    // =========================================================================
    // 6. Floating Tooltip Rendering (Only when menus are closed)
    // =========================================================================
    if (m_showGitTooltip && !m_gitTooltipText.empty() && m_activeGitMenu == GitMenuType::None) {
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
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcTipText, theme.brushTooltipText.Get());
    }
}

} // namespace phide::ui
