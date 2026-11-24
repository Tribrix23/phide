/**
 * @file EditorCanvas.hpp
 * @brief High-performance Direct2D editor canvas, multi-tab bar, line gutter, and syntax rendering.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <list>
#include <unordered_map>
#include "phide/app/AppConfig.hpp"
#include "phide/app/LocalTimeline.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/editor/BracketPairAnalyzer.hpp"
#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/editor/PredictionEngine.hpp"
#include "phide/editor/WorkspaceNavigation.hpp"
#include "phide/editor/LanguageIntelligence.hpp"
#include "phide/dev/DevWorkflowEngines.hpp"
#include "phide/git/GitWorkflowEngine.hpp"
#include "phide/db/SqlEngine.hpp"
#include "phide/editor/SnippetManager.hpp"
#include "phide/ui/KeyboardShortcutsView.hpp"
#include "phide/ui/PackageManagerView.hpp"
#include "phide/ui/NotebookView.hpp"
#include "phide/ui/GraphView.hpp"
#include "phide/ui/ActivityHeatmapView.hpp"
#include "phide/ui/SettingsView.hpp"
#include "phide/ui/ApiTesterView.hpp"
#include "phide/ui/InteractiveGuideView.hpp"
#include "phide/ui/WelcomeView.hpp"
#include "phide/ui/PhideLogo.hpp"
#include "phide/ui/ColorPickerModal.hpp"
#include "phide/ui/StructuredViewers.hpp"
#include "phide/ui/AssetViewer.hpp"
#include "phide/ui/ErdView.hpp"
#include "phide/ui/AuditView.hpp"
#include <unordered_map>

namespace phide::ui {

/**
 * @struct LineColorSpan
 * @brief Represents a parsed color token (hex, rgb, or Tailwind class) within a line.
 */
struct LineColorSpan {
    size_t col = 0;
    size_t len = 0;
    std::wstring colorStr;
    D2D1_COLOR_F color{ 0.0f, 0.0f, 0.0f, 1.0f };
    ColorFormat format{ ColorFormat::Hex };
    bool hasAlpha = false;
};

/**
 * @struct LearningCourseDef
 * @brief Represents a course in the Learning Hub with metadata, tags, and banner asset.
 */
struct LearningCourseDef {
    std::wstring id;
    std::wstring category; // "Python", "JavaScript", "PHP", "Others"
    std::wstring imageFile;
    std::wstring creator = L"Koda";
    std::wstring title;
    std::wstring description;
    int lessons = 24;
    int hours = 6;
    std::wstring difficulty = L"Beginner"; // "Beginner", "Intermediate", "Advanced"
    D2D1_COLOR_F badgeColor{ 0.06f, 0.73f, 0.51f, 1.0f };
    D2D1_COLOR_F badgeBg{ 0.02f, 0.18f, 0.09f, 0.85f };
    D2D1_COLOR_F badgeBorder{ 0.06f, 0.73f, 0.51f, 0.45f };
    D2D1_RECT_F cardBounds{};
};

/**
 * @class EditorCanvas
 * @brief Renders the editor workspace including multi-tabs, line numbers, syntax-colored lines, and interactive views.
 */
class EditorCanvas {
public:
    enum class BetterCommentType {
        Normal = 0,
        Urgent,    ///< Crimson (#FF2E63) - !!, URGENT, CRITICAL, HOTFIX
        Asap,      ///< Vivid Magenta (#E040FB) - ASAP, IMMEDIATE, PRIORITY
        Alert,     ///< Red (#FF5555) - !, ERROR, FIXME, BUG
        Todo,      ///< Orange (#FF9900) - TODO, [ ], []
        Warning,   ///< Yellow (#FFCC00) - ^, WARNING, WARN, DEPRECATED
        Hack,      ///< Warm Coral (#FF7A59) - HACK, WORKAROUND, TEMP, XXX, SECURITY
        Note,      ///< Cyan-Teal (#2DD4BF) - NOTE, INFO, DOC
        Review,    ///< Lavender (#B388FF) - REVIEW, OPTIMIZE, PERF, REFACTOR
        Question,  ///< Sky Blue (#46A0FF) - ?, QUESTION, WHY
        Important, ///< Mint Green (#61D674) - *, IMPORTANT
        Done       ///< Muted Grey (#7E848E) - [x], [X] (strikethrough)
    };

    struct BetterCommentInfo {
        BetterCommentType type = BetterCommentType::Normal;
        bool hasCheckbox = false;
        bool isChecked = false;
        size_t checkboxCol = std::wstring::npos;
        size_t checkboxLen = 0;
        size_t tagCol = std::wstring::npos;
        size_t tagLen = 0;
    };

    struct CommentAnnotationEntry {
        size_t line = 0;
        size_t tagCol = std::wstring::npos;
        size_t tagLen = 0;
        BetterCommentType type = BetterCommentType::Normal;
        std::wstring tagLabel;
        std::wstring preview;
    };

    struct CommentIndicatorHit {
        D2D1_RECT_F bounds{};
        size_t line = 0;
        BetterCommentType type = BetterCommentType::Normal;
        std::wstring tagLabel;
        std::wstring preview;
    };

    struct DocCommentSummary {
        uint64_t bufferInstanceId = 0;
        uint64_t bufferVersion = UINT64_MAX;
        std::wstring docExt;
        size_t urgentCount = 0;
        size_t asapCount = 0;
        size_t alertCount = 0;
        size_t todoCount = 0;
        size_t warningCount = 0;
        size_t hackCount = 0;
        size_t noteCount = 0;
        size_t reviewCount = 0;
        size_t questionCount = 0;
        size_t importantCount = 0;
        std::vector<CommentAnnotationEntry> entries;
        std::unordered_map<size_t, BetterCommentType> inheritedLineTypes;
    };

    struct TodoCheckboxHit {
        D2D1_RECT_F bounds{};
        size_t line = 0;
        bool isChecked = false;
    };
    EditorCanvas();
    ~EditorCanvas();

    /**
     * @brief Renders the entire editor canvas (multi-tabs, active document, split editor side-by-side panes, or placeholder watermark).
     * @param highlightQuery Search query string to highlight across visible lines.
     * @param highlightMatchCase Whether search query match is case-sensitive.
     * @param highlightWholeWord Whether search query matches whole words only.
     * @param highlightUseRegex Whether search query is treated as a regular expression.
     * @param isSplitView Whether side-by-side split editor mode is active (Image 4).
     * @param splitDocIdx Document index displayed in the secondary (right) split pane.
     * @param focusedPane Active pane receiving keyboard input (0 = Left, 1 = Right).
     */
    /**
     * @brief Renders the entire editor canvas supporting multi-column editor groups.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme.
     * @param config Application config.
     * @param groups List of active side-by-side editor groups.
     * @param focusedGroupIdx 0-based index of the currently focused editor group.
     * @param canvasLeft X coordinate where the editor canvas begins.
     * @param canvasWidth Total width allocated for all editor groups.
     * @param clientHeight Total client area height.
     * @param hasLeftDrawer True if sidebar drawer is open.
     * @param highlightQuery Search query for occurrences.
     * @param highlightMatchCase Case sensitive.
     * @param highlightWholeWord Whole word.
     * @param highlightUseRegex Regex.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                std::vector<editor::EditorGroup>& groups, int focusedGroupIdx,
                float canvasLeft, float canvasWidth, float clientHeight,
                bool hasLeftDrawer = false,
                const std::wstring& highlightQuery = L"",
                bool highlightMatchCase = false,
                bool highlightWholeWord = false,
                bool highlightUseRegex = false,
                bool isRenaming = false);

    /**
     * @brief Convenience overload for single-pane callers and unit tests.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                float canvasLeft, float canvasWidth, float clientHeight,
                bool hasLeftDrawer = false,
                const std::wstring& highlightQuery = L"",
                bool highlightMatchCase = false,
                bool highlightWholeWord = false,
                bool highlightUseRegex = false,
                bool isRenaming = false) {
        editor::EditorGroup g;
        g.docs = std::move(docs);
        g.activeDocIdx = activeDocIdx;
        std::vector<editor::EditorGroup> groups;
        groups.push_back(std::move(g));
        Render(pRT, theme, config, groups, 0, canvasLeft, canvasWidth, clientHeight, hasLeftDrawer,
               highlightQuery, highlightMatchCase, highlightWholeWord, highlightUseRegex, isRenaming);
        docs = std::move(groups[0].docs);
    }

    /**
     * @brief Displays the floating refactor action / warning popup matching Image 5.
     * @param message Text to display (e.g. L"No refactorings available").
     */
    void ShowRefactorPopup(const std::wstring& message = L"No refactorings available") {
        m_showRefactorPopup = true;
        m_refactorMessage = message;
    }
    void DismissRefactorPopup() noexcept { m_showRefactorPopup = false; }
    [[nodiscard]] bool IsRefactorPopupVisible() const noexcept { return m_showRefactorPopup; }
    [[nodiscard]] static D2D1_RECT_F ComputeRefactorPopupBounds(
        float tipX, float tipY, float popupW, float popupH,
        float arrowH, const D2D1_RECT_F& paneBounds, float clientHeight,
        const app::AppConfig& config, bool& outFlipBelow) noexcept;

    // Autocomplete / IntelliSense Popup State (Image 5)
    void ShowAutocomplete(const std::vector<editor::IntelliSenseItem>& items, size_t selectedIndex = 0);
    void DismissAutocomplete() noexcept { m_showAutocomplete = false; m_autocompleteNavigated = false; }
    [[nodiscard]] bool IsAutocompleteNavigated() const noexcept { return m_autocompleteNavigated; }
    [[nodiscard]] bool IsAutocompleteVisible() const noexcept { return m_showAutocomplete && !m_autocompleteItems.empty(); }
    [[nodiscard]] size_t GetAutocompleteSelectedIndex() const noexcept { return m_autocompleteSelectedIdx; }
    void SetAutocompleteSelectedIndex(size_t idx) noexcept;
    void SelectNextAutocomplete() noexcept;
    void SelectPrevAutocomplete() noexcept;
    [[nodiscard]] const std::vector<editor::IntelliSenseItem>& GetAutocompleteItems() const noexcept { return m_autocompleteItems; }
    [[nodiscard]] const editor::IntelliSenseItem* GetSelectedAutocompleteItem() const noexcept;
    [[nodiscard]] bool HitTestAutocomplete(POINT pt, int& outIdx) const;
    bool HandleAutocompleteMouseWheel(short delta) noexcept;
    void SetAutocompleteScrollOffset(size_t offset) noexcept { m_autocompleteScrollOffset = offset; }
    [[nodiscard]] size_t GetAutocompleteScrollOffset() const noexcept { return m_autocompleteScrollOffset; }
    [[nodiscard]] const D2D1_RECT_F& GetAutocompleteBounds() const noexcept { return m_autocompleteBounds; }

    // Inline Ghost Text with "Tab to complete" (Image 3)
    [[nodiscard]] const std::wstring& GetGhostText() const noexcept { return m_ghostText; }
    void SetGhostText(const std::wstring& ghost) { m_ghostText = ghost; }
    void ClearGhostText() noexcept { m_ghostText.clear(); }

    // NEXT: In-File & Cross-File Code Prediction (Images 3, 4, 5)
    void SetPrediction(const editor::CodePrediction& pred) { m_prediction = pred; }
    void ClearPrediction() noexcept { m_prediction = {}; m_crossFilePillBounds = { 0, 0, 0, 0 }; }
    [[nodiscard]] const editor::CodePrediction& GetPrediction() const noexcept { return m_prediction; }
    [[nodiscard]] bool HasPrediction() const noexcept { return m_prediction.isValid; }
    [[nodiscard]] bool ShouldShowPrediction(const editor::OpenDocument& doc) const noexcept;
    [[nodiscard]] const D2D1_RECT_F& GetCrossFilePillBounds() const noexcept { return m_crossFilePillBounds; }
    [[nodiscard]] bool HitTestCrossFilePill(POINT pt) const;
    void SetCrossFilePillHovered(bool hovered) noexcept { m_crossFilePillHovered = hovered; }
    [[nodiscard]] bool IsCrossFilePillHovered() const noexcept { return m_crossFilePillHovered; }

    /**
     * @brief Toggles caret blink phase.
     */
    void ToggleCaretBlink() noexcept { m_caretVisible = !m_caretVisible; }
    void ResetCaretBlink() noexcept {
        m_caretVisible = true;
        m_caretResetTick = GetTickCount64();
    }

    /**
     * @brief Computes normalized caret opacity [0.0..1.0] for editor.cursorBlinking modes
     *        ("blink", "smooth", "phase", "solid").
     */
    [[nodiscard]] static float ComputeCaretOpacity(const std::wstring& mode, ULONGLONG nowMs,
                                                   ULONGLONG resetTick, bool fallbackVisible) noexcept {
        if (mode == L"solid") return 1.0f;
        if (mode == L"smooth") {
            const ULONGLONG elapsed = (nowMs >= resetTick) ? (nowMs - resetTick) : 0ULL;
            const float phase = static_cast<float>(elapsed % 1050ULL) / 1050.0f;
            return 0.5f + 0.5f * std::cosf(2.0f * 3.14159265f * phase);
        }
        if (mode == L"phase") {
            const ULONGLONG elapsed = (nowMs >= resetTick) ? (nowMs - resetTick) : 0ULL;
            const ULONGLONG modMs = elapsed % 1200ULL;
            if (modMs < 420ULL) return 1.0f;
            const float phase = static_cast<float>(modMs - 420ULL) / 780.0f;
            return 0.5f + 0.5f * std::cosf(2.0f * 3.14159265f * phase);
        }
        return fallbackVisible ? 1.0f : 0.0f;
    }

    /**
     * @brief Computes width of optional dedicated Git blame gutter column (Author · 2d).
     */
    [[nodiscard]] static float ComputeGutterBlameWidth(const app::AppConfig& config) noexcept {
        return config.showGutterBlame ? config.ScaleF(122.0f) : 0.0f;
    }

    /**
     * @brief Computes dynamic line-number gutter width so 5-7+ digit line numbers never clip.
     */
    [[nodiscard]] static float ComputeGutterWidth(const app::AppConfig& config, size_t lineCount) noexcept {
        const float blameW = ComputeGutterBlameWidth(config);
        if (!config.showLineNumbers) return blameW + config.ScaleF(12.0f);
        size_t digits = 1;
        size_t n = lineCount;
        while (n >= 10) {
            n /= 10;
            ++digits;
        }
        if (digits <= 4) return blameW + config.ScaleF(50.0f);
        const float perDigit = (std::max)(11.5f, config.fontSize * 0.80f);
        return blameW + config.ScaleF(50.0f + static_cast<float>(digits - 4) * perDigit);
    }

    [[nodiscard]] static std::wstring ExtractCodeLensSymbolFromLine(const std::wstring& line, bool& outIsTest);

    /**
     * @brief Tests if mouse clicked on any tab header.
     * @param pt Mouse point in client coordinates.
     * @param docs Collection of open documents in the target pane.
     * @return Tab index (0-based) or -1 if no tab hit.
     */
    [[nodiscard]] int HitTestTab(POINT pt, const std::vector<editor::OpenDocument>& docs) const;

    /**
     * @brief Tests if mouse clicked any tab's close button (×).
     * @param pt Mouse point in client coordinates.
     * @param docs Collection of open documents in the target pane.
     * @return Tab index (0-based) or -1 if no close button hit.
     */
    [[nodiscard]] int HitTestTabClose(POINT pt, const std::vector<editor::OpenDocument>& docs) const;

    /**
     * @brief Tests if mouse clicked any tab's undock/dock toggle button (↗ / ↙).
     * @param pt Mouse point in client coordinates.
     * @param docs Collection of open documents in the target pane.
     * @return Tab index (0-based) or -1 if no undock/dock button hit.
     */
    [[nodiscard]] int HitTestTabUndock(POINT pt, const std::vector<editor::OpenDocument>& docs) const;

    /**
     * @brief Sets whether EditorCanvas is currently rendering inside a floating/undocked window
     *        so per-tab controls display Dock-Back (↙) instead of Undock (↗).
     */
    void SetRenderingFloatingWindow(bool isFloating) noexcept { m_isRenderingFloatingWindow = isFloating; }
    [[nodiscard]] bool IsRenderingFloatingWindow() const noexcept { return m_isRenderingFloatingWindow; }
    void SetFloatingTabBarHidden(bool hidden) noexcept { m_floatingTabBarHidden = hidden; }
    [[nodiscard]] bool IsFloatingTabBarHidden() const noexcept { return m_floatingTabBarHidden; }
    [[nodiscard]] bool ShouldHideTabBar(size_t groupCount, size_t paneDocCount) const noexcept {
        return m_floatingTabBarHidden || (m_isRenderingFloatingWindow && groupCount == 1 && paneDocCount <= 1);
    }
    void SetSuppressFloatingOverlays(bool suppress) noexcept { m_suppressFloatingOverlays = suppress; }
    [[nodiscard]] bool IsSuppressingFloatingOverlays() const noexcept { return m_suppressFloatingOverlays; }

    /**
     * @brief Tests if mouse clicked on the vertical scrollbar track or thumb.
     * @param pt Mouse point in client coordinates.
     * @param doc Active document containing scrollbar geometry.
     * @param outHitThumb Set to true if click was directly over the thumb pill.
     * @return True if scrollbar was clicked.
     */
    [[nodiscard]] bool HitTestScrollbar(POINT pt, const editor::OpenDocument& doc, bool& outHitThumb) const;

    /**
     * @brief Tests if mouse clicked on the horizontal scrollbar track or thumb.
     * @param pt Mouse point in client coordinates.
     * @param doc Active document containing horizontal scrollbar geometry.
     * @param outHitThumb Set to true if click was directly over the horizontal thumb pill.
     * @return True if horizontal scrollbar was clicked.
     */
    [[nodiscard]] bool HitTestHScrollbar(POINT pt, const editor::OpenDocument& doc, bool& outHitThumb) const;

    /**
     * @brief Tests if mouse clicked on the code minimap track or viewport slider.
     * @param pt Mouse point in client coordinates.
     * @param doc Active document containing minimap geometry.
     * @param outHitSlider Set to true if click was directly over the minimap slider box.
     * @return True if minimap was clicked.
     */
    [[nodiscard]] bool HitTestMinimap(POINT pt, const editor::OpenDocument& doc, bool& outHitSlider) const;

    struct FoldChevronHit {
        D2D1_RECT_F bounds{};
        size_t bufferLine = 0;
    };
    [[nodiscard]] const std::vector<FoldChevronHit>& GetFoldChevrons() const noexcept { return m_foldChevrons; }

    /**
     * @brief Tests if mouse clicked on a code folding chevron in the line number gutter.
     * @param pt Mouse point in client coordinates.
     * @return Buffer line index (0-based) of the fold toggle, or -1 if none hit.
     */
    [[nodiscard]] int HitTestFoldChevron(POINT pt) const;

    /**
     * @brief Tests if mouse clicked inside the editor gutter (e.g. to toggle a breakpoint).
     * @return True if hit, with outLine populated with the exact 0-based buffer line taking CodeLens and folding into account.
     */
    [[nodiscard]] bool HitTestGutter(POINT pt, const app::AppConfig& config, const editor::OpenDocument& doc,
                                     float canvasLeft, float clientHeight, size_t& outLine) const;

    /**
     * @brief Tests if mouse clicked inside the code area to position cursor.
     * @param clampToLineEnd When false, returns virtual column coordinates past end-of-line (for box selection).
     * @param outIsActualTextChar Optional flag set true only when pt lies directly over a real code line and within rendered characters (false for empty space below EOF, past line end, or virtual CodeLens/prediction lines).
     */
    bool HitTestCodeArea(POINT pt, const app::AppConfig& config, const editor::OpenDocument& doc,
                         float canvasLeft, float clientHeight, size_t& outLine, size_t& outCol,
                         bool clampToLineEnd = true, bool* outIsActualTextChar = nullptr) const;

    /**
     * @brief Computes whether a buffer line can be folded and returns its terminating line.
     * @param buffer Active text buffer.
     * @param startLine 0-based buffer line index to check.
     * @param outEndLine Output line where the foldable region ends.
     * @return True if startLine begins a foldable block.
     */
    static bool GetFoldRange(const editor::Buffer& buffer, size_t startLine, size_t& outEndLine);
    static bool GetFoldRangeForDoc(const editor::OpenDocument& doc, size_t startLine, size_t& outEndLine);

    /**
     * @brief Folds all foldable blocks and #region markers in the document.
     */
    static void FoldAll(editor::OpenDocument& doc);

    /**
     * @brief Expands all folded regions in the document.
     */
    static void UnfoldAll(editor::OpenDocument& doc);

    /**
     * @brief Folds blocks at or deeper than the requested 1-based nesting level (level 1 = top-level blocks).
     */
    static void FoldLevel(editor::OpenDocument& doc, int level);

    /**
     * @brief Finds the matching bracket or HTML/PHP block delimiter for (line, col).
     */
    static bool FindMatchingBracket(const editor::Buffer& buffer, size_t line, size_t col,
                                    size_t& outLine, size_t& outCol);

    /**
     * @brief Finds matching opening/closing HTML/JSX/PHP tag names if the cursor is inside a tag.
     */
    static bool FindMatchingHtmlTag(const editor::Buffer& buffer, size_t line, size_t col,
                                    size_t& outOpenLine, size_t& outOpenCol, size_t& outOpenLen,
                                    size_t& outCloseLine, size_t& outCloseCol, size_t& outCloseLen,
                                    bool& outInClosingTag);

    /**
     * @brief Accesses the embedded keyboard shortcuts interactive view.
     * @return Reference to the KeyboardShortcutsView component.
     */
    KeyboardShortcutsView& GetKeyboardShortcutsView() noexcept { return m_keybindingsView; }
    const KeyboardShortcutsView& GetKeyboardShortcutsView() const noexcept { return m_keybindingsView; }
    PackageManagerView& GetPackageManagerView() noexcept { return m_packageManagerView; }
    NotebookView& GetNotebookView() noexcept { return m_notebookView; }
    const NotebookView& GetNotebookView() const noexcept { return m_notebookView; }
    GraphView& GetGraphView() noexcept { return m_graphView; }
    ErdView& GetErdView() noexcept { return m_erdView; }
    const ErdView& GetErdView() const noexcept { return m_erdView; }
    AuditView& GetAuditView() noexcept { return m_auditView; }
    const AuditView& GetAuditView() const noexcept { return m_auditView; }
    ActivityHeatmapView& GetActivityHeatmapView() noexcept { return m_activityHeatmapView; }
    const ActivityHeatmapView& GetActivityHeatmapView() const noexcept { return m_activityHeatmapView; }
    SettingsView& GetSettingsView() noexcept { return m_settingsView; }
    ApiTesterView& GetApiTesterView() noexcept { return m_apiTesterView; }
    InteractiveGuideView& GetInteractiveGuideView() noexcept { return m_interactiveGuideView; }
    WelcomeView& GetWelcomeView() noexcept { return m_welcomeView; }

    [[nodiscard]] int HitTestWatermarkCard(POINT pt, const app::AppConfig& config, float canvasLeft, float canvasWidth, float headerH, float clientHeight) const;
    bool SetHoveredWatermarkRow(int row) noexcept {
        if (m_hoveredWatermarkRow != row) {
            m_hoveredWatermarkRow = row;
            return true;
        }
        return false;
    }
    [[nodiscard]] int GetHoveredWatermarkRow() const noexcept { return m_hoveredWatermarkRow; }

    [[nodiscard]] float GetLastCaretX() const noexcept { return m_lastCaretX; }
    [[nodiscard]] float GetLastCaretY() const noexcept { return m_lastCaretY; }

    /**
     * @brief Draws the Python virtual environment tag (.venv [3.14.7]) matching Image 2.
     */
    void DrawVenvTag(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                     const D2D1_RECT_F& bounds, bool isHovered = false);

    /**
     * @brief Renders the breadcrumbs navigation bar displaying file hierarchy and icon (Image 4).
     */
    void RenderBreadcrumbs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           const editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                           float barY, float barHeight, bool isFocusedPane = true);




    /**
     * @brief Releases device-dependent Direct2D resources (e.g. path geometries, watermark bitmaps).
     */
    void DiscardDeviceResources();
    void ResetCharWidth() const noexcept { m_charWidth = 0.0f; m_lineLayoutMap.clear(); m_lineLayoutLru.clear(); m_lineLayoutEnvSig = 0; }
    [[nodiscard]] float GetCharWidth() const noexcept { return m_charWidth; }

    void RenderTabs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    float canvasLeft, float canvasWidth, float headerH,
                    std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                    bool hasLeftDrawer, bool isFocused,
                    float& tabScrollOffset, float& totalTabsWidth, int groupIdx = 0,
                    D2D1_RECT_F* outTrack = nullptr, D2D1_RECT_F* outThumb = nullptr,
                    int* pLastScrolledActiveDocIdx = nullptr, bool isDraggingTabScroll = false);

    void RenderTabs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    float canvasLeft, float canvasWidth, float headerH,
                    std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                    bool hasLeftDrawer, bool isFocused) {
        float dummyScroll = 0.0f;
        float dummyTotal = 0.0f;
        RenderTabs(pRT, theme, config, canvasLeft, canvasWidth, headerH, docs, activeDocIdx, hasLeftDrawer, isFocused, dummyScroll, dummyTotal, 0);
    }

    void SetTabDragState(bool isDragging, int dragGroup, int dragIdx, int dropTargetGroup, int dropTargetIdx,
                         D2D1_POINT_2F dragPos = { 0.0f, 0.0f },
                         const std::wstring& dragTitle = L"") noexcept {
        m_isDraggingTab = isDragging;
        m_dragTabGroup = dragGroup;
        m_dragTabIdx = dragIdx;
        m_tabDropTargetGroup = dropTargetGroup;
        m_tabDropTargetIdx = dropTargetIdx;
        m_dragTabPos = dragPos;
        m_dragTabTitle = dragTitle;
    }

    void SetTabDragState(bool isDragging, int dragGroup, int dragIdx, int dropTargetIdx,
                         D2D1_POINT_2F dragPos = { 0.0f, 0.0f },
                         const std::wstring& dragTitle = L"") noexcept {
        SetTabDragState(isDragging, dragGroup, dragIdx, dragGroup, dropTargetIdx, dragPos, dragTitle);
    }
    [[nodiscard]] bool IsDraggingTab() const noexcept { return m_isDraggingTab; }

    void SetSplitState(const editor::EditorSplitState& state) noexcept { m_splitState = state; }
    [[nodiscard]] const editor::EditorSplitState& GetSplitState() const noexcept { return m_splitState; }
    [[nodiscard]] editor::EditorSplitState& GetSplitState() noexcept { return m_splitState; }

    void SetHoveredSplitterId(int id) noexcept { m_hoveredSplitterId = id; }
    [[nodiscard]] int GetHoveredSplitterId() const noexcept { return m_hoveredSplitterId; }

    void SetTabSnapPreview(editor::TabSnapZone zone, const D2D1_RECT_F& previewRect, int hoveredSnapBarSlot = -1) noexcept {
        m_tabSnapZone = zone;
        m_tabSnapPreviewRect = previewRect;
        m_hoveredSnapBarSlot = hoveredSnapBarSlot;
    }

    void RenderFolderView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                          float topY, float clientHeight);

    void SetWorkspaceRoot(const std::wstring& root) {
        m_workspaceRoot = root;
        m_notebookView.SetWorkspacePath(root);
    }
    [[nodiscard]] const std::wstring& GetWorkspaceRoot() const noexcept { return m_workspaceRoot; }

    // --- Floating Debug Toolbar & Cached Debug Resources (#28) ---
    void SetFloatingDebugState(bool active, bool isPaused) noexcept {
        m_floatingDebugActive = active;
        m_floatingDebugPaused = isPaused;
        if (!active) m_floatingDebugDragging = false;
    }
    void SetOnFloatingDebugAction(std::function<void(int dbgActionId)> cb) { m_onFloatingDebugAction = std::move(cb); }
    bool HandleFloatingDebugMouseMove(POINT pt);
    bool HandleFloatingDebugLButtonDown(POINT pt);
    bool HandleFloatingDebugLButtonUp(POINT pt) noexcept;
    [[nodiscard]] bool HitTestFloatingDebug(POINT pt) const noexcept;
    [[nodiscard]] bool IsDraggingFloatingDebug() const noexcept { return m_floatingDebugDragging; }

private:
    std::wstring m_workspaceRoot;
    bool m_caretVisible = true;
    ULONGLONG m_caretResetTick = 0;
    mutable float m_charWidth = 0.0f;
    mutable float m_cachedRenderFontSize = 0.0f;
    mutable std::wstring m_cachedRenderFontFamily;
    KeyboardShortcutsView m_keybindingsView;
    PackageManagerView m_packageManagerView;
    NotebookView m_notebookView;
    GraphView m_graphView;
    ErdView m_erdView;
    AuditView m_auditView;
    ActivityHeatmapView m_activityHeatmapView;
    SettingsView m_settingsView;
    ApiTesterView m_apiTesterView;
    InteractiveGuideView m_interactiveGuideView;
    WelcomeView m_welcomeView;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_pPhideLogoBitmap;
    int m_hoveredWatermarkRow = -1;

    // Cached path geometry for workspace container (curved ONLY at upper-left corner when drawer is closed)
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> m_pCanvasGeometry;
    float m_cachedLeft          = -1.0f;
    float m_cachedTop           = -1.0f;
    float m_cachedRight         = -1.0f;
    float m_cachedBottom        = -1.0f;
    bool  m_cachedHasLeftDrawer = false;
    bool  m_cachedSideBarOnRight = false;

    // Watermark logo in center of canvas (dark mode + light mode inverted)
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_pWatermarkBitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_pWatermarkBitmapLight;
    LucideIcons m_icons;

    /**
     * @brief Renders the empty workspace placeholder with brand watermark and key shortcut hints.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark/light brushes.
     * @param config Application DPI metrics and layout configuration.
     * @param canvasLeft Left coordinate of the canvas.
     * @param canvasWidth Width of the canvas.
     * @param headerH Height of the top title header.
     * @param clientHeight Total client window height.
     */
    void RenderPlaceholder(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           float canvasLeft, float canvasWidth, float headerH, float clientHeight);




    /**
     * @brief Renders the native picture viewer for images (PNG, JPG, BMP, GIF, ICO, WEBP) matching Image 2.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme containing dark/light brushes.
     * @param config Application metrics.
     * @param doc Open document whose image bitmap and dimensions are displayed.
     * @param canvasLeft Left boundary coordinate of the canvas.
     * @param canvasWidth Width of the canvas.
     * @param topY Vertical starting coordinate below the tab bar and breadcrumbs.
     * @param clientHeight Total client height.
     */
    void RenderImageView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                         float topY, float clientHeight);

    /**
     * @brief Renders the unsupported binary file view (red circled X, center text, subheader) matching Image 2.
     */
    void RenderUnsupportedBinaryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                    float topY, float clientHeight);


    /**
     * @brief Renders placeholder view when a binary/executable file is opened in diff view.
     */
    void RenderDiffBinaryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                              float topY, float clientHeight);

    /**
     * @brief Renders the multi-file commit diff view (matching Image 4) showing all files changed in a commit.
     */
    void RenderMultiFileDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                 float topY, float clientHeight, bool isPaneFocused);

    /**
     * @brief Renders syntax-highlighted tokens for a single line of code with fallback plain text.
     */
    void RenderSyntaxTokensLine(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                const editor::Buffer& buffer, size_t lineIdx,
                                float codeLeft, float lineY, float maxRight,
                                float lineH, float charW, IDWriteTextFormat* pCodeFmt,
                                ID2D1Brush* defaultTextBrush = nullptr,
                                const std::wstring& ext = L"");

    /**
     * @brief Renders a single editor pane (tabs, breadcrumbs, content).
     */
    void RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                          float canvasLeft, float canvasWidth, float headerH, float clientHeight,
                          bool hasLeftDrawer, bool isPaneFocused,
                          const std::wstring& highlightQuery,
                          bool highlightMatchCase,
                          bool highlightWholeWord,
                          bool highlightUseRegex,
                          bool isRenaming,
                          float& tabScrollOffset, float& totalTabsWidth, int groupIdx = 0,
                          D2D1_RECT_F* outTrack = nullptr, D2D1_RECT_F* outThumb = nullptr,
                          int* pLastScrolledActiveDocIdx = nullptr, bool isDraggingTabScroll = false,
                          bool* outRenderedPackages = nullptr);

    void RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                          float canvasLeft, float canvasWidth, float headerH, float clientHeight,
                          bool hasLeftDrawer, bool isPaneFocused,
                          const std::wstring& highlightQuery = L"",
                          bool highlightMatchCase = false,
                          bool highlightWholeWord = false,
                          bool highlightUseRegex = false,
                          bool isRenaming = false) {
        float dummyScroll = 0.0f;
        float dummyTotal = 0.0f;
        RenderSinglePane(pRT, theme, config, docs, activeDocIdx, canvasLeft, canvasWidth, headerH, clientHeight,
                         hasLeftDrawer, isPaneFocused, highlightQuery, highlightMatchCase, highlightWholeWord, highlightUseRegex, isRenaming,
                         dummyScroll, dummyTotal, 0);
    }

    /**
     * @brief Renders active buffer source lines, line gutter numbers, fold chevrons, and syntax highlights.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme for syntax colors.
     * @param config Application metrics.
     * @param doc Open document whose buffer and scroll/fold state are rendered.
     * @param canvasLeft Left boundary coordinate of the canvas.
     * @param canvasWidth Width of the canvas.
     * @param topY Vertical starting coordinate below the tab bar and breadcrumbs.
     * @param clientHeight Total client height.
     */
    void RenderCode(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                    float topY, float clientHeight, bool isFocused,
                    const std::wstring& highlightQuery = L"",
                    bool highlightMatchCase = false,
                    bool highlightWholeWord = false,
                    bool highlightUseRegex = false,
                    bool isRenaming = false,
                    int groupIdx = 0);

    /**
     * @brief Renders side-by-side diff comparison view with read-only left pane and editable right pane (Images 2, 3, 4, 5).
     */
    void RenderDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                        editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                        float topY, float clientHeight, bool isFocused);

    /**
     * @brief Renders interactive 3-way merge editor (Left: Current, Center: Editable Result, Right: Incoming) (#35).
     */
    void RenderThreeWayMergeView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                 float topY, float clientHeight, bool isFocused);

    /**
     * @brief Renders ambient background theme effects (3D perspective grid, particles, starfields) for unique themes.
     */
    void RenderCanvasThemeEffects(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  float canvasLeft, float canvasWidth, float topY, float clientHeight);

    void RenderRefactorPopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);
    void RenderAutocompletePopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 float clientWidth, float clientHeight);

    bool m_showRefactorPopup = false;
    std::wstring m_refactorMessage;
    float m_lastCaretX = 0.0f;
    float m_lastCaretY = 0.0f;

    bool m_showAutocomplete = false;
    bool m_autocompleteNavigated = false;
    std::vector<editor::IntelliSenseItem> m_autocompleteItems;
    size_t m_autocompleteSelectedIdx = 0;
    size_t m_autocompleteScrollOffset = 0;
    D2D1_RECT_F m_autocompleteBounds{};
    std::wstring m_ghostText;

    struct StickyScopeLine {
        size_t bufferLine = 0;
        std::wstring text;
        D2D1_RECT_F bounds{};
    };
    std::vector<StickyScopeLine> m_visibleStickyLines;
    int m_hoveredStickyIdx = -1;
    editor::CodePrediction m_prediction{};
    D2D1_RECT_F m_crossFilePillBounds{ 0, 0, 0, 0 };
    bool m_crossFilePillHovered = false;
    editor::EditorSplitState m_splitState{};
    int m_hoveredSplitterId = 0;
    editor::TabSnapZone m_tabSnapZone = editor::TabSnapZone::None;
    D2D1_RECT_F m_tabSnapPreviewRect{};
    int m_hoveredSnapBarSlot = -1;
    std::vector<float> m_scratchTabWidths;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pDropZoneFillBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pDragShadowBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pDragPillBgDarkBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pDragPillBgLightBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pSnapBarBgDarkBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pSnapBarBgLightBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pSnapSlotIdleBrush;
    std::vector<TodoCheckboxHit> m_visibleTodoCheckboxes;
    std::vector<CommentIndicatorHit> m_visibleCommentIndicators;

    // Tab Drag & Drop State
    bool m_isRenderingFloatingWindow = false;
    bool m_floatingTabBarHidden = false;
    bool m_suppressFloatingOverlays = false;
    bool m_isDraggingTab = false;
    int m_dragTabGroup = -1;
    int m_dragTabIdx = -1;
    int m_tabDropTargetGroup = -1;
    int m_tabDropTargetIdx = -1;
    D2D1_POINT_2F m_dragTabPos = { 0.0f, 0.0f };
    std::wstring m_dragTabTitle;

    std::vector<FoldChevronHit> m_foldChevrons;

    struct ColorSwatchHit {
        D2D1_RECT_F bounds{};
        size_t line = 0;
        size_t col = 0;
        size_t length = 0;
        std::wstring colorText;
    };
    std::vector<ColorSwatchHit> m_visibleColorSwatches;

    // Bracket pair analysis caching for ultra-smooth rendering
    editor::BracketAnalysisResult m_cachedBracketAnalysis;
    uint64_t m_cachedBufferVersion = static_cast<uint64_t>(-1);
    size_t m_cachedCursorLine = static_cast<size_t>(-1);
    size_t m_cachedCursorCol = static_cast<size_t>(-1);

    // Learning Hub state and bounds
    const std::vector<editor::EditorGroup>* m_pCurrentGroups = nullptr;
    std::vector<LearningCourseDef> m_learningCourses;
    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> m_learningHubBitmaps;
    std::wstring m_learningSearchQuery;
    bool m_isLearningSearchFocused = false;
    int m_learningActiveFilter = 0; // 0: All, 1: Python, 2: JavaScript, 3: PHP, 4: Others
    int m_learningSortOrder = 0; // 0: Most Popular, 1: Newest, 2: Lessons
    float m_learningScrollY = 0.0f;
    float m_learningMaxScrollY = 0.0f;
    int m_hoveredLearningCard = -1;
    int m_hoveredLearningFilter = -1;
    bool m_hoveredLearningSearch = false;
    bool m_hoveredLearningSort = false;

    D2D1_RECT_F m_learningSearchBounds{};
    D2D1_RECT_F m_learningFilterBounds[5]{}; // All, Python, JS, PHP, Others
    D2D1_RECT_F m_learningSortBounds{};

    // Legacy bounds for backward compatibility
    D2D1_RECT_F m_studentCardBounds[4]{};
    D2D1_RECT_F m_studentCardBtnBounds[4]{};
    D2D1_RECT_F m_studentQuickNbBtn{};
    D2D1_RECT_F m_studentQuickScratchBtn{};
    D2D1_RECT_F m_studentQuickDbBtn{};

    struct RenderedCourseCard {
        size_t courseIndex = 0;
        D2D1_RECT_F bounds{};
    };
    std::vector<RenderedCourseCard> m_renderedCourseCards;
    D2D1_RECT_F m_courseOpenRuntimesBtnBounds{};
    float m_courseScrollY = 0.0f;
    float m_courseMaxScrollY = 0.0f;

    void InitializeLearningCourses();

public:
    ID2D1Bitmap* GetLearningHubBitmap(ID2D1RenderTarget* pRT, const std::wstring& filename);
    [[nodiscard]] const std::vector<RenderedCourseCard>& GetRenderedCourseCards() const noexcept { return m_renderedCourseCards; }
    [[nodiscard]] const D2D1_RECT_F* GetLearningFilterBounds() const noexcept { return m_learningFilterBounds; }
    [[nodiscard]] const D2D1_RECT_F& GetLearningSortBounds() const noexcept { return m_learningSortBounds; }
    void RenderStudentView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                           float topY, float clientHeight);
    void RenderCourseView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                          float topY, float clientHeight);
    void RenderMarkdownPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                               float topY, float clientHeight);
    bool HandleMarkdownPreviewWheel(short delta, editor::OpenDocument& doc) noexcept;

    void RenderExtensionDetail(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                               float topY, float clientHeight);
    bool HandleExtensionDetailWheel(short delta, editor::OpenDocument& doc) noexcept;
    bool HandleExtensionDetailClick(POINT pt, editor::OpenDocument& doc);

    // --- #28 Integrated Test Explorer Special View & Gutter Test Status ---
    struct GutterTestHit {
        size_t line0 = 0;
        std::wstring testId;
        D2D1_RECT_F bounds{};
    };
    struct TestRowHit {
        size_t itemIndex = 0;
        D2D1_RECT_F rowBounds{};
        D2D1_RECT_F runBtnBounds{};
    };
    void SetTestResults(std::vector<phide::dev::TestItem> items, phide::dev::TestRunSummary summary);
    void SetTestIsRunning(bool running) noexcept { m_testIsRunning = running; }
    [[nodiscard]] bool IsTestRunning() const noexcept { return m_testIsRunning; }
    [[nodiscard]] const std::vector<phide::dev::TestItem>& GetTestItems() const noexcept { return m_testItems; }
    [[nodiscard]] bool HitTestGutterTest(POINT pt, size_t& outLine0, std::wstring& outTestId) const noexcept;
    void RenderTestExplorerView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                float canvasLeft, float canvasWidth, float topY, float clientHeight);
    bool HandleTestExplorerMouseDown(POINT pt, const app::AppConfig& config);
    bool HandleTestExplorerMouseWheel(short delta) noexcept;

    // #42 SQL Query Console & Schema Browser View (`sql_console`)
    void SetSqlWorkspaceRoot(const std::wstring& root,
                             const std::wstring& mysqlHost = L"localhost",
                             int mysqlPort = 3306,
                             const std::wstring& mysqlUser = L"root",
                             const std::wstring& mysqlPass = L"",
                             const std::wstring& mysqlDb = L"");
    void SetSqlProfiles(const std::vector<phide::db::SqlConnectionProfile>& profiles);
    void SetSqlQueryText(const std::wstring& sql);
    [[nodiscard]] std::wstring GetSqlQueryText() const { return m_sqlQueryDoc.buffer.GetText(); }
    [[nodiscard]] const editor::OpenDocument& GetSqlQueryDocument() const noexcept { return m_sqlQueryDoc; }
    [[nodiscard]] bool IsSqlQuerySelecting() const noexcept { return m_sqlQueryDoc.isSelecting; }
    [[nodiscard]] const D2D1_RECT_F& GetSqlQueryBoxBounds() const noexcept { return m_rcSqlQueryBox; }
    [[nodiscard]] const D2D1_RECT_F& GetSqlQueryTextBounds() const noexcept { return m_rcSqlQueryTextInner; }
    void RefreshActiveSqlSchema();
    void ExecuteActiveSqlQuery();
    void SetSqlNotifyHwnd(HWND hwnd) noexcept { m_sqlNotifyHwnd = hwnd; }
    bool ApplyPendingSqlAsyncResults();
    void StartRefreshActiveSqlSchemaAsync();
    void StartExecuteActiveSqlQueryAsync();
    void StartDiscoverSqlServersAsync();
    void StartToggleActiveSqlServerAsync();
    void UpdateMysqlProfilesPort(int newPort);
    void SetSqlSelectedProfileIndex(size_t idx) noexcept {
        if (idx < m_sqlProfiles.size()) m_sqlSelectedProfileIdx = idx;
    }
    /** @brief Switches active SQL server profile and asynchronously inspects its schema (syncs ERD + Console). */
    void SelectSqlProfile(size_t idx);
    /** @brief Switches active database/schema name on the current server profile and refreshes tables. */
    void SelectSqlDatabase(const std::wstring& dbName);
    /** @brief Asynchronously inspects another profile/database as the baseline schema for ERD Schema Diff. */
    void StartCompareSqlDatabaseAsync(size_t profileIdx, const std::wstring& dbName);
    /** @brief Pushes current SQL connection profiles, busy status, and errors into ErdView. */
    void SyncErdViewConnections();
    void SetSqlPageSize(size_t pageSize) noexcept {
        m_sqlPageSize = (std::max<size_t>)(10, (std::min<size_t>)(500, pageSize));
        m_sqlPage = 0;
    }
    [[nodiscard]] const std::vector<std::wstring>& GetSqlQueryHistory() const noexcept { return m_sqlQueryHistory; }
    [[nodiscard]] const std::vector<std::pair<std::wstring, std::wstring>>& GetSqlSavedSnippets() const noexcept { return m_sqlSavedSnippets; }
    void SaveCurrentSqlAsSnippet(const std::wstring& name = L"");
    void SaveSqlConsoleState() const;
    void LoadSqlConsoleState();
    [[nodiscard]] std::wstring GetActiveSqlServerActionLabel() const;
    [[nodiscard]] const D2D1_RECT_F& GetSqlServerToggleButtonBounds() const noexcept { return m_rcSqlStartMysqlBtn; }
    [[nodiscard]] const std::wstring& GetSqlStatusBanner() const noexcept { return m_sqlStatusBanner; }
    [[nodiscard]] bool IsSqlBusy() const noexcept { return m_sqlIsBusy; }
    [[nodiscard]] const phide::db::SqlQueryResult& GetSqlQueryResult() const noexcept { return m_sqlResult; }
    [[nodiscard]] const std::vector<phide::db::SqlConnectionProfile>& GetSqlProfiles() const noexcept { return m_sqlProfiles; }
    [[nodiscard]] const std::vector<phide::db::SqlTableInfo>& GetSqlTables() const noexcept { return m_sqlTables; }
    /**
     * @brief Returns combined schema tables across all connected databases + workspace .sql DDL files
     *        (active DB first) so host-language SQL validation never emits false positives across DBs.
     */
    [[nodiscard]] std::vector<phide::db::SqlTableInfo> GetSqlValidationTables() const;
    /** @brief Returns the currently selected database/schema name on the active SQL profile (empty if none). */
    [[nodiscard]] std::wstring GetActiveSqlDatabaseName() const {
        return (m_sqlSelectedProfileIdx < m_sqlProfiles.size())
            ? m_sqlProfiles[m_sqlSelectedProfileIdx].database
            : std::wstring{};
    }
    [[nodiscard]] phide::db::SqlDialect GetActiveSqlDialect() const noexcept {
        return (m_sqlSelectedProfileIdx < m_sqlProfiles.size())
            ? m_sqlProfiles[m_sqlSelectedProfileIdx].dialect
            : phide::db::SqlDialect::MySQL;
    }
    [[nodiscard]] std::wstring GetActiveSqlSchemaLabel() const {
        if (m_sqlSelectedProfileIdx < m_sqlProfiles.size()) {
            const auto& p = m_sqlProfiles[m_sqlSelectedProfileIdx];
            if (!p.database.empty()) return p.name + L" / " + p.database;
            return p.name;
        }
        return L"Workspace Schema";
    }
    [[nodiscard]] size_t GetSqlSelectedProfileIndex() const noexcept { return m_sqlSelectedProfileIdx; }
    [[nodiscard]] LPCWSTR GetSqlConsoleCursorForPoint(POINT pt) const noexcept;
    bool UpdateSqlConsoleHover(POINT pt) noexcept;
    void RenderSqlConsoleView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              float canvasLeft, float canvasWidth, float topY, float clientHeight);
    bool HandleSqlConsoleMouseDown(POINT pt, const app::AppConfig& config);
    bool HandleSqlConsoleDoubleClick(POINT pt, const app::AppConfig& config);
    bool HandleSqlConsoleMouseMove(POINT pt, const app::AppConfig& config, bool leftButtonDown);
    bool HandleSqlConsoleMouseUp(POINT pt);
    bool HandleSqlConsoleMouseWheel(POINT pt, short delta) noexcept;
    bool HandleSqlConsoleChar(wchar_t ch);
    bool HandleSqlConsoleKeyDown(WPARAM wParam, HWND hwnd, int modOverride = -1);
    std::function<void(const std::wstring&)> onSqlStatusToast;
    std::function<void()> onSqlQueryExecuted;
    std::function<void()> onRequestEnsureMysqlRunning;
    std::function<void(int port, bool running)> onMysqlServerStateChanged;
    std::function<void(bool openDiffMode)> onOpenSqlErdView;

    // #49 GUI Snippet Editor View (`snippet_editor`)
    void BindSnippetManager(editor::SnippetManager* pMgr);
    void SyncSnippetEditorFieldsFromSelection();
    void RenderSnippetEditorView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 float canvasLeft, float canvasWidth, float topY, float clientHeight);
    bool HandleSnippetEditorMouseDown(POINT pt, const app::AppConfig& config);
    bool HandleSnippetEditorMouseWheel(POINT pt, short delta) noexcept;
    bool HandleSnippetEditorChar(wchar_t ch);
    bool HandleSnippetEditorKeyDown(WPARAM wParam);

    std::function<void()> onTestRunAll;
    std::function<void()> onTestStop;
    std::function<void()> onTestRerunFailed;
    std::function<void()> onTestRefresh;
    std::function<void()> onToggleCoverageGutter;
    std::function<void()> onRefreshCoverage;
    std::function<void(const phide::dev::TestItem&)> onTestRunSingle;
    std::function<void(const std::wstring& filePath, size_t line0)> onTestOpenLocation;
    [[nodiscard]] bool IsPointInTestExplorerInteractive(POINT pt) const noexcept;

    void SetWorkspaceCoverage(phide::dev::WorkspaceCoverageSummary cov);
    [[nodiscard]] const phide::dev::WorkspaceCoverageSummary& GetWorkspaceCoverage() const noexcept { return m_workspaceCoverage; }

    // #32 GUI Workspace Task Runner View (`task_runner`)
    void SetWorkspaceTasks(std::vector<phide::dev::WorkspaceTaskDef> tasks);
    [[nodiscard]] const std::vector<phide::dev::WorkspaceTaskDef>& GetWorkspaceTasks() const noexcept { return m_workspaceTasks; }
    void RenderTaskRunnerView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              float canvasLeft, float canvasWidth, float topY, float clientHeight);
    bool HandleTaskRunnerMouseDown(POINT pt, const app::AppConfig& config);
    bool HandleTaskRunnerMouseWheel(short delta) noexcept;
    std::function<void(const phide::dev::WorkspaceTaskDef&)> onTaskRun;
    std::function<void(const phide::dev::WorkspaceTaskDef&)> onTaskStop;
    std::function<void(const phide::dev::WorkspaceTaskDef&)> onTaskRestart;
    std::function<bool(const std::wstring& label)> onIsTaskRunning;
    std::function<DWORD(const std::wstring& label)> onGetTaskExitCode;
    std::function<void()> onTaskRefresh;

    // #40 Active-File Git History (--follow) View (`file_history`)
    void SetFileHistoryTarget(const std::wstring& fullPath, const std::wstring& relPath, bool loading);
    void SetFileHistoryEntries(const std::wstring& fullPath,
                               std::vector<phide::git::GitWorkflowEngine::FileHistoryEntry> entries);
    [[nodiscard]] const std::vector<phide::git::GitWorkflowEngine::FileHistoryEntry>& GetFileHistoryEntries() const noexcept {
        return m_fileHistoryEntries;
    }
    void RenderFileHistoryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               float canvasLeft, float canvasWidth, float topY, float clientHeight);
    bool HandleFileHistoryMouseDown(POINT pt, const app::AppConfig& config);
    bool HandleFileHistoryMouseWheel(short delta) noexcept;
    std::function<void()> onFileHistoryRefresh;
    std::function<void(const std::wstring& commitHash, const std::wstring& relPath)> onFileHistoryOpenCommitDiff;
    std::function<void(const std::wstring& snapshotPath, const std::wstring& displayTime, const std::wstring& targetPath)> onFileHistoryOpenTimelineDiff;
    std::function<void(const std::wstring& snapshotPath, const std::wstring& targetPath)> onFileHistoryRestoreTimeline;
    float m_lastExtSubTabY = 0.0f;
    float m_lastExtSubTabH = 0.0f;
    float m_lastExtTab0X = 0.0f;
    float m_lastExtTab0W = 0.0f;
    float m_lastExtTab1X = 0.0f;
    float m_lastExtTab1W = 0.0f;
    float m_lastExtTab2X = 0.0f;
    float m_lastExtTab2W = 0.0f;
    D2D1_RECT_F m_lastExtDisableBtn{};
    D2D1_RECT_F m_lastExtUninstallBtn{};

    struct CodeLensAction {
        enum class ActionKind { Run, Debug, References, RunTest, DebugTest, AcceptCurrent, AcceptIncoming, AcceptBoth, OpenThreeWayMerge };
        ActionKind kind = ActionKind::Run;
        std::wstring symbolName;
        size_t conflictIdx = 0;
        size_t bufferLine = 0;
        int groupIdx = 0;
        bool isDebug = false;
        D2D1_RECT_F bounds{};
    };
    std::vector<CodeLensAction> m_activeCodeLensActions;
    int m_hoveredCodeLensIdx = -1;

    [[nodiscard]] int HitTestCodeLens(POINT pt) const noexcept;
    [[nodiscard]] bool HasHoveredCodeLens() const noexcept { return m_hoveredCodeLensIdx >= 0; }
    bool UpdateCodeLensHover(POINT pt);
    [[nodiscard]] const std::vector<CodeLensAction>& GetActiveCodeLensActions() const noexcept { return m_activeCodeLensActions; }
    [[nodiscard]] const D2D1_RECT_F& GetCourseOpenRuntimesBtnBounds() const noexcept { return m_courseOpenRuntimesBtnBounds; }
    [[nodiscard]] int HitTestCourseView(POINT pt) const noexcept;
    bool HandleCourseViewWheel(short delta) noexcept;
    void ResetCourseScroll() noexcept { m_courseScrollY = 0.0f; }
    [[nodiscard]] bool IsLearningSearchFocused() const noexcept { return m_isLearningSearchFocused; }
    void SetLearningSearchFocused(bool f) noexcept { m_isLearningSearchFocused = f; }
    void SetActiveLearningFilter(int f) noexcept { m_learningActiveFilter = f; m_learningScrollY = 0.0f; }
    void CycleLearningSortOrder() noexcept { m_learningSortOrder = (m_learningSortOrder + 1) % 3; }
    [[nodiscard]] int GetActiveLearningFilter() const noexcept { return m_learningActiveFilter; }
    [[nodiscard]] int GetLearningSortOrder() const noexcept { return m_learningSortOrder; }
    [[nodiscard]] const std::wstring& GetLearningSearchQuery() const noexcept { return m_learningSearchQuery; }
    void SetLearningSearchQuery(const std::wstring& q) { m_learningSearchQuery = q; m_learningScrollY = 0.0f; }
    bool HandleLearningChar(wchar_t ch);
    bool HandleLearningKeyDown(WPARAM key);
    bool HandleStudentMouseMove(POINT pt);
    bool HandleStudentMouseWheel(short delta);
    [[nodiscard]] int HitTestStudentView(POINT pt) const;
    [[nodiscard]] const LearningCourseDef& GetLearningCourse(size_t index) const;
    [[nodiscard]] size_t GetLearningCourseCount() const noexcept { return m_learningCourses.size(); }
    void ClearLearningHubBitmaps() noexcept { m_learningHubBitmaps.clear(); }

    std::function<void(ID2D1RenderTarget*, float, float, float, float)> m_onRenderTerminalInEditor;
    void SetOnRenderTerminalInEditor(std::function<void(ID2D1RenderTarget*, float, float, float, float)> cb) {
        m_onRenderTerminalInEditor = std::move(cb);
    }

    std::function<void()> m_onTriggerGarbageCollection;
    void SetOnTriggerGarbageCollection(std::function<void()> cb) {
        m_onTriggerGarbageCollection = std::move(cb);
    }

    struct DevPortInfo {
        int port = 0;
        DWORD pid = 0;
        std::wstring processName;
        std::wstring purpose;
        std::wstring protocol;
        std::wstring status;
        bool isDevelopment = false;
        std::wstring cpuPercentStr = L"0.0%";
        std::wstring memoryStr = L"0.0 MB";
        std::wstring commandLine;
    };

    struct ProjectStorageTelemetry {
        uint64_t totalBytes = 0;
        int fileCount = 0;
        int folderCount = 0;
        uint64_t codeBytes = 0;
        uint64_t assetBytes = 0;
        std::wstring formattedSize = L"0 B";
        ULONGLONG lastScanTick = 0;
    };

    struct ProcessRowHit {
        D2D1_RECT_F rc;
        int index;
    };

    std::vector<DevPortInfo> QueryActiveDevPorts();
    void UpdateProjectStorageTelemetry(const std::wstring& rootPath);

    void RenderMonitorView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          float canvasLeft, float canvasWidth, float topY, float clientHeight);
    bool HandleMonitorClick(POINT pt, const app::AppConfig& config);
    bool HandleMonitorMouseWheel(short delta);
    bool HandleMonitorLButtonDown(POINT pt, const app::AppConfig& config);
    bool HandleMonitorMouseMove(POINT pt);
    void ReleaseMonitorScrollbarDrag();
    [[nodiscard]] bool IsDraggingMonitorScrollbar() const noexcept { return m_isDraggingMonitorScrollbar; }
    void RefreshMonitorTelemetry();
    [[nodiscard]] bool HitTestMonitorInteractive(POINT pt) const;
    [[nodiscard]] bool HitTestMergeCenterPane(const editor::OpenDocument& doc, const app::AppConfig& config,
                                                POINT pt, size_t& outLine, size_t& outCol) const noexcept;
    mutable float m_lastMergeCanvasLeft = 0.0f;
    mutable float m_lastMergeCanvasWidth = 0.0f;
    mutable float m_lastMergeTopY = 0.0f;
    mutable float m_lastMergeClientH = 0.0f;
    double MeasureCurrentProcessCpu();

    D2D1_RECT_F m_rcMonitorRefreshBtn{};
    D2D1_RECT_F m_rcMonitorKillBtn{};
    D2D1_RECT_F m_rcMonitorPrevPage{};
    D2D1_RECT_F m_rcMonitorNextPage{};
    bool KillSelectedDevPortProcess();
    float m_monitorScrollY = 0.0f;
    float m_monitorMaxScrollY = 0.0f;
    bool m_isDraggingMonitorScrollbar = false;
    float m_monitorDragStartMouseY = 0.0f;
    float m_monitorDragStartScrollY = 0.0f;
    D2D1_RECT_F m_rcMonitorScrollTrack{};
    D2D1_RECT_F m_rcMonitorScrollThumb{};
    size_t m_monitorCurrentPage = 0;
    size_t m_monitorPageSize = 8;
    int m_monitorSelectedProcessIndex = 0;
    std::vector<ProcessRowHit> m_monitorRowHits;
    float m_monitorAnimPhase = 0.0f;
    std::vector<DevPortInfo> m_cachedDevPorts;
    ULONGLONG m_lastDevPortsQueryTick = 0;
    ProjectStorageTelemetry m_projectStorage;
    std::vector<float> m_cpuSparklineHistory;
    std::vector<float> m_memSparklineHistory;
    std::vector<float> m_storageSparklineHistory;
    std::vector<float> m_netSparklineHistory;
    std::vector<float> m_gpuSparklineHistory;
    int m_displayRefreshHz = 60;
    float m_monCachedDpiScale = -1.0f;
    std::wstring m_monCachedFontFam;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtBigVal;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtCardTitle;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtCapLabel;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtSub;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtSubRight;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtBodySm;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtBodyRight;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtSmallCenter;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtDonutVal;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_monFmtBadge;
    float m_currentCpuPercent = 0.0f;
    std::wstring m_currentCpuStr = L"0.0%";
    ULONGLONG m_lastCpuSampleTime = 0;
    ULONGLONG m_lastCpuKernelTime = 0;
    ULONGLONG m_lastCpuUserTime = 0;
    int m_numProcessors = 0;

    void RunGarbageCollection(bool clearRenderCaches = true) {
        if (clearRenderCaches) {
            m_minimapBitmapCache.clear();
            m_minimapSearchCache.clear();
            m_docCommentCache.clear();
            m_lineLayoutMap.clear();
            m_lineLayoutLru.clear();
            m_lineLayoutEnvSig = 0;
            m_codeLensWidthCache.clear();
        }
        m_foldChevrons.shrink_to_fit();
        m_visibleColorSwatches.shrink_to_fit();
        m_scratchTabWidths.shrink_to_fit();
        m_visibleTodoCheckboxes.shrink_to_fit();
        m_visibleCommentIndicators.shrink_to_fit();
        m_visibleStickyLines.clear();
        m_visibleStickyLines.shrink_to_fit();
        m_hoveredStickyIdx = -1;
        m_autocompleteItems.shrink_to_fit();
        m_cachedBracketAnalysis.brackets.shrink_to_fit();
        m_cachedBracketAnalysis.pairs.shrink_to_fit();
        m_cachedBufferVersion = static_cast<uint64_t>(-1);
        m_cachedCursorLine = static_cast<size_t>(-1);
        m_cachedCursorCol = static_cast<size_t>(-1);
        m_learningHubBitmaps.clear();
        m_learningCourses.shrink_to_fit();
        m_renderedCourseCards.shrink_to_fit();
        m_breadcrumbHits.shrink_to_fit();
        m_breadcrumbDropdownItems.shrink_to_fit();
        m_activeCodeLensActions.shrink_to_fit();
        m_activeDocSymbols.shrink_to_fit();
        m_testRowHits.shrink_to_fit();
        m_visibleGutterTests.shrink_to_fit();
        m_testItems.shrink_to_fit();
        m_monitorRowHits.shrink_to_fit();
        m_cachedDevPorts.shrink_to_fit();
        if (!m_peekPanel.isOpen) {
            m_peekPanel.items.clear();
            m_peekPanel.previewLines.clear();
        }
        m_peekPanel.items.shrink_to_fit();
        m_peekPanel.previewLines.shrink_to_fit();
        if (!m_findBar.isOpen) {
            m_findBar.matches.clear();
        }
        m_findBar.matches.shrink_to_fit();
        m_findBar.query.shrink_to_fit();
        m_findBar.replaceQuery.shrink_to_fit();
        m_sqlResult.columns.shrink_to_fit();
        m_sqlResult.rows.shrink_to_fit();
        for (auto& r : m_sqlResult.rows) {
            r.shrink_to_fit();
        }
        m_sqlTables.shrink_to_fit();
        m_sqlProfiles.shrink_to_fit();
        m_sqlProfileHits.shrink_to_fit();
        m_sqlDbHits.shrink_to_fit();
        m_sqlTableHits.shrink_to_fit();
        m_sqlColHeaderHits.shrink_to_fit();
        m_snipListHits.shrink_to_fit();
        m_packageManagerView.RunGarbageCollection();
        m_notebookView.RunGarbageCollection();
        m_activityHeatmapView.RunGarbageCollection();
    }
    [[nodiscard]] const ColorSwatchHit* HitTestColorSwatch(POINT pt) const;

    [[nodiscard]] int HitTestStickyHeader(POINT pt) const noexcept;
    [[nodiscard]] const std::vector<StickyScopeLine>& GetVisibleStickyLines() const noexcept { return m_visibleStickyLines; }
    bool SetHoveredStickyIndex(int idx) noexcept;


    [[nodiscard]] bool HitTestTodoCheckbox(POINT pt, TodoCheckboxHit& outHit) const noexcept;
    [[nodiscard]] bool HitTestCommentIndicator(POINT pt, CommentIndicatorHit& outHit) const noexcept;
    [[nodiscard]] const std::vector<CommentIndicatorHit>& GetVisibleCommentIndicators() const noexcept { return m_visibleCommentIndicators; }

    [[nodiscard]] static BetterCommentInfo ClassifyBetterComment(
        const std::wstring& lineStr, size_t startCol, size_t length,
        BetterCommentType inheritedBlockType = BetterCommentType::Normal);
    [[nodiscard]] static ID2D1Brush* ResolveBetterCommentBrush(const Theme& theme, BetterCommentType type) noexcept;
    [[nodiscard]] static D2D1_COLOR_F GetBetterCommentColor(const Theme& theme, BetterCommentType type) noexcept;
    /// Thread-safe static helper that scans a buffer for BetterComment / TODO annotations without mutating UI caches.
    [[nodiscard]] static DocCommentSummary ComputeDocCommentSummary(const editor::Buffer& buffer, const std::wstring& ext);
    [[nodiscard]] const DocCommentSummary& GetDocCommentSummary(const editor::Buffer& buffer, const std::wstring& ext) const;
    void PruneDocCommentCache(const std::vector<uint64_t>& liveBufferIds) const;
    [[nodiscard]] const std::vector<TodoCheckboxHit>& GetVisibleTodoCheckboxes() const noexcept { return m_visibleTodoCheckboxes; }
    [[nodiscard]] const std::vector<ColorSwatchHit>& GetVisibleColorSwatches() const noexcept { return m_visibleColorSwatches; }

    static std::vector<LineColorSpan> ScanLineColors(const std::wstring& lineStr, bool enableTailwind = true);
    static size_t CountSwatchesBefore(const std::vector<LineColorSpan>& spans, size_t col);
    static float GetColumnVisualOffset(const std::vector<LineColorSpan>& spans, size_t col, float charW, float swatchSpan);
    static float GetColumnVisualOffset(const std::wstring& lineText, const std::vector<LineColorSpan>& spans, size_t col, float charW, float swatchSpan, int tabSize = 4);

    [[nodiscard]] static size_t HitTestVisualLineColumn(
        const std::wstring& lineStr,
        const std::vector<LineColorSpan>& lineColors,
        float relX, float charW, float swatchSpan, int tabSize = 4) noexcept;
    [[nodiscard]] static ID2D1Brush* ResolveSyntaxTokenBrush(
        const Theme& theme,
        const editor::SyntaxToken& tok,
        size_t bufLine,
        const std::wstring& lineStr,
        const std::unordered_map<size_t, std::vector<const editor::BracketItem*>>& bracketsByLine,
        BetterCommentType inheritedComment = BetterCommentType::Normal,
        bool enableBracketColorization = true) noexcept;
    static void DrawInlineColorSwatch(
        ID2D1RenderTarget* pRT,
        const LineColorSpan& cs,
        float swatchBaseX, float lineY, float lineH,
        float swatchSpan, float swatchSize,
        const app::AppConfig& config,
        D2D1_RECT_F& outRcSwatch);
    static void DrawWavySquiggle(
        ID2D1RenderTarget* pRT,
        float x1, float x2, float lineY, float lineH,
        ID2D1Brush* brush, const app::AppConfig& config, float strokeW = 1.3f);
    static void DrawActiveBracketPairFrames(
        ID2D1RenderTarget* pRT,
        Theme& theme,
        const app::AppConfig& config,
        const editor::BracketPair& pair,
        float openX, float openY, float closeX, float closeY,
        float charW, float lineH,
        bool drawOpen, bool drawClose);
    static bool HitTestAutocompletePanel(
        POINT pt,
        const D2D1_RECT_F& popupBounds,
        size_t totalItems,
        size_t scrollOffset,
        int& outIdx) noexcept;
    static void RenderAutocompletePopupPanel(
        ID2D1RenderTarget* pRT,
        Theme& theme,
        const app::AppConfig& config,
        const std::vector<editor::IntelliSenseItem>& items,
        size_t selectedIdx,
        size_t& inOutScrollOffset,
        float anchorX, float anchorY, float lineH, float minY,
        float clientWidth, float clientHeight,
        D2D1_RECT_F& outBounds,
        LucideIcons& icons);

    // --- Section 2 Overlay Widgets (#11, #14, #15, #16, #17) ---
    struct BreadcrumbHit {
        D2D1_RECT_F bounds{};
        int segmentIndex = 0;
        bool isSymbolTail = false;
        bool isFolder = false;
        std::wstring label;
        std::wstring resolvedPath;
    };

    struct BreadcrumbDropdownItem {
        D2D1_RECT_F bounds{};
        std::wstring label;
        std::wstring fullPath;
        size_t line = 0;
        size_t col = 0;
        bool isFolder = false;
        bool isSymbol = false;
    };

    struct InlineFindBarState {
        bool isOpen = false;
        bool isReplaceOpen = false;
        int focusedInput = 1; // 1 = find, 2 = replace
        std::wstring query;
        std::wstring replaceQuery;
        bool matchCase = false;
        bool wholeWord = false;
        bool useRegex = false;
        bool inSelection = false;
        bool preserveCase = false;
        bool hasSelectionFilter = false;
        editor::SearchRangeFilter selFilter{};
        std::vector<editor::SearchLineMatch> matches;
        int currentMatchIdx = -1;

        D2D1_RECT_F rcBar{};
        D2D1_RECT_F rcChevron{};
        D2D1_RECT_F rcFindInput{};
        D2D1_RECT_F rcReplaceInput{};
        D2D1_RECT_F rcMatchCaseBtn{};
        D2D1_RECT_F rcWholeWordBtn{};
        D2D1_RECT_F rcRegexBtn{};
        D2D1_RECT_F rcInSelectionBtn{};
        D2D1_RECT_F rcPrevBtn{};
        D2D1_RECT_F rcNextBtn{};
        D2D1_RECT_F rcCloseBtn{};
        D2D1_RECT_F rcPreserveCaseBtn{};
        D2D1_RECT_F rcReplaceOneBtn{};
        D2D1_RECT_F rcReplaceAllBtn{};
    };

    enum class InlineFindAction {
        None,
        Consumed,
        Closed,
        FindNext,
        FindPrev,
        ReplaceCurrent,
        ReplaceAll,
        OptionsChanged
    };

    enum class PeekMode {
        PeekDefinition = 0,
        References = 1,
        CallHierarchy = 2,
        CodeMetrics = 3,
        TypeHierarchy = 4
    };

    struct PeekItemEntry {
        std::wstring label;
        std::wstring detail;
        std::wstring filePath;
        size_t line = 0;
        size_t col = 0;
        std::wstring snippet;
        D2D1_RECT_F bounds{};
    };

    struct InlinePeekPanelState {
        bool isOpen = false;
        bool metricsLoading = false;
        PeekMode mode = PeekMode::PeekDefinition;
        std::wstring title;
        std::wstring subtitle;
        size_t anchorLine = 0;
        float anchorScreenY = -1.0f;
        int selectedIndex = 0;
        int listScrollIdx = 0;
        float codeScrollY = 0.0f;
        std::vector<PeekItemEntry> items;
        std::vector<std::wstring> previewLines;
        size_t previewStartLine = 0;
        editor::WorkspaceCodeMetrics metrics{};

        D2D1_RECT_F rcPanel{};
        D2D1_RECT_F rcCloseBtn{};
        D2D1_RECT_F rcOpenFileBtn{};
    };

    enum class PeekAction {
        None,
        Consumed,
        Closed,
        NavigateToSelected,
        SelectionChanged
    };

    struct InlineRenameState {
        bool isOpen = false;
        bool showPreview = false;
        std::wstring oldName;
        std::wstring newName;
        std::wstring statusNote;
        editor::RenamePlan plan;
        D2D1_RECT_F rcBox{};
        D2D1_RECT_F rcInput{};
        D2D1_RECT_F rcApplyBtn{};
        D2D1_RECT_F rcPreviewBtn{};
        D2D1_RECT_F rcCancelBtn{};
    };

    enum class RenameAction {
        None,
        Consumed,
        Cancelled,
        ApplyRequested,
        PreviewToggled
    };

    void SetActiveDocSymbols(std::vector<editor::DocumentSymbol> syms) { m_activeDocSymbols = std::move(syms); }
    [[nodiscard]] const std::vector<BreadcrumbHit>& GetBreadcrumbHits() const noexcept { return m_breadcrumbHits; }
    [[nodiscard]] bool IsBreadcrumbDropdownOpen() const noexcept { return m_breadcrumbDropdownOpen; }
    void DismissBreadcrumbDropdown() noexcept {
        m_breadcrumbDropdownOpen = false;
        m_breadcrumbDropdownItems.clear();
    }
    bool HandleBreadcrumbClick(POINT pt, const app::AppConfig& config,
                               const std::wstring& activeFilePath, const std::wstring& workspaceRoot,
                               std::wstring& outOpenFile, size_t& outJumpLine, size_t& outJumpCol,
                               bool& outJumpInCurrentFile);

    InlineFindBarState& GetFindBar() noexcept { return m_findBar; }
    const InlineFindBarState& GetFindBar() const noexcept { return m_findBar; }
    void OpenInlineFindBar(bool withReplace, const std::wstring& seedQuery,
                           const editor::SearchRangeFilter* selRange = nullptr);
    void CloseInlineFindBar() noexcept { m_findBar.isOpen = false; }
    void RecomputeInlineFindMatches(const editor::OpenDocument& doc);
    InlineFindAction StepInlineFindMatch(editor::OpenDocument& doc, bool forward);
    InlineFindAction HandleInlineFindClick(POINT pt);
    InlineFindAction HandleInlineFindKeyDown(WPARAM vk, bool ctrl, bool shift);
    bool HandleInlineFindChar(wchar_t ch);

    InlinePeekPanelState& GetPeekPanel() noexcept { return m_peekPanel; }
    const InlinePeekPanelState& GetPeekPanel() const noexcept { return m_peekPanel; }
    void OpenPeekPanel(PeekMode mode, const std::wstring& title, const std::wstring& subtitle,
                       size_t anchorLine, std::vector<PeekItemEntry> items);
    void OpenCodeMetricsLoadingPanel(const std::wstring& activeFileLabel);
    void RenderFileLoadingView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                               float topY, float clientHeight);
    void OpenCodeMetricsPanel(const editor::WorkspaceCodeMetrics& metrics, const std::wstring& activeFileLabel);
    void ClosePeekPanel() noexcept {
        m_peekPanel.isOpen = false;
        m_peekPanel.rcPanel = {};
        m_peekPanel.rcCloseBtn = {};
        m_peekPanel.rcOpenFileBtn = {};
    }
    void LoadSelectedPeekPreview();
    PeekAction HandlePeekClick(POINT pt);
    PeekAction HandlePeekKeyDown(WPARAM vk);
    bool HandlePeekWheel(POINT pt, short delta);
    [[nodiscard]] LPCWSTR GetPeekCursorForPoint(POINT pt) const noexcept;

    InlineRenameState& GetRenameWidget() noexcept { return m_renameWidget; }
    const InlineRenameState& GetRenameWidget() const noexcept { return m_renameWidget; }
    void OpenRenameWidget(const std::wstring& oldName, editor::RenamePlan plan, const std::wstring& statusNote = L"");
    void CloseRenameWidget() noexcept { m_renameWidget.isOpen = false; m_renameWidget.showPreview = false; }
    RenameAction HandleRenameClick(POINT pt);
    RenameAction HandleRenameKeyDown(WPARAM vk);
    bool HandleRenameChar(wchar_t ch);

private:
    /// Shared tab/drag-pill icon renderer covering special views, folders, Python bitmaps, and Seti/Lucide file icons.
    void DrawTabDocumentIcon(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                             const D2D1_RECT_F& iconBounds, const editor::OpenDocument& doc,
                             const std::wstring& title, bool isActive);
    void RenderBreadcrumbDropdown(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);
    void RenderInlineFindBar(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);
    void RenderInlinePeekPanel(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);
    void RenderInlineRenameWidget(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);
    void RenderRichHoverCard(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);
    void RenderSignatureHelpPopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);
    void RenderCodeActionMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight);

public:
    // --- #22 & #23 & #24 Language Intelligence Overlay APIs ---
    [[nodiscard]] bool HitTestBreadcrumbErrorBadge(POINT pt) const noexcept;

    struct HoverCardState {
        bool visible = false;
        size_t line = 0;
        size_t col = 0;
        POINT anchorPt{};
        std::wstring rawMarkdown;
        std::vector<editor::MarkdownBlock> blocks;
        D2D1_RECT_F bounds{};
        D2D1_RECT_F paneBounds{};
    };
    void ShowHoverCard(size_t line, size_t col, POINT screenPt, const std::wstring& markdown);
    void SetHoverCardPaneBounds(const D2D1_RECT_F& bounds) noexcept { m_hoverCard.paneBounds = bounds; }
    void DismissHoverCard() noexcept { m_hoverCard.visible = false; m_hoverCard.bounds = {}; }
    [[nodiscard]] bool IsHoverCardVisible() const noexcept { return m_hoverCard.visible; }
    [[nodiscard]] const HoverCardState& GetHoverCardState() const noexcept { return m_hoverCard; }

    struct SignatureHelpPopupState {
        bool visible = false;
        lsp::SignatureHelpResult result{};
        int selectedOverload = 0;
        D2D1_RECT_F bounds{};
        D2D1_RECT_F prevBtnBounds{};
        D2D1_RECT_F nextBtnBounds{};
    };
    void ShowSignatureHelp(const lsp::SignatureHelpResult& sigHelp);
    void DismissSignatureHelp() noexcept { m_sigHelpPopup.visible = false; m_sigHelpPopup.bounds = {}; }
    [[nodiscard]] bool IsSignatureHelpVisible() const noexcept { return m_sigHelpPopup.visible; }
    [[nodiscard]] bool HandleSignatureHelpMouseDown(POINT pt);
    void CycleSignatureOverload(int delta) noexcept;

    struct CodeActionMenuState {
        bool visible = false;
        bool isRefactorMode = false;
        size_t line = 0;
        int selectedIdx = 0;
        int scrollOffset = 0;
        std::vector<lsp::CodeActionItem> items;
        std::vector<D2D1_RECT_F> itemBounds;
        std::vector<int> itemIndices;
        D2D1_RECT_F bounds{};
        D2D1_RECT_F lightbulbBounds{};
        bool hasLightbulb = false;
        size_t lightbulbLine = 0;
    };
    void SetLightbulbState(bool available, size_t line) noexcept {
        m_codeActionMenu.hasLightbulb = available;
        m_codeActionMenu.lightbulbLine = line;
        if (!available) m_codeActionMenu.lightbulbBounds = {};
    }
    void ShowCodeActionMenu(size_t line, const std::vector<lsp::CodeActionItem>& items, bool isRefactorMode = false);
    void DismissCodeActionMenu() noexcept {
        m_codeActionMenu.visible = false;
        m_codeActionMenu.bounds = {};
        m_codeActionMenu.itemBounds.clear();
        m_codeActionMenu.itemIndices.clear();
    }
    [[nodiscard]] bool IsCodeActionMenuVisible() const noexcept { return m_codeActionMenu.visible; }
    [[nodiscard]] bool HitTestLightbulb(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestCodeActionMenu(POINT pt, int& outItemIdx) const noexcept;
    bool HandleCodeActionMenuMouseMove(POINT pt) noexcept;
    bool HandleCodeActionMenuKeyDown(WPARAM vk, int& outAcceptedIndex);
    bool HandleCodeActionMenuMouseDown(POINT pt, int& outAcceptedIndex);
    [[nodiscard]] int GetCodeActionMenuSelectedIndex() const noexcept { return m_codeActionMenu.selectedIdx; }
    [[nodiscard]] const std::vector<lsp::CodeActionItem>& GetCodeActionMenuItems() const noexcept { return m_codeActionMenu.items; }

private:
    D2D1_RECT_F m_lastActivePaneBounds{250.0f, 90.0f, 1100.0f, 700.0f};
    std::vector<editor::DocumentSymbol> m_activeDocSymbols;
    std::vector<BreadcrumbHit> m_breadcrumbHits;
    D2D1_RECT_F m_breadcrumbErrBadgeBounds{};
    bool m_breadcrumbDropdownOpen = false;
    int m_breadcrumbDropdownSegIdx = -1;
    bool m_breadcrumbDropdownForSymbols = false;
    D2D1_RECT_F m_rcBreadcrumbDropdown{};
    std::vector<BreadcrumbDropdownItem> m_breadcrumbDropdownItems;

    InlineFindBarState m_findBar;
    InlinePeekPanelState m_peekPanel;
    InlineRenameState m_renameWidget;
    HoverCardState m_hoverCard;
    SignatureHelpPopupState m_sigHelpPopup;
    CodeActionMenuState m_codeActionMenu;
    bool m_inMultiPaneRender = false;

    // #28 Test Explorer state
    std::vector<phide::dev::TestItem> m_testItems;
    phide::dev::TestRunSummary m_testSummary{};
    bool m_testIsRunning = false;
    int m_testFilterMode = 0; // 0=All, 1=Failed, 2=Passed
    int m_testSelectedIdx = -1;
    float m_testScrollY = 0.0f;
    float m_testMaxScrollY = 0.0f;
    D2D1_RECT_F m_rcTestRunAllBtn{};
    D2D1_RECT_F m_rcTestRefreshBtn{};
    D2D1_RECT_F m_rcTestFilterBtns[3]{};
    D2D1_RECT_F m_rcTestRerunFailedBtn{};
    D2D1_RECT_F m_rcCovToggleBtn{};
    D2D1_RECT_F m_rcCovRefreshBtn{};
    std::vector<TestRowHit> m_testRowHits;
    phide::dev::WorkspaceCoverageSummary m_workspaceCoverage;

    // #32 GUI Workspace Task Runner state & cached Test/Task brushes (reset in DiscardDeviceResources)
    struct TaskRowHit {
        D2D1_RECT_F rcRow{};
        D2D1_RECT_F rcRunBtn{};
        D2D1_RECT_F rcStopBtn{};
        size_t taskIdx = 0;
    };
    std::vector<phide::dev::WorkspaceTaskDef> m_workspaceTasks;
    std::vector<TaskRowHit> m_taskRowHits;
    float m_taskScrollY = 0.0f;
    float m_taskMaxScrollY = 0.0f;
    D2D1_RECT_F m_rcTaskRefreshBtn{};
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTestRowBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTestRowSel;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTestBtnBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTestPass;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTestFail;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTestWarn;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTestExpectedBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushTestActualBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushCovGreen;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushCovRed;
    ID2D1RenderTarget* m_cachedTestTaskRT = nullptr;
    void EnsureTestTaskBrushes(ID2D1RenderTarget* pRT);
    std::vector<GutterTestHit> m_visibleGutterTests;

    // #42 SQL Query Console state
    struct SqlTableHit {
        D2D1_RECT_F bounds{};
        size_t tableIndex = 0;
    };
    struct SqlProfileHit {
        D2D1_RECT_F bounds{};
        size_t profileIndex = 0;
    };
    struct SqlDbHit {
        D2D1_RECT_F bounds{};
        std::wstring dbName;
    };
    struct SqlColHeaderHit {
        D2D1_RECT_F bounds{};
        size_t colIndex = 0;
    };
    struct SqlAsyncInbox {
        std::mutex mtx;
        std::atomic<bool> alive{ true };
        uint64_t schemaReqGen = 0;
        uint64_t queryReqGen = 0;
        uint64_t discoverReqGen = 0;
        uint64_t serverToggleReqGen = 0;
        uint64_t diffCompareReqGen = 0;
        bool hasSchemaUpdate = false;
        size_t schemaProfileIdx = 0;
        std::vector<phide::db::SqlTableInfo> schemaTables;
        std::vector<phide::db::SqlTableInfo> schemaAllValidationTables;
        std::vector<std::wstring> schemaDatabases;
        std::wstring schemaError;
        bool hasQueryUpdate = false;
        phide::db::SqlQueryResult queryResult;
        int64_t queryServerTotalRows = -1;
        bool queryUsedServerPaging = false;
        size_t queryRequestedPage = 0;
        bool refreshSchemaAfterQuery = false;
        bool hasDiscoverUpdate = false;
        std::vector<phide::db::SqlConnectionProfile> discoveredProfiles;
        bool hasServerToggleUpdate = false;
        size_t toggledProfileIdx = 0;
        bool toggledConnected = false;
        int toggledPort = 0;
        bool toggledSuccess = false;
        std::wstring serverToggleMessage;
        bool hasDiffCompareUpdate = false;
        std::vector<phide::db::SqlTableInfo> diffCompareTables;
        std::wstring diffCompareLabel;
    };
    std::shared_ptr<SqlAsyncInbox> m_sqlAsyncInbox = std::make_shared<SqlAsyncInbox>();
    uint64_t m_sqlSchemaReqGen = 0;
    uint64_t m_sqlQueryReqGen = 0;
    uint64_t m_sqlDiscoverReqGen = 0;
    uint64_t m_sqlServerToggleReqGen = 0;
    uint64_t m_sqlDiffCompareReqGen = 0;
    bool m_sqlIsBusy = false;
    HWND m_sqlNotifyHwnd = nullptr;
    std::wstring m_sqlMysqlHost = L"localhost";
    int m_sqlMysqlPort = 3306;
    std::wstring m_sqlMysqlUser = L"root";
    std::wstring m_sqlMysqlPass;
    std::wstring m_sqlMysqlDb;
    std::vector<phide::db::SqlConnectionProfile> m_sqlProfiles;
    size_t m_sqlSelectedProfileIdx = 0;
    std::vector<phide::db::SqlTableInfo> m_sqlTables;
    std::vector<phide::db::SqlTableInfo> m_allSqlValidationTables; ///< Multi-DB + DDL tables for false-positive-free code SQL validation
    int m_sqlExpandedTableIdx = -1;
    editor::OpenDocument m_sqlQueryDoc;
    float m_sqlQueryLineH = 20.0f;
    float m_sqlQueryCharW = 8.0f;
    float m_sqlQueryScrollX = 0.0f;
    float m_sqlQueryMaxScrollY = 0.0f;
    float m_sqlQueryMaxScrollX = 0.0f;
    int m_sqlQueryTabSize = 4;
    ULONGLONG m_sqlLastLDownTime = 0;
    POINT m_sqlLastLDownPt{ -10000, -10000 };
    void InitSqlQueryDocument(const std::wstring& initialSql);
    void HitTestSqlQueryEditor(POINT pt, const app::AppConfig& config, size_t& outLine, size_t& outCol) const noexcept;
    void EnsureSqlQueryCursorVisible() noexcept;
    phide::db::SqlQueryResult m_sqlResult;
    std::wstring m_sqlSchemaError;
    std::wstring m_sqlStatusBanner;
    bool m_sqlEditorFocused = true;
    size_t m_sqlPage = 0;
    size_t m_sqlPageSize = 50;
    int m_sqlSortCol = -1;
    bool m_sqlSortAsc = true;
    float m_sqlSidebarScrollY = 0.0f;
    float m_sqlMaxSidebarScrollY = 0.0f;
    float m_sqlQueryScrollY = 0.0f;
    float m_sqlGridScrollY = 0.0f;
    float m_sqlGridScrollX = 0.0f;
    float m_sqlMaxGridScrollY = 0.0f;
    int m_sqlHoveredBtnId = 0;
    int m_sqlHoveredProfileIdx = -1;
    int m_sqlHoveredDbIdx = -1;
    int m_sqlHoveredTableIdx = -1;
    int m_sqlHoveredColIdx = -1;
    D2D1_RECT_F m_rcSqlViewport{};
    D2D1_RECT_F m_rcSqlRunBtn{};
    D2D1_RECT_F m_rcSqlRefreshBtn{};
    D2D1_RECT_F m_rcSqlNewMemBtn{};
    D2D1_RECT_F m_rcSqlStartMysqlBtn{};
    D2D1_RECT_F m_rcSqlExportCsvBtn{};
    D2D1_RECT_F m_rcSqlExportJsonBtn{};
    D2D1_RECT_F m_rcSqlErdBtn{};
    D2D1_RECT_F m_rcSqlDiffBtn{};
    D2D1_RECT_F m_rcSqlPrevPageBtn{};
    D2D1_RECT_F m_rcSqlNextPageBtn{};
    D2D1_RECT_F m_rcSqlQueryBox{};
    D2D1_RECT_F m_rcSqlQueryTextInner{};
    D2D1_RECT_F m_rcSqlSidebar{};
    D2D1_RECT_F m_rcSqlGrid{};
    std::vector<SqlProfileHit> m_sqlProfileHits;
    std::vector<SqlDbHit> m_sqlDbHits;
    std::vector<SqlTableHit> m_sqlTableHits;
    std::vector<SqlColHeaderHit> m_sqlColHeaderHits;
    struct SqlCellHit {
        D2D1_RECT_F bounds{};
        size_t row = 0;
        size_t col = 0;
    };
    struct SqlHistoryHit {
        D2D1_RECT_F bounds{};
        std::wstring sql;
    };
    std::vector<SqlCellHit> m_sqlCellHits;
    std::vector<SqlHistoryHit> m_sqlHistoryHits;
    bool m_sqlServerPagedMode = false;
    int64_t m_sqlServerTotalRows = -1;
    std::wstring m_sqlPagedBaseQuery;
    std::wstring m_sqlActiveTableSchema;
    std::wstring m_sqlActiveTableName;
    bool m_sqlCellEditActive = false;
    size_t m_sqlEditRow = 0;
    size_t m_sqlEditCol = 0;
    std::wstring m_sqlCellEditText;
    std::vector<std::wstring> m_sqlQueryHistory;
    std::vector<std::pair<std::wstring, std::wstring>> m_sqlSavedSnippets;
    bool m_sqlHistoryMenuOpen = false;
    D2D1_RECT_F m_rcSqlSaveSnipBtn{};
    D2D1_RECT_F m_rcSqlHistoryBtn{};
    D2D1_RECT_F m_rcSqlHistoryPopup{};

    // #49 GUI Snippet Editor state
    struct SnipListHit {
        D2D1_RECT_F bounds{};
        size_t idx = 0;
    };
    editor::SnippetManager* m_pSnippetMgr = nullptr;
    int m_snipSelectedIdx = -1;
    int m_snipFocusedField = 0; ///< 0=Name, 1=Prefix, 2=Scope, 3=Desc, 4=Body
    std::wstring m_snipDraftName;
    std::wstring m_snipDraftPrefix;
    std::wstring m_snipDraftScope;
    std::wstring m_snipDraftDesc;
    std::wstring m_snipDraftBody;
    size_t m_snipBodyCursor = 0;
    std::wstring m_snipStatusMsg;
    float m_snipListScrollY = 0.0f;
    float m_snipMaxListScrollY = 0.0f;
    D2D1_RECT_F m_rcSnipNewBtn{};
    D2D1_RECT_F m_rcSnipSaveBtn{};
    D2D1_RECT_F m_rcSnipDeleteBtn{};
    D2D1_RECT_F m_rcSnipListPane{};
    D2D1_RECT_F m_rcSnipField[5]{};
    std::vector<SnipListHit> m_snipListHits;

    // Cached render-target brushes for EditorCanvas (recreated only on theme switch / device reset)
    void EnsureCachedEditorBrushes(ID2D1RenderTarget* pRT, const Theme& theme);
    ID2D1RenderTarget* m_cachedEditorBrushRT = nullptr;
    bool m_cachedEditorBrushDark = true;
    bool m_hasCachedEditorBrushes = false;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pOccWordFillBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pOccWordBorderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pRulerBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pStickyBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pStickyHoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pStickySepBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pStickyShadowBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pInlayBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pInlayBorderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pDebugGutterRedBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pDebugGutterAmberBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pDebugGutterGreenBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pDebugFloatBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pInlineBlameBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pUnnecessaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pScopeGuideBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pSelBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pGutterBlueBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pGutterGreenBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pGutterRedBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pScrollbarThumbBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pScrollbarThumbDragBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pClNormalBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pClHoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pBreadcrumbBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pErrBadgeBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pErrBadgeBorderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapSelBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapCursorBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapTargetBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapWordTickBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapHlTickBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapOccBandBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapSliderBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapSliderBgDragBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapSliderBorderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pMinimapSliderBorderDragBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pHScrollThumbBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pHScrollThumbDragBrush;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> m_pPausedArrowGeo;

    struct CodeLineLayoutKey {
        uint64_t bufferInstanceId = 0;
        uint64_t bufferVersion = UINT64_MAX;
        size_t bufferLine = 0;
        size_t sliceStart = 0;
        size_t sliceEnd = 0;
        uint64_t semanticVer = UINT64_MAX;
        std::wstring docExt;

        bool operator==(const CodeLineLayoutKey& o) const noexcept {
            return bufferInstanceId == o.bufferInstanceId &&
                   bufferVersion == o.bufferVersion &&
                   bufferLine == o.bufferLine &&
                   sliceStart == o.sliceStart &&
                   sliceEnd == o.sliceEnd &&
                   semanticVer == o.semanticVer &&
                   docExt == o.docExt;
        }
    };
    struct CodeLineLayoutKeyHash {
        size_t operator()(const CodeLineLayoutKey& k) const noexcept {
            uint64_t h = 14695981039346656037ULL;
            auto mix = [&](uint64_t v) {
                h ^= v;
                h *= 1099511628211ULL;
            };
            mix(k.bufferInstanceId);
            mix(k.bufferVersion);
            mix(k.bufferLine);
            mix(k.sliceStart);
            mix(k.sliceEnd);
            mix(k.semanticVer);
            for (wchar_t c : k.docExt) mix(static_cast<uint64_t>(c));
            return static_cast<size_t>(h);
        }
    };
    struct CodeLineLayoutEntry {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        std::list<CodeLineLayoutKey>::iterator lruIt;
    };
    mutable uint64_t m_lineLayoutEnvSig = 0;
    mutable std::list<CodeLineLayoutKey> m_lineLayoutLru;
    mutable std::unordered_map<CodeLineLayoutKey, CodeLineLayoutEntry, CodeLineLayoutKeyHash> m_lineLayoutMap;

    mutable IDWriteTextFormat* m_cachedClFmt = nullptr;
    mutable float m_cachedClDpi = 0.0f;
    mutable std::unordered_map<std::wstring, float> m_codeLensWidthCache;

    struct MinimapBitmapCacheEntry {
        ID2D1RenderTarget* pRT = nullptr;
        uint64_t bufferInstanceId = 0;
        uint64_t bufferVersion = UINT64_MAX;
        size_t lineCount = 0;
        size_t minimapStartLine = 0;
        size_t visibleRows = 0;
        float rowStep = 0.0f;
        std::wstring docExt;
        float minimapW = 0.0f;
        float miniH = 0.0f;
        float dpiScale = 1.0f;
        uint64_t themeSig = 0;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };
    std::vector<MinimapBitmapCacheEntry> m_minimapBitmapCache;

    struct MinimapSearchMatchCacheEntry {
        uint64_t bufferInstanceId = 0;
        uint64_t bufferVersion = UINT64_MAX;
        std::wstring query;
        bool matchCase = false;
        std::vector<size_t> matchLines;
    };
    std::vector<MinimapSearchMatchCacheEntry> m_minimapSearchCache;
    mutable std::vector<DocCommentSummary> m_docCommentCache;
    StructuredViewRenderer m_structuredRenderer;

    // #35 Cached inline unified diff rows (avoids 60fps re-diff allocations, bounded to 50000 rows)
    std::wstring m_inlineDiffCacheKey;
    size_t m_inlineDiffCacheLeftCount = 0;
    size_t m_inlineDiffCacheRightCount = 0;
    size_t m_inlineDiffCacheHunkCount = 0;
    size_t m_inlineDiffCacheAddedCount = 0;
    size_t m_inlineDiffCacheModifiedCount = 0;
    size_t m_inlineDiffCacheDeletedCount = 0;
    std::vector<git::GitWorkflowEngine::InlineDiffRow> m_cachedInlineDiffRows;

    // #40 Active-File Git History (--follow) View state (bounded <= 100 entries)
    struct FileHistoryRowHit {
        size_t idx = 0;
        D2D1_RECT_F rowBounds{};
        D2D1_RECT_F diffBtnBounds{};
    };
    std::wstring m_fileHistoryTargetPath;
    std::wstring m_fileHistoryRelPath;
    bool m_fileHistoryLoading = false;
    std::vector<phide::git::GitWorkflowEngine::FileHistoryEntry> m_fileHistoryEntries;
    std::vector<FileHistoryRowHit> m_fileHistoryRowHits;
    float m_fileHistoryScrollY = 0.0f;
    float m_fileHistoryMaxScrollY = 0.0f;
    D2D1_RECT_F m_rcFileHistoryRefreshBtn{};

    // #49 Out-of-workspace Local Timeline snapshots (<= 8 entries/file)
    struct TimelineRowHit {
        size_t idx = 0;
        D2D1_RECT_F rowBounds{};
        D2D1_RECT_F diffBtnBounds{};
        D2D1_RECT_F restoreBtnBounds{};
    };
    std::vector<phide::app::TimelineSnapshotEntry> m_timelineEntries;
    std::vector<TimelineRowHit> m_timelineRowHits;

    void RenderInlineDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                              float topY, float clientHeight, bool isFocused);
    void RenderDiffPeekPopover(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               const editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                               float topY, float clientHeight, float lineH, float gutterW,
                               size_t visualLineIdx);
    void RenderBlameHoverPopover(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 const editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                 float topY, float clientHeight, float lineH, float gutterW,
                                 size_t visualLineIdx);

    bool m_floatingDebugActive = false;
    bool m_floatingDebugPaused = false;
    bool m_floatingDebugDragging = false;
    float m_floatingDebugDragStartX = 0.0f;
    float m_floatingDebugStartOffset = 0.0f;
    float m_floatingDebugOffsetX = 0.0f;
    int m_hoveredFloatingDbgIdx = -1;
    D2D1_RECT_F m_floatingDebugBounds{};
    D2D1_RECT_F m_floatingDebugGripBounds{};
    struct FloatingDbgBtn { int actionId = 0; D2D1_RECT_F bounds{}; };
    std::vector<FloatingDbgBtn> m_floatingDebugBtns;
    std::function<void(int dbgActionId)> m_onFloatingDebugAction;
    void RenderFloatingDebugToolbar(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    float canvasLeft, float canvasWidth, float headerH);

public:
    [[nodiscard]] StructuredViewRenderer& GetStructuredRenderer() noexcept { return m_structuredRenderer; }
};

} // namespace phide::ui
