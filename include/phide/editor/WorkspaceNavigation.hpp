/**
 * @file WorkspaceNavigation.hpp
 * @brief Shared search, fuzzy ranking, document outline, workspace symbol index,
 *        references, call hierarchy, code metrics, cross-file rename, and task discovery.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "phide/workspace/IgnoreEngine.hpp"

namespace phide::editor {

enum class SymbolKind {
    Unknown = 0,
    File,
    Module,
    Namespace,
    Package,
    Class,
    Method,
    Property,
    Field,
    Constructor,
    Enum,
    Interface,
    Function,
    Variable,
    Constant,
    Struct,
    Section
};

struct DocumentSymbol {
    std::wstring name;
    std::wstring detail;
    std::wstring containerName;
    SymbolKind kind = SymbolKind::Function;
    size_t line = 0;       // 0-based start line
    size_t col = 0;        // 0-based start column
    size_t endLine = 0;    // 0-based end line
    int depth = 0;         // nesting depth for outline indentation
};

struct IndexedSymbol {
    std::wstring name;
    std::wstring detail;
    std::wstring filePath;
    SymbolKind kind = SymbolKind::Function;
    size_t line = 0;       // 0-based
    size_t col = 0;        // 0-based
};

struct SearchLineMatch {
    size_t line = 0;
    size_t col = 0;
    size_t length = 0;
};

struct SearchRangeFilter {
    bool restrictToRange = false;
    size_t startLine = 0;
    size_t startCol = 0;
    size_t endLine = 0;
    size_t endCol = 0;
};

struct OpenDocContentSnapshot {
    std::wstring filePath;
    std::wstring content;
    bool isDirty = false;
    std::wstring title;
    bool isUntitled = false;
};

struct ReferenceLocation {
    std::wstring filePath;
    size_t line = 0;       // 0-based
    size_t col = 0;        // 0-based
    size_t length = 0;
    std::wstring previewLine;
    bool isDefinition = false;
};

struct CallHierarchyNode {
    std::wstring name;
    std::wstring detail;
    std::wstring filePath;
    size_t line = 0;
    size_t col = 0;
    bool isIncoming = true;
};

struct FileCodeMetrics {
    std::wstring filePath;
    size_t totalLines = 0;
    size_t codeLines = 0;
    size_t blankLines = 0;
    size_t commentLines = 0;
    size_t symbolCount = 0;
    size_t functionCount = 0;
    size_t classCount = 0;
    size_t cyclomaticComplexity = 1;
    size_t maxNestingDepth = 0;
};

struct WorkspaceCodeMetrics {
    size_t totalFiles = 0;
    size_t totalLines = 0;
    size_t codeLines = 0;
    size_t blankLines = 0;
    size_t commentLines = 0;
    size_t totalFunctions = 0;
    size_t totalClasses = 0;
    size_t avgComplexity = 1;
    std::vector<FileCodeMetrics> topComplexFiles;
};

struct RenameFileEditGroup {
    std::wstring filePath;
    bool isDirtyBuffer = false;
    std::vector<ReferenceLocation> edits;
};

struct RenamePlan {
    std::wstring oldName;
    std::wstring newName;
    size_t totalEdits = 0;
    std::vector<RenameFileEditGroup> fileGroups;
    bool valid = false;
    std::wstring errorMessage;
};

struct DiscoveredTask {
    std::wstring label;
    std::wstring command;
    std::wstring detail;
    bool isDebugConfig = false;
};

/**
 * @brief Converts a replacement string to match the casing pattern of `matchedText`.
 */
[[nodiscard]] std::wstring ApplyPreserveCase(const std::wstring& matchedText, const std::wstring& replacement);

/**
 * @brief Finds all matches on a single line respecting case, whole-word, and regex options.
 */
std::vector<SearchLineMatch> FindMatchesInLine(
    const std::wstring& lineText,
    const std::wstring& query,
    bool matchCase,
    bool matchWord,
    bool useRegex);

/**
 * @brief Finds all matches across an entire buffer within an optional line/col range.
 */
std::vector<SearchLineMatch> FindMatchesInLines(
    const std::vector<std::wstring>& lines,
    const std::wstring& query,
    bool matchCase,
    bool matchWord,
    bool useRegex,
    const SearchRangeFilter& filter = {});

/**
 * @brief Replaces all matches on a single line, optionally supporting regex & preserve case.
 */
bool ReplaceInLine(
    std::wstring& lineText,
    const std::wstring& query,
    const std::wstring& replacement,
    bool matchCase,
    bool matchWord,
    bool useRegex,
    bool preserveCase);

/**
 * @brief Scores a fuzzy match (returns < 0 if query does not match candidate).
 */
[[nodiscard]] int ScoreFuzzyMatch(
    const std::wstring& query,
    const std::wstring& title,
    const std::wstring& subtitle = L"",
    bool isOpen = false,
    bool isRecent = false);

/**
 * @brief Parses ":line" or ":line:col" strings safely (1-based inputs returned as 1-based; col=0 if omitted).
 */
bool ParseLineColSpec(const std::wstring& spec, int& outLine1Based, int& outCol1Based);

/**
 * @brief Returns a short badge label for SymbolKind (e.g. "fn", "cls", "var", "sec").
 */
[[nodiscard]] const wchar_t* GetSymbolKindTag(SymbolKind kind) noexcept;

/**
 * @brief Extracts structured document symbols from source text (fast static parser fallback for all languages).
 */
std::vector<DocumentSymbol> ExtractDocumentSymbols(const std::wstring& text, const std::wstring& ext);

/**
 * @brief Computes code metrics for a single buffer.
 */
FileCodeMetrics ComputeFileMetrics(const std::wstring& filePath, const std::wstring& text, const std::wstring& ext);

/**
 * @brief Computes aggregated workspace code metrics (bounded traversal, skips ignored/vendor dirs).
 */
WorkspaceCodeMetrics ComputeWorkspaceMetrics(
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    const std::vector<OpenDocContentSnapshot>& openSnapshots = {},
    size_t maxFiles = 600);

/**
 * @brief Finds all whole-word identifier references across open buffers + workspace files.
 */
std::vector<ReferenceLocation> FindWorkspaceReferences(
    const std::wstring& symbolName,
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    const std::vector<OpenDocContentSnapshot>& openSnapshots = {},
    size_t maxFiles = 800,
    size_t maxResults = 500);

/**
 * @brief Builds incoming/outgoing call hierarchy nodes around `symbolName` from open buffers + workspace files.
 */
std::vector<CallHierarchyNode> BuildStaticCallHierarchy(
    const std::wstring& symbolName,
    const std::wstring& activeFilePath,
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    const std::vector<OpenDocContentSnapshot>& openSnapshots = {});

/**
 * @brief Validates an identifier name and constructs a multi-file RenamePlan.
 */
RenamePlan BuildStaticRenamePlan(
    const std::wstring& oldName,
    const std::wstring& newName,
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    const std::vector<OpenDocContentSnapshot>& openSnapshots = {},
    size_t maxFiles = 800);

/**
 * @brief Discovers real runnable tasks (npm scripts, composer scripts, Makefile targets, .vscode/tasks.json, scripts/*).
 */
std::vector<DiscoveredTask> DiscoverWorkspaceTasks(const std::wstring& workspaceRoot);

/**
 * @brief Discovers debug / launch configurations (.vscode/launch.json and runtime entrypoints).
 */
std::vector<DiscoveredTask> DiscoverLaunchConfigurations(const std::wstring& workspaceRoot);

/**
 * @class WorkspaceSymbolIndex
 * @brief Asynchronous, non-blocking workspace symbol index for Go to Definition (#14) and Workspace Symbol Search (#13).
 */
class WorkspaceSymbolIndex {
public:
    WorkspaceSymbolIndex() = default;
    ~WorkspaceSymbolIndex();

    void Clear();

    /**
     * @brief Starts an asynchronous background index of `workspaceRoot` (cancels any older run cleanly).
     */
    void RebuildAsync(
        const std::wstring& workspaceRoot,
        const phide::workspace::IgnoreEngine* pIgnore = nullptr,
        std::function<void()> onCompleted = nullptr,
        size_t maxFiles = 1800);

    /**
     * @brief Synchronous bounded index (used in tests or immediate queries on small repos).
     */
    void RebuildSync(
        const std::wstring& workspaceRoot,
        const phide::workspace::IgnoreEngine* pIgnore = nullptr,
        size_t maxFiles = 1800);

    /**
     * @brief Updates symbols for a single open or saved file immediately without re-scanning disk.
     */
    void UpdateFileSymbols(const std::wstring& filePath, const std::wstring& text);

    /**
     * @brief Searches indexed symbols using fuzzy scoring.
     */
    [[nodiscard]] std::vector<IndexedSymbol> SearchSymbols(const std::wstring& query, size_t maxResults = 60) const;

    /**
     * @brief Finds exact or prefix definition matches for `symbolName`.
     */
    [[nodiscard]] std::vector<IndexedSymbol> FindDefinitions(
        const std::wstring& symbolName,
        const std::wstring& preferFilePath = L"") const;

    [[nodiscard]] bool IsIndexing() const noexcept { return m_isIndexing.load(); }
    [[nodiscard]] size_t GetSymbolCount() const;
    [[nodiscard]] size_t GetTotalSymbolCount() const { return GetSymbolCount(); }
    [[nodiscard]] size_t GetIndexedFileCount() const;
    [[nodiscard]] std::wstring GetIndexedRoot() const;

private:
    void IndexRootInternal(
        uint64_t generation,
        std::wstring workspaceRoot,
        std::shared_ptr<phide::workspace::IgnoreEngine> ignoreCopy,
        size_t maxFiles,
        std::function<void()> onCompleted);

    mutable std::mutex m_mutex;
    std::wstring m_indexedRoot;
    std::unordered_map<std::wstring, std::vector<IndexedSymbol>> m_symbolsByFile;
    std::atomic<uint64_t> m_generation{0};
    std::atomic<bool> m_isIndexing{false};
    std::shared_ptr<std::atomic<bool>> m_alive{std::make_shared<std::atomic<bool>>(true)};
};

} // namespace phide::editor
