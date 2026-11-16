/**
 * @file GitWorkflowEngine.hpp
 * @brief Native Git workflow helpers: merge conflict parsing & 3-way resolution (#35),
 *        unified hunk patch building for partial staging/revert (#33), stash parsing (#33),
 *        and branch/tag validation.
 */

#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include "phide/editor/Buffer.hpp"
#include "phide/editor/OpenDocument.hpp"

namespace phide::git {

/**
 * @enum ConflictResolutionChoice
 * @brief Target resolution strategy for a Git merge conflict block.
 */
enum class ConflictResolutionChoice {
    AcceptCurrent,
    AcceptIncoming,
    AcceptBoth
};

/**
 * @struct MergeConflictBlock
 * @brief Represents a single `<<<<<<< ... ======= ... >>>>>>>` (or diff3 `|||||||`) region.
 */
struct MergeConflictBlock {
    size_t startLine = 0;                       ///< 0-based line index of `<<<<<<<`
    size_t baseLine = static_cast<size_t>(-1);  ///< 0-based line index of optional `|||||||` (diff3)
    size_t separatorLine = 0;                   ///< 0-based line index of `=======`
    size_t endLine = 0;                         ///< 0-based line index of `>>>>>>>`

    std::wstring currentLabel;                  ///< Label after `<<<<<<< ` (e.g. "HEAD")
    std::wstring baseLabel;                     ///< Label after `||||||| `
    std::wstring incomingLabel;                 ///< Label after `>>>>>>> ` (e.g. "feature/x")

    std::vector<std::wstring> currentLines;     ///< Lines belonging to Current (HEAD)
    std::vector<std::wstring> baseLines;        ///< Optional Common Ancestor lines
    std::vector<std::wstring> incomingLines;    ///< Lines belonging to Incoming branch
};

/**
 * @struct ThreeWayMergePreview
 * @brief Materialized 3-pane views (Current, Merged/Result, Incoming) for the 3-way Merge Editor.
 */
struct ThreeWayMergePreview {
    std::wstring currentText;
    std::wstring resultText;
    std::wstring incomingText;
    size_t conflictCount = 0;
};

/**
 * @struct GitStashEntry
 * @brief Parsed entry from `git stash list`.
 */
struct GitStashEntry {
    size_t index = 0;
    std::wstring refName;   ///< e.g. L"stash@{0}"
    std::wstring branch;    ///< Branch name if present
    std::wstring message;   ///< Stash description
};

/**
 * @class GitWorkflowEngine
 * @brief Pure stateless engine for Git conflict resolution, partial hunk patches, and stashes.
 */
class GitWorkflowEngine {
public:
    /**
     * @brief Parses all complete merge conflict markers inside a line vector.
     */
    [[nodiscard]] static std::vector<MergeConflictBlock> ParseMergeConflicts(const std::vector<std::wstring>& lines);

    /**
     * @brief Overload parsing directly from an editor Buffer.
     */
    [[nodiscard]] static std::vector<MergeConflictBlock> ParseMergeConflicts(const editor::Buffer& buf);

    /**
     * @brief Replaces a single conflict block (`startLine..endLine`) with Current, Incoming, or Both lines.
     */
    [[nodiscard]] static std::vector<std::wstring> ApplyConflictResolution(
        const std::vector<std::wstring>& lines,
        const MergeConflictBlock& block,
        ConflictResolutionChoice choice);

    /**
     * @brief Resolves every conflict block in `lines` from bottom to top using `choice`.
     */
    [[nodiscard]] static std::vector<std::wstring> ResolveAllConflicts(
        const std::vector<std::wstring>& lines,
        ConflictResolutionChoice choice);

    /**
     * @brief Constructs full Current / Result / Incoming texts for the 3-way merge view.
     */
    [[nodiscard]] static ThreeWayMergePreview BuildThreeWayMergePreview(const std::vector<std::wstring>& lines);

    /**
     * @brief Returns true if `git status --porcelain` XY codes indicate an unmerged conflict path.
     */
    [[nodiscard]] static bool IsUnmergedGitStatus(wchar_t indexCode, wchar_t workTreeCode) noexcept;

    /**
     * @brief Parses output from `git stash list`.
     */
    [[nodiscard]] static std::vector<GitStashEntry> ParseGitStashList(const std::wstring& stashListOutput);

    /**
     * @brief Builds a valid UTF-8 zero-context unified diff patch (`git apply --cached --unidiff-zero -`)
     *        for staging/unstaging a single `DiffHunkItem` from side-by-side buffers.
     */
    [[nodiscard]] static std::string BuildUnifiedHunkPatch(
        const std::wstring& repoRelPath,
        const editor::Buffer& leftBuf,
        const editor::Buffer& rightBuf,
        const editor::OpenDocument::DiffHunkItem& hunk);

    /**
     * @brief Produces a new right-hand line vector with `hunk` reverted back to `leftBuf` content.
     */
    [[nodiscard]] static std::vector<std::wstring> RevertDiffHunkInLines(
        const std::vector<std::wstring>& rightLines,
        const editor::Buffer& leftBuf,
        const editor::OpenDocument::DiffHunkItem& hunk);

    /**
     * @brief Sanitizes user-entered branch or tag names into safe Git ref characters.
     */
    [[nodiscard]] static std::wstring SanitizeBranchOrTagName(const std::wstring& raw);

    struct DiffHunkPatchSpec {
        size_t leftStart = 0;
        size_t leftCount = 0;
        size_t rightStart = 0;
        size_t rightCount = 0;
    };

    struct InlineDiffRow {
        enum class Kind { Context, Deleted, Added } kind = Kind::Context;
        size_t leftLine = 0;   // 1-based display line (0 if N/A)
        size_t rightLine = 0;  // 1-based display line (0 if N/A)
        size_t hunkIdx = static_cast<size_t>(-1);
        std::wstring text;
    };

    struct FileHistoryEntry {
        std::wstring shortHash;
        std::wstring fullHash;
        std::wstring date;
        std::wstring author;
        std::wstring message;
    };

    struct WorktreeEntry {
        std::wstring path;
        std::wstring headHash;
        std::wstring branch;
        bool isBare = false;
        bool isDetached = false;
        bool isLocked = false;
    };

    struct SubmoduleEntry {
        wchar_t state = L' '; // ' ' initialized, '-' uninitialized, '+' ahead/dirty, 'U' merge conflict
        std::wstring hash;
        std::wstring path;
        std::wstring refDesc;
    };

    struct RebaseTodoItem {
        std::wstring action = L"pick"; // pick, reword, squash, fixup, drop
        std::wstring hash;
        std::wstring subject;
    };

    [[nodiscard]] static std::string BuildUnifiedSelectedRangePatch(
        const std::wstring& repoRelPath,
        const editor::Buffer& leftBuf,
        const editor::Buffer& rightBuf,
        const std::vector<DiffHunkPatchSpec>& hunks,
        size_t selStartLine,
        size_t selEndLine,
        bool reverse = false);

    [[nodiscard]] static std::vector<InlineDiffRow> BuildInlineUnifiedDiffRows(
        const editor::Buffer& leftBuf,
        const editor::Buffer& rightBuf,
        const std::vector<DiffHunkPatchSpec>& hunks,
        size_t maxRows = 50000);

    [[nodiscard]] static std::vector<FileHistoryEntry> ParseFollowHistoryLog(
        const std::wstring& logOutput,
        size_t maxEntries = 100);

    [[nodiscard]] static std::vector<WorktreeEntry> ParseWorktreeListPorcelain(
        const std::wstring& porcelainOutput);

    [[nodiscard]] static std::vector<SubmoduleEntry> ParseSubmoduleStatus(
        const std::wstring& statusOutput);

    [[nodiscard]] static std::string BuildInteractiveRebaseSequence(
        const std::vector<RebaseTodoItem>& items);
};

} // namespace phide::git
