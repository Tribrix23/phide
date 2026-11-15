/**
 * @file ActivityHeatmapView.hpp
 * @brief Interactive GitHub-style Commit, Code Churn & IDE Activity Heatmap view.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @enum HeatmapMetric
 * @brief Data dimension displayed in the contribution heatmap grid.
 */
enum class HeatmapMetric {
    Commits = 0,
    CodeChurn = 1,
    IdeActivity = 2
};

/**
 * @enum HeatmapRange
 * @brief Displayed week span (`Auto` shrinks cleanly on narrow panes so squares never squeeze).
 */
enum class HeatmapRange {
    Auto = 0,
    Weeks16 = 16,
    Weeks26 = 26,
    Weeks52 = 52
};

/**
 * @struct HeatmapCommitEntry
 * @brief Lightweight metadata for a Git commit rendered in the day drill-down list.
 */
struct HeatmapCommitEntry {
    std::wstring hash;
    std::wstring shortHash;
    std::wstring date;      ///< YYYY-MM-DD
    std::wstring author;
    std::wstring message;
    int additions = 0;
    int deletions = 0;
    bool isMine = false;
};

/**
 * @struct HeatmapDayRecord
 * @brief Aggregated Git and local IDE activity metrics for a single calendar date (`YYYY-MM-DD`).
 */
struct HeatmapDayRecord {
    std::wstring date;
    int commitCount = 0;
    int myCommitCount = 0;
    int linesAdded = 0;
    int linesDeleted = 0;
    int myLinesAdded = 0;
    int myLinesDeleted = 0;

    // Local discrete IDE milestones persisted across sessions
    int saves = 0;
    int commitsTriggered = 0;
    int builds = 0;
    int testsPassed = 0;
    int testsFailed = 0;
    int sqlQueries = 0;

    [[nodiscard]] int TotalChurn(bool onlyMine) const noexcept {
        return onlyMine ? (myLinesAdded + myLinesDeleted) : (linesAdded + linesDeleted);
    }

    [[nodiscard]] int TotalIdeActions() const noexcept {
        return saves + commitsTriggered + builds + testsPassed + testsFailed + sqlQueries;
    }
};

/**
 * @struct HeatmapStats
 * @brief Summary KPI metrics computed over the currently displayed heatmap window.
 */
struct HeatmapStats {
    int totalValue = 0;
    int activeDays = 0;
    int currentStreak = 0;
    int longestStreak = 0;
    int peakValue = 0;
    std::wstring peakDate;
    int displayedWeeks = 52;
    int windowDays = 365;
};

/**
 * @class ActivityHeatmapView
 * @brief Responsive GitHub-style Contribution & IDE Activity Heatmap view with async Git ingestion,
 *        hover tooltips, adaptive week sizing, and clickable commit-diff drill-down.
 */
class ActivityHeatmapView {
public:
    struct CellHit {
        D2D1_RECT_F bounds{};
        std::wstring date;
        int value = 0;
        int secondaryValue = 0; ///< e.g. deletions when Churn, or commitCount for Ide breakdown
        int level = 0;
        bool isFuture = false;
    };

    struct PillHit {
        D2D1_RECT_F bounds{};
        int id = 0;
    };

    struct CommitHit {
        D2D1_RECT_F bounds{};
        std::wstring hash;
    };

    ActivityHeatmapView();
    ~ActivityHeatmapView();

    /**
     * @brief Records a discrete workspace milestone cleanly with zero hot-loop overhead.
     */
    void RecordSave();
    void RecordCommitMilestone();
    void RecordBuild();
    void RecordTestRun(bool allPassed);
    void RecordSqlQuery();

    /**
     * @brief Launches an asynchronous background `git log` + `--numstat` scan for `repoRoot`.
     * Posts `notifyMsg` to `notifyHwnd` when pending results are ready to apply.
     */
    void StartRefreshAsync(const std::wstring& repoRoot, HWND notifyHwnd, UINT notifyMsg);

    /**
     * @brief Consumes completed background Git scan results on the UI thread.
     * @return True if state changed and requires repaint.
     */
    bool ApplyPendingRefresh();

    /**
     * @brief Ensures persisted IDE activity history is loaded once from disk.
     */
    void EnsureLoaded();

    /**
     * @brief Flushes modified IDE activity records atomically if dirty (or forced on exit/GC).
     */
    void SaveIdeHistoryIfDirty(bool force = false);

    /**
     * @brief Renders the responsive heatmap view inside the editor pane bounds.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float canvasLeft, float canvasWidth, float topY, float clientHeight);

    bool HandleMouseMove(POINT pt);
    bool HandleLButtonDown(POINT pt, const app::AppConfig& config);
    bool HandleLButtonUp();
    bool HandleMouseWheel(short delta);
    [[nodiscard]] LPCWSTR GetCursorForPoint(POINT pt) const noexcept;
    [[nodiscard]] bool IsDraggingScrollbar() const noexcept { return m_isDraggingScrollbar; }
    void ReleaseScrollbarDrag() noexcept { m_isDraggingScrollbar = false; }

    void SetOnOpenCommitDiff(std::function<void(const std::wstring& hash)> cb) {
        m_onOpenCommitDiff = std::move(cb);
    }
    void SetOnRequestRefresh(std::function<void()> cb) {
        m_onRequestRefresh = std::move(cb);
    }

    /**
     * @brief Releases transient hit vectors and compacts aged records (>400 days) during GC.
     */
    void RunGarbageCollection();

    // --- State Accessors & Pure Helper API (also exercised by automated tests) ---
    void SetMetric(HeatmapMetric metric) noexcept { m_metric = metric; }
    [[nodiscard]] HeatmapMetric GetMetric() const noexcept { return m_metric; }
    void SetRange(HeatmapRange range) noexcept { m_range = range; }
    [[nodiscard]] HeatmapRange GetRange() const noexcept { return m_range; }
    void SetOnlyMyCommits(bool mine) noexcept { m_onlyMyCommits = mine; }
    [[nodiscard]] bool GetOnlyMyCommits() const noexcept { return m_onlyMyCommits; }

    [[nodiscard]] const std::wstring& GetCurrentRepoPath() const noexcept { return m_repoPath; }
    [[nodiscard]] bool IsGitRepo() const noexcept { return m_isGitRepo; }
    [[nodiscard]] bool IsLoading() const noexcept { return m_isLoading; }
    [[nodiscard]] const std::wstring& GetSelectedDate() const noexcept { return m_selectedDate; }
    [[nodiscard]] const std::wstring& GetHoveredDate() const noexcept { return m_hoveredDate; }
    [[nodiscard]] int GetLastRenderedWeeks() const noexcept { return m_lastRenderedWeeks; }
    [[nodiscard]] float GetLastCellSize() const noexcept { return m_lastCellSize; }
    [[nodiscard]] const std::vector<CellHit>& GetCellHits() const noexcept { return m_cellHits; }
    [[nodiscard]] const std::vector<CommitHit>& GetCommitHits() const noexcept { return m_commitHits; }
    [[nodiscard]] std::vector<HeatmapCommitEntry> GetCommitsForDate(const std::wstring& ymd) const;
    [[nodiscard]] HeatmapStats ComputeStats(int weeks, const std::wstring& endYmd) const;
    [[nodiscard]] int GetValueForDate(const std::wstring& ymd) const noexcept;

    // --- Testing & Deterministic Helpers ---
    void SetReferenceDateForTesting(const std::wstring& ymd) { m_overrideTodayYmd = ymd; }
    void SetStoragePathForTesting(const std::wstring& filePath);
    void ApplyGitScanForTesting(const std::wstring& repoRoot, bool isGitRepo, const std::wstring& localAuthor,
                                std::map<std::wstring, HeatmapDayRecord> gitDays,
                                std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>> commitsByDate);

    static bool ParseDateYmd(const std::wstring& ymd, int& outYear, int& outMonth, int& outDay) noexcept;
    static std::wstring FormatDateYmd(int year, int month, int day);
    static int64_t DaysFromCivil(int year, int month, int day) noexcept;
    static void CivilFromDays(int64_t daysSinceEpoch, int& outYear, int& outMonth, int& outDay) noexcept;
    static int DayOfWeekYmd(const std::wstring& ymd) noexcept; ///< 0 = Sunday .. 6 = Saturday
    static std::wstring AddDaysYmd(const std::wstring& ymd, int deltaDays);
    static std::wstring TodayLocalYmd();
    static void ParseGitLogOutput(const std::wstring& output, const std::wstring& localAuthor,
                                  std::map<std::wstring, HeatmapDayRecord>& inOutDays,
                                  std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>>& inOutCommits);
    static void ParseGitNumstatOutput(const std::wstring& output, const std::wstring& localAuthor,
                                      std::map<std::wstring, HeatmapDayRecord>& inOutDays,
                                      std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>>& inOutCommits);

private:
    struct AsyncInbox {
        std::mutex mtx;
        bool hasUpdate = false;
        uint64_t reqGen = 0;
        std::wstring repoPath;
        bool isGitRepo = false;
        std::wstring gitAuthor;
        std::map<std::wstring, HeatmapDayRecord> gitDays;
        std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>> commitsByDate;
    };

    void EnsureTextFormats(Theme& theme, const app::AppConfig& config);
    [[nodiscard]] std::wstring EffectiveTodayYmd() const;
    [[nodiscard]] int ResolveTargetWeeks(float gridAvailW, float scale, int& outMaxFitWeeks) const noexcept;
    [[nodiscard]] int ComputeIntensityLevel(int value, int maxWindowVal) const noexcept;
    [[nodiscard]] D2D1_COLOR_F GetLevelColor(int level, bool isDark) const noexcept;
    void AutoSelectDefaultDayIfNeeded(const std::wstring& endYmd);
    void PruneAgedIdeDays(const std::wstring& todayYmd);
    void LoadIdeHistory();
    [[nodiscard]] std::wstring ResolveDefaultStoragePath() const;
    [[nodiscard]] std::wstring FormatDateDisplay(const std::wstring& ymd) const;
    [[nodiscard]] std::wstring FormatMetricTooltipText(const HeatmapDayRecord* pRec, const std::wstring& ymd) const;

    HeatmapMetric m_metric = HeatmapMetric::Commits;
    HeatmapRange m_range = HeatmapRange::Auto;
    bool m_onlyMyCommits = false;

    std::wstring m_repoPath;
    std::wstring m_gitAuthor;
    bool m_isGitRepo = false;
    bool m_isLoading = false;
    bool m_hasLoadedGitOnce = false;

    // Unified map keyed by YYYY-MM-DD (holds both IDE metrics and latest Git fields)
    std::map<std::wstring, HeatmapDayRecord> m_days;
    std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>> m_commitsByDate;

    std::wstring m_selectedDate;
    std::wstring m_hoveredDate;
    int m_hoveredPillId = 0;
    int m_hoveredCommitIdx = -1;
    POINT m_lastMousePt{ -1, -1 };

    float m_scrollY = 0.0f;
    float m_maxScrollY = 0.0f;
    float m_contentTotalHeight = 0.0f;
    bool m_isDraggingScrollbar = false;
    float m_scrollDragStartY = 0.0f;
    float m_scrollDragStartScrollY = 0.0f;
    D2D1_RECT_F m_rcViewport{};
    D2D1_RECT_F m_rcScrollTrack{};
    D2D1_RECT_F m_rcScrollThumb{};
    D2D1_RECT_F m_rcRefreshBtn{};
    D2D1_RECT_F m_rcSwitchIdeBtn{};

    int m_lastRenderedWeeks = 0;
    float m_lastCellSize = 0.0f;

    std::vector<CellHit> m_cellHits;
    std::vector<PillHit> m_pillHits;
    std::vector<CommitHit> m_commitHits;

    // Persistence state for local IDE milestones
    bool m_ideLoaded = false;
    bool m_ideDirty = false;
    ULONGLONG m_lastIdeSaveTick = 0;
    std::wstring m_customStoragePath;
    std::wstring m_overrideTodayYmd;

    // Async refresh coordination
    std::shared_ptr<std::atomic<bool>> m_isAlive = std::make_shared<std::atomic<bool>>(true);
    std::atomic<uint64_t> m_refreshGen{ 0 };
    std::shared_ptr<AsyncInbox> m_asyncInbox = std::make_shared<AsyncInbox>();

    std::function<void(const std::wstring& hash)> m_onOpenCommitDiff;
    std::function<void()> m_onRequestRefresh;

    // Cached scale-dependent text formats
    float m_fmtScale = 0.0f;
    std::wstring m_fmtFamily;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtTitle;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtHeading;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBody;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtSmall;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtMetricValue;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtMono;
};

} // namespace phide::ui
