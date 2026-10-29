/**
 * @file NotebookView.hpp
 * @brief Interactive Jupyter / Colab-style notebook interface with modular code/markdown cells,
 *        top execution toolbar, syntax highlighting, and background asynchronous execution engine.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/editor/NotebookModel.hpp"
#include "phide/editor/NotebookKernel.hpp"

namespace phide::ui {

/**
 * @class NotebookView
 * @brief Dedicated interactive view for Jupyter Notebook documents (.ipynb).
 */
class NotebookView {
public:
    NotebookView();
    ~NotebookView();

    /**
     * @brief Renders the notebook view (toolbar, cells, code editor, and live outputs).
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                editor::OpenDocument& doc, float left, float width, float top, float height);

    /**
     * @brief Mouse event handling.
     */
    bool HandleLButtonDown(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config, HWND hwnd);
    bool HandleMouseMove(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config);
    bool HandleLButtonUp(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config);
    bool HandleMouseWheel(short delta, POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config);

    /**
     * @brief Keyboard event handling.
     */
    bool HandleKeyDown(WPARAM wParam, editor::OpenDocument& doc, HWND hwnd);
    bool HandleChar(wchar_t ch, editor::OpenDocument& doc, HWND hwnd);

    /**
     * @brief Cursor query for mouse position.
     */
    LPCWSTR GetCursorForPoint(POINT pt, const editor::OpenDocument& doc) const;

    /**
     * @brief Sets workspace folder path for virtualenv auto-detection.
     */
    void SetWorkspacePath(const std::wstring& wsPath) { m_workspacePath = wsPath; }

    /**
     * @brief Cell execution controls.
     */
    void RunCell(int cellIdx, editor::OpenDocument& doc, HWND hwnd);
    void RunAllCells(editor::OpenDocument& doc, HWND hwnd);
    void StopExecution(editor::OpenDocument& doc);
    void RestartKernel(editor::OpenDocument& doc);

    [[nodiscard]] bool IsExecuting() const noexcept { return m_isExecuting.load(); }
    void SetScrollY(float s) noexcept { m_scrollY = s; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnAddCode() const noexcept { return m_btnAddCode; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnAddText() const noexcept { return m_btnAddText; }

    void RunGarbageCollection() {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_cellQueue.shrink_to_fit();
    }

    /**
     * @brief Hit-tests code area coordinates to obtain line and column with character metrics precision.
     */
    bool HitTestCodeArea(const editor::NotebookCell& cell, POINT clientPt, const app::AppConfig& config,
                         size_t& outLine, size_t& outCol) const;
    float EnsureCharWidth(Theme& theme, const app::AppConfig& config);
    std::wstring FindPythonExecutable(const std::wstring& targetFilePath = L"") const;

private:
    std::wstring m_workspacePath;
    float m_scrollY = 0.0f;
    float m_totalHeight = 0.0f;
    float m_viewHeight = 0.0f;
    bool m_isDraggingScrollbar = false;
    float m_dragScrollStartY = 0.0f;
    float m_dragScrollStartOffset = 0.0f;
    D2D1_RECT_F m_scrollTrack{};
    D2D1_RECT_F m_scrollThumb{};

    // Caret blinking
    ULONGLONG m_lastBlinkTime = 0;
    bool m_caretVisible = true;

    // Double click & text selection tracking
    ULONGLONG m_lastLDownTime = 0;
    POINT m_lastLDownPt{ 0, 0 };
    bool m_isSelectingText = false;
    mutable float m_charWidth = 0.0f;

    // Hover state tracking
    int m_hoveredCellIdx = -1;
    int m_hoveredBtn = 0; // 1: Run, 2: Del, 3: Up, 4: Down, 5: ClearOutput, 6: Drag
    int m_hoveredToolbarBtn = 0; // 1: RunAll, 2: ClearAll, 3: AddCode, 4: AddText
    int m_hoveredBottomCellIdx = -1; // Cell index whose bottom border is hovered
    int m_hoveredBottomBtn = 0; // 1: + Code, 2: + Text

    // Drag & drop cell reorganization
    bool m_isDraggingCell = false;
    int m_dragCandidateCellIdx = -1;
    int m_draggedCellIdx = -1;
    int m_dropTargetCellIdx = -1;
    POINT m_dragStartPt{};

    // Cell Output Scrollbar Dragging State
    int m_dragOutputCellIdx = -1;
    bool m_isDraggingOutputVScroll = false;
    bool m_isDraggingOutputHScroll = false;
    float m_dragOutputStartPos = 0.0f;
    float m_dragOutputStartOffset = 0.0f;

    // Toolbar button hit regions (icon-only, minimal)
    D2D1_RECT_F m_btnRunAll{};
    D2D1_RECT_F m_btnClearAll{};
    D2D1_RECT_F m_btnAddCode{};
    D2D1_RECT_F m_btnAddText{};

    // Background execution engine & sequential queue
    editor::NotebookKernel m_kernel;
    std::atomic<bool> m_isExecuting{ false };
    std::atomic<int> m_executingCellIdx{ -1 };
    std::atomic<bool> m_shouldStop{ false };
    std::atomic<bool> m_isQueueRunning{ false };
    std::atomic<int> m_queueNextCellIdx{ -1 };
    HANDLE m_hChildProcess = NULL;
    std::thread m_execThread;
    std::mutex m_execMutex;
    std::vector<int> m_cellQueue;
    std::mutex m_queueMutex;
    float m_spinAngle = 0.0f;
    ULONGLONG m_lastAnimTick = 0;

    LucideIcons m_icons;

    // Helper utilities
    void StopExecution_Internal();
    void ExecuteScriptAsync(int cellIdx, const std::wstring& script, const std::wstring& lang,
                            std::shared_ptr<editor::NotebookDocument> nbDoc, HWND hwnd);
    void DrawRunMorphButton(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                            const D2D1_RECT_F& bounds, bool isExecuting, bool isQueued, bool hasError, bool isHovered);
    void RenderTooltip(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       const D2D1_RECT_F& targetBounds, const std::wstring& text);
};

} // namespace phide::ui
