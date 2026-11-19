/**
 * @file WorkspaceAuditEngine.hpp
 * @brief Deterministic Dead Code, Unused Export, Unused CSS Selector, Unused Dependency Audit (#7),
 *        and .env vs .env.example Drift Detector & Code Env-Key Validator (#8).
 *
 * All scanning is strictly bounded in file count/size, supports cooperative cancellation via
 * std::atomic<bool>, and never persists or leaks .env secret values.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace phide::editor {

/**
 * @enum AuditCategory
 * @brief Classification of a workspace audit or environment drift finding.
 */
enum class AuditCategory {
    UnusedExport = 0,
    UnusedCssSelector = 1,
    UnusedDependency = 2,
    EnvDrift = 3
};

/**
 * @struct AuditFinding
 * @brief Single actionable finding produced by WorkspaceAuditEngine or EnvAuditEngine.
 */
struct AuditFinding {
    AuditCategory category = AuditCategory::UnusedExport;
    int severity = 2;             ///< 1 = Error, 2 = Warning, 3 = Info
    std::wstring symbol;          ///< Export name, CSS selector, package name, or ENV key
    std::wstring filePath;        ///< Full local path to offending file
    std::wstring displayPath;     ///< Workspace-relative display path
    size_t line0 = 0;             ///< 0-based line index
    size_t col0 = 0;              ///< 0-based column index
    std::wstring message;         ///< Human-readable explanation
    bool canSyncEnvExample = false; ///< True when 1-click "Sync Keys to .env.example" can resolve it
};

/**
 * @struct EnvKeyEntry
 * @brief Key name + line index extracted from a .env or .env.example file (never stores secret values).
 */
struct EnvKeyEntry {
    std::wstring key;
    size_t line0 = 0;
};

/**
 * @struct WorkspaceAuditReport
 * @brief Aggregated result of a full dead-code, unused-CSS, dependency, and .env drift audit.
 */
struct WorkspaceAuditReport {
    std::vector<AuditFinding> findings;
    size_t filesScanned = 0;
    size_t unusedExportCount = 0;
    size_t unusedCssCount = 0;
    size_t unusedDepCount = 0;
    size_t envDriftCount = 0;
    uint64_t elapsedMs = 0;
    bool cancelled = false;
    bool completed = false;
};

/**
 * @class EnvAuditEngine
 * @brief Static .env / .env.example drift detector and host-code env key validator (#8).
 */
class EnvAuditEngine {
public:
    /**
     * @brief Extracts key names (never values) from a .env or .env.example file.
     */
    [[nodiscard]] static std::vector<EnvKeyEntry> ParseEnvFileKeys(const std::wstring& filePath);

    /**
     * @brief Parses key names directly from in-memory .env text (never stores values).
     */
    [[nodiscard]] static std::vector<EnvKeyEntry> ParseEnvTextKeys(const std::wstring& text);

    /**
     * @brief Builds the union of known keys defined in workspace .env and .env.example.
     */
    [[nodiscard]] static std::unordered_set<std::wstring> CollectKnownWorkspaceEnvKeys(const std::wstring& workspaceRoot);

    /**
     * @brief Compares .env and .env.example in `workspaceRoot` and returns drift findings.
     */
    [[nodiscard]] static std::vector<AuditFinding> CheckEnvFileDrift(const std::wstring& workspaceRoot);

    /**
     * @brief Scans a single source buffer (`filePath`) for references to undefined env keys.
     *        Only emits findings when `knownKeys` is non-empty (i.e. workspace has .env or .env.example).
     */
    [[nodiscard]] static std::vector<AuditFinding> ScanCodeEnvReferences(
        const std::wstring& filePath,
        const std::wstring& workspaceRoot,
        const std::wstring& content,
        const std::unordered_set<std::wstring>& knownKeys);

    /**
     * @brief Appends missing .env key names (with empty `KEY=` placeholders, never secret values)
     *        to `.env.example` in `workspaceRoot`.
     * @return true if `.env.example` was created/updated cleanly.
     */
    static bool SyncEnvExampleKeys(const std::wstring& workspaceRoot, int& outAddedCount);
};

/**
 * @class WorkspaceAuditEngine
 * @brief Bounded, thread-safe, deterministic analyzer for unused exports, CSS selectors,
 *        dependencies, and .env drift (#7 + #8).
 */
class WorkspaceAuditEngine {
public:
    static constexpr size_t kMaxAuditFiles = 2000u;
    static constexpr uint64_t kMaxAuditFileBytes = 512u * 1024u;

    /**
     * @brief Executes a full workspace dead-code, unused-CSS, dependency, and .env drift audit.
     *        Safe to run on a background worker thread; honors `cancelFlag`.
     */
    [[nodiscard]] static WorkspaceAuditReport RunFullAudit(
        const std::wstring& workspaceRoot,
        const std::atomic<bool>* cancelFlag = nullptr);
};

} // namespace phide::editor
