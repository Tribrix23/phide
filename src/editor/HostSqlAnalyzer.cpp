/**
 * @file HostSqlAnalyzer.cpp
 * @brief Implementation of schema-aware SQL autocomplete and validation inside PHP/JS/TS strings (#11).
 */

#include "phide/editor/HostSqlAnalyzer.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <unordered_set>

namespace phide::editor {

namespace {

static std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static std::wstring ToUpperW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towupper(c));
    return s;
}

static bool IsSqlIdentChar(wchar_t c) noexcept {
    return iswalnum(c) || c == L'_';
}

static bool IsSqlKeywordOrFunc(const std::wstring& upper) {
    static const std::unordered_set<std::wstring> kSet = {
        L"SELECT", L"DISTINCT", L"ALL", L"TOP", L"FROM", L"WHERE", L"AND", L"OR", L"NOT",
        L"IN", L"IS", L"NULL", L"TRUE", L"FALSE", L"AS", L"ON", L"USING", L"JOIN", L"INNER",
        L"LEFT", L"RIGHT", L"FULL", L"OUTER", L"CROSS", L"NATURAL", L"GROUP", L"BY", L"HAVING",
        L"ORDER", L"ASC", L"DESC", L"NULLS", L"FIRST", L"LAST", L"LIMIT", L"OFFSET", L"FETCH",
        L"NEXT", L"ROWS", L"ONLY", L"INSERT", L"INTO", L"VALUES", L"UPDATE", L"SET", L"DELETE",
        L"REPLACE", L"CREATE", L"ALTER", L"DROP", L"TABLE", L"IF", L"EXISTS", L"CASE", L"WHEN",
        L"THEN", L"ELSE", L"END", L"LIKE", L"ILIKE", L"REGEXP", L"RLIKE", L"BETWEEN", L"UNION",
        L"INTERSECT", L"EXCEPT", L"WITH", L"RECURSIVE", L"COUNT", L"SUM", L"AVG", L"MIN", L"MAX",
        L"COALESCE", L"IFNULL", L"NULLIF", L"GREATEST", L"LEAST", L"NOW", L"CURDATE",
        L"CURRENT_TIMESTAMP", L"CURRENT_DATE", L"CURRENT_TIME", L"DATE", L"TIME", L"DATETIME",
        L"TIMESTAMP", L"YEAR", L"MONTH", L"DAY", L"HOUR", L"MINUTE", L"SECOND", L"DATE_FORMAT",
        L"DATEDIFF", L"DATE_ADD", L"DATE_SUB", L"LOWER", L"UPPER", L"TRIM", L"LTRIM", L"RTRIM",
        L"LENGTH", L"CHAR_LENGTH", L"CONCAT", L"CONCAT_WS", L"GROUP_CONCAT", L"STRING_AGG",
        L"SUBSTR", L"SUBSTRING", L"REPLACE", L"CAST", L"CONVERT", L"ROUND", L"FLOOR", L"CEIL",
        L"CEILING", L"ABS", L"JSON_EXTRACT", L"JSON_OBJECT", L"JSON_ARRAYAGG", L"DUPLICATE",
        L"KEY", L"RETURNING", L"CONFLICT", L"DO", L"NOTHING", L"EXCLUDED", L"DEFAULT", L"INTERVAL",
        L"OVER", L"PARTITION", L"ROW_NUMBER", L"RANK", L"DENSE_RANK", L"ANY", L"SOME", L"IGNORE",
        L"FOR", L"SHARE", L"LOCK"
    };
    return kSet.count(upper) > 0;
}

/** @brief Returns true if `nameLow` is a built-in engine system catalog/table name. */
static bool IsSystemSchemaOrTable(const std::wstring& schemaLow, const std::wstring& tableLow) {
    if (schemaLow == L"information_schema" || schemaLow == L"performance_schema" ||
        schemaLow == L"pg_catalog" || schemaLow == L"mysql" || schemaLow == L"sys") {
        return true;
    }
    return tableLow == L"sqlite_master" || tableLow == L"sqlite_schema" ||
           tableLow == L"sqlite_temp_master" || tableLow == L"sqlite_temp_schema";
}

/**
 * @brief Extracts a file-level database name hint from DSN strings or DB config literals
 *        (e.g. `dbname=student_info`, `DB_DATABASE`, `mysqli_select_db(..., 'student_info')`).
 */
static std::wstring ExtractDocumentDbHint(const std::vector<std::wstring>& lines) {
    const size_t scanLimit = (std::min<size_t>)(lines.size(), 300);
    for (size_t l = 0; l < scanLimit; ++l) {
        const std::wstring& ln = lines[l];
        std::wstring low = ToLowerW(ln);

        // Pattern 1: DSN `dbname=student_info` or `database=student_info`
        for (const wchar_t* key : { L"dbname=", L"database=" }) {
            size_t pos = low.find(key);
            if (pos != std::wstring::npos) {
                size_t p = pos + wcslen(key);
                while (p < ln.size() && (iswspace(ln[p]) || ln[p] == L'\'' || ln[p] == L'"')) ++p;
                size_t e = p;
                while (e < ln.size() && (iswalnum(ln[e]) || ln[e] == L'_' || ln[e] == L'-')) ++e;
                if (e > p) return ToLowerW(ln.substr(p, e - p));
            }
        }

        // Pattern 2: `'database' => 'student_info'` or `"database": "student_info"`
        size_t dbKey = low.find(L"database");
        if (dbKey != std::wstring::npos) {
            size_t arrow = low.find(L"=>", dbKey);
            size_t colon = low.find(L":", dbKey);
            size_t sep = (arrow != std::wstring::npos && (colon == std::wstring::npos || arrow < colon)) ? arrow + 2
                       : (colon != std::wstring::npos ? colon + 1 : std::wstring::npos);
            if (sep != std::wstring::npos && sep - dbKey < 16) {
                while (sep < ln.size() && iswspace(ln[sep])) ++sep;
                if (sep < ln.size() && (ln[sep] == L'\'' || ln[sep] == L'"')) {
                    wchar_t q = ln[sep++];
                    size_t e = sep;
                    while (e < ln.size() && (iswalnum(ln[e]) || ln[e] == L'_' || ln[e] == L'-')) ++e;
                    if (e > sep && e < ln.size() && ln[e] == q) {
                        return ToLowerW(ln.substr(sep, e - sep));
                    }
                }
            }
        }

        // Pattern 3: `select_db('student_info')`
        size_t sdb = low.find(L"select_db(");
        if (sdb != std::wstring::npos) {
            size_t qPos = ln.find_first_of(L"'\"", sdb + 10);
            if (qPos != std::wstring::npos) {
                wchar_t q = ln[qPos];
                size_t e = qPos + 1;
                while (e < ln.size() && (iswalnum(ln[e]) || ln[e] == L'_' || ln[e] == L'-')) ++e;
                if (e > qPos + 1 && e < ln.size() && ln[e] == q) {
                    return ToLowerW(ln.substr(qPos + 1, e - (qPos + 1)));
                }
            }
        }
    }
    return {};
}

struct SqlSpan {
    std::wstring text;
    std::vector<size_t> charLine; ///< 0-based line for each char in `text`
    std::vector<size_t> charCol;  ///< 0-based col for each char in `text`
    bool hasSqlCallHint = false;
};

/** @brief Checks if the preceding code window contains a known SQL execution API call (PDO, mysqli, pg, knex, etc.). */
static bool HasPrecedingSqlCallHint(const std::vector<std::wstring>& lines, size_t openLine, size_t openCol) {
    std::wstring ctx;
    size_t startL = (openLine > 2) ? (openLine - 2) : 0;
    for (size_t l = startL; l <= openLine; ++l) {
        size_t endC = (l == openLine) ? (std::min)(openCol, lines[l].size()) : lines[l].size();
        ctx += lines[l].substr(0, endC);
        ctx.push_back(L' ');
    }
    std::wstring low = ToLowerW(ctx);
    const wchar_t* hints[] = {
        L"->prepare(", L"->query(", L"->exec(", L"mysqli_query(", L"mysqli_prepare(",
        L"db.query(", L"pool.query(", L"client.query(", L"knex.raw(", L"sequelize.query(",
        L"prisma.$queryraw", L"prisma.$executeraw", L"sql`", L"execute("
    };
    for (const wchar_t* h : hints) {
        if (low.rfind(h) != std::wstring::npos &&
            low.size() - low.rfind(h) <= 64) {
            return true;
        }
    }
    return false;
}

/** @brief Returns true if `text` begins with a DML/CTE verb accompanied by its mandatory structural clause. */
static bool StartsWithSqlVerb(const std::wstring& text) {
    size_t p = 0;
    while (p < text.size() && iswspace(text[p])) ++p;
    if (p >= text.size()) return false;
    size_t e = p;
    while (e < text.size() && iswalpha(text[e])) ++e;
    std::wstring first = ToUpperW(text.substr(p, e - p));
    if (first == L"SELECT" || first == L"INSERT" || first == L"UPDATE" ||
        first == L"DELETE" || first == L"REPLACE" || first == L"WITH") {
        // Require a following whitespace so words like "Update user profile" are further checked
        if (e < text.size() && iswspace(text[e])) {
            std::wstring upper = ToUpperW(text);
            if (first == L"SELECT" && upper.find(L" FROM ") != std::wstring::npos) return true;
            if (first == L"SELECT" && upper.find(L"\nFROM") != std::wstring::npos) return true;
            if (first == L"INSERT" && upper.find(L" INTO ") != std::wstring::npos) return true;
            if (first == L"UPDATE" && upper.find(L" SET ") != std::wstring::npos &&
                text.find(L'=') != std::wstring::npos) return true;
            if (first == L"DELETE" && upper.find(L" FROM ") != std::wstring::npos) return true;
            if (first == L"REPLACE" && upper.find(L" INTO ") != std::wstring::npos) return true;
            if (first == L"WITH" && upper.find(L"SELECT") != std::wstring::npos) return true;
        }
    }
    return false;
}

/** @brief Lightweight verb check used during typing when clauses like FROM/SET may not be typed yet. */
static bool StartsWithSqlPrefixForCompletion(const std::wstring& text) {
    size_t p = 0;
    while (p < text.size() && iswspace(text[p])) ++p;
    if (p >= text.size()) return false;
    size_t e = p;
    while (e < text.size() && iswalpha(text[e])) ++e;
    std::wstring first = ToUpperW(text.substr(p, e - p));
    return (first == L"SELECT" || first == L"INSERT" || first == L"UPDATE" ||
            first == L"DELETE" || first == L"REPLACE" || first == L"WITH") &&
           e < text.size() && iswspace(text[e]);
}

/**
 * @brief Scans `lines` (in a bounded window) to extract string literals and track whether
 *        `(targetLine, targetCol)` falls inside one.
 */
static std::vector<SqlSpan> CollectStringLiterals(
    const std::vector<std::wstring>& lines,
    size_t startLine,
    size_t endLine,
    size_t targetLine,
    size_t targetCol,
    int* outCursorSpanIdx,
    size_t* outCursorSpanOffset) {

    std::vector<SqlSpan> spans;
    if (outCursorSpanIdx) *outCursorSpanIdx = -1;
    if (outCursorSpanOffset) *outCursorSpanOffset = 0;
    if (lines.empty() || startLine >= lines.size()) return spans;
    endLine = (std::min)(endLine, lines.size() - 1);

    bool inBlockComment = false;
    bool inString = false;
    wchar_t quote = 0;
    SqlSpan cur;
    bool cursorRecorded = false;

    for (size_t l = startLine; l <= endLine; ++l) {
        const std::wstring& ln = lines[l];
        for (size_t c = 0; c < ln.size(); ++c) {
            if (inString && !cursorRecorded && l == targetLine && c == targetCol) {
                *outCursorSpanIdx = static_cast<int>(spans.size());
                *outCursorSpanOffset = cur.text.size();
                cursorRecorded = true;
            }
            wchar_t ch = ln[c];
            if (inBlockComment) {
                if (ch == L'*' && c + 1 < ln.size() && ln[c + 1] == L'/') {
                    inBlockComment = false;
                    ++c;
                }
                continue;
            }
            if (!inString) {
                if (ch == L'/' && c + 1 < ln.size() && ln[c + 1] == L'/') break;
                if (ch == L'#' && (c == 0 || ln[c - 1] != L'&')) break;
                if (ch == L'/' && c + 1 < ln.size() && ln[c + 1] == L'*') {
                    inBlockComment = true;
                    ++c;
                    continue;
                }
                if (ch == L'\'' || ch == L'"' || ch == L'`') {
                    inString = true;
                    quote = ch;
                    cur = SqlSpan{};
                    cur.hasSqlCallHint = HasPrecedingSqlCallHint(lines, l, c);
                }
            } else {
                if (ch == L'\\' && c + 1 < ln.size()) {
                    // Preserve spacing placeholder for escaped char
                    cur.text.push_back(L' ');
                    cur.charLine.push_back(l);
                    cur.charCol.push_back(c);
                    ++c;
                    cur.text.push_back(ln[c]);
                    cur.charLine.push_back(l);
                    cur.charCol.push_back(c);
                    continue;
                }
                if (ch == quote) {
                    if (outCursorSpanIdx && !cursorRecorded && l == targetLine && c == targetCol) {
                        *outCursorSpanIdx = static_cast<int>(spans.size());
                        *outCursorSpanOffset = cur.text.size();
                        cursorRecorded = true;
                    }
                    inString = false;
                    spans.push_back(std::move(cur));
                    cur = SqlSpan{};
                    continue;
                }
                cur.text.push_back(ch);
                cur.charLine.push_back(l);
                cur.charCol.push_back(c);
            }
        }
        if (inString) {
            if (outCursorSpanIdx && !cursorRecorded && l == targetLine && targetCol >= ln.size()) {
                *outCursorSpanIdx = static_cast<int>(spans.size());
                *outCursorSpanOffset = cur.text.size();
                cursorRecorded = true;
            }
            cur.text.push_back(L'\n');
            cur.charLine.push_back(l);
            cur.charCol.push_back(ln.size());
        }
    }
    if (inString) {
        spans.push_back(std::move(cur));
    }
    return spans;
}

struct SqlTok {
    enum class Kind { Word, Dot, Comma, LParen, RParen, Placeholder, Other };
    Kind kind = Kind::Other;
    std::wstring raw;
    std::wstring upper;
    size_t offset = 0;
    size_t len = 0;
};

static std::vector<SqlTok> TokenizeSqlSpan(const std::wstring& text) {
    std::vector<SqlTok> toks;
    size_t i = 0;
    while (i < text.size()) {
        wchar_t ch = text[i];
        if (iswspace(ch)) { ++i; continue; }

        // Placeholders: ?, :name, $var, {$var}, ${expr}
        if (ch == L'?') {
            toks.push_back({ SqlTok::Kind::Placeholder, L"?", L"?", i, 1 });
            ++i;
            continue;
        }
        if (ch == L'$' && i + 1 < text.size() && text[i + 1] == L'{') {
            int d = 1;
            size_t j = i + 2;
            while (j < text.size() && d > 0) {
                if (text[j] == L'{') ++d;
                else if (text[j] == L'}') --d;
                ++j;
            }
            toks.push_back({ SqlTok::Kind::Placeholder, L"?", L"?", i, j - i });
            i = j;
            continue;
        }
        if (ch == L'{' && i + 1 < text.size() && text[i + 1] == L'$') {
            size_t endB = text.find(L'}', i + 2);
            size_t j = (endB != std::wstring::npos) ? (endB + 1) : text.size();
            toks.push_back({ SqlTok::Kind::Placeholder, L"?", L"?", i, j - i });
            i = j;
            continue;
        }
        if ((ch == L':' || ch == L'$') && i + 1 < text.size() && IsSqlIdentChar(text[i + 1])) {
            size_t j = i + 1;
            while (j < text.size() && IsSqlIdentChar(text[j])) ++j;
            toks.push_back({ SqlTok::Kind::Placeholder, L"?", L"?", i, j - i });
            i = j;
            continue;
        }

        // Backtick-quoted identifier `table_or_col`
        if (ch == L'`') {
            size_t endBt = text.find(L'`', i + 1);
            if (endBt != std::wstring::npos && endBt > i + 1) {
                std::wstring id = text.substr(i + 1, endBt - (i + 1));
                toks.push_back({ SqlTok::Kind::Word, id, ToUpperW(id), i + 1, id.size() });
                i = endBt + 1;
                continue;
            }
        }

        // Single-quoted SQL string literal inside double-quoted/backtick host string
        if (ch == L'\'') {
            size_t j = i + 1;
            while (j < text.size()) {
                if (text[j] == L'\\' && j + 1 < text.size()) { j += 2; continue; }
                if (text[j] == L'\'') { ++j; break; }
                ++j;
            }
            toks.push_back({ SqlTok::Kind::Placeholder, L"'str'", L"'STR'", i, j - i });
            i = j;
            continue;
        }

        // Numeric literal
        if (iswdigit(ch)) {
            size_t j = i + 1;
            while (j < text.size() && (iswdigit(text[j]) || text[j] == L'.')) ++j;
            toks.push_back({ SqlTok::Kind::Placeholder, text.substr(i, j - i), L"0", i, j - i });
            i = j;
            continue;
        }

        if (IsSqlIdentChar(ch)) {
            size_t j = i + 1;
            while (j < text.size() && IsSqlIdentChar(text[j])) ++j;
            std::wstring raw = text.substr(i, j - i);
            toks.push_back({ SqlTok::Kind::Word, raw, ToUpperW(raw), i, j - i });
            i = j;
            continue;
        }

        if (ch == L'.')      toks.push_back({ SqlTok::Kind::Dot, L".", L".", i, 1 });
        else if (ch == L',') toks.push_back({ SqlTok::Kind::Comma, L",", L",", i, 1 });
        else if (ch == L'(') toks.push_back({ SqlTok::Kind::LParen, L"(", L"(", i, 1 });
        else if (ch == L')') toks.push_back({ SqlTok::Kind::RParen, L")", L")", i, 1 });
        else                 toks.push_back({ SqlTok::Kind::Other, std::wstring(1, ch), std::wstring(1, ch), i, 1 });
        ++i;
    }
    return toks;
}

/** @brief Extracted table reference from a SQL FROM/JOIN/INTO/UPDATE clause. */
struct ReferencedTableTok {
    size_t tokIdx = 0;
    std::wstring schemaName; ///< Optional `schema.` prefix when written as `db.table`
    std::wstring tableName;
};

static void ExtractReferencedTablesAndAliases(
    const std::vector<SqlTok>& toks,
    std::vector<std::wstring>& outTables,
    std::unordered_map<std::wstring, std::wstring>& outAliasToTable,
    std::vector<ReferencedTableTok>* outTableTokens = nullptr,
    std::unordered_set<std::wstring>* outCteNames = nullptr,
    std::unordered_map<std::wstring, std::wstring>* outTableToSchema = nullptr) {

    outTables.clear();
    outAliasToTable.clear();
    if (outTableTokens) outTableTokens->clear();
    if (outCteNames) outCteNames->clear();
    if (outTableToSchema) outTableToSchema->clear();
    std::unordered_set<std::wstring> seenTbl;

    // Collect CTE names (`WITH cte_name AS (...), cte2 AS (...)`) so they are never flagged as missing tables
    if (!toks.empty() && toks[0].kind == SqlTok::Kind::Word && toks[0].upper == L"WITH") {
        int depth = 0;
        for (size_t i = 1; i + 1 < toks.size(); ++i) {
            if (toks[i].kind == SqlTok::Kind::LParen) { ++depth; continue; }
            if (toks[i].kind == SqlTok::Kind::RParen) { if (depth > 0) --depth; continue; }
            if (depth == 0 && toks[i].kind == SqlTok::Kind::Word &&
                toks[i + 1].kind == SqlTok::Kind::Word && toks[i + 1].upper == L"AS" &&
                !IsSqlKeywordOrFunc(toks[i].upper)) {
                if (outCteNames) outCteNames->insert(ToLowerW(toks[i].raw));
            }
            if (depth == 0 && toks[i].kind == SqlTok::Kind::Word &&
                (toks[i].upper == L"SELECT" || toks[i].upper == L"INSERT" ||
                 toks[i].upper == L"UPDATE" || toks[i].upper == L"DELETE")) {
                break;
            }
        }
    }

    for (size_t i = 0; i < toks.size(); ++i) {
        if (toks[i].kind != SqlTok::Kind::Word) continue;
        const std::wstring& kw = toks[i].upper;
        if (kw == L"FROM" || kw == L"JOIN" || kw == L"INTO" || kw == L"UPDATE") {
            size_t ti = i + 1;
            std::wstring schemaQual;
            // Handle qualified `schema.table` without discarding `schema`
            if (ti + 2 < toks.size() && toks[ti].kind == SqlTok::Kind::Word &&
                toks[ti + 1].kind == SqlTok::Kind::Dot && toks[ti + 2].kind == SqlTok::Kind::Word) {
                schemaQual = toks[ti].raw;
                ti += 2;
            }
            if (ti < toks.size() && toks[ti].kind == SqlTok::Kind::Word && !IsSqlKeywordOrFunc(toks[ti].upper)) {
                std::wstring tblName = toks[ti].raw;
                std::wstring tblLow = ToLowerW(tblName);
                if (seenTbl.insert(tblLow).second) outTables.push_back(tblName);
                outAliasToTable[tblLow] = tblName;
                if (outTableToSchema && !schemaQual.empty()) {
                    (*outTableToSchema)[tblLow] = ToLowerW(schemaQual);
                }
                if (outTableTokens) {
                    outTableTokens->push_back({ ti, schemaQual, tblName });
                }

                // Optional alias: `FROM users u` or `FROM users AS u`
                size_t ai = ti + 1;
                if (ai < toks.size() && toks[ai].kind == SqlTok::Kind::Word && toks[ai].upper == L"AS") {
                    ++ai;
                }
                if (ai < toks.size() && toks[ai].kind == SqlTok::Kind::Word &&
                    !IsSqlKeywordOrFunc(toks[ai].upper)) {
                    outAliasToTable[ToLowerW(toks[ai].raw)] = tblName;
                }
            }
        }
    }
}

} // namespace

bool HostSqlAnalyzer::IsSupportedHostExtension(const std::wstring& extLower) noexcept {
    return extLower == L".php" || extLower == L".js" || extLower == L".mjs" ||
           extLower == L".cjs" || extLower == L".ts" || extLower == L".jsx" || extLower == L".tsx";
}

HostSqlCursorContext HostSqlAnalyzer::ExtractContextAtCursor(
    const std::vector<std::wstring>& lines,
    size_t cursorLine,
    size_t cursorCol) {

    HostSqlCursorContext ctx;
    if (lines.empty() || cursorLine >= lines.size()) return ctx;

    const size_t minL = (cursorLine > 80) ? (cursorLine - 80) : 0;
    const size_t maxL = (std::min)(lines.size() - 1, cursorLine + 40);
    int spanIdx = -1;
    size_t cursorOffset = 0;
    auto spans = CollectStringLiterals(lines, minL, maxL, cursorLine, cursorCol, &spanIdx, &cursorOffset);
    if (spanIdx < 0 || static_cast<size_t>(spanIdx) >= spans.size()) return ctx;

    const SqlSpan& span = spans[static_cast<size_t>(spanIdx)];
    if (!span.hasSqlCallHint && !StartsWithSqlPrefixForCompletion(span.text)) {
        return ctx;
    }

    ctx.inSqlString = true;
    cursorOffset = (std::min)(cursorOffset, span.text.size());

    // Extract prefix and optional `<qualifier>.` right before cursorOffset
    size_t pStart = cursorOffset;
    while (pStart > 0 && IsSqlIdentChar(span.text[pStart - 1])) --pStart;
    ctx.prefix = span.text.substr(pStart, cursorOffset - pStart);

    auto allToks = TokenizeSqlSpan(span.text);
    ExtractReferencedTablesAndAliases(allToks, ctx.referencedTables, ctx.aliasToTable);

    if (pStart > 0 && span.text[pStart - 1] == L'.') {
        size_t qEnd = pStart - 1;
        size_t qStart = qEnd;
        while (qStart > 0 && IsSqlIdentChar(span.text[qStart - 1])) --qStart;
        if (qEnd > qStart) {
            ctx.expectation = HostSqlExpectation::QualifiedColumn;
            ctx.qualifier = span.text.substr(qStart, qEnd - qStart);
            return ctx;
        }
    }

    // Check last SQL keyword before `pStart`
    std::wstring before = span.text.substr(0, pStart);
    auto beforeToks = TokenizeSqlSpan(before);
    for (int i = static_cast<int>(beforeToks.size()) - 1; i >= 0; --i) {
        if (beforeToks[i].kind != SqlTok::Kind::Word) continue;
        const std::wstring& kw = beforeToks[i].upper;
        if (kw == L"FROM" || kw == L"JOIN" || kw == L"INTO" || kw == L"UPDATE" || kw == L"TABLE") {
            ctx.expectation = HostSqlExpectation::TableName;
            return ctx;
        }
        if (kw == L"SELECT" || kw == L"WHERE" || kw == L"AND" || kw == L"OR" ||
            kw == L"SET" || kw == L"ON" || kw == L"BY" || kw == L"HAVING") {
            ctx.expectation = HostSqlExpectation::ColumnName;
            return ctx;
        }
        if (kw == L"VALUES" || kw == L"LIMIT" || kw == L"OFFSET") {
            ctx.expectation = HostSqlExpectation::Keyword;
            return ctx;
        }
    }
    ctx.expectation = HostSqlExpectation::Keyword;
    return ctx;
}

std::vector<HostSqlDiagnostic> HostSqlAnalyzer::ValidateDocumentSql(
    const std::wstring& filePath,
    const std::vector<std::wstring>& lines,
    const std::vector<phide::db::SqlTableInfo>& schemaTables,
    const std::wstring& preferredSchema) {

    std::vector<HostSqlDiagnostic> diags;
    if (schemaTables.empty() || lines.empty()) return diags;

    std::wstring ext = ToLowerW(std::filesystem::path(filePath).extension().wstring());
    if (!IsSupportedHostExtension(ext)) return diags;

    // Build multi-schema lowercase lookup tables so queries referencing tables in ANY connected DB
    // (not just the single DB open in the ER Diagram) validate cleanly without false positives.
    const std::wstring docHintSchema = ExtractDocumentDbHint(lines);
    const std::wstring prefLow = ToLowerW(preferredSchema);

    std::unordered_map<std::wstring, std::vector<const phide::db::SqlTableInfo*>> tablesByName;
    std::unordered_map<std::wstring, const phide::db::SqlTableInfo*> qualifiedTableMap;
    std::unordered_map<const phide::db::SqlTableInfo*, std::unordered_set<std::wstring>> tableColsByPtr;

    for (const auto& t : schemaTables) {
        std::wstring lowT = ToLowerW(t.name);
        tablesByName[lowT].push_back(&t);
        if (!t.schemaName.empty()) {
            qualifiedTableMap[ToLowerW(t.schemaName) + L"." + lowT] = &t;
        }
        auto& colSet = tableColsByPtr[&t];
        for (const auto& col : t.columns) {
            colSet.insert(ToLowerW(col.name));
        }
    }

    // Resolves candidate SqlTableInfo definitions for `tblLow` honoring `db.tbl` qualifiers & document hints
    auto resolveCandidateTables = [&](const std::wstring& tblLow,
                                      const std::wstring& explicitSchemaLow) -> std::vector<const phide::db::SqlTableInfo*> {
        if (!explicitSchemaLow.empty()) {
            auto qIt = qualifiedTableMap.find(explicitSchemaLow + L"." + tblLow);
            if (qIt != qualifiedTableMap.end()) return { qIt->second };
        }
        auto bIt = tablesByName.find(tblLow);
        if (bIt == tablesByName.end()) return {};
        if (!docHintSchema.empty()) {
            std::vector<const phide::db::SqlTableInfo*> hinted;
            for (const auto* tp : bIt->second) {
                if (ToLowerW(tp->schemaName) == docHintSchema) hinted.push_back(tp);
            }
            if (!hinted.empty()) return hinted;
        }
        // If no file-level DB hint exists, check all candidate definitions of `tblLow` across connected DBs
        // (putting `prefLow` first if present) so switching ERD database never triggers false column errors.
        if (!prefLow.empty() && bIt->second.size() > 1) {
            std::vector<const phide::db::SqlTableInfo*> ordered;
            for (const auto* tp : bIt->second) {
                if (ToLowerW(tp->schemaName) == prefLow) ordered.push_back(tp);
            }
            for (const auto* tp : bIt->second) {
                if (ToLowerW(tp->schemaName) != prefLow) ordered.push_back(tp);
            }
            return ordered;
        }
        return bIt->second;
    };

    const size_t maxLines = (std::min)(lines.size(), size_t(4000));
    auto spans = CollectStringLiterals(lines, 0, maxLines - 1, 0, 0, nullptr, nullptr);

    for (const auto& span : spans) {
        if (!span.hasSqlCallHint && !StartsWithSqlVerb(span.text)) continue;
        if (!StartsWithSqlVerb(span.text)) continue;

        auto toks = TokenizeSqlSpan(span.text);
        if (toks.empty()) continue;

        std::vector<std::wstring> refTables;
        std::unordered_map<std::wstring, std::wstring> aliasToTable;
        std::vector<ReferencedTableTok> tableTokens;
        std::unordered_set<std::wstring> cteNames;
        std::unordered_map<std::wstring, std::wstring> tableToSchema;
        ExtractReferencedTablesAndAliases(toks, refTables, aliasToTable, &tableTokens, &cteNames, &tableToSchema);

        // Collect SELECT column aliases (`expr AS total`) so ORDER BY / GROUP BY / HAVING never flag them
        std::unordered_set<std::wstring> selectColAliases;
        for (size_t i = 0; i + 1 < toks.size(); ++i) {
            if (toks[i].kind == SqlTok::Kind::Word && toks[i].upper == L"AS" &&
                toks[i + 1].kind == SqlTok::Kind::Word && !IsSqlKeywordOrFunc(toks[i + 1].upper)) {
                selectColAliases.insert(ToLowerW(toks[i + 1].raw));
            }
        }

        // 1. Validate referenced table names (allowing CTEs, system tables, and any connected schema table)
        bool allTablesKnown = !refTables.empty();
        bool hasCteOrSystemRef = false;
        std::unordered_set<size_t> skipWordIndices;
        for (const auto& rt : tableTokens) {
            const size_t tIdx = rt.tokIdx;
            const std::wstring& tName = rt.tableName;
            const std::wstring tblLow = ToLowerW(tName);
            const std::wstring schemaLow = ToLowerW(rt.schemaName);

            skipWordIndices.insert(tIdx);
            if (!rt.schemaName.empty() && tIdx >= 2) {
                skipWordIndices.insert(tIdx - 2);
            }
            // Also skip the optional alias token right after `table [AS] alias`
            if (tIdx + 1 < toks.size() && toks[tIdx + 1].kind == SqlTok::Kind::Word &&
                toks[tIdx + 1].upper == L"AS" && tIdx + 2 < toks.size()) {
                skipWordIndices.insert(tIdx + 2);
            } else if (tIdx + 1 < toks.size() && toks[tIdx + 1].kind == SqlTok::Kind::Word &&
                       !IsSqlKeywordOrFunc(toks[tIdx + 1].upper)) {
                skipWordIndices.insert(tIdx + 1);
            }

            if (cteNames.count(tblLow) > 0 || IsSystemSchemaOrTable(schemaLow, tblLow)) {
                hasCteOrSystemRef = true;
                continue;
            }

            const auto candidates = resolveCandidateTables(tblLow, schemaLow);
            if (candidates.empty()) {
                allTablesKnown = false;
                size_t off = toks[tIdx].offset;
                if (off < span.charLine.size()) {
                    std::wstring qualLabel = rt.schemaName.empty() ? tName : (rt.schemaName + L"." + tName);
                    diags.push_back({
                        span.charLine[off],
                        span.charCol[off],
                        toks[tIdx].len,
                        2,
                        L"Unknown SQL table '" + qualLabel + L"' (not found in connected database schemas)"
                    });
                }
            }
        }

        if (!allTablesKnown || hasCteOrSystemRef) continue;

        // 2. Validate qualified (`alias.col`) and unqualified column references
        std::string activeClause;
        for (size_t i = 0; i < toks.size(); ++i) {
            if (skipWordIndices.count(i) > 0) continue;
            if (toks[i].kind != SqlTok::Kind::Word) continue;

            if (IsSqlKeywordOrFunc(toks[i].upper)) {
                const auto& u = toks[i].upper;
                if (u == L"SELECT") activeClause = "SELECT";
                else if (u == L"FROM" || u == L"JOIN") activeClause = "FROM";
                else if (u == L"WHERE" || u == L"ON" || u == L"HAVING") activeClause = "WHERE";
                else if (u == L"SET") activeClause = "SET";
                else if (u == L"INTO") activeClause = "INTO";
                else if (u == L"VALUES") activeClause = "VALUES";
                else if (u == L"ORDER" || u == L"GROUP") activeClause = "BY";
                else if (u == L"LIMIT" || u == L"OFFSET") activeClause = "LIMIT";
                continue;
            }

            // Skip function calls `my_fn(...)`
            if (i + 1 < toks.size() && toks[i + 1].kind == SqlTok::Kind::LParen) continue;
            // Skip SELECT column aliases after `AS col_alias`
            if (i > 0 && toks[i - 1].kind == SqlTok::Kind::Word && toks[i - 1].upper == L"AS") continue;

            // Qualified column: `qualifier.column`
            if (i + 2 < toks.size() && toks[i + 1].kind == SqlTok::Kind::Dot &&
                toks[i + 2].kind == SqlTok::Kind::Word) {
                std::wstring qualLow = ToLowerW(toks[i].raw);
                auto aIt = aliasToTable.find(qualLow);
                if (aIt != aliasToTable.end()) {
                    std::wstring tblLow = ToLowerW(aIt->second);
                    std::wstring expSch = tableToSchema.count(tblLow) ? tableToSchema[tblLow] : L"";
                    const auto candidates = resolveCandidateTables(tblLow, expSch);
                    if (!candidates.empty()) {
                        std::wstring colRaw = toks[i + 2].raw;
                        std::wstring colLow = ToLowerW(colRaw);
                        bool foundCol = false;
                        for (const auto* tp : candidates) {
                            auto cIt = tableColsByPtr.find(tp);
                            if (cIt != tableColsByPtr.end() && cIt->second.count(colLow) > 0) {
                                foundCol = true;
                                break;
                            }
                        }
                        if (!foundCol) {
                            size_t off = toks[i + 2].offset;
                            if (off < span.charLine.size()) {
                                diags.push_back({
                                    span.charLine[off],
                                    span.charCol[off],
                                    toks[i + 2].len,
                                    2,
                                    L"Unknown SQL column '" + colRaw + L"' on table '" + aIt->second + L"'"
                                });
                            }
                        }
                    }
                }
                i += 2;
                continue;
            }

            // Unqualified column inside SELECT, WHERE, SET, BY, or INSERT INTO tbl (...)
            if (activeClause == "SELECT" || activeClause == "WHERE" ||
                activeClause == "SET" || activeClause == "BY" || activeClause == "INTO") {
                std::wstring colLow = ToLowerW(toks[i].raw);
                if ((activeClause == "BY" || activeClause == "WHERE") && selectColAliases.count(colLow) > 0) {
                    continue;
                }
                bool existsInAnyRefTable = false;
                for (const auto& rt : refTables) {
                    std::wstring rtLow = ToLowerW(rt);
                    std::wstring expSch = tableToSchema.count(rtLow) ? tableToSchema[rtLow] : L"";
                    const auto candidates = resolveCandidateTables(rtLow, expSch);
                    for (const auto* tp : candidates) {
                        auto cIt = tableColsByPtr.find(tp);
                        if (cIt != tableColsByPtr.end() && cIt->second.count(colLow) > 0) {
                            existsInAnyRefTable = true;
                            break;
                        }
                    }
                    if (existsInAnyRefTable) break;
                }
                if (!existsInAnyRefTable) {
                    size_t off = toks[i].offset;
                    if (off < span.charLine.size()) {
                        diags.push_back({
                            span.charLine[off],
                            span.charCol[off],
                            toks[i].len,
                            2,
                            L"Unknown SQL column '" + toks[i].raw + L"' (not found in referenced schema table)"
                        });
                    }
                }
            }
        }
    }

    return diags;
}

} // namespace phide::editor
