/**
 * @file OpenDocument.hpp
 * @brief Representation of an opened editor tab, buffer, and preview lifecycle state.
 */

#pragma once

#include <string>
#include <set>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <d2d1.h>
#include <wrl/client.h>
#include "phide/editor/Buffer.hpp"
#include "phide/lsp/LspClient.hpp"
#include "phide/editor/NotebookModel.hpp"

namespace phide::editor {

/**
 * @struct OpenDocument
 * @brief Encapsulates a document's filesystem path, text buffer, dirty state, and preview mode.
 */
struct OpenDocument {
    std::wstring filePath;       ///< Full filesystem path (empty for virtual/special views)
    std::wstring fileName;       ///< Display title or basename (e.g. index.php)
    Buffer buffer;               ///< Dedicated text buffer
    std::wstring originalDiskText; ///< Pristine file text on disk / HEAD for accurate clean/dirty tracking
    bool isDirty = false;        ///< True if document has unsaved edits
    bool isPreview = false;      ///< True if single-clicked and not yet edited/pinned (VS Code preview tab)
    bool isSpecialView = false;  ///< True for Keyboard Shortcuts, settings, etc.
    bool isRemoteFile = false;   ///< True if opened via Remote Explorer (SSH/SCP) and synced on save
    std::wstring remoteSshTarget;
    std::wstring remoteSshPort;
    std::wstring remoteSshIdentity;
    std::wstring remoteFilePath;
    std::wstring viewType;       ///< Special view identifier (e.g. L"keybindings", L"course")
    std::wstring courseId;       ///< Course identifier for course special views (e.g. L"python-1")
    D2D1_RECT_F tabBounds{};     ///< Screen bounding box of the tab
    D2D1_RECT_F closeBounds{};   ///< Screen bounding box of the tab close button (×)
    D2D1_RECT_F splitTabBounds{};   ///< Screen bounding box of the tab in secondary/right split pane
    D2D1_RECT_F splitCloseBounds{}; ///< Screen bounding box of tab close button in right split pane

    // Breakpoints for Debugging (F9 / Debug Menu)
    std::vector<size_t> breakpoints; ///< 0-based line indices where breakpoints are set
    size_t debugPausedLine = static_cast<size_t>(-1); ///< 0-based line where debugger is currently paused (-1 if not paused)

    // GitLens Inline Blame and Authorship State
    struct LineBlameInfo {
        std::wstring commitHash;
        std::wstring author;
        std::wstring relativeTime;
        std::wstring summary;
        bool isUncommitted = false;
    };
    std::unordered_map<size_t, LineBlameInfo> gitBlameMap; ///< 0-based buffer line -> LineBlameInfo
    bool gitBlameLoaded = false;

    // Text Selection State (Images 2, 3, 4)
    bool hasSelection = false;          ///< True if text is selected
    CursorPosition selAnchor{ 0, 0 };   ///< Starting anchor of selection
    CursorPosition selEnd{ 0, 0 };      ///< Ending position of selection
    bool isSelecting = false;           ///< True while user is dragging mouse to select text
    std::wstring selectedWord;          ///< When a word/symbol is double-clicked or selected, the word text for multi-occurrence highlight

    // Multi-Cursor / Next Occurrence Selection (Ctrl+D multi-editing)
    struct SelectionRange {
        CursorPosition anchor{ 0, 0 };
        CursorPosition cursor{ 0, 0 };

        void GetRange(size_t& sLine, size_t& sCol, size_t& eLine, size_t& eCol) const noexcept {
            if (anchor.line < cursor.line || (anchor.line == cursor.line && anchor.col <= cursor.col)) {
                sLine = anchor.line; sCol = anchor.col;
                eLine = cursor.line; eCol = cursor.col;
            } else {
                sLine = cursor.line; sCol = cursor.col;
                eLine = anchor.line; eCol = anchor.col;
            }
        }
    };
    std::vector<SelectionRange> secondarySelections;

    [[nodiscard]] bool HasSelection() const noexcept {
        return hasSelection && (selAnchor.line != selEnd.line || selAnchor.col != selEnd.col);
    }

    void ClearSelection() noexcept {
        hasSelection = false;
        isSelecting = false;
        selectedWord.clear();
        secondarySelections.clear();
    }

    void GetSelectionRange(size_t& startLine, size_t& startCol, size_t& endLine, size_t& endCol) const noexcept {
        if (selAnchor.line < selEnd.line || (selAnchor.line == selEnd.line && selAnchor.col <= selEnd.col)) {
            startLine = selAnchor.line; startCol = selAnchor.col;
            endLine = selEnd.line; endCol = selEnd.col;
        } else {
            startLine = selEnd.line; startCol = selEnd.col;
            endLine = selAnchor.line; endCol = selAnchor.col;
        }
    }

    [[nodiscard]] std::wstring GetSelectedText() const {
        if (!HasSelection()) return L"";
        size_t sLine, sCol, eLine, eCol;
        GetSelectionRange(sLine, sCol, eLine, eCol);
        if (sLine == eLine) {
            const auto& line = buffer.GetLine(sLine);
            if (sCol < line.length()) {
                return line.substr(sCol, (std::min)(eCol - sCol, line.length() - sCol));
            }
            return L"";
        }
        std::wstring result;
        for (size_t l = sLine; l <= eLine; ++l) {
            const auto& line = buffer.GetLine(l);
            if (l == sLine) {
                if (sCol < line.length()) result += line.substr(sCol);
            } else if (l == eLine) {
                if (eCol <= line.length()) result += line.substr(0, eCol);
                else result += line;
            } else {
                result += line;
            }
            if (l < eLine) result += L"\n";
        }
        return result;
    }

    void SelectWordAt(size_t lineIdx, size_t colIdx) {
        const auto& line = buffer.GetLine(lineIdx);
        if (line.empty()) return;
        if (colIdx >= line.length()) colIdx = line.length() > 0 ? line.length() - 1 : 0;

        auto isWordChar = [](wchar_t ch) {
            return iswalnum(ch) || ch == L'_' || ch == L'$';
        };

        if (!isWordChar(line[colIdx])) {
            return;
        }

        size_t startCol = colIdx;
        while (startCol > 0 && isWordChar(line[startCol - 1])) {
            startCol--;
        }
        size_t endCol = colIdx;
        while (endCol < line.length() && isWordChar(line[endCol])) {
            endCol++;
        }

        selAnchor = { lineIdx, startCol };
        selEnd = { lineIdx, endCol };
        hasSelection = true;
        buffer.SetCursor(lineIdx, endCol);
        selectedWord = line.substr(startCol, endCol - startCol);
    }

    bool AddNextOccurrence() {
        if (!HasSelection()) {
            SelectWordAt(buffer.GetCursor().line, buffer.GetCursor().col);
            return HasSelection();
        }

        std::wstring text = GetSelectedText();
        if (text.empty()) return false;

        size_t searchLine = buffer.GetCursor().line;
        size_t searchCol = buffer.GetCursor().col;
        if (!secondarySelections.empty()) {
            size_t sl, sc, el, ec;
            secondarySelections.back().GetRange(sl, sc, el, ec);
            searchLine = el;
            searchCol = ec;
        } else {
            size_t sl, sc, el, ec;
            GetSelectionRange(sl, sc, el, ec);
            searchLine = el;
            searchCol = ec;
        }

        size_t totalLines = buffer.GetLineCount();
        bool found = false;
        size_t foundLine = 0, foundCol = 0;

        // Search from (searchLine, searchCol) to end
        for (size_t l = searchLine; l < totalLines && !found; ++l) {
            const auto& line = buffer.GetLine(l);
            size_t startC = (l == searchLine) ? searchCol : 0;
            if (startC <= line.length()) {
                size_t pos = line.find(text, startC);
                if (pos != std::wstring::npos) {
                    found = true;
                    foundLine = l;
                    foundCol = pos;
                }
            }
        }

        // If not found, wrap around from beginning to searchLine
        if (!found) {
            for (size_t l = 0; l <= searchLine && !found; ++l) {
                const auto& line = buffer.GetLine(l);
                size_t limit = (l == searchLine) ? searchCol : line.length();
                size_t pos = line.find(text, 0);
                while (pos != std::wstring::npos && pos < limit) {
                    bool alreadySelected = false;
                    size_t psL, psC, peL, peC;
                    GetSelectionRange(psL, psC, peL, peC);
                    if (l == psL && pos == psC) alreadySelected = true;
                    for (const auto& sec : secondarySelections) {
                        size_t ssL, ssC, seL, seC;
                        sec.GetRange(ssL, ssC, seL, seC);
                        if (l == ssL && pos == ssC) { alreadySelected = true; break; }
                    }
                    if (!alreadySelected) {
                        found = true;
                        foundLine = l;
                        foundCol = pos;
                        break;
                    }
                    pos = line.find(text, pos + 1);
                }
            }
        }

        if (found) {
            SelectionRange newSel;
            newSel.anchor = { foundLine, foundCol };
            newSel.cursor = { foundLine, foundCol + text.length() };
            secondarySelections.push_back(newSel);
            return true;
        }
        return false;
    }

    bool DeleteSelection() {
        if (!HasSelection()) return false;
        size_t sLine, sCol, eLine, eCol;
        GetSelectionRange(sLine, sCol, eLine, eCol);
        buffer.DeleteRange(sLine, sCol, eLine, eCol);
        ClearSelection();
        return true;
    }

    // Viewport Scrolling State
    size_t scrollLine = 0;              ///< Top-most visible visual line index
    float scrollOffsetX = 0.0f;         ///< Horizontal scroll offset in pixels
    float maxContentWidth = 0.0f;       ///< Maximum content width in pixels across lines
    float markdownScrollY = 0.0f;       ///< Vertical scroll offset in pixels for Markdown Preview
    float markdownMaxScrollY = 0.0f;    ///< Max vertical scroll range in pixels for Markdown Preview
    std::wstring extensionId;           ///< Identifier for extension details view (e.g. L"dsznajder.es7-react-js-snippets")
    int extensionSubTab = 0;            ///< 0 = DETAILS, 1 = FEATURES, 2 = CHANGELOG
    float extensionScrollY = 0.0f;      ///< Vertical scroll offset for Extension Detail view
    float extensionMaxScrollY = 0.0f;   ///< Max vertical scroll range for Extension Detail view
    D2D1_RECT_F scrollbarTrack{};       ///< Bounding box of the vertical scrollbar track
    D2D1_RECT_F scrollbarThumb{};       ///< Bounding box of the vertical scrollbar thumb
    bool isDraggingScrollbar = false;   ///< True while user is actively dragging the scrollbar thumb
    float dragStartY = 0.0f;            ///< Mouse Y coordinate at start of scrollbar drag
    size_t dragStartScrollLine = 0;     ///< scrollLine at start of scrollbar drag

    // Horizontal Scrollbar State
    D2D1_RECT_F hScrollbarTrack{};      ///< Bounding box of horizontal scrollbar track
    D2D1_RECT_F hScrollbarThumb{};      ///< Bounding box of horizontal scrollbar thumb
    bool isDraggingHScrollbar = false;  ///< True while user is actively dragging the horizontal scrollbar
    float dragStartHX = 0.0f;           ///< Mouse X coordinate at start of horizontal scrollbar drag
    float dragStartScrollOffsetX = 0.0f;///< scrollOffsetX at start of horizontal scrollbar drag

    // Target Line / Search Jump Flash State
    size_t targetHighlightLine = static_cast<size_t>(-1);
    size_t targetHighlightCol = 0;
    size_t targetHighlightLen = 0;
    ULONGLONG targetHighlightTime = 0;   ///< Timestamp when jump occurred

    // Minimap Interaction State
    D2D1_RECT_F minimapTrack{};         ///< Bounding box of the minimap on the right of the editor
    D2D1_RECT_F minimapSlider{};        ///< Bounding box of the visible viewport slider in the minimap
    bool isDraggingMinimap = false;     ///< True while user is actively dragging the minimap slider
    float minimapDragStartY = 0.0f;     ///< Mouse Y at start of drag
    float minimapGrabOffsetY = 0.0f;    ///< Offset from slider top when grabbed
    size_t minimapDragStartLine = 0;    ///< scrollLine at start of drag

    // Code Folding State (Images 2 & 3)
    std::set<size_t> foldedLines;       ///< Set of starting line indices (0-based buffer line) that are folded

    // Inlay Hints & Ghost Completion State (Images 1, 2, 3, 5)
    struct InlayHint {
        size_t line = 0;              ///< 0-based buffer line index
        size_t col = 0;               ///< Column index where hint badge appears
        std::wstring label;           ///< Badge text e.g. L": {shape, __add__}" or L"Tab to complete"
        bool isGhostText = false;      ///< True for inline completion ghost text
    };
    std::vector<InlayHint> inlayHints;

    // Syntax Error Diagnostics & Hover Popover State
    std::vector<phide::lsp::Diagnostic> diagnostics;   ///< Diagnostic errors and warnings for this document
    size_t hoveredDiagnosticLine = static_cast<size_t>(-1); ///< Line of currently hovered diagnostic error
    D2D1_RECT_F diagnosticTooltipBounds{};             ///< Hitbox of floating diagnostic error tooltip

    // Picture Viewer State (Image 2)
    bool isImageView = false;           ///< True if this document is displaying a picture/image file
    Microsoft::WRL::ComPtr<ID2D1Bitmap> pImageBitmap; ///< Decoded Direct2D bitmap
    UINT imageWidth = 0;                ///< Pixel width of the decoded image
    UINT imageHeight = 0;               ///< Pixel height of the decoded image
    uint64_t imageFileSize = 0;         ///< File size on disk in bytes

    // Unsupported Binary File View State (Image 2)
    bool isUnsupportedBinary = false;    ///< True if this document is an unsupported binary file (e.g. .exe, .dll, video)
    uint64_t binaryFileSize = 0;         ///< File size on disk in bytes
    std::wstring binaryFileType;         ///< Human-readable binary type descriptor
    D2D1_RECT_F unsupportedActionBtn{};  ///< Bounding box of "Reveal in File Explorer" button

    // Folder Explorer View State (when a folder is dragged/opened in an editor tab)
    struct FolderEntryItem {
        std::wstring name;           ///< Entry filename or folder name
        std::wstring fullPath;       ///< Full filesystem path
        std::wstring typeLabel;      ///< Human-readable file/folder type
        std::wstring sizeLabel;      ///< Formatted size or child item count
        std::wstring modifiedLabel;  ///< Formatted last-modified timestamp
        uint64_t fileSize = 0;       ///< Raw size in bytes (for files)
        bool isFolder = false;       ///< True if entry is a directory
        bool isParentLink = false;   ///< True if entry is the ".." parent folder row
        D2D1_RECT_F rowBounds{};     ///< Cached hit-test bounds from last render
    };
    bool isFolderView = false;                       ///< True if this document displays a Folder Explorer view
    std::vector<FolderEntryItem> folderEntries;      ///< Cached directory entries (folders first, then files)
    int folderHoveredIdx = -1;                       ///< Hovered entry index in Folder Explorer view
    int folderSelectedIdx = -1;                      ///< Selected entry index in Folder Explorer view
    float folderScrollY = 0.0f;                      ///< Vertical scroll offset in pixels
    float folderMaxScrollY = 0.0f;                   ///< Maximum scroll offset in pixels
    bool folderGridView = false;                     ///< False = Details list view, True = Grid/Tiles view
    int folderHoveredBtn = 0;                        ///< 1 = Up, 2 = Refresh, 3 = View toggle, 4 = Reveal in Explorer
    D2D1_RECT_F folderUpBtnBounds{};                 ///< Hitbox for "Up" parent button
    D2D1_RECT_F folderRefreshBtnBounds{};            ///< Hitbox for "Refresh" button
    D2D1_RECT_F folderViewToggleBtnBounds{};         ///< Hitbox for List/Grid view toggle button
    D2D1_RECT_F folderRevealBtnBounds{};             ///< Hitbox for "Reveal in File Explorer" button

    static std::wstring FormatFileTypeLabel(const std::wstring& filename, bool isDir) {
        if (isDir) return L"File folder";
        std::wstring lower = filename;
        for (auto& c : lower) c = towlower(c);
        if (lower == L"package.json" || lower == L"composer.json" || lower == L"tsconfig.json") return L"Configuration JSON";
        if (lower == L".gitignore" || lower == L".gitattributes") return L"Git Configuration";
        if (lower == L"dockerfile") return L"Docker Configuration";
        auto dot = lower.find_last_of(L'.');
        if (dot == std::wstring::npos) return L"File";
        std::wstring ext = lower.substr(dot);
        if (ext == L".tsx") return L"TypeScript React";
        if (ext == L".jsx") return L"JavaScript React";
        if (ext == L".ts" || ext == L".mts" || ext == L".cts") return L"TypeScript Source";
        if (ext == L".js" || ext == L".mjs" || ext == L".cjs") return L"JavaScript Source";
        if (ext == L".css" || ext == L".scss" || ext == L".sass" || ext == L".less") return L"Stylesheet";
        if (ext == L".html" || ext == L".htm") return L"HTML Document";
        if (ext == L".json" || ext == L".jsonc") return L"JSON Document";
        if (ext == L".md" || ext == L".markdown" || ext == L".mdx") return L"Markdown Document";
        if (ext == L".php" || ext == L".phtml") return L"PHP Script";
        if (ext == L".py" || ext == L".pyw") return L"Python Script";
        if (ext == L".ipynb") return L"Jupyter Notebook";
        if (ext == L".cpp" || ext == L".cc" || ext == L".cxx" || ext == L".c") return L"C/C++ Source";
        if (ext == L".hpp" || ext == L".hxx" || ext == L".h") return L"C/C++ Header";
        if (ext == L".sql") return L"SQL Script";
        if (ext == L".xml" || ext == L".svg") return L"XML / Vector Graphic";
        if (ext == L".yml" || ext == L".yaml") return L"YAML Document";
        if (ext == L".toml" || ext == L".ini" || ext == L".env") return L"Configuration File";
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" || ext == L".webp" || ext == L".ico" || ext == L".bmp") return L"Image File";
        if (ext == L".sh" || ext == L".bat" || ext == L".cmd" || ext == L".ps1") return L"Shell Script";
        if (ext.length() > 1) {
            std::wstring upperExt = ext.substr(1);
            for (auto& c : upperExt) c = towupper(c);
            return upperExt + L" File";
        }
        return L"File";
    }

    static std::wstring FormatByteSize(uint64_t bytes) {
        if (bytes < 1024) return std::to_wstring(bytes) + L" B";
        if (bytes < 1024 * 1024) {
            uint64_t kb10 = (bytes * 10) / 1024;
            return std::to_wstring(kb10 / 10) + L"." + std::to_wstring(kb10 % 10) + L" KB";
        }
        uint64_t mb10 = (bytes * 10) / (1024 * 1024);
        return std::to_wstring(mb10 / 10) + L"." + std::to_wstring(mb10 % 10) + L" MB";
    }

    void RefreshFolderEntries() {
        folderEntries.clear();
        folderHoveredIdx = -1;
        folderSelectedIdx = -1;
        if (filePath.empty()) return;

        std::error_code ec;
        std::filesystem::path dirPath(filePath);
        if (!std::filesystem::exists(dirPath, ec) || !std::filesystem::is_directory(dirPath, ec)) {
            return;
        }

        // Add ".." parent folder entry if parent exists and is different from current path
        std::filesystem::path parentP = dirPath.parent_path();
        if (!parentP.empty() && parentP != dirPath && std::filesystem::exists(parentP, ec)) {
            FolderEntryItem upItem;
            upItem.name = L"..";
            upItem.fullPath = parentP.wstring();
            upItem.typeLabel = L"Parent folder";
            upItem.isFolder = true;
            upItem.isParentLink = true;
            folderEntries.push_back(std::move(upItem));
        }

        std::vector<FolderEntryItem> subDirs;
        std::vector<FolderEntryItem> files;
        for (std::filesystem::directory_iterator it(dirPath, std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            const auto& entry = *it;
            FolderEntryItem item;
            item.name = entry.path().filename().wstring();
            if (item.name.empty()) continue;
            item.fullPath = entry.path().wstring();
            item.isFolder = entry.is_directory(ec);
            item.typeLabel = FormatFileTypeLabel(item.name, item.isFolder);

            // Format last write time using Win32 GetFileAttributesExW for accurate local time
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExW(item.fullPath.c_str(), GetFileExInfoStandard, &fad)) {
                FILETIME localFt{};
                SYSTEMTIME st{};
                if (FileTimeToLocalFileTime(&fad.ftLastWriteTime, &localFt) &&
                    FileTimeToSystemTime(&localFt, &st)) {
                    wchar_t timeBuf[64]{};
                    swprintf_s(timeBuf, L"%04d-%02d-%02d %02d:%02d",
                               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
                    item.modifiedLabel = timeBuf;
                }
                if (!item.isFolder) {
                    ULARGE_INTEGER uli{};
                    uli.LowPart = fad.nFileSizeLow;
                    uli.HighPart = fad.nFileSizeHigh;
                    item.fileSize = uli.QuadPart;
                    item.sizeLabel = FormatByteSize(item.fileSize);
                }
            }

            if (item.isFolder) {
                size_t childCount = 0;
                std::error_code childEc;
                for (std::filesystem::directory_iterator cit(entry.path(), std::filesystem::directory_options::skip_permission_denied, childEc), cend;
                     cit != cend && !childEc; cit.increment(childEc)) {
                    ++childCount;
                    if (childCount >= 500) break;
                }
                if (childCount >= 500) {
                    item.sizeLabel = L"500+ items";
                } else if (childCount == 1) {
                    item.sizeLabel = L"1 item";
                } else {
                    item.sizeLabel = std::to_wstring(childCount) + L" items";
                }
                subDirs.push_back(std::move(item));
            } else {
                files.push_back(std::move(item));
            }
        }

        auto cmpName = [](const FolderEntryItem& a, const FolderEntryItem& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        };
        std::sort(subDirs.begin(), subDirs.end(), cmpName);
        std::sort(files.begin(), files.end(), cmpName);

        folderEntries.reserve(folderEntries.size() + subDirs.size() + files.size());
        for (auto& d : subDirs) folderEntries.push_back(std::move(d));
        for (auto& f : files) folderEntries.push_back(std::move(f));
        folderEntries.shrink_to_fit();
    }

    // Git Diff & Deleted File State (Images 2, 3, 4, 5)
    bool isDiffView = false;             ///< True if this document is displaying side-by-side diff
    bool isDiffBinary = false;           ///< True if diff target is a binary file (e.g. .exe, .dll) not suitable for text comparison
    std::wstring diffBinaryFileName;     ///< File name of the binary diff target
    bool isDeletedInGit = false;         ///< True if this document was deleted in git and loaded from HEAD
    bool isDiffLeftReadOnly = true;      ///< True if left pane is read-only (always true for working tree diff)
    bool isDiffRightReadOnly = false;    ///< True if right pane is read-only (true for historical commit diffs)
    int diffActivePane = 1;              ///< 0 = Left (Original/HEAD), 1 = Right (Working Tree)
    Buffer diffOriginalBuffer;           ///< Left pane buffer (HEAD or parent commit)
    std::wstring diffOriginalTitle;      ///< Left pane title (e.g. "LucideIcons.cpp (HEAD)")
    std::wstring diffWorkingTitle;       ///< Right pane title (e.g. "LucideIcons.cpp (Working Tree)")
    std::set<size_t> diffAddedLines;     ///< Right-side line indices (0-based) added relative to HEAD
    std::set<size_t> diffModifiedLines;  ///< Right-side line indices (0-based) modified relative to HEAD
    std::set<size_t> diffDeletedLines;   ///< Left-side line indices (0-based) where deletions occurred
    std::set<size_t> diffDeletedGutterMarkers; ///< Right-side line index where a deletion occurred in the gutter

    struct DiffHunkItem {
        size_t leftStart = 0;   // 0-based line index in left buffer
        size_t leftCount = 0;   // number of lines in left buffer
        size_t rightStart = 0;  // 0-based line index in right buffer
        size_t rightCount = 0;  // number of lines in right buffer
        enum class HunkType { Modified, Added, Deleted } type = HunkType::Modified;
    };
    std::vector<DiffHunkItem> diffHunks; ///< Structured diff hunks between left and right buffers

    // Multi-File Commit Diff View (Image 4)
    struct MultiFileDiffEntry {
        std::wstring filePath;           ///< Relative path (e.g. "include/phide/app/Window.hpp")
        std::wstring fileName;           ///< Basename (e.g. "Window.hpp")
        std::wstring relDir;             ///< Directory path (e.g. "include\\phide\\app")
        std::wstring status;             ///< Status code (e.g. "M", "A", "D")
        bool isCollapsed = false;        ///< Collapsed accordion state
        bool isBinary = false;           ///< True for binary files (e.g. .exe, .png)
        Buffer originalBuffer;           // Parent commit content
        Buffer workingBuffer;            // Commit content
        std::set<size_t> addedLines;
        std::set<size_t> modifiedLines;
        std::set<size_t> deletedLines;
        std::vector<DiffHunkItem> hunks;
        D2D1_RECT_F headerBounds{};      // Clickable file header banner
    };
    bool isMultiFileDiff = false;        ///< True if this document renders all files changed in a commit (Image 4)
    std::wstring commitHash;             ///< Commit hash for multi-file commit diff
    std::wstring commitAuthor;           ///< Commit author
    std::wstring commitDate;             ///< Commit date
    std::wstring commitMessage;          ///< Full commit message
    std::vector<MultiFileDiffEntry> multiFileDiffs; ///< List of all files changed in this commit
    std::shared_ptr<NotebookDocument> notebookDoc; ///< Attached interactive notebook model when viewType == L"notebook"

    // CSS Peek Hover Info (Image 1)
    struct CssPeekInfo {
        bool active = false;
        std::wstring selector;
        std::vector<std::wstring> lines;
        size_t targetLine = 0;
        size_t targetCol = 0;
        D2D1_RECT_F anchorRect{};
    } cssPeek;

    // Import Cost annotations cache (line index -> annotation text e.g. "   71.7 KB (gzipped: 25.9 KB)")
    std::unordered_map<size_t, std::wstring> importCostMap;
    bool importCostCalculated = false;

    // External Disk-Change Detector state
    std::filesystem::file_time_type lastDiskWriteTime{};
    bool hasLastDiskWriteTime = false;
    bool hasExternalDiskConflict = false;
    D2D1_RECT_F conflictBannerBounds{};
    D2D1_RECT_F conflictReloadBtnBounds{};
    D2D1_RECT_F conflictDiffBtnBounds{};
    D2D1_RECT_F conflictOverwriteBtnBounds{};
};

/**
 * @struct EditorGroup
 * @brief Manages an independent editor column/pane with its own list of open tabs and active document.
 */
struct EditorGroup {
    std::vector<OpenDocument> docs;      ///< Independent list of document tabs open in this editor column
    int activeDocIdx = -1;               ///< Index of the currently active document tab in this group
    D2D1_RECT_F bounds{};                ///< Screen bounding rectangle of this editor pane
    float tabScrollOffset = 0.0f;        ///< Horizontal scroll offset in pixels for tab strip
    float totalTabsWidth = 0.0f;         ///< Total width of all tabs in this strip
    D2D1_RECT_F tabScrollTrack{};        ///< Hit-test bounds for tab horizontal scroll track
    D2D1_RECT_F tabScrollThumb{};        ///< Hit-test bounds for tab horizontal scroll thumb
    bool isDraggingTabScroll = false;    ///< True when tab scroll thumb is currently being dragged
    float dragTabScrollStartX = 0.0f;    ///< Starting mouse X when dragging tab scroll
    float dragTabScrollStartOffset = 0.0f; ///< Starting tabScrollOffset when dragging tab scroll
    int lastScrolledActiveDocIdx = -1;   ///< Tracks last active doc idx to only auto-scroll into view when active tab changes
};

/**
 * @enum EditorSplitLayout
 * @brief Windows-style 2D multi-pane arrangements for editor groups.
 */
enum class EditorSplitLayout : int {
    Single = 0,             ///< 1 Pane: [0] full canvas
    TwoColumns = 1,         ///< 2 Panes: [0] Left, [1] Right
    ThreeColumns = 2,       ///< 3 Panes: [0] Left, [1] Center, [2] Right
    TwoRows = 3,            ///< 2 Panes: [0] Top, [1] Bottom
    ThreeRows = 4,          ///< 3 Panes: [0] Top, [1] Middle, [2] Bottom
    LeftSplitRightFull = 5, ///< 3 Panes: [0] Top-Left, [1] Bottom-Left, [2] Right-Full (Windows Snap / User screenshot)
    LeftFullRightSplit = 6, ///< 3 Panes: [0] Left-Full, [1] Top-Right, [2] Bottom-Right
    TopFullBottomSplit = 7, ///< 3 Panes: [0] Top-Full, [1] Bottom-Left, [2] Bottom-Right
    TopSplitBottomFull = 8, ///< 3 Panes: [0] Top-Left, [1] Top-Right, [2] Bottom-Full
    Grid2x2 = 9             ///< 4 Panes: [0] Top-Left, [1] Bottom-Left, [2] Top-Right, [3] Bottom-Right
};

/**
 * @enum TabSnapZone
 * @brief Target drop zone when dragging an editor tab across the workspace.
 */
enum class TabSnapZone : int {
    None = 0,
    TabBar = 1,             ///< Reorder/insert into target pane's tab strip
    Center = 2,             ///< Drop into existing target pane
    Left = 3,               ///< Snap to Left half
    Right = 4,              ///< Snap to Right half
    Top = 5,                ///< Snap to Top half
    Bottom = 6,             ///< Snap to Bottom half
    TopLeft = 7,            ///< Snap to Top-Left quadrant (LeftSplitRightFull / Grid2x2)
    BottomLeft = 8,         ///< Snap to Bottom-Left quadrant (LeftSplitRightFull / Grid2x2)
    TopRight = 9,           ///< Snap to Top-Right quadrant (LeftFullRightSplit / Grid2x2)
    BottomRight = 10,       ///< Snap to Bottom-Right quadrant (LeftFullRightSplit / Grid2x2)
    SnapBarSlot = 11        ///< Dropped directly onto a slot in the top-center Windows Snap Layout Bar
};

/**
 * @struct EditorSplitterSegment
 * @brief Lightweight stack-allocated descriptor for a draggable divider between panes.
 */
struct EditorSplitterSegment {
    int id = 0;             ///< 1 = X1, 2 = X2, 3 = Y1, 4 = Y2
    bool isVertical = true; ///< true = vertical divider (IDC_SIZEWE), false = horizontal divider (IDC_SIZENS)
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
};

/**
 * @struct SnapBarSlot
 * @brief Stack-allocated descriptor for a single target box inside the Windows 11 Snap Layout Bar.
 */
struct SnapBarSlot {
    EditorSplitLayout layout = EditorSplitLayout::TwoColumns;
    int zoneIdx = 0;
    D2D1_RECT_F miniRect{};
    D2D1_RECT_F previewRect{};
};

/**
 * @struct EditorSplitState
 * @brief Zero-allocation 2D split layout engine storing normalized splitter ratios.
 */
struct EditorSplitState {
    EditorSplitLayout layout = EditorSplitLayout::Single;
    float splitX1 = 0.50f;   ///< Primary vertical split ratio [0.15 .. 0.85]
    float splitX2 = 0.6667f; ///< Secondary vertical split ratio (for ThreeColumns)
    float splitY1 = 0.50f;   ///< Primary horizontal split ratio (Left column Y or Top/Bottom Y)
    float splitY2 = 0.50f;   ///< Secondary horizontal split ratio (Right column Y or ThreeRows Y2)

    static constexpr int GetLayoutGroupCount(EditorSplitLayout mode) noexcept {
        switch (mode) {
            case EditorSplitLayout::Single: return 1;
            case EditorSplitLayout::TwoColumns:
            case EditorSplitLayout::TwoRows: return 2;
            case EditorSplitLayout::ThreeColumns:
            case EditorSplitLayout::ThreeRows:
            case EditorSplitLayout::LeftSplitRightFull:
            case EditorSplitLayout::LeftFullRightSplit:
            case EditorSplitLayout::TopFullBottomSplit:
            case EditorSplitLayout::TopSplitBottomFull: return 3;
            case EditorSplitLayout::Grid2x2: return 4;
        }
        return 1;
    }

    void NormalizeLayoutForCount(size_t count) noexcept {
        if (count <= 1) {
            layout = EditorSplitLayout::Single;
        } else if (count == 2) {
            if (layout != EditorSplitLayout::TwoColumns && layout != EditorSplitLayout::TwoRows) {
                layout = EditorSplitLayout::TwoColumns;
            }
        } else if (count == 3) {
            if (layout != EditorSplitLayout::LeftSplitRightFull &&
                layout != EditorSplitLayout::LeftFullRightSplit &&
                layout != EditorSplitLayout::ThreeColumns &&
                layout != EditorSplitLayout::ThreeRows &&
                layout != EditorSplitLayout::TopFullBottomSplit &&
                layout != EditorSplitLayout::TopSplitBottomFull) {
                layout = EditorSplitLayout::LeftSplitRightFull;
            }
        } else {
            layout = EditorSplitLayout::Grid2x2;
        }
    }

    void ComputeGroupBounds(std::vector<EditorGroup>& groups,
                            float canvasLeft, float topY, float canvasWidth, float bottomY,
                            EditorSplitterSegment* outSegments = nullptr,
                            size_t* outSegmentCount = nullptr) noexcept {
        size_t count = groups.size();
        if (outSegmentCount) *outSegmentCount = 0;
        if (count == 0) return;

        NormalizeLayoutForCount(count);
        const float rightX = canvasLeft + (std::max)(20.0f, canvasWidth);
        const float totalH = (std::max)(20.0f, bottomY - topY);

        auto addSeg = [&](int id, bool isVert, float x1, float y1, float x2, float y2) {
            if (outSegments && outSegmentCount && *outSegmentCount < 4) {
                outSegments[*outSegmentCount] = { id, isVert, x1, y1, x2, y2 };
                (*outSegmentCount)++;
            }
        };

        if (count == 1) {
            groups[0].bounds = D2D1::RectF(canvasLeft, topY, rightX, bottomY);
            return;
        }

        if (count == 2) {
            if (layout == EditorSplitLayout::TwoRows) {
                splitY1 = (std::clamp)(splitY1, 0.15f, 0.85f);
                float y1 = topY + std::round(totalH * splitY1);
                groups[0].bounds = D2D1::RectF(canvasLeft, topY, rightX, y1);
                groups[1].bounds = D2D1::RectF(canvasLeft, y1 + 1.0f, rightX, bottomY);
                addSeg(3, false, canvasLeft, y1 + 0.5f, rightX, y1 + 0.5f);
            } else {
                splitX1 = (std::clamp)(splitX1, 0.15f, 0.85f);
                float x1 = canvasLeft + std::round(canvasWidth * splitX1);
                groups[0].bounds = D2D1::RectF(canvasLeft, topY, x1, bottomY);
                groups[1].bounds = D2D1::RectF(x1 + 1.0f, topY, rightX, bottomY);
                addSeg(1, true, x1 + 0.5f, topY, x1 + 0.5f, bottomY);
            }
            return;
        }

        if (count == 3) {
            switch (layout) {
                case EditorSplitLayout::LeftSplitRightFull: {
                    splitX1 = (std::clamp)(splitX1, 0.15f, 0.85f);
                    splitY1 = (std::clamp)(splitY1, 0.15f, 0.85f);
                    float x1 = canvasLeft + std::round(canvasWidth * splitX1);
                    float y1 = topY + std::round(totalH * splitY1);
                    groups[0].bounds = D2D1::RectF(canvasLeft, topY, x1, y1);
                    groups[1].bounds = D2D1::RectF(canvasLeft, y1 + 1.0f, x1, bottomY);
                    groups[2].bounds = D2D1::RectF(x1 + 1.0f, topY, rightX, bottomY);
                    addSeg(1, true, x1 + 0.5f, topY, x1 + 0.5f, bottomY);
                    addSeg(3, false, canvasLeft, y1 + 0.5f, x1, y1 + 0.5f);
                    break;
                }
                case EditorSplitLayout::LeftFullRightSplit: {
                    splitX1 = (std::clamp)(splitX1, 0.15f, 0.85f);
                    splitY2 = (std::clamp)(splitY2, 0.15f, 0.85f);
                    float x1 = canvasLeft + std::round(canvasWidth * splitX1);
                    float y2 = topY + std::round(totalH * splitY2);
                    groups[0].bounds = D2D1::RectF(canvasLeft, topY, x1, bottomY);
                    groups[1].bounds = D2D1::RectF(x1 + 1.0f, topY, rightX, y2);
                    groups[2].bounds = D2D1::RectF(x1 + 1.0f, y2 + 1.0f, rightX, bottomY);
                    addSeg(1, true, x1 + 0.5f, topY, x1 + 0.5f, bottomY);
                    addSeg(4, false, x1 + 1.0f, y2 + 0.5f, rightX, y2 + 0.5f);
                    break;
                }
                case EditorSplitLayout::TopFullBottomSplit: {
                    splitY1 = (std::clamp)(splitY1, 0.15f, 0.85f);
                    splitX1 = (std::clamp)(splitX1, 0.15f, 0.85f);
                    float y1 = topY + std::round(totalH * splitY1);
                    float x1 = canvasLeft + std::round(canvasWidth * splitX1);
                    groups[0].bounds = D2D1::RectF(canvasLeft, topY, rightX, y1);
                    groups[1].bounds = D2D1::RectF(canvasLeft, y1 + 1.0f, x1, bottomY);
                    groups[2].bounds = D2D1::RectF(x1 + 1.0f, y1 + 1.0f, rightX, bottomY);
                    addSeg(3, false, canvasLeft, y1 + 0.5f, rightX, y1 + 0.5f);
                    addSeg(1, true, x1 + 0.5f, y1 + 1.0f, x1 + 0.5f, bottomY);
                    break;
                }
                case EditorSplitLayout::TopSplitBottomFull: {
                    splitY1 = (std::clamp)(splitY1, 0.15f, 0.85f);
                    splitX1 = (std::clamp)(splitX1, 0.15f, 0.85f);
                    float y1 = topY + std::round(totalH * splitY1);
                    float x1 = canvasLeft + std::round(canvasWidth * splitX1);
                    groups[0].bounds = D2D1::RectF(canvasLeft, topY, x1, y1);
                    groups[1].bounds = D2D1::RectF(x1 + 1.0f, topY, rightX, y1);
                    groups[2].bounds = D2D1::RectF(canvasLeft, y1 + 1.0f, rightX, bottomY);
                    addSeg(3, false, canvasLeft, y1 + 0.5f, rightX, y1 + 0.5f);
                    addSeg(1, true, x1 + 0.5f, topY, x1 + 0.5f, y1);
                    break;
                }
                case EditorSplitLayout::ThreeRows: {
                    splitY1 = (std::clamp)(splitY1, 0.14f, 0.70f);
                    splitY2 = (std::clamp)(splitY2, splitY1 + 0.14f, 0.86f);
                    float y1 = topY + std::round(totalH * splitY1);
                    float y2 = topY + std::round(totalH * splitY2);
                    groups[0].bounds = D2D1::RectF(canvasLeft, topY, rightX, y1);
                    groups[1].bounds = D2D1::RectF(canvasLeft, y1 + 1.0f, rightX, y2);
                    groups[2].bounds = D2D1::RectF(canvasLeft, y2 + 1.0f, rightX, bottomY);
                    addSeg(3, false, canvasLeft, y1 + 0.5f, rightX, y1 + 0.5f);
                    addSeg(4, false, canvasLeft, y2 + 0.5f, rightX, y2 + 0.5f);
                    break;
                }
                case EditorSplitLayout::ThreeColumns:
                default: {
                    splitX1 = (std::clamp)(splitX1, 0.14f, 0.70f);
                    splitX2 = (std::clamp)(splitX2, splitX1 + 0.14f, 0.86f);
                    float x1 = canvasLeft + std::round(canvasWidth * splitX1);
                    float x2 = canvasLeft + std::round(canvasWidth * splitX2);
                    groups[0].bounds = D2D1::RectF(canvasLeft, topY, x1, bottomY);
                    groups[1].bounds = D2D1::RectF(x1 + 1.0f, topY, x2, bottomY);
                    groups[2].bounds = D2D1::RectF(x2 + 1.0f, topY, rightX, bottomY);
                    addSeg(1, true, x1 + 0.5f, topY, x1 + 0.5f, bottomY);
                    addSeg(2, true, x2 + 0.5f, topY, x2 + 0.5f, bottomY);
                    break;
                }
            }
            return;
        }

        // count >= 4: Grid2x2
        splitX1 = (std::clamp)(splitX1, 0.15f, 0.85f);
        splitY1 = (std::clamp)(splitY1, 0.15f, 0.85f);
        splitY2 = (std::clamp)(splitY2, 0.15f, 0.85f);
        float x1 = canvasLeft + std::round(canvasWidth * splitX1);
        float y1 = topY + std::round(totalH * splitY1);
        float y2 = topY + std::round(totalH * splitY2);
        groups[0].bounds = D2D1::RectF(canvasLeft, topY, x1, y1);
        groups[1].bounds = D2D1::RectF(canvasLeft, y1 + 1.0f, x1, bottomY);
        groups[2].bounds = D2D1::RectF(x1 + 1.0f, topY, rightX, y2);
        groups[3].bounds = D2D1::RectF(x1 + 1.0f, y2 + 1.0f, rightX, bottomY);
        addSeg(1, true, x1 + 0.5f, topY, x1 + 0.5f, bottomY);
        addSeg(3, false, canvasLeft, y1 + 0.5f, x1, y1 + 0.5f);
        addSeg(4, false, x1 + 1.0f, y2 + 0.5f, rightX, y2 + 0.5f);
    }

    [[nodiscard]] int HitTestSplitter(POINT pt, const std::vector<EditorGroup>& groups,
                                      float canvasLeft, float topY, float canvasWidth, float bottomY,
                                      float tolerance = 5.0f) const noexcept {
        if (groups.size() <= 1) return 0;
        EditorSplitState temp = *this;
        std::vector<EditorGroup> dummy(groups.size());
        EditorSplitterSegment segs[4]{};
        size_t segCount = 0;
        temp.ComputeGroupBounds(dummy, canvasLeft, topY, canvasWidth, bottomY, segs, &segCount);

        int hitVert = 0;
        int hitHorz = 0;
        float px = static_cast<float>(pt.x);
        float py = static_cast<float>(pt.y);

        for (size_t i = 0; i < segCount; ++i) {
            const auto& s = segs[i];
            if (s.isVertical) {
                if (std::abs(px - s.x1) <= tolerance && py >= s.y1 - tolerance && py <= s.y2 + tolerance) {
                    hitVert = s.id;
                }
            } else {
                if (std::abs(py - s.y1) <= tolerance && px >= s.x1 - tolerance && px <= s.x2 + tolerance) {
                    hitHorz = s.id;
                }
            }
        }
        if (hitVert > 0 && hitHorz > 0) return 5; // 2D Intersection (both X and Y)
        if (hitVert > 0) return hitVert;
        if (hitHorz > 0) return hitHorz;
        return 0;
    }

    static void BuildSnapBarLayout(float canvasLeft, float topY, float canvasWidth, float bottomY, float scale,
                                   SnapBarSlot* outSlots, size_t& outSlotCount, D2D1_RECT_F& outBarBounds) noexcept {
        outSlotCount = 0;
        const float itemW = 48.0f * scale;
        const float itemH = 32.0f * scale;
        const float gap = 8.0f * scale;
        const float padX = 10.0f * scale;
        const float padY = 7.0f * scale;
        const int numLayouts = 5;
        const float barW = padX * 2.0f + numLayouts * itemW + (numLayouts - 1) * gap;
        const float barH = padY * 2.0f + itemH;
        const float barX = canvasLeft + (canvasWidth - barW) * 0.5f;
        const float barY = topY + 6.0f * scale;
        outBarBounds = D2D1::RectF(barX, barY, barX + barW, barY + barH);

        const float rightX = canvasLeft + canvasWidth;
        const float midX = canvasLeft + canvasWidth * 0.5f;
        const float midY = topY + (bottomY - topY) * 0.5f;
        const float innerGap = 2.0f * scale;

        auto addSlot = [&](EditorSplitLayout lay, int zIdx, D2D1_RECT_F mini, D2D1_RECT_F prev) {
            if (outSlots && outSlotCount < 16) {
                outSlots[outSlotCount++] = { lay, zIdx, mini, prev };
            }
        };

        // 0: TwoColumns [ Left | Right ]
        {
            float lx = barX + padX + 0 * (itemW + gap);
            float ly = barY + padY;
            float hw = (itemW - innerGap) * 0.5f;
            addSlot(EditorSplitLayout::TwoColumns, 0,
                    D2D1::RectF(lx, ly, lx + hw, ly + itemH),
                    D2D1::RectF(canvasLeft, topY, midX, bottomY));
            addSlot(EditorSplitLayout::TwoColumns, 1,
                    D2D1::RectF(lx + hw + innerGap, ly, lx + itemW, ly + itemH),
                    D2D1::RectF(midX, topY, rightX, bottomY));
        }
        // 1: LeftSplitRightFull [ Top-Left / Bottom-Left | Right ] (User's screenshot!)
        {
            float lx = barX + padX + 1 * (itemW + gap);
            float ly = barY + padY;
            float hw = (itemW - innerGap) * 0.5f;
            float hh = (itemH - innerGap) * 0.5f;
            addSlot(EditorSplitLayout::LeftSplitRightFull, 0,
                    D2D1::RectF(lx, ly, lx + hw, ly + hh),
                    D2D1::RectF(canvasLeft, topY, midX, midY));
            addSlot(EditorSplitLayout::LeftSplitRightFull, 1,
                    D2D1::RectF(lx, ly + hh + innerGap, lx + hw, ly + itemH),
                    D2D1::RectF(canvasLeft, midY, midX, bottomY));
            addSlot(EditorSplitLayout::LeftSplitRightFull, 2,
                    D2D1::RectF(lx + hw + innerGap, ly, lx + itemW, ly + itemH),
                    D2D1::RectF(midX, topY, rightX, bottomY));
        }
        // 2: LeftFullRightSplit [ Left | Top-Right / Bottom-Right ]
        {
            float lx = barX + padX + 2 * (itemW + gap);
            float ly = barY + padY;
            float hw = (itemW - innerGap) * 0.5f;
            float hh = (itemH - innerGap) * 0.5f;
            addSlot(EditorSplitLayout::LeftFullRightSplit, 0,
                    D2D1::RectF(lx, ly, lx + hw, ly + itemH),
                    D2D1::RectF(canvasLeft, topY, midX, bottomY));
            addSlot(EditorSplitLayout::LeftFullRightSplit, 1,
                    D2D1::RectF(lx + hw + innerGap, ly, lx + itemW, ly + hh),
                    D2D1::RectF(midX, topY, rightX, midY));
            addSlot(EditorSplitLayout::LeftFullRightSplit, 2,
                    D2D1::RectF(lx + hw + innerGap, ly + hh + innerGap, lx + itemW, ly + itemH),
                    D2D1::RectF(midX, midY, rightX, bottomY));
        }
        // 3: TwoRows [ Top / Bottom ]
        {
            float lx = barX + padX + 3 * (itemW + gap);
            float ly = barY + padY;
            float hh = (itemH - innerGap) * 0.5f;
            addSlot(EditorSplitLayout::TwoRows, 0,
                    D2D1::RectF(lx, ly, lx + itemW, ly + hh),
                    D2D1::RectF(canvasLeft, topY, rightX, midY));
            addSlot(EditorSplitLayout::TwoRows, 1,
                    D2D1::RectF(lx, ly + hh + innerGap, lx + itemW, ly + itemH),
                    D2D1::RectF(canvasLeft, midY, rightX, bottomY));
        }
        // 4: Grid2x2 [ Top-Left / Bottom-Left | Top-Right / Bottom-Right ]
        {
            float lx = barX + padX + 4 * (itemW + gap);
            float ly = barY + padY;
            float hw = (itemW - innerGap) * 0.5f;
            float hh = (itemH - innerGap) * 0.5f;
            addSlot(EditorSplitLayout::Grid2x2, 0,
                    D2D1::RectF(lx, ly, lx + hw, ly + hh),
                    D2D1::RectF(canvasLeft, topY, midX, midY));
            addSlot(EditorSplitLayout::Grid2x2, 1,
                    D2D1::RectF(lx, ly + hh + innerGap, lx + hw, ly + itemH),
                    D2D1::RectF(canvasLeft, midY, midX, bottomY));
            addSlot(EditorSplitLayout::Grid2x2, 2,
                    D2D1::RectF(lx + hw + innerGap, ly, lx + itemW, ly + hh),
                    D2D1::RectF(midX, topY, rightX, midY));
            addSlot(EditorSplitLayout::Grid2x2, 3,
                    D2D1::RectF(lx + hw + innerGap, ly + hh + innerGap, lx + itemW, ly + itemH),
                    D2D1::RectF(midX, midY, rightX, bottomY));
        }
    }
};

} // namespace phide::editor
