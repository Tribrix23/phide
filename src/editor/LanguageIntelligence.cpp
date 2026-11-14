/**
 * @file LanguageIntelligence.cpp
 * @brief Implementation of Auto-Import & Organize Imports (#21), Rich Hover & Signature Help (#22),
 *        External Linter Aggregation & Problems (#23), Code Actions & Refactorings (#24),
 *        and Inlay Hints & CodeLens (#25).
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "phide/editor/LanguageIntelligence.hpp"
#include "phide/editor/WebIntelliSenseData.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace phide::editor {

namespace {

[[nodiscard]] std::wstring ToLowerW(std::wstring s) {
    for (wchar_t& ch : s) ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}

[[nodiscard]] std::wstring TrimW(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

[[nodiscard]] bool IsIdentCharW(wchar_t c) noexcept {
    return std::iswalnum(c) || c == L'_' || c == L'$';
}

[[nodiscard]] bool IsIdentStartW(wchar_t c) noexcept {
    return std::iswalpha(c) || c == L'_' || c == L'$';
}

[[nodiscard]] std::wstring GetExtLower(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    return ToLowerW(path.substr(dot));
}

[[nodiscard]] bool IsJsTsExt(const std::wstring& ext) noexcept {
    return ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" ||
           ext == L".mjs" || ext == L".cjs" || ext == L".mts" || ext == L".cts";
}

[[nodiscard]] bool IsCppExt(const std::wstring& ext) noexcept {
    return ext == L".c" || ext == L".cpp" || ext == L".cc" || ext == L".cxx" ||
           ext == L".h" || ext == L".hpp" || ext == L".hh" || ext == L".hxx";
}

[[nodiscard]] std::wstring DetectLineEnding(const std::wstring& content) {
    return (content.find(L"\r\n") != std::wstring::npos) ? L"\r\n" : L"\n";
}

[[nodiscard]] std::vector<std::wstring> SplitLinesPreserve(const std::wstring& content, bool* outEndedWithNewline = nullptr) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (size_t i = 0; i < content.size(); ++i) {
        wchar_t ch = content[i];
        if (ch == L'\r') {
            if (i + 1 < content.size() && content[i + 1] == L'\n') ++i;
            lines.push_back(std::move(cur));
            cur.clear();
        } else if (ch == L'\n') {
            lines.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    bool trailingNl = !content.empty() && content.back() == L'\n';
    if (outEndedWithNewline) *outEndedWithNewline = trailingNl;
    if (!trailingNl || lines.empty()) {
        lines.push_back(std::move(cur));
    }
    return lines;
}

[[nodiscard]] std::wstring JoinLinesWithEnding(const std::vector<std::wstring>& lines,
                                               const std::wstring& nl,
                                               bool trailingNewline) {
    std::wstring out;
    size_t est = lines.size() * 32;
    out.reserve(est);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out += nl;
        out += lines[i];
    }
    if (trailingNewline && !lines.empty()) out += nl;
    return out;
}

[[nodiscard]] std::unordered_set<std::wstring> CollectIdentifiersInLines(
    const std::vector<std::wstring>& lines,
    const std::unordered_set<size_t>& skipLineIndices) {
    std::unordered_set<std::wstring> used;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (skipLineIndices.count(i)) continue;
        const std::wstring& line = lines[i];
        size_t pos = 0;
        while (pos < line.size()) {
            if (IsIdentStartW(line[pos])) {
                size_t start = pos++;
                while (pos < line.size() && IsIdentCharW(line[pos])) ++pos;
                used.insert(line.substr(start, pos - start));
            } else {
                ++pos;
            }
        }
    }
    return used;
}

[[nodiscard]] std::wstring Utf8ToWideStr(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}

[[nodiscard]] std::string WideToUtf8Str(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string u(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), u.data(), n, nullptr, nullptr);
    return u;
}

[[nodiscard]] bool ReadUtf8FileBounded(const fs::path& p, std::wstring& outContent, size_t maxBytes = 256 * 1024) {
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    if (ec || sz > maxBytes) return false;
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::string raw(static_cast<size_t>(sz), '\0');
    ifs.read(raw.data(), static_cast<std::streamsize>(sz));
    raw.resize(static_cast<size_t>(ifs.gcount()));
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    outContent = Utf8ToWideStr(raw);
    return true;
}

[[nodiscard]] bool StartsWithNoCase(const std::wstring& haystack, const std::wstring& prefix) {
    if (haystack.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::towlower(haystack[i]) != std::towlower(prefix[i])) return false;
    }
    return true;
}

[[nodiscard]] std::wstring ComputeRelativeImportPath(const std::wstring& fromFilePath, const std::wstring& targetFilePath, const std::wstring& targetExt) {
    std::error_code ec;
    fs::path fromDir = fs::path(fromFilePath).parent_path();
    fs::path rel = fs::path(targetFilePath).lexically_relative(fromDir);
    std::wstring relW = rel.wstring();
    std::replace(relW.begin(), relW.end(), L'\\', L'/');
    if (IsJsTsExt(targetExt)) {
        size_t dot = relW.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > relW.find_last_of(L'/')) {
            // Also strip .d in .d.ts
            std::wstring sub = relW.substr(0, dot);
            if (sub.size() >= 2 && sub.substr(sub.size() - 2) == L".d") {
                sub.erase(sub.size() - 2);
            }
            relW = std::move(sub);
        }
        if (relW.rfind(L"/", 0) != 0 && relW.rfind(L"./", 0) != 0 && relW.rfind(L"../", 0) != 0) {
            relW = L"./" + relW;
        }
    }
    return relW;
}

[[nodiscard]] int CountWholeWordOccurrences(const std::wstring& text, const std::wstring& symbol) {
    if (symbol.empty() || text.empty()) return 0;
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(symbol, pos)) != std::wstring::npos) {
        bool leftOk = (pos == 0) || !IsIdentCharW(text[pos - 1]);
        size_t after = pos + symbol.size();
        bool rightOk = (after >= text.size()) || !IsIdentCharW(text[after]);
        if (leftOk && rightOk) ++count;
        pos = after;
    }
    return count;
}

// ============================================================================
// Internal JS/TS Import Organization
// ============================================================================

struct JsImportSpec {
    std::wstring importedName; // e.g. "useState" or "Button" or "* as fs"
    std::wstring localAlias;   // e.g. "us" if "useState as us", else same as importedName
};

struct JsImportRecord {
    int group = 1; // 1=external/node, 2=alias (@/,~/), 3=relative (./,../), 4=style/side-effect
    bool isTypeOnly = false;
    bool isSideEffect = false;
    std::wstring moduleSpec;
    std::wstring quoteChar = L"'";
    std::wstring defaultBinding;
    std::wstring namespaceBinding; // without "* as "
    std::vector<JsImportSpec> namedSpecs;
};

int ClassifyJsModuleGroup(const std::wstring& spec, bool isSideEffect) {
    std::wstring low = ToLowerW(spec);
    if (isSideEffect || low.rfind(L".css") != std::wstring::npos || low.rfind(L".scss") != std::wstring::npos ||
        low.rfind(L".less") != std::wstring::npos || low.rfind(L".sass") != std::wstring::npos) {
        return 4;
    }
    if (spec.rfind(L"./", 0) == 0 || spec.rfind(L"../", 0) == 0 || spec == L"." || spec == L"..") {
        return 3;
    }
    if (spec.rfind(L"@/", 0) == 0 || spec.rfind(L"~/", 0) == 0 || spec.rfind(L"#", 0) == 0) {
        return 2;
    }
    return 1;
}

bool ParseJsImportStatement(const std::wstring& stmt, JsImportRecord& out) {
    std::wstring t = TrimW(stmt);
    if (t.rfind(L"import ", 0) != 0 && t.rfind(L"import\t", 0) != 0) return false;
    if (!t.empty() && t.back() == L';') t.pop_back();
    t = TrimW(t);

    std::wstring rest = TrimW(t.substr(6));
    if (rest.empty()) return false;

    // Side-effect import: import './foo.css'
    if (rest[0] == L'\'' || rest[0] == L'"' || rest[0] == L'`') {
        wchar_t q = rest[0];
        size_t q2 = rest.find(q, 1);
        if (q2 == std::wstring::npos) return false;
        out.isSideEffect = true;
        out.quoteChar = std::wstring(1, q);
        out.moduleSpec = rest.substr(1, q2 - 1);
        out.group = 4;
        return true;
    }

    if (rest.rfind(L"type ", 0) == 0) {
        out.isTypeOnly = true;
        rest = TrimW(rest.substr(5));
    }

    // Locate trailing `from 'module'`
    size_t q2 = rest.find_last_of(L"'\"`");
    if (q2 == std::wstring::npos || q2 == 0) return false;
    wchar_t q = rest[q2];
    size_t q1 = rest.rfind(q, q2 - 1);
    if (q1 == std::wstring::npos) return false;
    out.quoteChar = std::wstring(1, q);
    out.moduleSpec = rest.substr(q1 + 1, q2 - q1 - 1);

    std::wstring beforeModule = TrimW(rest.substr(0, q1));
    if (beforeModule.size() < 4 || beforeModule.substr(beforeModule.size() - 4) != L"from") return false;
    std::wstring clause = TrimW(beforeModule.substr(0, beforeModule.size() - 4));
    if (clause.empty()) return false;

    // Extract { a, b as c } if present
    size_t lBrace = clause.find(L'{');
    size_t rBrace = clause.find(L'}');
    std::wstring headPart = clause;
    if (lBrace != std::wstring::npos && rBrace != std::wstring::npos && rBrace > lBrace) {
        std::wstring inside = clause.substr(lBrace + 1, rBrace - lBrace - 1);
        headPart = TrimW(clause.substr(0, lBrace));
        if (!headPart.empty() && headPart.back() == L',') headPart.pop_back();
        headPart = TrimW(headPart);

        std::wstringstream ss(inside);
        std::wstring part;
        while (std::getline(ss, part, L',')) {
            part = TrimW(part);
            if (part.empty()) continue;
            bool itemType = false;
            if (part.rfind(L"type ", 0) == 0) {
                itemType = true;
                part = TrimW(part.substr(5));
            }
            size_t asPos = part.find(L" as ");
            JsImportSpec sp;
            if (asPos != std::wstring::npos) {
                sp.importedName = (itemType ? L"type " : L"") + TrimW(part.substr(0, asPos));
                sp.localAlias = TrimW(part.substr(asPos + 4));
            } else {
                sp.importedName = (itemType ? L"type " : L"") + part;
                sp.localAlias = part;
            }
            out.namedSpecs.push_back(std::move(sp));
        }
    }

    // Namespace or default binding in headPart
    if (!headPart.empty()) {
        size_t comma = headPart.find(L',');
        if (comma != std::wstring::npos) {
            out.defaultBinding = TrimW(headPart.substr(0, comma));
            std::wstring nsPart = TrimW(headPart.substr(comma + 1));
            if (nsPart.rfind(L"*", 0) == 0) {
                size_t asPos = nsPart.find(L" as ");
                if (asPos != std::wstring::npos) out.namespaceBinding = TrimW(nsPart.substr(asPos + 4));
            }
        } else if (headPart[0] == L'*') {
            size_t asPos = headPart.find(L" as ");
            if (asPos != std::wstring::npos) out.namespaceBinding = TrimW(headPart.substr(asPos + 4));
        } else {
            out.defaultBinding = headPart;
        }
    }

    out.group = ClassifyJsModuleGroup(out.moduleSpec, false);
    return !out.moduleSpec.empty();
}

std::wstring FormatJsImportStatement(const JsImportRecord& rec) {
    if (rec.isSideEffect) {
        return L"import " + rec.quoteChar + rec.moduleSpec + rec.quoteChar + L";";
    }
    std::wstring s = rec.isTypeOnly ? L"import type " : L"import ";
    bool needComma = false;
    if (!rec.defaultBinding.empty()) {
        s += rec.defaultBinding;
        needComma = true;
    }
    if (!rec.namespaceBinding.empty()) {
        if (needComma) s += L", ";
        s += L"* as " + rec.namespaceBinding;
        needComma = true;
    }
    if (!rec.namedSpecs.empty()) {
        if (needComma) s += L", ";
        s += L"{ ";
        for (size_t i = 0; i < rec.namedSpecs.size(); ++i) {
            if (i > 0) s += L", ";
            const auto& sp = rec.namedSpecs[i];
            std::wstring cleanImp = sp.importedName;
            std::wstring prefix;
            if (cleanImp.rfind(L"type ", 0) == 0) {
                prefix = L"type ";
                cleanImp = cleanImp.substr(5);
            }
            if (cleanImp == sp.localAlias) {
                s += prefix + cleanImp;
            } else {
                s += prefix + cleanImp + L" as " + sp.localAlias;
            }
        }
        s += L" }";
    }
    s += L" from " + rec.quoteChar + rec.moduleSpec + rec.quoteChar + L";";
    return s;
}

OrganizeImportsResult OrganizeJsTsImports(const std::wstring& ext, const std::wstring& content) {
    OrganizeImportsResult res;
    res.updatedText = content;
    bool endedNl = false;
    auto lines = SplitLinesPreserve(content, &endedNl);
    std::wstring nl = DetectLineEnding(content);

    // Collect top directives ('use client', 'use server', 'use strict') + leading comments before imports
    std::vector<std::wstring> preambleLines;
    std::vector<JsImportRecord> records;
    std::unordered_set<size_t> importLineIndices;

    size_t i = 0;
    while (i < lines.size()) {
        std::wstring t = TrimW(lines[i]);
        if (t.empty() || t.rfind(L"//", 0) == 0 || t.rfind(L"/*", 0) == 0 || t.rfind(L"*", 0) == 0) {
            preambleLines.push_back(lines[i]);
            ++i;
            continue;
        }
        if (t == L"'use client';" || t == L"\"use client\";" || t == L"'use client'" || t == L"\"use client\"" ||
            t == L"'use server';" || t == L"\"use server\";" || t == L"'use strict';" || t == L"\"use strict\";") {
            preambleLines.push_back(lines[i]);
            ++i;
            continue;
        }
        break;
    }

    // Collect import statements (supporting multi-line `{ ... }` imports)
    while (i < lines.size()) {
        std::wstring t = TrimW(lines[i]);
        if (t.empty()) {
            importLineIndices.insert(i);
            ++i;
            continue;
        }
        if (t.rfind(L"import ", 0) == 0 || t.rfind(L"import\t", 0) == 0) {
            std::wstring combined = t;
            importLineIndices.insert(i);
            size_t j = i;
            bool closedQuote = (combined.find(L'\'') != std::wstring::npos ||
                                combined.find(L'"') != std::wstring::npos ||
                                combined.find(L'`') != std::wstring::npos);
            while (combined.find(L';') == std::wstring::npos &&
                   (!closedQuote || (combined.find(L'{') != std::wstring::npos && combined.find(L'}') == std::wstring::npos)) &&
                   j + 1 < lines.size()) {
                ++j;
                importLineIndices.insert(j);
                combined += L" " + TrimW(lines[j]);
                if (combined.find(L"from ") != std::wstring::npos &&
                    combined.find_last_of(L"'\"`") > combined.find(L"from ")) {
                    break;
                }
            }
            JsImportRecord rec;
            if (ParseJsImportStatement(combined, rec)) {
                records.push_back(std::move(rec));
                i = j + 1;
                continue;
            } else {
                // Roll back skip marks if unparseable (e.g. dynamic import())
                for (size_t k = i; k <= j; ++k) importLineIndices.erase(k);
                break;
            }
        }
        break;
    }

    // Trim trailing blank lines created by import scanning when no imports found
    if (records.empty()) return res;

    // Collect rest of document starting at line `i`
    size_t bodyStartLine = i;
    while (bodyStartLine < lines.size() && TrimW(lines[bodyStartLine]).empty()) ++bodyStartLine;

    std::unordered_set<size_t> skipForUsage;
    for (size_t k = 0; k < bodyStartLine; ++k) skipForUsage.insert(k);
    auto usedIds = CollectIdentifiersInLines(lines, skipForUsage);
    bool hasJsxMarkup = false;
    if (ext == L".jsx" || ext == L".tsx") {
        for (size_t k = bodyStartLine; k < lines.size(); ++k) {
            if (lines[k].find(L'<') != std::wstring::npos) {
                hasJsxMarkup = true;
                break;
            }
        }
    }

    // Merge identical (group, isTypeOnly, moduleSpec) entries and prune unused bindings
    struct MergeKey {
        int group = 1;
        bool isType = false;
        std::wstring moduleSpec;
        bool operator<(const MergeKey& o) const {
            if (group != o.group) return group < o.group;
            if (isType != o.isType) return isType < o.isType;
            return moduleSpec < o.moduleSpec;
        }
    };
    std::map<MergeKey, JsImportRecord> merged;
    std::vector<JsImportRecord> sideEffects;
    int removedCount = 0;

    for (auto& rec : records) {
        if (rec.isSideEffect) {
            bool dup = false;
            for (const auto& s : sideEffects) {
                if (s.moduleSpec == rec.moduleSpec) { dup = true; break; }
            }
            if (!dup) sideEffects.push_back(rec);
            continue;
        }
        if (!rec.defaultBinding.empty()) {
            bool preserveReact = (rec.defaultBinding == L"React" && hasJsxMarkup);
            if (!preserveReact && !usedIds.count(rec.defaultBinding)) {
                rec.defaultBinding.clear();
                ++removedCount;
            }
        }
        if (!rec.namespaceBinding.empty()) {
            if (!usedIds.count(rec.namespaceBinding)) {
                rec.namespaceBinding.clear();
                ++removedCount;
            }
        }
        std::vector<JsImportSpec> keptNamed;
        for (const auto& sp : rec.namedSpecs) {
            if (usedIds.count(sp.localAlias)) {
                keptNamed.push_back(sp);
            } else {
                ++removedCount;
            }
        }
        rec.namedSpecs = std::move(keptNamed);
        if (rec.defaultBinding.empty() && rec.namespaceBinding.empty() && rec.namedSpecs.empty()) {
            continue;
        }
        MergeKey key{ rec.group, rec.isTypeOnly, rec.moduleSpec };
        auto it = merged.find(key);
        if (it == merged.end()) {
            merged.emplace(key, std::move(rec));
        } else {
            if (it->second.defaultBinding.empty()) it->second.defaultBinding = rec.defaultBinding;
            if (it->second.namespaceBinding.empty()) it->second.namespaceBinding = rec.namespaceBinding;
            for (const auto& sp : rec.namedSpecs) {
                bool exists = false;
                for (const auto& cur : it->second.namedSpecs) {
                    if (cur.localAlias == sp.localAlias && cur.importedName == sp.importedName) {
                        exists = true; break;
                    }
                }
                if (!exists) it->second.namedSpecs.push_back(sp);
            }
        }
    }

    // Reconstruct document
    std::vector<std::wstring> outLines;
    // Strip trailing blank lines in preamble before re-adding imports
    while (!preambleLines.empty() && TrimW(preambleLines.back()).empty()) preambleLines.pop_back();
    for (const auto& pl : preambleLines) outLines.push_back(pl);
    if (!preambleLines.empty() && (!merged.empty() || !sideEffects.empty())) {
        outLines.push_back(L"");
    }

    int lastGroup = -1;
    int totalKept = 0;
    for (auto& [k, rec] : merged) {
        std::sort(rec.namedSpecs.begin(), rec.namedSpecs.end(), [](const JsImportSpec& a, const JsImportSpec& b) {
            return ToLowerW(a.importedName) < ToLowerW(b.importedName);
        });
        if (lastGroup != -1 && k.group != lastGroup) {
            outLines.push_back(L"");
        }
        lastGroup = k.group;
        outLines.push_back(FormatJsImportStatement(rec));
        ++totalKept;
    }
    if (!sideEffects.empty()) {
        std::sort(sideEffects.begin(), sideEffects.end(), [](const JsImportRecord& a, const JsImportRecord& b) {
            return ToLowerW(a.moduleSpec) < ToLowerW(b.moduleSpec);
        });
        if (lastGroup != -1) outLines.push_back(L"");
        for (const auto& se : sideEffects) {
            outLines.push_back(FormatJsImportStatement(se));
            ++totalKept;
        }
    }

    if (bodyStartLine < lines.size()) {
        if (!outLines.empty()) outLines.push_back(L"");
        for (size_t k = bodyStartLine; k < lines.size(); ++k) {
            outLines.push_back(lines[k]);
        }
    }

    std::wstring nextText = JoinLinesWithEnding(outLines, nl, endedNl);
    res.changed = (nextText != content);
    res.updatedText = std::move(nextText);
    res.removedUnusedCount = removedCount;
    res.totalImportCount = totalKept;
    return res;
}

// ============================================================================
// Internal Python Import Organization (PEP 8 / isort)
// ============================================================================

bool IsPythonStdlibRoot(const std::wstring& modRoot) {
    static const std::unordered_set<std::wstring> kStdlib = {
        L"abc", L"argparse", L"asyncio", L"base64", L"bisect", L"builtins", L"collections",
        L"concurrent", L"contextlib", L"copy", L"csv", L"ctypes", L"dataclasses", L"datetime",
        L"decimal", L"enum", L"errno", L"fcntl", L"fnmatch", L"functools", L"gc", L"glob",
        L"gzip", L"hashlib", L"heapq", L"hmac", L"html", L"http", L"importlib", L"inspect",
        L"io", L"itertools", L"json", L"logging", L"math", L"multiprocessing", L"operator",
        L"os", L"pathlib", L"pickle", L"platform", L"pprint", L"queue", L"random", L"re",
        L"secrets", L"shlex", L"shutil", L"signal", L"socket", L"sqlite3", L"ssl", L"stat",
        L"statistics", L"string", L"struct", L"subprocess", L"sys", L"tempfile", L"textwrap",
        L"threading", L"time", L"timeit", L"traceback", L"types", L"typing", L"unicodedata",
        L"unittest", L"urllib", L"uuid", L"venv", L"warnings", L"weakref", L"xml", L"zipfile", L"zlib"
    };
    return kStdlib.count(modRoot) > 0;
}

OrganizeImportsResult OrganizePythonImports(const std::wstring& content) {
    OrganizeImportsResult res;
    res.updatedText = content;
    bool endedNl = false;
    auto lines = SplitLinesPreserve(content, &endedNl);
    std::wstring nl = DetectLineEnding(content);

    std::vector<std::wstring> preamble;
    size_t i = 0;
    // Shebang / encoding / leading comments / module docstring
    while (i < lines.size()) {
        std::wstring t = TrimW(lines[i]);
        if (t.empty() || t[0] == L'#') {
            preamble.push_back(lines[i]);
            ++i;
            continue;
        }
        if (t.rfind(L"\"\"\"", 0) == 0 || t.rfind(L"'''", 0) == 0) {
            std::wstring delim = t.substr(0, 3);
            preamble.push_back(lines[i]);
            if (t.size() >= 6 && t.rfind(delim) >= 3) {
                ++i;
                continue;
            }
            ++i;
            while (i < lines.size()) {
                preamble.push_back(lines[i]);
                if (lines[i].find(delim) != std::wstring::npos) { ++i; break; }
                ++i;
            }
            continue;
        }
        break;
    }

    struct PyImportItem {
        int group = 2; // 0=__future__, 1=stdlib, 2=third-party, 3=relative
        bool isFrom = false;
        std::wstring moduleName;
        std::vector<std::pair<std::wstring, std::wstring>> names; // (name, alias_or_name)
    };
    std::vector<PyImportItem> items;

    while (i < lines.size()) {
        std::wstring t = TrimW(lines[i]);
        if (t.empty()) { ++i; continue; }
        if (t.rfind(L"import ", 0) == 0) {
            std::wstring specStr = TrimW(t.substr(7));
            // Split comma-separated `import os, sys as s`
            std::wstringstream ss(specStr);
            std::wstring seg;
            while (std::getline(ss, seg, L',')) {
                seg = TrimW(seg);
                if (seg.empty()) continue;
                size_t asPos = seg.find(L" as ");
                std::wstring mod = (asPos != std::wstring::npos) ? TrimW(seg.substr(0, asPos)) : seg;
                std::wstring alias = (asPos != std::wstring::npos) ? TrimW(seg.substr(asPos + 4)) : mod;
                size_t dotPos = mod.find(L'.');
                std::wstring rootMod = (dotPos != std::wstring::npos) ? mod.substr(0, dotPos) : mod;
                std::wstring bindName = (asPos != std::wstring::npos) ? alias : rootMod;
                PyImportItem it;
                it.isFrom = false;
                it.moduleName = mod;
                it.group = IsPythonStdlibRoot(rootMod) ? 1 : 2;
                it.names.push_back({ mod, bindName });
                if (asPos != std::wstring::npos) {
                    it.names.back().first = mod + L" as " + alias;
                }
                items.push_back(std::move(it));
            }
            ++i;
            continue;
        }
        if (t.rfind(L"from ", 0) == 0) {
            std::wstring combined = t;
            size_t j = i;
            if (combined.find(L'(') != std::wstring::npos && combined.find(L')') == std::wstring::npos) {
                while (j + 1 < lines.size()) {
                    ++j;
                    combined += L" " + TrimW(lines[j]);
                    if (combined.find(L')') != std::wstring::npos) break;
                }
            }
            size_t impKw = combined.find(L" import ");
            if (impKw != std::wstring::npos) {
                std::wstring mod = TrimW(combined.substr(5, impKw - 5));
                std::wstring rhs = TrimW(combined.substr(impKw + 8));
                if (!rhs.empty() && rhs.front() == L'(' && rhs.back() == L')') {
                    rhs = TrimW(rhs.substr(1, rhs.size() - 2));
                }
                PyImportItem it;
                it.isFrom = true;
                it.moduleName = mod;
                if (mod == L"__future__") it.group = 0;
                else if (!mod.empty() && mod[0] == L'.') it.group = 3;
                else {
                    size_t dotPos = mod.find(L'.');
                    std::wstring rootMod = (dotPos != std::wstring::npos) ? mod.substr(0, dotPos) : mod;
                    it.group = IsPythonStdlibRoot(rootMod) ? 1 : 2;
                }
                std::wstringstream ss(rhs);
                std::wstring seg;
                while (std::getline(ss, seg, L',')) {
                    seg = TrimW(seg);
                    if (seg.empty()) continue;
                    size_t asPos = seg.find(L" as ");
                    std::wstring alias = (asPos != std::wstring::npos) ? TrimW(seg.substr(asPos + 4)) : seg;
                    it.names.push_back({ seg, alias });
                }
                items.push_back(std::move(it));
                i = j + 1;
                continue;
            }
        }
        break;
    }

    if (items.empty()) return res;

    size_t bodyStart = i;
    while (bodyStart < lines.size() && TrimW(lines[bodyStart]).empty()) ++bodyStart;
    std::unordered_set<size_t> skip;
    for (size_t k = 0; k < bodyStart; ++k) skip.insert(k);
    auto usedIds = CollectIdentifiersInLines(lines, skip);

    int removed = 0;
    struct Key {
        int group;
        bool isFrom;
        std::wstring mod;
        bool operator<(const Key& o) const {
            if (group != o.group) return group < o.group;
            if (isFrom != o.isFrom) return isFrom < o.isFrom;
            return mod < o.mod;
        }
    };
    std::map<Key, std::vector<std::wstring>> groupedFrom;
    std::map<Key, std::wstring> plainImports;

    for (const auto& it : items) {
        if (!it.isFrom) {
            const auto& p = it.names.front();
            if (usedIds.count(p.second)) {
                plainImports[{ it.group, false, it.moduleName }] = L"import " + p.first;
            } else {
                ++removed;
            }
        } else {
            Key k{ it.group, true, it.moduleName };
            for (const auto& np : it.names) {
                if (np.first == L"*" || usedIds.count(np.second)) {
                    auto& vec = groupedFrom[k];
                    if (std::find(vec.begin(), vec.end(), np.first) == vec.end()) {
                        vec.push_back(np.first);
                    }
                } else {
                    ++removed;
                }
            }
        }
    }

    struct FormattedPyLine {
        int group;
        bool isFrom;
        std::wstring mod;
        std::wstring text;
    };
    std::vector<FormattedPyLine> formatted;
    for (const auto& [k, lineStr] : plainImports) {
        formatted.push_back({ k.group, false, k.mod, lineStr });
    }
    for (auto& [k, vec] : groupedFrom) {
        if (vec.empty()) continue;
        std::sort(vec.begin(), vec.end());
        std::wstring lineStr = L"from " + k.mod + L" import ";
        for (size_t x = 0; x < vec.size(); ++x) {
            if (x > 0) lineStr += L", ";
            lineStr += vec[x];
        }
        formatted.push_back({ k.group, true, k.mod, std::move(lineStr) });
    }
    std::sort(formatted.begin(), formatted.end(), [](const FormattedPyLine& a, const FormattedPyLine& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.isFrom != b.isFrom) return a.isFrom < b.isFrom;
        return ToLowerW(a.mod) < ToLowerW(b.mod);
    });

    std::vector<std::wstring> outLines;
    while (!preamble.empty() && TrimW(preamble.back()).empty()) preamble.pop_back();
    for (const auto& pl : preamble) outLines.push_back(pl);
    if (!preamble.empty() && !formatted.empty()) outLines.push_back(L"");

    int lastGrp = -1;
    for (const auto& f : formatted) {
        if (lastGrp != -1 && f.group != lastGrp) outLines.push_back(L"");
        lastGrp = f.group;
        outLines.push_back(f.text);
    }
    if (bodyStart < lines.size()) {
        if (!outLines.empty()) outLines.push_back(L"");
        for (size_t k = bodyStart; k < lines.size(); ++k) outLines.push_back(lines[k]);
    }

    std::wstring nextText = JoinLinesWithEnding(outLines, nl, endedNl);
    res.changed = (nextText != content);
    res.updatedText = std::move(nextText);
    res.removedUnusedCount = removed;
    res.totalImportCount = static_cast<int>(formatted.size());
    return res;
}

OrganizeImportsResult OrganizePhpUses(const std::wstring& content) {
    OrganizeImportsResult res;
    res.updatedText = content;
    bool endedNl = false;
    auto lines = SplitLinesPreserve(content, &endedNl);
    std::wstring nl = DetectLineEnding(content);

    std::vector<std::wstring> preamble;
    size_t i = 0;
    while (i < lines.size()) {
        std::wstring t = TrimW(lines[i]);
        if (t.empty() || t.rfind(L"<?", 0) == 0 || t.rfind(L"declare(", 0) == 0 ||
            t.rfind(L"namespace ", 0) == 0 || t.rfind(L"//", 0) == 0 || t.rfind(L"/*", 0) == 0 || t.rfind(L"*", 0) == 0) {
            preamble.push_back(lines[i]);
            ++i;
            continue;
        }
        break;
    }

    struct PhpUse {
        int group = 0; // 0=class, 1=function, 2=const
        std::wstring rawLine;
        std::wstring shortName;
    };
    std::vector<PhpUse> uses;
    while (i < lines.size()) {
        std::wstring t = TrimW(lines[i]);
        if (t.empty()) { ++i; continue; }
        if (t.rfind(L"use ", 0) == 0 && t.find(L'(') == std::wstring::npos && t.back() == L';') {
            PhpUse u;
            std::wstring body = TrimW(t.substr(4, t.size() - 5));
            if (body.rfind(L"function ", 0) == 0) { u.group = 1; body = TrimW(body.substr(9)); }
            else if (body.rfind(L"const ", 0) == 0) { u.group = 2; body = TrimW(body.substr(6)); }
            size_t asPos = body.find(L" as ");
            if (asPos != std::wstring::npos) {
                u.shortName = TrimW(body.substr(asPos + 4));
            } else {
                size_t slash = body.find_last_of(L'\\');
                u.shortName = (slash != std::wstring::npos) ? body.substr(slash + 1) : body;
            }
            u.rawLine = t;
            uses.push_back(std::move(u));
            ++i;
            continue;
        }
        break;
    }

    if (uses.empty()) return res;

    size_t bodyStart = i;
    while (bodyStart < lines.size() && TrimW(lines[bodyStart]).empty()) ++bodyStart;
    std::unordered_set<size_t> skip;
    for (size_t k = 0; k < bodyStart; ++k) skip.insert(k);
    auto usedIds = CollectIdentifiersInLines(lines, skip);

    int removed = 0;
    std::vector<PhpUse> kept;
    std::unordered_set<std::wstring> seen;
    for (const auto& u : uses) {
        if (!usedIds.count(u.shortName)) {
            ++removed;
            continue;
        }
        if (seen.insert(u.rawLine).second) kept.push_back(u);
    }
    std::sort(kept.begin(), kept.end(), [](const PhpUse& a, const PhpUse& b) {
        if (a.group != b.group) return a.group < b.group;
        return ToLowerW(a.rawLine) < ToLowerW(b.rawLine);
    });

    std::vector<std::wstring> outLines;
    while (!preamble.empty() && TrimW(preamble.back()).empty()) preamble.pop_back();
    for (const auto& pl : preamble) outLines.push_back(pl);
    if (!preamble.empty() && !kept.empty()) outLines.push_back(L"");
    int lastGrp = -1;
    for (const auto& u : kept) {
        if (lastGrp != -1 && u.group != lastGrp) outLines.push_back(L"");
        lastGrp = u.group;
        outLines.push_back(u.rawLine);
    }
    if (bodyStart < lines.size()) {
        if (!outLines.empty()) outLines.push_back(L"");
        for (size_t k = bodyStart; k < lines.size(); ++k) outLines.push_back(lines[k]);
    }
    std::wstring nextText = JoinLinesWithEnding(outLines, nl, endedNl);
    res.changed = (nextText != content);
    res.updatedText = std::move(nextText);
    res.removedUnusedCount = removed;
    res.totalImportCount = static_cast<int>(kept.size());
    return res;
}

OrganizeImportsResult OrganizeCppIncludes(const std::wstring& filePath, const std::wstring& content) {
    OrganizeImportsResult res;
    res.updatedText = content;
    bool endedNl = false;
    auto lines = SplitLinesPreserve(content, &endedNl);
    std::wstring nl = DetectLineEnding(content);

    std::wstring stem = ToLowerW(fs::path(filePath).stem().wstring());

    std::vector<std::wstring> preamble;
    size_t i = 0;
    while (i < lines.size()) {
        std::wstring t = TrimW(lines[i]);
        if (t.empty() || t.rfind(L"//", 0) == 0 || t.rfind(L"/*", 0) == 0 || t.rfind(L"*", 0) == 0 ||
            t == L"#pragma once") {
            preamble.push_back(lines[i]);
            ++i;
            continue;
        }
        break;
    }

    struct CppInc {
        int group = 1; // 0=matching header, 1=quoted project, 2=angle third-party, 3=angle std
        std::wstring line;
        std::wstring headerPath;
    };
    std::vector<CppInc> incs;
    std::unordered_set<std::wstring> seen;
    while (i < lines.size()) {
        std::wstring t = TrimW(lines[i]);
        if (t.empty()) { ++i; continue; }
        if (t.rfind(L"#include", 0) == 0) {
            std::wstring after = TrimW(t.substr(8));
            CppInc inc;
            inc.line = t;
            if (!after.empty() && after.front() == L'"') {
                size_t q2 = after.find(L'"', 1);
                inc.headerPath = (q2 != std::wstring::npos) ? after.substr(1, q2 - 1) : after;
                std::wstring hStem = ToLowerW(fs::path(inc.headerPath).stem().wstring());
                inc.group = (!stem.empty() && hStem == stem) ? 0 : 1;
            } else if (!after.empty() && after.front() == L'<') {
                size_t q2 = after.find(L'>', 1);
                inc.headerPath = (q2 != std::wstring::npos) ? after.substr(1, q2 - 1) : after;
                bool isStd = (inc.headerPath.find(L'.') == std::wstring::npos &&
                              inc.headerPath.find(L'/') == std::wstring::npos);
                inc.group = isStd ? 3 : 2;
            } else {
                break;
            }
            if (seen.insert(inc.line).second) incs.push_back(std::move(inc));
            ++i;
            continue;
        }
        break;
    }
    if (incs.empty()) return res;

    size_t bodyStart = i;
    while (bodyStart < lines.size() && TrimW(lines[bodyStart]).empty()) ++bodyStart;

    std::sort(incs.begin(), incs.end(), [](const CppInc& a, const CppInc& b) {
        if (a.group != b.group) return a.group < b.group;
        return ToLowerW(a.headerPath) < ToLowerW(b.headerPath);
    });

    std::vector<std::wstring> outLines;
    while (!preamble.empty() && TrimW(preamble.back()).empty()) preamble.pop_back();
    for (const auto& pl : preamble) outLines.push_back(pl);
    if (!preamble.empty()) outLines.push_back(L"");
    int lastGrp = -1;
    for (const auto& inc : incs) {
        if (lastGrp != -1 && inc.group != lastGrp) outLines.push_back(L"");
        lastGrp = inc.group;
        outLines.push_back(inc.line);
    }
    if (bodyStart < lines.size()) {
        outLines.push_back(L"");
        for (size_t k = bodyStart; k < lines.size(); ++k) outLines.push_back(lines[k]);
    }
    std::wstring nextText = JoinLinesWithEnding(outLines, nl, endedNl);
    res.changed = (nextText != content);
    res.updatedText = std::move(nextText);
    res.totalImportCount = static_cast<int>(incs.size());
    return res;
}

} // namespace

// ============================================================================
// #21 Implementation: ImportOrganizer
// ============================================================================

OrganizeImportsResult ImportOrganizer::OrganizeImports(const std::wstring& filePath,
                                                       const std::wstring& content) {
    std::wstring ext = GetExtLower(filePath);
    if (IsJsTsExt(ext)) return OrganizeJsTsImports(ext, content);
    if (ext == L".py" || ext == L".pyw") return OrganizePythonImports(content);
    if (ext == L".php") return OrganizePhpUses(content);
    if (IsCppExt(ext)) return OrganizeCppIncludes(filePath, content);
    return OrganizeImportsResult{ content, false, 0, 0 };
}

std::vector<ImportCandidate> ImportOrganizer::FindAutoImportCandidates(
    const std::wstring& filePath,
    const std::wstring& workspaceRoot,
    const std::wstring& symbolQuery,
    const std::wstring& currentContent,
    int maxResults) {
    std::vector<ImportCandidate> results;
    std::wstring query = TrimW(symbolQuery);
    if (query.empty() || maxResults <= 0) return results;

    std::wstring ext = GetExtLower(filePath);
    bool isJs = IsJsTsExt(ext);
    bool isPy = (ext == L".py" || ext == L".pyw");
    bool isPhp = (ext == L".php");
    if (!isJs && !isPy && !isPhp) return results;

    auto alreadyImported = [&](const std::wstring& sym, const std::wstring& mod) {
        if (currentContent.find(mod) != std::wstring::npos &&
            CountWholeWordOccurrences(currentContent, sym) > 0) {
            return true;
        }
        return false;
    };

    auto pushIfMatch = [&](const ImportCandidate& cand) {
        if (static_cast<int>(results.size()) >= maxResults) return;
        if (!StartsWithNoCase(cand.symbol, query)) return;
        if (alreadyImported(cand.symbol, cand.moduleSpecifier)) return;
        for (const auto& existing : results) {
            if (existing.symbol == cand.symbol && existing.moduleSpecifier == cand.moduleSpecifier) return;
        }
        results.push_back(cand);
    };

    if (isJs) {
        struct BuiltinImportDef { const wchar_t* sym; const wchar_t* mod; const wchar_t* kind; bool defExp; };
        static const BuiltinImportDef kJsBuiltins[] = {
            { L"useState", L"react", L"React", false },
            { L"useEffect", L"react", L"React", false },
            { L"useRef", L"react", L"React", false },
            { L"useMemo", L"react", L"React", false },
            { L"useCallback", L"react", L"React", false },
            { L"useContext", L"react", L"React", false },
            { L"useReducer", L"react", L"React", false },
            { L"Fragment", L"react", L"React", false },
            { L"NextRequest", L"next/server", L"Next.js", false },
            { L"NextResponse", L"next/server", L"Next.js", false },
            { L"useRouter", L"next/navigation", L"Next.js", false },
            { L"usePathname", L"next/navigation", L"Next.js", false },
            { L"useSearchParams", L"next/navigation", L"Next.js", false },
            { L"redirect", L"next/navigation", L"Next.js", false },
            { L"notFound", L"next/navigation", L"Next.js", false },
            { L"cookies", L"next/headers", L"Next.js", false },
            { L"headers", L"next/headers", L"Next.js", false },
            { L"Image", L"next/image", L"Next.js", true },
            { L"Link", L"next/link", L"Next.js", true },
            { L"readFile", L"node:fs/promises", L"Node", false },
            { L"writeFile", L"node:fs/promises", L"Node", false },
            { L"join", L"node:path", L"Node", false },
            { L"resolve", L"node:path", L"Node", false },
            { L"EventEmitter", L"node:events", L"Node", false },
        };
        for (const auto& b : kJsBuiltins) {
            ImportCandidate c;
            c.symbol = b.sym;
            c.moduleSpecifier = b.mod;
            c.sourceKind = b.kind;
            c.isDefaultExport = b.defExp;
            c.importStatement = b.defExp
                ? (L"import " + c.symbol + L" from '" + c.moduleSpecifier + L"';")
                : (L"import { " + c.symbol + L" } from '" + c.moduleSpecifier + L"';");
            pushIfMatch(c);
        }
    } else if (isPy) {
        struct PyBuiltinImportDef { const wchar_t* sym; const wchar_t* mod; bool wholeMod; };
        static const PyBuiltinImportDef kPyBuiltins[] = {
            { L"Path", L"pathlib", false },
            { L"dataclass", L"dataclasses", false },
            { L"Optional", L"typing", false },
            { L"List", L"typing", false },
            { L"Dict", L"typing", false },
            { L"Any", L"typing", false },
            { L"Union", L"typing", false },
            { L"Tuple", L"typing", false },
            { L"Callable", L"typing", false },
            { L"datetime", L"datetime", false },
            { L"timedelta", L"datetime", false },
            { L"os", L"os", true },
            { L"sys", L"sys", true },
            { L"json", L"json", true },
            { L"re", L"re", true },
            { L"math", L"math", true },
            { L"asyncio", L"asyncio", true },
            { L"subprocess", L"subprocess", true },
        };
        for (const auto& b : kPyBuiltins) {
            ImportCandidate c;
            c.symbol = b.sym;
            c.moduleSpecifier = b.mod;
            c.sourceKind = L"Python Stdlib";
            c.importStatement = b.wholeMod
                ? (L"import " + c.moduleSpecifier)
                : (L"from " + c.moduleSpecifier + L" import " + c.symbol);
            pushIfMatch(c);
        }
    }

    // Bounded workspace scan (max 250 matched files / 600 visited entries, skip hidden/vendor/build folders)
    fs::path root = !workspaceRoot.empty() ? fs::path(workspaceRoot)
                                           : fs::path(filePath).parent_path();
    std::error_code ec;
    if (!root.empty() && fs::exists(root, ec) && fs::is_directory(root, ec)) {
        int scanned = 0;
        int visited = 0;
        static const std::unordered_set<std::wstring> kSkipDirs = {
            L"node_modules", L".git", L".phide", L"build", L"dist", L"out", L"vendor",
            L".venv", L"venv", L"__pycache__", L".next", L"appdata", L"coverage", L"tmp", L"temp"
        };
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        for (; it != fs::recursive_directory_iterator() && scanned < 250 && visited < 600 &&
               static_cast<int>(results.size()) < maxResults;
             it.increment(ec)) {
            if (ec) continue;
            ++visited;
            if (it.depth() > 6) {
                it.disable_recursion_pending();
                continue;
            }
            const auto& p = it->path();
            if (it->is_directory(ec)) {
                std::wstring dName = ToLowerW(p.filename().wstring());
                if ((!dName.empty() && dName[0] == L'.') || kSkipDirs.count(dName)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            std::wstring fPath = p.wstring();
            if (ToLowerW(fPath) == ToLowerW(filePath)) continue;
            std::wstring fExt = GetExtLower(fPath);
            if ((isJs && !IsJsTsExt(fExt)) || (isPy && fExt != L".py") || (isPhp && fExt != L".php")) continue;

            ++scanned;
            std::wstring fileText;
            if (!ReadUtf8FileBounded(p, fileText, 128 * 1024)) continue;

            if (isJs) {
                std::wstring relMod = ComputeRelativeImportPath(filePath, fPath, fExt);
                auto lines = SplitLinesPreserve(fileText);
                for (const auto& l : lines) {
                    std::wstring tl = TrimW(l);
                    if (tl.rfind(L"export ", 0) != 0) continue;
                    bool isDef = (tl.find(L"export default ") == 0);
                    static const wchar_t* kPrefixes[] = {
                        L"export default function ", L"export default class ", L"export default ",
                        L"export async function ", L"export function ", L"export class ",
                        L"export const ", L"export let ", L"export interface ", L"export type ", L"export enum "
                    };
                    for (const wchar_t* pref : kPrefixes) {
                        if (tl.rfind(pref, 0) == 0) {
                            std::wstring rem = TrimW(tl.substr(wcslen(pref)));
                            size_t endSym = 0;
                            while (endSym < rem.size() && IsIdentCharW(rem[endSym])) ++endSym;
                            if (endSym > 0) {
                                std::wstring sym = rem.substr(0, endSym);
                                ImportCandidate c;
                                c.symbol = sym;
                                c.moduleSpecifier = relMod;
                                c.sourceKind = L"Workspace";
                                c.isDefaultExport = isDef;
                                c.importStatement = isDef
                                    ? (L"import " + sym + L" from '" + relMod + L"';")
                                    : (L"import { " + sym + L" } from '" + relMod + L"';");
                                pushIfMatch(c);
                            }
                            break;
                        }
                    }
                }
            } else if (isPy) {
                std::error_code relEc;
                fs::path rel = p.lexically_relative(root);
                std::wstring mod = rel.wstring();
                if (mod.size() > 3 && mod.substr(mod.size() - 3) == L".py") mod.erase(mod.size() - 3);
                std::replace(mod.begin(), mod.end(), L'\\', L'.');
                std::replace(mod.begin(), mod.end(), L'/', L'.');
                if (mod.rfind(L"__", 0) == 0) continue;
                auto lines = SplitLinesPreserve(fileText);
                for (const auto& l : lines) {
                    if (!l.empty() && (l[0] == L' ' || l[0] == L'\t')) continue; // top-level only
                    for (const wchar_t* pref : { L"def ", L"async def ", L"class " }) {
                        if (l.rfind(pref, 0) == 0) {
                            std::wstring rem = l.substr(wcslen(pref));
                            size_t endSym = 0;
                            while (endSym < rem.size() && IsIdentCharW(rem[endSym])) ++endSym;
                            if (endSym > 0) {
                                std::wstring sym = rem.substr(0, endSym);
                                ImportCandidate c;
                                c.symbol = sym;
                                c.moduleSpecifier = mod;
                                c.sourceKind = L"Workspace";
                                c.importStatement = L"from " + mod + L" import " + sym;
                                pushIfMatch(c);
                            }
                            break;
                        }
                    }
                }
            } else if (isPhp) {
                std::wstring ns;
                auto lines = SplitLinesPreserve(fileText);
                for (const auto& l : lines) {
                    std::wstring tl = TrimW(l);
                    if (tl.rfind(L"namespace ", 0) == 0 && tl.back() == L';') {
                        ns = TrimW(tl.substr(10, tl.size() - 11));
                    } else if (tl.rfind(L"class ", 0) == 0 || tl.rfind(L"interface ", 0) == 0 || tl.rfind(L"trait ", 0) == 0) {
                        size_t sp = tl.find(L' ');
                        std::wstring rem = TrimW(tl.substr(sp + 1));
                        size_t endSym = 0;
                        while (endSym < rem.size() && IsIdentCharW(rem[endSym])) ++endSym;
                        if (endSym > 0) {
                            std::wstring sym = rem.substr(0, endSym);
                            std::wstring fqn = ns.empty() ? sym : (ns + L"\\" + sym);
                            ImportCandidate c;
                            c.symbol = sym;
                            c.moduleSpecifier = fqn;
                            c.sourceKind = L"Workspace";
                            c.importStatement = L"use " + fqn + L";";
                            pushIfMatch(c);
                        }
                    }
                }
            }
        }
    }

    // Prioritize exact matches first
    std::stable_sort(results.begin(), results.end(), [&](const ImportCandidate& a, const ImportCandidate& b) {
        bool aEq = (ToLowerW(a.symbol) == ToLowerW(query));
        bool bEq = (ToLowerW(b.symbol) == ToLowerW(query));
        if (aEq != bEq) return aEq > bEq;
        return a.symbol.size() < b.symbol.size();
    });
    return results;
}

std::wstring ImportOrganizer::InsertImportStatement(const std::wstring& filePath,
                                                    const std::wstring& content,
                                                    const ImportCandidate& candidate,
                                                    size_t* outInsertedLine) {
    bool endedNl = false;
    auto lines = SplitLinesPreserve(content, &endedNl);
    std::wstring nl = DetectLineEnding(content);
    std::wstring ext = GetExtLower(filePath);

    // 1. Check if named import from same module can be merged directly into an existing import line
    if (IsJsTsExt(ext) && !candidate.isDefaultExport && !candidate.moduleSpecifier.empty()) {
        for (size_t i = 0; i < lines.size() && i < 120; ++i) {
            std::wstring tl = TrimW(lines[i]);
            if (tl.rfind(L"import ", 0) != 0) continue;
            if (tl.find(L"'" + candidate.moduleSpecifier + L"'") == std::wstring::npos &&
                tl.find(L"\"" + candidate.moduleSpecifier + L"\"") == std::wstring::npos) {
                continue;
            }
            size_t rBrace = lines[i].find(L'}');
            size_t lBrace = lines[i].find(L'{');
            if (lBrace != std::wstring::npos && rBrace != std::wstring::npos && rBrace > lBrace) {
                if (lines[i].find(candidate.symbol, lBrace) != std::wstring::npos) {
                    if (outInsertedLine) *outInsertedLine = i;
                    return content;
                }
                std::wstring beforeClose = TrimW(lines[i].substr(lBrace + 1, rBrace - lBrace - 1));
                std::wstring insertPiece = beforeClose.empty() ? candidate.symbol : (L", " + candidate.symbol);
                // Insert right before `}` (or before trailing space before `}`)
                size_t insertPos = rBrace;
                while (insertPos > lBrace + 1 && lines[i][insertPos - 1] == L' ') --insertPos;
                lines[i].insert(insertPos, insertPiece);
                if (outInsertedLine) *outInsertedLine = i;
                return JoinLinesWithEnding(lines, nl, endedNl);
            }
        }
    }

    // Check duplicate statement already present
    for (size_t i = 0; i < lines.size(); ++i) {
        if (TrimW(lines[i]) == TrimW(candidate.importStatement)) {
            if (outInsertedLine) *outInsertedLine = i;
            return content;
        }
    }

    // Find best insertion line after directives / namespace / existing imports
    size_t insertIdx = 0;
    bool seenImport = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring tl = TrimW(lines[i]);
        if (tl.rfind(L"import ", 0) == 0 || tl.rfind(L"from ", 0) == 0 ||
            (ext == L".php" && tl.rfind(L"use ", 0) == 0 && tl.find(L'(') == std::wstring::npos)) {
            insertIdx = i + 1;
            seenImport = true;
            continue;
        }
        if (!seenImport && (tl == L"'use client';" || tl == L"\"use client\";" ||
                            tl == L"'use server';" || tl == L"\"use server\";" ||
                            tl == L"<?php" || tl.rfind(L"namespace ", 0) == 0 ||
                            tl.rfind(L"#!", 0) == 0 || tl.rfind(L"//", 0) == 0)) {
            insertIdx = i + 1;
            continue;
        }
        if (tl.empty()) continue;
        break;
    }

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertIdx), candidate.importStatement);
    if (outInsertedLine) *outInsertedLine = insertIdx;
    return JoinLinesWithEnding(lines, nl, endedNl);
}

// ============================================================================
// #22 Implementation: HoverAndSignatureHelper
// ============================================================================

std::vector<MarkdownBlock> HoverAndSignatureHelper::ParseMarkdownBlocks(const std::wstring& markdown) {
    std::vector<MarkdownBlock> blocks;
    auto lines = SplitLinesPreserve(markdown);
    bool inCode = false;
    MarkdownBlock curCode;
    curCode.kind = MarkdownBlock::Kind::CodeBlock;

    auto stripInlineBackticksAndBold = [](std::wstring s) {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (i + 1 < s.size() && ((s[i] == L'*' && s[i + 1] == L'*') || (s[i] == L'_' && s[i + 1] == L'_'))) {
                ++i;
                continue;
            }
            if (s[i] == L'`') continue;
            out.push_back(s[i]);
        }
        return out;
    };

    for (const auto& rawLine : lines) {
        std::wstring t = TrimW(rawLine);
        if (t.rfind(L"```", 0) == 0) {
            if (!inCode) {
                inCode = true;
                curCode.lang = TrimW(t.substr(3));
                curCode.text.clear();
            } else {
                inCode = false;
                if (!curCode.text.empty()) {
                    while (!curCode.text.empty() && curCode.text.back() == L'\n') curCode.text.pop_back();
                    blocks.push_back(curCode);
                }
                curCode.text.clear();
                curCode.lang.clear();
            }
            continue;
        }
        if (inCode) {
            if (!curCode.text.empty()) curCode.text.push_back(L'\n');
            curCode.text += rawLine;
            continue;
        }
        if (t.empty()) continue;
        if (t == L"---" || t == L"***" || t == L"___") {
            blocks.push_back({ MarkdownBlock::Kind::Separator, L"", L"" });
            continue;
        }
        if (t[0] == L'#') {
            size_t hashEnd = 0;
            while (hashEnd < t.size() && t[hashEnd] == L'#') ++hashEnd;
            blocks.push_back({ MarkdownBlock::Kind::Heading, stripInlineBackticksAndBold(TrimW(t.substr(hashEnd))), L"" });
            continue;
        }
        if (t.rfind(L"- ", 0) == 0 || t.rfind(L"* ", 0) == 0) {
            blocks.push_back({ MarkdownBlock::Kind::Bullet, L"\u2022 " + stripInlineBackticksAndBold(TrimW(t.substr(2))), L"" });
            continue;
        }
        blocks.push_back({ MarkdownBlock::Kind::Paragraph, stripInlineBackticksAndBold(t), L"" });
    }
    if (inCode && !curCode.text.empty()) {
        blocks.push_back(std::move(curCode));
    }
    return blocks;
}

phide::lsp::HoverResult HoverAndSignatureHelper::LookupStaticHover(
    const std::wstring& filePath,
    const std::vector<std::wstring>& docLines,
    size_t line,
    size_t col) {
    phide::lsp::HoverResult res;
    if (line >= docLines.size()) return res;
    const std::wstring& lineStr = docLines[line];
    if (lineStr.empty()) return res;

    size_t probe = (col < lineStr.size()) ? col : (lineStr.size() - 1);
    if (!IsIdentCharW(lineStr[probe]) && probe > 0 && IsIdentCharW(lineStr[probe - 1])) {
        --probe;
    }
    if (!IsIdentCharW(lineStr[probe])) return res;

    size_t start = probe;
    while (start > 0 && IsIdentCharW(lineStr[start - 1])) --start;
    size_t end = probe + 1;
    while (end < lineStr.size() && IsIdentCharW(lineStr[end])) ++end;
    std::wstring token = lineStr.substr(start, end - start);
    if (token.empty()) return res;

    res.rangeLine = line;
    res.rangeStartCol = start;
    res.rangeEndCol = end;

    std::wstring ext = GetExtLower(filePath);
    std::wstring langTag = IsJsTsExt(ext) ? L"ts" : (ext == L".py" ? L"python" : (ext == L".php" ? L"php" : (IsCppExt(ext) ? L"cpp" : L"")));

    // 1. Check same-file declaration (with preceding JSDoc / docstring / comment)
    for (size_t i = 0; i < docLines.size() && i < 2000; ++i) {
        std::wstring tl = TrimW(docLines[i]);
        bool match = false;
        if (tl.find(L"function " + token) != std::wstring::npos ||
            tl.find(L"class " + token) != std::wstring::npos ||
            tl.find(L"interface " + token) != std::wstring::npos ||
            tl.find(L"type " + token + L" ") != std::wstring::npos ||
            tl.find(L"def " + token + L"(") != std::wstring::npos ||
            tl.find(L"const " + token + L" ") != std::wstring::npos ||
            tl.find(L"const " + token + L"=") != std::wstring::npos ||
            tl.find(L"let " + token + L" ") != std::wstring::npos) {
            if (i != line) match = true;
        }
        if (match) {
            std::wstring docComment;
            for (int back = 1; back <= 5 && i >= static_cast<size_t>(back); ++back) {
                std::wstring prev = TrimW(docLines[i - back]);
                if (prev.rfind(L"//", 0) == 0 || prev.rfind(L"#", 0) == 0 || prev.rfind(L"/*", 0) == 0 || prev.rfind(L"*", 0) == 0) {
                    docComment = prev + (docComment.empty() ? L"" : (L"\n" + docComment));
                } else {
                    break;
                }
            }
            res.isValid = true;
            res.markdown = L"```" + langTag + L"\n" + tl + L"\n```";
            if (!docComment.empty()) res.markdown += L"\n---\n" + docComment;
            res.markdown += L"\nDeclared at line " + std::to_wstring(i + 1);
            return res;
        }
    }

    // 2. Check Web/TS/DOM/Canvas/React built-in vocabulary
    if (IsJsTsExt(ext) || ext.empty()) {
        const std::vector<IntelliSenseItem>* lists[] = {
            &WebIntelliSenseData::GetTypeScriptGlobalsAndUtilityTypes(),
            &WebIntelliSenseData::GetJavaScriptGlobals(),
            &WebIntelliSenseData::GetJavaScriptDomMethods(),
            &WebIntelliSenseData::GetJavaScriptPrototypeMethods(),
            &WebIntelliSenseData::GetCanvasRenderingContext2DMethods()
        };
        for (const auto* vec : lists) {
            for (const auto& it : *vec) {
                if (it.label == token) {
                    res.isValid = true;
                    res.markdown = L"```ts\n" + it.detail + L"\n```";
                    if (!it.documentation.empty()) {
                        res.markdown += L"\n---\n" + it.documentation;
                    }
                    return res;
                }
            }
        }
    }

    // 3. Python builtins quick hover
    if (ext == L".py") {
        struct PyHoverDef { const wchar_t* name; const wchar_t* sig; const wchar_t* doc; };
        static const PyHoverDef kPyHovers[] = {
            { L"len", L"def len(obj: Sized) -> int", L"Return the number of items in a container." },
            { L"range", L"def range(start: int, stop?: int, step?: int) -> range", L"Return an immutable sequence of numbers." },
            { L"enumerate", L"def enumerate(iterable, start: int = 0)", L"Yield pairs containing a count and a value from the iterable." },
            { L"zip", L"def zip(*iterables, strict: bool = False)", L"Iterate over multiple iterables in parallel." },
            { L"open", L"def open(file, mode: str = 'r', encoding: str | None = None)", L"Open file and return a corresponding stream." },
            { L"print", L"def print(*values, sep: str = ' ', end: str = '\\n', file=None)", L"Print objects to the text stream file." },
            { L"isinstance", L"def isinstance(obj: object, class_or_tuple) -> bool", L"Return whether an object is an instance of a class or tuple of classes." },
            { L"sorted", L"def sorted(iterable, *, key=None, reverse: bool = False) -> list", L"Return a new list containing all items from the iterable in ascending order." },
        };
        for (const auto& h : kPyHovers) {
            if (token == h.name) {
                res.isValid = true;
                res.markdown = std::wstring(L"```python\n") + h.sig + L"\n```\n---\n" + h.doc;
                return res;
            }
        }
    }

    return res;
}

std::vector<std::wstring> HoverAndSignatureHelper::SplitSignatureParameters(const std::wstring& paramsStr) {
    std::vector<std::wstring> params;
    int depthAngle = 0;
    int depthParen = 0;
    int depthBracket = 0;
    int depthBrace = 0;
    wchar_t quote = 0;
    std::wstring cur;

    for (size_t i = 0; i < paramsStr.size(); ++i) {
        wchar_t ch = paramsStr[i];
        if (quote) {
            cur.push_back(ch);
            if (ch == L'\\' && i + 1 < paramsStr.size()) { cur.push_back(paramsStr[++i]); continue; }
            if (ch == quote) quote = 0;
            continue;
        }
        if (ch == L'\'' || ch == L'"' || ch == L'`') {
            quote = ch;
            cur.push_back(ch);
            continue;
        }
        if (ch == L'(') ++depthParen;
        else if (ch == L')' && depthParen > 0) --depthParen;
        else if (ch == L'[') ++depthBracket;
        else if (ch == L']' && depthBracket > 0) --depthBracket;
        else if (ch == L'{') ++depthBrace;
        else if (ch == L'}' && depthBrace > 0) --depthBrace;
        else if (ch == L'<' && !(i + 1 < paramsStr.size() && (paramsStr[i + 1] == L'=' || paramsStr[i + 1] == L'>'))) ++depthAngle;
        else if (ch == L'>' && depthAngle > 0 && !(i > 0 && paramsStr[i - 1] == L'=')) --depthAngle;

        if (ch == L',' && depthAngle == 0 && depthParen == 0 && depthBracket == 0 && depthBrace == 0) {
            std::wstring trimmed = TrimW(cur);
            if (!trimmed.empty()) params.push_back(std::move(trimmed));
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    std::wstring tail = TrimW(cur);
    if (!tail.empty()) params.push_back(std::move(tail));
    return params;
}

CallContext HoverAndSignatureHelper::ExtractCallContext(const std::vector<std::wstring>& docLines,
                                                        size_t cursorLine,
                                                        size_t cursorCol) {
    CallContext ctx;
    if (docLines.empty() || cursorLine >= docLines.size()) return ctx;

    int depthParen = 0;
    int depthBracket = 0;
    int depthBrace = 0;
    int commaCount = 0;

    size_t minLine = (cursorLine > 15) ? (cursorLine - 15) : 0;
    for (size_t lineIdx = cursorLine + 1; lineIdx > minLine; --lineIdx) {
        size_t l = lineIdx - 1;
        const std::wstring& s = docLines[l];
        size_t maxCol = (l == cursorLine) ? std::min(cursorCol, s.size()) : s.size();

        wchar_t stringQuote = 0;
        // Quick forward scan up to maxCol to know quote regions on this line
        std::vector<bool> inStr(maxCol, false);
        for (size_t i = 0; i < maxCol; ++i) {
            wchar_t c = s[i];
            if (stringQuote) {
                inStr[i] = true;
                if (c == L'\\' && i + 1 < maxCol) { inStr[++i] = true; continue; }
                if (c == stringQuote) stringQuote = 0;
            } else if (c == L'\'' || c == L'"' || c == L'`') {
                stringQuote = c;
                inStr[i] = true;
            }
        }

        for (size_t colIdx = maxCol; colIdx > 0; --colIdx) {
            size_t cPos = colIdx - 1;
            if (inStr[cPos]) continue;
            wchar_t ch = s[cPos];

            if (ch == L')') ++depthParen;
            else if (ch == L']') ++depthBracket;
            else if (ch == L'}') ++depthBrace;
            else if (ch == L'[') { if (depthBracket > 0) --depthBracket; else return ctx; }
            else if (ch == L'{') { if (depthBrace > 0) --depthBrace; else return ctx; }
            else if (ch == L';') { return ctx; }
            else if (ch == L',' && depthParen == 0 && depthBracket == 0 && depthBrace == 0) {
                ++commaCount;
            } else if (ch == L'(') {
                if (depthParen > 0) {
                    --depthParen;
                } else {
                    // Found matching '('! Extract identifier before '('
                    size_t idEnd = cPos;
                    while (idEnd > 0 && std::iswspace(s[idEnd - 1])) --idEnd;
                    size_t idStart = idEnd;
                    while (idStart > 0 && IsIdentCharW(s[idStart - 1])) --idStart;
                    if (idStart == idEnd) return ctx;
                    ctx.inCall = true;
                    ctx.calleeName = s.substr(idStart, idEnd - idStart);
                    ctx.activeParameter = commaCount;
                    ctx.parenLine = l;
                    ctx.parenCol = cPos;

                    // Optional receiver `obj.method(`
                    if (idStart >= 2 && s[idStart - 1] == L'.') {
                        size_t rEnd = idStart - 1;
                        size_t rStart = rEnd;
                        while (rStart > 0 && IsIdentCharW(s[rStart - 1])) --rStart;
                        ctx.receiverName = s.substr(rStart, rEnd - rStart);
                    }
                    return ctx;
                }
            }
        }
    }
    return ctx;
}

phide::lsp::SignatureHelpResult HoverAndSignatureHelper::LookupStaticSignatureHelp(
    const std::wstring& filePath,
    const std::vector<std::wstring>& docLines,
    size_t cursorLine,
    size_t cursorCol) {
    phide::lsp::SignatureHelpResult res;
    CallContext ctx = ExtractCallContext(docLines, cursorLine, cursorCol);
    if (!ctx.inCall || ctx.calleeName.empty()) return res;

    auto makeSig = [&](const std::wstring& label, const std::wstring& doc) {
        phide::lsp::SignatureInfo info;
        info.label = label;
        info.documentation = doc;
        size_t lp = label.find(L'(');
        size_t rp = label.rfind(L')');
        if (lp != std::wstring::npos && rp != std::wstring::npos && rp > lp) {
            auto parts = SplitSignatureParameters(label.substr(lp + 1, rp - lp - 1));
            for (const auto& p : parts) {
                info.parameters.push_back({ p, L"" });
            }
        }
        info.activeParameter = ctx.activeParameter;
        res.signatures.push_back(std::move(info));
    };

    // 1. Check same-file function declaration `function foo(...)` or `def foo(...)`
    for (size_t i = 0; i < docLines.size() && i < 2000; ++i) {
        std::wstring tl = TrimW(docLines[i]);
        size_t pos = tl.find(ctx.calleeName + L"(");
        if (pos == std::wstring::npos) continue;
        bool isDecl = (tl.rfind(L"function ", 0) == 0 || tl.rfind(L"async function ", 0) == 0 ||
                       tl.rfind(L"export function ", 0) == 0 || tl.rfind(L"def ", 0) == 0 ||
                       tl.rfind(L"async def ", 0) == 0);
        if (isDecl && tl.find(L')', pos) != std::wstring::npos) {
            makeSig(tl, L"Declared at line " + std::to_wstring(i + 1));
            break;
        }
    }

    // 2. Check WebIntelliSenseData signatures
    const std::vector<IntelliSenseItem>* lists[] = {
        &WebIntelliSenseData::GetJavaScriptGlobals(),
        &WebIntelliSenseData::GetJavaScriptDomMethods(),
        &WebIntelliSenseData::GetJavaScriptPrototypeMethods(),
        &WebIntelliSenseData::GetCanvasRenderingContext2DMethods(),
        &WebIntelliSenseData::GetTypeScriptGlobalsAndUtilityTypes()
    };
    for (const auto* vec : lists) {
        for (const auto& it : *vec) {
            if (it.label == ctx.calleeName && it.detail.find(L'(') != std::wstring::npos) {
                makeSig(it.detail, it.documentation);
            }
        }
    }

    // 3. Curated common overloads across JS/TS/React/Python/PHP
    struct CuratedSig { const wchar_t* name; const wchar_t* sig; const wchar_t* doc; };
    static const CuratedSig kCurated[] = {
        { L"useState", L"useState<S>(initialState: S | (() => S)): [S, Dispatch<SetStateAction<S>>]", L"Returns a stateful value and a function to update it." },
        { L"useEffect", L"useEffect(effect: EffectCallback, deps?: DependencyList): void", L"Runs a side-effect callback after render." },
        { L"useMemo", L"useMemo<T>(factory: () => T, deps: DependencyList): T", L"Memoizes an expensive computed value across renders." },
        { L"useCallback", L"useCallback<T extends Function>(callback: T, deps: DependencyList): T", L"Memoizes a callback reference across renders." },
        { L"useRef", L"useRef<T>(initialValue: T): MutableRefObject<T>", L"Creates a mutable ref container persisted across renders." },
        { L"fetch", L"fetch(input: RequestInfo | URL, init?: RequestInit): Promise<Response>", L"Starts an asynchronous HTTP network fetch." },
        { L"setTimeout", L"setTimeout(handler: TimerHandler, timeout?: number, ...arguments: any[]): number", L"Schedules handler execution after timeout milliseconds." },
        { L"setInterval", L"setInterval(handler: TimerHandler, timeout?: number, ...arguments: any[]): number", L"Repeatedly executes handler every timeout milliseconds." },
        { L"addEventListener", L"addEventListener(type: string, listener: EventListener, options?: boolean | AddEventListenerOptions): void", L"Registers an event listener on the target." },
        { L"open", L"open(file: PathLike, mode: str = 'r', buffering: int = -1, encoding: str | None = None)", L"Python built-in file open." },
        { L"range", L"range(stop: int) -> range", L"Sequence from 0 to stop - 1." },
        { L"range", L"range(start: int, stop: int, step: int = 1) -> range", L"Sequence from start to stop - 1 stepping by step." },
        { L"array_map", L"array_map(?callable $callback, array $array, array ...$arrays): array", L"Applies callback to elements of the given arrays." },
        { L"str_replace", L"str_replace(array|string $search, array|string $replace, string|array $subject, int &$count = null): string|array", L"Replaces occurrences of search string with replacement." },
        { L"preg_match", L"preg_match(string $pattern, string $subject, array &$matches = null, int $flags = 0, int $offset = 0): int|false", L"Performs a regular expression match." },
    };
    (void)filePath;
    for (const auto& cs : kCurated) {
        if (ctx.calleeName == cs.name) {
            bool dup = false;
            for (const auto& existing : res.signatures) {
                if (existing.label == cs.sig) { dup = true; break; }
            }
            if (!dup) makeSig(cs.sig, cs.doc);
        }
    }

    if (!res.signatures.empty()) {
        res.isValid = true;
        res.activeSignature = 0;
        res.activeParameter = ctx.activeParameter;
    }
    return res;
}

// ============================================================================
// #23 Implementation: ExternalLinterEngine & Problems Model
// ============================================================================

namespace {

std::wstring ResolvePathRelativeToRoot(const std::wstring& rawPath, const std::wstring& workspaceRoot) {
    std::wstring trimmed = TrimW(rawPath);
    if (trimmed.empty()) return L"";
    fs::path p(trimmed);
    if (p.is_absolute() || workspaceRoot.empty()) {
        std::wstring n = p.lexically_normal().wstring();
        return n;
    }
    return (fs::path(workspaceRoot) / p).lexically_normal().wstring();
}

std::wstring MakeDisplayRelative(const std::wstring& fullPath, const std::wstring& workspaceRoot) {
    if (workspaceRoot.empty() || fullPath.empty()) return fs::path(fullPath).filename().wstring();
    std::error_code ec;
    fs::path rel = fs::path(fullPath).lexically_relative(fs::path(workspaceRoot));
    std::wstring s = rel.wstring();
    if (s.empty() || s == L"." || s.rfind(L"..", 0) == 0) return fs::path(fullPath).filename().wstring();
    std::replace(s.begin(), s.end(), L'\\', L'/');
    return s;
}

} // namespace

std::vector<ProblemEntry> ExternalLinterEngine::ParseTscOutput(const std::wstring& output,
                                                               const std::wstring& workspaceRoot) {
    std::vector<ProblemEntry> items;
    auto lines = SplitLinesPreserve(output);
    for (const auto& l : lines) {
        std::wstring tl = TrimW(l);
        if (tl.empty()) continue;
        // Format A: file(line,col): error TS2304: message
        size_t lp = tl.find(L'(');
        size_t rp = (lp != std::wstring::npos) ? tl.find(L"):", lp) : std::wstring::npos;
        if (lp != std::wstring::npos && rp != std::wstring::npos) {
            std::wstring filePart = tl.substr(0, lp);
            std::wstring coords = tl.substr(lp + 1, rp - lp - 1);
            size_t comma = coords.find(L',');
            if (comma != std::wstring::npos) {
                try {
                    size_t ln = static_cast<size_t>(std::stoul(coords.substr(0, comma)));
                    size_t cl = static_cast<size_t>(std::stoul(coords.substr(comma + 1)));
                    std::wstring rest = TrimW(tl.substr(rp + 2));
                    ProblemEntry e;
                    e.filePath = ResolvePathRelativeToRoot(filePart, workspaceRoot);
                    e.displayPath = MakeDisplayRelative(e.filePath, workspaceRoot);
                    e.line = (ln > 0) ? ln - 1 : 0;
                    e.col = (cl > 0) ? cl - 1 : 0;
                    e.source = L"tsc";
                    e.severity = (rest.rfind(L"warning", 0) == 0) ? 2 : 1;
                    size_t colonMsg = rest.find(L": ");
                    if (colonMsg != std::wstring::npos) {
                        std::wstring head = rest.substr(0, colonMsg);
                        size_t tsPos = head.find(L"TS");
                        if (tsPos != std::wstring::npos) e.code = head.substr(tsPos);
                        e.message = TrimW(rest.substr(colonMsg + 2));
                    } else {
                        e.message = rest;
                    }
                    items.push_back(std::move(e));
                } catch (...) {}
            }
        }
    }
    return items;
}

std::vector<ProblemEntry> ExternalLinterEngine::ParseEslintCompactOutput(const std::wstring& output,
                                                                         const std::wstring& workspaceRoot) {
    std::vector<ProblemEntry> items;
    auto lines = SplitLinesPreserve(output);
    for (const auto& l : lines) {
        std::wstring tl = TrimW(l);
        // Format: path: line X, col Y, Error|Warning - message (rule)
        size_t marker = tl.find(L": line ");
        if (marker == std::wstring::npos) continue;
        std::wstring filePart = tl.substr(0, marker);
        std::wstring rest = tl.substr(marker + 7);
        size_t cMarker = rest.find(L", col ");
        size_t comma2 = (cMarker != std::wstring::npos) ? rest.find(L", ", cMarker + 6) : std::wstring::npos;
        size_t dash = (comma2 != std::wstring::npos) ? rest.find(L" - ", comma2 + 2) : std::wstring::npos;
        if (cMarker == std::wstring::npos || comma2 == std::wstring::npos || dash == std::wstring::npos) continue;
        try {
            size_t ln = static_cast<size_t>(std::stoul(rest.substr(0, cMarker)));
            size_t cl = static_cast<size_t>(std::stoul(rest.substr(cMarker + 6, comma2 - (cMarker + 6))));
            std::wstring sevStr = TrimW(rest.substr(comma2 + 2, dash - (comma2 + 2)));
            std::wstring msg = TrimW(rest.substr(dash + 3));
            std::wstring code;
            if (!msg.empty() && msg.back() == L')') {
                size_t op = msg.rfind(L" (");
                if (op != std::wstring::npos) {
                    code = msg.substr(op + 2, msg.size() - op - 3);
                    msg = TrimW(msg.substr(0, op));
                }
            }
            ProblemEntry e;
            e.filePath = ResolvePathRelativeToRoot(filePart, workspaceRoot);
            e.displayPath = MakeDisplayRelative(e.filePath, workspaceRoot);
            e.line = (ln > 0) ? ln - 1 : 0;
            e.col = (cl > 0) ? cl - 1 : 0;
            e.severity = (ToLowerW(sevStr) == L"warning") ? 2 : 1;
            e.message = std::move(msg);
            e.code = std::move(code);
            e.source = L"eslint";
            items.push_back(std::move(e));
        } catch (...) {}
    }
    return items;
}

std::vector<ProblemEntry> ExternalLinterEngine::ParsePhpStanOutput(const std::wstring& output,
                                                                   const std::wstring& workspaceRoot) {
    std::vector<ProblemEntry> items;
    auto lines = SplitLinesPreserve(output);
    std::wstring currentFile;
    for (const auto& l : lines) {
        std::wstring tl = TrimW(l);
        if (tl.rfind(L"------ ", 0) == 0 && tl.find(L".php") != std::wstring::npos) {
            std::wstring sub = TrimW(tl.substr(7));
            size_t sp = sub.find(L" ---");
            if (sp != std::wstring::npos) sub = TrimW(sub.substr(0, sp));
            currentFile = ResolvePathRelativeToRoot(sub, workspaceRoot);
            continue;
        }
        if (tl.rfind(L"Line ", 0) == 0 && tl.find(L".php") != std::wstring::npos) {
            std::wstring sub = TrimW(tl.substr(5));
            currentFile = ResolvePathRelativeToRoot(sub, workspaceRoot);
            continue;
        }
        // Standard raw row: `  12     Undefined variable: $foo`
        if (!currentFile.empty() && !tl.empty() && std::iswdigit(tl[0])) {
            size_t dEnd = 0;
            while (dEnd < tl.size() && std::iswdigit(tl[dEnd])) ++dEnd;
            std::wstring msg = TrimW(tl.substr(dEnd));
            if (!msg.empty() && msg.rfind(L"--", 0) != 0) {
                try {
                    size_t ln = static_cast<size_t>(std::stoul(tl.substr(0, dEnd)));
                    ProblemEntry e;
                    e.filePath = currentFile;
                    e.displayPath = MakeDisplayRelative(currentFile, workspaceRoot);
                    e.line = (ln > 0) ? ln - 1 : 0;
                    e.col = 0;
                    e.severity = 1;
                    e.source = L"phpstan";
                    e.message = std::move(msg);
                    items.push_back(std::move(e));
                } catch (...) {}
            }
        }
    }
    return items;
}

std::vector<ProblemEntry> ExternalLinterEngine::ParseRuffOrFlake8Output(const std::wstring& output,
                                                                        const std::wstring& workspaceRoot) {
    std::vector<ProblemEntry> items;
    auto lines = SplitLinesPreserve(output);
    for (const auto& l : lines) {
        std::wstring tl = TrimW(l);
        if (tl.empty()) continue;
        // Format: C:\path\file.py:12:5: F401 'os' imported but unused
        size_t firstColon = (tl.size() > 2 && tl[1] == L':') ? tl.find(L':', 2) : tl.find(L':');
        if (firstColon == std::wstring::npos) continue;
        size_t secondColon = tl.find(L':', firstColon + 1);
        size_t thirdColon = (secondColon != std::wstring::npos) ? tl.find(L':', secondColon + 1) : std::wstring::npos;
        if (secondColon == std::wstring::npos || thirdColon == std::wstring::npos) continue;
        try {
            std::wstring filePart = tl.substr(0, firstColon);
            size_t ln = static_cast<size_t>(std::stoul(tl.substr(firstColon + 1, secondColon - firstColon - 1)));
            size_t cl = static_cast<size_t>(std::stoul(tl.substr(secondColon + 1, thirdColon - secondColon - 1)));
            std::wstring rest = TrimW(tl.substr(thirdColon + 1));
            std::wstring code;
            std::wstring msg = rest;
            size_t sp = rest.find(L' ');
            if (sp != std::wstring::npos) {
                code = rest.substr(0, sp);
                msg = TrimW(rest.substr(sp + 1));
            }
            ProblemEntry e;
            e.filePath = ResolvePathRelativeToRoot(filePart, workspaceRoot);
            e.displayPath = MakeDisplayRelative(e.filePath, workspaceRoot);
            e.line = (ln > 0) ? ln - 1 : 0;
            e.col = (cl > 0) ? cl - 1 : 0;
            e.severity = (!code.empty() && (code[0] == L'W' || code[0] == L'F' || code[0] == L'I')) ? 2 : 1;
            e.code = std::move(code);
            e.message = std::move(msg);
            e.source = L"ruff";
            items.push_back(std::move(e));
        } catch (...) {}
    }
    return items;
}

namespace {

bool ToolExistsOnDisk(const std::wstring& exeName, const std::wstring& workspaceRoot, std::wstring& outResolvedPath) {
    std::error_code ec;
    if (!workspaceRoot.empty()) {
        fs::path localNode = fs::path(workspaceRoot) / L"node_modules" / L".bin" / exeName;
        if (fs::exists(localNode, ec)) { outResolvedPath = localNode.wstring(); return true; }
        fs::path localPhp = fs::path(workspaceRoot) / L"vendor" / L"bin" / exeName;
        if (fs::exists(localPhp, ec)) { outResolvedPath = localPhp.wstring(); return true; }
        fs::path venvPy = fs::path(workspaceRoot) / L".venv" / L"Scripts" / exeName;
        if (fs::exists(venvPy, ec)) { outResolvedPath = venvPy.wstring(); return true; }
    }
    wchar_t buf[MAX_PATH]{};
    if (SearchPathW(nullptr, exeName.c_str(), nullptr, MAX_PATH, buf, nullptr) > 0) {
        outResolvedPath = buf;
        return true;
    }
    return false;
}

std::wstring RunHiddenCliCaptured(const std::wstring& cmdLine, const std::wstring& workDir, DWORD timeoutMs = 3500) {
    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return L"";
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmdLine;
    const wchar_t* cwd = workDir.empty() ? nullptr : workDir.c_str();
    BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return L"";
    }

    std::string rawOut;
    rawOut.reserve(8192);
    char chunk[4096];
    DWORD startTick = GetTickCount();
    constexpr size_t kMaxOutBytes = 192 * 1024;
    while (rawOut.size() < kMaxOutBytes) {
        DWORD avail = 0;
        if (!PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail > 0) {
            DWORD got = 0;
            if (!ReadFile(hRead, chunk, sizeof(chunk), &got, nullptr) || got == 0) break;
            rawOut.append(chunk, got);
        } else {
            DWORD waitRes = WaitForSingleObject(pi.hProcess, 40);
            if (waitRes == WAIT_OBJECT_0) {
                DWORD got = 0;
                while (rawOut.size() < kMaxOutBytes && PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                    if (!ReadFile(hRead, chunk, sizeof(chunk), &got, nullptr) || got == 0) break;
                    rawOut.append(chunk, got);
                }
                break;
            }
            if (GetTickCount() - startTick > timeoutMs) {
                TerminateProcess(pi.hProcess, 1);
                break;
            }
        }
    }
    CloseHandle(hRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return Utf8ToWideStr(rawOut);
}

} // namespace

std::vector<ProblemEntry> ExternalLinterEngine::RunLintersForFile(const std::wstring& filePath,
                                                                   const std::wstring& workspaceRoot) {
    std::vector<ProblemEntry> all;
    if (filePath.empty()) return all;
    std::wstring ext = GetExtLower(filePath);
    std::wstring toolPath;

    if (ext == L".py" || ext == L".pyw") {
        if (ToolExistsOnDisk(L"ruff.exe", workspaceRoot, toolPath)) {
            std::wstring out = RunHiddenCliCaptured(
                L"\"" + toolPath + L"\" check --output-format=concise --no-fix \"" + filePath + L"\"",
                workspaceRoot);
            all = ParseRuffOrFlake8Output(out, workspaceRoot);
        } else if (ToolExistsOnDisk(L"flake8.exe", workspaceRoot, toolPath)) {
            std::wstring out = RunHiddenCliCaptured(
                L"\"" + toolPath + L"\" \"" + filePath + L"\"",
                workspaceRoot);
            all = ParseRuffOrFlake8Output(out, workspaceRoot);
        }
    } else if (IsJsTsExt(ext)) {
        if (ToolExistsOnDisk(L"eslint.cmd", workspaceRoot, toolPath)) {
            std::wstring out = RunHiddenCliCaptured(
                L"\"" + toolPath + L"\" -f compact \"" + filePath + L"\"",
                workspaceRoot);
            auto es = ParseEslintCompactOutput(out, workspaceRoot);
            all.insert(all.end(), std::make_move_iterator(es.begin()), std::make_move_iterator(es.end()));
        }
    } else if (ext == L".php") {
        if (ToolExistsOnDisk(L"phpstan.bat", workspaceRoot, toolPath) ||
            ToolExistsOnDisk(L"phpstan", workspaceRoot, toolPath)) {
            std::wstring out = RunHiddenCliCaptured(
                L"\"" + toolPath + L"\" analyse --no-progress --error-format=raw \"" + filePath + L"\"",
                workspaceRoot);
            auto php = ParsePhpStanOutput(out, workspaceRoot);
            all.insert(all.end(), std::make_move_iterator(php.begin()), std::make_move_iterator(php.end()));
        }
    }

    if (all.size() > 150) all.resize(150);
    return all;
}

std::vector<ProblemEntry> ExternalLinterEngine::FilterProblems(const std::vector<ProblemEntry>& all,
                                                               const ProblemsFilterState& filter,
                                                               const std::wstring& activeFilePath) {
    std::vector<ProblemEntry> out;
    std::wstring activeLow = ToLowerW(activeFilePath);
    std::wstring qLow = ToLowerW(TrimW(filter.searchText));

    for (const auto& p : all) {
        if (filter.severityFilter == 1 && p.severity != 1) continue;
        if (filter.severityFilter == 2 && p.severity != 2) continue;
        if (filter.severityFilter == 3 && p.severity < 3) continue;
        if (filter.currentFileOnly && ToLowerW(p.filePath) != activeLow) continue;
        if (!qLow.empty()) {
            std::wstring mLow = ToLowerW(p.message);
            std::wstring fLow = ToLowerW(p.displayPath);
            std::wstring sLow = ToLowerW(p.source);
            std::wstring cLow = ToLowerW(p.code);
            if (mLow.find(qLow) == std::wstring::npos && fLow.find(qLow) == std::wstring::npos &&
                sLow.find(qLow) == std::wstring::npos && cLow.find(qLow) == std::wstring::npos) {
                continue;
            }
        }
        out.push_back(p);
    }
    return out;
}

void ExternalLinterEngine::CountSeverities(const std::vector<ProblemEntry>& all,
                                           int& outErrors,
                                           int& outWarnings,
                                           int& outInfos) {
    outErrors = 0;
    outWarnings = 0;
    outInfos = 0;
    for (const auto& p : all) {
        if (p.severity == 1) ++outErrors;
        else if (p.severity == 2) ++outWarnings;
        else ++outInfos;
    }
}

// ============================================================================
// #24 Implementation: CodeActionEngine & Structural Refactorings
// ============================================================================

std::wstring CodeActionEngine::ApplyTextEditsToString(const std::wstring& content,
                                                      const std::vector<phide::lsp::LspTextEdit>& edits) {
    if (edits.empty()) return content;
    bool endedNl = false;
    auto lines = SplitLinesPreserve(content, &endedNl);
    std::wstring nl = DetectLineEnding(content);

    // Precompute UTF-16 line offsets on reconstructed string with uniform `\n` for exact offset splicing
    std::vector<phide::lsp::LspTextEdit> sorted = edits;
    std::sort(sorted.begin(), sorted.end(), [](const phide::lsp::LspTextEdit& a, const phide::lsp::LspTextEdit& b) {
        if (a.startLine != b.startLine) return a.startLine > b.startLine;
        return a.startCol > b.startCol;
    });

    for (const auto& e : sorted) {
        if (e.startLine >= lines.size()) {
            while (lines.size() <= e.startLine) lines.push_back(L"");
        }
        size_t eLine = std::min(e.endLine, lines.size() - 1);
        size_t sCol = std::min(e.startCol, lines[e.startLine].size());
        size_t eCol = std::min(e.endCol, lines[eLine].size());

        std::wstring prefix = lines[e.startLine].substr(0, sCol);
        std::wstring suffix = lines[eLine].substr(eCol);

        // Split replacement newText into lines
        std::wstring combinedNew = prefix + e.newText + suffix;
        auto repLines = SplitLinesPreserve(combinedNew);
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(e.startLine),
                    lines.begin() + static_cast<std::ptrdiff_t>(eLine + 1));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(e.startLine),
                     repLines.begin(), repLines.end());
    }
    return JoinLinesWithEnding(lines, nl, endedNl);
}

std::vector<phide::lsp::CodeActionItem> CodeActionEngine::GenerateLocalCodeActions(
    const std::wstring& filePath,
    const std::wstring& workspaceRoot,
    const std::wstring& docContent,
    const std::vector<phide::lsp::Diagnostic>& diagnostics,
    size_t selStartLine,
    size_t selStartCol,
    size_t selEndLine,
    size_t selEndCol) {
    std::vector<phide::lsp::CodeActionItem> actions;
    auto lines = SplitLinesPreserve(docContent);
    if (lines.empty() || selStartLine >= lines.size()) return actions;

    std::wstring ext = GetExtLower(filePath);
    bool isJs = IsJsTsExt(ext);
    bool isPy = (ext == L".py" || ext == L".pyw");
    bool isPhp = (ext == L".php");
    bool isCpp = IsCppExt(ext);

    std::wstring nl = DetectLineEnding(docContent);
    bool hasSelection = (selStartLine != selEndLine || selStartCol != selEndCol);

    // Compute leading indentation of selStartLine
    const std::wstring& curLineStr = lines[selStartLine];
    size_t indentEnd = 0;
    while (indentEnd < curLineStr.size() && (curLineStr[indentEnd] == L' ' || curLineStr[indentEnd] == L'\t')) ++indentEnd;
    std::wstring indent = curLineStr.substr(0, indentEnd);

    auto makeWholeDocEdit = [&](const std::wstring& newContent) {
        phide::lsp::WorkspaceEditResult we;
        phide::lsp::LspTextEdit te;
        te.startLine = 0;
        te.startCol = 0;
        te.endLine = lines.size() - 1;
        te.endCol = lines.back().size();
        te.newText = newContent;
        we.fileChanges.push_back({ filePath, { te } });
        return we;
    };

    // --- 1. Quick Fix: Add Missing Import from diagnostics or token under cursor ---
    std::wstring unresolvedSym;
    for (const auto& d : diagnostics) {
        if (d.line != selStartLine && !(d.line + 1 == selStartLine)) continue;
        // Extract `'Symbol'` from `Cannot find name 'Symbol'` or `Undefined name 'Symbol'`
        size_t q1 = d.message.find(L'\'');
        size_t q2 = (q1 != std::wstring::npos) ? d.message.find(L'\'', q1 + 1) : std::wstring::npos;
        if (q1 != std::wstring::npos && q2 != std::wstring::npos && q2 > q1 + 1) {
            unresolvedSym = d.message.substr(q1 + 1, q2 - q1 - 1);
            break;
        }
    }
    if (unresolvedSym.empty() && !curLineStr.empty()) {
        size_t probe = (selStartCol < curLineStr.size()) ? selStartCol : (curLineStr.size() - 1);
        if (!IsIdentCharW(curLineStr[probe]) && probe > 0 && IsIdentCharW(curLineStr[probe - 1])) --probe;
        if (IsIdentCharW(curLineStr[probe])) {
            size_t a = probe;
            while (a > 0 && IsIdentCharW(curLineStr[a - 1])) --a;
            size_t b = probe + 1;
            while (b < curLineStr.size() && IsIdentCharW(curLineStr[b])) ++b;
            unresolvedSym = curLineStr.substr(a, b - a);
        }
    }
    if (!unresolvedSym.empty()) {
        auto impCands = ImportOrganizer::FindAutoImportCandidates(filePath, workspaceRoot, unresolvedSym, docContent, 4);
        for (const auto& c : impCands) {
            if (c.symbol != unresolvedSym) continue;
            phide::lsp::CodeActionItem act;
            act.title = L"Add import: " + c.importStatement;
            act.kind = L"quickfix";
            act.isPreferred = true;
            std::wstring nextContent = ImportOrganizer::InsertImportStatement(filePath, docContent, c);
            if (nextContent != docContent) {
                act.edit = makeWholeDocEdit(nextContent);
                actions.push_back(std::move(act));
            }
        }
    }

    // --- 2. Quick Fix: Add 'use client' for React client hooks in TS/JS ---
    if (isJs && docContent.find(L"'use client'") == std::wstring::npos && docContent.find(L"\"use client\"") == std::wstring::npos) {
        if (docContent.find(L"useState") != std::wstring::npos || docContent.find(L"useEffect") != std::wstring::npos ||
            docContent.find(L"useRouter") != std::wstring::npos) {
            phide::lsp::CodeActionItem act;
            act.title = L"Add \"use client\" directive at top of file";
            act.kind = L"quickfix";
            phide::lsp::LspTextEdit te{ 0, 0, 0, 0, L"\"use client\";" + nl + nl };
            act.edit.fileChanges.push_back({ filePath, { te } });
            actions.push_back(std::move(act));
        }
    }

    // --- 3. Quick Fix: Organize Imports / Remove unused imports ---
    {
        auto org = ImportOrganizer::OrganizeImports(filePath, docContent);
        if (org.changed) {
            phide::lsp::CodeActionItem act;
            act.title = org.removedUnusedCount > 0
                ? (L"Organize Imports & Remove " + std::to_wstring(org.removedUnusedCount) + L" unused")
                : L"Organize & Sort Imports";
            act.kind = L"source.organizeImports";
            act.edit = makeWholeDocEdit(org.updatedText);
            actions.push_back(std::move(act));
        }
    }

    // --- 4. Refactoring: Extract to Local Variable (single-line expression selected) ---
    if (hasSelection && selStartLine == selEndLine && selEndCol > selStartCol && selEndCol <= curLineStr.size()) {
        std::wstring expr = TrimW(curLineStr.substr(selStartCol, selEndCol - selStartCol));
        if (!expr.empty() && expr.find(L';') == std::wstring::npos) {
            phide::lsp::CodeActionItem act;
            act.title = L"Extract to local variable (extractedVar)";
            act.kind = L"refactor.extract.variable";
            std::wstring varName = isPhp ? L"$extractedVar" : (isPy ? L"extracted_var" : L"extractedVar");
            std::wstring decl;
            if (isJs) decl = indent + L"const " + varName + L" = " + expr + L";" + nl;
            else if (isPy) decl = indent + varName + L" = " + expr + nl;
            else if (isPhp) decl = indent + varName + L" = " + expr + L";" + nl;
            else if (isCpp) decl = indent + L"auto " + varName + L" = " + expr + L";" + nl;
            else decl = indent + L"const " + varName + L" = " + expr + L";" + nl;

            phide::lsp::LspTextEdit replaceExpr{ selStartLine, selStartCol, selEndLine, selEndCol, varName };
            phide::lsp::LspTextEdit insertDecl{ selStartLine, 0, selStartLine, 0, decl };
            act.edit.fileChanges.push_back({ filePath, { replaceExpr, insertDecl } });
            actions.push_back(std::move(act));
        }
    }

    // --- 5. Refactoring: Extract to Function / Method (multi-line or expression selected) ---
    if (hasSelection && selEndLine < lines.size()) {
        std::wstring selectedBlock;
        for (size_t ln = selStartLine; ln <= selEndLine; ++ln) {
            size_t cA = (ln == selStartLine) ? std::min(selStartCol, lines[ln].size()) : 0;
            size_t cB = (ln == selEndLine) ? std::min(selEndCol, lines[ln].size()) : lines[ln].size();
            if (ln > selStartLine) selectedBlock += nl;
            selectedBlock += lines[ln].substr(cA, cB - cA);
        }
        std::wstring trimmedBlock = TrimW(selectedBlock);
        if (!trimmedBlock.empty()) {
            phide::lsp::CodeActionItem act;
            act.title = L"Extract to function (extractedFn)";
            act.kind = L"refactor.extract.function";
            std::wstring callText = isPy ? L"extracted_fn()" : L"extractedFn()";
            if (selStartLine != selEndLine && !isPy) callText += L";";

            std::wstring fnText;
            if (isPy) {
                fnText = nl + L"def extracted_fn():" + nl + L"    " +
                         (selStartLine == selEndLine ? (L"return " + trimmedBlock) : trimmedBlock) + nl;
            } else if (isPhp) {
                fnText = nl + L"function extractedFn() {" + nl + L"    " +
                         (selStartLine == selEndLine ? (L"return " + trimmedBlock + L";") : trimmedBlock) + nl + L"}" + nl;
            } else {
                fnText = nl + L"function extractedFn() {" + nl + L"    " +
                         (selStartLine == selEndLine ? (L"return " + trimmedBlock + L";") : trimmedBlock) + nl + L"}" + nl;
            }
            size_t lastLine = lines.size() - 1;
            phide::lsp::LspTextEdit appendFn{ lastLine, lines[lastLine].size(), lastLine, lines[lastLine].size(), fnText };
            phide::lsp::LspTextEdit replaceCall{ selStartLine, selStartCol, selEndLine, selEndCol, callText };
            act.edit.fileChanges.push_back({ filePath, { appendFn, replaceCall } });
            actions.push_back(std::move(act));
        }
    }

    // --- 6. Refactoring: Inline Variable on declaration line (`const x = expr;`) ---
    if (!hasSelection && (isJs || isPy)) {
        std::wstring tl = TrimW(curLineStr);
        std::wstring varName;
        std::wstring initExpr;
        if (isJs && (tl.rfind(L"const ", 0) == 0 || tl.rfind(L"let ", 0) == 0)) {
            size_t sp = tl.find(L' ');
            size_t eq = tl.find(L'=', sp);
            if (eq != std::wstring::npos) {
                varName = TrimW(tl.substr(sp + 1, eq - sp - 1));
                size_t semi = tl.rfind(L';');
                initExpr = TrimW(tl.substr(eq + 1, (semi != std::wstring::npos && semi > eq) ? (semi - eq - 1) : std::wstring::npos));
            }
        } else if (isPy && tl.find(L" = ") != std::wstring::npos && tl.rfind(L"def ", 0) != 0) {
            size_t eq = tl.find(L" = ");
            varName = TrimW(tl.substr(0, eq));
            initExpr = TrimW(tl.substr(eq + 3));
        }
        bool simpleIdent = !varName.empty() && IsIdentStartW(varName[0]);
        for (wchar_t ch : varName) if (!IsIdentCharW(ch)) { simpleIdent = false; break; }
        if (simpleIdent && !initExpr.empty()) {
            // Find usages on subsequent lines within next 60 lines
            std::vector<phide::lsp::LspTextEdit> edits;
            for (size_t ln = selStartLine + 1; ln < lines.size() && ln < selStartLine + 60; ++ln) {
                size_t p = 0;
                while ((p = lines[ln].find(varName, p)) != std::wstring::npos) {
                    bool lOk = (p == 0 || !IsIdentCharW(lines[ln][p - 1]));
                    bool rOk = (p + varName.size() >= lines[ln].size() || !IsIdentCharW(lines[ln][p + varName.size()]));
                    if (lOk && rOk) {
                        edits.push_back({ ln, p, ln, p + varName.size(), L"(" + initExpr + L")" });
                    }
                    p += varName.size();
                }
            }
            if (!edits.empty()) {
                // Remove declaration line
                size_t delEndLine = (selStartLine + 1 < lines.size()) ? selStartLine + 1 : selStartLine;
                size_t delEndCol = (delEndLine > selStartLine) ? 0 : lines[selStartLine].size();
                edits.push_back({ selStartLine, 0, delEndLine, delEndCol, L"" });
                phide::lsp::CodeActionItem act;
                act.title = L"Inline variable '" + varName + L"'";
                act.kind = L"refactor.inline";
                act.edit.fileChanges.push_back({ filePath, std::move(edits) });
                actions.push_back(std::move(act));
            }
        }
    }

    // --- 7. Refactoring: Arrow Function <-> Function Declaration (JS/TS) ---
    if (isJs) {
        std::wstring tl = TrimW(curLineStr);
        // `function foo(a, b) {` -> `const foo = (a, b) => {`
        if (tl.rfind(L"function ", 0) == 0 || tl.rfind(L"async function ", 0) == 0) {
            bool isAsync = (tl.rfind(L"async ", 0) == 0);
            size_t fnPos = curLineStr.find(L"function ");
            size_t lp = curLineStr.find(L'(', fnPos);
            size_t rp = (lp != std::wstring::npos) ? curLineStr.find(L')', lp) : std::wstring::npos;
            size_t lb = (rp != std::wstring::npos) ? curLineStr.find(L'{', rp) : std::wstring::npos;
            if (fnPos != std::wstring::npos && lp != std::wstring::npos && lb != std::wstring::npos) {
                std::wstring fnName = TrimW(curLineStr.substr(fnPos + 9, lp - (fnPos + 9)));
                std::wstring paramsWithRet = curLineStr.substr(lp, lb - lp);
                if (!fnName.empty()) {
                    std::wstring replaced = indent + L"const " + fnName + L" = " +
                                            (isAsync ? L"async " : L"") + TrimW(paramsWithRet) + L" => " + curLineStr.substr(lb);
                    phide::lsp::CodeActionItem act;
                    act.title = L"Convert '" + fnName + L"' to arrow function";
                    act.kind = L"refactor.rewrite.arrow";
                    act.edit.fileChanges.push_back({ filePath, { { selStartLine, 0, selStartLine, curLineStr.size(), replaced } } });
                    actions.push_back(std::move(act));
                }
            }
        } else if ((tl.rfind(L"const ", 0) == 0 || tl.rfind(L"let ", 0) == 0) && tl.find(L"=>") != std::wstring::npos) {
            // `const foo = (a, b) => {` -> `function foo(a, b) {`
            size_t kwSp = curLineStr.find(L' ');
            size_t eq = curLineStr.find(L'=', kwSp);
            size_t arrow = curLineStr.find(L"=>", eq);
            if (kwSp != std::wstring::npos && eq != std::wstring::npos && arrow != std::wstring::npos) {
                std::wstring fnName = TrimW(curLineStr.substr(kwSp + 1, eq - kwSp - 1));
                std::wstring rhs = TrimW(curLineStr.substr(eq + 1, arrow - eq - 1));
                bool isAsync = (rhs.rfind(L"async ", 0) == 0);
                if (isAsync) rhs = TrimW(rhs.substr(6));
                std::wstring afterArrow = TrimW(curLineStr.substr(arrow + 2));
                if (!fnName.empty() && !rhs.empty() && rhs.front() == L'(') {
                    std::wstring body = (!afterArrow.empty() && afterArrow.front() == L'{')
                        ? afterArrow
                        : (L"{ return " + afterArrow + L" }");
                    std::wstring replaced = indent + (isAsync ? L"async function " : L"function ") + fnName + rhs + L" " + body;
                    phide::lsp::CodeActionItem act;
                    act.title = L"Convert '" + fnName + L"' to function declaration";
                    act.kind = L"refactor.rewrite.function";
                    act.edit.fileChanges.push_back({ filePath, { { selStartLine, 0, selStartLine, curLineStr.size(), replaced } } });
                    actions.push_back(std::move(act));
                }
            }
        }
    }

    // --- 8. Refactoring: String concatenation <-> Template literal (JS/TS) ---
    if (isJs && curLineStr.find(L'+') != std::wstring::npos &&
        (curLineStr.find(L'\'') != std::wstring::npos || curLineStr.find(L'"') != std::wstring::npos)) {
        // Convert right-hand side of `= ... + ...` or selected expression containing `'...' + ident`
        size_t startRange = (hasSelection && selStartLine == selEndLine) ? selStartCol : 0;
        size_t endRange = (hasSelection && selStartLine == selEndLine) ? selEndCol : curLineStr.size();
        if (!hasSelection) {
            size_t eq = curLineStr.find(L'=');
            if (eq != std::wstring::npos) startRange = eq + 1;
            size_t semi = curLineStr.rfind(L';');
            if (semi != std::wstring::npos && semi > startRange) endRange = semi;
        }
        std::wstring slice = TrimW(curLineStr.substr(startRange, endRange - startRange));
        if (slice.find(L'+') != std::wstring::npos) {
            // Split on '+' outside quotes
            std::vector<std::wstring> parts;
            std::wstring cur;
            wchar_t q = 0;
            bool hasStringLit = false;
            for (size_t p = 0; p < slice.size(); ++p) {
                wchar_t ch = slice[p];
                if (q) {
                    cur.push_back(ch);
                    if (ch == L'\\' && p + 1 < slice.size()) { cur.push_back(slice[++p]); continue; }
                    if (ch == q) q = 0;
                } else if (ch == L'\'' || ch == L'"') {
                    q = ch;
                    hasStringLit = true;
                    cur.push_back(ch);
                } else if (ch == L'+') {
                    parts.push_back(TrimW(cur));
                    cur.clear();
                } else {
                    cur.push_back(ch);
                }
            }
            if (!TrimW(cur).empty()) parts.push_back(TrimW(cur));
            if (hasStringLit && parts.size() >= 2) {
                std::wstring tmpl = L"`";
                for (const auto& pt : parts) {
                    if (pt.size() >= 2 && ((pt.front() == L'\'' && pt.back() == L'\'') ||
                                           (pt.front() == L'"' && pt.back() == L'"'))) {
                        tmpl += pt.substr(1, pt.size() - 2);
                    } else {
                        tmpl += L"${" + pt + L"}";
                    }
                }
                tmpl += L"`";
                // Preserve leading space if converting after `=`
                size_t actualStart = curLineStr.find(slice, startRange);
                if (actualStart != std::wstring::npos) {
                    phide::lsp::CodeActionItem act;
                    act.title = L"Convert concatenation to template literal";
                    act.kind = L"refactor.rewrite.template";
                    act.edit.fileChanges.push_back({ filePath, { { selStartLine, actualStart, selStartLine, actualStart + slice.size(), tmpl } } });
                    actions.push_back(std::move(act));
                }
            }
        }
    }

    // --- 9. Refactoring: Wrap Selection / Current Line in try/catch (or try/except) ---
    {
        size_t wrapEndLine = hasSelection ? selEndLine : selStartLine;
        if (wrapEndLine < lines.size()) {
            std::wstring inner;
            for (size_t ln = selStartLine; ln <= wrapEndLine; ++ln) {
                if (ln > selStartLine) inner += nl;
                inner += L"    " + lines[ln];
            }
            std::wstring wrapped;
            if (isPy) {
                wrapped = indent + L"try:" + nl + inner + nl + indent + L"except Exception as err:" + nl + indent + L"    raise err";
            } else if (isCpp) {
                wrapped = indent + L"try {" + nl + inner + nl + indent + L"} catch (const std::exception& err) {" + nl + indent + L"    throw;" + nl + indent + L"}";
            } else {
                wrapped = indent + L"try {" + nl + inner + nl + indent + L"} catch (err) {" + nl + indent + L"    console.error(err);" + nl + indent + L"}";
            }
            phide::lsp::CodeActionItem act;
            act.title = isPy ? L"Wrap in try / except block" : L"Wrap in try / catch block";
            act.kind = L"refactor.surround.tryCatch";
            act.edit.fileChanges.push_back({ filePath, { { selStartLine, 0, wrapEndLine, lines[wrapEndLine].size(), wrapped } } });
            actions.push_back(std::move(act));
        }
    }

    return actions;
}

// ============================================================================
// #25 Implementation: InlayAndCodeLensEngine
// ============================================================================

std::vector<StaticInlayHint> InlayAndCodeLensEngine::ComputeStaticInlayHints(
    const std::wstring& filePath,
    const std::vector<std::wstring>& lines,
    size_t startLine,
    size_t endLine,
    size_t maxHints) {
    std::vector<StaticInlayHint> hints;
    if (lines.empty() || maxHints == 0) return hints;

    std::wstring ext = GetExtLower(filePath);
    bool isJs = IsJsTsExt(ext);
    bool isPy = (ext == L".py" || ext == L".pyw");
    bool isCpp = IsCppExt(ext);

    size_t sLine = std::min(startLine, lines.size());
    size_t eLine = std::min(endLine + 1, lines.size());

    // Parameter names for common known calls
    struct ParamHintSpec { const wchar_t* callPrefix; const wchar_t* p1; const wchar_t* p2; const wchar_t* p3; const wchar_t* p4; };
    static const ParamHintSpec kParamSpecs[] = {
        { L"setTimeout(", L"fn:", L"ms:", nullptr, nullptr },
        { L"setInterval(", L"fn:", L"ms:", nullptr, nullptr },
        { L"fillRect(", L"x:", L"y:", L"w:", L"h:" },
        { L"strokeRect(", L"x:", L"y:", L"w:", L"h:" },
        { L"clearRect(", L"x:", L"y:", L"w:", L"h:" },
        { L".slice(", L"start:", L"end:", nullptr, nullptr },
        { L".substring(", L"start:", L"end:", nullptr, nullptr },
        { L"Math.pow(", L"base:", L"exp:", nullptr, nullptr },
    };

    for (size_t i = sLine; i < eLine && hints.size() < maxHints; ++i) {
        const std::wstring& l = lines[i];
        std::wstring tl = TrimW(l);
        if (tl.empty() || tl.rfind(L"//", 0) == 0 || tl.rfind(L"#", 0) == 0) continue;

        // 1. Inferred variable type hints (skip if explicit `: Type` already annotated)
        if (isJs || isCpp || isPy) {
            size_t varStart = std::wstring::npos;
            size_t prefixLen = 0;
            if (isJs) {
                if (tl.rfind(L"const ", 0) == 0) { varStart = l.find(L"const ") + 6; prefixLen = 6; }
                else if (tl.rfind(L"let ", 0) == 0) { varStart = l.find(L"let ") + 4; prefixLen = 4; }
            } else if (isCpp) {
                if (tl.rfind(L"auto ", 0) == 0) { varStart = l.find(L"auto ") + 5; prefixLen = 5; }
            }
            (void)prefixLen;
            if (varStart != std::wstring::npos) {
                size_t vEnd = varStart;
                while (vEnd < l.size() && IsIdentCharW(l[vEnd])) ++vEnd;
                size_t eq = l.find(L'=', vEnd);
                size_t colon = l.find(L':', vEnd);
                if (vEnd > varStart && eq != std::wstring::npos && (colon == std::wstring::npos || colon > eq)) {
                    std::wstring rhs = TrimW(l.substr(eq + 1));
                    if (!rhs.empty() && rhs.back() == L';') rhs.pop_back();
                    rhs = TrimW(rhs);
                    std::wstring inferred;
                    if (!rhs.empty() && (rhs[0] == L'"' || rhs[0] == L'\'' || rhs[0] == L'`')) inferred = L": string";
                    else if (rhs == L"true" || rhs == L"false") inferred = L": boolean";
                    else if (!rhs.empty() && (std::iswdigit(rhs[0]) || (rhs[0] == L'-' && rhs.size() > 1 && std::iswdigit(rhs[1])))) {
                        inferred = isCpp ? L": double/int" : L": number";
                    } else if (!rhs.empty() && rhs[0] == L'[') {
                        inferred = L": Array";
                    } else if (rhs.rfind(L"new ", 0) == 0) {
                        size_t lp = rhs.find(L'(');
                        if (lp != std::wstring::npos && lp > 4) {
                            inferred = L": " + TrimW(rhs.substr(4, lp - 4));
                        }
                    } else if (rhs.rfind(L"useState(", 0) == 0) {
                        inferred = L": [state, setState]";
                    }
                    if (!inferred.empty()) {
                        hints.push_back({ i, vEnd, inferred, 1 });
                    }
                }
            }
        }

        // 2. Parameter name hints for well-known calls with literal args
        for (const auto& spec : kParamSpecs) {
            if (hints.size() >= maxHints) break;
            size_t callPos = l.find(spec.callPrefix);
            if (callPos == std::wstring::npos) continue;
            size_t openParen = callPos + wcslen(spec.callPrefix) - 1;
            // Find matching closing paren on same line
            int depth = 1;
            wchar_t q = 0;
            size_t argStarts[4]{ openParen + 1, 0, 0, 0 };
            int argCount = 1;
            size_t scan = openParen + 1;
            while (scan < l.size() && depth > 0) {
                wchar_t c = l[scan];
                if (q) {
                    if (c == L'\\' && scan + 1 < l.size()) { scan += 2; continue; }
                    if (c == q) q = 0;
                } else {
                    if (c == L'\'' || c == L'"' || c == L'`') q = c;
                    else if (c == L'(') ++depth;
                    else if (c == L')') --depth;
                    else if (c == L',' && depth == 1 && argCount < 4) {
                        argStarts[argCount++] = scan + 1;
                    }
                }
                ++scan;
            }
            const wchar_t* labels[] = { spec.p1, spec.p2, spec.p3, spec.p4 };
            for (int a = 0; a < argCount && a < 4 && hints.size() < maxHints; ++a) {
                if (!labels[a]) continue;
                size_t col = argStarts[a];
                while (col < l.size() && (l[col] == L' ' || l[col] == L'\t')) ++col;
                if (col < l.size() && l[col] != L')') {
                    hints.push_back({ i, col, labels[a], 2 });
                }
            }
        }
    }
    return hints;
}

std::vector<CodeLensSymbol> InlayAndCodeLensEngine::ScanCodeLensEntries(
    const std::wstring& filePath,
    const std::vector<std::wstring>& lines,
    const std::vector<std::wstring>& peerBuffersText,
    size_t maxEntries) {
    std::vector<CodeLensSymbol> entries;
    if (lines.empty() || maxEntries == 0) return entries;

    std::wstring ext = GetExtLower(filePath);
    bool isJs = IsJsTsExt(ext);
    bool isPy = (ext == L".py" || ext == L".pyw");
    bool isPhp = (ext == L".php");
    bool isCpp = IsCppExt(ext);

    std::wstring joinedSelf;
    for (const auto& l : lines) {
        joinedSelf += l;
        joinedSelf.push_back(L'\n');
    }

    for (size_t i = 0; i < lines.size() && entries.size() < maxEntries; ++i) {
        std::wstring tl = TrimW(lines[i]);
        if (tl.empty() || tl.rfind(L"//", 0) == 0 || tl.rfind(L"#", 0) == 0) continue;

        // Test block detection (Jest/Vitest `describe`/`it`/`test`, Python `def test_`, GTest `TEST(`)
        if ((isJs && (tl.rfind(L"it(", 0) == 0 || tl.rfind(L"test(", 0) == 0 || tl.rfind(L"describe(", 0) == 0)) ||
            (isCpp && (tl.rfind(L"TEST(", 0) == 0 || tl.rfind(L"TEST_F(", 0) == 0))) {
            entries.push_back({ i, L"", 0, false, true });
            continue;
        }

        std::wstring sym;
        bool isMain = false;
        if (isJs) {
            static const wchar_t* kDecls[] = {
                L"export default async function ", L"export default function ",
                L"export async function ", L"export function ", L"async function ", L"function ",
                L"export class ", L"class ", L"export interface ", L"interface "
            };
            for (const wchar_t* d : kDecls) {
                if (tl.rfind(d, 0) == 0) {
                    std::wstring rem = TrimW(tl.substr(wcslen(d)));
                    size_t end = 0;
                    while (end < rem.size() && IsIdentCharW(rem[end])) ++end;
                    if (end > 0) sym = rem.substr(0, end);
                    break;
                }
            }
            if (sym.empty() && (tl.rfind(L"export const ", 0) == 0 || tl.rfind(L"const ", 0) == 0) &&
                tl.find(L"=>") != std::wstring::npos) {
                size_t kwSp = tl.find(L"const ") + 6;
                size_t end = kwSp;
                while (end < tl.size() && IsIdentCharW(tl[end])) ++end;
                if (end > kwSp) sym = tl.substr(kwSp, end - kwSp);
            }
            isMain = (sym == L"main");
        } else if (isPy) {
            if (tl == L"if __name__ == '__main__':" || tl == L"if __name__ == \"__main__\":") {
                entries.push_back({ i, L"__main__", 0, true, false });
                continue;
            }
            for (const wchar_t* d : { L"async def ", L"def ", L"class " }) {
                if (tl.rfind(d, 0) == 0) {
                    std::wstring rem = TrimW(tl.substr(wcslen(d)));
                    size_t end = 0;
                    while (end < rem.size() && IsIdentCharW(rem[end])) ++end;
                    if (end > 0) sym = rem.substr(0, end);
                    break;
                }
            }
            isMain = (sym == L"main");
        } else if (isPhp) {
            for (const wchar_t* d : { L"public static function ", L"public function ", L"protected function ", L"private function ", L"function ", L"class " }) {
                if (tl.rfind(d, 0) == 0) {
                    std::wstring rem = TrimW(tl.substr(wcslen(d)));
                    size_t end = 0;
                    while (end < rem.size() && IsIdentCharW(rem[end])) ++end;
                    if (end > 0) sym = rem.substr(0, end);
                    break;
                }
            }
        } else if (isCpp && !lines[i].empty() && !std::iswspace(lines[i][0])) {
            size_t lp = tl.find(L'(');
            if (lp != std::wstring::npos && (tl.find(L'{') != std::wstring::npos || (i + 1 < lines.size() && TrimW(lines[i + 1]).rfind(L"{", 0) == 0))) {
                size_t end = lp;
                while (end > 0 && std::iswspace(tl[end - 1])) --end;
                size_t start = end;
                while (start > 0 && IsIdentCharW(tl[start - 1])) --start;
                if (end > start) sym = tl.substr(start, end - start);
                isMain = (sym == L"main");
            }
        }

        if (!sym.empty()) {
            bool isTestFn = StartsWithNoCase(sym, L"test_") || StartsWithNoCase(sym, L"test");
            if (isTestFn && sym.size() <= 4) isTestFn = false;
            int totalOcc = CountWholeWordOccurrences(joinedSelf, sym);
            for (const auto& peer : peerBuffersText) {
                totalOcc += CountWholeWordOccurrences(peer, sym);
            }
            int refs = (totalOcc > 0) ? (totalOcc - 1) : 0;
            entries.push_back({ i, std::move(sym), refs, isMain, isTestFn });
        }
    }

    return entries;
}

} // namespace phide::editor
