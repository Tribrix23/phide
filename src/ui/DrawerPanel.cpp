/**
 * @file DrawerPanel.cpp
 * @brief Implementation of the slide-out sidebar drawer view with real filesystem browsing.
 */

#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/ActivityRail.hpp"
#include <algorithm>

namespace phide::ui {

DrawerPanel::DrawerPanel() {
    // Starts with no folder opened - mock tree is completely removed
    m_hasOpenFolder = false;
    m_visibleItems.clear();
}

void DrawerPanel::OpenFolder(const std::wstring& folderPath) {
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
    m_expandedDirs.insert(m_openFolderPath);

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
    const float cornerRadius = 14.0f;

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

    // 2. Render Drawer Contents
    const float fullDrawerW = config.ScaleF(static_cast<float>(config.drawerWidth));
    const float minContentW = config.ScaleF(40.0f);

    if (currentDrawerW >= minContentW) {
        D2D1_RECT_F clipRect = D2D1::RectF(railW, headerH, railW + currentDrawerW, clientHeight);
        pRT->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

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

            // Tree Items with Seti/Material Icons & Indentation Guidelines
            const float itemH = config.ScaleF(24.0f);
            for (size_t i = 0; i < m_visibleItems.size(); ++i) {
                auto& item = m_visibleItems[i];
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

                // 3. Icon (Vector Folder shape or Seti/Material File Badge)
                float iconX = chevronX + config.ScaleF(10.0f);
                if (item.isFolder) {
                    // Folder vector icon: tab + body
                    float fx = iconX;
                    float fy = cy - config.ScaleF(6.0f);
                    float fw = config.ScaleF(15.0f);
                    float fh = config.ScaleF(12.0f);

                    // Tab
                    D2D1_ROUNDED_RECT rcTab = D2D1::RoundedRect(
                        D2D1::RectF(fx, fy, fx + config.ScaleF(6.0f), fy + config.ScaleF(4.0f)),
                        1.0f, 1.0f
                    );
                    pRT->FillRoundedRectangle(rcTab, item.isExpanded ? theme.brushIconFolderOpen.Get() : theme.brushIconFolder.Get());

                    // Body
                    D2D1_ROUNDED_RECT rcBody = D2D1::RoundedRect(
                        D2D1::RectF(fx, fy + config.ScaleF(2.0f), fx + fw, fy + fh),
                        2.0f, 2.0f
                    );
                    pRT->FillRoundedRectangle(rcBody, item.isExpanded ? theme.brushIconFolderOpen.Get() : theme.brushIconFolder.Get());
                    if (item.isExpanded) {
                        // Draw folded flap highlight
                        pRT->DrawLine(
                            D2D1::Point2F(fx, fy + config.ScaleF(4.5f)),
                            D2D1::Point2F(fx + fw, fy + config.ScaleF(4.5f)),
                            theme.brushBorderSubtle.Get(),
                            1.0f
                        );
                    }
                } else {
                    // File type icon badges
                    float fx = iconX;
                    float fy = cy - config.ScaleF(6.0f);
                    std::wstring lower = item.label;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

                    if (lower.ends_with(L".php") || lower.ends_with(L".phtml")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(17.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconPurple.Get());
                        const wchar_t* tag = L"php";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".cpp") || lower.ends_with(L".cxx") || lower.ends_with(L".cc")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(17.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconBlue.Get());
                        const wchar_t* tag = L"C++";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".c")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(14.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconBlue.Get());
                        const wchar_t* tag = L"C";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".hpp") || lower.ends_with(L".h")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(15.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconBlue.Get());
                        const wchar_t* tag = L"H";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".js") || lower.ends_with(L".mjs") || lower.ends_with(L".cjs")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(15.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconYellow.Get());
                        const wchar_t* tag = L"JS";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushWindowBg.Get());
                    } else if (lower.ends_with(L".ts") || lower.ends_with(L".tsx")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(15.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconBlue.Get());
                        const wchar_t* tag = L"TS";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".json")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(15.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconYellow.Get());
                        const wchar_t* tag = L"{}";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushWindowBg.Get());
                    } else if (lower.ends_with(L".md") || lower.ends_with(L".markdown")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(17.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconBlue.Get());
                        const wchar_t* tag = L"MD";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".html") || lower.ends_with(L".htm")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(16.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconOrange.Get());
                        const wchar_t* tag = L"<>";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".css") || lower.ends_with(L".scss") || lower.ends_with(L".less")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(14.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconBlue.Get());
                        const wchar_t* tag = L"#";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".bat") || lower.ends_with(L".cmd") || lower.ends_with(L".sh") || lower.ends_with(L".ps1")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(15.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconGreen.Get());
                        const wchar_t* tag = L">_";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".yml") || lower.ends_with(L".yaml")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(14.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconRed.Get());
                        const wchar_t* tag = L"Y!";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else if (lower.ends_with(L".png") || lower.ends_with(L".jpg") || lower.ends_with(L".jpeg") || lower.ends_with(L".svg") || lower.ends_with(L".gif") || lower.ends_with(L".ico")) {
                        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
                            D2D1::RectF(fx, fy, fx + config.ScaleF(16.0f), fy + config.ScaleF(12.0f)),
                            2.5f, 2.5f
                        );
                        pRT->FillRoundedRectangle(rrc, theme.brushIconPurple.Get());
                        const wchar_t* tag = L"IMG";
                        pRT->DrawText(tag, static_cast<UINT32>(wcslen(tag)), theme.fontBadge.Get(), rrc.rect, theme.brushTextWhite.Get());
                    } else {
                        // Default clean document outline
                        D2D1_RECT_F docRc = D2D1::RectF(fx + 1.0f, fy, fx + config.ScaleF(12.0f), fy + config.ScaleF(12.5f));
                        pRT->DrawRectangle(docRc, theme.brushIconGray.Get(), 1.2f);
                    }
                }

                // 4. Draw Item Text Label
                float labelX = iconX + config.ScaleF(22.0f);
                D2D1_RECT_F rcLabel = D2D1::RectF(
                    labelX,
                    curY,
                    railW + fullDrawerW - config.ScaleF(10.0f),
                    curY + itemH
                );
                pRT->DrawText(
                    item.label.c_str(),
                    static_cast<UINT32>(item.label.length()),
                    theme.fontBody.Get(),
                    rcLabel,
                    isHovered ? theme.brushTextWhite.Get() : (item.isFolder ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
                );

                curY += itemH;
            }
        }

        pRT->PopAxisAlignedClip();
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

} // namespace phide::ui
