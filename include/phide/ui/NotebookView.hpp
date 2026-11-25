/**
 * @file NotebookView.hpp
 * @brief Interactive Jupyter / Colab-style notebook interface with modular code/markdown cells,
 *        top execution toolbar, syntax highlighting, and background asynchronous execution engine.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <list>
#include <functional>
#include <unordered_map>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/editor/NotebookModel.hpp"
#include "phide/editor/NotebookKernel.hpp"
#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/lsp/LspClient.hpp"

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
     * @brief Retrieves cleaned, visible output lines for a notebook cell (ANSI stripped).
     */
    std::vector<std::wstring> GetOutputLines(const editor::NotebookCell& cell) const;

    /**
     * @brief Sets workspace folder path for virtualenv auto-detection.
     */
    void SetWorkspacePath(const std::wstring& wsPath) {
        auto sameWs = [](const std::wstring& a, const std::wstring& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                wchar_t ca = (a[i] == L'/') ? L'\\' : static_cast<wchar_t>(towlower(a[i]));
                wchar_t cb = (b[i] == L'/') ? L'\\' : static_cast<wchar_t>(towlower(b[i]));
                if (ca != cb) return false;
            }
            return true;
        };
        if (!sameWs(m_workspacePath, wsPath)) {
            m_workspacePath = wsPath;
            m_workspaceVersion++;
        }
    }
    [[nodiscard]] const std::wstring& GetWorkspacePath() const noexcept { return m_workspacePath; }
    [[nodiscard]] std::wstring GetKernelPythonPathForTest() const { return m_kernel.GetPythonPath(); }
    [[nodiscard]] std::wstring GetKernelWorkingDirForTest() const { return m_kernel.GetWorkingDir(); }

    /**
     * @brief Cell execution controls.
     */
    void RunCell(int cellIdx, editor::OpenDocument& doc, HWND hwnd);
    void RunAllCells(editor::OpenDocument& doc, HWND hwnd);
    void StopExecution(editor::OpenDocument& doc);
    void DiscardDeviceResources();
    void RestartKernel(editor::OpenDocument& doc, HWND hwnd = nullptr);
    void RefreshPythonKernels(editor::OpenDocument& doc);
    void QueryVariablesAsync(const std::shared_ptr<editor::NotebookDocument>& nbDoc, HWND hwnd);
    void InspectVariableTableAsync(const std::wstring& varName, const std::wstring& typeHint, HWND hwnd);
    void CloseVariableTableModal() noexcept;
    void RefreshKernelDropdown(editor::OpenDocument& doc) { RefreshPythonKernels(doc); }

    struct VariableTableModalState {
        bool isOpen = false;
        bool isLoading = false;
        std::wstring varName;
        std::wstring typeName;
        std::wstring shapeLabel;
        std::wstring statusMessage;
        bool truncated = false;
        std::vector<std::wstring> columns;
        std::vector<std::vector<std::wstring>> rows;
        std::vector<float> colWidths;
        float scrollY = 0.0f;
        float maxScrollY = 0.0f;
        float scrollX = 0.0f;
        float maxScrollX = 0.0f;
        D2D1_RECT_F rcModal{};
        D2D1_RECT_F rcCloseBtn{};
        D2D1_RECT_F rcViewport{};
    };
    [[nodiscard]] const VariableTableModalState& GetVariableTableModal() const noexcept { return m_varTableModal; }
    [[nodiscard]] static bool ParseVariableTableJson(const std::string& jsonStr, VariableTableModalState& outState);

    [[nodiscard]] bool IsExecuting() const noexcept { return m_isExecuting.load(); }
    void SetScrollY(float s) noexcept { m_scrollY = s; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnAddCode() const noexcept { return m_btnAddCode; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnAddText() const noexcept { return m_btnAddText; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnCutCellForTest() const noexcept { return m_btnCutCell; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnCopyCellForTest() const noexcept { return m_btnCopyCell; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnPasteCellForTest() const noexcept { return m_btnPasteCell; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnRunCellForTest() const noexcept { return m_btnRunCell; }
    [[nodiscard]] const D2D1_RECT_F& GetBtnCellTypePickerForTest() const noexcept { return m_btnCellTypePicker; }
    [[nodiscard]] const D2D1_RECT_F& GetCellTypeItemCodeBoundsForTest() const noexcept { return m_cellTypeItemCodeBounds; }
    [[nodiscard]] const D2D1_RECT_F& GetCellTypeItemMdBoundsForTest() const noexcept { return m_cellTypeItemMdBounds; }
    [[nodiscard]] bool IsCellTypeDropdownOpenForTest() const noexcept { return m_cellTypeDropdownOpen; }

    enum class MarkdownFormatAction {
        Bold,
        Italic,
        Quote,
        Link,
        Image,
        BulletList,
        NumberList,
        Heading
    };
    void ApplyMarkdownFormat(editor::NotebookDocument& nb, int cellIdx, MarkdownFormatAction action, HWND hwnd);

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
    void SubmitCellInput(editor::NotebookCell& cell, HWND hwnd);

    struct CellColorSwatchHit {
        D2D1_RECT_F bounds{};
        int cellIdx = -1;
        size_t line = 0;
        size_t col = 0;
        size_t length = 0;
        std::wstring colorText;
    };
    struct CellDiagHit {
        D2D1_RECT_F bounds{};
        int cellIdx = -1;
        size_t line = 0;
        int severity = 1;
        std::wstring message;
    };
    struct MarkdownLinkHit {
        D2D1_RECT_F bounds{};
        int cellIdx = -1;
        std::wstring url;
    };
    struct MarkdownImageHit {
        D2D1_RECT_F bounds{};
        int cellIdx = -1;
        std::wstring target;
        uint64_t imageId = 0;
        std::vector<BYTE> imageBytes;
        uint32_t pixelW = 0;
        uint32_t pixelH = 0;
    };

    void SetHostHwnd(HWND hwnd) noexcept { m_hostHwnd = hwnd; }
    void SeedMarkdownImageForTest(const std::wstring& target, const std::vector<BYTE>& bytes, uint32_t w, uint32_t h);
    static std::wstring FormatLatexMathForTest(const std::wstring& latex);
    static std::wstring ResolveMarkdownImageUrlForTest(const std::wstring& rawUrl);
    [[nodiscard]] const std::vector<MarkdownLinkHit>& GetVisibleMdLinksForTest() const noexcept { return m_visibleMdLinks; }
    [[nodiscard]] const std::vector<MarkdownImageHit>& GetVisibleMdImagesForTest() const noexcept { return m_visibleMdImages; }
    [[nodiscard]] const std::wstring& GetLastOpenedMdLinkForTest() const noexcept { return m_lastOpenedMdLinkForTest; }

    using ImportCostAnalyzerFn = std::function<std::unordered_map<size_t, std::wstring>(
        const editor::Buffer&, const std::wstring& scopePath)>;
    using CompletionQueryFn = std::function<std::vector<editor::IntelliSenseItem>(
        const std::vector<std::wstring>& combinedLines, size_t line, size_t col, const std::wstring& lineText)>;
    using OpenColorPickerFn = std::function<void(const CellColorSwatchHit& swatch)>;

    void SetImportCostAnalyzer(ImportCostAnalyzerFn fn) {
        m_importCostFn = std::move(fn);
        InvalidateImportCostCache();
    }
    void SetCompletionQueryCallback(CompletionQueryFn fn) { m_completionFn = std::move(fn); }
    void SetOpenColorPickerCallback(OpenColorPickerFn fn) { m_openColorPickerFn = std::move(fn); }
    void InvalidateImportCostCache() noexcept { m_importCostVersion++; }
    void DismissAutocomplete() noexcept {
        m_nbAutocompleteVisible = false;
        m_nbAutocompleteNavigated = false;
        m_nbAutocompleteItems.clear();
        m_nbAutocompleteSelectedIdx = 0;
        m_nbAutocompleteScrollOffset = 0;
        m_nbAutocompleteBounds = {};
    }
    [[nodiscard]] bool IsAutocompleteVisible() const noexcept {
        return m_nbAutocompleteVisible && !m_nbAutocompleteItems.empty();
    }
    bool CommitAutocompleteSelection(editor::OpenDocument& doc, HWND hwnd);
    void RefreshCellAnalysis(editor::OpenDocument& doc, size_t cellIdx);

    [[nodiscard]] const D2D1_RECT_F& GetRunAllBtnBoundsForTest() const noexcept { return m_btnRunAll; }
    [[nodiscard]] const D2D1_RECT_F& GetLastTooltipRectForTest() const noexcept { return m_lastTooltipRect; }
    [[nodiscard]] bool IsLightboxOpenForTest() const noexcept { return m_lightboxOpen; }
    [[nodiscard]] float GetLightboxZoomForTest() const noexcept { return m_lightboxZoom; }
    void GetLightboxBtnBoundsForTest(D2D1_RECT_F& outSave, D2D1_RECT_F& outCopy, D2D1_RECT_F& outClose) const noexcept {
        outSave = m_lightboxBtnSave;
        outCopy = m_lightboxBtnCopy;
        outClose = m_lightboxBtnClose;
    }
    [[nodiscard]] const std::vector<CellColorSwatchHit>& GetVisibleColorSwatchesForTest() const noexcept { return m_visibleColorSwatches; }
    [[nodiscard]] const std::vector<editor::IntelliSenseItem>& GetAutocompleteItemsForTest() const noexcept { return m_nbAutocompleteItems; }
    [[nodiscard]] std::vector<lsp::Diagnostic> GetCellDiagnosticsForTest(const std::string& cellId) const;
    [[nodiscard]] std::unordered_map<size_t, std::wstring> GetCellImportCostsForTest(const std::string& cellId) const;
    [[nodiscard]] float ComputeCellLineNumberGutterWidth(const editor::NotebookCell& cell, const app::AppConfig& config, float charW) const noexcept;

private:
    std::wstring m_workspacePath;
    uint64_t m_workspaceVersion = 1;
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
    int m_hoveredBtn = 0; // 1: Run, 2: Del, 3: Up, 4: Down, 5: ClearOutput, 6: Drag, 7: Duplicate, 8: OutputCollapse, 9: OutputCopy, 10: EditMd, 11: CollapseInput, 12..20: MdToolbar(Bold..Done)
    int m_hoveredImgCellIdx = -1;
    int m_hoveredImgOutIdx = -1;
    int m_hoveredImgBtn = 0; // 1: Lightbox/Inspect, 2: Copy PNG, 3: Save PNG
    int m_hoveredToolbarBtn = 0; // 1: RunAll, 2: ClearAll, 3: AddCode, 4: AddText, 5: Kernel, 6: Restart, 7: Vars, 8: VarTable, 9: Cut, 10: Copy, 11: Paste, 12: RunCell, 13: CellType, 14: Interrupt
    int m_hoveredBottomCellIdx = -1; // Cell index whose bottom border is hovered
    int m_hoveredBottomBtn = 0; // 1: + Code, 2: + Text

    // Embedded Full Code Editor State (Diagnostics, Import Cost, Swatches, IntelliSense, Brackets)
    struct CellAnalysisEntry {
        std::string cellId;
        uint64_t cellVersion = UINT64_MAX;
        uint64_t contextVersion = UINT64_MAX;
        uint64_t importCostVersion = UINT64_MAX;
        std::wstring pythonExe;
        std::vector<lsp::Diagnostic> diagnostics;
        std::unordered_map<size_t, std::wstring> importCostMap;
    };
    std::unordered_map<std::string, CellAnalysisEntry> m_cellAnalysisCache;
    std::vector<CellColorSwatchHit> m_visibleColorSwatches;
    std::vector<CellDiagHit> m_visibleDiagHits;
    int m_hoveredDiagIdx = -1;
    uint64_t m_importCostVersion = 1;
    ImportCostAnalyzerFn m_importCostFn;
    CompletionQueryFn m_completionFn;
    OpenColorPickerFn m_openColorPickerFn;

    bool m_nbAutocompleteVisible = false;
    bool m_nbAutocompleteNavigated = false;
    wchar_t m_suppressCharTarget = 0;
    std::vector<editor::IntelliSenseItem> m_nbAutocompleteItems;
    size_t m_nbAutocompleteSelectedIdx = 0;
    size_t m_nbAutocompleteScrollOffset = 0;
    D2D1_RECT_F m_nbAutocompleteBounds{};
    float m_lastCaretX = 0.0f;
    float m_lastCaretY = 0.0f;
    float m_lastLineH = 20.0f;
    float m_lastViewLeft = 0.0f;
    float m_lastViewWidth = 900.0f;
    float m_lastViewTop = 0.0f;
    float m_lastViewHeight = 600.0f;
    int m_lastTabSize = 4;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbScopeGuide;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbSelBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbWordOccBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbGutterSep;

    void QueryActiveCellAutocomplete(editor::OpenDocument& doc);
    void EnsureActiveCellCursorVisible(editor::OpenDocument& doc) noexcept;
    void BuildCombinedNotebookLines(const editor::NotebookDocument& nb, size_t activeCellIdx,
                                    std::vector<std::wstring>& outLines, size_t& outLineOffset) const;

    // Built-in interactive Plot/Image Lightbox Viewer state
    bool m_lightboxOpen = false;
    uint64_t m_lightboxImageId = 0;
    uint32_t m_lightboxPixelW = 0;
    uint32_t m_lightboxPixelH = 0;
    std::vector<BYTE> m_lightboxPngBytes;
    float m_lightboxZoom = 1.0f;
    float m_lightboxPanX = 0.0f;
    float m_lightboxPanY = 0.0f;
    bool m_lightboxDragging = false;
    POINT m_lightboxDragStart{ 0, 0 };
    float m_lightboxPanStartX = 0.0f;
    float m_lightboxPanStartY = 0.0f;
    int m_lightboxHoveredBtn = 0; // 1: ZoomOut, 2: Reset100, 3: ZoomIn, 4: CopyImg, 5: SaveImg, 6: Close
    D2D1_RECT_F m_lightboxCardBounds{};
    D2D1_RECT_F m_lightboxCanvasBounds{};
    D2D1_RECT_F m_lightboxBtnZoomOut{};
    D2D1_RECT_F m_lightboxBtnReset{};
    D2D1_RECT_F m_lightboxBtnZoomIn{};
    D2D1_RECT_F m_lightboxBtnCopy{};
    D2D1_RECT_F m_lightboxBtnSave{};
    D2D1_RECT_F m_lightboxBtnClose{};
    ULONGLONG m_copiedFeedbackUntil = 0;
    int m_copiedFeedbackCellIdx = -1;
    bool m_scrollActiveCellIntoView = false;

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

    // Code Area Horizontal Scrollbar Dragging State
    int m_dragCodeCellIdx = -1;
    bool m_isDraggingCodeHScroll = false;
    float m_dragCodeStartPos = 0.0f;
    float m_dragCodeStartOffset = 0.0f;

    // Toolbar button hit regions (modern flat toolbar)
    D2D1_RECT_F m_btnRunAll{};
    D2D1_RECT_F m_btnRunCell{};
    D2D1_RECT_F m_btnClearAll{};
    D2D1_RECT_F m_btnAddCode{};
    D2D1_RECT_F m_btnAddText{};
    D2D1_RECT_F m_btnCutCell{};
    D2D1_RECT_F m_btnCopyCell{};
    D2D1_RECT_F m_btnPasteCell{};
    D2D1_RECT_F m_btnCellTypePicker{};
    D2D1_RECT_F m_btnKernelPicker{};
    D2D1_RECT_F m_btnRestartKernel{};
    D2D1_RECT_F m_btnInterruptKernel{};
    D2D1_RECT_F m_btnVariables{};
    D2D1_RECT_F m_btnVarTable{};
    std::vector<D2D1_RECT_F> m_varInspectBtnBounds;
    int m_hoveredVarInspectIdx = -1;
    VariableTableModalState m_varTableModal;
    D2D1_RECT_F m_kernelDropdownBounds{};
    std::vector<D2D1_RECT_F> m_kernelDropdownItemBounds;
    bool m_kernelDropdownOpen = false;
    bool m_hoverDropdownItemIdx = false;
    int m_hoveredKernelDropdownIdx = -1;

    D2D1_RECT_F m_cellTypeDropdownBounds{};
    D2D1_RECT_F m_cellTypeItemCodeBounds{};
    D2D1_RECT_F m_cellTypeItemMdBounds{};
    bool m_cellTypeDropdownOpen = false;
    int m_hoveredCellTypeDropdownIdx = -1;

    struct CellClipboard {
        bool hasCell = false;
        editor::NotebookCellType type = editor::NotebookCellType::Code;
        std::wstring language;
        std::wstring text;
    };
    CellClipboard m_cellClipboard;

    // Cached D2D brushes / WIC / bounded LRU bitmap cache (per-RT, cleared in DiscardDeviceResources)
    struct CachedNbBitmap {
        uint64_t imageId = 0;
        float pxW = 0.0f;
        float pxH = 0.0f;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };
    static constexpr size_t kMaxCachedNbBitmaps = 16u;
    std::list<CachedNbBitmap> m_bitmapLru;
    std::unordered_map<uint64_t, std::list<CachedNbBitmap>::iterator> m_bitmapIndex;
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_pWicFactory;
    ID2D1RenderTarget* m_cachedNbRT = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbInner;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbStop;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbSpin;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbIdleBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbIdleHover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbErrRed;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbOkGreen;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbTrack;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbThumb;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbThumbDrag;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbBlueText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbPillBgDark;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbPillBgLight;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbPopupBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushNbRowHover;

    float m_cachedFmtDpi = 0.0f;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtNbBtn;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtNbActionGlyph;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtNbModalTitle;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtNbUiLeading;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtNbMdProse;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtNbMdBold;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtNbMdItalic;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtNbMonoSmall;

    std::atomic<bool> m_isDestroying{ false };
    std::atomic<int> m_activeWorkers{ 0 };
    HWND m_hostHwnd = nullptr;

    enum class MdImageStatus { Pending, Ready, Failed };
    struct CachedMdImage {
        MdImageStatus status = MdImageStatus::Pending;
        std::vector<BYTE> bytes;
        uint64_t imageId = 0;
        uint32_t pixelW = 0;
        uint32_t pixelH = 0;
        std::wstring errorMsg;
    };
    std::mutex m_mdImageCacheMutex;
    std::unordered_map<std::wstring, CachedMdImage> m_mdImageCache;
    std::vector<MarkdownLinkHit> m_visibleMdLinks;
    std::vector<MarkdownImageHit> m_visibleMdImages;
    int m_hoveredMdLinkIdx = -1;
    std::wstring m_lastOpenedMdLinkForTest;

    void EnsureNbBrushes(ID2D1RenderTarget* pRT);
    void EnsureNbTextFormats(Theme& theme, const app::AppConfig& config);
    ID2D1Bitmap* GetOrCreateOutputBitmap(ID2D1RenderTarget* pRT, uint64_t imageId, const std::vector<BYTE>& pngBytes, float& outW, float& outH);
    std::vector<std::wstring> DiscoverPythonExecutables(const std::wstring& targetFilePath) const;

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
    mutable std::recursive_mutex m_outputsMutex;
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
                       const D2D1_RECT_F& targetBounds, const std::wstring& text,
                       float canvasLeft = 0.0f, float canvasWidth = 0.0f);
    bool HitTestOutputArea(const editor::NotebookCell& cell, POINT clientPt, const app::AppConfig& config,
                           size_t& outLine, size_t& outCol) const;
    bool CopyPngToClipboard(const std::vector<BYTE>& pngBytes, HWND hwnd);
    bool SavePngWithDialog(const std::vector<BYTE>& pngBytes, HWND hwnd);
    void OpenImageLightbox(const editor::CellOutput& out);
    void RenderImageLightbox(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                             float left, float width, float top, float height);
    float ComputeScaledImageSize(const editor::CellOutput& out, float availW, float imageZoomScale,
                                 const app::AppConfig& config, float& outW, float& outH) const noexcept;
    float MeasureRenderedMarkdownHeight(Theme& theme, const editor::OpenDocument& doc, const editor::NotebookCell& cell,
                                        const app::AppConfig& config, float maxAvailW);
    void RenderMarkdownPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               const editor::OpenDocument& doc, const editor::NotebookCell& cell,
                               const D2D1_RECT_F& bounds, int cellIdx);
    bool ProbeWicImageDimensions(const std::vector<BYTE>& bytes, uint32_t& outW, uint32_t& outH);
    CachedMdImage EnsureMarkdownImage(const editor::OpenDocument& doc, const std::wstring& rawTarget);
    D2D1_RECT_F m_lastTooltipRect{};
};

} // namespace phide::ui
