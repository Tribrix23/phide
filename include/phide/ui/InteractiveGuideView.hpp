/**
 * @file InteractiveGuideView.hpp
 * @brief Modern interactive feature guide powered by real embedded EditorCanvas code editors.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/ui/ColorPickerModal.hpp"

namespace phide::ui {

class EditorCanvas;

/**
 * @struct GuideEditorBlock
 * @brief Represents an interactive playground powered by PhIDE's real EditorCanvas.
 */
struct GuideEditorBlock {
    int id = 0;
    std::wstring languageExt;
    editor::OpenDocument doc;
    D2D1_RECT_F cardBounds{};
    D2D1_RECT_F codeBoxBounds{};
    D2D1_RECT_F resetBtnBounds{};
    bool isFocused = false;
    bool readOnly = false;

    // Live IntelliSense state (Section 2)
    bool showIntelliSense = false;
    std::vector<editor::IntelliSenseItem> intelliSenseItems;
    size_t intelliSenseSelectedIdx = 0;
    size_t intelliSenseScrollOffset = 0;
    D2D1_RECT_F intelliSensePopupBounds{};

    // Real ghost text demo (Section 1)
    std::wstring ghostText;
    size_t ghostLine = static_cast<size_t>(-1);
    size_t ghostCol = static_cast<size_t>(-1);

    // Cached layout metrics reported by EditorCanvas::RenderEmbeddedCodeBlock
    float lastCaretX = 0.0f;
    float lastCaretY = 0.0f;
    float lastCharW = 8.0f;
    float lastLineH = 22.0f;
    float lastGutterW = 36.0f;
};

/**
 * @class InteractiveGuideView
 * @brief Modern ErdView-inspired interactive walkthrough using real EditorCanvas playgrounds.
 */
class InteractiveGuideView {
public:
    InteractiveGuideView();
    ~InteractiveGuideView() = default;

    void SetHostEditor(EditorCanvas* pHostEditor) noexcept { m_pHostEditor = pHostEditor; }

    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float left, float width, float top, float height,
                EditorCanvas* pHostEditor = nullptr);

    bool HandleMouseMove(POINT clientPt);
    bool HandleLButtonDown(POINT clientPt, bool altPressed = false);
    bool HandleLButtonUp(POINT clientPt);
    bool HandleChar(wchar_t ch);
    bool HandleKeyDown(WPARAM key);
    bool HandleMouseWheel(short delta, float viewHeight);

    void ToggleCaretBlink() noexcept { m_caretBlink = !m_caretBlink; }
    [[nodiscard]] LPCWSTR GetCursorForPoint(POINT pt) const;
    void ResetScroll() noexcept { m_scrollY = 0.0f; }

    std::function<void(const std::wstring& actionId)> onLaunchAction;

private:
    enum class GuideButtonId {
        None = 0,
        ResetAll,
        AcceptGhost,
        ResetGhost,
        TriggerIntelliSense,
        ResetIntelliSense,
        CompleteAllTasks,
        ClearAllTasks,
        ResetTasks,
        AddAccentColor,
        ResetColors,
        JumpMatchedBracket,
        ResetBrackets,
        SelectAllOccurrences,
        AddLineCursors,
        ResetMultiCursor,
        ApiMethodGet,
        ApiMethodPost,
        ApiMethodPut,
        ApiPresetChat,
        ApiPresetUsers,
        ApiPresetHealth,
        ApiSendRequest,
        OpenFullApiTester,
        OpenTaskRunner,
        OpenErd,
        OpenSqlConsole,
        OpenPackages,
        ToggleZenMode
    };

    struct ActionHit {
        GuideButtonId id = GuideButtonId::None;
        D2D1_RECT_F bounds{};
    };

    struct FilterHit {
        int category = 0; // 0=All, 1=Editor & AI, 2=Colors & CSS, 3=Tasks & Tags, 4=Tools & Launchpad
        D2D1_RECT_F bounds{};
    };

    struct BentoJumpHit {
        int sectionIdx = 0; // 0..7
        int filterCat = 0;
        float scrollOffsetY = 0.0f;
        D2D1_RECT_F bounds{};
    };

    EditorCanvas* m_pHostEditor = nullptr;
    app::AppConfig m_lastConfig{};

    float m_scrollY = 0.0f;
    float m_maxScrollY = 0.0f;
    float m_contentTotalHeight = 0.0f;
    D2D1_RECT_F m_viewBounds{};
    D2D1_RECT_F m_scrollTrackBounds{};
    D2D1_RECT_F m_scrollThumbBounds{};

    int m_activeCategory = 0; // 0=All, 1=Editor & AI, 2=Colors & CSS, 3=Tasks & Tags, 4=Tools & Launchpad
    std::vector<GuideEditorBlock> m_editors; // 0: PHP Ghost, 1: JS IntelliSense, 2: TS Tasks, 3: CSS Colors, 4: TSX Brackets/Comments, 5: TS Multi-cursor, 6: API JSON
    int m_focusedEditorIdx = 0;
    bool m_caretBlink = true;
    std::set<int> m_exploredSections;

    // Interactive API Simulator state
    int m_apiMethodIdx = 1; // 0=GET, 1=POST, 2=PUT
    int m_apiPresetIdx = 0; // 0=/v1/chat/completions, 1=/v1/users/profile, 2=/api/health
    int m_apiStatusCode = 200;
    int m_apiLatencyMs = 184;
    int m_apiPayloadBytes = 4820;
    int m_apiSendCount = 1;

    // Hit tracking
    std::vector<ActionHit> m_actionHits;
    std::vector<FilterHit> m_filterHits;
    std::vector<BentoJumpHit> m_bentoHits;
    float m_sectionAnchorY[8]{};
    GuideButtonId m_hoveredAction = GuideButtonId::None;
    int m_hoveredFilter = -1;
    int m_hoveredBento = -1;

    editor::IntelliSenseEngine m_intelliSense;
    ui::ColorPickerModal m_colorPicker;

    void InitializeEditors();
    void ResetEditorBlock(int id);
    void UpdateIntelliSense(GuideEditorBlock& ed);
    void ApplyCompletion(GuideEditorBlock& ed, size_t idx);
    void UpdateApiSimulatorResponse();
    void ExecuteButtonAction(GuideButtonId id);
    void MarkSectionExplored(int secIdx);
};

} // namespace phide::ui
