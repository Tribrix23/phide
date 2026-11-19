/**
 * @file ErdView.hpp
 * @brief Interactive Database ER Diagram (Crow's Foot, UML, Chen, Barker) and Schema Diff /
 *        Migration SQL Generator special view (#10).
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

#include "phide/app/AppConfig.hpp"
#include "phide/db/ErdAndMigrationEngine.hpp"
#include "phide/db/SqlEngine.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @struct ErdCallbacks
 * @brief Host callbacks invoked by ErdView for refreshing live DB schema, picking .sql snapshots,
 *        opening generated migration SQL inside SQL Console, and saving SVG exports.
 */
/**
 * @brief Host callbacks invoked by the interactive ER Diagram view for connection switching,
 *        live schema refresh, async diff inspection, SQL console navigation, and file dialogs.
 */
struct ErdCallbacks {
    std::function<void()> onRefreshLiveSchema;
    std::function<void()> onDetectServers;
    std::function<void(size_t profileIdx)> onSelectProfile;
    std::function<void(const std::wstring& databaseName)> onSelectDatabase;
    std::function<void(size_t profileIdx, const std::wstring& databaseName)> onSelectCompareDatabase;
    std::function<void(const std::wstring& sqlText)> onOpenSqlInConsole;
    std::function<std::wstring()> onPromptOpenSqlFile;
    std::function<std::wstring(const std::wstring& defaultName)> onPromptSaveSvgFile;
};

/**
 * @class ErdView
 * @brief Direct2D interactive ER Diagram canvas with 4 notation modes and side-by-side Schema Diff.
 */
class ErdView {
public:
    ErdView() = default;
    ~ErdView() = default;

    void SetCallbacks(ErdCallbacks cbs) { m_callbacks = std::move(cbs); }

    /**
     * @brief Loads `tables` into the ERD canvas and refreshes auto-layout if table set changed.
     */
    void SetSchemaTables(const std::vector<phide::db::SqlTableInfo>& tables,
                         phide::db::SqlDialect dialect,
                         const std::wstring& schemaLabel);

    /**
     * @brief Syncs discovered database server profiles, selected index, busy status, and error string
     *        so the ERD displays live server & database switcher pills matching SQL Query Console.
     */
    void SetConnectionProfiles(const std::vector<phide::db::SqlConnectionProfile>& profiles,
                               size_t selectedIdx,
                               bool isBusy,
                               const std::wstring& schemaError);

    /**
     * @brief Sets the baseline/comparison schema (from a .sql file or second DB profile) and recomputes diff.
     */
    void SetComparisonSchema(const std::vector<phide::db::SqlTableInfo>& baselineTables,
                             const std::wstring& baselineLabel);

    /**
     * @brief Switches directly to Schema Diff mode with optional auto-diff against baseline.
     */
    void OpenSchemaDiffMode(bool open);

    void DiscardDeviceResources() noexcept {}

    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float canvasLeft, float canvasWidth, float topY, float clientHeight);

    bool HandleLButtonDown(POINT clientPt, HWND hwnd, const app::AppConfig& config);
    bool HandleLButtonUp(POINT clientPt);
    bool HandleMouseMove(POINT clientPt, const app::AppConfig& config);
    bool HandleMouseWheel(short delta, bool isCtrlDown, POINT clientPt, const app::AppConfig& config);
    bool HandleChar(wchar_t ch);
    bool HandleKeyDown(WPARAM vk);

    /**
     * @brief Returns the appropriate Win32 cursor (IDC_HAND, IDC_IBEAM, IDC_SIZEALL, or IDC_ARROW)
     *        for `clientPt` inside the ER Diagram view.
     */
    [[nodiscard]] LPCWSTR GetCursorForPoint(POINT clientPt) const noexcept;

    /** @brief Read-only accessors for current schema interpretation and routed orthogonal connectors. */
    [[nodiscard]] const phide::db::ErdSchemaInterpretation& GetInterpretation() const noexcept { return m_interp; }
    [[nodiscard]] const std::vector<phide::db::ErdRoutedEdge>& GetRoutedEdges() const noexcept { return m_routedEdges; }

private:
    void EnsureRoutesUpToDate();
    void AutoArrangeNodes();
    void RecalculateNodeHeights();
    void RecomputeDiff();
    D2D1_POINT_2F ScreenToWorld(POINT pt) const noexcept;
    void SetToast(const std::wstring& msg);

    ErdCallbacks m_callbacks;
    std::vector<phide::db::SqlTableInfo> m_tables;
    std::vector<phide::db::SqlTableInfo> m_baselineTables;
    std::vector<phide::db::ErdNodeLayout> m_nodes;
    phide::db::SqlDialect m_dialect = phide::db::SqlDialect::MySQL;
    std::wstring m_schemaLabel = L"Active Schema";
    std::wstring m_baselineLabel = L"None (Load .sql)";
    phide::db::SchemaDiffResult m_diffResult;

    phide::db::ErdSchemaInterpretation m_interp;
    std::vector<phide::db::ErdRoutedEdge> m_routedEdges;
    bool m_routesDirty = true;
    bool m_inferImplicitFks = false;
    bool m_userMovedNodes = false;

    phide::db::ErdNotation m_notation = phide::db::ErdNotation::CrowsFoot;
    bool m_showChenAttributes = true;
    bool m_showDiffPanel = false;
    bool m_searchFocused = false;
    std::wstring m_searchFilter;
    float m_diffScrollY = 0.0f;
    float m_diffMaxScrollY = 0.0f;

    float m_panX = 40.0f;
    float m_panY = 40.0f;
    float m_zoom = 1.0f;
    D2D1_RECT_F m_rcCanvasViewport{};
    D2D1_RECT_F m_rcDiffViewport{};

    int m_selectedNodeIdx = -1;
    int m_draggingNodeIdx = -1;
    bool m_isPanningCanvas = false;
    POINT m_lastDragPt{};
    float m_nodeDragOffsetX = 0.0f;
    float m_nodeDragOffsetY = 0.0f;

    // Connected Server Profiles & Active Database Switcher state (#10)
    struct ErdHitRect {
        D2D1_RECT_F bounds{};
        size_t profileIdx = 0;
        std::wstring dbName;
    };
    std::vector<phide::db::SqlConnectionProfile> m_profiles;
    size_t m_selectedProfileIdx = 0;
    bool m_isConnectionBusy = false;
    std::wstring m_connectionError;
    std::vector<ErdHitRect> m_profileHits;
    std::vector<ErdHitRect> m_dbHits;
    std::vector<ErdHitRect> m_diffDbHits;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwFactory;

    int m_hoveredBtnId = -1;
    int m_hoveredProfileIdx = -1;
    int m_hoveredDbIdx = -1;
    int m_hoveredDiffDbIdx = -1;
    int m_hoveredNodeIdx = -1;
    bool m_hoveredSearchBox = false;
    std::wstring m_toastMsg;
    ULONGLONG m_toastExpire = 0;

    // Toolbar Hit Rects
    D2D1_RECT_F m_rcBtnCrowsFoot{};
    D2D1_RECT_F m_rcBtnUml{};
    D2D1_RECT_F m_rcBtnChen{};
    D2D1_RECT_F m_rcBtnBarker{};
    D2D1_RECT_F m_rcBtnChenAttrs{};
    D2D1_RECT_F m_rcBtnAutoLayout{};
    D2D1_RECT_F m_rcBtnZoomFit{};
    D2D1_RECT_F m_rcBtnRefresh{};
    D2D1_RECT_F m_rcBtnDetect{};
    D2D1_RECT_F m_rcBtnEmptyRefresh{};
    D2D1_RECT_F m_rcBtnLoadSql{};
    D2D1_RECT_F m_rcBtnToggleDiff{};
    D2D1_RECT_F m_rcBtnExportSvg{};
    D2D1_RECT_F m_rcBtnInferFks{};
    D2D1_RECT_F m_rcBtnOpenMigConsole{};
    D2D1_RECT_F m_rcBtnCopyMigration{};
    D2D1_RECT_F m_rcSearchBox{};
};

} // namespace phide::ui
