/**
 * @file EditorOverlays.cpp
 * @brief Interactive EditorCanvas overlays for Section 2 features:
 *        - Inline Per-Editor Find & Replace Bar (#11)
 *        - Inline Peek Definition / References / Call Hierarchy / Code Metrics Panel (#14, #16)
 *        - Interactive Breadcrumb Sibling/Symbol Dropdown (#15)
 *        - Inline Cross-File Symbol Rename Widget (#17)
 */

#include "phide/ui/EditorCanvas.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cwctype>

namespace phide::ui {

namespace {

bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) noexcept {
    return rc.right > rc.left && rc.bottom > rc.top &&
           pt.x >= rc.left && pt.x <= rc.right &&
           pt.y >= rc.top && pt.y <= rc.bottom;
}

void DrawToggleChip(
    ID2D1RenderTarget* pRT,
    Theme& theme,
    const D2D1_RECT_F& rc,
    const wchar_t* label,
    bool active,
    bool enabled = true) {
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, 3.0f, 3.0f);
    if (active) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
        pRT->CreateSolidColorBrush(
            D2D1::ColorF(theme.colors.brandAccent.r, theme.colors.brandAccent.g, theme.colors.brandAccent.b, 0.28f),
            &fillBrush
        );
        if (fillBrush) pRT->FillRoundedRectangle(&rrc, fillBrush.Get());
        if (theme.brushBrandAccent) pRT->DrawRoundedRectangle(&rrc, theme.brushBrandAccent.Get(), 1.0f);
    } else if (theme.brushBorderSubtle) {
        pRT->DrawRoundedRectangle(&rrc, theme.brushBorderSubtle.Get(), 0.8f);
    }

    ID2D1Brush* textBrush = !enabled
        ? theme.brushTextMuted.Get()
        : (active ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get());
    IDWriteTextFormat* fmt = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get();
    if (fmt && textBrush) {
        pRT->DrawText(label, static_cast<UINT32>(wcslen(label)), fmt, rc, textBrush);
    }
}

void PopulateDirDropdownItems(
    const std::filesystem::path& folder,
    std::vector<EditorCanvas::BreadcrumbDropdownItem>& outItems) {
    outItems.clear();
    std::error_code ec;
    if (folder.empty() || !std::filesystem::exists(folder, ec) || !std::filesystem::is_directory(folder, ec)) {
        return;
    }

    std::vector<EditorCanvas::BreadcrumbDropdownItem> dirs;
    std::vector<EditorCanvas::BreadcrumbDropdownItem> files;
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec || count >= 80) break;
        EditorCanvas::BreadcrumbDropdownItem it;
        it.label = entry.path().filename().wstring();
        it.fullPath = entry.path().wstring();
        it.isFolder = entry.is_directory(ec);
        if (it.isFolder) dirs.push_back(std::move(it));
        else files.push_back(std::move(it));
        count++;
    }
    auto cmpLabel = [](const auto& a, const auto& b) {
        return _wcsicmp(a.label.c_str(), b.label.c_str()) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), cmpLabel);
    std::sort(files.begin(), files.end(), cmpLabel);
    for (auto& d : dirs) outItems.push_back(std::move(d));
    for (auto& f : files) outItems.push_back(std::move(f));
    if (outItems.size() > 40) outItems.resize(40);
}

} // namespace

// =============================================================================
// Interactive Breadcrumb Sibling / Symbol Dropdown (#15)
// =============================================================================

bool EditorCanvas::HandleBreadcrumbClick(
    POINT pt,
    const app::AppConfig& config,
    const std::wstring& activeFilePath,
    const std::wstring& workspaceRoot,
    std::wstring& outOpenFile,
    size_t& outJumpLine,
    size_t& outJumpCol,
    bool& outJumpInCurrentFile) {
    (void)config;
    outOpenFile.clear();
    outJumpLine = 0;
    outJumpCol = 0;
    outJumpInCurrentFile = false;

    if (m_breadcrumbDropdownOpen) {
        if (PtInRectF(pt, m_rcBreadcrumbDropdown)) {
            for (const auto& item : m_breadcrumbDropdownItems) {
                if (PtInRectF(pt, item.bounds)) {
                    if (item.isSymbol) {
                        outJumpLine = item.line;
                        outJumpCol = item.col;
                        outJumpInCurrentFile = true;
                        DismissBreadcrumbDropdown();
                        return true;
                    }
                    if (!item.isFolder && !item.fullPath.empty()) {
                        outOpenFile = item.fullPath;
                        DismissBreadcrumbDropdown();
                        return true;
                    }
                    if (item.isFolder && !item.fullPath.empty()) {
                        PopulateDirDropdownItems(item.fullPath, m_breadcrumbDropdownItems);
                        return true;
                    }
                }
            }
            return true;
        }
    }

    for (const auto& hit : m_breadcrumbHits) {
        if (PtInRectF(pt, hit.bounds)) {
            if (m_breadcrumbDropdownOpen && m_breadcrumbDropdownSegIdx == hit.segmentIndex &&
                m_breadcrumbDropdownForSymbols == hit.isSymbolTail) {
                DismissBreadcrumbDropdown();
                return true;
            }
            m_breadcrumbDropdownSegIdx = hit.segmentIndex;
            m_breadcrumbDropdownForSymbols = hit.isSymbolTail;
            m_breadcrumbDropdownItems.clear();

            if (hit.isSymbolTail) {
                for (const auto& sym : m_activeDocSymbols) {
                    BreadcrumbDropdownItem item;
                    item.label = std::wstring(L"[") + editor::GetSymbolKindTag(sym.kind) + L"] " + sym.name + L"  :" + std::to_wstring(sym.line + 1);
                    item.fullPath = activeFilePath;
                    item.line = sym.line;
                    item.col = sym.col;
                    item.isSymbol = true;
                    m_breadcrumbDropdownItems.push_back(std::move(item));
                    if (m_breadcrumbDropdownItems.size() >= 40) break;
                }
            } else {
                std::filesystem::path targetPath(hit.resolvedPath.empty() ? activeFilePath : hit.resolvedPath);
                std::error_code ec;
                std::filesystem::path folderToList;
                if (hit.isFolder && std::filesystem::is_directory(targetPath, ec)) {
                    folderToList = targetPath;
                } else if (!targetPath.parent_path().empty() && std::filesystem::exists(targetPath.parent_path(), ec)) {
                    folderToList = targetPath.parent_path();
                } else if (!workspaceRoot.empty()) {
                    folderToList = workspaceRoot;
                }
                PopulateDirDropdownItems(folderToList, m_breadcrumbDropdownItems);
            }

            m_breadcrumbDropdownOpen = !m_breadcrumbDropdownItems.empty();
            return true;
        }
    }

    if (m_breadcrumbDropdownOpen) {
        DismissBreadcrumbDropdown();
    }
    return false;
}

void EditorCanvas::RenderBreadcrumbDropdown(
    ID2D1RenderTarget* pRT,
    Theme& theme,
    const app::AppConfig& config,
    float clientHeight) {
    if (!pRT || !m_breadcrumbDropdownOpen || m_breadcrumbDropdownItems.empty()) return;

    float anchorX = m_lastActivePaneBounds.left + config.ScaleF(14.0f);
    float anchorY = m_lastActivePaneBounds.top;
    for (const auto& h : m_breadcrumbHits) {
        if (h.segmentIndex == m_breadcrumbDropdownSegIdx && h.isSymbolTail == m_breadcrumbDropdownForSymbols) {
            anchorX = h.bounds.left;
            anchorY = h.bounds.bottom + config.ScaleF(2.0f);
            break;
        }
    }

    const float rowH = config.ScaleF(22.0f);
    const float dropW = config.ScaleF(260.0f);
    const size_t maxRows = (std::min)(m_breadcrumbDropdownItems.size(), size_t(12));
    const float dropH = static_cast<float>(maxRows) * rowH + config.ScaleF(8.0f);

    if (anchorX + dropW > m_lastActivePaneBounds.right - config.ScaleF(8.0f)) {
        anchorX = (std::max)(m_lastActivePaneBounds.left + config.ScaleF(8.0f),
                             m_lastActivePaneBounds.right - dropW - config.ScaleF(8.0f));
    }
    if (anchorY + dropH > clientHeight - config.ScaleF(8.0f)) {
        anchorY = (std::max)(config.ScaleF(40.0f), clientHeight - dropH - config.ScaleF(8.0f));
    }

    m_rcBreadcrumbDropdown = D2D1::RectF(anchorX, anchorY, anchorX + dropW, anchorY + dropH);
    D2D1_ROUNDED_RECT rrcDrop = D2D1::RoundedRect(m_rcBreadcrumbDropdown, 5.0f, 5.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.13f, 0.14f, 0.17f, 0.98f) : D2D1::ColorF(0.98f, 0.98f, 0.99f, 0.98f),
        &bgBrush
    );
    if (bgBrush) pRT->FillRoundedRectangle(&rrcDrop, bgBrush.Get());
    if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(&rrcDrop, theme.brushBorderSubtle.Get(), 1.0f);

    pRT->PushAxisAlignedClip(m_rcBreadcrumbDropdown, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float curY = anchorY + config.ScaleF(4.0f);
    for (size_t i = 0; i < m_breadcrumbDropdownItems.size(); ++i) {
        if (i >= maxRows) {
            m_breadcrumbDropdownItems[i].bounds = D2D1::RectF(0, 0, 0, 0);
            continue;
        }
        auto& item = m_breadcrumbDropdownItems[i];
        item.bounds = D2D1::RectF(anchorX + config.ScaleF(4.0f), curY, anchorX + dropW - config.ScaleF(4.0f), curY + rowH);
        D2D1_RECT_F rcText = D2D1::RectF(item.bounds.left + config.ScaleF(8.0f), curY, item.bounds.right - config.ScaleF(6.0f), curY + rowH);
        std::wstring text = (item.isFolder ? L"[Dir] " : L"") + item.label;
        ID2D1Brush* br = item.isSymbol ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get();
        pRT->DrawText(text.c_str(), static_cast<UINT32>(text.length()), theme.fontBody.Get(), rcText, br);
        curY += rowH;
    }
    pRT->PopAxisAlignedClip();
}

// =============================================================================
// Inline Per-Editor Find & Replace Bar (#11)
// =============================================================================

void EditorCanvas::OpenInlineFindBar(
    bool withReplace,
    const std::wstring& seedQuery,
    const editor::SearchRangeFilter* selRange) {
    m_findBar.isOpen = true;
    m_findBar.isReplaceOpen = withReplace;
    m_findBar.focusedInput = 1;
    if (selRange && selRange->restrictToRange) {
        m_findBar.selFilter = *selRange;
        m_findBar.hasSelectionFilter = true;
        m_findBar.inSelection = (selRange->endLine > selRange->startLine || selRange->endCol > selRange->startCol);
    } else {
        m_findBar.hasSelectionFilter = false;
        m_findBar.inSelection = false;
    }
    if (!seedQuery.empty()) {
        m_findBar.query = seedQuery;
    }
}

void EditorCanvas::RecomputeInlineFindMatches(const editor::OpenDocument& doc) {
    m_findBar.matches = editor::FindMatchesInLines(
        doc.buffer.GetLines(),
        m_findBar.query,
        m_findBar.matchCase,
        m_findBar.wholeWord,
        m_findBar.useRegex,
        (m_findBar.inSelection && m_findBar.hasSelectionFilter) ? m_findBar.selFilter : editor::SearchRangeFilter{}
    );
    if (m_findBar.matches.empty()) {
        m_findBar.currentMatchIdx = -1;
        return;
    }
    if (m_findBar.currentMatchIdx >= 0 &&
        m_findBar.currentMatchIdx < static_cast<int>(m_findBar.matches.size())) {
        return;
    }
    const auto curPos = doc.buffer.GetCursor();
    int bestIdx = 0;
    for (size_t i = 0; i < m_findBar.matches.size(); ++i) {
        const auto& m = m_findBar.matches[i];
        if (m.line > curPos.line || (m.line == curPos.line && m.col >= curPos.col)) {
            bestIdx = static_cast<int>(i);
            break;
        }
    }
    m_findBar.currentMatchIdx = bestIdx;
}

EditorCanvas::InlineFindAction EditorCanvas::StepInlineFindMatch(editor::OpenDocument& doc, bool forward) {
    if (m_findBar.matches.empty()) {
        RecomputeInlineFindMatches(doc);
    }
    if (m_findBar.matches.empty()) {
        m_findBar.currentMatchIdx = -1;
        return InlineFindAction::Consumed;
    }
    const int n = static_cast<int>(m_findBar.matches.size());
    if (m_findBar.currentMatchIdx < 0 || m_findBar.currentMatchIdx >= n) {
        m_findBar.currentMatchIdx = forward ? 0 : (n - 1);
    } else {
        m_findBar.currentMatchIdx = forward
            ? ((m_findBar.currentMatchIdx + 1) % n)
            : ((m_findBar.currentMatchIdx + n - 1) % n);
    }
    const auto& m = m_findBar.matches[static_cast<size_t>(m_findBar.currentMatchIdx)];
    doc.secondarySelections.clear();
    doc.hasSelection = true;
    doc.selAnchor = { m.line, m.col };
    doc.selEnd = { m.line, m.col + m.length };
    doc.buffer.SetCursor(m.line, m.col + m.length);
    doc.targetHighlightLine = static_cast<size_t>(-1);
    if (m.line < doc.scrollLine || m.line > doc.scrollLine + 18) {
        doc.scrollLine = (m.line > 5) ? (m.line - 5) : 0;
    }
    return forward ? InlineFindAction::FindNext : InlineFindAction::FindPrev;
}

EditorCanvas::InlineFindAction EditorCanvas::HandleInlineFindClick(POINT pt) {
    if (!m_findBar.isOpen || !PtInRectF(pt, m_findBar.rcBar)) {
        return InlineFindAction::None;
    }
    if (PtInRectF(pt, m_findBar.rcCloseBtn)) {
        m_findBar.isOpen = false;
        return InlineFindAction::Closed;
    }
    if (PtInRectF(pt, m_findBar.rcChevron)) {
        m_findBar.isReplaceOpen = !m_findBar.isReplaceOpen;
        return InlineFindAction::Consumed;
    }
    if (PtInRectF(pt, m_findBar.rcFindInput)) {
        m_findBar.focusedInput = 1;
        return InlineFindAction::Consumed;
    }
    if (m_findBar.isReplaceOpen && PtInRectF(pt, m_findBar.rcReplaceInput)) {
        m_findBar.focusedInput = 2;
        return InlineFindAction::Consumed;
    }
    if (PtInRectF(pt, m_findBar.rcMatchCaseBtn)) {
        m_findBar.matchCase = !m_findBar.matchCase;
        m_findBar.currentMatchIdx = -1;
        return InlineFindAction::OptionsChanged;
    }
    if (PtInRectF(pt, m_findBar.rcWholeWordBtn)) {
        m_findBar.wholeWord = !m_findBar.wholeWord;
        m_findBar.currentMatchIdx = -1;
        return InlineFindAction::OptionsChanged;
    }
    if (PtInRectF(pt, m_findBar.rcRegexBtn)) {
        m_findBar.useRegex = !m_findBar.useRegex;
        m_findBar.currentMatchIdx = -1;
        return InlineFindAction::OptionsChanged;
    }
    if (PtInRectF(pt, m_findBar.rcInSelectionBtn)) {
        m_findBar.inSelection = !m_findBar.inSelection;
        m_findBar.currentMatchIdx = -1;
        return InlineFindAction::OptionsChanged;
    }
    if (PtInRectF(pt, m_findBar.rcPrevBtn)) {
        return InlineFindAction::FindPrev;
    }
    if (PtInRectF(pt, m_findBar.rcNextBtn)) {
        return InlineFindAction::FindNext;
    }
    if (m_findBar.isReplaceOpen) {
        if (PtInRectF(pt, m_findBar.rcPreserveCaseBtn)) {
            m_findBar.preserveCase = !m_findBar.preserveCase;
            return InlineFindAction::Consumed;
        }
        if (PtInRectF(pt, m_findBar.rcReplaceOneBtn)) {
            return InlineFindAction::ReplaceCurrent;
        }
        if (PtInRectF(pt, m_findBar.rcReplaceAllBtn)) {
            return InlineFindAction::ReplaceAll;
        }
    }
    return InlineFindAction::Consumed;
}

EditorCanvas::InlineFindAction EditorCanvas::HandleInlineFindKeyDown(WPARAM vk, bool ctrl, bool shift) {
    if (!m_findBar.isOpen) return InlineFindAction::None;
    if (vk == VK_ESCAPE) {
        m_findBar.isOpen = false;
        return InlineFindAction::Closed;
    }
    if (vk == VK_F3) {
        return shift ? InlineFindAction::FindPrev : InlineFindAction::FindNext;
    }
    if (vk == VK_TAB && m_findBar.isReplaceOpen) {
        m_findBar.focusedInput = (m_findBar.focusedInput == 1) ? 2 : 1;
        return InlineFindAction::Consumed;
    }
    if (vk == VK_RETURN) {
        if (m_findBar.focusedInput == 2 && m_findBar.isReplaceOpen) {
            return (ctrl || shift) ? InlineFindAction::ReplaceAll : InlineFindAction::ReplaceCurrent;
        }
        return shift ? InlineFindAction::FindPrev : InlineFindAction::FindNext;
    }
    if (vk == VK_BACK) {
        std::wstring& dst = (m_findBar.isReplaceOpen && m_findBar.focusedInput == 2)
            ? m_findBar.replaceQuery
            : m_findBar.query;
        if (!dst.empty()) {
            dst.pop_back();
            if (m_findBar.focusedInput == 1) {
                m_findBar.currentMatchIdx = -1;
                return InlineFindAction::OptionsChanged;
            }
        }
        return InlineFindAction::Consumed;
    }
    return InlineFindAction::None;
}

bool EditorCanvas::HandleInlineFindChar(wchar_t ch) {
    if (!m_findBar.isOpen) return false;
    if (ch < 32) return false;
    std::wstring& dst = (m_findBar.isReplaceOpen && m_findBar.focusedInput == 2)
        ? m_findBar.replaceQuery
        : m_findBar.query;
    dst.push_back(ch);
    if (m_findBar.focusedInput == 1) {
        m_findBar.currentMatchIdx = -1;
    }
    return true;
}

void EditorCanvas::RenderInlineFindBar(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    if (!pRT || !m_findBar.isOpen) return;

    const float paneLeft = m_lastActivePaneBounds.left;
    const float paneRight = m_lastActivePaneBounds.right;
    const float paneTop = m_lastActivePaneBounds.top;
    const float availW = (std::max)(config.ScaleF(280.0f), paneRight - paneLeft - config.ScaleF(24.0f));
    const float barW = (std::min)(config.ScaleF(440.0f), availW);
    const float rowH = config.ScaleF(28.0f);
    const float barH = m_findBar.isReplaceOpen ? (rowH * 2.0f + config.ScaleF(14.0f)) : (rowH + config.ScaleF(10.0f));
    const float barX = (std::max)(paneLeft + config.ScaleF(8.0f), paneRight - barW - config.ScaleF(22.0f));
    const float barY = paneTop + config.ScaleF(6.0f);

    m_findBar.rcBar = D2D1::RectF(barX, barY, barX + barW, barY + barH);
    D2D1_ROUNDED_RECT rrcBar = D2D1::RoundedRect(m_findBar.rcBar, 5.0f, 5.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> barBg;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 0.98f) : D2D1::ColorF(0.96f, 0.97f, 0.98f, 0.98f),
        &barBg
    );
    if (barBg) pRT->FillRoundedRectangle(&rrcBar, barBg.Get());
    if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(&rrcBar, theme.brushBorderSubtle.Get(), 1.0f);

    // Row 1: [v] [Find Input + Badge] [Aa] [ab] [.*] [Sel] [Prev] [Next] [X]
    const float y1 = barY + config.ScaleF(5.0f);
    const float chevW = config.ScaleF(18.0f);
    m_findBar.rcChevron = D2D1::RectF(barX + config.ScaleF(4.0f), y1, barX + config.ScaleF(4.0f) + chevW, y1 + rowH - config.ScaleF(2.0f));
    pRT->DrawText(m_findBar.isReplaceOpen ? L"\u25BE" : L"\u25B8", 1,
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                  m_findBar.rcChevron, theme.brushTextSecondary.Get());

    const float btnW = config.ScaleF(22.0f);
    const float btnH = config.ScaleF(20.0f);
    const float btnY1 = y1 + (rowH - config.ScaleF(2.0f) - btnH) * 0.5f;
    float rightCursor = barX + barW - config.ScaleF(6.0f);

    auto placeRightBtn = [&](float width) {
        D2D1_RECT_F rc = D2D1::RectF(rightCursor - width, btnY1, rightCursor, btnY1 + btnH);
        rightCursor -= (width + config.ScaleF(3.0f));
        return rc;
    };

    m_findBar.rcCloseBtn = placeRightBtn(btnW);
    m_findBar.rcNextBtn = placeRightBtn(btnW);
    m_findBar.rcPrevBtn = placeRightBtn(btnW);
    m_findBar.rcInSelectionBtn = placeRightBtn(config.ScaleF(26.0f));
    m_findBar.rcRegexBtn = placeRightBtn(btnW);
    m_findBar.rcWholeWordBtn = placeRightBtn(btnW);
    m_findBar.rcMatchCaseBtn = placeRightBtn(btnW);

    const float badgeW = config.ScaleF(46.0f);
    D2D1_RECT_F rcBadge = placeRightBtn(badgeW);

    const float inputLeft = m_findBar.rcChevron.right + config.ScaleF(4.0f);
    m_findBar.rcFindInput = D2D1::RectF(inputLeft, y1 + config.ScaleF(2.0f),
                                        (std::max)(inputLeft + config.ScaleF(40.0f), rcBadge.left - config.ScaleF(4.0f)),
                                        y1 + rowH - config.ScaleF(4.0f));
    D2D1_ROUNDED_RECT rrcFind = D2D1::RoundedRect(m_findBar.rcFindInput, 3.0f, 3.0f);
    if (theme.brushCanvasBg) pRT->FillRoundedRectangle(&rrcFind, theme.brushCanvasBg.Get());
    pRT->DrawRoundedRectangle(&rrcFind,
                              m_findBar.focusedInput == 1 ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(),
                              m_findBar.focusedInput == 1 ? 1.4f : 1.0f);

    pRT->PushAxisAlignedClip(m_findBar.rcFindInput, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    D2D1_RECT_F rcFindTxt = D2D1::RectF(m_findBar.rcFindInput.left + config.ScaleF(6.0f), m_findBar.rcFindInput.top,
                                        m_findBar.rcFindInput.right - config.ScaleF(4.0f), m_findBar.rcFindInput.bottom);
    if (m_findBar.query.empty()) {
        pRT->DrawText(L"Find", 4, theme.fontBody.Get(), rcFindTxt, theme.brushTextMuted.Get());
    } else {
        pRT->DrawText(m_findBar.query.c_str(), static_cast<UINT32>(m_findBar.query.length()),
                      theme.fontBody.Get(), rcFindTxt, theme.brushTextPrimary.Get());
    }
    pRT->PopAxisAlignedClip();

    std::wstring countTxt;
    if (m_findBar.query.empty()) {
        countTxt = L"";
    } else if (m_findBar.matches.empty()) {
        countTxt = L"0/0";
    } else {
        int disp = (m_findBar.currentMatchIdx >= 0) ? (m_findBar.currentMatchIdx + 1) : 1;
        countTxt = std::to_wstring(disp) + L"/" + std::to_wstring(m_findBar.matches.size());
    }
    if (!countTxt.empty()) {
        pRT->DrawText(countTxt.c_str(), static_cast<UINT32>(countTxt.length()),
                      theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                      rcBadge, m_findBar.matches.empty() ? theme.brushIconRed.Get() : theme.brushTextSecondary.Get());
    }

    DrawToggleChip(pRT, theme, m_findBar.rcMatchCaseBtn, L"Aa", m_findBar.matchCase);
    DrawToggleChip(pRT, theme, m_findBar.rcWholeWordBtn, L"ab", m_findBar.wholeWord);
    DrawToggleChip(pRT, theme, m_findBar.rcRegexBtn, L".*", m_findBar.useRegex);
    DrawToggleChip(pRT, theme, m_findBar.rcInSelectionBtn, L"Sel", m_findBar.inSelection, m_findBar.hasSelectionFilter);
    DrawToggleChip(pRT, theme, m_findBar.rcPrevBtn, L"\u2191", false);
    DrawToggleChip(pRT, theme, m_findBar.rcNextBtn, L"\u2193", false);
    DrawToggleChip(pRT, theme, m_findBar.rcCloseBtn, L"\u2715", false);

    // Row 2 (Replace)
    if (m_findBar.isReplaceOpen) {
        const float y2 = y1 + rowH + config.ScaleF(2.0f);
        const float btnY2 = y2 + (rowH - config.ScaleF(2.0f) - btnH) * 0.5f;
        float r2Cursor = barX + barW - config.ScaleF(6.0f);
        auto placeR2Btn = [&](float width) {
            D2D1_RECT_F rc = D2D1::RectF(r2Cursor - width, btnY2, r2Cursor, btnY2 + btnH);
            r2Cursor -= (width + config.ScaleF(3.0f));
            return rc;
        };

        m_findBar.rcReplaceAllBtn = placeR2Btn(config.ScaleF(34.0f));
        m_findBar.rcReplaceOneBtn = placeR2Btn(config.ScaleF(38.0f));
        m_findBar.rcPreserveCaseBtn = placeR2Btn(config.ScaleF(26.0f));

        m_findBar.rcReplaceInput = D2D1::RectF(inputLeft, y2 + config.ScaleF(2.0f),
                                               (std::max)(inputLeft + config.ScaleF(40.0f), r2Cursor - config.ScaleF(4.0f)),
                                               y2 + rowH - config.ScaleF(4.0f));
        D2D1_ROUNDED_RECT rrcRep = D2D1::RoundedRect(m_findBar.rcReplaceInput, 3.0f, 3.0f);
        if (theme.brushCanvasBg) pRT->FillRoundedRectangle(&rrcRep, theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(&rrcRep,
                                  m_findBar.focusedInput == 2 ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(),
                                  m_findBar.focusedInput == 2 ? 1.4f : 1.0f);

        pRT->PushAxisAlignedClip(m_findBar.rcReplaceInput, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        D2D1_RECT_F rcRepTxt = D2D1::RectF(m_findBar.rcReplaceInput.left + config.ScaleF(6.0f), m_findBar.rcReplaceInput.top,
                                           m_findBar.rcReplaceInput.right - config.ScaleF(4.0f), m_findBar.rcReplaceInput.bottom);
        if (m_findBar.replaceQuery.empty()) {
            pRT->DrawText(L"Replace", 7, theme.fontBody.Get(), rcRepTxt, theme.brushTextMuted.Get());
        } else {
            pRT->DrawText(m_findBar.replaceQuery.c_str(), static_cast<UINT32>(m_findBar.replaceQuery.length()),
                          theme.fontBody.Get(), rcRepTxt, theme.brushTextPrimary.Get());
        }
        pRT->PopAxisAlignedClip();

        DrawToggleChip(pRT, theme, m_findBar.rcPreserveCaseBtn, L"AB", m_findBar.preserveCase);
        DrawToggleChip(pRT, theme, m_findBar.rcReplaceOneBtn, L"Rep", false);
        DrawToggleChip(pRT, theme, m_findBar.rcReplaceAllBtn, L"All", false);
    } else {
        m_findBar.rcReplaceInput = {};
        m_findBar.rcPreserveCaseBtn = {};
        m_findBar.rcReplaceOneBtn = {};
        m_findBar.rcReplaceAllBtn = {};
    }
}

// =============================================================================
// Inline Peek Definition / References / Call Hierarchy / Code Metrics Panel (#14, #16)
// =============================================================================

void EditorCanvas::OpenPeekPanel(
    PeekMode mode,
    const std::wstring& title,
    const std::wstring& subtitle,
    size_t anchorLine,
    std::vector<PeekItemEntry> items) {
    m_peekPanel.isOpen = true;
    m_peekPanel.mode = mode;
    m_peekPanel.title = title;
    m_peekPanel.subtitle = subtitle;
    m_peekPanel.anchorLine = anchorLine;
    m_peekPanel.items = std::move(items);
    m_peekPanel.selectedIndex = 0;
    m_peekPanel.listScrollIdx = 0;
    m_peekPanel.codeScrollY = 0.0f;
    m_peekPanel.previewLines.clear();
    m_peekPanel.previewStartLine = 0;
    LoadSelectedPeekPreview();
}

void EditorCanvas::OpenCodeMetricsPanel(
    const editor::WorkspaceCodeMetrics& metrics,
    const std::wstring& activeFileLabel) {
    m_peekPanel.isOpen = true;
    m_peekPanel.mode = PeekMode::CodeMetrics;
    m_peekPanel.title = L"Code Metrics & Complexity";
    m_peekPanel.subtitle = activeFileLabel;
    m_peekPanel.metrics = metrics;
    m_peekPanel.items.clear();
    m_peekPanel.previewLines.clear();
    m_peekPanel.codeScrollY = 0.0f;
}

void EditorCanvas::LoadSelectedPeekPreview() {
    m_peekPanel.previewLines.clear();
    m_peekPanel.previewStartLine = 0;
    if (m_peekPanel.selectedIndex < 0 || m_peekPanel.selectedIndex >= static_cast<int>(m_peekPanel.items.size())) {
        return;
    }
    const auto& sel = m_peekPanel.items[static_cast<size_t>(m_peekPanel.selectedIndex)];
    if (sel.filePath.empty()) {
        if (!sel.snippet.empty()) {
            m_peekPanel.previewLines.push_back(sel.snippet);
            m_peekPanel.previewStartLine = sel.line;
        }
        return;
    }
    std::ifstream ifs(sel.filePath, std::ios::binary);
    if (!ifs.is_open()) return;
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (raw.empty() || raw.size() > 2 * 1024 * 1024) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0);
    if (wlen <= 0) return;
    std::wstring content(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), &content[0], wlen);

    std::vector<std::wstring> allLines;
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = content.size();
        std::wstring ln = content.substr(pos, nl - pos);
        if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
        allLines.push_back(std::move(ln));
        if (nl == content.size()) break;
        pos = nl + 1;
    }

    size_t startLn = (sel.line > 4) ? (sel.line - 4) : 0;
    size_t endLn = (std::min)(allLines.size(), startLn + 18);
    m_peekPanel.previewStartLine = startLn;
    for (size_t i = startLn; i < endLn; ++i) {
        m_peekPanel.previewLines.push_back(allLines[i]);
    }
}

EditorCanvas::PeekAction EditorCanvas::HandlePeekClick(POINT pt) {
    if (!m_peekPanel.isOpen) return PeekAction::None;
    if (!PtInRectF(pt, m_peekPanel.rcPanel)) return PeekAction::None;

    if (PtInRectF(pt, m_peekPanel.rcCloseBtn)) {
        m_peekPanel.isOpen = false;
        return PeekAction::Closed;
    }
    if (m_peekPanel.mode != PeekMode::CodeMetrics && PtInRectF(pt, m_peekPanel.rcOpenFileBtn)) {
        return PeekAction::NavigateToSelected;
    }
    for (size_t i = 0; i < m_peekPanel.items.size(); ++i) {
        if (PtInRectF(pt, m_peekPanel.items[i].bounds)) {
            if (m_peekPanel.selectedIndex == static_cast<int>(i)) {
                return PeekAction::NavigateToSelected;
            }
            m_peekPanel.selectedIndex = static_cast<int>(i);
            LoadSelectedPeekPreview();
            return PeekAction::SelectionChanged;
        }
    }
    return PeekAction::Consumed;
}

EditorCanvas::PeekAction EditorCanvas::HandlePeekKeyDown(WPARAM vk) {
    if (!m_peekPanel.isOpen) return PeekAction::None;
    if (vk == VK_ESCAPE) {
        m_peekPanel.isOpen = false;
        return PeekAction::Closed;
    }
    if (m_peekPanel.mode != PeekMode::CodeMetrics && !m_peekPanel.items.empty()) {
        int n = static_cast<int>(m_peekPanel.items.size());
        if (vk == VK_DOWN) {
            m_peekPanel.selectedIndex = (m_peekPanel.selectedIndex + 1) % n;
            LoadSelectedPeekPreview();
            return PeekAction::SelectionChanged;
        }
        if (vk == VK_UP) {
            m_peekPanel.selectedIndex = (m_peekPanel.selectedIndex + n - 1) % n;
            LoadSelectedPeekPreview();
            return PeekAction::SelectionChanged;
        }
        if (vk == VK_RETURN) {
            return PeekAction::NavigateToSelected;
        }
    }
    return PeekAction::None;
}

bool EditorCanvas::HandlePeekWheel(POINT pt, short delta) {
    if (!m_peekPanel.isOpen || !PtInRectF(pt, m_peekPanel.rcPanel)) return false;
    if (m_peekPanel.mode == PeekMode::CodeMetrics) {
        m_peekPanel.codeScrollY = (std::max)(0.0f, m_peekPanel.codeScrollY - (delta > 0 ? 24.0f : -24.0f));
        return true;
    }
    if (!m_peekPanel.items.empty()) {
        int maxStart = (std::max)(0, static_cast<int>(m_peekPanel.items.size()) - 6);
        m_peekPanel.listScrollIdx = (std::max)(0, (std::min)(maxStart, m_peekPanel.listScrollIdx + (delta > 0 ? -1 : 1)));
        return true;
    }
    return true;
}

void EditorCanvas::RenderInlinePeekPanel(
    ID2D1RenderTarget* pRT,
    Theme& theme,
    const app::AppConfig& config,
    float clientHeight) {
    if (!pRT || !m_peekPanel.isOpen) return;

    const float pad = config.ScaleF(20.0f);
    const float pLeft = m_lastActivePaneBounds.left + pad;
    const float pRight = (std::max)(pLeft + config.ScaleF(360.0f), m_lastActivePaneBounds.right - pad);
    const float panelH = (std::min)(config.ScaleF(300.0f), (std::max)(config.ScaleF(160.0f), clientHeight * 0.44f));
    const float pTop = (std::max)(m_lastActivePaneBounds.top + config.ScaleF(42.0f),
                                  (m_lastActivePaneBounds.bottom - panelH) * 0.45f);

    m_peekPanel.rcPanel = D2D1::RectF(pLeft, pTop, pRight, pTop + panelH);
    D2D1_ROUNDED_RECT rrcPanel = D2D1::RoundedRect(m_peekPanel.rcPanel, 6.0f, 6.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush, hdrBrush, accentLineBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.11f, 0.12f, 0.15f, 0.99f) : D2D1::ColorF(0.97f, 0.98f, 0.99f, 0.99f),
        &bgBrush
    );
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.16f, 0.18f, 0.22f, 1.0f) : D2D1::ColorF(0.91f, 0.93f, 0.96f, 1.0f),
        &hdrBrush
    );
    pRT->CreateSolidColorBrush(theme.colors.brandAccent, &accentLineBrush);

    if (bgBrush) pRT->FillRoundedRectangle(&rrcPanel, bgBrush.Get());
    if (accentLineBrush) pRT->DrawRoundedRectangle(&rrcPanel, accentLineBrush.Get(), 1.4f);

    const float hdrH = config.ScaleF(28.0f);
    D2D1_RECT_F rcHdr = D2D1::RectF(pLeft + 1.0f, pTop + 1.0f, pRight - 1.0f, pTop + hdrH);
    if (hdrBrush) pRT->FillRectangle(rcHdr, hdrBrush.Get());

    m_peekPanel.rcCloseBtn = D2D1::RectF(pRight - config.ScaleF(30.0f), pTop + config.ScaleF(4.0f),
                                          pRight - config.ScaleF(6.0f), pTop + hdrH - config.ScaleF(4.0f));
    if (m_peekPanel.mode != PeekMode::CodeMetrics && !m_peekPanel.items.empty()) {
        m_peekPanel.rcOpenFileBtn = D2D1::RectF(m_peekPanel.rcCloseBtn.left - config.ScaleF(62.0f),
                                                pTop + config.ScaleF(4.0f),
                                                m_peekPanel.rcCloseBtn.left - config.ScaleF(6.0f),
                                                pTop + hdrH - config.ScaleF(4.0f));
        DrawToggleChip(pRT, theme, m_peekPanel.rcOpenFileBtn, L"Open", true);
    } else {
        m_peekPanel.rcOpenFileBtn = {};
    }
    DrawToggleChip(pRT, theme, m_peekPanel.rcCloseBtn, L"\u2715", false);

    std::wstring hdrLabel = m_peekPanel.title;
    if (!m_peekPanel.subtitle.empty()) hdrLabel += L"  -  " + m_peekPanel.subtitle;
    D2D1_RECT_F rcHdrTxt = D2D1::RectF(pLeft + config.ScaleF(10.0f), pTop,
                                       (m_peekPanel.rcOpenFileBtn.left > 0.0f ? m_peekPanel.rcOpenFileBtn.left : m_peekPanel.rcCloseBtn.left) - config.ScaleF(8.0f),
                                       pTop + hdrH);
    pRT->PushAxisAlignedClip(rcHdrTxt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    pRT->DrawText(hdrLabel.c_str(), static_cast<UINT32>(hdrLabel.length()),
                  theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get(),
                  rcHdrTxt, theme.brushTextPrimary.Get());
    pRT->PopAxisAlignedClip();

    const float bodyTop = pTop + hdrH + config.ScaleF(4.0f);
    const float bodyBottom = pTop + panelH - config.ScaleF(6.0f);
    D2D1_RECT_F rcBody = D2D1::RectF(pLeft + config.ScaleF(8.0f), bodyTop, pRight - config.ScaleF(8.0f), bodyBottom);
    pRT->PushAxisAlignedClip(rcBody, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_peekPanel.mode == PeekMode::CodeMetrics) {
        const auto& ws = m_peekPanel.metrics;
        const float kpiH = config.ScaleF(44.0f);
        const float cardW = (rcBody.right - rcBody.left - config.ScaleF(24.0f)) / 4.0f;
        size_t maxCC = 1;
        for (const auto& tf : ws.topComplexFiles) {
            if (tf.cyclomaticComplexity > maxCC) maxCC = tf.cyclomaticComplexity;
        }
        struct KpiPair { std::wstring title; std::wstring value; };
        std::vector<KpiPair> kpis = {
            { L"Files", std::to_wstring(ws.totalFiles) },
            { L"Code Lines", std::to_wstring(ws.codeLines) + L" (" + std::to_wstring(ws.totalLines) + L" total)" },
            { L"Functions", std::to_wstring(ws.totalFunctions) },
            { L"Complexity Avg/Max", std::to_wstring(ws.avgComplexity) + L" / " + std::to_wstring(maxCC) }
        };
        for (size_t k = 0; k < 4; ++k) {
            float kx = rcBody.left + static_cast<float>(k) * (cardW + config.ScaleF(8.0f));
            D2D1_RECT_F rcKpi = D2D1::RectF(kx, bodyTop + config.ScaleF(4.0f), kx + cardW, bodyTop + config.ScaleF(4.0f) + kpiH);
            pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcKpi, 4.0f, 4.0f), theme.brushBorderSubtle.Get(), 1.0f);
            D2D1_RECT_F rcKt = D2D1::RectF(kx + config.ScaleF(6.0f), rcKpi.top + config.ScaleF(3.0f), rcKpi.right - config.ScaleF(4.0f), rcKpi.top + config.ScaleF(18.0f));
            D2D1_RECT_F rcKv = D2D1::RectF(kx + config.ScaleF(6.0f), rcKpi.top + config.ScaleF(19.0f), rcKpi.right - config.ScaleF(4.0f), rcKpi.bottom);
            pRT->DrawText(kpis[k].title.c_str(), static_cast<UINT32>(kpis[k].title.length()), theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(), rcKt, theme.brushTextMuted.Get());
            pRT->DrawText(kpis[k].value.c_str(), static_cast<UINT32>(kpis[k].value.length()), theme.fontBody.Get(), rcKv, theme.brushTextPrimary.Get());
        }

        float rowY = bodyTop + kpiH + config.ScaleF(14.0f) - m_peekPanel.codeScrollY;
        const float rH = config.ScaleF(20.0f);
        for (const auto& f : ws.topComplexFiles) {
            if (rowY + rH >= bodyTop + kpiH + config.ScaleF(10.0f) && rowY < bodyBottom) {
                std::wstring lineTxt = std::filesystem::path(f.filePath).filename().wstring() +
                                       L"   Lines: " + std::to_wstring(f.codeLines) +
                                       L"   Funcs: " + std::to_wstring(f.functionCount) +
                                       L"   Comments: " + std::to_wstring(f.commentLines) +
                                       L"   MaxCC: " + std::to_wstring(f.cyclomaticComplexity);
                D2D1_RECT_F rcRow = D2D1::RectF(rcBody.left + config.ScaleF(4.0f), rowY, rcBody.right - config.ScaleF(4.0f), rowY + rH);
                pRT->DrawText(lineTxt.c_str(), static_cast<UINT32>(lineTxt.length()),
                              theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                              rcRow, theme.brushTextSecondary.Get());
            }
            rowY += rH;
        }
    } else {
        const float listW = (std::max)(config.ScaleF(160.0f), (rcBody.right - rcBody.left) * 0.36f);
        const float codeRight = rcBody.right - listW - config.ScaleF(8.0f);
        const float splitX = codeRight + config.ScaleF(4.0f);
        pRT->DrawLine(D2D1::Point2F(splitX, bodyTop), D2D1::Point2F(splitX, bodyBottom), theme.brushBorderSubtle.Get(), 1.0f);

        // Left: Code preview
        D2D1_RECT_F rcCode = D2D1::RectF(rcBody.left, bodyTop, codeRight, bodyBottom);
        pRT->PushAxisAlignedClip(rcCode, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        size_t highlightBufLine = (m_peekPanel.selectedIndex >= 0 && m_peekPanel.selectedIndex < static_cast<int>(m_peekPanel.items.size()))
            ? m_peekPanel.items[static_cast<size_t>(m_peekPanel.selectedIndex)].line : static_cast<size_t>(-1);
        const float lineH = config.ScaleF(18.0f);
        float cy = bodyTop + config.ScaleF(2.0f);
        for (size_t i = 0; i < m_peekPanel.previewLines.size() && cy < bodyBottom; ++i) {
            size_t actualLine = m_peekPanel.previewStartLine + i;
            D2D1_RECT_F rcLine = D2D1::RectF(rcCode.left, cy, rcCode.right, cy + lineH);
            if (actualLine == highlightBufLine) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hlBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.25f), &hlBrush);
                if (hlBrush) pRT->FillRectangle(rcLine, hlBrush.Get());
            }
            std::wstring numbered = std::to_wstring(actualLine + 1) + L"  " + m_peekPanel.previewLines[i];
            pRT->DrawText(numbered.c_str(), static_cast<UINT32>(numbered.length()),
                          theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                          rcLine, theme.brushTextPrimary.Get());
            cy += lineH;
        }
        pRT->PopAxisAlignedClip();

        // Right: Selectable items list
        const float itemH = config.ScaleF(24.0f);
        float ly = bodyTop;
        for (size_t i = 0; i < m_peekPanel.items.size(); ++i) {
            auto& entry = m_peekPanel.items[i];
            if (static_cast<int>(i) < m_peekPanel.listScrollIdx || ly + itemH > bodyBottom) {
                entry.bounds = {};
                continue;
            }
            entry.bounds = D2D1::RectF(splitX + config.ScaleF(4.0f), ly, rcBody.right, ly + itemH);
            if (static_cast<int>(i) == m_peekPanel.selectedIndex) {
                pRT->FillRectangle(entry.bounds, theme.brushItemActive ? theme.brushItemActive.Get() : theme.brushItemHover.Get());
            }
            std::wstring txt = entry.label;
            if (!entry.detail.empty()) txt += L"  " + entry.detail;
            D2D1_RECT_F rcITxt = D2D1::RectF(entry.bounds.left + config.ScaleF(6.0f), ly, entry.bounds.right - config.ScaleF(4.0f), ly + itemH);
            pRT->DrawText(txt.c_str(), static_cast<UINT32>(txt.length()), theme.fontBody.Get(), rcITxt, theme.brushTextPrimary.Get());
            ly += itemH;
        }
    }

    pRT->PopAxisAlignedClip();
}

// =============================================================================
// Inline Cross-File Symbol Rename Widget (#17)
// =============================================================================

void EditorCanvas::OpenRenameWidget(
    const std::wstring& oldName,
    editor::RenamePlan plan,
    const std::wstring& statusNote) {
    m_renameWidget.isOpen = true;
    m_renameWidget.showPreview = false;
    m_renameWidget.oldName = oldName;
    m_renameWidget.newName = oldName;
    m_renameWidget.plan = std::move(plan);
    m_renameWidget.statusNote = statusNote;
}

EditorCanvas::RenameAction EditorCanvas::HandleRenameClick(POINT pt) {
    if (!m_renameWidget.isOpen) return RenameAction::None;
    if (!PtInRectF(pt, m_renameWidget.rcBox)) {
        CloseRenameWidget();
        return RenameAction::Cancelled;
    }
    if (PtInRectF(pt, m_renameWidget.rcApplyBtn)) {
        return RenameAction::ApplyRequested;
    }
    if (PtInRectF(pt, m_renameWidget.rcPreviewBtn)) {
        m_renameWidget.showPreview = !m_renameWidget.showPreview;
        return RenameAction::PreviewToggled;
    }
    if (PtInRectF(pt, m_renameWidget.rcCancelBtn)) {
        CloseRenameWidget();
        return RenameAction::Cancelled;
    }
    return RenameAction::Consumed;
}

EditorCanvas::RenameAction EditorCanvas::HandleRenameKeyDown(WPARAM vk) {
    if (!m_renameWidget.isOpen) return RenameAction::None;
    if (vk == VK_ESCAPE) {
        CloseRenameWidget();
        return RenameAction::Cancelled;
    }
    if (vk == VK_RETURN) {
        return RenameAction::ApplyRequested;
    }
    if (vk == VK_BACK) {
        if (!m_renameWidget.newName.empty()) {
            m_renameWidget.newName.pop_back();
        }
        return RenameAction::Consumed;
    }
    return RenameAction::None;
}

bool EditorCanvas::HandleRenameChar(wchar_t ch) {
    if (!m_renameWidget.isOpen) return false;
    if (ch < 32) return false;
    if (iswalnum(ch) || ch == L'_' || ch == L'$') {
        m_renameWidget.newName.push_back(ch);
    }
    return true;
}

void EditorCanvas::RenderInlineRenameWidget(
    ID2D1RenderTarget* pRT,
    Theme& theme,
    const app::AppConfig& config,
    float clientHeight) {
    if (!pRT || !m_renameWidget.isOpen) return;

    const float boxW = config.ScaleF(330.0f);
    const float baseH = config.ScaleF(64.0f);
    const size_t previewCount = m_renameWidget.showPreview ? (std::min)(m_renameWidget.plan.fileGroups.size(), size_t(6)) : 0;
    const float previewH = previewCount > 0 ? (static_cast<float>(previewCount) * config.ScaleF(18.0f) + config.ScaleF(10.0f)) : 0.0f;
    const float boxH = baseH + previewH;

    float boxX = m_lastCaretX > 20.0f ? (m_lastCaretX - config.ScaleF(20.0f)) : (m_lastActivePaneBounds.left + config.ScaleF(40.0f));
    float boxY = m_lastCaretY > 40.0f ? (m_lastCaretY + config.ScaleF(22.0f)) : (m_lastActivePaneBounds.top + config.ScaleF(30.0f));
    if (boxX + boxW > m_lastActivePaneBounds.right - config.ScaleF(12.0f)) {
        boxX = (std::max)(m_lastActivePaneBounds.left + config.ScaleF(8.0f), m_lastActivePaneBounds.right - boxW - config.ScaleF(12.0f));
    }
    if (boxY + boxH > clientHeight - config.ScaleF(10.0f)) {
        boxY = (std::max)(m_lastActivePaneBounds.top + config.ScaleF(8.0f), clientHeight - boxH - config.ScaleF(10.0f));
    }

    m_renameWidget.rcBox = D2D1::RectF(boxX, boxY, boxX + boxW, boxY + boxH);
    D2D1_ROUNDED_RECT rrcBox = D2D1::RoundedRect(m_renameWidget.rcBox, 5.0f, 5.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 0.99f) : D2D1::ColorF(0.97f, 0.98f, 0.99f, 0.99f),
        &bgBrush
    );
    if (bgBrush) pRT->FillRoundedRectangle(&rrcBox, bgBrush.Get());
    if (theme.brushBrandAccent) pRT->DrawRoundedRectangle(&rrcBox, theme.brushBrandAccent.Get(), 1.3f);

    m_renameWidget.rcInput = D2D1::RectF(boxX + config.ScaleF(8.0f), boxY + config.ScaleF(8.0f),
                                         boxX + boxW - config.ScaleF(8.0f), boxY + config.ScaleF(32.0f));
    D2D1_ROUNDED_RECT rrcIn = D2D1::RoundedRect(m_renameWidget.rcInput, 3.0f, 3.0f);
    if (theme.brushCanvasBg) pRT->FillRoundedRectangle(&rrcIn, theme.brushCanvasBg.Get());
    if (theme.brushBrandAccent) pRT->DrawRoundedRectangle(&rrcIn, theme.brushBrandAccent.Get(), 1.2f);

    D2D1_RECT_F rcTxt = D2D1::RectF(m_renameWidget.rcInput.left + config.ScaleF(6.0f), m_renameWidget.rcInput.top,
                                    m_renameWidget.rcInput.right - config.ScaleF(6.0f), m_renameWidget.rcInput.bottom);
    pRT->DrawText(m_renameWidget.newName.c_str(), static_cast<UINT32>(m_renameWidget.newName.length()),
                  theme.fontBody.Get(), rcTxt, theme.brushTextPrimary.Get());

    const float row2Y = boxY + config.ScaleF(38.0f);
    const float btnH = config.ScaleF(20.0f);
    m_renameWidget.rcApplyBtn = D2D1::RectF(boxX + config.ScaleF(8.0f), row2Y, boxX + config.ScaleF(62.0f), row2Y + btnH);
    m_renameWidget.rcPreviewBtn = D2D1::RectF(boxX + config.ScaleF(68.0f), row2Y, boxX + config.ScaleF(132.0f), row2Y + btnH);
    m_renameWidget.rcCancelBtn = D2D1::RectF(boxX + config.ScaleF(138.0f), row2Y, boxX + config.ScaleF(162.0f), row2Y + btnH);

    DrawToggleChip(pRT, theme, m_renameWidget.rcApplyBtn, L"Apply", true);
    DrawToggleChip(pRT, theme, m_renameWidget.rcPreviewBtn, L"Preview", m_renameWidget.showPreview);
    DrawToggleChip(pRT, theme, m_renameWidget.rcCancelBtn, L"\u2715", false);

    std::wstring info = std::to_wstring(m_renameWidget.plan.totalEdits) + L" edits / " +
                        std::to_wstring(m_renameWidget.plan.fileGroups.size()) + L" files";
    D2D1_RECT_F rcInfo = D2D1::RectF(boxX + config.ScaleF(168.0f), row2Y, boxX + boxW - config.ScaleF(8.0f), row2Y + btnH);
    pRT->DrawText(info.c_str(), static_cast<UINT32>(info.length()),
                  theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                  rcInfo, theme.brushTextSecondary.Get());

    if (m_renameWidget.showPreview && previewCount > 0) {
        float py = boxY + baseH;
        for (size_t i = 0; i < previewCount; ++i) {
            const auto& f = m_renameWidget.plan.fileGroups[i];
            std::wstring lineTxt = std::filesystem::path(f.filePath).filename().wstring() +
                                   L" (" + std::to_wstring(f.edits.size()) + L" refs)";
            D2D1_RECT_F rcP = D2D1::RectF(boxX + config.ScaleF(10.0f), py, boxX + boxW - config.ScaleF(10.0f), py + config.ScaleF(18.0f));
            pRT->DrawText(lineTxt.c_str(), static_cast<UINT32>(lineTxt.length()),
                          theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                          rcP, theme.brushTextSecondary.Get());
            py += config.ScaleF(18.0f);
        }
    }
}

} // namespace phide::ui
