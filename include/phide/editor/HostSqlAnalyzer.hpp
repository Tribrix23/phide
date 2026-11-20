/**
 * @file HostSqlAnalyzer.hpp
 * @brief Schema-aware SQL autocomplete & static table/column validation inside PHP, JS, and TS
 *        string literals (#11).
 *
 * Detects SQL queries inside host-language calls (`$pdo->prepare("...")`, `mysqli_query(...)`,
 * `db.query(...)`, `pool.query(...)`, `knex.raw(...)`, template literals `` `SELECT ...` ``),
 * resolves table aliases, ignores placeholders (`?`, `:param`, `$1`, `$var`, `${expr}`), and
 * exposes both completion context extraction and diagnostic validation against active SqlTableInfo.
 */

#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "phide/db/SqlEngine.hpp"

namespace phide::editor {

/**
 * @enum HostSqlExpectation
 * @brief Expected token category at the cursor inside a host-language SQL string literal.
 */
enum class HostSqlExpectation {
    None = 0,
    TableName,       ///< After FROM, JOIN, INTO, UPDATE, TABLE
    ColumnName,      ///< After SELECT, WHERE, AND, OR, SET, ON, GROUP BY, ORDER BY, HAVING
    QualifiedColumn, ///< Immediately after `<table_or_alias>.`
    Keyword          ///< General SQL keyword position
};

/**
 * @struct HostSqlCursorContext
 * @brief Extracted SQL context at the current cursor position in a PHP / JS / TS document.
 */
struct HostSqlCursorContext {
    bool inSqlString = false;
    HostSqlExpectation expectation = HostSqlExpectation::None;
    std::wstring prefix;             ///< Partial identifier right before cursor
    std::wstring qualifier;          ///< Table name or alias before '.' when QualifiedColumn
    std::vector<std::wstring> referencedTables; ///< Tables referenced in FROM/JOIN/UPDATE/INTO
    std::unordered_map<std::wstring, std::wstring> aliasToTable; ///< Lowercase alias -> table name
};

/**
 * @struct HostSqlDiagnostic
 * @brief Table or column mismatch found inside a PHP / JS / TS SQL string literal.
 */
struct HostSqlDiagnostic {
    size_t line0 = 0;
    size_t col0 = 0;
    size_t length = 1;
    int severity = 2; ///< 2 = Warning
    std::wstring message;
};

/**
 * @class HostSqlAnalyzer
 * @brief Stateless analyzer for host-language SQL completion context and schema diagnostics (#11).
 */
class HostSqlAnalyzer {
public:
    /**
     * @brief Returns true if `extLower` (e.g. L".php", L".js", L".ts") is a supported host language.
     */
    [[nodiscard]] static bool IsSupportedHostExtension(const std::wstring& extLower) noexcept;

    /**
     * @brief Analyzes the host buffer around `(cursorLine, cursorCol)` to detect whether the cursor
     *        is inside a SQL string literal and what SQL tokens are valid at that position.
     */
    [[nodiscard]] static HostSqlCursorContext ExtractContextAtCursor(
        const std::vector<std::wstring>& lines,
        size_t cursorLine,
        size_t cursorCol);

    /**
     * @brief Validates all SQL string literals in `lines` against `schemaTables` and reports
     *        unknown table or column names with exact 0-based line/column spans.
     *        Supports multi-schema tables (preferring `preferredSchema` or file-level DSN hints)
     *        so switching the active ERD database never produces false-positive errors.
     *        Returns empty if `schemaTables` is empty.
     */
    [[nodiscard]] static std::vector<HostSqlDiagnostic> ValidateDocumentSql(
        const std::wstring& filePath,
        const std::vector<std::wstring>& lines,
        const std::vector<phide::db::SqlTableInfo>& schemaTables,
        const std::wstring& preferredSchema = L"");
};

} // namespace phide::editor
