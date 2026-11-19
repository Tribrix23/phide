/**
 * @file ErdAndMigrationEngine.cpp
 * @brief Implementation of SQL DDL parsing, Schema Diff, Migration SQL generation, and
 *        multi-notation ERD SVG export (#10).
 */

#include "phide/db/ErdAndMigrationEngine.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace phide::db {

namespace {

static std::wstring TrimW(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && iswspace(s[a])) ++a;
    size_t b = s.size();
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static std::wstring ToUpperW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towupper(c));
    return s;
}

static std::wstring UnquoteIdent(std::wstring s) {
    s = TrimW(s);
    auto dot = s.rfind(L'.');
    if (dot != std::wstring::npos) {
        s = TrimW(s.substr(dot + 1));
    }
    if (s.size() >= 2) {
        wchar_t a = s.front();
        wchar_t b = s.back();
        if ((a == L'`' && b == L'`') || (a == L'"' && b == L'"') || (a == L'[' && b == L']')) {
            s = s.substr(1, s.size() - 2);
        }
    }
    return s;
}

static std::wstring QuoteIdent(const std::wstring& ident, SqlDialect dialect) {
    if (dialect == SqlDialect::MySQL) return L"`" + ident + L"`";
    return L"\"" + ident + L"\"";
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

static std::string EscapeXml(const std::wstring& w) {
    std::string s = WideToUtf8(w);
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out.push_back(c);
    }
    return out;
}

static std::wstring StripSqlComments(const std::wstring& sql) {
    std::wstring out;
    out.reserve(sql.size());
    bool inStr = false;
    wchar_t q = 0;
    for (size_t i = 0; i < sql.size(); ++i) {
        wchar_t c = sql[i];
        if (inStr) {
            out.push_back(c);
            if (c == L'\\' && i + 1 < sql.size()) {
                out.push_back(sql[++i]);
                continue;
            }
            if (c == q) inStr = false;
            continue;
        }
        if (c == L'\'' || c == L'"' || c == L'`') {
            inStr = true;
            q = c;
            out.push_back(c);
            continue;
        }
        if (c == L'-' && i + 1 < sql.size() && sql[i + 1] == L'-') {
            while (i < sql.size() && sql[i] != L'\n') ++i;
            out.push_back(L'\n');
            continue;
        }
        if (c == L'/' && i + 1 < sql.size() && sql[i + 1] == L'*') {
            i += 2;
            while (i + 1 < sql.size() && !(sql[i] == L'*' && sql[i + 1] == L'/')) ++i;
            ++i;
            out.push_back(L' ');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

static std::vector<std::wstring> SplitTopLevelCommaItems(const std::wstring& body) {
    std::vector<std::wstring> items;
    int depth = 0;
    bool inStr = false;
    wchar_t q = 0;
    size_t start = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        wchar_t c = body[i];
        if (inStr) {
            if (c == L'\\' && i + 1 < body.size()) { ++i; continue; }
            if (c == q) inStr = false;
            continue;
        }
        if (c == L'\'' || c == L'"' || c == L'`') {
            inStr = true;
            q = c;
        } else if (c == L'(') {
            ++depth;
        } else if (c == L')') {
            if (depth > 0) --depth;
        } else if (c == L',' && depth == 0) {
            std::wstring part = TrimW(body.substr(start, i - start));
            if (!part.empty()) items.push_back(std::move(part));
            start = i + 1;
        }
    }
    std::wstring last = TrimW(body.substr(start));
    if (!last.empty()) items.push_back(std::move(last));
    return items;
}

static std::wstring FormatColDefSql(const SqlColumnInfo& col, SqlDialect dialect) {
    std::wstring typeStr = col.type.empty() ? L"TEXT" : col.type;
    std::wstring def = QuoteIdent(col.name, dialect) + L" " + typeStr;
    if (col.notNull || col.isPrimaryKey) {
        def += L" NOT NULL";
    }
    return def;
}

static std::wstring BuildCreateTableSql(const SqlTableInfo& tbl, SqlDialect dialect) {
    std::wstring sql = L"CREATE TABLE " + QuoteIdent(tbl.name, dialect) + L" (\n";
    std::vector<std::wstring> pks;
    for (size_t i = 0; i < tbl.columns.size(); ++i) {
        const auto& c = tbl.columns[i];
        sql += L"    " + FormatColDefSql(c, dialect);
        if (c.isPrimaryKey) pks.push_back(QuoteIdent(c.name, dialect));
        if (i + 1 < tbl.columns.size() || !pks.empty() || !tbl.foreignKeys.empty()) sql += L",";
        sql += L"\n";
    }
    if (!pks.empty()) {
        sql += L"    PRIMARY KEY (";
        for (size_t i = 0; i < pks.size(); ++i) {
            if (i > 0) sql += L", ";
            sql += pks[i];
        }
        sql += L")";
        if (!tbl.foreignKeys.empty()) sql += L",";
        sql += L"\n";
    }
    for (size_t i = 0; i < tbl.foreignKeys.size(); ++i) {
        const auto& fk = tbl.foreignKeys[i];
        sql += L"    FOREIGN KEY (" + QuoteIdent(fk.column, dialect) + L") REFERENCES " +
               QuoteIdent(fk.refTable, dialect) + L"(" + QuoteIdent(fk.refColumn, dialect) + L")";
        if (i + 1 < tbl.foreignKeys.size()) sql += L",";
        sql += L"\n";
    }
    sql += L");";
    return sql;
}

static bool IsFkColumn(const SqlTableInfo& t, const std::wstring& colName) {
    std::wstring low = ToLowerW(colName);
    for (const auto& fk : t.foreignKeys) {
        if (ToLowerW(fk.column) == low) return true;
    }
    return false;
}

} // namespace

std::vector<SqlTableInfo> ErdAndMigrationEngine::ParseSqlDdlSchema(const std::wstring& ddlSql) {
    std::vector<SqlTableInfo> tables;
    if (ddlSql.empty()) return tables;

    std::wstring clean = StripSqlComments(ddlSql);
    std::wstring upper = ToUpperW(clean);

    size_t pos = 0;
    while ((pos = upper.find(L"CREATE", pos)) != std::wstring::npos) {
        if (pos > 0 && iswalnum(upper[pos - 1])) { pos += 6; continue; }
        size_t p = pos + 6;
        while (p < upper.size() && iswspace(upper[p])) ++p;
        if (upper.compare(p, 9, L"TEMPORARY") == 0) {
            p += 9;
            while (p < upper.size() && iswspace(upper[p])) ++p;
        }
        if (upper.compare(p, 5, L"TABLE") != 0) { pos = p; continue; }
        p += 5;
        while (p < upper.size() && iswspace(upper[p])) ++p;
        if (upper.compare(p, 2, L"IF") == 0) {
            size_t notExists = upper.find(L"EXISTS", p);
            if (notExists != std::wstring::npos && notExists - p < 24) {
                p = notExists + 6;
                while (p < upper.size() && iswspace(upper[p])) ++p;
            }
        }

        size_t openParen = clean.find(L'(', p);
        if (openParen == std::wstring::npos) break;

        SqlTableInfo tbl;
        tbl.name = UnquoteIdent(clean.substr(p, openParen - p));

        // Find matching ')'
        int depth = 1;
        bool inStr = false;
        wchar_t q = 0;
        size_t cur = openParen + 1;
        while (cur < clean.size() && depth > 0) {
            wchar_t ch = clean[cur];
            if (inStr) {
                if (ch == L'\\' && cur + 1 < clean.size()) { cur += 2; continue; }
                if (ch == q) inStr = false;
            } else {
                if (ch == L'\'' || ch == L'"' || ch == L'`') { inStr = true; q = ch; }
                else if (ch == L'(') ++depth;
                else if (ch == L')') --depth;
            }
            ++cur;
        }
        if (depth != 0 || tbl.name.empty()) { pos = openParen + 1; continue; }

        std::wstring body = clean.substr(openParen + 1, (cur - 1) - (openParen + 1));
        auto items = SplitTopLevelCommaItems(body);
        std::unordered_set<std::wstring> tablePks;

        for (const auto& item : items) {
            std::wstring up = ToUpperW(item);
            if (up.rfind(L"PRIMARY", 0) == 0 && up.find(L"KEY") != std::wstring::npos) {
                size_t lp = item.find(L'(');
                size_t rp = item.rfind(L')');
                if (lp != std::wstring::npos && rp != std::wstring::npos && rp > lp) {
                    for (const auto& pkPart : SplitTopLevelCommaItems(item.substr(lp + 1, rp - lp - 1))) {
                        tablePks.insert(ToLowerW(UnquoteIdent(pkPart)));
                    }
                }
                continue;
            }
            if (up.rfind(L"CONSTRAINT", 0) == 0 || up.rfind(L"FOREIGN", 0) == 0) {
                size_t fkPos = up.find(L"FOREIGN");
                size_t refPos = up.find(L"REFERENCES");
                if (fkPos != std::wstring::npos && refPos != std::wstring::npos && refPos > fkPos) {
                    size_t lp1 = item.find(L'(', fkPos);
                    size_t rp1 = item.find(L')', lp1 != std::wstring::npos ? lp1 : fkPos);
                    size_t lp2 = item.find(L'(', refPos);
                    size_t rp2 = item.find(L')', lp2 != std::wstring::npos ? lp2 : refPos);
                    if (lp1 != std::wstring::npos && rp1 != std::wstring::npos &&
                        lp2 != std::wstring::npos && rp2 != std::wstring::npos) {
                        SqlForeignKeyInfo fk;
                        fk.column = UnquoteIdent(item.substr(lp1 + 1, rp1 - lp1 - 1));
                        fk.refTable = UnquoteIdent(item.substr(refPos + 10, lp2 - (refPos + 10)));
                        fk.refColumn = UnquoteIdent(item.substr(lp2 + 1, rp2 - lp2 - 1));
                        if (!fk.column.empty() && !fk.refTable.empty()) {
                            tbl.foreignKeys.push_back(std::move(fk));
                        }
                    }
                }
                continue;
            }
            if (up.rfind(L"UNIQUE", 0) == 0 || up.rfind(L"KEY ", 0) == 0 ||
                up.rfind(L"INDEX ", 0) == 0 || up.rfind(L"CHECK", 0) == 0 ||
                up.rfind(L"FULLTEXT", 0) == 0) {
                continue;
            }

            // Column definition: `col_name` TYPE(...) [NOT NULL] [PRIMARY KEY] [REFERENCES p(c)]
            size_t colEnd = 0;
            if (item[0] == L'`' || item[0] == L'"' || item[0] == L'[') {
                wchar_t closeQ = (item[0] == L'[') ? L']' : item[0];
                size_t qEnd = item.find(closeQ, 1);
                if (qEnd != std::wstring::npos) colEnd = qEnd + 1;
            } else {
                while (colEnd < item.size() && !iswspace(item[colEnd])) ++colEnd;
            }
            if (colEnd == 0) continue;

            SqlColumnInfo col;
            col.name = UnquoteIdent(item.substr(0, colEnd));
            std::wstring rest = TrimW(item.substr(colEnd));
            std::wstring restUp = ToUpperW(rest);

            // Extract type token (including optional `(len, dec)`)
            size_t tEnd = 0;
            int tDepth = 0;
            while (tEnd < rest.size()) {
                wchar_t tc = rest[tEnd];
                if (tc == L'(') ++tDepth;
                else if (tc == L')' && tDepth > 0) --tDepth;
                else if (iswspace(tc) && tDepth == 0) {
                    // Keep `UNSIGNED` / `PRECISION`? Stop at constraints.
                    std::wstring nextRest = TrimW(ToUpperW(rest.substr(tEnd)));
                    if (nextRest.rfind(L"NOT ", 0) == 0 || nextRest.rfind(L"NULL", 0) == 0 ||
                        nextRest.rfind(L"PRIMARY", 0) == 0 || nextRest.rfind(L"DEFAULT", 0) == 0 ||
                        nextRest.rfind(L"AUTO_INCREMENT", 0) == 0 || nextRest.rfind(L"AUTOINCREMENT", 0) == 0 ||
                        nextRest.rfind(L"UNIQUE", 0) == 0 || nextRest.rfind(L"REFERENCES", 0) == 0 ||
                        nextRest.rfind(L"COMMENT", 0) == 0 || nextRest.rfind(L"ON ", 0) == 0 ||
                        nextRest.rfind(L"CHECK", 0) == 0) {
                        break;
                    }
                }
                ++tEnd;
            }
            col.type = TrimW(rest.substr(0, tEnd));
            col.notNull = (restUp.find(L"NOT NULL") != std::wstring::npos);
            col.isPrimaryKey = (restUp.find(L"PRIMARY KEY") != std::wstring::npos);

            size_t refPos = restUp.find(L"REFERENCES");
            if (refPos != std::wstring::npos) {
                size_t lp = rest.find(L'(', refPos);
                size_t rp = rest.find(L')', lp != std::wstring::npos ? lp : refPos);
                if (lp != std::wstring::npos && rp != std::wstring::npos && rp > lp) {
                    SqlForeignKeyInfo fk;
                    fk.column = col.name;
                    fk.refTable = UnquoteIdent(rest.substr(refPos + 10, lp - (refPos + 10)));
                    fk.refColumn = UnquoteIdent(rest.substr(lp + 1, rp - lp - 1));
                    if (!fk.refTable.empty()) tbl.foreignKeys.push_back(std::move(fk));
                }
            }
            if (!col.name.empty()) tbl.columns.push_back(std::move(col));
        }

        for (auto& col : tbl.columns) {
            if (tablePks.count(ToLowerW(col.name)) > 0) {
                col.isPrimaryKey = true;
                col.notNull = true;
            }
        }

        tables.push_back(std::move(tbl));
        pos = cur;
    }
    return tables;
}

SchemaDiffResult ErdAndMigrationEngine::ComputeSchemaDiff(
    const std::vector<SqlTableInfo>& baseline,
    const std::vector<SqlTableInfo>& desired,
    SqlDialect dialect,
    const std::wstring& baselineLabel,
    const std::wstring& desiredLabel) {

    SchemaDiffResult res;
    res.baselineLabel = baselineLabel;
    res.targetLabel = desiredLabel;

    std::unordered_map<std::wstring, const SqlTableInfo*> baseMap, desMap;
    for (const auto& t : baseline) baseMap[ToLowerW(t.name)] = &t;
    for (const auto& t : desired)  desMap[ToLowerW(t.name)]  = &t;

    std::wostringstream sql;
    sql << L"-- Migration: " << baselineLabel << L" -> " << desiredLabel << L"\n";

    // 1. Added tables
    for (const auto& dt : desired) {
        if (baseMap.find(ToLowerW(dt.name)) == baseMap.end()) {
            SchemaDiffItem item;
            item.kind = SchemaDiffKind::TableAdded;
            item.tableName = dt.name;
            item.newSpec = std::to_wstring(dt.columns.size()) + L" columns";
            item.summary = L"+ CREATE TABLE " + dt.name + L" (" + item.newSpec + L")";
            res.items.push_back(std::move(item));
            res.addedCount++;
            sql << L"\n" << BuildCreateTableSql(dt, dialect) << L"\n";
        }
    }

    // 2. Diff matching tables (Columns + Foreign Keys)
    for (const auto& dt : desired) {
        auto bIt = baseMap.find(ToLowerW(dt.name));
        if (bIt == baseMap.end()) continue;
        const SqlTableInfo& bt = *bIt->second;

        std::unordered_map<std::wstring, const SqlColumnInfo*> baseCols, desCols;
        for (const auto& c : bt.columns) baseCols[ToLowerW(c.name)] = &c;
        for (const auto& c : dt.columns) desCols[ToLowerW(c.name)]  = &c;

        for (const auto& dc : dt.columns) {
            auto cIt = baseCols.find(ToLowerW(dc.name));
            if (cIt == baseCols.end()) {
                SchemaDiffItem item;
                item.kind = SchemaDiffKind::ColumnAdded;
                item.tableName = dt.name;
                item.itemName = dc.name;
                item.newSpec = FormatColDefSql(dc, dialect);
                item.summary = L"+ ADD COLUMN " + dt.name + L"." + dc.name + L" (" + dc.type + L")";
                res.items.push_back(std::move(item));
                res.addedCount++;
                sql << L"ALTER TABLE " << QuoteIdent(dt.name, dialect)
                    << L" ADD COLUMN " << FormatColDefSql(dc, dialect) << L";\n";
            } else {
                const SqlColumnInfo& bc = *cIt->second;
                if (ToLowerW(bc.type) != ToLowerW(dc.type) || bc.notNull != dc.notNull) {
                    SchemaDiffItem item;
                    item.kind = SchemaDiffKind::ColumnModified;
                    item.tableName = dt.name;
                    item.itemName = dc.name;
                    item.oldSpec = bc.type + (bc.notNull ? L" NOT NULL" : L" NULL");
                    item.newSpec = dc.type + (dc.notNull ? L" NOT NULL" : L" NULL");
                    item.summary = L"~ MODIFY " + dt.name + L"." + dc.name + L": " + item.oldSpec + L" -> " + item.newSpec;
                    res.items.push_back(std::move(item));
                    res.modifiedCount++;
                    if (dialect == SqlDialect::MySQL) {
                        sql << L"ALTER TABLE " << QuoteIdent(dt.name, dialect)
                            << L" MODIFY COLUMN " << FormatColDefSql(dc, dialect) << L";\n";
                    } else {
                        sql << L"-- SQLite column type/nullability change requires table rebuild for "
                            << dt.name << L"." << dc.name << L" (" << item.oldSpec << L" -> " << item.newSpec << L")\n";
                    }
                }
            }
        }

        for (const auto& bc : bt.columns) {
            if (desCols.find(ToLowerW(bc.name)) == desCols.end()) {
                SchemaDiffItem item;
                item.kind = SchemaDiffKind::ColumnRemoved;
                item.tableName = dt.name;
                item.itemName = bc.name;
                item.oldSpec = bc.type;
                item.summary = L"- DROP COLUMN " + dt.name + L"." + bc.name;
                res.items.push_back(std::move(item));
                res.removedCount++;
                sql << L"ALTER TABLE " << QuoteIdent(dt.name, dialect)
                    << L" DROP COLUMN " << QuoteIdent(bc.name, dialect) << L";\n";
            }
        }

        // Foreign keys delta
        auto fkKey = [](const SqlForeignKeyInfo& f) {
            return ToLowerW(f.column) + L"->" + ToLowerW(f.refTable) + L"." + ToLowerW(f.refColumn);
        };
        std::unordered_set<std::wstring> baseFks, desFks;
        for (const auto& f : bt.foreignKeys) baseFks.insert(fkKey(f));
        for (const auto& f : dt.foreignKeys) desFks.insert(fkKey(f));

        for (const auto& df : dt.foreignKeys) {
            if (baseFks.count(fkKey(df)) == 0) {
                SchemaDiffItem item;
                item.kind = SchemaDiffKind::ForeignKeyAdded;
                item.tableName = dt.name;
                item.itemName = df.column;
                item.newSpec = df.refTable + L"." + df.refColumn;
                item.summary = L"+ ADD FK " + dt.name + L"." + df.column + L" -> " + item.newSpec;
                res.items.push_back(std::move(item));
                res.addedCount++;
                if (dialect == SqlDialect::MySQL) {
                    sql << L"ALTER TABLE " << QuoteIdent(dt.name, dialect)
                        << L" ADD CONSTRAINT " << QuoteIdent(L"fk_" + dt.name + L"_" + df.column, dialect)
                        << L" FOREIGN KEY (" << QuoteIdent(df.column, dialect) << L") REFERENCES "
                        << QuoteIdent(df.refTable, dialect) << L"(" << QuoteIdent(df.refColumn, dialect) << L");\n";
                } else {
                    sql << L"-- Add foreign key " << dt.name << L"." << df.column
                        << L" -> " << df.refTable << L"." << df.refColumn << L"\n";
                }
            }
        }
    }

    // 3. Removed tables
    for (const auto& bt : baseline) {
        if (desMap.find(ToLowerW(bt.name)) == desMap.end()) {
            SchemaDiffItem item;
            item.kind = SchemaDiffKind::TableRemoved;
            item.tableName = bt.name;
            item.summary = L"- DROP TABLE " + bt.name;
            res.items.push_back(std::move(item));
            res.removedCount++;
            sql << L"DROP TABLE IF EXISTS " << QuoteIdent(bt.name, dialect) << L";\n";
        }
    }

    if (res.items.empty()) {
        sql << L"-- Schemas are identical (0 differences found).\n";
    }
    res.migrationSql = sql.str();
    return res;
}

std::wstring ErdAndMigrationEngine::GenerateFullDdl(const std::vector<SqlTableInfo>& schema, SqlDialect dialect) {
    std::wostringstream out;
    out << L"-- Phide Generated Schema DDL (" << schema.size() << L" tables)\n";
    for (const auto& t : schema) {
        out << L"\n" << BuildCreateTableSql(t, dialect) << L"\n";
    }
    return out.str();
}

namespace {

struct RectObstacle {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

static bool SegIntersectsRect(float x1, float y1, float x2, float y2, const RectObstacle& r) noexcept {
    const float minX = (std::min)(x1, x2);
    const float maxX = (std::max)(x1, x2);
    const float minY = (std::min)(y1, y2);
    const float maxY = (std::max)(y1, y2);
    return (maxX > r.left + 0.5f) && (minX < r.right - 0.5f) &&
           (maxY > r.top  + 0.5f) && (minY < r.bottom - 0.5f);
}

static bool IsFkColumnInTable(const SqlTableInfo& tbl, const std::wstring& colName, bool* outNullable = nullptr) {
    const std::wstring low = ToLowerW(colName);
    for (const auto& fk : tbl.foreignKeys) {
        if (ToLowerW(fk.column) == low) {
            if (outNullable) {
                *outNullable = true;
                for (const auto& c : tbl.columns) {
                    if (ToLowerW(c.name) == low) {
                        *outNullable = !c.notNull;
                        break;
                    }
                }
            }
            return true;
        }
    }
    return false;
}

static int FindColumnIndex(const SqlTableInfo& tbl, const std::wstring& colName) {
    const std::wstring low = ToLowerW(colName);
    for (size_t i = 0; i < tbl.columns.size(); ++i) {
        if (ToLowerW(tbl.columns[i].name) == low) return static_cast<int>(i);
    }
    return -1;
}

static float EstimateColumnRowCenterY(const ErdNodeLayout& node, const SqlTableInfo& tbl, int colIdx, ErdNotation notation) {
    const float hdrH = (notation == ErdNotation::Uml) ? 40.0f : 31.0f;
    const float minY = node.y + hdrH + 10.0f;
    const float maxY = (std::max)(minY, node.y + node.height - 10.0f);
    if (colIdx < 0 || tbl.columns.empty()) {
        return (minY + maxY) * 0.5f;
    }
    const size_t clampedIdx = (std::min)(static_cast<size_t>(colIdx), size_t(13));
    const float rawY = node.y + hdrH + 6.0f + static_cast<float>(clampedIdx) * 20.0f + 9.5f;
    return std::clamp(rawY, minY, maxY);
}

static std::wstring DeriveConciseRelationLabel(const std::wstring& fkCol, bool inferred) {
    if (fkCol.empty()) return inferred ? L"~ref" : L"has";
    std::wstring s = ToLowerW(fkCol);
    if (s.size() > 3 && s.rfind(L"_id") == s.size() - 3) {
        s = s.substr(0, s.size() - 3);
    }
    if (s.empty() || s.size() > 9) return inferred ? L"~ref" : L"has";
    return inferred ? (L"~" + s) : s;
}

static std::wstring FormatCompactSvgType(std::wstring t) {
    if (t.empty()) return t;
    std::wstring low = ToLowerW(t);
    auto rep = [&](const std::wstring& pref, const std::wstring& shortP) -> bool {
        if (low.rfind(pref, 0) == 0) {
            t = shortP + t.substr(pref.size());
            return true;
        }
        return false;
    };
    if (rep(L"timestamp with time zone", L"timestamptz")) return t;
    if (rep(L"timestamp without time zone", L"timestamp")) return t;
    if (rep(L"time with time zone", L"timetz")) return t;
    if (rep(L"time without time zone", L"time")) return t;
    if (rep(L"character varying", L"varchar")) return t;
    if (rep(L"double precision", L"float8")) return t;
    return t;
}

} // namespace

ErdSchemaInterpretation ErdAndMigrationEngine::InterpretSchema(
    const std::vector<SqlTableInfo>& tables,
    bool inferImplicitFks) {

    ErdSchemaInterpretation out;
    const size_t n = tables.size();
    out.entities.resize(n);
    std::unordered_map<std::wstring, size_t> nameToIdx;
    for (size_t i = 0; i < n; ++i) {
        out.entities[i].tableIdx = i;
        nameToIdx[ToLowerW(tables[i].name)] = i;
    }

    // Track explicit FK columns on each child table to avoid duplicate inferred edges
    std::vector<std::unordered_set<std::wstring>> explicitFkCols(n);

    for (size_t ci = 0; ci < n; ++ci) {
        const auto& childTbl = tables[ci];
        int pkCount = 0;
        for (const auto& c : childTbl.columns) {
            if (c.isPrimaryKey) ++pkCount;
        }
        for (const auto& fk : childTbl.foreignKeys) {
            auto pIt = nameToIdx.find(ToLowerW(fk.refTable));
            if (pIt == nameToIdx.end()) continue;
            const size_t pi = pIt->second;
            explicitFkCols[ci].insert(ToLowerW(fk.column));

            ErdInterpretedRelation rel;
            rel.parentTableIdx = pi;
            rel.childTableIdx = ci;
            rel.fkColumn = fk.column;
            rel.refColumn = fk.refColumn.empty() ? L"id" : fk.refColumn;
            rel.childColIdx = FindColumnIndex(childTbl, fk.column);
            rel.parentColIdx = FindColumnIndex(tables[pi], rel.refColumn);
            if (rel.parentColIdx < 0) {
                for (size_t k = 0; k < tables[pi].columns.size(); ++k) {
                    if (tables[pi].columns[k].isPrimaryKey) {
                        rel.parentColIdx = static_cast<int>(k);
                        break;
                    }
                }
            }
            if (rel.childColIdx >= 0 && static_cast<size_t>(rel.childColIdx) < childTbl.columns.size()) {
                const auto& ccol = childTbl.columns[static_cast<size_t>(rel.childColIdx)];
                rel.childOptional = !ccol.notNull;
                rel.isIdentifying = ccol.isPrimaryKey;
                rel.isOneToOne = (ccol.isPrimaryKey && pkCount == 1);
            }
            rel.isSelfRef = (pi == ci);
            rel.isInferred = false;
            rel.relationLabel = DeriveConciseRelationLabel(rel.fkColumn, false);
            out.relations.push_back(std::move(rel));

            if (pi != ci) {
                ++out.entities[pi].inboundFkCount;
                ++out.entities[ci].outboundFkCount;
            }
        }
    }

    // Optional heuristic *_id FK inference (for schema tables missing explicit FOREIGN KEY DDL)
    if (inferImplicitFks) {
        for (size_t ci = 0; ci < n; ++ci) {
            const auto& childTbl = tables[ci];
            int pkCount = 0;
            for (const auto& c : childTbl.columns) {
                if (c.isPrimaryKey) ++pkCount;
            }
            for (size_t cIdx = 0; cIdx < childTbl.columns.size(); ++cIdx) {
                const auto& col = childTbl.columns[cIdx];
                const std::wstring colLow = ToLowerW(col.name);
                if (explicitFkCols[ci].count(colLow)) continue;
                if (colLow.size() <= 3 || colLow.rfind(L"_id") != colLow.size() - 3) continue;

                const std::wstring stem = colLow.substr(0, colLow.size() - 3);
                const std::wstring stemPlural = stem + L"s";
                size_t matchIdx = SIZE_MAX;
                size_t matchCount = 0;
                for (size_t pi = 0; pi < n; ++pi) {
                    if (pi == ci) continue;
                    const std::wstring tLow = ToLowerW(tables[pi].name);
                    if (tLow == stem || tLow == stemPlural ||
                        (tLow.size() > stem.size() + 1 && tLow.rfind(L"_" + stem) == tLow.size() - stem.size() - 1) ||
                        (tLow.size() > stemPlural.size() + 1 && tLow.rfind(L"_" + stemPlural) == tLow.size() - stemPlural.size() - 1)) {
                        matchIdx = pi;
                        ++matchCount;
                    }
                }
                if (matchCount == 1 && matchIdx < n) {
                    ErdInterpretedRelation rel;
                    rel.parentTableIdx = matchIdx;
                    rel.childTableIdx = ci;
                    rel.fkColumn = col.name;
                    rel.refColumn = L"id";
                    rel.childColIdx = static_cast<int>(cIdx);
                    rel.parentColIdx = FindColumnIndex(tables[matchIdx], L"id");
                    if (rel.parentColIdx < 0 && !tables[matchIdx].columns.empty()) rel.parentColIdx = 0;
                    rel.childOptional = !col.notNull;
                    rel.isIdentifying = col.isPrimaryKey;
                    rel.isOneToOne = (col.isPrimaryKey && pkCount == 1);
                    rel.isSelfRef = false;
                    rel.isInferred = true;
                    rel.relationLabel = DeriveConciseRelationLabel(rel.fkColumn, true);
                    out.relations.push_back(std::move(rel));
                    ++out.entities[matchIdx].inboundFkCount;
                    ++out.entities[ci].outboundFkCount;
                }
            }
        }
    }

    // Classify entity roles (Strong, M:N Junction, WeakIdentifying, Standalone) & compute DAG layers
    for (size_t i = 0; i < n; ++i) {
        const auto& tbl = tables[i];
        auto& ent = out.entities[i];

        std::unordered_set<size_t> distinctParents;
        bool hasIdentifyingFk = false;
        for (const auto& r : out.relations) {
            if (r.childTableIdx == i && !r.isSelfRef) {
                distinctParents.insert(r.parentTableIdx);
                if (r.isIdentifying) hasIdentifyingFk = true;
            }
        }

        size_t nonKeyPayloadCols = 0;
        for (const auto& col : tbl.columns) {
            if (col.isPrimaryKey) continue;
            bool isRelFk = false;
            const std::wstring colLow = ToLowerW(col.name);
            for (const auto& r : out.relations) {
                if (r.childTableIdx == i && ToLowerW(r.fkColumn) == colLow) {
                    isRelFk = true;
                    break;
                }
            }
            if (!isRelFk) ++nonKeyPayloadCols;
        }

        if (ent.inboundFkCount == 0 && ent.outboundFkCount == 0) {
            ent.isStandalone = true;
            ent.role = ErdEntityRole::Standalone;
        } else if (distinctParents.size() >= 2 && nonKeyPayloadCols <= 2 && ent.inboundFkCount == 0) {
            ent.role = ErdEntityRole::Junction;
            ent.badgeLabel = L"M:N";
        } else if (hasIdentifyingFk) {
            ent.role = ErdEntityRole::WeakIdentifying;
            ent.badgeLabel = L"WEAK";
        } else {
            ent.role = ErdEntityRole::Strong;
        }
    }

    // Topological longest-path layer assignment on connected component (edge: parent -> child)
    for (size_t pass = 0; pass < n; ++pass) {
        bool changed = false;
        for (const auto& r : out.relations) {
            if (r.isSelfRef) continue;
            const int targetLayer = out.entities[r.parentTableIdx].layer + 1;
            if (out.entities[r.childTableIdx].layer < targetLayer) {
                out.entities[r.childTableIdx].layer = targetLayer;
                changed = true;
            }
        }
        if (!changed) break;
    }

    return out;
}

std::vector<ErdNodeLayout> ErdAndMigrationEngine::ComputeSmartLayout(
    const std::vector<SqlTableInfo>& tables,
    const std::vector<ErdNodeLayout>& sizedNodes,
    ErdNotation notation,
    bool inferImplicitFks) {

    std::vector<ErdNodeLayout> result = sizedNodes;
    if (result.size() != tables.size()) {
        result.resize(tables.size());
        for (size_t i = 0; i < tables.size(); ++i) result[i].tableName = tables[i].name;
    }
    if (tables.empty()) return result;

    const ErdSchemaInterpretation interp = InterpretSchema(tables, inferImplicitFks);

    int maxRelLayer = 0;
    std::vector<size_t> standaloneIdxs;
    for (size_t i = 0; i < tables.size(); ++i) {
        if (interp.entities[i].isStandalone) {
            standaloneIdxs.push_back(i);
        } else {
            maxRelLayer = (std::max)(maxRelLayer, interp.entities[i].layer);
        }
    }

    std::vector<std::vector<size_t>> layerCols(static_cast<size_t>(maxRelLayer + 1));
    bool hasAnyConnected = false;
    for (size_t i = 0; i < tables.size(); ++i) {
        if (!interp.entities[i].isStandalone) {
            hasAnyConnected = true;
            layerCols[static_cast<size_t>(interp.entities[i].layer)].push_back(i);
        }
    }

    // Initial vertical sort inside each layer by degree (hubs first) & name
    for (auto& vec : layerCols) {
        std::sort(vec.begin(), vec.end(), [&](size_t a, size_t b) {
            size_t degA = interp.entities[a].inboundFkCount + interp.entities[a].outboundFkCount;
            size_t degB = interp.entities[b].inboundFkCount + interp.entities[b].outboundFkCount;
            if (degA != degB) return degA > degB;
            return tables[a].name < tables[b].name;
        });
    }

    // 4-sweep Barycenter ordering to minimize crossing wires between adjacent hierarchical layers
    std::vector<float> orderRank(tables.size(), 0.0f);
    for (size_t l = 0; l < layerCols.size(); ++l) {
        for (size_t pos = 0; pos < layerCols[l].size(); ++pos) {
            orderRank[layerCols[l][pos]] = static_cast<float>(pos);
        }
    }
    for (int sweep = 0; sweep < 4; ++sweep) {
        for (size_t l = 1; l < layerCols.size(); ++l) {
            auto& vec = layerCols[l];
            std::unordered_map<size_t, float> bary;
            for (size_t idx : vec) {
                float sum = 0.0f;
                int cnt = 0;
                for (const auto& r : interp.relations) {
                    if (!r.isSelfRef && r.childTableIdx == idx && !interp.entities[r.parentTableIdx].isStandalone) {
                        sum += orderRank[r.parentTableIdx];
                        ++cnt;
                    }
                }
                bary[idx] = (cnt > 0) ? (sum / static_cast<float>(cnt)) : orderRank[idx];
            }
            std::stable_sort(vec.begin(), vec.end(), [&](size_t a, size_t b) {
                return bary[a] < bary[b];
            });
            for (size_t pos = 0; pos < vec.size(); ++pos) orderRank[vec[pos]] = static_cast<float>(pos);
        }
        for (int l = static_cast<int>(layerCols.size()) - 2; l >= 0; --l) {
            auto& vec = layerCols[static_cast<size_t>(l)];
            std::unordered_map<size_t, float> bary;
            for (size_t idx : vec) {
                float sum = 0.0f;
                int cnt = 0;
                for (const auto& r : interp.relations) {
                    if (!r.isSelfRef && r.parentTableIdx == idx && !interp.entities[r.childTableIdx].isStandalone) {
                        sum += orderRank[r.childTableIdx];
                        ++cnt;
                    }
                }
                bary[idx] = (cnt > 0) ? (sum / static_cast<float>(cnt)) : orderRank[idx];
            }
            std::stable_sort(vec.begin(), vec.end(), [&](size_t a, size_t b) {
                return bary[a] < bary[b];
            });
            for (size_t pos = 0; pos < vec.size(); ++pos) orderRank[vec[pos]] = static_cast<float>(pos);
        }
    }

    // Generous corridor spacing for routing channels (extra breathing room for Chen diamonds & attribute ovals)
    const float gapX = (notation == ErdNotation::Chen) ? 228.0f : 136.0f;
    const float gapY = (notation == ErdNotation::Chen) ? 148.0f : 64.0f;

    float curColX = 0.0f;
    float connectedMaxBottom = 0.0f;
    if (hasAnyConnected) {
        for (size_t l = 0; l < layerCols.size(); ++l) {
            if (layerCols[l].empty()) continue;
            float colW = 256.0f;
            for (size_t idx : layerCols[l]) colW = (std::max)(colW, result[idx].width);

            float curY = 0.0f;
            for (size_t idx : layerCols[l]) {
                result[idx].x = curColX;
                result[idx].y = curY;
                curY += result[idx].height + gapY;
            }
            connectedMaxBottom = (std::max)(connectedMaxBottom, curY);
            curColX += colW + gapX;
        }
    }

    // Place disconnected standalone tables neatly in an isolated grid row below connected graph (never amid FK wires)
    if (!standaloneIdxs.empty()) {
        std::sort(standaloneIdxs.begin(), standaloneIdxs.end(), [&](size_t a, size_t b) {
            return tables[a].name < tables[b].name;
        });
        const float startY = hasAnyConnected ? (connectedMaxBottom + (notation == ErdNotation::Chen ? 56.0f : 28.0f)) : 0.0f;
        const size_t sCols = (std::max)(size_t(1), (std::min)(size_t(4), standaloneIdxs.size()));
        std::vector<float> sColMaxW(sCols, 256.0f);
        for (size_t k = 0; k < standaloneIdxs.size(); ++k) {
            sColMaxW[k % sCols] = (std::max)(sColMaxW[k % sCols], result[standaloneIdxs[k]].width);
        }
        std::vector<float> sColX(sCols, 0.0f);
        const float sGapX = (notation == ErdNotation::Chen) ? 92.0f : 72.0f;
        const float sGapY = (notation == ErdNotation::Chen) ? 92.0f : 48.0f;
        for (size_t c = 1; c < sCols; ++c) {
            sColX[c] = sColX[c - 1] + sColMaxW[c - 1] + sGapX;
        }
        std::vector<float> sColY(sCols, startY);
        for (size_t k = 0; k < standaloneIdxs.size(); ++k) {
            size_t idx = standaloneIdxs[k];
            size_t c = k % sCols;
            result[idx].x = sColX[c];
            result[idx].y = sColY[c];
            sColY[c] += result[idx].height + sGapY;
        }
    }

    return result;
}

std::vector<ErdRoutedEdge> ErdAndMigrationEngine::RouteErdEdges(
    const std::vector<SqlTableInfo>& tables,
    const std::vector<ErdNodeLayout>& nodes,
    const ErdSchemaInterpretation& interp,
    ErdNotation notation) {

    std::vector<ErdRoutedEdge> routed;
    if (nodes.size() != tables.size() || interp.relations.empty()) return routed;

    routed.resize(interp.relations.size());

    enum class PortSide { Left = 0, Right = 1 };
    struct PortEndpoint {
        size_t relIdx = 0;
        bool isParent = true;
        PortSide side = PortSide::Right;
        float idealY = 0.0f;
        float otherCenterY = 0.0f;
    };

    std::vector<std::vector<PortEndpoint>> leftPorts(tables.size());
    std::vector<std::vector<PortEndpoint>> rightPorts(tables.size());

    // 1. Choose port sides cleanly per relation without creating inside-box reversals
    for (size_t ri = 0; ri < interp.relations.size(); ++ri) {
        const auto& rel = interp.relations[ri];
        auto& re = routed[ri];
        re.relationIndex = ri;
        re.parentTableIdx = rel.parentTableIdx;
        re.childTableIdx = rel.childTableIdx;
        re.childOptional = rel.childOptional;
        re.isOneToOne = rel.isOneToOne;
        re.isIdentifying = rel.isIdentifying;
        re.isSelfRef = rel.isSelfRef;
        re.isInferred = rel.isInferred;
        re.relationLabel = rel.relationLabel;

        const auto& pn = nodes[rel.parentTableIdx];
        const auto& cn = nodes[rel.childTableIdx];

        if (rel.isSelfRef) {
            re.dir1 = 1.0f;
            re.dir2 = 1.0f;
            PortEndpoint epP{ ri, true, PortSide::Right,
                              EstimateColumnRowCenterY(pn, tables[rel.parentTableIdx], rel.parentColIdx, notation),
                              pn.y + pn.height * 0.5f };
            PortEndpoint epC{ ri, false, PortSide::Right,
                              EstimateColumnRowCenterY(cn, tables[rel.childTableIdx], rel.childColIdx, notation),
                              cn.y + cn.height * 0.5f };
            rightPorts[rel.parentTableIdx].push_back(epP);
            rightPorts[rel.childTableIdx].push_back(epC);
            continue;
        }

        const float gapR2L = cn.x - (pn.x + pn.width);
        const float gapL2R = pn.x - (cn.x + cn.width);
        const float pMidX = pn.x + pn.width * 0.5f;
        const float cMidX = cn.x + cn.width * 0.5f;

        PortSide pSide = PortSide::Right;
        PortSide cSide = PortSide::Left;
        if (gapR2L >= 36.0f) {
            pSide = PortSide::Right;
            cSide = PortSide::Left;
        } else if (gapL2R >= 36.0f) {
            pSide = PortSide::Left;
            cSide = PortSide::Right;
        } else {
            // Horizontally overlapping / stacked cards: exit & enter the SAME outer corridor
            if (cMidX >= pMidX) {
                pSide = PortSide::Right;
                cSide = PortSide::Right;
            } else {
                pSide = PortSide::Left;
                cSide = PortSide::Left;
            }
        }

        re.dir1 = (pSide == PortSide::Right) ? 1.0f : -1.0f;
        re.dir2 = (cSide == PortSide::Right) ? 1.0f : -1.0f;

        PortEndpoint epP{
            ri, true, pSide,
            EstimateColumnRowCenterY(pn, tables[rel.parentTableIdx], rel.parentColIdx, notation),
            cn.y + cn.height * 0.5f
        };
        PortEndpoint epC{
            ri, false, cSide,
            EstimateColumnRowCenterY(cn, tables[rel.childTableIdx], rel.childColIdx, notation),
            pn.y + pn.height * 0.5f
        };
        if (pSide == PortSide::Right) rightPorts[rel.parentTableIdx].push_back(epP);
        else leftPorts[rel.parentTableIdx].push_back(epP);

        if (cSide == PortSide::Right) rightPorts[rel.childTableIdx].push_back(epC);
        else leftPorts[rel.childTableIdx].push_back(epC);
    }

    // 2. Distribute ports along each table edge with strict >=18px spacing (eliminates symbol collision & T-junctions)
    auto assignPortsOnEdge = [&](size_t tIdx, PortSide side, std::vector<PortEndpoint>& eps) {
        if (eps.empty()) return;
        const auto& nd = nodes[tIdx];
        const float hdrH = (notation == ErdNotation::Uml) ? 40.0f : 31.0f;
        const float minY = nd.y + hdrH + 10.0f;
        const float maxY = (std::max)(minY, nd.y + nd.height - 10.0f);
        const float anchorX = (side == PortSide::Right) ? (nd.x + nd.width) : nd.x;

        std::sort(eps.begin(), eps.end(), [&](const PortEndpoint& a, const PortEndpoint& b) {
            if (std::abs(a.idealY - b.idealY) > 2.0f) return a.idealY < b.idealY;
            if (std::abs(a.otherCenterY - b.otherCenterY) > 1.0f) return a.otherCenterY < b.otherCenterY;
            return a.relIdx < b.relIdx;
        });

        const size_t k = eps.size();
        const float minGap = (k <= 1) ? 0.0f : (std::min)(19.0f, (maxY - minY) / static_cast<float>(k - 1));
        std::vector<float> ys(k);
        for (size_t i = 0; i < k; ++i) {
            ys[i] = std::clamp(eps[i].idealY, minY, maxY);
            if (i > 0 && ys[i] < ys[i - 1] + minGap) {
                ys[i] = ys[i - 1] + minGap;
            }
        }
        if (ys[k - 1] > maxY) {
            ys[k - 1] = maxY;
            for (int i = static_cast<int>(k) - 2; i >= 0; --i) {
                if (ys[static_cast<size_t>(i)] > ys[static_cast<size_t>(i + 1)] - minGap) {
                    ys[static_cast<size_t>(i)] = ys[static_cast<size_t>(i + 1)] - minGap;
                }
            }
        }

        for (size_t i = 0; i < k; ++i) {
            auto& re = routed[eps[i].relIdx];
            if (eps[i].isParent) {
                re.x1 = anchorX;
                re.y1 = ys[i];
            } else {
                re.x2 = anchorX;
                re.y2 = ys[i];
            }
        }
    };

    for (size_t ti = 0; ti < tables.size(); ++ti) {
        assignPortsOnEdge(ti, PortSide::Left, leftPorts[ti]);
        assignPortsOnEdge(ti, PortSide::Right, rightPorts[ti]);
    }

    // 3. Build obstacle rectangles around ALL table cards (extra vertical cushion in Chen for attribute ovals)
    std::vector<RectObstacle> tightObs(nodes.size()), padObs(nodes.size());
    const float clearX = (notation == ErdNotation::Chen) ? 28.0f : 24.0f;
    const float clearY = (notation == ErdNotation::Chen) ? 46.0f : 22.0f;
    for (size_t i = 0; i < nodes.size(); ++i) {
        tightObs[i] = { nodes[i].x, nodes[i].y, nodes[i].x + nodes[i].width, nodes[i].y + nodes[i].height };
        padObs[i] = { nodes[i].x - clearX, nodes[i].y - clearY,
                      nodes[i].x + nodes[i].width + clearX, nodes[i].y + nodes[i].height + clearY };
    }

    // 4. Build deduplicated orthogonal Hanan + staggered corridor tracks so routes stay separated cleanly
    std::vector<float> rawXs, rawYs;
    rawXs.reserve(nodes.size() * 6 + routed.size() * 4);
    rawYs.reserve(nodes.size() * 6 + routed.size() * 4);

    // Keep exact stub coordinates pinned so A* start/goal never snap off-axis onto nearby margin tracks
    std::vector<float> pinnedX, pinnedY;
    constexpr float kStubLen = 30.0f;
    for (const auto& re : routed) {
        if (re.isSelfRef) continue;
        pinnedX.push_back(re.x1 + re.dir1 * kStubLen);
        pinnedX.push_back(re.x2 + re.dir2 * kStubLen);
        pinnedY.push_back(re.y1);
        pinnedY.push_back(re.y2);
    }

    auto addIfClearOfPinned = [](std::vector<float>& dst, const std::vector<float>& pinned, float val) {
        for (float p : pinned) {
            if (std::abs(val - p) < 5.5f) return;
        }
        dst.push_back(val);
    };
    rawXs = pinnedX;
    rawYs = pinnedY;

    std::vector<std::pair<float, float>> xSpans, ySpans;
    for (const auto& nd : nodes) {
        addIfClearOfPinned(rawXs, pinnedX, nd.x - 26.0f);
        addIfClearOfPinned(rawXs, pinnedX, nd.x - 42.0f);
        addIfClearOfPinned(rawXs, pinnedX, nd.x + nd.width + 26.0f);
        addIfClearOfPinned(rawXs, pinnedX, nd.x + nd.width + 42.0f);
        addIfClearOfPinned(rawYs, pinnedY, nd.y - clearY);
        addIfClearOfPinned(rawYs, pinnedY, nd.y - clearY - 14.0f);
        addIfClearOfPinned(rawYs, pinnedY, nd.y + nd.height + clearY);
        addIfClearOfPinned(rawYs, pinnedY, nd.y + nd.height + clearY + 14.0f);
        xSpans.push_back({ nd.x, nd.x + nd.width });
        ySpans.push_back({ nd.y, nd.y + nd.height });
    }
    std::sort(xSpans.begin(), xSpans.end());
    for (size_t i = 0; i + 1 < xSpans.size(); ++i) {
        float gap = xSpans[i + 1].first - xSpans[i].second;
        if (gap >= 44.0f) {
            float mid = (xSpans[i].second + xSpans[i + 1].first) * 0.5f;
            addIfClearOfPinned(rawXs, pinnedX, mid);
            addIfClearOfPinned(rawXs, pinnedX, mid - 14.0f);
            addIfClearOfPinned(rawXs, pinnedX, mid + 14.0f);
            if (gap >= 90.0f) {
                addIfClearOfPinned(rawXs, pinnedX, mid - 28.0f);
                addIfClearOfPinned(rawXs, pinnedX, mid + 28.0f);
            }
        }
    }
    std::sort(ySpans.begin(), ySpans.end());
    for (size_t i = 0; i + 1 < ySpans.size(); ++i) {
        float gap = ySpans[i + 1].first - ySpans[i].second;
        if (gap >= 38.0f) {
            float mid = (ySpans[i].second + ySpans[i + 1].first) * 0.5f;
            addIfClearOfPinned(rawYs, pinnedY, mid);
            addIfClearOfPinned(rawYs, pinnedY, mid - 13.0f);
            addIfClearOfPinned(rawYs, pinnedY, mid + 13.0f);
        }
    }

    auto buildUniqueTracks = [](std::vector<float>& vals, size_t maxTracks) -> std::vector<float> {
        std::sort(vals.begin(), vals.end());
        std::vector<float> out;
        out.reserve(vals.size());
        for (float v : vals) {
            if (out.empty() || std::abs(v - out.back()) >= 1.5f) {
                out.push_back(v);
            }
        }
        while (out.size() > maxTracks) {
            std::vector<float> thinned;
            thinned.reserve(out.size());
            for (size_t i = 0; i < out.size(); ++i) {
                if ((i & 1u) == 0 || i + 1 == out.size()) thinned.push_back(out[i]);
            }
            out = std::move(thinned);
        }
        return out;
    };

    const std::vector<float> gridX = buildUniqueTracks(rawXs, 130);
    const std::vector<float> gridY = buildUniqueTracks(rawYs, 130);

    auto findNearestTrack = [](const std::vector<float>& grid, float target) -> size_t {
        if (grid.empty()) return 0;
        auto it = std::lower_bound(grid.begin(), grid.end(), target);
        if (it == grid.end()) return grid.size() - 1;
        if (it == grid.begin()) return 0;
        size_t idx = static_cast<size_t>(it - grid.begin());
        return (std::abs(grid[idx] - target) < std::abs(grid[idx - 1] - target)) ? idx : (idx - 1);
    };

    // Maintain routed segments so parallel edges spread cleanly onto non-overlapping lanes
    struct UsedH { float y, xMin, xMax; };
    struct UsedV { float x, yMin, yMax; };
    std::vector<UsedH> usedH;
    std::vector<UsedV> usedV;

    // Order short-span edges first so tight connections claim direct lanes before long highways route around
    std::vector<size_t> routeOrder(routed.size());
    for (size_t i = 0; i < routed.size(); ++i) routeOrder[i] = i;
    std::sort(routeOrder.begin(), routeOrder.end(), [&](size_t a, size_t b) {
        float da = std::abs(routed[a].x1 - routed[a].x2) + std::abs(routed[a].y1 - routed[a].y2);
        float db = std::abs(routed[b].x1 - routed[b].x2) + std::abs(routed[b].y1 - routed[b].y2);
        return da < db;
    });

    const size_t W = gridX.size();
    const size_t H = gridY.size();
    std::vector<float> distBuf;
    std::vector<int> prevStateBuf;
    if (W > 0 && H > 0) {
        distBuf.resize(W * H * 2, 1e30f);
        prevStateBuf.resize(W * H * 2, -1);
    }

    int selfLoopCount = 0;
    for (size_t ri : routeOrder) {
        auto& re = routed[ri];
        if (re.isSelfRef) {
            const auto& pn = nodes[re.parentTableIdx];
            const float loopX = pn.x + pn.width + 44.0f + static_cast<float>(selfLoopCount) * 16.0f;
            ++selfLoopCount;
            if (std::abs(re.y1 - re.y2) < 16.0f) {
                re.y1 = (std::max)(pn.y + 40.0f, (std::min)(re.y1, pn.y + pn.height - 32.0f));
                re.y2 = (std::min)(pn.y + pn.height - 14.0f, re.y1 + 22.0f);
            }
            re.points = {
                { re.x1, re.y1 }, { loopX, re.y1 }, { loopX, re.y2 }, { re.x2, re.y2 }
            };
            continue;
        }

        const float sx1 = re.x1 + re.dir1 * kStubLen;
        const float sy1 = re.y1;
        const float sx2 = re.x2 + re.dir2 * kStubLen;
        const float sy2 = re.y2;

        std::vector<ErdPointF> midPts;
        if (W >= 2 && H >= 2) {
            const size_t gxStart = findNearestTrack(gridX, sx1);
            const size_t gyStart = findNearestTrack(gridY, sy1);
            const size_t gxGoal  = findNearestTrack(gridX, sx2);
            const size_t gyGoal  = findNearestTrack(gridY, sy2);

            std::fill(distBuf.begin(), distBuf.end(), 1e30f);
            std::fill(prevStateBuf.begin(), prevStateBuf.end(), -1);

            struct QNode {
                float f;
                float g;
                int state;
                bool operator>(const QNode& o) const noexcept { return f > o.f; }
            };
            auto encode = [W](size_t x, size_t y, int dir) -> int {
                return static_cast<int>((y * W + x) * 2 + static_cast<size_t>(dir));
            };

            std::priority_queue<QNode, std::vector<QNode>, std::greater<QNode>> pq;
            const int s0 = encode(gxStart, gyStart, 0); // start stub is horizontal
            distBuf[static_cast<size_t>(s0)] = 0.0f;
            pq.push({ std::abs(gridX[gxStart] - sx2) + std::abs(gridY[gyStart] - sy2), 0.0f, s0 });

            int bestGoalState = -1;
            float bestGoalCost = 1e30f;
            size_t pops = 0;
            constexpr size_t kMaxAStarPops = 18000;

            auto evalSegCost = [&](float ax, float ay, float bx, float by, int newDir, int prevDir) -> float {
                float base = std::abs(bx - ax) + std::abs(by - ay);
                if (newDir != prevDir) base += 60.0f; // strong preference for clean straight/Z bends

                for (size_t k = 0; k < tightObs.size(); ++k) {
                    if (SegIntersectsRect(ax, ay, bx, by, tightObs[k])) {
                        base += 500000.0f; // never route under ANY table card
                    } else if (SegIntersectsRect(ax, ay, bx, by, padObs[k])) {
                        base += 32000.0f;  // keep 16px clear margin around tables
                    }
                }

                if (newDir == 0) {
                    const float segMinX = (std::min)(ax, bx);
                    const float segMaxX = (std::max)(ax, bx);
                    for (const auto& u : usedH) {
                        if (std::abs(ay - u.y) < 7.5f && segMaxX > u.xMin - 4.0f && segMinX < u.xMax + 4.0f) {
                            base += 9000.0f; // force separate parallel horizontal track
                        }
                    }
                    for (const auto& uv : usedV) {
                        if (uv.x > segMinX + 2.0f && uv.x < segMaxX - 2.0f &&
                            ay > uv.yMin + 2.0f && ay < uv.yMax - 2.0f) {
                            base += 180.0f; // prefer avoiding crossings when cheap
                        }
                    }
                } else {
                    const float segMinY = (std::min)(ay, by);
                    const float segMaxY = (std::max)(ay, by);
                    for (const auto& uv : usedV) {
                        if (std::abs(ax - uv.x) < 7.5f && segMaxY > uv.yMin - 4.0f && segMinY < uv.yMax + 4.0f) {
                            base += 9000.0f; // force separate parallel vertical track (zero T-junctions)
                        }
                    }
                    for (const auto& u : usedH) {
                        if (u.y > segMinY + 2.0f && u.y < segMaxY - 2.0f &&
                            ax > u.xMin + 2.0f && ax < u.xMax - 2.0f) {
                            base += 180.0f;
                        }
                    }
                }
                return base;
            };

            while (!pq.empty() && pops < kMaxAStarPops) {
                QNode cur = pq.top();
                pq.pop();
                if (cur.g > distBuf[static_cast<size_t>(cur.state)]) continue;
                ++pops;

                const size_t cell = static_cast<size_t>(cur.state) / 2;
                const int prevDir = static_cast<size_t>(cur.state) & 1;
                const size_t cx = cell % W;
                const size_t cy = cell / W;

                if (cx == gxGoal && cy == gyGoal) {
                    const float finishCost = cur.g + ((prevDir != 0) ? 35.0f : 0.0f);
                    if (finishCost < bestGoalCost) {
                        bestGoalCost = finishCost;
                        bestGoalState = cur.state;
                    }
                    break;
                }

                const float ax = gridX[cx];
                const float ay = gridY[cy];
                const int dxs[4] = { 1, -1, 0, 0 };
                const int dys[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; ++d) {
                    const int nxi = static_cast<int>(cx) + dxs[d];
                    const int nyi = static_cast<int>(cy) + dys[d];
                    if (nxi < 0 || nyi < 0 || nxi >= static_cast<int>(W) || nyi >= static_cast<int>(H)) continue;
                    const size_t nx = static_cast<size_t>(nxi);
                    const size_t ny = static_cast<size_t>(nyi);
                    const int newDir = (d < 2) ? 0 : 1;
                    const float bx = gridX[nx];
                    const float by = gridY[ny];

                    const float stepCost = evalSegCost(ax, ay, bx, by, newDir, prevDir);
                    const float ng = cur.g + stepCost;
                    const int ns = encode(nx, ny, newDir);
                    if (ng < distBuf[static_cast<size_t>(ns)]) {
                        distBuf[static_cast<size_t>(ns)] = ng;
                        prevStateBuf[static_cast<size_t>(ns)] = cur.state;
                        const float h = std::abs(bx - gridX[gxGoal]) + std::abs(by - gridY[gyGoal]);
                        pq.push({ ng + h, ng, ns });
                    }
                }
            }

            if (bestGoalState >= 0) {
                std::vector<ErdPointF> rev;
                int s = bestGoalState;
                while (s >= 0) {
                    const size_t c = static_cast<size_t>(s) / 2;
                    rev.push_back({ gridX[c % W], gridY[c / W] });
                    s = prevStateBuf[static_cast<size_t>(s)];
                }
                std::reverse(rev.begin(), rev.end());
                midPts = std::move(rev);
            }
        }

        // Assemble full polyline starting cleanly at exact (x1, y1) and ending at (x2, y2)
        std::vector<ErdPointF> full;
        full.push_back({ re.x1, re.y1 });
        full.push_back({ sx1, sy1 });
        if (!midPts.empty()) {
            // Re-align A* boundary endpoints smoothly onto exact sy1/sy2 orthogonal rails
            for (const auto& pt : midPts) {
                if (full.back().x != pt.x && full.back().y != pt.y) {
                    full.push_back({ pt.x, full.back().y });
                }
                full.push_back(pt);
            }
            if (std::abs(full.back().x - sx2) >= 0.5f && std::abs(full.back().y - sy2) >= 0.5f) {
                full.push_back({ full.back().x, sy2 });
            }
            full.push_back({ sx2, sy2 });
        } else {
            const float midX = (sx1 + sx2) * 0.5f;
            full.push_back({ midX, sy1 });
            full.push_back({ midX, sy2 });
            full.push_back({ sx2, sy2 });
        }
        full.push_back({ re.x2, re.y2 });

        // Collapse duplicates & collinear orthogonal midpoints
        std::vector<ErdPointF> clean;
        for (const auto& pt : full) {
            if (!clean.empty() && std::abs(clean.back().x - pt.x) < 0.5f && std::abs(clean.back().y - pt.y) < 0.5f) {
                continue;
            }
            while (clean.size() >= 2) {
                const auto& a = clean[clean.size() - 2];
                const auto& b = clean[clean.size() - 1];
                const bool sameX = (std::abs(a.x - b.x) < 0.5f && std::abs(b.x - pt.x) < 0.5f);
                const bool sameY = (std::abs(a.y - b.y) < 0.5f && std::abs(b.y - pt.y) < 0.5f);
                if (sameX || sameY) {
                    clean.pop_back();
                } else {
                    break;
                }
            }
            clean.push_back(pt);
        }

        // Enforce strict orthogonality between any slight grid-snap residuals
        re.points.clear();
        for (size_t i = 0; i < clean.size(); ++i) {
            if (!re.points.empty()) {
                const auto& prev = re.points.back();
                if (std::abs(prev.x - clean[i].x) >= 0.5f && std::abs(prev.y - clean[i].y) >= 0.5f) {
                    re.points.push_back({ prev.x, clean[i].y });
                }
            }
            re.points.push_back(clean[i]);
        }

        // Record routed segments in occupancy lists so subsequent edges stay on distinct channels
        for (size_t i = 0; i + 1 < re.points.size(); ++i) {
            const auto& a = re.points[i];
            const auto& b = re.points[i + 1];
            if (std::abs(a.y - b.y) < 0.5f) {
                usedH.push_back({ a.y, (std::min)(a.x, b.x), (std::max)(a.x, b.x) });
            } else if (std::abs(a.x - b.x) < 0.5f) {
                usedV.push_back({ a.x, (std::min)(a.y, b.y), (std::max)(a.y, b.y) });
            }
        }
    }

    // 5. Compute collision-free Chen diamond placements and Line-Jump bridge arcs at intersections
    std::vector<ErdPointF> placedChenDiamonds;
    const float ovalPadY = (notation == ErdNotation::Chen) ? 54.0f : 24.0f;
    auto isDiamondClear = [&](float cx, float cy, bool checkDiamonds) -> bool {
        for (const auto& nd : nodes) {
            if (cx + 38.0f > nd.x - 6.0f && cx - 38.0f < nd.x + nd.width + 6.0f &&
                cy + 20.0f > nd.y - ovalPadY && cy - 20.0f < nd.y + nd.height + ovalPadY) {
                return false;
            }
        }
        if (checkDiamonds) {
            for (const auto& pd : placedChenDiamonds) {
                if (std::abs(cx - pd.x) < 76.0f && std::abs(cy - pd.y) < 40.0f) return false;
            }
        }
        return true;
    };

    for (size_t ei = 0; ei < routed.size(); ++ei) {
        auto& re = routed[ei];
        if (re.points.size() < 2) continue;

        float totalLen = 0.0f;
        for (size_t s = 0; s + 1 < re.points.size(); ++s) {
            totalLen += std::abs(re.points[s + 1].x - re.points[s].x) + std::abs(re.points[s + 1].y - re.points[s].y);
        }
        auto pointAtDist = [&](float dist) -> ErdPointF {
            float acc = 0.0f;
            for (size_t s = 0; s + 1 < re.points.size(); ++s) {
                const float dx = re.points[s + 1].x - re.points[s].x;
                const float dy = re.points[s + 1].y - re.points[s].y;
                const float sl = std::abs(dx) + std::abs(dy);
                if (acc + sl >= dist && sl > 0.0f) {
                    const float t = (dist - acc) / sl;
                    return { re.points[s].x + dx * t, re.points[s].y + dy * t };
                }
                acc += sl;
            }
            return re.points[re.points.size() / 2];
        };

        re.midPoint = pointAtDist(totalLen * 0.5f);
        if (notation == ErdNotation::Chen && totalLen > 40.0f) {
            std::vector<ErdPointF> cands;
            static constexpr float kFracs[] = {
                0.50f, 0.42f, 0.58f, 0.34f, 0.66f, 0.25f, 0.75f, 0.18f, 0.82f, 0.12f, 0.88f
            };
            for (float fr : kFracs) cands.push_back(pointAtDist(totalLen * fr));
            for (size_t s = 0; s + 1 < re.points.size(); ++s) {
                const float dx = re.points[s + 1].x - re.points[s].x;
                const float dy = re.points[s + 1].y - re.points[s].y;
                if (std::abs(dx) + std::abs(dy) >= 50.0f) {
                    cands.push_back({ (re.points[s].x + re.points[s + 1].x) * 0.5f,
                                      (re.points[s].y + re.points[s + 1].y) * 0.5f });
                    cands.push_back({ re.points[s].x + dx * 0.33f, re.points[s].y + dy * 0.33f });
                    cands.push_back({ re.points[s].x + dx * 0.67f, re.points[s].y + dy * 0.67f });
                }
            }
            bool picked = false;
            for (const auto& pt : cands) {
                if (isDiamondClear(pt.x, pt.y, true)) {
                    re.midPoint = pt;
                    picked = true;
                    break;
                }
            }
            if (!picked) {
                for (const auto& pt : cands) {
                    if (isDiamondClear(pt.x, pt.y, false)) {
                        // Nudge vertically/horizontally to avoid overlapping a sibling diamond
                        float tx = pt.x, ty = pt.y;
                        for (int attempt = 1; attempt <= 6; ++attempt) {
                            const float off = static_cast<float>((attempt + 1) / 2) * 42.0f * ((attempt & 1) ? 1.0f : -1.0f);
                            if (isDiamondClear(tx, ty + off, true)) { ty += off; break; }
                        }
                        re.midPoint = { tx, ty };
                        break;
                    }
                }
            }
            placedChenDiamonds.push_back(re.midPoint);
        }

        // Scan horizontal segments against vertical segments of ALL other edges for Line Jumps
        for (size_t s = 0; s + 1 < re.points.size(); ++s) {
            const auto& a = re.points[s];
            const auto& b = re.points[s + 1];
            if (std::abs(a.y - b.y) >= 0.5f) continue;

            const float hY = a.y;
            const float minX = (std::min)(a.x, b.x);
            const float maxX = (std::max)(a.x, b.x);

            // Only leave 15.5px on the port side that holds the Crow's Foot/UML symbol, 5.5px on free bends
            const bool portAtMin = ((s == 0 && std::abs(minX - re.x1) < 1.0f && re.dir1 > 0) ||
                                    (s + 2 == re.points.size() && std::abs(minX - re.x2) < 1.0f && re.dir2 > 0));
            const bool portAtMax = ((s == 0 && std::abs(maxX - re.x1) < 1.0f && re.dir1 < 0) ||
                                    (s + 2 == re.points.size() && std::abs(maxX - re.x2) < 1.0f && re.dir2 < 0));
            const float leftPad  = portAtMin ? 15.5f : 5.5f;
            const float rightPad = portAtMax ? 15.5f : 5.5f;
            const float validMinX = minX + leftPad;
            const float validMaxX = maxX - rightPad;
            if (validMaxX <= validMinX + 1.0f) continue;

            std::vector<float> crossXs;
            for (size_t ej = 0; ej < routed.size(); ++ej) {
                if (ei == ej) continue;
                const auto& other = routed[ej];
                for (size_t s2 = 0; s2 + 1 < other.points.size(); ++s2) {
                    const auto& c = other.points[s2];
                    const auto& d = other.points[s2 + 1];
                    if (std::abs(c.x - d.x) >= 0.5f) continue;
                    const float vX = c.x;
                    const float vMinY = (std::min)(c.y, d.y);
                    const float vMaxY = (std::max)(c.y, d.y);
                    if (vX > validMinX && vX < validMaxX && hY > vMinY + 3.5f && hY < vMaxY - 3.5f) {
                        crossXs.push_back(vX);
                    }
                }
            }
            if (crossXs.empty()) continue;
            std::sort(crossXs.begin(), crossXs.end());
            float lastAdded = -1e9f;
            for (float cx : crossXs) {
                if (cx - lastAdded >= 12.0f) {
                    re.jumps.push_back({ s, cx });
                    lastAdded = cx;
                }
            }
        }
    }

    return routed;
}

std::string ErdAndMigrationEngine::ExportErdToSvg(
    const std::vector<SqlTableInfo>& tables,
    const std::vector<ErdNodeLayout>& nodes,
    ErdNotation notation,
    bool inferImplicitFks) {

    if (nodes.empty()) return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"200\"></svg>";

    float minX = nodes[0].x, minY = nodes[0].y;
    float maxX = nodes[0].x + nodes[0].width, maxY = nodes[0].y + nodes[0].height;
    std::unordered_map<std::wstring, const SqlTableInfo*> tblMap;
    for (const auto& n : nodes) {
        minX = (std::min)(minX, n.x);
        minY = (std::min)(minY, n.y);
        maxX = (std::max)(maxX, n.x + n.width);
        maxY = (std::max)(maxY, n.y + n.height);
    }
    if (notation == ErdNotation::Chen) {
        minY -= 40.0f;
        maxY += 40.0f;
    }
    for (const auto& t : tables) tblMap[ToLowerW(t.name)] = &t;

    const float pad = (notation == ErdNotation::Chen) ? 72.0f : 56.0f;
    const float svgW = (std::max)(360.0f, (maxX - minX) + pad * 2.0f + 48.0f);
    const float svgH = (std::max)(260.0f, (maxY - minY) + pad * 2.0f + 24.0f);
    const float ox = pad - minX;
    const float oy = pad - minY;

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << static_cast<int>(svgW)
        << "\" height=\"" << static_cast<int>(svgH) << "\" viewBox=\"0 0 "
        << static_cast<int>(svgW) << " " << static_cast<int>(svgH) << "\">\n";
    svg << "  <defs>\n"
        << "    <pattern id=\"erdGrid\" width=\"32\" height=\"32\" patternUnits=\"userSpaceOnUse\">\n"
        << "      <path d=\"M 32 0 L 0 0 0 32\" fill=\"none\" stroke=\"#181D27\" stroke-width=\"0.8\" />\n"
        << "    </pattern>\n"
        << "  </defs>\n";
    svg << "  <rect width=\"100%\" height=\"100%\" fill=\"#0F1218\" />\n";
    svg << "  <rect width=\"100%\" height=\"100%\" fill=\"url(#erdGrid)\" />\n";

    // Draw obstacle-free orthogonal routed edges with jump arcs at crossings matching live Direct2D canvas
    const ErdSchemaInterpretation interp = InterpretSchema(tables, inferImplicitFks);
    const std::vector<ErdRoutedEdge> edges = RouteErdEdges(tables, nodes, interp, notation);

    for (const auto& edge : edges) {
        if (edge.points.size() < 2) continue;
        svg << "  <path d=\"M " << (edge.points[0].x + ox) << " " << (edge.points[0].y + oy);
        for (size_t s = 0; s + 1 < edge.points.size(); ++s) {
            const auto& a = edge.points[s];
            const auto& b = edge.points[s + 1];
            if (std::abs(a.y - b.y) < 0.5f) {
                std::vector<float> jxs;
                for (const auto& j : edge.jumps) {
                    if (j.segmentIndex == s) jxs.push_back(j.crossX);
                }
                if (!jxs.empty()) {
                    const float ySvg = a.y + oy;
                    const float r = 5.0f;
                    if (b.x >= a.x) {
                        std::sort(jxs.begin(), jxs.end());
                        for (float jx : jxs) {
                            svg << " L " << (jx - r + ox) << " " << ySvg
                                << " A " << r << " " << r << " 0 0 1 " << (jx + r + ox) << " " << ySvg;
                        }
                    } else {
                        std::sort(jxs.rbegin(), jxs.rend());
                        for (float jx : jxs) {
                            svg << " L " << (jx + r + ox) << " " << ySvg
                                << " A " << r << " " << r << " 0 0 0 " << (jx - r + ox) << " " << ySvg;
                        }
                    }
                }
            }
            svg << " L " << (b.x + ox) << " " << (b.y + oy);
        }
        svg << "\" fill=\"none\" stroke=\"" << (edge.isInferred ? "#94A3B8" : "#38BDF8") << "\" stroke-width=\"1.55\" ";
        if (notation == ErdNotation::Barker || edge.isInferred) svg << "stroke-dasharray=\"5,3\" ";
        svg << "/>\n";

        const float sx1 = edge.x1 + ox, sy1 = edge.y1 + oy;
        const float sx2 = edge.x2 + ox, sy2 = edge.y2 + oy;
        const float smx = edge.midPoint.x + ox, smy = edge.midPoint.y + oy;

        if (notation == ErdNotation::Chen) {
            svg << "  <polygon points=\"" << smx << "," << (smy - 17) << " "
                << (smx + 34) << "," << smy << " " << smx << "," << (smy + 17) << " "
                << (smx - 34) << "," << smy
                << "\" fill=\"#1E293B\" stroke=\"#38BDF8\" stroke-width=\"1.5\" />\n";
            svg << "  <text x=\"" << smx << "\" y=\"" << (smy + 4.0f)
                << "\" text-anchor=\"middle\" fill=\"#F1F5F9\" font-family=\"Segoe UI\" font-size=\"11\">"
                << EscapeXml(edge.relationLabel.empty() ? L"has" : edge.relationLabel) << "</text>\n";
            svg << "  <text x=\"" << (sx1 + edge.dir1 * 12.0f) << "\" y=\"" << (sy1 - 6.0f)
                << "\" text-anchor=\"middle\" fill=\"#E2E8F0\" font-family=\"Segoe UI\" font-size=\"11\">1</text>\n";
            svg << "  <text x=\"" << (sx2 + edge.dir2 * 12.0f) << "\" y=\"" << (sy2 - 6.0f)
                << "\" text-anchor=\"middle\" fill=\"#E2E8F0\" font-family=\"Segoe UI\" font-size=\"11\">"
                << (edge.isOneToOne ? "1" : "N") << "</text>\n";
        } else if (notation == ErdNotation::Uml) {
            svg << "  <polygon points=\"" << sx1 << "," << sy1 << " "
                << (sx1 + edge.dir1 * 7.0f) << "," << (sy1 - 5.0f) << " "
                << (sx1 + edge.dir1 * 14.0f) << "," << sy1 << " "
                << (sx1 + edge.dir1 * 7.0f) << "," << (sy1 + 5.0f)
                << "\" fill=\"" << (edge.childOptional ? "#0F1218" : "#38BDF8") << "\" stroke=\"#38BDF8\" stroke-width=\"1.4\" />\n";
            svg << "  <text x=\"" << (sx1 + edge.dir1 * 22.0f) << "\" y=\"" << (sy1 - 7.0f)
                << "\" text-anchor=\"middle\" fill=\"#E2E8F0\" font-family=\"Segoe UI\" font-size=\"11\">"
                << (edge.childOptional ? "0..1" : "1") << "</text>\n";
            svg << "  <text x=\"" << (sx2 + edge.dir2 * 18.0f) << "\" y=\"" << (sy2 - 7.0f)
                << "\" text-anchor=\"middle\" fill=\"#E2E8F0\" font-family=\"Segoe UI\" font-size=\"11\">"
                << (edge.isOneToOne ? "0..1" : (edge.childOptional ? "0..*" : "1..*")) << "</text>\n";
        } else {
            // Crow's Foot / Barker endpoints (circles, double bars, forks)
            auto drawSvgMarker = [&](float px, float py, float dir, bool isMany, bool isOpt) {
                const float mx = px + dir * 13.0f;
                if (isOpt) {
                    svg << "  <circle cx=\"" << mx << "\" cy=\"" << py
                        << "\" r=\"4\" fill=\"#0F1218\" stroke=\"#38BDF8\" stroke-width=\"1.55\" />\n";
                } else {
                    svg << "  <line x1=\"" << mx << "\" y1=\"" << (py - 5.5f) << "\" x2=\"" << mx
                        << "\" y2=\"" << (py + 5.5f) << "\" stroke=\"#38BDF8\" stroke-width=\"1.55\" />\n";
                }
                if (isMany) {
                    const float fb = px + dir * 8.5f;
                    svg << "  <line x1=\"" << fb << "\" y1=\"" << py << "\" x2=\"" << px << "\" y2=\"" << (py - 6.0f)
                        << "\" stroke=\"#38BDF8\" stroke-width=\"1.55\" />\n";
                    svg << "  <line x1=\"" << fb << "\" y1=\"" << py << "\" x2=\"" << px << "\" y2=\"" << (py + 6.0f)
                        << "\" stroke=\"#38BDF8\" stroke-width=\"1.55\" />\n";
                } else {
                    const float bx = px + dir * 5.0f;
                    svg << "  <line x1=\"" << bx << "\" y1=\"" << (py - 5.5f) << "\" x2=\"" << bx
                        << "\" y2=\"" << (py + 5.5f) << "\" stroke=\"#38BDF8\" stroke-width=\"1.55\" />\n";
                }
            };
            if (notation == ErdNotation::Barker) {
                if (!edge.isOneToOne) {
                    const float fb = sx2 + edge.dir2 * 10.0f;
                    svg << "  <line x1=\"" << fb << "\" y1=\"" << sy2 << "\" x2=\"" << sx2 << "\" y2=\"" << (sy2 - 6.0f)
                        << "\" stroke=\"#38BDF8\" stroke-width=\"1.55\" />\n";
                    svg << "  <line x1=\"" << fb << "\" y1=\"" << sy2 << "\" x2=\"" << sx2 << "\" y2=\"" << (sy2 + 6.0f)
                        << "\" stroke=\"#38BDF8\" stroke-width=\"1.55\" />\n";
                }
            } else {
                drawSvgMarker(sx1, sy1, edge.dir1, false, edge.childOptional);
                drawSvgMarker(sx2, sy2, edge.dir2, !edge.isOneToOne, !edge.isOneToOne && edge.childOptional);
            }
        }
    }

    // Draw Entity Nodes matching live Direct2D card design (#181C24 body, #212836 header, two-column PK/FK + compact types)
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        const auto& n = nodes[ni];
        auto tIt = tblMap.find(ToLowerW(n.tableName));
        if (tIt == tblMap.end()) continue;
        const SqlTableInfo& tbl = *tIt->second;
        const float x = n.x + ox;
        const float y = n.y + oy;
        const float rx = (notation == ErdNotation::Barker) ? 10.0f : ((notation == ErdNotation::Chen || notation == ErdNotation::Uml) ? 1.5f : 6.0f);
        const float hdrH = (notation == ErdNotation::Uml) ? 40.0f : 31.0f;

        if (notation == ErdNotation::Chen && !tbl.columns.empty()) {
            const size_t maxAttrs = (std::min)(tbl.columns.size(), size_t(4));
            for (size_t ai = 0; ai < maxAttrs; ++ai) {
                const bool topRow = (ai % 2 == 0);
                const bool leftSlot = (ai < 2);
                const float ax = x + n.width * (leftSlot ? 0.28f : 0.72f);
                const float ay = topRow ? (y - 22.0f) : (y + n.height + 22.0f);
                const float stemY = topRow ? y : (y + n.height);
                svg << "  <line x1=\"" << ax << "\" y1=\"" << stemY << "\" x2=\"" << ax << "\" y2=\"" << ay
                    << "\" stroke=\"#313A4B\" stroke-width=\"1.0\" />\n";
                svg << "  <ellipse cx=\"" << ax << "\" cy=\"" << ay << "\" rx=\"40\" ry=\"12\" fill=\"#181C24\" stroke=\""
                    << (tbl.columns[ai].isPrimaryKey ? "#FBBF24" : "#313A4B") << "\" stroke-width=\"1.1\" />\n";
                std::wstring aLbl = tbl.columns[ai].name;
                if (aLbl.size() > 11) aLbl = aLbl.substr(0, 9) + L"..";
                svg << "  <text x=\"" << ax << "\" y=\"" << (ay + 3.5f)
                    << "\" text-anchor=\"middle\" fill=\"#94A3B8\" font-family=\"Consolas\" font-size=\"10.5\">"
                    << EscapeXml(aLbl) << "</text>\n";
            }
        }

        const std::wstring roleBadge = (ni < interp.entities.size()) ? interp.entities[ni].badgeLabel : L"";
        svg << "  <rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << n.width << "\" height=\"" << n.height
            << "\" rx=\"" << rx << "\" fill=\"#181C24\" stroke=\"#313A4B\" stroke-width=\"1.2\" />\n";
        svg << "  <rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << n.width << "\" height=\"" << hdrH
            << "\" rx=\"" << rx << "\" fill=\"#212836\" />\n";
        svg << "  <line x1=\"" << x << "\" y1=\"" << (y + hdrH) << "\" x2=\"" << (x + n.width)
            << "\" y2=\"" << (y + hdrH) << "\" stroke=\"#313A4B\" stroke-width=\"1.0\" />\n";

        if (notation == ErdNotation::Uml) {
            const std::wstring stereo = (ni < interp.entities.size() && interp.entities[ni].role == ErdEntityRole::Junction)
                ? L"<<junction>>" : L"<<entity>>";
            svg << "  <text x=\"" << (x + n.width * 0.5f) << "\" y=\"" << (y + 13.0f)
                << "\" text-anchor=\"middle\" fill=\"#94A3B8\" font-family=\"Segoe UI\" font-size=\"10.5\">"
                << EscapeXml(stereo) << "</text>\n";
            svg << "  <text x=\"" << (x + n.width * 0.5f) << "\" y=\"" << (y + 31.0f)
                << "\" text-anchor=\"middle\" fill=\"#E2E8F0\" font-family=\"Segoe UI\" font-size=\"12.5\" font-weight=\"600\">"
                << EscapeXml(tbl.name) << "</text>\n";
        } else {
            const size_t maxTitleChars = static_cast<size_t>((n.width - (roleBadge.empty() ? 24.0f : 72.0f)) / 6.7f);
            std::wstring titleDisp = tbl.name;
            if (titleDisp.size() > maxTitleChars && maxTitleChars > 4) {
                titleDisp = titleDisp.substr(0, maxTitleChars - 3) + L"...";
            }
            svg << "  <text x=\"" << (x + 12.0f) << "\" y=\"" << (y + 20.0f)
                << "\" fill=\"#E2E8F0\" font-family=\"Segoe UI\" font-size=\"12.5\" font-weight=\"600\">"
                << EscapeXml(titleDisp) << "</text>\n";
            if (!roleBadge.empty()) {
                const float bx = x + n.width - 48.0f, by = y + 7.0f;
                svg << "  <rect x=\"" << bx << "\" y=\"" << by << "\" width=\"38\" height=\"17\" rx=\"4\" fill=\"#1E293B\" stroke=\"#38BDF8\" stroke-width=\"0.9\" />\n";
                svg << "  <text x=\"" << (bx + 19.0f) << "\" y=\"" << (by + 12.0f)
                    << "\" text-anchor=\"middle\" fill=\"#38BDF8\" font-family=\"Segoe UI\" font-size=\"10\" font-weight=\"600\">"
                    << EscapeXml(roleBadge) << "</text>\n";
            }
        }

        const size_t maxVis = (std::min)(tbl.columns.size(), size_t(14));
        float cy = y + hdrH + 19.0f;
        for (size_t ci = 0; ci < maxVis; ++ci) {
            const auto& col = tbl.columns[ci];
            bool isFk = IsFkColumn(tbl, col.name);
            std::wstring badge;
            if (notation == ErdNotation::Uml) badge = col.isPrimaryKey ? L"+" : (isFk ? L"#" : L"-");
            else if (notation == ErdNotation::Barker) badge = col.isPrimaryKey ? L"#" : (col.notNull ? L"*" : L"o");
            else badge = col.isPrimaryKey ? L"PK" : (isFk ? L"FK" : L"");

            if (!badge.empty()) {
                svg << "  <text x=\"" << (x + 10.0f) << "\" y=\"" << cy
                    << "\" fill=\"" << (col.isPrimaryKey ? "#FBBF24" : (isFk ? "#38BDF8" : "#94A3B8"))
                    << "\" font-family=\"Consolas\" font-size=\"11.5\" font-weight=\"600\">"
                    << EscapeXml(badge) << "</text>\n";
            }
            const std::wstring compactT = FormatCompactSvgType(col.type);
            const size_t maxNameChars = static_cast<size_t>((std::max)(8.0f, (n.width - 48.0f - static_cast<float>(compactT.size()) * 7.2f) / 7.2f));
            std::wstring cName = col.name;
            if (cName.size() > maxNameChars && maxNameChars > 4) cName = cName.substr(0, maxNameChars - 3) + L"...";
            svg << "  <text x=\"" << (x + 35.0f) << "\" y=\"" << cy
                << "\" fill=\"#E2E8F0\" font-family=\"Consolas\" font-size=\"12\">"
                << EscapeXml(cName) << "</text>\n";
            svg << "  <text x=\"" << (x + n.width - 10.0f) << "\" y=\"" << cy
                << "\" text-anchor=\"end\" fill=\"#94A3B8\" font-family=\"Consolas\" font-size=\"11.5\">"
                << EscapeXml(compactT) << "</text>\n";
            cy += 20.0f;
        }
        if (tbl.columns.size() > maxVis) {
            svg << "  <text x=\"" << (x + 12.0f) << "\" y=\"" << cy
                << "\" fill=\"#94A3B8\" font-family=\"Segoe UI\" font-size=\"11\">... +"
                << (tbl.columns.size() - maxVis) << " more</text>\n";
        }
    }
    svg << "</svg>\n";
    return svg.str();
}

} // namespace phide::db
