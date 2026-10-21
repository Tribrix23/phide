/**
 * @file OpenDocument.hpp
 * @brief Representation of an opened editor tab, buffer, and preview lifecycle state.
 */

#pragma once

#include <string>
#include <d2d1.h>
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
};

} // namespace phide::editor
