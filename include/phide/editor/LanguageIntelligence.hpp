/**
 * @file LanguageIntelligence.hpp
 * @brief Unified Language Intelligence engine for Auto-Import & Import Organization (#21),
 *        Rich Hover & Signature Help (#22), External Linter Aggregation & Problems (#23),
 *        Code Actions & Structural Refactorings (#24), and Inlay Hints / CodeLens (#25).
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "phide/lsp/LspClient.hpp"

namespace phide::editor {

// ============================================================================
// #21: Auto-Import & Import Organization
// ============================================================================

struct ImportCandidate {
    std::wstring symbol;
    std::wstring moduleSpecifier;
    std::wstring importStatement;
    std::wstring sourceKind;      ///< e.g. "React", "Next.js", "Node", "Python Stdlib", "Workspace"
    bool isDefaultExport = false;
};

struct OrganizeImportsResult {
    std::wstring updatedText;
    bool changed = false;
    int removedUnusedCount = 0;
    int totalImportCount = 0;
};

class ImportOrganizer {
public:
    [[nodiscard]] static OrganizeImportsResult OrganizeImports(const std::wstring& filePath,
                                                               const std::wstring& content);

    [[nodiscard]] static std::vector<ImportCandidate> FindAutoImportCandidates(
        const std::wstring& filePath,
        const std::wstring& workspaceRoot,
        const std::wstring& symbolQuery,
        const std::wstring& currentContent,
        int maxResults = 12);

    [[nodiscard]] static std::wstring InsertImportStatement(const std::wstring& filePath,
                                                            const std::wstring& content,
                                                            const ImportCandidate& candidate,
                                                            size_t* outInsertedLine = nullptr);
};

// ============================================================================
// #22: Rich Hover Documentation & Signature Parameter Help
// ============================================================================

struct MarkdownBlock {
    enum class Kind {
        Paragraph,
        CodeBlock,
        Heading,
        Bullet,
        Separator
    };
    Kind kind = Kind::Paragraph;
    std::wstring text;
    std::wstring lang;
};

struct CallContext {
    bool inCall = false;
    std::wstring calleeName;
    std::wstring receiverName;
    int activeParameter = 0;
    size_t parenLine = 0;
    size_t parenCol = 0;
};

class HoverAndSignatureHelper {
public:
    [[nodiscard]] static std::vector<MarkdownBlock> ParseMarkdownBlocks(const std::wstring& markdown);

    [[nodiscard]] static phide::lsp::HoverResult LookupStaticHover(
        const std::wstring& filePath,
        const std::vector<std::wstring>& docLines,
        size_t line,
        size_t col);

    [[nodiscard]] static CallContext ExtractCallContext(
        const std::vector<std::wstring>& docLines,
        size_t cursorLine,
        size_t cursorCol);

    [[nodiscard]] static phide::lsp::SignatureHelpResult LookupStaticSignatureHelp(
        const std::wstring& filePath,
        const std::vector<std::wstring>& docLines,
        size_t cursorLine,
        size_t cursorCol);

    /// Splits a call/declaration parameter string "a: int, b: list[str] = []" respecting nested brackets.
    [[nodiscard]] static std::vector<std::wstring> SplitSignatureParameters(const std::wstring& paramsStr);
};

// ============================================================================
// #23: External Linter Aggregation & Problems Panel Model
// ============================================================================

struct ProblemEntry {
    std::wstring filePath;
    std::wstring displayPath;
    size_t line = 0;
    size_t col = 0;
    size_t length = 1;
    int severity = 1; // 1 = Error, 2 = Warning, 3 = Info/Hint
    std::wstring message;
    std::wstring source; // "Syntax", "LSP", "tsc", "eslint", "phpstan", "ruff", "Terminal"
    std::wstring code;
};

struct ProblemsFilterState {
    int severityFilter = 0; // 0 = All, 1 = Errors, 2 = Warnings, 3 = Info
    bool currentFileOnly = false;
    std::wstring searchText;
};

class ExternalLinterEngine {
public:
    [[nodiscard]] static std::vector<ProblemEntry> ParseTscOutput(const std::wstring& output,
                                                                  const std::wstring& workspaceRoot);
    [[nodiscard]] static std::vector<ProblemEntry> ParseEslintCompactOutput(const std::wstring& output,
                                                                            const std::wstring& workspaceRoot);
    [[nodiscard]] static std::vector<ProblemEntry> ParsePhpStanOutput(const std::wstring& output,
                                                                      const std::wstring& workspaceRoot);
    [[nodiscard]] static std::vector<ProblemEntry> ParseRuffOrFlake8Output(const std::wstring& output,
                                                                           const std::wstring& workspaceRoot);

    /// Runs applicable available CLI linters asynchronously/synchronously in background worker with strict timeout.
    [[nodiscard]] static std::vector<ProblemEntry> RunLintersForFile(const std::wstring& filePath,
                                                                     const std::wstring& workspaceRoot);

    [[nodiscard]] static std::vector<ProblemEntry> FilterProblems(const std::vector<ProblemEntry>& all,
                                                                  const ProblemsFilterState& filter,
                                                                  const std::wstring& activeFilePath);

    static void CountSeverities(const std::vector<ProblemEntry>& all,
                                int& outErrors,
                                int& outWarnings,
                                int& outInfos);
};

// ============================================================================
// #24: Code Actions & Structural Refactorings
// ============================================================================

class CodeActionEngine {
public:
    [[nodiscard]] static std::vector<phide::lsp::CodeActionItem> GenerateLocalCodeActions(
        const std::wstring& filePath,
        const std::wstring& workspaceRoot,
        const std::wstring& docContent,
        const std::vector<phide::lsp::Diagnostic>& diagnostics,
        size_t selStartLine,
        size_t selStartCol,
        size_t selEndLine,
        size_t selEndCol);

    /// Applies an LSP / local TextEdit list cleanly to text in reverse document order.
    [[nodiscard]] static std::wstring ApplyTextEditsToString(
        const std::wstring& content,
        const std::vector<phide::lsp::LspTextEdit>& edits);
};

// ============================================================================
// #25: Static Inlay Hints & Enhanced CodeLens
// ============================================================================

struct StaticInlayHint {
    size_t line = 0;
    size_t col = 0;
    std::wstring label;
    int kind = 1; // 1 = Type, 2 = Parameter
};

struct CodeLensSymbol {
    size_t line = 0;
    std::wstring symbolName;
    int referenceCount = 0;
    bool isMainEntry = false;
    bool isTestEntry = false;
};

class InlayAndCodeLensEngine {
public:
    [[nodiscard]] static std::vector<StaticInlayHint> ComputeStaticInlayHints(
        const std::wstring& filePath,
        const std::vector<std::wstring>& lines,
        size_t startLine = 0,
        size_t endLine = 250,
        size_t maxHints = 160);

    [[nodiscard]] static std::vector<CodeLensSymbol> ScanCodeLensEntries(
        const std::wstring& filePath,
        const std::vector<std::wstring>& lines,
        const std::vector<std::wstring>& peerBuffersText,
        size_t maxEntries = 80);
};

} // namespace phide::editor
