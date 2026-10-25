/**
 * @file OpenDocument.hpp
 * @brief Representation of an opened editor tab, buffer, and preview lifecycle state.
 */

#pragma once

#include <string>
#include <set>
#include <d2d1.h>
#include <wrl/client.h>
#include "phide/editor/Buffer.hpp"
#include "phide/lsp/LspClient.hpp"

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
    std::wstring viewType;       ///< Special view identifier (e.g. L"keybindings")
    D2D1_RECT_F tabBounds{};     ///< Screen bounding box of the tab
    D2D1_RECT_F closeBounds{};   ///< Screen bounding box of the tab close button (×)
    D2D1_RECT_F splitTabBounds{};   ///< Screen bounding box of the tab in secondary/right split pane
    D2D1_RECT_F splitCloseBounds{}; ///< Screen bounding box of tab close button in right split pane

    // Breakpoints for Debugging (F9 / Debug Menu)
    std::vector<size_t> breakpoints; ///< 0-based line indices where breakpoints are set

    // Text Selection State (Images 2, 3, 4)
    bool hasSelection = false;          ///< True if text is selected
    CursorPosition selAnchor{ 0, 0 };   ///< Starting anchor of selection
    CursorPosition selEnd{ 0, 0 };      ///< Ending position of selection
    bool isSelecting = false;           ///< True while user is dragging mouse to select text
    std::wstring selectedWord;          ///< When a word/symbol is double-clicked or selected, the word text for multi-occurrence highlight

    [[nodiscard]] bool HasSelection() const noexcept {
        return hasSelection && (selAnchor.line != selEnd.line || selAnchor.col != selEnd.col);
    }

    void ClearSelection() noexcept {
        hasSelection = false;
        isSelecting = false;
        selectedWord.clear();
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

    bool DeleteSelection() {
        if (!HasSelection()) return false;
        size_t sLine, sCol, eLine, eCol;
        GetSelectionRange(sLine, sCol, eLine, eCol);
        if (sLine == eLine) {
            const auto& line = buffer.GetLine(sLine);
            std::wstring newLine = line.substr(0, sCol);
            if (eCol < line.length()) {
                newLine += line.substr(eCol);
            }
            buffer.SetLine(sLine, newLine);
            buffer.SetCursor(sLine, sCol);
        } else {
            const auto& startLineText = buffer.GetLine(sLine);
            const auto& endLineText = buffer.GetLine(eLine);
            std::wstring merged = startLineText.substr(0, sCol);
            if (eCol < endLineText.length()) {
                merged += endLineText.substr(eCol);
            }
            std::vector<std::wstring> newLines;
            for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
                if (l < sLine) {
                    newLines.push_back(buffer.GetLine(l));
                } else if (l == sLine) {
                    newLines.push_back(merged);
                } else if (l > eLine) {
                    newLines.push_back(buffer.GetLine(l));
                }
            }
            buffer.SetLines(newLines);
            buffer.SetCursor(sLine, sCol);
        }
        ClearSelection();
        return true;
    }

    // Viewport Scrolling State
    size_t scrollLine = 0;              ///< Top-most visible visual line index
    float scrollOffsetX = 0.0f;         ///< Horizontal scroll offset in pixels
    D2D1_RECT_F scrollbarTrack{};       ///< Bounding box of the vertical scrollbar track
    D2D1_RECT_F scrollbarThumb{};       ///< Bounding box of the vertical scrollbar thumb
    bool isDraggingScrollbar = false;   ///< True while user is actively dragging the scrollbar thumb
    float dragStartY = 0.0f;            ///< Mouse Y coordinate at start of scrollbar drag
    size_t dragStartScrollLine = 0;     ///< scrollLine at start of scrollbar drag

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
    std::set<size_t> diffDeletedLines;   ///< Left-side line indices (0-based) where deletions occurred

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

} // namespace phide::editor
