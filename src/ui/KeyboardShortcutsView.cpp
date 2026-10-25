/**
 * @file KeyboardShortcutsView.cpp
 * @brief Implementation of interactive VS Code-styled Keyboard Shortcuts view.
 */

#include "phide/ui/KeyboardShortcutsView.hpp"
#include <algorithm>

namespace phide::ui {

KeyboardShortcutsView::KeyboardShortcutsView() {
    InitializeShortcuts();
}

void KeyboardShortcutsView::InitializeShortcuts() {
    // Verified genuine, operational shortcuts in phide (all fake/unimplemented entries removed)
    m_shortcuts = {
        // --- File Operations ---
        { L"File: New Text File", { L"Ctrl", L"N" }, L"global", L"Default" },
        { L"File: New File...", { L"Ctrl", L"Alt", L"N" }, L"global", L"Default" },
        { L"File: New Window", { L"Ctrl", L"Shift", L"N" }, L"global", L"Default" },
        { L"File: Open File...", { L"Ctrl", L"O" }, L"global", L"Default" },
        { L"File: Open Folder...", { L"Ctrl", L"K", L"Ctrl", L"O" }, L"global", L"Default" },
        { L"File: Save", { L"Ctrl", L"S" }, L"editorTextFocus", L"Default" },
        { L"File: Save As...", { L"Ctrl", L"Shift", L"S" }, L"editorTextFocus", L"Default" },
        { L"File: Save All", { L"Ctrl", L"K", L"S" }, L"global", L"Default" },
        { L"File: Close Editor", { L"Ctrl", L"W" }, L"editorTextFocus", L"Default" },
        { L"File: Close Folder", { L"Ctrl", L"K", L"F" }, L"global", L"Default" },
        { L"File: Reopen Closed Editor", { L"Ctrl", L"Shift", L"T" }, L"global", L"Default" },

        // --- Preferences ---
        { L"Preferences: Color Theme", { L"Ctrl", L"K", L"Ctrl", L"T" }, L"global", L"Default" },
        { L"Preferences: Open Keyboard Shortcuts", { L"Ctrl", L"K", L"Ctrl", L"S" }, L"global", L"Default" },
        { L"Preferences: Settings", { L"Ctrl", L"," }, L"global", L"Default" },

        // --- Edit & Refactoring ---
        { L"Edit: Undo", { L"Ctrl", L"Z" }, L"editorTextFocus", L"Default" },
        { L"Edit: Redo", { L"Ctrl", L"Y" }, L"editorTextFocus", L"Default" },
        { L"Edit: Cut", { L"Ctrl", L"X" }, L"editorTextFocus", L"Default" },
        { L"Edit: Copy", { L"Ctrl", L"C" }, L"editorTextFocus", L"Default" },
        { L"Edit: Paste", { L"Ctrl", L"V" }, L"editorTextFocus", L"Default" },
        { L"Edit: Select All", { L"Ctrl", L"A" }, L"editorTextFocus", L"Default" },
        { L"Edit: Find", { L"Ctrl", L"F" }, L"editorTextFocus", L"Default" },
        { L"Edit: Replace", { L"Ctrl", L"H" }, L"editorTextFocus", L"Default" },
        { L"Edit: Refactor...", { L"Ctrl", L"Shift", L"R" }, L"editorTextFocus", L"Default" },
        { L"Edit: Change All Occurrences", { L"Ctrl", L"F2" }, L"editorTextFocus", L"Default" },
        { L"Edit: Toggle Line Comment", { L"Ctrl", L"/" }, L"editorTextFocus", L"Default" },
        { L"Edit: Toggle Block Comment", { L"Shift", L"Alt", L"A" }, L"editorTextFocus", L"Default" },
        { L"Edit: Indent Line", { L"Tab" }, L"editorTextFocus", L"Default" },
        { L"Edit: Outdent Line", { L"Shift", L"Tab" }, L"editorTextFocus", L"Default" },

        // --- Explorer Operations ---
        { L"Explorer: Undo Last File Operation", { L"Ctrl", L"Z" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Redo Last File Operation", { L"Ctrl", L"Y" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Rename Item", { L"Enter" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Rename Item (Alternative)", { L"F2" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Delete Item", { L"Delete" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Cut Item", { L"Ctrl", L"X" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Copy Item", { L"Ctrl", L"C" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Paste Item", { L"Ctrl", L"V" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Copy Path", { L"Shift", L"Alt", L"C" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Copy Relative Path", { L"Ctrl", L"K", L"Ctrl", L"Shift", L"C" }, L"filesExplorerFocus", L"Default" },

        // --- Search & Navigation ---
        { L"Navigation: Quick Open (Go to File)", { L"Ctrl", L"P" }, L"global", L"Default" },
        { L"Navigation: Quick Open / File Search", { L"Ctrl", L"K" }, L"global", L"Default" },
        { L"Navigation: Show All Commands", { L"Ctrl", L"Shift", L"P" }, L"global", L"Default" },
        { L"Navigation: Command Palette (Alternative)", { L"F1" }, L"global", L"Default" },
        { L"Navigation: Go to Definition", { L"F12" }, L"editorTextFocus", L"Default" },
        { L"Search: Find in Files", { L"Ctrl", L"Shift", L"F" }, L"global", L"Default" },
        { L"Search: Replace in Files", { L"Ctrl", L"Shift", L"H" }, L"global", L"Default" },

        // --- Debug & Execution Controls ---
        { L"Run: Run Without Debugging", { L"Ctrl", L"F5" }, L"global", L"Default" },
        { L"Debug: Start Debugging", { L"F5" }, L"global", L"Default" },
        { L"Debug: Stop Execution / Server", { L"Shift", L"F5" }, L"global", L"Default" },
        { L"Debug: Restart Execution / Server", { L"Ctrl", L"Shift", L"F5" }, L"global", L"Default" },
        { L"Debug: Toggle Breakpoint", { L"F9" }, L"editorTextFocus", L"Default" },
        { L"Debug: Step Over", { L"F10" }, L"debugFocus", L"Default" },
        { L"Debug: Step Into", { L"F11" }, L"debugFocus", L"Default" },
        { L"Debug: Step Out", { L"Shift", L"F11" }, L"debugFocus", L"Default" },
        { L"Debug: Continue Execution", { L"F5" }, L"debugFocus", L"Default" },
        { L"Debug: Toggle Debug Console", { L"Ctrl", L"Shift", L"Y" }, L"global", L"Default" },

        // --- Code Editing & IntelliSense ---
        { L"Code: Format Document", { L"Shift", L"Alt", L"F" }, L"editorTextFocus", L"Default" },
        { L"Code: Rename Symbol", { L"F2" }, L"editorTextFocus", L"Default" },
        { L"Code: Trigger IntelliSense Autocomplete", { L"Ctrl", L"Space" }, L"editorTextFocus", L"Default" },
        { L"Code: Emmet Expand Abbreviation", { L"Tab" }, L"editorTextFocus", L"Default" },

        // --- Source Control ---
        { L"Source Control: Commit Changes", { L"Ctrl", L"Enter" }, L"sourceControlFocus", L"Default" },
        { L"Source Control: Dismiss / Close Box", { L"Escape" }, L"sourceControlFocus", L"Default" },

        // --- View & Layout ---
        { L"View: Split Editor Right", { L"Ctrl", L"\\" }, L"global", L"Default" },
        { L"View: Toggle Primary Side Bar", { L"Ctrl", L"B" }, L"global", L"Default" },
        { L"View: Toggle Runtimes Panel (Extensions)", { L"Ctrl", L"Shift", L"X" }, L"global", L"Default" },
        { L"View: Toggle Full Screen", { L"F11" }, L"global", L"Default" },
        { L"View: Toggle Word Wrap", { L"Alt", L"Z" }, L"editorTextFocus", L"Default" },
        { L"View: Zoom In", { L"Ctrl", L"=" }, L"global", L"Default" },
        { L"View: Zoom Out", { L"Ctrl", L"-" }, L"global", L"Default" },
        { L"View: Reset Zoom", { L"Ctrl", L"0" }, L"global", L"Default" },
        { L"View: Next Tab", { L"Ctrl", L"Tab" }, L"global", L"Default" },
        { L"View: Previous Tab", { L"Ctrl", L"Shift", L"Tab" }, L"global", L"Default" },
        { L"View: Show Explorer", { L"Ctrl", L"Shift", L"E" }, L"global", L"Default" },
        { L"View: Show Search", { L"Ctrl", L"Shift", L"F" }, L"global", L"Default" },
        { L"View: Show Source Control", { L"Ctrl", L"Shift", L"G" }, L"global", L"Default" },

        // --- Terminal & Window ---
        { L"Terminal: Create New Terminal", { L"Ctrl", L"Shift", L"`" }, L"global", L"Default" },
        { L"Terminal: Toggle Terminal Panel", { L"Ctrl", L"`" }, L"global", L"Default" },
        { L"Terminal: Split Active Terminal Tab", { L"Ctrl", L"Shift", L"5" }, L"terminalFocus", L"Default" },
        { L"Terminal: Split Active Terminal (Alternative)", { L"Ctrl", L"Shift", L"\\" }, L"terminalFocus", L"Default" },
        { L"Window: Close Window", { L"Alt", L"F4" }, L"global", L"Default" },
    };
}

std::vector<size_t> KeyboardShortcutsView::GetFilteredIndices() const {
    std::vector<size_t> indices;
    if (m_searchQuery.empty()) {
        for (size_t i = 0; i < m_shortcuts.size(); ++i) indices.push_back(i);
        return indices;
    }

    std::wstring q = m_searchQuery;
    for (auto& c : q) c = towlower(c);

    for (size_t i = 0; i < m_shortcuts.size(); ++i) {
        const auto& item = m_shortcuts[i];
        std::wstring cmd = item.command;
        for (auto& c : cmd) c = towlower(c);

        std::wstring keys;
        for (const auto& k : item.keyPills) keys += k + L" ";
        for (auto& c : keys) c = towlower(c);

        if (cmd.find(q) != std::wstring::npos || keys.find(q) != std::wstring::npos) {
            indices.push_back(i);
        }
    }
    return indices;
}

void KeyboardShortcutsView::DrawKeycapBadge(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                            const std::wstring& text, float& curX, float centerY, float badgeH) {
    float textW = 0.0f;
    IDWriteTextFormat* pFmt = theme.fontKeycap ? theme.fontKeycap.Get() : theme.fontMenuShortcut.Get();
    if (theme.dwriteFactory && pFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.length()),
                                                          pFmt, 200.0f, badgeH, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                textW = tm.widthIncludingTrailingWhitespace;
            }
        }
    }

    const float padX = config.ScaleF(7.0f);
    const float badgeW = (std::max)(config.ScaleF(24.0f), textW + padX * 2.0f);
    D2D1_RECT_F rcBadge = D2D1::RectF(
        curX,
        centerY - badgeH / 2.0f,
        curX + badgeW,
        centerY + badgeH / 2.0f
    );

    // Pill background (#2E2E2E / #333333) with 3.5px corner radius
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcBadge, 3.5f, 3.5f);
    pRT->FillRoundedRectangle(rrc, theme.brushItemHover ? theme.brushItemHover.Get() : theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrc, theme.brushBorderHover ? theme.brushBorderHover.Get() : theme.brushBorderSubtle.Get(), 1.0f);

    // Pill text - perfectly centered horizontally and vertically
    pRT->DrawText(text.c_str(), static_cast<UINT32>(text.length()),
                  pFmt, rcBadge, theme.brushTextPrimary.Get());

    curX += badgeW + config.ScaleF(4.0f);
}

void KeyboardShortcutsView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                   float left, float width, float top, float height) {
    if (!pRT) return;
    m_viewBounds = D2D1::RectF(left, top, left + width, top + height);

    // Background
    pRT->FillRectangle(m_viewBounds, theme.brushCanvasBg.Get());

    const float marginX = config.ScaleF(24.0f);
    float curY = top + config.ScaleF(14.0f);

    // 1. Search Bar at Top: "Type to search in keybindings"
    const float searchH = config.ScaleF(32.0f);
    m_searchBoxBounds = D2D1::RectF(
        left + marginX,
        curY,
        left + width - marginX,
        curY + searchH
    );

    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(m_searchBoxBounds, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcSearch, theme.IsDarkMode() ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());

    // Focus / Active outline
    if (m_isSearchFocused) {
        pRT->DrawRoundedRectangle(rrcSearch, theme.brushBrandAccent.Get(), 1.5f);
    } else {
        pRT->DrawRoundedRectangle(rrcSearch, theme.brushBorderHover.Get(), 1.0f);
    }

    // Search Icon glyph (Magnifying Glass 🔍)
    const wchar_t* searchGlyph = L"\u26B2";
    D2D1_RECT_F rcIcon = D2D1::RectF(
        m_searchBoxBounds.left + config.ScaleF(8.0f),
        curY,
        m_searchBoxBounds.left + config.ScaleF(28.0f),
        curY + searchH
    );
    pRT->DrawText(searchGlyph, 1, theme.fontMenu.Get(), rcIcon, theme.brushTextMuted.Get());

    // Search Text / Placeholder
    D2D1_RECT_F rcSearchText = D2D1::RectF(
        m_searchBoxBounds.left + config.ScaleF(32.0f),
        curY,
        m_searchBoxBounds.right - config.ScaleF(10.0f),
        curY + searchH
    );

    ID2D1Brush* textBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();

    if (m_searchQuery.empty()) {
        const wchar_t* placeholder = L"Type to search in keybindings";
        pRT->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)),
                      theme.fontBody.Get(), rcSearchText, theme.brushTextMuted.Get());
    } else {
        pRT->DrawText(m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                      theme.fontBody.Get(), rcSearchText, textBrush);

        // Blinking search cursor
        if (m_isSearchFocused) {
            float textW = 0.0f;
            if (theme.dwriteFactory && theme.fontBody) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                                                                  theme.fontBody.Get(), 1000.0f, searchH, &pLayout))) {
                    DWRITE_TEXT_METRICS tm{};
                    if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                        textW = tm.widthIncludingTrailingWhitespace;
                    }
                }
            }
            float cursorX = rcSearchText.left + textW + 1.0f;
            float cy = curY + searchH / 2.0f;
            pRT->DrawLine(
                D2D1::Point2F(cursorX, cy - config.ScaleF(8.0f)),
                D2D1::Point2F(cursorX, cy + config.ScaleF(8.0f)),
                textBrush,
                1.5f
            );
        }
    }

    curY += searchH + config.ScaleF(14.0f);

    // 2. Table Column Headers
    const float headerH = config.ScaleF(26.0f);
    D2D1_RECT_F rcHeaderRow = D2D1::RectF(left + marginX, curY, left + width - marginX, curY + headerH);
    pRT->FillRectangle(rcHeaderRow, theme.brushDrawerBg.Get());

    const float tableW = rcHeaderRow.right - rcHeaderRow.left;
    const float colCommandW = tableW * 0.36f;
    const float colKeysW    = tableW * 0.28f;
    const float colWhenW    = tableW * 0.22f;

    float col0X = rcHeaderRow.left + config.ScaleF(8.0f);
    float col1X = rcHeaderRow.left + colCommandW;
    float col2X = col1X + colKeysW;
    float col3X = col2X + colWhenW;

    pRT->DrawText(L"Command", 7, theme.fontSectionHeader.Get(),
                  D2D1::RectF(col0X, curY, col1X, curY + headerH), theme.brushTextSecondary.Get());
    pRT->DrawText(L"Keybinding", 10, theme.fontSectionHeader.Get(),
                  D2D1::RectF(col1X, curY, col2X, curY + headerH), theme.brushTextSecondary.Get());
    pRT->DrawText(L"When", 4, theme.fontSectionHeader.Get(),
                  D2D1::RectF(col2X, curY, col3X, curY + headerH), theme.brushTextSecondary.Get());
    pRT->DrawText(L"Source", 6, theme.fontSectionHeader.Get(),
                  D2D1::RectF(col3X, curY, rcHeaderRow.right, curY + headerH), theme.brushTextSecondary.Get());

    // Divider line under header
    pRT->DrawLine(
        D2D1::Point2F(rcHeaderRow.left, curY + headerH),
        D2D1::Point2F(rcHeaderRow.right, curY + headerH),
        theme.brushBorderSubtle.Get(), 1.0f
    );

    curY += headerH + 1.0f;

    // 3. Table Rows with Clipping
    D2D1_RECT_F rcListClip = D2D1::RectF(left + marginX, curY, left + width - marginX, top + height - config.ScaleF(10.0f));
    pRT->PushAxisAlignedClip(rcListClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float rowY = curY - m_scrollOffsetY;
    const float rowH = config.ScaleF(30.0f);
    const float badgeH = config.ScaleF(20.0f);

    auto filtered = GetFilteredIndices();
    for (size_t idx = 0; idx < filtered.size(); ++idx) {
        if (rowY + rowH >= rcListClip.top && rowY <= rcListClip.bottom) {
            size_t itemIdx = filtered[idx];
            const auto& item = m_shortcuts[itemIdx];

            D2D1_RECT_F rcRow = D2D1::RectF(rcListClip.left, rowY, rcListClip.right, rowY + rowH);
            bool isHovered = (m_hoveredRow == static_cast<int>(idx));

            if (isHovered) {
                D2D1_ROUNDED_RECT rrcRow = D2D1::RoundedRect(rcRow, 3.0f, 3.0f);
                pRT->FillRoundedRectangle(rrcRow, theme.brushItemHover.Get());
            }

            // Command Label (White text)
            D2D1_RECT_F rcCmd = D2D1::RectF(col0X, rowY, col1X, rowY + rowH);
            pRT->DrawText(item.command.c_str(), static_cast<UINT32>(item.command.length()),
                          theme.fontBody.Get(), rcCmd, theme.brushTextPrimary.Get());

            // Keybinding Badges (VS Code Pill Keycaps)
            float curBadgeX = col1X;
            float rowCenterY = rowY + rowH / 2.0f;
            for (size_t p = 0; p < item.keyPills.size(); ++p) {
                DrawKeycapBadge(pRT, theme, config, item.keyPills[p], curBadgeX, rowCenterY, badgeH);
            }

            // When Label
            D2D1_RECT_F rcWhen = D2D1::RectF(col2X, rowY, col3X, rowY + rowH);
            pRT->DrawText(item.when.c_str(), static_cast<UINT32>(item.when.length()),
                          theme.fontMenuShortcut.Get(), rcWhen, theme.brushTextMuted.Get());

            // Source Label
            D2D1_RECT_F rcSource = D2D1::RectF(col3X, rowY, rcListClip.right, rowY + rowH);
            pRT->DrawText(item.source.c_str(), static_cast<UINT32>(item.source.length()),
                          theme.fontMenuShortcut.Get(), rcSource, theme.brushTextMuted.Get());

            // Subtle row separator
            pRT->DrawLine(
                D2D1::Point2F(rcRow.left, rowY + rowH - 0.5f),
                D2D1::Point2F(rcRow.right, rowY + rowH - 0.5f),
                theme.brushBorderSubtle.Get(), 0.5f
            );
        }
        rowY += rowH;
    }

    pRT->PopAxisAlignedClip();
}

bool KeyboardShortcutsView::HandleMouseMove(POINT clientPt) {
    int oldHover = m_hoveredRow;
    m_hoveredRow = -1;

    float curY = m_searchBoxBounds.bottom + 27.0f;
    float rowY = curY - m_scrollOffsetY;
    const float rowH = 30.0f;

    auto filtered = GetFilteredIndices();
    for (size_t idx = 0; idx < filtered.size(); ++idx) {
        if (clientPt.x >= m_viewBounds.left && clientPt.x <= m_viewBounds.right &&
            clientPt.y >= rowY && clientPt.y < rowY + rowH) {
            m_hoveredRow = static_cast<int>(idx);
            break;
        }
        rowY += rowH;
    }

    return (m_hoveredRow != oldHover);
}

bool KeyboardShortcutsView::HandleLButtonDown(POINT clientPt) {
    if (clientPt.x >= m_searchBoxBounds.left && clientPt.x <= m_searchBoxBounds.right &&
        clientPt.y >= m_searchBoxBounds.top && clientPt.y <= m_searchBoxBounds.bottom) {
        m_isSearchFocused = true;
        return true;
    }
    m_isSearchFocused = false;
    return false;
}

bool KeyboardShortcutsView::HandleMouseWheel(short delta) {
    const float scrollSpeed = 40.0f;
    m_scrollOffsetY -= (delta > 0 ? scrollSpeed : -scrollSpeed);
    if (m_scrollOffsetY < 0.0f) m_scrollOffsetY = 0.0f;
    return true;
}

bool KeyboardShortcutsView::HandleChar(wchar_t ch) {
    if (!m_isSearchFocused) return false;
    if (ch >= 32) {
        m_searchQuery += ch;
        m_scrollOffsetY = 0.0f;
        return true;
    }
    return false;
}

bool KeyboardShortcutsView::HandleKeyDown(WPARAM key) {
    if (!m_isSearchFocused) return false;
    if (key == VK_BACK) {
        if (!m_searchQuery.empty()) {
            m_searchQuery.pop_back();
            m_scrollOffsetY = 0.0f;
            return true;
        }
    } else if (key == VK_ESCAPE) {
        m_searchQuery.clear();
        m_scrollOffsetY = 0.0f;
        return true;
    }
    return false;
}

} // namespace phide::ui
