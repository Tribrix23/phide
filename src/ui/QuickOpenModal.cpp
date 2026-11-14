/**
 * @file QuickOpenModal.cpp
 * @brief Implementation of the Quick Open file search palette matching user reference Image 5.
 */

#include "phide/ui/QuickOpenModal.hpp"
#include "phide/plugin/PluginManager.hpp"
#include <filesystem>
#include <algorithm>
#include <cwctype>
#include <fstream>

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
    m_lastSelectedCol = -1;
    m_allItems.clear();
    m_recentPaths = recentPaths;
    m_openPaths = openPaths;
    m_workspaceFolder = workspaceFolder;

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

    // 3. Scan Workspace Files (up to 1000 files, skipping noise directories)
    if (!workspaceFolder.empty() && std::filesystem::exists(workspaceFolder)) {
        size_t count = 0;
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     workspaceFolder, std::filesystem::directory_options::skip_permission_denied)) {
                if (count >= 1000) break;
                if (!entry.is_regular_file()) continue;

                auto pathStr = entry.path().wstring();
                // Skip git, .phide, build outputs, node_modules
                if (pathStr.find(L"\\.git\\") != std::wstring::npos ||
                    pathStr.find(L"\\.phide\\") != std::wstring::npos ||
                    pathStr.find(L"\\node_modules\\") != std::wstring::npos ||
                    pathStr.find(L"\\bin\\") != std::wstring::npos ||
                    pathStr.find(L"\\obj\\") != std::wstring::npos ||
                    pathStr.find(L"\\build\\") != std::wstring::npos ||
                    pathStr.find(L"\\dist\\") != std::wstring::npos ||
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
    m_modalBounds = D2D1::RectF(290.0f, 46.0f, 910.0f, 486.0f);
    m_inputBounds = D2D1::RectF(300.0f, 56.0f, 900.0f, 90.0f);
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
    } else if (m_query.rfind(L"%", 0) == 0) {
        // Mode: Search for Text across files (%)
        std::wstring term = m_query.substr(1);
        while (!term.empty() && term.front() == L' ') term.erase(0, 1);

        if (term.empty()) {
            QuickOpenItem actSearch;
            actSearch.type = QuickOpenItem::Type::Action;
            actSearch.title = L"Type text to search in workspace files...";
            actSearch.description = L"Matches will be searched across open and workspace files";
            actSearch.iconType = FileIconType::DefaultFile;
            m_filteredItems.push_back(std::move(actSearch));

            QuickOpenItem actOpenSearch;
            actOpenSearch.type = QuickOpenItem::Type::Action;
            actOpenSearch.title = L"Open Search View in Side Bar";
            actOpenSearch.description = L"Ctrl + Shift + F";
            actOpenSearch.shortcut = L"Ctrl + Shift + F";
            actOpenSearch.actionCommand = L"open_search:";
            actOpenSearch.iconType = FileIconType::DefaultFile;
            m_filteredItems.push_back(std::move(actOpenSearch));
        } else {
            QuickOpenItem actFullSearch;
            actFullSearch.type = QuickOpenItem::Type::Action;
            actFullSearch.title = L"Search for \"" + term + L"\" in entire workspace";
            actFullSearch.description = L"Open Search View in sidebar";
            actFullSearch.actionCommand = L"open_search:" + term;
            actFullSearch.iconType = FileIconType::DefaultFile;
            m_filteredItems.push_back(std::move(actFullSearch));

            std::wstring termLower = term;
            for (auto& c : termLower) c = towlower(c);

            std::vector<std::wstring> candidateFiles = m_openPaths;
            for (const auto& it : m_allItems) {
                if (it.type == QuickOpenItem::Type::File && !it.fullPath.empty()) {
                    if (std::find(candidateFiles.begin(), candidateFiles.end(), it.fullPath) == candidateFiles.end()) {
                        candidateFiles.push_back(it.fullPath);
                    }
                }
            }

            size_t totalMatches = 0;
            for (const auto& filePath : candidateFiles) {
                if (totalMatches >= 40) break;
                std::filesystem::path p(filePath);
                std::wstring ext = p.extension().wstring();
                for (auto& c : ext) c = towlower(c);
                if (ext == L".png" || ext == L".jpg" || ext == L".ico" || ext == L".exe" || ext == L".dll" || ext == L".obj" || ext == L".bin") {
                    continue;
                }
                std::ifstream ifs(filePath);
                if (!ifs.is_open()) continue;

                std::string lineA;
                int lineNum = 1;
                while (std::getline(ifs, lineA) && lineNum <= 500 && totalMatches < 40) {
                    if (lineA.empty()) { lineNum++; continue; }
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, lineA.c_str(), -1, NULL, 0);
                    std::wstring lineW;
                    if (wlen > 1) {
                        lineW.resize(wlen - 1);
                        MultiByteToWideChar(CP_UTF8, 0, lineA.c_str(), -1, &lineW[0], wlen);
                    } else {
                        lineW.assign(lineA.begin(), lineA.end());
                    }

                    std::wstring lineLower = lineW;
                    for (auto& c : lineLower) c = towlower(c);
                    if (lineLower.find(termLower) != std::wstring::npos) {
                        size_t startPos = lineW.find_first_not_of(L" \t");
                        std::wstring trimmed = (startPos != std::wstring::npos) ? lineW.substr(startPos) : lineW;
                        if (trimmed.length() > 80) trimmed = trimmed.substr(0, 77) + L"...";

                        QuickOpenItem matchItem;
                        matchItem.type = QuickOpenItem::Type::File;
                        matchItem.title = trimmed;
                        matchItem.description = p.filename().wstring() + L":" + std::to_wstring(lineNum);
                        matchItem.fullPath = filePath;
                        matchItem.targetLine = lineNum;
                        matchItem.iconType = LucideIcons::GetIconTypeForPath(filePath, false, false);
                        m_filteredItems.push_back(std::move(matchItem));
                        totalMatches++;
                    }
                    lineNum++;
                }
            }
        }
    } else if (m_query.rfind(L">", 0) == 0) {
        // Mode: Show and Run Commands (>)
        std::wstring term = m_query.substr(1);
        while (!term.empty() && term.front() == L' ') term.erase(0, 1);
        std::wstring termLower = term;
        for (auto& c : termLower) c = towlower(c);

        struct PaletteCmd {
            std::wstring title;
            std::wstring shortcut;
            std::wstring actionId;
        };

        static const std::vector<PaletteCmd> kPaletteCommands = {
            { L"Preferences: Open Settings", L"Ctrl + ,", L"cmd_open_settings" },
            { L"Preferences: Open Keyboard Shortcuts", L"Ctrl + K Ctrl + S", L"cmd_shortcuts" },
            { L"File: New Text File", L"Ctrl + N", L"cmd_new_file" },
            { L"File: New Window", L"Ctrl + Shift + N", L"cmd_new_window" },
            { L"File: Open File...", L"Ctrl + O", L"cmd_open_file" },
            { L"File: Open Folder...", L"Ctrl + K Ctrl + O", L"cmd_open_folder" },
            { L"File: Save", L"Ctrl + S", L"cmd_save" },
            { L"File: Save As...", L"Ctrl + Shift + S", L"cmd_save_as" },
            { L"File: Save All", L"Ctrl + K S", L"cmd_save_all" },
            { L"File: Close Editor", L"Ctrl + W", L"cmd_close_editor" },
            { L"Edit: Undo", L"Ctrl + Z", L"cmd_undo" },
            { L"Edit: Redo", L"Ctrl + Y", L"cmd_redo" },
            { L"Edit: Cut", L"Ctrl + X", L"cmd_cut" },
            { L"Edit: Copy", L"Ctrl + C", L"cmd_copy" },
            { L"Edit: Paste", L"Ctrl + V", L"cmd_paste" },
            { L"Edit: Find", L"Ctrl + F", L"cmd_find_inline" },
            { L"Edit: Replace", L"Ctrl + H", L"cmd_replace_inline" },
            { L"Edit: Find in Files", L"Ctrl + Shift + F", L"cmd_find_in_files" },
            { L"Edit: Replace in Files", L"Ctrl + Shift + H", L"cmd_replace_in_files" },
            { L"Edit: Toggle Line Comment", L"Ctrl + /", L"cmd_toggle_line_comment" },
            { L"Edit: Toggle Block Comment", L"Shift + Alt + A", L"cmd_toggle_block_comment" },
            { L"Selection: Select All Occurrences", L"Ctrl + Shift + L", L"cmd_select_all_occurrences" },
            { L"Selection: Add Cursor Above", L"Ctrl + Alt + Up", L"cmd_add_cursor_up" },
            { L"Selection: Add Cursor Below", L"Ctrl + Alt + Down", L"cmd_add_cursor_down" },
            { L"Lines: Move Line Up", L"Alt + Up", L"cmd_move_line_up" },
            { L"Lines: Move Line Down", L"Alt + Down", L"cmd_move_line_down" },
            { L"Lines: Copy Line Up", L"Shift + Alt + Up", L"cmd_duplicate_line_up" },
            { L"Lines: Copy Line Down", L"Shift + Alt + Down", L"cmd_duplicate_line_down" },
            { L"Lines: Delete Line", L"Ctrl + Shift + K", L"cmd_delete_line" },
            { L"Lines: Join Lines", L"Ctrl + J", L"cmd_join_lines" },
            { L"Lines: Sort Lines Ascending", L"", L"cmd_sort_lines_asc" },
            { L"Lines: Sort Lines Descending", L"", L"cmd_sort_lines_desc" },
            { L"Lines: Trim Trailing Whitespace", L"", L"cmd_trim_trailing_whitespace" },
            { L"Editor: Format Document", L"Shift + Alt + F", L"cmd_format_document" },
            { L"Editor: Format Selection", L"Ctrl + K Ctrl + F", L"cmd_format_selection" },
            { L"Editor: Jump to Matching Bracket", L"Ctrl + Shift + \\", L"cmd_jump_to_bracket" },
            { L"Go: Go to Definition", L"F12", L"cmd_goto_definition" },
            { L"Go: Peek Definition", L"Alt + F12", L"cmd_peek_definition" },
            { L"Go: Go to Type Definition", L"", L"cmd_type_definition" },
            { L"Go: Go to Implementation", L"Ctrl + F12", L"cmd_implementation" },
            { L"Go: Find All References", L"Shift + F12", L"cmd_find_references" },
            { L"Go: Go to Symbol in Editor...", L"Ctrl + Shift + O", L"cmd_goto_symbol" },
            { L"Go: Go to Symbol in Workspace...", L"Ctrl + T", L"cmd_goto_workspace_symbol" },
            { L"Go: Back", L"Alt + Left", L"cmd_nav_back" },
            { L"Go: Forward", L"Alt + Right", L"cmd_nav_forward" },
            { L"Refactor: Rename Symbol", L"F2", L"cmd_rename_symbol" },
            { L"View: Call Hierarchy", L"", L"cmd_call_hierarchy" },
            { L"View: Code Metrics", L"", L"cmd_code_metrics" },
            { L"View: Toggle Explorer Outline", L"", L"cmd_toggle_outline" },
            { L"View: Focus Next Editor Group", L"Ctrl + K Ctrl + Right", L"cmd_focus_group_next" },
            { L"View: Focus Previous Editor Group", L"Ctrl + K Ctrl + Left", L"cmd_focus_group_prev" },
            { L"Bookmarks: Toggle Bookmark", L"Ctrl + Alt + K", L"cmd_toggle_bookmark" },
            { L"Bookmarks: Next Bookmark", L"Ctrl + Alt + N", L"cmd_next_bookmark" },
            { L"Bookmarks: Previous Bookmark", L"Ctrl + Alt + P", L"cmd_prev_bookmark" },
            { L"Bookmarks: Clear All Bookmarks", L"", L"cmd_clear_bookmarks" },
            { L"Fold: Fold All", L"Ctrl + K Ctrl + 0", L"cmd_fold_all" },
            { L"Fold: Unfold All", L"Ctrl + K Ctrl + J", L"cmd_unfold_all" },
            { L"Fold: Fold Level 1", L"Ctrl + K Ctrl + 1", L"cmd_fold_level_1" },
            { L"Fold: Fold Level 2", L"Ctrl + K Ctrl + 2", L"cmd_fold_level_2" },
            { L"Fold: Fold Level 3", L"Ctrl + K Ctrl + 3", L"cmd_fold_level_3" },
            { L"File: Toggle Line Ending (CRLF / LF)", L"", L"cmd_toggle_eol" },
            { L"File: Change Encoding to UTF-8", L"", L"cmd_encoding_utf8" },
            { L"File: Change Encoding to UTF-8 with BOM", L"", L"cmd_encoding_utf8_bom" },
            { L"File: Change Encoding to UTF-16 LE", L"", L"cmd_encoding_utf16le" },
            { L"File: Change Encoding to Windows-1252", L"", L"cmd_encoding_win1252" },
            { L"View: Toggle Primary Side Bar", L"Ctrl + B", L"cmd_toggle_sidebar" },
            { L"View: Toggle Terminal", L"Ctrl + `", L"cmd_toggle_terminal" },
            { L"View: Color Theme", L"Ctrl + K Ctrl + T", L"cmd_theme_picker" },
            { L"View: Keyboard Shortcuts", L"Ctrl + K Ctrl + S", L"cmd_shortcuts" },
            { L"View: Runtimes", L"Ctrl + Shift + X", L"cmd_runtimes" },
            { L"Terminal: New Terminal", L"Ctrl + Shift + `", L"cmd_new_terminal" },
            { L"Run: Start Debugging", L"F5", L"cmd_start_debugging" },
            { L"Run: Run Without Debugging", L"Ctrl + F5", L"cmd_run_without_debugging" },
            { L"Git: Commit", L"Ctrl + Enter", L"cmd_git_commit" },
            { L"Git: Refresh", L"Ctrl + Shift + R", L"cmd_git_refresh" }
        };

        auto pushScoredCmd = [&](const std::wstring& title, const std::wstring& shortcut, const std::wstring& actionId) {
            int score = term.empty() ? 1 : phide::editor::ScoreFuzzyMatch(term, title);
            if (score > 0) {
                QuickOpenItem it;
                it.type = QuickOpenItem::Type::Action;
                it.title = title;
                it.shortcut = shortcut;
                it.actionCommand = actionId;
                it.iconType = FileIconType::Terminal;
                it.score = score;
                m_filteredItems.push_back(std::move(it));
            }
        };

        for (const auto& cmd : kPaletteCommands) {
            pushScoredCmd(cmd.title, cmd.shortcut, cmd.actionId);
        }

        // Append commands contributed by plugins, extensions, and mods
        auto pluginCmds = phide::plugin::PluginManager::Instance().GetCommands();
        for (const auto& pCmd : pluginCmds) {
            pushScoredCmd(pCmd.title, pCmd.shortcut, pCmd.id);
        }

        if (!term.empty() && m_filteredItems.size() > 1) {
            std::stable_sort(m_filteredItems.begin(), m_filteredItems.end(),
                             [](const QuickOpenItem& a, const QuickOpenItem& b) {
                                 return a.score > b.score;
                             });
        }
    } else if (m_query.rfind(L":", 0) == 0) {
        // Mode: Go to Line & Column (:line or :line:col)
        std::wstring lineStr = m_query.substr(1);
        while (!lineStr.empty() && lineStr.front() == L' ') lineStr.erase(0, 1);
        int lineNum = 0;
        int colNum = 0;
        bool validSpec = phide::editor::ParseLineColSpec(lineStr, lineNum, colNum);

        QuickOpenItem it;
        it.type = QuickOpenItem::Type::Action;
        if (validSpec && lineNum > 0) {
            if (colNum > 0) {
                it.title = L"Go to Line " + std::to_wstring(lineNum) + L", Column " + std::to_wstring(colNum);
                it.description = L":<line>:<column>";
                it.actionCommand = L"goto_line:" + std::to_wstring(lineNum) + L":" + std::to_wstring(colNum);
            } else {
                it.title = L"Go to Line " + std::to_wstring(lineNum);
                it.description = L"Jump to line " + std::to_wstring(lineNum) + L" in active editor";
                it.actionCommand = L"goto_line:" + std::to_wstring(lineNum);
            }
        } else {
            it.title = L"Type line[:column] number to go to...";
            it.description = L":<line>[:col]";
        }
        it.iconType = FileIconType::DefaultFile;
        m_filteredItems.push_back(std::move(it));
    } else if (m_query.rfind(L"@", 0) == 0) {
        // Mode: Go to Document Symbol (@)
        std::wstring symTerm = m_query.substr(1);
        while (!symTerm.empty() && symTerm.front() == L' ') symTerm.erase(0, 1);

        std::vector<QuickOpenItem> symResults;
        auto pushDocSym = [&](const phide::editor::DocumentSymbol& sym, const std::wstring& path) {
            int score = symTerm.empty() ? 1 : phide::editor::ScoreFuzzyMatch(symTerm, sym.name, sym.containerName);
            if (score <= 0) return;
            QuickOpenItem symItem;
            symItem.type = QuickOpenItem::Type::File;
            std::wstring prefix = std::wstring(L"[") + phide::editor::GetSymbolKindTag(sym.kind) + L"] ";
            std::wstring container = sym.containerName.empty() ? L"" : (sym.containerName + L".");
            symItem.title = prefix + container + sym.name;
            std::wstring fname = path.empty() ? L"Active Document" : std::filesystem::path(path).filename().wstring();
            symItem.description = fname + L":" + std::to_wstring(sym.line + 1);
            symItem.fullPath = path;
            symItem.targetLine = static_cast<int>(sym.line + 1);
            symItem.targetCol = static_cast<int>(sym.col + 1);
            symItem.score = score + ((!m_activeFilePath.empty() && _wcsicmp(path.c_str(), m_activeFilePath.c_str()) == 0) ? 40 : 0);
            symItem.iconType = path.empty() ? FileIconType::DefaultFile : LucideIcons::GetIconTypeForPath(path, false, false);
            symResults.push_back(std::move(symItem));
        };

        if (!m_lspActiveSymbols.empty()) {
            for (const auto& s : m_lspActiveSymbols) {
                pushDocSym(s, m_activeFilePath);
            }
        } else if (!m_activeFileContent.empty()) {
            std::wstring ext = m_activeFilePath.empty() ? L".cpp" : std::filesystem::path(m_activeFilePath).extension().wstring();
            auto syms = phide::editor::ExtractDocumentSymbols(m_activeFileContent, ext);
            for (const auto& s : syms) {
                pushDocSym(s, m_activeFilePath);
            }
        }

        for (const auto& path : m_openPaths) {
            if (!m_activeFilePath.empty() && _wcsicmp(path.c_str(), m_activeFilePath.c_str()) == 0 &&
                (!m_lspActiveSymbols.empty() || !m_activeFileContent.empty())) {
                continue;
            }
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs.is_open()) continue;
            std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            if (raw.empty() || raw.size() > 1024 * 1024) continue;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0);
            if (wlen <= 0) continue;
            std::wstring text(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), &text[0], wlen);
            auto syms = phide::editor::ExtractDocumentSymbols(text, std::filesystem::path(path).extension().wstring());
            for (const auto& s : syms) {
                pushDocSym(s, path);
                if (symResults.size() >= 120) break;
            }
            if (symResults.size() >= 120) break;
        }

        if (!symTerm.empty() && symResults.size() > 1) {
            std::stable_sort(symResults.begin(), symResults.end(),
                             [](const QuickOpenItem& a, const QuickOpenItem& b) { return a.score > b.score; });
        }
        if (symResults.size() > 60) symResults.resize(60);

        if (symResults.empty()) {
            QuickOpenItem itHeader;
            itHeader.type = QuickOpenItem::Type::Action;
            itHeader.title = symTerm.empty() ? L"No symbols found in active editor" : L"No matching symbol \"" + symTerm + L"\"";
            itHeader.description = L"@<symbol>";
            itHeader.iconType = FileIconType::DefaultFile;
            m_filteredItems.push_back(std::move(itHeader));
        } else {
            m_filteredItems = std::move(symResults);
        }
    } else if (m_query.rfind(L"#", 0) == 0) {
        // Mode: Go to Workspace Symbol (#)
        std::wstring symTerm = m_query.substr(1);
        while (!symTerm.empty() && symTerm.front() == L' ') symTerm.erase(0, 1);

        if (!m_symbolIndex || m_symbolIndex->GetIndexedFileCount() == 0) {
            QuickOpenItem hint;
            hint.type = QuickOpenItem::Type::Action;
            hint.title = L"Workspace symbol index is empty or indexing...";
            hint.description = L"#<symbol> across workspace files";
            hint.iconType = FileIconType::DefaultFile;
            m_filteredItems.push_back(std::move(hint));
        } else {
            auto hits = m_symbolIndex->SearchSymbols(symTerm, 60);
            for (const auto& s : hits) {
                QuickOpenItem item;
                item.type = QuickOpenItem::Type::File;
                item.title = std::wstring(L"[") + phide::editor::GetSymbolKindTag(s.kind) + L"] " +
                             (s.detail.empty() ? s.name : (s.name + L" — " + s.detail));
                item.description = std::filesystem::path(s.filePath).filename().wstring() + L":" + std::to_wstring(s.line + 1);
                item.fullPath = s.filePath;
                item.targetLine = static_cast<int>(s.line + 1);
                item.targetCol = static_cast<int>(s.col + 1);
                item.iconType = LucideIcons::GetIconTypeForPath(s.filePath, false, false);
                m_filteredItems.push_back(std::move(item));
            }
            if (m_filteredItems.empty()) {
                QuickOpenItem emptyIt;
                emptyIt.type = QuickOpenItem::Type::Action;
                emptyIt.title = L"No workspace symbols matching \"" + symTerm + L"\"";
                emptyIt.description = std::to_wstring(m_symbolIndex->GetTotalSymbolCount()) + L" indexed symbols";
                m_filteredItems.push_back(std::move(emptyIt));
            }
        }
    } else if (m_query == L"?") {
        // Mode: Help Prefix List (?)
        struct PrefixHelp {
            std::wstring prefix;
            std::wstring label;
            std::wstring desc;
        };
        static const std::vector<PrefixHelp> kPrefixes = {
            { L">", L"Show and Run Commands", L"Execute IDE actions and commands" },
            { L"%", L"Search for Text", L"Search text across workspace files" },
            { L"@", L"Go to Symbol in Editor", L"Jump to function, method, or class" },
            { L"#", L"Go to Symbol in Workspace", L"Search indexed symbols across workspace" },
            { L":", L"Go to Line and Column", L"Jump to :line or :line:col in active editor" },
            { L"debug", L"Start Debugging", L"Launch debugger configuration" },
            { L"task", L"Run Task", L"Run workspace script / build task" }
        };
        for (const auto& pfx : kPrefixes) {
            QuickOpenItem it;
            it.type = QuickOpenItem::Type::Action;
            it.title = pfx.prefix + L"  " + pfx.label;
            it.description = pfx.desc;
            it.actionCommand = L"prefix:" + pfx.prefix + (pfx.prefix == L"debug" || pfx.prefix == L"task" ? L" " : L"");
            it.iconType = FileIconType::DefaultFile;
            m_filteredItems.push_back(std::move(it));
        }
    } else if (m_query.rfind(L"debug", 0) == 0) {
        std::wstring term = m_query.substr(5);
        while (!term.empty() && term.front() == L' ') term.erase(0, 1);

        auto configs = phide::editor::DiscoverLaunchConfigurations(m_workspaceFolder);
        for (const auto& cfg : configs) {
            int score = term.empty() ? 1 : phide::editor::ScoreFuzzyMatch(term, cfg.label, cfg.command);
            if (score <= 0) continue;
            QuickOpenItem it;
            it.type = QuickOpenItem::Type::Action;
            it.title = cfg.label;
            it.description = cfg.detail.empty() ? cfg.command : (cfg.detail + L" - " + cfg.command);
            it.actionCommand = L"cmd_start_debugging";
            it.shortcut = L"F5";
            it.iconType = FileIconType::Terminal;
            m_filteredItems.push_back(std::move(it));
        }

        QuickOpenItem it2;
        it2.type = QuickOpenItem::Type::Action;
        it2.title = L"Run Without Debugging (Ctrl+F5)";
        it2.description = L"Execute active project or file";
        it2.actionCommand = L"cmd_run_without_debugging";
        it2.shortcut = L"Ctrl + F5";
        it2.iconType = FileIconType::Terminal;
        m_filteredItems.push_back(std::move(it2));
    } else if (m_query.rfind(L"task", 0) == 0) {
        std::wstring term = m_query.substr(4);
        while (!term.empty() && term.front() == L' ') term.erase(0, 1);

        auto tasks = phide::editor::DiscoverWorkspaceTasks(m_workspaceFolder);
        for (const auto& t : tasks) {
            int score = term.empty() ? 1 : phide::editor::ScoreFuzzyMatch(term, t.label, t.command);
            if (score <= 0) continue;
            QuickOpenItem it;
            it.type = QuickOpenItem::Type::Action;
            it.title = t.label;
            it.description = t.detail + L" - " + t.command;
            it.actionCommand = L"run_task:" + t.command;
            it.iconType = FileIconType::Terminal;
            it.score = score;
            m_filteredItems.push_back(std::move(it));
        }
        if (m_filteredItems.empty()) {
            QuickOpenItem it;
            it.type = QuickOpenItem::Type::Action;
            it.title = L"Open Integrated Terminal to Run Task";
            it.description = L"No package.json / tasks.json / Makefile tasks discovered";
            it.actionCommand = L"cmd_new_terminal";
            it.iconType = FileIconType::Terminal;
            m_filteredItems.push_back(std::move(it));
        }
    } else {
        // File search with optional :line[:col] suffix and fuzzy ranking
        std::wstring fileQuery = m_query;
        int suffixLine = -1;
        int suffixCol = -1;
        size_t firstColon = std::wstring::npos;
        // Skip drive letter colon like "C:\" when scanning for :line[:col]
        for (size_t pos = 0; pos < m_query.size(); ++pos) {
            if (m_query[pos] == L':') {
                if (pos == 1 && iswalpha(m_query[0])) continue;
                int ln = 0, cl = 0;
                if (phide::editor::ParseLineColSpec(m_query.substr(pos + 1), ln, cl) && ln > 0) {
                    firstColon = pos;
                    suffixLine = ln;
                    suffixCol = cl;
                    break;
                }
            }
        }
        if (firstColon != std::wstring::npos) {
            fileQuery = m_query.substr(0, firstColon);
        }

        for (const auto& item : m_allItems) {
            if (item.type != QuickOpenItem::Type::File && fileQuery.empty()) {
                m_filteredItems.push_back(item);
                continue;
            }
            if (item.type != QuickOpenItem::Type::File) continue;
            int score = fileQuery.empty()
                ? (item.isRecent ? 100 : 10)
                : phide::editor::ScoreFuzzyMatch(fileQuery, item.title, item.description, false, item.isRecent);
            if (score > 0) {
                QuickOpenItem ranked = item;
                ranked.score = score;
                if (suffixLine > 0) {
                    ranked.targetLine = suffixLine;
                    ranked.targetCol = suffixCol;
                }
                m_filteredItems.push_back(std::move(ranked));
            }
        }
        std::stable_sort(m_filteredItems.begin(), m_filteredItems.end(),
                         [](const QuickOpenItem& a, const QuickOpenItem& b) {
                             return a.score > b.score;
                         });
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
    float textWidth = 0.0f;
    if (m_query.empty()) {
        const wchar_t* placeholder = L"Search files by name (append : to go to line or @ to go to symbol)";
        pRT->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)),
                      theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get(),
                      rcText, theme.brushTextMuted.Get());
    } else {
        pRT->DrawText(m_query.c_str(), static_cast<UINT32>(m_query.length()),
                      theme.fontBody.Get(), rcText, theme.brushTextPrimary.Get());
        textWidth = static_cast<float>(m_query.length()) * config.ScaleF(7.5f);
    }

    // Active input blinking caret | (Image 5)
    bool caretBlink = ((GetTickCount64() / 500) % 2 == 0);
    if (caretBlink) {
        float caretX = rcText.left + textWidth;
        float caretTop = m_inputBounds.top + config.ScaleF(5.0f);
        float caretBottom = m_inputBounds.bottom - config.ScaleF(5.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> caretBrush;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f) : D2D1::ColorF(0.0f, 0.47f, 0.83f, 1.0f), &caretBrush);
        if (caretBrush) {
            pRT->DrawLine(D2D1::Point2F(caretX, caretTop), D2D1::Point2F(caretX, caretBottom), caretBrush.Get(), 1.8f);
        }
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

bool QuickOpenModal::HandleMouseDown(POINT pt, std::wstring& outSelectedFile, int& outTargetLine, std::wstring& outActionCommand) {
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
            const auto& item = m_filteredItems[i];
            if (item.type == QuickOpenItem::Type::File) {
                outSelectedFile = item.fullPath;
                outTargetLine = item.targetLine;
                m_lastSelectedCol = item.targetCol;
                Close();
                return true;
            } else {
                const auto& actCmd = item.actionCommand;
                if (actCmd.rfind(L"prefix:", 0) == 0) {
                    m_query = actCmd.substr(7);
                    FilterItems();
                    return true;
                } else if (!actCmd.empty()) {
                    outActionCommand = actCmd;
                    Close();
                    return true;
                } else if (item.title.find(L"Show and Run Commands") != std::wstring::npos) {
                    m_query = L">";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Search for Text") != std::wstring::npos) {
                    m_query = L"% ";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Go to Symbol in Editor") != std::wstring::npos) {
                    m_query = L"@";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Start Debugging") != std::wstring::npos) {
                    m_query = L"debug ";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Run Task") != std::wstring::npos) {
                    m_query = L"task ";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"More ?") != std::wstring::npos) {
                    m_query = L"?";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Go to File") != std::wstring::npos) {
                    m_query = L"";
                    FilterItems();
                    return true;
                }
            }
            break;
        }
    }
    return true;
}

bool QuickOpenModal::HandleKeyDown(WPARAM wParam, std::wstring& outSelectedFile, int& outTargetLine, std::wstring& outActionCommand) {
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
                outTargetLine = item.targetLine;
                m_lastSelectedCol = item.targetCol;
                Close();
                return true;
            } else {
                const auto& actCmd = item.actionCommand;
                if (actCmd.rfind(L"prefix:", 0) == 0) {
                    m_query = actCmd.substr(7);
                    FilterItems();
                    return true;
                } else if (!actCmd.empty()) {
                    outActionCommand = actCmd;
                    Close();
                    return true;
                } else if (item.title.find(L"Show and Run Commands") != std::wstring::npos) {
                    m_query = L">";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Search for Text") != std::wstring::npos) {
                    m_query = L"% ";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Go to Symbol in Editor") != std::wstring::npos) {
                    m_query = L"@";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Start Debugging") != std::wstring::npos) {
                    m_query = L"debug ";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Run Task") != std::wstring::npos) {
                    m_query = L"task ";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"More ?") != std::wstring::npos) {
                    m_query = L"?";
                    FilterItems();
                    return true;
                } else if (item.title.find(L"Go to File") != std::wstring::npos) {
                    m_query = L"";
                    FilterItems();
                    return true;
                }
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
