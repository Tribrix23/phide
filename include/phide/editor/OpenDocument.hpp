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

namespace phide::editor {

/**
 * @struct OpenDocument
 * @brief Encapsulates a document's filesystem path, text buffer, dirty state, and preview mode.
 */
struct OpenDocument {
    std::wstring filePath;       ///< Full filesystem path (empty for virtual/special views)
    std::wstring fileName;       ///< Display title or basename (e.g. index.php)
    Buffer buffer;               ///< Dedicated text buffer
    bool isDirty = false;        ///< True if document has unsaved edits
    bool isPreview = false;      ///< True if single-clicked and not yet edited/pinned (VS Code preview tab)
    bool isSpecialView = false;  ///< True for Keyboard Shortcuts, settings, etc.
    std::wstring viewType;       ///< Special view identifier (e.g. L"keybindings")
    D2D1_RECT_F tabBounds{};     ///< Screen bounding box of the tab
    D2D1_RECT_F closeBounds{};   ///< Screen bounding box of the tab close button (×)
    D2D1_RECT_F splitTabBounds{};   ///< Screen bounding box of the tab in secondary/right split pane
    D2D1_RECT_F splitCloseBounds{}; ///< Screen bounding box of tab close button in right split pane

    // Viewport Scrolling State
    size_t scrollLine = 0;              ///< Top-most visible visual line index
    float scrollOffsetX = 0.0f;         ///< Horizontal scroll offset in pixels
    D2D1_RECT_F scrollbarTrack{};       ///< Bounding box of the vertical scrollbar track
    D2D1_RECT_F scrollbarThumb{};       ///< Bounding box of the vertical scrollbar thumb
    bool isDraggingScrollbar = false;   ///< True while user is actively dragging the scrollbar thumb
    float dragStartY = 0.0f;            ///< Mouse Y coordinate at start of scrollbar drag
    size_t dragStartScrollLine = 0;     ///< scrollLine at start of scrollbar drag

    // Code Folding State (Images 2 & 3)
    std::set<size_t> foldedLines;       ///< Set of starting line indices (0-based buffer line) that are folded

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
};

/**
 * @struct EditorGroup
 * @brief Manages an independent editor column/pane with its own list of open tabs and active document.
 */
struct EditorGroup {
    std::vector<OpenDocument> docs;      ///< Independent list of document tabs open in this editor column
    int activeDocIdx = -1;               ///< Index of the currently active document tab in this group
    D2D1_RECT_F bounds{};                ///< Screen bounding rectangle of this editor pane
};

} // namespace phide::editor
