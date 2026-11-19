/**
 * @file SyntaxValidator.cpp
 * @brief High-performance multi-language syntax validator implementation.
 */

#include <windows.h>
#include "phide/editor/SyntaxValidator.hpp"
#include "phide/editor/WebIntelliSenseData.hpp"
#include "phide/plugin/PluginManager.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <stack>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace phide::editor {

namespace {

[[nodiscard]] bool IsIdentStartSv(wchar_t c) noexcept {
    return iswalpha(c) || c == L'_';
}

[[nodiscard]] bool IsIdentCharSv(wchar_t c) noexcept {
    return iswalnum(c) || c == L'_';
}

/**
 * @brief Counts identifier token occurrences across a Python buffer outside strings and comments.
 *        Also extracts identifiers referenced inside `__all__ = [...]`.
 */
void CollectPythonIdentCounts(
    const Buffer& buffer,
    std::unordered_map<std::wstring, size_t>& outCounts,
    std::unordered_set<std::wstring>& outExportedAll) {
    bool inTriple = false;
    wchar_t tCh = 0;
    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        bool inSgl = false;
        wchar_t sCh = 0;
        const bool isAllLine = (line.find(L"__all__") != std::wstring::npos);
        for (size_t c = 0; c < line.size();) {
            wchar_t ch = line[c];
            if (inTriple) {
                if (ch == tCh && c + 2 < line.size() && line[c + 1] == tCh && line[c + 2] == tCh) {
                    inTriple = false;
                    c += 3;
                } else {
                    ++c;
                }
                continue;
            }
            if (inSgl) {
                if (ch == L'\\') { c += 2; continue; }
                if (ch == sCh) { inSgl = false; ++c; continue; }
                if (isAllLine && IsIdentStartSv(ch)) {
                    size_t st = c;
                    while (c < line.size() && IsIdentCharSv(line[c])) ++c;
                    outExportedAll.insert(line.substr(st, c - st));
                    continue;
                }
                ++c;
                continue;
            }
            if ((ch == L'"' || ch == L'\'') && c + 2 < line.size() && line[c + 1] == ch && line[c + 2] == ch) {
                inTriple = true;
                tCh = ch;
                c += 3;
                continue;
            }
            if (ch == L'"' || ch == L'\'') {
                inSgl = true;
                sCh = ch;
                ++c;
                continue;
            }
            if (ch == L'#') break;
            if (IsIdentStartSv(ch)) {
                size_t st = c++;
                while (c < line.size() && IsIdentCharSv(line[c])) ++c;
                outCounts[line.substr(st, c - st)]++;
                continue;
            }
            ++c;
        }
    }
}

void AppendPythonUnusedDiagnostics(const Buffer& buffer, std::vector<lsp::Diagnostic>& diagnostics) {
    static const std::unordered_set<std::wstring> kSkip = {
        L"self", L"cls", L"_", L"__all__", L"__version__", L"__author__", L"True", L"False", L"None",
        L"if", L"else", L"elif", L"for", L"while", L"in", L"return", L"import", L"from", L"as", L"with",
        L"def", L"class", L"try", L"except", L"finally", L"raise", L"pass", L"break", L"continue", L"global", L"nonlocal"
    };

    std::unordered_map<std::wstring, size_t> counts;
    std::unordered_set<std::wstring> exportedAll;
    CollectPythonIdentCounts(buffer, counts, exportedAll);

    struct ImportBinding {
        std::wstring bound;
        size_t itemCol = 0;
        size_t itemLen = 0;
    };

    int pyParenDepth = 0;
    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& raw = buffer.GetLine(l);
        if (raw.find(L"# noqa") != std::wstring::npos) {
            for (wchar_t ch : raw) {
                if (ch == L'(' || ch == L'[' || ch == L'{') ++pyParenDepth;
                else if ((ch == L')' || ch == L']' || ch == L'}') && pyParenDepth > 0) --pyParenDepth;
            }
            continue;
        }

        std::wstring code = raw;
        {
            bool inQ = false;
            wchar_t qCh = 0;
            for (size_t i = 0; i < raw.size(); ++i) {
                wchar_t ch = raw[i];
                if (!inQ && (ch == L'"' || ch == L'\'')) { inQ = true; qCh = ch; }
                else if (inQ && ch == L'\\') { ++i; }
                else if (inQ && ch == qCh) { inQ = false; }
                else if (!inQ && ch == L'#') { code = raw.substr(0, i); break; }
            }
        }
        size_t lead = 0;
        while (lead < code.size() && (code[lead] == L' ' || code[lead] == L'\t')) ++lead;
        size_t codeEnd = code.size();
        while (codeEnd > lead && (code[codeEnd - 1] == L' ' || code[codeEnd - 1] == L'\t')) --codeEnd;
        if (codeEnd <= lead) continue;
        std::wstring trimmed = code.substr(lead, codeEnd - lead);

        if (trimmed.rfind(L"import ", 0) == 0 || trimmed.rfind(L"from ", 0) == 0) {
            if (trimmed.find(L"__future__") != std::wstring::npos || trimmed.find(L'*') != std::wstring::npos) {
                continue;
            }
            std::vector<ImportBinding> items;
            size_t parseStart = 0;
            if (trimmed.rfind(L"from ", 0) == 0) {
                size_t impPos = trimmed.find(L" import ");
                if (impPos == std::wstring::npos) continue;
                parseStart = lead + impPos + 8;
            } else {
                parseStart = lead + 7;
            }

            size_t p = parseStart;
            while (p < codeEnd) {
                while (p < codeEnd && (code[p] == L' ' || code[p] == L',' || code[p] == L'(' || code[p] == L')')) ++p;
                if (p >= codeEnd) break;
                size_t itemStart = p;
                size_t modStart = p;
                while (p < codeEnd && (IsIdentCharSv(code[p]) || code[p] == L'.')) ++p;
                if (p == modStart) { ++p; continue; }
                std::wstring fullMod = code.substr(modStart, p - modStart);
                size_t dotPos = fullMod.find(L'.');
                std::wstring bound = (dotPos != std::wstring::npos && trimmed.rfind(L"import ", 0) == 0)
                    ? fullMod.substr(0, dotPos) : fullMod;

                size_t sp = p;
                while (sp < codeEnd && code[sp] == L' ') ++sp;
                if (sp + 3 <= codeEnd && code.compare(sp, 3, L"as ") == 0) {
                    p = sp + 3;
                    while (p < codeEnd && code[p] == L' ') ++p;
                    size_t alStart = p;
                    while (p < codeEnd && IsIdentCharSv(code[p])) ++p;
                    if (p > alStart) bound = code.substr(alStart, p - alStart);
                }
                if (!bound.empty() && kSkip.find(bound) == kSkip.end()) {
                    items.push_back({ bound, itemStart, p - itemStart });
                }
            }

            if (items.empty()) continue;
            std::vector<size_t> unusedIdxs;
            for (size_t idx = 0; idx < items.size(); ++idx) {
                const auto& it = items[idx];
                if (exportedAll.count(it.bound)) continue;
                if (counts[it.bound] <= 1) {
                    unusedIdxs.push_back(idx);
                }
            }
            if (unusedIdxs.size() == items.size()) {
                lsp::Diagnostic d;
                d.line = l;
                d.col = lead;
                d.length = codeEnd - lead;
                d.severity = 4;
                d.source = L"Python";
                d.code = L"unused-import";
                d.isUnnecessary = true;
                d.message = L"Python: '" + items[0].bound + L"' is imported but unused";
                diagnostics.push_back(std::move(d));
            } else {
                for (size_t uidx : unusedIdxs) {
                    const auto& it = items[uidx];
                    lsp::Diagnostic d;
                    d.line = l;
                    d.col = it.itemCol;
                    d.length = (std::max<size_t>)(1, it.itemLen);
                    d.severity = 4;
                    d.source = L"Python";
                    d.code = L"unused-import";
                    d.isUnnecessary = true;
                    d.message = L"Python: '" + it.bound + L"' is imported but unused";
                    diagnostics.push_back(std::move(d));
                }
            }
            continue;
        }

        const int lineStartDepth = pyParenDepth;
        for (size_t i = lead; i < codeEnd; ++i) {
            wchar_t ch = code[i];
            if (ch == L'(' || ch == L'[' || ch == L'{') ++pyParenDepth;
            else if ((ch == L')' || ch == L']' || ch == L'}') && pyParenDepth > 0) --pyParenDepth;
        }

        if (lineStartDepth == 0 && IsIdentStartSv(code[lead]) && code[codeEnd - 1] != L',') {
            size_t p = lead;
            while (p < codeEnd && IsIdentCharSv(code[p])) ++p;
            std::wstring lhs = code.substr(lead, p - lead);
            if (!lhs.empty() && lhs[0] != L'_' && kSkip.find(lhs) == kSkip.end() && !exportedAll.count(lhs)) {
                bool allUpper = true;
                for (wchar_t uc : lhs) {
                    if (iswalpha(uc) && !iswupper(uc)) { allUpper = false; break; }
                }
                if (!allUpper) {
                    size_t q = p;
                    while (q < codeEnd && (code[q] == L' ' || code[q] == L'\t')) ++q;
                    if (q < codeEnd && code[q] == L':') {
                        size_t eqPos = code.find(L'=', q + 1);
                        if (eqPos != std::wstring::npos && eqPos < codeEnd) q = eqPos;
                    }
                    if (q < codeEnd && code[q] == L'=' && (q + 1 >= codeEnd || code[q + 1] != L'=')) {
                        if (counts[lhs] <= 1) {
                            lsp::Diagnostic d;
                            d.line = l;
                            d.col = lead;
                            d.length = lhs.size();
                            d.severity = 4;
                            d.source = L"Python";
                            d.code = L"unused-variable";
                            d.isUnnecessary = true;
                            d.message = L"Python: '" + lhs + L"' is assigned but never used";
                            diagnostics.push_back(std::move(d));
                        }
                    }
                }
            }
        }
    }
}

void AppendJsUnusedDiagnostics(const Buffer& buffer, size_t startLine, size_t endLine, bool isJsx,
                               std::vector<lsp::Diagnostic>& diagnostics) {
    const size_t lineCount = buffer.GetLineCount();
    const size_t effEnd = (std::min)(endLine, lineCount);
    if (startLine != 0 || effEnd < lineCount || effEnd == 0) return;
    std::unordered_map<std::wstring, size_t> counts;
    bool inBlk = false;
    for (size_t l = 0; l < effEnd; ++l) {
        const auto& line = buffer.GetLine(l);
        bool inS = false;
        wchar_t sCh = 0;
        for (size_t c = 0; c < line.size();) {
            wchar_t ch = line[c];
            if (inBlk) {
                if (ch == L'*' && c + 1 < line.size() && line[c + 1] == L'/') { inBlk = false; c += 2; }
                else ++c;
                continue;
            }
            if (inS) {
                if (ch == L'\\') { c += 2; continue; }
                if (ch == sCh) inS = false;
                ++c;
                continue;
            }
            if (ch == L'/' && c + 1 < line.size() && line[c + 1] == L'/') break;
            if (ch == L'/' && c + 1 < line.size() && line[c + 1] == L'*') { inBlk = true; c += 2; continue; }
            if (ch == L'"' || ch == L'\'' || ch == L'`') { inS = true; sCh = ch; ++c; continue; }
            if (iswalpha(ch) || ch == L'_' || ch == L'$') {
                size_t st = c++;
                while (c < line.size() && (iswalnum(line[c]) || line[c] == L'_' || line[c] == L'$')) ++c;
                counts[line.substr(st, c - st)]++;
                continue;
            }
            ++c;
        }
    }

    for (size_t l = 0; l < effEnd; ++l) {
        const auto& raw = buffer.GetLine(l);
        size_t lead = 0;
        while (lead < raw.size() && iswspace(raw[lead])) ++lead;
        if (lead >= raw.size()) continue;
        std::wstring trimmed = raw.substr(lead);
        if (trimmed.rfind(L"export ", 0) == 0) continue;

        if (trimmed.rfind(L"import ", 0) == 0) {
            size_t fromPos = trimmed.find(L" from ");
            if (fromPos == std::wstring::npos) continue;
            std::wstring clause = trimmed.substr(7, fromPos - 7);
            std::vector<std::pair<std::wstring, std::pair<size_t, size_t>>> bindings;
            size_t bOpen = clause.find(L'{');
            size_t bClose = (bOpen != std::wstring::npos) ? clause.find(L'}', bOpen) : std::wstring::npos;
            size_t defEnd = (bOpen != std::wstring::npos) ? bOpen : clause.size();
            {
                std::wstring head = clause.substr(0, defEnd);
                size_t asPos = head.find(L" as ");
                if (asPos != std::wstring::npos) {
                    size_t p = asPos + 4;
                    while (p < head.size() && iswspace(head[p])) ++p;
                    size_t st = p;
                    while (p < head.size() && (iswalnum(head[p]) || head[p] == L'_' || head[p] == L'$')) ++p;
                    if (p > st) bindings.push_back({ head.substr(st, p - st), { lead + 7 + st, p - st } });
                } else {
                    size_t p = 0;
                    while (p < head.size() && (iswspace(head[p]) || head[p] == L',')) ++p;
                    size_t st = p;
                    while (p < head.size() && (iswalnum(head[p]) || head[p] == L'_' || head[p] == L'$')) ++p;
                    std::wstring nm = (p > st) ? head.substr(st, p - st) : L"";
                    if (!nm.empty() && nm != L"type") {
                        bindings.push_back({ nm, { lead + 7 + st, p - st } });
                    }
                }
            }
            if (bOpen != std::wstring::npos && bClose != std::wstring::npos) {
                size_t p = bOpen + 1;
                while (p < bClose) {
                    while (p < bClose && (iswspace(clause[p]) || clause[p] == L',')) ++p;
                    if (p >= bClose) break;
                    size_t itemStart = p;
                    size_t st = p;
                    while (p < bClose && (iswalnum(clause[p]) || clause[p] == L'_' || clause[p] == L'$')) ++p;
                    if (p == st) { ++p; continue; }
                    std::wstring sym = clause.substr(st, p - st);
                    while (p < bClose && iswspace(clause[p])) ++p;
                    if (p + 3 <= bClose && clause.compare(p, 3, L"as ") == 0) {
                        p += 3;
                        while (p < bClose && iswspace(clause[p])) ++p;
                        size_t alS = p;
                        while (p < bClose && (iswalnum(clause[p]) || clause[p] == L'_' || clause[p] == L'$')) ++p;
                        if (p > alS) sym = clause.substr(alS, p - alS);
                    }
                    if (!sym.empty() && sym != L"type") {
                        bindings.push_back({ sym, { lead + 7 + itemStart, p - itemStart } });
                    }
                }
            }
            if (bindings.empty()) continue;
            size_t unusedCnt = 0;
            for (const auto& b : bindings) {
                if (isJsx && b.first == L"React") continue;
                if (b.first[0] != L'_' && counts[b.first] <= 1) ++unusedCnt;
            }
            if (unusedCnt > 0 && unusedCnt == bindings.size()) {
                size_t lineEnd = raw.size();
                while (lineEnd > lead && iswspace(raw[lineEnd - 1])) --lineEnd;
                diagnostics.push_back({ l, lead, lineEnd - lead,
                    L"JS/TS: '" + bindings[0].first + L"' is declared but never used",
                    4, L"JS/TS", L"unused-import", true });
            } else {
                for (const auto& b : bindings) {
                    if (isJsx && b.first == L"React") continue;
                    if (b.first[0] != L'_' && counts[b.first] <= 1) {
                        diagnostics.push_back({ l, b.second.first, (std::max<size_t>)(1, b.second.second),
                            L"JS/TS: '" + b.first + L"' is declared but never used",
                            4, L"JS/TS", L"unused-import", true });
                    }
                }
            }
            continue;
        }

        size_t kwLen = 0;
        if (trimmed.rfind(L"const ", 0) == 0) kwLen = 6;
        else if (trimmed.rfind(L"let ", 0) == 0) kwLen = 4;
        else if (trimmed.rfind(L"var ", 0) == 0) kwLen = 4;
        if (kwLen > 0) {
            size_t p = kwLen;
            while (p < trimmed.size() && iswspace(trimmed[p])) ++p;
            if (p < trimmed.size() && (iswalpha(trimmed[p]) || trimmed[p] == L'_' || trimmed[p] == L'$')) {
                size_t st = p++;
                while (p < trimmed.size() && (iswalnum(trimmed[p]) || trimmed[p] == L'_' || trimmed[p] == L'$')) ++p;
                std::wstring vname = trimmed.substr(st, p - st);
                if (!vname.empty() && vname[0] != L'_' && counts[vname] <= 1) {
                    size_t q = p;
                    while (q < trimmed.size() && iswspace(trimmed[q])) ++q;
                    if (q < trimmed.size() && (trimmed[q] == L'=' || trimmed[q] == L':')) {
                        diagnostics.push_back({ l, lead + st, vname.size(),
                            L"JS/TS: '" + vname + L"' is declared but its value is never read",
                            4, L"JS/TS", L"unused-var", true });
                    }
                }
            }
        }
    }
}

void AppendPhpUnusedDiagnostics(const Buffer& buffer, std::vector<lsp::Diagnostic>& diagnostics) {
    static const std::unordered_set<std::wstring> kSkipDollar = {
        L"$this", L"$_GET", L"$_POST", L"$_SERVER", L"$_SESSION", L"$_ENV",
        L"$_COOKIE", L"$_FILES", L"$_REQUEST", L"$GLOBALS", L"$_"
    };
    std::unordered_map<std::wstring, size_t> varCounts;
    std::unordered_map<std::wstring, size_t> classUseCounts;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        for (size_t c = 0; c < line.size();) {
            if (line[c] == L'$' && c + 1 < line.size() && (iswalpha(line[c + 1]) || line[c + 1] == L'_')) {
                size_t st = c++;
                while (c < line.size() && IsIdentCharSv(line[c])) ++c;
                varCounts[line.substr(st, c - st)]++;
                continue;
            }
            if (IsIdentStartSv(line[c])) {
                size_t st = c++;
                while (c < line.size() && IsIdentCharSv(line[c])) ++c;
                classUseCounts[line.substr(st, c - st)]++;
                continue;
            }
            ++c;
        }
    }

    bool inPhp = false;
    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        if (line.find(L"<?php") != std::wstring::npos || line.find(L"<?=") != std::wstring::npos) inPhp = true;
        if (!inPhp) continue;
        size_t lead = 0;
        while (lead < line.size() && iswspace(line[lead])) ++lead;
        if (lead >= line.size()) continue;
        std::wstring trimmed = line.substr(lead);

        if (trimmed.rfind(L"use ", 0) == 0 && trimmed.find(L'(') == std::wstring::npos) {
            size_t semi = trimmed.find(L';');
            if (semi != std::wstring::npos) {
                std::wstring body = trimmed.substr(4, semi - 4);
                size_t asPos = body.find(L" as ");
                std::wstring sym;
                if (asPos != std::wstring::npos) {
                    sym = body.substr(asPos + 4);
                } else {
                    size_t sl = body.find_last_of(L'\\');
                    sym = (sl != std::wstring::npos) ? body.substr(sl + 1) : body;
                }
                while (!sym.empty() && iswspace(sym.front())) sym.erase(sym.begin());
                while (!sym.empty() && iswspace(sym.back())) sym.pop_back();
                if (!sym.empty() && sym[0] != L'_' && classUseCounts[sym] <= 1) {
                    diagnostics.push_back({ l, lead, semi + 1,
                        L"PHP: Symbol '" + sym + L"' is imported via 'use' but never used",
                        4, L"PHP", L"unused-use", true });
                }
            }
        } else if (trimmed[0] == L'$') {
            size_t p = 1;
            while (p < trimmed.size() && IsIdentCharSv(trimmed[p])) ++p;
            std::wstring v = trimmed.substr(0, p);
            if (v.size() > 1 && v[1] != L'_' && kSkipDollar.find(v) == kSkipDollar.end()) {
                size_t q = p;
                while (q < trimmed.size() && iswspace(trimmed[q])) ++q;
                if (q < trimmed.size() && trimmed[q] == L'=' && (q + 1 >= trimmed.size() || trimmed[q + 1] != L'=')) {
                    if (varCounts[v] <= 1) {
                        diagnostics.push_back({ l, lead, v.size(),
                            L"PHP: Variable '" + v + L"' is assigned but never used",
                            4, L"PHP", L"unused-var", true });
                    }
                }
            }
        }
        if (line.find(L"?>") != std::wstring::npos) inPhp = false;
    }
}

bool IsJsRegexStart(const std::wstring& str, size_t pos) {
    if (pos >= str.length() || str[pos] != L'/') return false;
    // JSX closing tag '</' or self-closing '/>' or comment '//' or '/*' is NEVER a regex
    if (pos > 0 && str[pos - 1] == L'<') return false;
    if (pos + 1 < str.length() && (str[pos + 1] == L'>' || str[pos + 1] == L'/' || str[pos + 1] == L'*')) return false;

    // Find preceding non-whitespace character and preceding token
    int p = static_cast<int>(pos) - 1;
    while (p >= 0 && iswspace(str[p])) p--;
    if (p < 0) return true; // Start of line

    wchar_t prevChar = str[p];
    if (prevChar == L'(' || prevChar == L'[' || prevChar == L'{' ||
        prevChar == L',' || prevChar == L';' || prevChar == L':' ||
        prevChar == L'=' || prevChar == L'!' || prevChar == L'?' ||
        prevChar == L'&' || prevChar == L'|' || prevChar == L'+' ||
        prevChar == L'-' || prevChar == L'*' || prevChar == L'%' ||
        prevChar == L'^' || prevChar == L'~') {
        return true;
    }
    // Check for arrow function '=>'
    if (prevChar == L'>' && p > 0 && str[p - 1] == L'=') {
        return true;
    }
    // Check if preceding token is a keyword: return, case, default, yield, await, typeof, void, delete, in, instanceof, do, else
    if (iswalnum(prevChar) || prevChar == L'_' || prevChar == L'$') {
        int wordEnd = p;
        int wordStart = p;
        while (wordStart >= 0 && (iswalnum(str[wordStart]) || str[wordStart] == L'_' || str[wordStart] == L'$')) {
            wordStart--;
        }
        wordStart++;
        std::wstring word = str.substr(wordStart, wordEnd - wordStart + 1);
        if (word == L"return" || word == L"case" || word == L"default" ||
            word == L"yield" || word == L"await" || word == L"typeof" ||
            word == L"void" || word == L"delete" || word == L"in" ||
            word == L"instanceof" || word == L"do" || word == L"else") {
            return true;
        }
    }
    return false;
}

size_t ScanJsRegexEnd(const std::wstring& str, size_t pos) {
    if (pos >= str.length() || str[pos] != L'/') return std::wstring::npos;
    bool inCharClass = false;
    size_t r = pos + 1;
    for (; r < str.length(); ++r) {
        if (str[r] == L'\\') {
            r++; // skip escaped char
            continue;
        }
        if (str[r] == L'[' && !inCharClass) {
            inCharClass = true;
        } else if (str[r] == L']' && inCharClass) {
            inCharClass = false;
        } else if (str[r] == L'/' && !inCharClass) {
            break;
        }
    }
    if (r < str.length() && str[r] == L'/') {
        // Regex successfully matched: consume regex and its flags (g, i, m, s, u, y, d, v)
        while (r + 1 < str.length() && iswalpha(str[r + 1])) {
            r++;
        }
        return r;
    }
    return std::wstring::npos;
}

} // anonymous namespace

std::vector<lsp::Diagnostic> SyntaxValidator::Validate(
    const std::wstring& filePath,
    const Buffer& buffer,
    bool validateScripts,
    bool validateStyles
) {
    if (buffer.GetLineCount() == 0) return {};

    std::wstring ext;
    try {
        std::filesystem::path p(filePath);
        ext = p.extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    } catch (...) {}

    std::vector<lsp::Diagnostic> diags;
    if (ext == L".json" || ext == L".jsonc" || ext == L".eslintrc" || ext == L".prettierrc") {
        diags = ValidateJson(buffer);
        auto schemaDiags = ValidateJsonSchema(filePath, buffer);
        diags.insert(diags.end(), schemaDiags.begin(), schemaDiags.end());
    } else if (ext == L".css" || ext == L".scss" || ext == L".less") {
        diags = ValidateCss(buffer);
    } else if (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" || ext == L".mjs" || ext == L".cjs") {
        bool isJsx = (ext == L".jsx" || ext == L".tsx");
        diags = ValidateJavaScript(buffer, 0, static_cast<size_t>(-1), isJsx);
        auto reactDiags = ValidateReactAndESLint(buffer, isJsx);
        diags.insert(diags.end(), reactDiags.begin(), reactDiags.end());
    } else if (ext == L".html" || ext == L".htm" || ext == L".xml" || ext == L".svg") {
        diags = ValidateHtml(buffer, validateScripts, validateStyles);
    } else if (ext == L".php") {
        diags = ValidatePhp(buffer);
        auto htmlDiags = ValidateHtml(buffer, validateScripts, validateStyles);
        diags.insert(diags.end(), htmlDiags.begin(), htmlDiags.end());
    } else if (ext == L".py") {
        diags = ValidatePython(buffer);
    } else if (ext == L".yml" || ext == L".yaml") {
        diags = ValidateYaml(buffer);
        auto yamlSchemaDiags = ValidateYamlSchema(filePath, buffer);
        diags.insert(diags.end(), yamlSchemaDiags.begin(), yamlSchemaDiags.end());
    } else if (ext == L".cpp" || ext == L".c" || ext == L".cc" || ext == L".cxx" ||
               ext == L".h" || ext == L".hpp" || ext == L".hxx" || ext == L".cs" ||
               ext == L".java" || ext == L".rs" || ext == L".go") {
        diags = ValidateBracketsAndQuotes(buffer, false);
    }

    auto pluginDiags = phide::plugin::PluginManager::Instance().RunDiagnostics(filePath, ext, buffer);
    if (!pluginDiags.empty()) {
        diags.insert(diags.end(), pluginDiags.begin(), pluginDiags.end());
    }
    return diags;
}

std::vector<lsp::Diagnostic> SyntaxValidator::CollectUnusedSymbolHints(
    const std::wstring& filePath,
    const Buffer& buffer
) {
    if (buffer.GetLineCount() == 0) return {};

    std::wstring ext;
    try {
        std::filesystem::path p(filePath);
        ext = p.extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    } catch (...) {}

    std::vector<lsp::Diagnostic> hints;
    if (ext == L".py") {
        AppendPythonUnusedDiagnostics(buffer, hints);
    } else if (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" || ext == L".mjs" || ext == L".cjs") {
        bool isJsx = (ext == L".jsx" || ext == L".tsx");
        AppendJsUnusedDiagnostics(buffer, 0, static_cast<size_t>(-1), isJsx, hints);
    } else if (ext == L".php") {
        AppendPhpUnusedDiagnostics(buffer, hints);
    }
    return hints;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateJson(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct BracketInfo {
        wchar_t ch;
        size_t line;
        size_t col;
        bool isObject;
    };
    std::vector<BracketInfo> bracketStack;

    bool inString = false;
    size_t strStartLine = 0;
    size_t strStartCol = 0;
    bool hasLastComma = false;
    size_t commaLine = 0;
    size_t commaCol = 0;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        for (size_t c = 0; c < line.length(); ++c) {
            wchar_t ch = line[c];

            if (inString) {
                if (ch == L'\\') {
                    c++; // Skip escaped char
                    continue;
                }
                if (ch == L'"') {
                    inString = false;
                }
                continue;
            }

            // Single line comment in JSONC
            if (ch == L'/' && c + 1 < line.length() && line[c + 1] == L'/') {
                break;
            }

            if (ch == L'"') {
                inString = true;
                strStartLine = l;
                strStartCol = c;
                hasLastComma = false;
                continue;
            }

            if (ch == L',') {
                hasLastComma = true;
                commaLine = l;
                commaCol = c;
                continue;
            }

            if (iswspace(ch)) {
                continue;
            }

            if (ch == L'{') {
                bracketStack.push_back({ ch, l, c, true });
                hasLastComma = false;
            } else if (ch == L'[') {
                bracketStack.push_back({ ch, l, c, false });
                hasLastComma = false;
            } else if (ch == L'}') {
                if (hasLastComma) {
                    diagnostics.push_back({ commaLine, commaCol, 1, L"Trailing comma is not allowed in JSON", 1 });
                    hasLastComma = false;
                }
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, L"Unexpected closing brace '}'", 1 });
                } else if (!bracketStack.back().isObject) {
                    diagnostics.push_back({ l, c, 1, L"Mismatched closing brace: expected ']' but found '}'", 1 });
                    bracketStack.pop_back();
                } else {
                    bracketStack.pop_back();
                }
            } else if (ch == L']') {
                if (hasLastComma) {
                    diagnostics.push_back({ commaLine, commaCol, 1, L"Trailing comma is not allowed in JSON array", 1 });
                    hasLastComma = false;
                }
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, L"Unexpected closing bracket ']'", 1 });
                } else if (bracketStack.back().isObject) {
                    diagnostics.push_back({ l, c, 1, L"Mismatched closing bracket: expected '}' but found ']'", 1 });
                    bracketStack.pop_back();
                } else {
                    bracketStack.pop_back();
                }
            } else {
                hasLastComma = false;
            }
        }

        if (inString) {
            // String didn't close before end of line in JSON
            diagnostics.push_back({ strStartLine, strStartCol, (std::max)(1ULL, line.length() - strStartCol), L"Unterminated string literal in JSON", 1 });
            inString = false;
        }
    }

    while (!bracketStack.empty()) {
        const auto& b = bracketStack.back();
        std::wstring msg = b.isObject ? L"Unclosed brace '{' (missing matching '}')" : L"Unclosed bracket '[' (missing matching ']')";
        diagnostics.push_back({ b.line, b.col, 1, msg, 1 });
        bracketStack.pop_back();
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateBracketsAndQuotes(const Buffer& buffer, bool isPhp) {
    (void)isPhp;
    std::vector<lsp::Diagnostic> diagnostics;

    struct OpenBracket {
        wchar_t ch;
        size_t line;
        size_t col;
    };
    std::vector<OpenBracket> bracketStack;

    bool inBlockComment = false;
    size_t blockCommentLine = 0;
    size_t blockCommentCol = 0;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        bool inDoubleQuote = false;
        bool inSingleQuote = false;
        size_t quoteStartCol = 0;

        for (size_t c = 0; c < line.length(); ++c) {
            wchar_t ch = line[c];

            // Inside multi-line block comment /* ... */
            if (inBlockComment) {
                if (ch == L'*' && c + 1 < line.length() && line[c + 1] == L'/') {
                    inBlockComment = false;
                    c++;
                }
                continue;
            }

            // Inside double quote string
            if (inDoubleQuote) {
                if (ch == L'\\') {
                    c++; // Skip escaped char
                    continue;
                }
                if (ch == L'"') {
                    inDoubleQuote = false;
                }
                continue;
            }

            // Inside single quote string
            if (inSingleQuote) {
                if (ch == L'\\') {
                    c++; // Skip escaped char
                    continue;
                }
                if (ch == L'\'') {
                    inSingleQuote = false;
                }
                continue;
            }

            // Check for line comment //
            if (ch == L'/' && c + 1 < line.length() && line[c + 1] == L'/') {
                if (c > 0 && line[c - 1] == L':') {
                    continue; // Part of a URL (http://, https://, etc.), NOT a line comment
                }
                break; // Ignore rest of line
            }

            // Check for block comment /*
            if (ch == L'/' && c + 1 < line.length() && line[c + 1] == L'*') {
                inBlockComment = true;
                blockCommentLine = l;
                blockCommentCol = c;
                c++;
                continue;
            }

            // String start
            if (ch == L'"') {
                inDoubleQuote = true;
                quoteStartCol = c;
                continue;
            }
            if (ch == L'\'') {
                inSingleQuote = true;
                quoteStartCol = c;
                continue;
            }

            // Brackets
            if (ch == L'{' || ch == L'(' || ch == L'[') {
                bracketStack.push_back({ ch, l, c });
            } else if (ch == L'}' || ch == L')' || ch == L']') {
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, L"Syntax Error: Unmatched closing bracket '" + std::wstring(1, ch) + L"'", 1 });
                } else {
                    wchar_t openCh = bracketStack.back().ch;
                    bool matches = (openCh == L'{' && ch == L'}') ||
                                   (openCh == L'(' && ch == L')') ||
                                   (openCh == L'[' && ch == L']');
                    if (!matches) {
                        wchar_t expected = (openCh == L'{') ? L'}' : (openCh == L'(' ? L')' : L']');
                        diagnostics.push_back({ l, c, 1,
                            L"Syntax Error: Mismatched bracket: expected '" + std::wstring(1, expected) + L"' but found '" + std::wstring(1, ch) + L"'", 1 });
                        bracketStack.pop_back();
                    } else {
                        bracketStack.pop_back();
                    }
                }
            }
        }

        // Unterminated single line string literal
        if (inDoubleQuote) {
            diagnostics.push_back({ l, quoteStartCol, (std::max)(1ULL, line.length() - quoteStartCol), L"Syntax Error: Unterminated string literal", 1 });
        } else if (inSingleQuote) {
            diagnostics.push_back({ l, quoteStartCol, (std::max)(1ULL, line.length() - quoteStartCol), L"Syntax Error: Unterminated character/string literal", 1 });
        }
    }

    if (inBlockComment) {
        diagnostics.push_back({ blockCommentLine, blockCommentCol, 2, L"Syntax Error: Unterminated block comment '/*'", 1 });
    }

    // Unclosed brackets at EOF
    while (!bracketStack.empty()) {
        const auto& b = bracketStack.back();
        wchar_t expected = (b.ch == L'{') ? L'}' : (b.ch == L'(' ? L')' : L']');
        diagnostics.push_back({ b.line, b.col, 1,
            L"Syntax Error: Unclosed bracket '" + std::wstring(1, b.ch) + L"' (missing matching '" + std::wstring(1, expected) + L"')", 1 });
        bracketStack.pop_back();
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateCss(
    const Buffer& buffer,
    size_t startLine,
    size_t endLine
) {
    std::vector<lsp::Diagnostic> diagnostics;
    size_t lineCount = buffer.GetLineCount();
    if (endLine > lineCount) endLine = lineCount;

    static const std::unordered_set<std::wstring> knownProps = {
        // Layout & Display
        L"display", L"position", L"top", L"right", L"bottom", L"left", L"z-index", L"float", L"clear",
        L"visibility", L"box-sizing", L"overflow", L"overflow-x", L"overflow-y", L"clip-path",
        L"isolation", L"contain", L"container", L"container-name", L"container-type",
        // Flexbox & Grid
        L"flex", L"flex-direction", L"flex-wrap", L"flex-flow", L"justify-content", L"align-items",
        L"align-content", L"align-self", L"justify-items", L"justify-self", L"place-items", L"place-content", L"place-self",
        L"flex-grow", L"flex-shrink", L"flex-basis", L"order",
        L"gap", L"row-gap", L"column-gap", L"grid", L"grid-template-columns", L"grid-template-rows",
        L"grid-template-areas", L"grid-column", L"grid-column-start", L"grid-column-end",
        L"grid-row", L"grid-row-start", L"grid-row-end", L"grid-area", L"grid-auto-flow",
        L"grid-auto-columns", L"grid-auto-rows",
        // Dimensions & Insets
        L"width", L"min-width", L"max-width", L"height", L"min-height", L"max-height",
        L"inline-size", L"min-inline-size", L"max-inline-size", L"block-size", L"min-block-size", L"max-block-size",
        L"aspect-ratio", L"inset", L"inset-block", L"inset-block-start", L"inset-block-end",
        L"inset-inline", L"inset-inline-start", L"inset-inline-end",
        // Margins & Paddings
        L"margin", L"margin-top", L"margin-right", L"margin-bottom", L"margin-left",
        L"margin-block", L"margin-block-start", L"margin-block-end",
        L"margin-inline", L"margin-inline-start", L"margin-inline-end",
        L"padding", L"padding-top", L"padding-right", L"padding-bottom", L"padding-left",
        L"padding-block", L"padding-block-start", L"padding-block-end",
        L"padding-inline", L"padding-inline-start", L"padding-inline-end",
        // Colors & Backgrounds
        L"color", L"color-scheme", L"accent-color", L"caret-color",
        L"background", L"background-color", L"background-image", L"background-position", L"background-position-x", L"background-position-y",
        L"background-size", L"background-repeat", L"background-attachment", L"background-clip", L"background-origin", L"background-blend-mode",
        L"opacity", L"filter", L"backdrop-filter", L"mix-blend-mode",
        // Borders & Outlines
        L"border", L"border-width", L"border-style", L"border-color", L"border-radius",
        L"border-top", L"border-top-width", L"border-top-style", L"border-top-color", L"border-top-left-radius", L"border-top-right-radius",
        L"border-right", L"border-right-width", L"border-right-style", L"border-right-color",
        L"border-bottom", L"border-bottom-width", L"border-bottom-style", L"border-bottom-color", L"border-bottom-left-radius", L"border-bottom-right-radius",
        L"border-left", L"border-left-width", L"border-left-style", L"border-left-color",
        L"border-block", L"border-block-width", L"border-block-style", L"border-block-color",
        L"border-block-start", L"border-block-end",
        L"border-inline", L"border-inline-width", L"border-inline-style", L"border-inline-color",
        L"border-inline-start", L"border-inline-end",
        L"border-collapse", L"border-spacing", L"box-shadow",
        L"outline", L"outline-width", L"outline-style", L"outline-color", L"outline-offset",
        // Typography & Text
        L"font", L"font-family", L"font-size", L"font-weight", L"font-style", L"font-variant", L"font-display",
        L"line-height", L"letter-spacing", L"word-spacing", L"text-align", L"text-align-last",
        L"text-decoration", L"text-decoration-line", L"text-decoration-style", L"text-decoration-color", L"text-decoration-thickness",
        L"text-underline-offset", L"text-transform", L"text-overflow", L"text-shadow", L"text-indent",
        L"white-space", L"word-break", L"overflow-wrap", L"word-wrap", L"text-wrap", L"hyphens",
        L"vertical-align", L"writing-mode", L"direction", L"unicode-bidi",
        // Transitions & Animations
        L"transition", L"transition-property", L"transition-duration", L"transition-timing-function", L"transition-delay",
        L"animation", L"animation-name", L"animation-duration", L"animation-timing-function", L"animation-delay",
        L"animation-iteration-count", L"animation-direction", L"animation-fill-mode", L"animation-play-state",
        // Transforms
        L"transform", L"transform-origin", L"transform-style", L"perspective", L"perspective-origin", L"backface-visibility",
        L"scale", L"rotate", L"translate",
        // User Interface & Behavior
        L"cursor", L"pointer-events", L"user-select", L"resize", L"appearance",
        L"touch-action", L"overscroll-behavior", L"overscroll-behavior-x", L"overscroll-behavior-y",
        L"scroll-behavior", L"scroll-margin", L"scroll-margin-top", L"scroll-margin-right", L"scroll-margin-bottom", L"scroll-margin-left",
        L"scroll-margin-inline", L"scroll-margin-block", L"scroll-padding", L"scroll-padding-top", L"scroll-padding-right", L"scroll-padding-bottom", L"scroll-padding-left",
        L"scroll-padding-inline", L"scroll-padding-block", L"scroll-snap-type", L"scroll-snap-align",
        L"scrollbar-width", L"scrollbar-color", L"scrollbar-gutter",
        L"content", L"will-change", L"all",
        // SVG & Masks
        L"fill", L"fill-opacity", L"fill-rule", L"stroke", L"stroke-width", L"stroke-linecap", L"stroke-linejoin", L"stroke-dasharray", L"stroke-dashoffset", L"stroke-opacity",
        L"mask", L"mask-image", L"mask-size", L"mask-repeat", L"mask-position", L"mask-clip",
        // Object & Media
        L"object-fit", L"object-position", L"image-rendering", L"image-orientation",
        // Containment & Intrinsic
        L"contain-intrinsic-size", L"contain-intrinsic-width", L"contain-intrinsic-height",
        L"contain-intrinsic-inline-size", L"contain-intrinsic-block-size",
        // Multi-column
        L"columns", L"column-count", L"column-width", L"column-rule", L"column-rule-width",
        L"column-rule-style", L"column-rule-color", L"column-span", L"column-fill",
        // Font & Typography Extensions
        L"font-feature-settings", L"font-variation-settings", L"font-synthesis", L"font-kerning",
        L"font-optical-sizing", L"font-palette", L"font-smooth",
        L"text-rendering", L"text-orientation", L"text-emphasis", L"text-emphasis-color",
        L"text-emphasis-style", L"text-emphasis-position", L"text-size-adjust", L"tab-size",
        L"line-clamp",
        // Paging & Break
        L"break-after", L"break-before", L"break-inside", L"page-break-after", L"page-break-before", L"page-break-inside",
        L"box-decoration-break",
        // Counters & Quotes
        L"quotes", L"counter-reset", L"counter-increment", L"counter-set",
        // Scroll & Animation Timelines
        L"scroll-timeline", L"scroll-timeline-name", L"scroll-timeline-axis",
        L"view-timeline", L"view-timeline-name", L"view-timeline-axis", L"timeline-scope",
        L"animation-timeline", L"animation-range", L"animation-range-start", L"animation-range-end",
        // Form & Sizing
        L"field-sizing", L"zoom",
        // Color & Print Adjustment
        L"print-color-adjust", L"forced-color-adjust", L"color-interpolation", L"color-interpolation-filters",
        // SVG & Math
        L"clip", L"clip-rule", L"stroke-miterlimit", L"math-style", L"math-depth",
        // CSS Anchoring
        L"anchor-name", L"position-anchor", L"position-try", L"position-try-options", L"position-try-order", L"position-visibility",
        // Tables & Lists
        L"list-style", L"list-style-type", L"list-style-position", L"list-style-image",
        L"table-layout", L"caption-side", L"empty-cells"
    };

    struct BraceInfo { size_t line; size_t col; };
    std::vector<BraceInfo> braceStack;

    bool inBlock = false;
    bool inComment = false;

    for (size_t l = startLine; l < endLine; ++l) {
        const auto& line = buffer.GetLine(l);
        size_t c = 0;
        while (c < line.length()) {
            if (inComment) {
                if (c + 1 < line.length() && line[c] == L'*' && line[c + 1] == L'/') {
                    inComment = false;
                    c += 2;
                } else {
                    c++;
                }
                continue;
            }

            if (c + 1 < line.length() && line[c] == L'/' && line[c + 1] == L'*') {
                inComment = true;
                c += 2;
                continue;
            }

            wchar_t ch = line[c];

            // Skip string literals "..." or '...'
            if (ch == L'"' || ch == L'\'') {
                wchar_t quote = ch;
                c++;
                while (c < line.length()) {
                    if (line[c] == L'\\') {
                        c += 2;
                    } else if (line[c] == quote) {
                        c++;
                        break;
                    } else {
                        c++;
                    }
                }
                continue;
            }

            if (ch == L'{') {
                braceStack.push_back({ l, c });
                inBlock = true;
                c++;
                continue;
            } else if (ch == L'}') {
                if (braceStack.empty()) {
                    diagnostics.push_back({ l, c, 1, L"CSS Syntax Error: Unexpected closing brace '}'", 1 });
                } else {
                    braceStack.pop_back();
                    inBlock = !braceStack.empty();
                }
                c++;
                continue;
            }

            // Inside CSS declaration block { ... }
            if (inBlock && (iswalpha(ch) || (ch == L'-' && c + 1 < line.length() && (line[c + 1] == L'-' || iswalpha(line[c + 1]))))) {
                size_t propStart = c;
                while (c < line.length() && (iswalnum(line[c]) || line[c] == L'-' || line[c] == L'_')) {
                    c++;
                }
                std::wstring propName = line.substr(propStart, c - propStart);
                for (auto& k : propName) k = static_cast<wchar_t>(towlower(k));

                // Find the first delimiter among '{', '}', ';' following this property name (skipping strings)
                size_t dIdx = c;
                wchar_t firstDelim = L'\0';
                size_t firstDelimPos = std::wstring::npos;
                while (dIdx < line.length()) {
                    wchar_t dc = line[dIdx];
                    if (dc == L'"' || dc == L'\'') {
                        wchar_t dq = dc;
                        dIdx++;
                        while (dIdx < line.length()) {
                            if (line[dIdx] == L'\\') {
                                dIdx += 2;
                            } else if (line[dIdx] == dq) {
                                dIdx++;
                                break;
                            } else {
                                dIdx++;
                            }
                        }
                        continue;
                    }
                    if (dc == L'{' || dc == L'}' || dc == L';') {
                        firstDelim = dc;
                        firstDelimPos = dIdx;
                        break;
                    }
                    dIdx++;
                }

                // If the first delimiter is '{', this is a nested selector or nested rule (e.g. '@media ... {' or '.nested {')
                if (firstDelim == L'{') {
                    c = firstDelimPos;
                    continue;
                }

                while (c < line.length() && iswspace(line[c])) c++;

                if (c >= line.length() || line[c] != L':') {
                    // Only flag missing colon if the statement actually ends with a ';'
                    if (firstDelim == L';') {
                        diagnostics.push_back({ l, propStart, propName.length(), L"CSS Syntax Error: Missing colon ':' after property '" + propName + L"'", 1 });
                        c = firstDelimPos + 1;
                    } else {
                        c = propStart + 1;
                    }
                    continue;
                }

                // Colon found! Check if property is known
                static const std::unordered_set<std::wstring> webDataProps = []() {
                    std::unordered_set<std::wstring> s;
                    for (const auto& item : WebIntelliSenseData::GetCssProperties()) {
                        std::wstring lower = item.label;
                        for (auto& chL : lower) chL = static_cast<wchar_t>(towlower(chL));
                        s.insert(std::move(lower));
                    }
                    return s;
                }();

                if (propName.rfind(L"--", 0) != 0 && propName.rfind(L"-webkit-", 0) != 0 &&
                    propName.rfind(L"-moz-", 0) != 0 && propName.rfind(L"-ms-", 0) != 0 && propName.rfind(L"-o-", 0) != 0 &&
                    propName.rfind(L"scroll-margin", 0) != 0 && propName.rfind(L"scroll-padding", 0) != 0) {
                    if (knownProps.find(propName) == knownProps.end() && webDataProps.find(propName) == webDataProps.end()) {
                        diagnostics.push_back({ l, propStart, propName.length(), L"CSS Linter: Unknown CSS property '" + propName + L"'", 2 });
                    }
                }
                size_t colonCol = c;
                c++; // skip ':'

                // Scan value until ';' or '}'
                size_t valStart = c;
                while (c < line.length() && line[c] != L';' && line[c] != L'}') {
                    c++;
                }
                std::wstring valStr = line.substr(valStart, c - valStart);

                // Strip inline /* ... */ comments from value before checking emptiness & tokens
                std::wstring cleanVal;
                cleanVal.reserve(valStr.length());
                for (size_t vi = 0; vi < valStr.length();) {
                    if (vi + 1 < valStr.length() && valStr[vi] == L'/' && valStr[vi + 1] == L'*') {
                        size_t endCmt = valStr.find(L"*/", vi + 2);
                        if (endCmt != std::wstring::npos) {
                            cleanVal.append(endCmt + 2 - vi, L' ');
                            vi = endCmt + 2;
                        } else {
                            cleanVal.append(valStr.length() - vi, L' ');
                            break;
                        }
                    } else {
                        cleanVal.push_back(valStr[vi]);
                        vi++;
                    }
                }

                std::wstring trimmedVal = cleanVal;
                while (!trimmedVal.empty() && iswspace(trimmedVal.front())) trimmedVal.erase(trimmedVal.begin());
                while (!trimmedVal.empty() && iswspace(trimmedVal.back())) trimmedVal.pop_back();

                // 1. Check for empty property value (e.g. `width: ;`, `width:;`, or `width:` with no continuation)
                if (trimmedVal.empty()) {
                    bool continuesOnNextLine = false;
                    if (c >= line.length() && l + 1 < endLine) {
                        std::wstring nextLn = buffer.GetLine(l + 1);
                        size_t ns = 0;
                        while (ns < nextLn.length() && iswspace(nextLn[ns])) ns++;
                        if (ns < nextLn.length() && nextLn[ns] != L'}' && nextLn[ns] != L';') {
                            size_t nextColon = nextLn.find(L':', ns);
                            size_t nextParen = nextLn.find(L'(', ns);
                            if (nextColon == std::wstring::npos || (nextParen != std::wstring::npos && nextParen < nextColon)) {
                                continuesOnNextLine = true;
                            }
                        }
                    }
                    if (!continuesOnNextLine) {
                        size_t endCol = (c < line.length() && line[c] == L';') ? (c + 1) : c;
                        size_t errLen = (endCol > propStart) ? (endCol - propStart) : (colonCol - propStart + 1);
                        diagnostics.push_back({ l, propStart, errLen, L"CSS Syntax Error: Property value expected for '" + propName + L"'", 1 });
                    }
                } else {
                    // 2. Check for unbalanced parentheses in value (e.g. `calc(100% - 20px;`)
                    int parenDepth = 0;
                    size_t firstOpenParen = 0;
                    bool inValQuote = false;
                    wchar_t valQuoteCh = 0;
                    for (size_t vi = 0; vi < cleanVal.length(); ++vi) {
                        wchar_t vc = cleanVal[vi];
                        if (inValQuote) {
                            if (vc == L'\\') { vi++; continue; }
                            if (vc == valQuoteCh) inValQuote = false;
                            continue;
                        }
                        if (vc == L'"' || vc == L'\'') {
                            inValQuote = true;
                            valQuoteCh = vc;
                            continue;
                        }
                        if (vc == L'(') {
                            if (parenDepth == 0) firstOpenParen = vi;
                            parenDepth++;
                        } else if (vc == L')') {
                            if (parenDepth == 0) {
                                diagnostics.push_back({ l, valStart + vi, 1, L"CSS Syntax Error: Unmatched closing parenthesis ')' in property value", 1 });
                            } else {
                                parenDepth--;
                            }
                        }
                    }
                    if (parenDepth > 0 && (c < line.length() && (line[c] == L';' || line[c] == L'}'))) {
                        diagnostics.push_back({ l, valStart + firstOpenParen, 1, L"CSS Syntax Error: Unclosed parenthesis '(' in property value", 1 });
                    }

                    // 3. Check for unknown/invalid CSS units on numeric dimensions (e.g. `20abc`)
                    static const std::unordered_set<std::wstring> validCssUnits = {
                        L"%", L"px", L"em", L"rem", L"vh", L"vw", L"vmin", L"vmax", L"vi", L"vb",
                        L"svh", L"svw", L"svmin", L"svmax", L"svi", L"svb",
                        L"lvh", L"lvw", L"lvmin", L"lvmax", L"lvi", L"lvb",
                        L"dvh", L"dvw", L"dvmin", L"dvmax", L"dvi", L"dvb",
                        L"ch", L"ex", L"cap", L"ic", L"lh", L"rlh",
                        L"cm", L"mm", L"q", L"in", L"pc", L"pt",
                        L"deg", L"grad", L"rad", L"turn",
                        L"s", L"ms", L"hz", L"khz",
                        L"dpi", L"dpcm", L"dppx", L"x",
                        L"fr", L"cqw", L"cqh", L"cqi", L"cqb", L"cqmin", L"cqmax",
                        L"n" // e.g. 2n+1 in nth-child
                    };
                    if (cleanVal.find(L"url(") == std::wstring::npos && propName != L"unicode-range" &&
                        propName != L"grid-area" && propName != L"grid-column" && propName != L"grid-row" &&
                        propName != L"font-family" && propName != L"animation" && propName != L"animation-name") {
                        bool inQ = false;
                        wchar_t qC = 0;
                        for (size_t vi = 0; vi < cleanVal.length();) {
                            wchar_t vc = cleanVal[vi];
                            if (inQ) {
                                if (vc == L'\\') { vi += 2; continue; }
                                if (vc == qC) inQ = false;
                                vi++;
                                continue;
                            }
                            if (vc == L'"' || vc == L'\'') {
                                inQ = true;
                                qC = vc;
                                vi++;
                                continue;
                            }
                            if (vc == L'#') {
                                vi++;
                                while (vi < cleanVal.length() && (iswalnum(cleanVal[vi]) || cleanVal[vi] == L'-' || cleanVal[vi] == L'_')) vi++;
                                continue;
                            }
                            bool prevIsIdent = (vi > 0 && (iswalpha(cleanVal[vi - 1]) || cleanVal[vi - 1] == L'-' || cleanVal[vi - 1] == L'_' || cleanVal[vi - 1] == L'#'));
                            if (!prevIsIdent && iswdigit(vc)) {
                                size_t numStart = vi;
                                while (vi < cleanVal.length() && iswdigit(cleanVal[vi])) vi++;
                                if (vi < cleanVal.length() && cleanVal[vi] == L'.') {
                                    vi++;
                                    while (vi < cleanVal.length() && iswdigit(cleanVal[vi])) vi++;
                                }
                                size_t unitStart = vi;
                                while (vi < cleanVal.length() && (iswalpha(cleanVal[vi]) || cleanVal[vi] == L'%')) {
                                    vi++;
                                }
                                if (vi > unitStart) {
                                    std::wstring unitStr = cleanVal.substr(unitStart, vi - unitStart);
                                    for (auto& cu : unitStr) cu = static_cast<wchar_t>(towlower(cu));
                                    if (validCssUnits.find(unitStr) == validCssUnits.end()) {
                                        diagnostics.push_back({ l, valStart + numStart, vi - numStart,
                                            L"CSS Syntax Error: Unknown CSS unit '" + unitStr + L"'", 1 });
                                    }
                                }
                                continue;
                            }
                            vi++;
                        }
                    }

                    // 4. Check for missing semicolon at end of single-line declaration when followed by another property
                    if (c >= line.length() && !trimmedVal.empty()) {
                        wchar_t lastCh = trimmedVal.back();
                        if (lastCh != L',' && lastCh != L'{' && lastCh != L'(' && lastCh != L'+' && lastCh != L'-' && lastCh != L'*' && lastCh != L'/') {
                            for (size_t nl = l + 1; nl < endLine; ++nl) {
                                std::wstring nextLn = buffer.GetLine(nl);
                                size_t ns = 0;
                                while (ns < nextLn.length() && iswspace(nextLn[ns])) ns++;
                                if (ns >= nextLn.length()) continue;
                                if (ns + 1 < nextLn.length() && nextLn[ns] == L'/' && nextLn[ns + 1] == L'*') break;
                                if (iswalpha(nextLn[ns]) || (nextLn[ns] == L'-' && ns + 1 < nextLn.length() && (nextLn[ns + 1] == L'-' || iswalpha(nextLn[ns + 1])))) {
                                    size_t np = ns;
                                    while (np < nextLn.length() && (iswalnum(nextLn[np]) || nextLn[np] == L'-' || nextLn[np] == L'_')) np++;
                                    while (np < nextLn.length() && iswspace(nextLn[np])) np++;
                                    if (np < nextLn.length() && nextLn[np] == L':') {
                                        size_t nextBrace = nextLn.find(L'{', np);
                                        if (nextBrace == std::wstring::npos) {
                                            size_t endNonWs = line.length();
                                            while (endNonWs > valStart && iswspace(line[endNonWs - 1])) endNonWs--;
                                            size_t colMark = (endNonWs > 0) ? (endNonWs - 1) : colonCol;
                                            diagnostics.push_back({ l, colMark, 1, L"CSS Syntax Error: Missing semicolon ';' at end of declaration", 1 });
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }
                }

                // Check for invalid hex colors in value (ignore url(...) fragments)
                if (valStr.find(L"url(") == std::wstring::npos) {
                    size_t hashPos = 0;
                    while ((hashPos = valStr.find(L'#', hashPos)) != std::wstring::npos) {
                        size_t hEnd = hashPos + 1;
                        while (hEnd < valStr.length() && iswxdigit(valStr[hEnd])) hEnd++;
                        size_t hLen = hEnd - hashPos - 1;
                        bool atBoundary = (hEnd >= valStr.length() || iswspace(valStr[hEnd]) ||
                                           valStr[hEnd] == L';' || valStr[hEnd] == L',' ||
                                           valStr[hEnd] == L')' || valStr[hEnd] == L'}');
                        if (atBoundary && hLen > 0 && hLen != 3 && hLen != 4 && hLen != 6 && hLen != 8) {
                            diagnostics.push_back({ l, valStart + hashPos, hEnd - hashPos, L"CSS Syntax Error: Invalid hex color length in '" + valStr.substr(hashPos, hEnd - hashPos) + L"'", 1 });
                        }
                        hashPos = (hEnd > hashPos) ? hEnd : (hashPos + 1);
                    }
                }

                if (c < line.length() && line[c] == L';') {
                    c++;
                }
                // NOTE: If line[c] == L'}', we do not consume it here, allowing the outer loop to process the block close
                continue;
            } else {
                c++;
            }
        }
    }

    for (const auto& b : braceStack) {
        diagnostics.push_back({ b.line, b.col, 1, L"CSS Syntax Error: Unclosed brace '{'", 1 });
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateJavaScript(
    const Buffer& buffer,
    size_t startLine,
    size_t endLine,
    bool isJsx
) {
    std::vector<lsp::Diagnostic> diagnostics;
    size_t lineCount = buffer.GetLineCount();
    if (endLine > lineCount) endLine = lineCount;

    struct Bracket { wchar_t ch; size_t line; size_t col; };
    std::vector<Bracket> stack;

    bool inBlockComment = false;
    struct TemplateContext { int braceDepth = 0; };
    std::vector<TemplateContext> templateStack;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    std::vector<std::wstring> jsxTagStack;
    std::vector<size_t> jsxExprBraceLevels;
    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"link", L"meta", L"param", L"source", L"track", L"wbr"
    };

    for (size_t l = startLine; l < endLine; ++l) {
        const auto& line = buffer.GetLine(l);

        for (size_t c = 0; c < line.length(); ++c) {
            // 1. Block comment handling
            if (inBlockComment) {
                if (c + 1 < line.length() && line[c] == L'*' && line[c + 1] == L'/') {
                    inBlockComment = false;
                    c++;
                }
                continue;
            }

            // 2. Template literal text mode (when not in an interpolated ${...} expression)
            if (!templateStack.empty() && templateStack.back().braceDepth == 0) {
                if (line[c] == L'\\') {
                    c++; // skip escaped character inside template literal
                    continue;
                }
                if (line[c] == L'`') {
                    templateStack.pop_back(); // template literal ended
                    continue;
                }
                if (line[c] == L'$' && c + 1 < line.length() && line[c + 1] == L'{') {
                    // Enter template interpolation ${...}
                    templateStack.back().braceDepth = 1;
                    c++; // skip '$'
                    continue;
                }
                continue;
            }

            // 3. Single-quote string literal
            if (inSingleQuote) {
                if (line[c] == L'\\') {
                    c++;
                    continue;
                }
                if (line[c] == L'\'') {
                    inSingleQuote = false;
                }
                continue;
            }

            // 4. Double-quote string literal
            if (inDoubleQuote) {
                if (line[c] == L'\\') {
                    c++;
                    continue;
                }
                if (line[c] == L'"') {
                    inDoubleQuote = false;
                }
                continue;
            }

            // 4.5 JSX syntax and children text mode handling
            if (isJsx) {
                bool inJsxText = !jsxTagStack.empty() && jsxExprBraceLevels.empty();
                if (inJsxText) {
                    // Inside JSX text: '{' enters a JavaScript expression
                    if (line[c] == L'{') {
                        stack.push_back({ L'{', l, c });
                        jsxExprBraceLevels.push_back(stack.size());
                        continue;
                    }
                    if (line[c] == L'<') {
                        // Check for closing tag </Tag> or </>
                        if (c + 1 < line.length() && line[c + 1] == L'/') {
                            size_t gt = line.find(L'>', c + 2);
                            if (gt != std::wstring::npos) {
                                std::wstring closeTag;
                                for (size_t k = c + 2; k < gt; ++k) {
                                    if (!iswspace(line[k])) closeTag.push_back(towlower(line[k]));
                                }
                                if (!jsxTagStack.empty()) {
                                    if (closeTag.empty() || jsxTagStack.back() == closeTag) {
                                        jsxTagStack.pop_back();
                                    } else {
                                        for (int idx = static_cast<int>(jsxTagStack.size()) - 1; idx >= 0; --idx) {
                                            if (jsxTagStack[idx] == closeTag) {
                                                jsxTagStack.erase(jsxTagStack.begin() + idx, jsxTagStack.end());
                                                break;
                                            }
                                        }
                                    }
                                }
                                c = gt;
                                continue;
                            }
                        } else if (c + 1 < line.length() && line[c + 1] == L'>') {
                            // Opening fragment <>
                            jsxTagStack.push_back(L"");
                            c++;
                            continue;
                        } else if (c + 1 < line.length() && (iswalpha(line[c + 1]) || line[c + 1] == L'_')) {
                            // Find the matching '>' of the JSX opening tag across single or multiple lines
                            size_t gtLine = l;
                            size_t gtCol = std::wstring::npos;
                            bool inQ = false;
                            wchar_t qC = L'\0';
                            int bDepth = 0;
                            for (size_t sl = l; sl < lineCount && sl - l <= 25; ++sl) {
                                const std::wstring& sLine = buffer.GetLine(sl);
                                size_t startK = (sl == l) ? (c + 1) : 0;
                                for (size_t k = startK; k < sLine.length(); ++k) {
                                    wchar_t kc = sLine[k];
                                    if (inQ) {
                                        if (kc == L'\\' && k + 1 < sLine.length()) { k++; continue; }
                                        if (kc == qC) inQ = false;
                                    } else {
                                        if (kc == L'"' || kc == L'\'' || kc == L'`') {
                                            inQ = true;
                                            qC = kc;
                                        } else if (kc == L'{') {
                                            bDepth++;
                                        } else if (kc == L'}') {
                                            if (bDepth > 0) bDepth--;
                                        } else if (kc == L'>' && bDepth == 0) {
                                            if (k > 0 && sLine[k - 1] == L'=') continue; // Ignore arrow function '=>'
                                            gtLine = sl;
                                            gtCol = k;
                                            break;
                                        }
                                    }
                                }
                                if (gtCol != std::wstring::npos) break;
                            }
                            if (gtCol != std::wstring::npos) {
                                size_t tagEnd = c + 1;
                                while (tagEnd < line.length() && (iswalnum(line[tagEnd]) || line[tagEnd] == L'_' || line[tagEnd] == L'-' || line[tagEnd] == L'.')) {
                                    tagEnd++;
                                }
                                std::wstring tag;
                                for (size_t k = c + 1; k < tagEnd; ++k) tag.push_back(towlower(line[k]));
                                const std::wstring& endLineRef = buffer.GetLine(gtLine);
                                bool isSelfClosing = (gtCol > 0 && endLineRef[gtCol - 1] == L'/') || (voidTags.count(tag) > 0);
                                // Avoid misclassifying TypeScript generics (e.g. fn<Foo, Bar>(), useRef<any>()) as JSX elements
                                bool looksLikeTsGeneric = false;
                                if (gtLine == l) {
                                    std::wstring slice = line.substr(c + 1, gtCol - (c + 1));
                                    if (slice.find(L'=') == std::wstring::npos &&
                                        (slice.find(L',') != std::wstring::npos || slice.find(L'|') != std::wstring::npos ||
                                         slice.find(L'&') != std::wstring::npos || slice.find(L" extends ") != std::wstring::npos)) {
                                        looksLikeTsGeneric = true;
                                    }
                                }
                                size_t afterGt = gtCol + 1;
                                while (afterGt < endLineRef.length() && iswspace(endLineRef[afterGt])) ++afterGt;
                                if (afterGt < endLineRef.length() &&
                                    (endLineRef[afterGt] == L'(' || endLineRef[afterGt] == L';' || endLineRef[afterGt] == L'.')) {
                                    looksLikeTsGeneric = true;
                                }
                                if (!isSelfClosing && !looksLikeTsGeneric) {
                                    jsxTagStack.push_back(tag);
                                }
                                if (gtLine > l) {
                                    l = gtLine;
                                    break;
                                }
                                c = gtCol;
                                continue;
                            }
                        }
                    }
                    // All other characters inside JSX element children (prose, punctuation, &nbsp;) are literal JSX text!
                    continue;
                }

                // In normal JS code: check if '<' starts a JSX element (Fragment or Tag)
                if (line[c] == L'<') {
                    // In JS/TS, a JSX tag CANNOT immediately follow an identifier or closing bracket!
                    int p = static_cast<int>(c) - 1;
                    while (p >= 0 && iswspace(line[p])) p--;
                    wchar_t pch = (p >= 0) ? line[p] : L'\0';
                    bool isPrevIdent = iswalnum(pch) || pch == L'_' || pch == L'$' || pch == L')' || pch == L']';

                    if (!isPrevIdent) {
                        if (c + 1 < line.length() && line[c + 1] == L'/') {
                            size_t gt = line.find(L'>', c + 2);
                            if (gt != std::wstring::npos) {
                                std::wstring closeTag;
                                for (size_t k = c + 2; k < gt; ++k) {
                                    if (!iswspace(line[k])) closeTag.push_back(towlower(line[k]));
                                }
                                if (!jsxTagStack.empty()) {
                                    if (closeTag.empty() || jsxTagStack.back() == closeTag) {
                                        jsxTagStack.pop_back();
                                    } else {
                                        for (int idx = static_cast<int>(jsxTagStack.size()) - 1; idx >= 0; --idx) {
                                            if (jsxTagStack[idx] == closeTag) {
                                                jsxTagStack.erase(jsxTagStack.begin() + idx, jsxTagStack.end());
                                                break;
                                            }
                                        }
                                    }
                                }
                                c = gt;
                                continue;
                            }
                        } else if (c + 1 < line.length() && line[c + 1] == L'>') {
                            // Opening fragment <>
                            jsxTagStack.push_back(L"");
                            c++;
                            continue;
                        } else if (c + 1 < line.length() && (iswalpha(line[c + 1]) || line[c + 1] == L'_')) {
                            // Find the matching '>' of the JSX opening tag across single or multiple lines
                            size_t gtLine = l;
                            size_t gtCol = std::wstring::npos;
                            bool inQ = false;
                            wchar_t qC = L'\0';
                            int bDepth = 0;
                            for (size_t sl = l; sl < lineCount && sl - l <= 25; ++sl) {
                                const std::wstring& sLine = buffer.GetLine(sl);
                                size_t startK = (sl == l) ? (c + 1) : 0;
                                for (size_t k = startK; k < sLine.length(); ++k) {
                                    wchar_t kc = sLine[k];
                                    if (inQ) {
                                        if (kc == L'\\' && k + 1 < sLine.length()) { k++; continue; }
                                        if (kc == qC) inQ = false;
                                    } else {
                                        if (kc == L'"' || kc == L'\'' || kc == L'`') {
                                            inQ = true;
                                            qC = kc;
                                        } else if (kc == L'{') {
                                            bDepth++;
                                        } else if (kc == L'}') {
                                            if (bDepth > 0) bDepth--;
                                        } else if (kc == L'>' && bDepth == 0) {
                                            if (k > 0 && sLine[k - 1] == L'=') continue; // Ignore arrow function '=>'
                                            gtLine = sl;
                                            gtCol = k;
                                            break;
                                        }
                                    }
                                }
                                if (gtCol != std::wstring::npos) break;
                            }
                            if (gtCol != std::wstring::npos) {
                                size_t tagEnd = c + 1;
                                while (tagEnd < line.length() && (iswalnum(line[tagEnd]) || line[tagEnd] == L'_' || line[tagEnd] == L'-' || line[tagEnd] == L'.')) {
                                    tagEnd++;
                                }
                                std::wstring tag;
                                for (size_t k = c + 1; k < tagEnd; ++k) tag.push_back(towlower(line[k]));
                                const std::wstring& endLineRef = buffer.GetLine(gtLine);
                                bool isSelfClosing = (gtCol > 0 && endLineRef[gtCol - 1] == L'/') || (voidTags.count(tag) > 0);
                                // Avoid misclassifying TypeScript generics (e.g. fn<Foo, Bar>(), useRef<any>()) as JSX elements
                                bool looksLikeTsGeneric = false;
                                if (gtLine == l) {
                                    std::wstring slice = line.substr(c + 1, gtCol - (c + 1));
                                    if (slice.find(L'=') == std::wstring::npos &&
                                        (slice.find(L',') != std::wstring::npos || slice.find(L'|') != std::wstring::npos ||
                                         slice.find(L'&') != std::wstring::npos || slice.find(L" extends ") != std::wstring::npos)) {
                                        looksLikeTsGeneric = true;
                                    }
                                }
                                size_t afterGt = gtCol + 1;
                                while (afterGt < endLineRef.length() && iswspace(endLineRef[afterGt])) ++afterGt;
                                if (afterGt < endLineRef.length() &&
                                    (endLineRef[afterGt] == L'(' || endLineRef[afterGt] == L';' || endLineRef[afterGt] == L'.')) {
                                    looksLikeTsGeneric = true;
                                }
                                if (!isSelfClosing && !looksLikeTsGeneric) {
                                    jsxTagStack.push_back(tag);
                                }
                                if (gtLine > l) {
                                    l = gtLine;
                                    break;
                                }
                                c = gtCol;
                                continue;
                            }
                        }
                    }
                }
            }

            // 5. Comments
            if (c + 1 < line.length() && line[c] == L'/' && line[c + 1] == L'/') {
                if (c > 0 && line[c - 1] == L':') {
                    continue; // Part of a URL (http://, https://, etc.), NOT a line comment
                }
                if (line.find(L"</", c + 2) != std::wstring::npos) {
                    continue; // Inside JSX text followed by closing tag (e.g. <span>// comment</span>), NOT a JS comment
                }
                break; // Single line comment: ignore rest of line
            }
            if (c + 1 < line.length() && line[c] == L'/' && line[c + 1] == L'*') {
                inBlockComment = true;
                c++;
                continue;
            }

            // 6. Regular expression literal detection (e.g. line.replace(/"([^"]+)":/g, ...))
            if (line[c] == L'/') {
                if (IsJsRegexStart(line, c)) {
                    size_t r = ScanJsRegexEnd(line, c);
                    if (r != std::wstring::npos) {
                        c = r;
                        continue;
                    }
                }
            }

            // 7. Start of new strings / template literals
            wchar_t ch = line[c];
            if (ch == L'\'') {
                if (c > 0 && (iswalnum(line[c - 1]) || line[c - 1] == L'_')) {
                    continue; // Contraction / apostrophe in JSX/HTML text (e.g. isn't, don't, user's), NOT a string literal
                }
                inSingleQuote = true;
                continue;
            }
            if (ch == L'"') {
                inDoubleQuote = true;
                continue;
            }
            if (ch == L'`') {
                templateStack.push_back({ 0 });
                continue;
            }

            // 8. Bracket parsing and balance verification
            if (ch == L'(' || ch == L'[' || ch == L'{') {
                if (!templateStack.empty() && ch == L'{') {
                    templateStack.back().braceDepth++;
                }
                stack.push_back({ ch, l, c });
            } else if (ch == L')' || ch == L']' || ch == L'}') {
                if (!templateStack.empty() && ch == L'}') {
                    if (templateStack.back().braceDepth == 1) {
                        // Closing brace of the ${...} interpolation
                        templateStack.back().braceDepth = 0;
                        continue;
                    } else if (templateStack.back().braceDepth > 1) {
                        templateStack.back().braceDepth--;
                    }
                }
                if (isJsx && ch == L'}' && !jsxExprBraceLevels.empty() && stack.size() == jsxExprBraceLevels.back()) {
                    jsxExprBraceLevels.pop_back();
                }

                if (stack.empty()) {
                    diagnostics.push_back({ l, c, 1, std::wstring(L"JS Syntax Error: Unexpected closing bracket '") + ch + L"'", 1 });
                } else {
                    wchar_t expected = (stack.back().ch == L'(' ? L')' : (stack.back().ch == L'[' ? L']' : L'}'));
                    if (ch == expected) {
                        stack.pop_back();
                    } else {
                        diagnostics.push_back({ l, c, 1, std::wstring(L"JS Syntax Error: Mismatched bracket: expected '") + expected + L"' (opened at line " + std::to_wstring(stack.back().line + 1) + L":" + std::to_wstring(stack.back().col) + L") but found '" + ch + L"'", 1 });
                        stack.pop_back();
                    }
                }
            }
        }

        // Single and double quotes cannot span across lines in JS unless ended with a backslash
        if (inSingleQuote) {
            if (line.empty() || line.back() != L'\\') {
                diagnostics.push_back({ l, line.length() > 0 ? line.length() - 1 : 0, 1, L"JS Syntax Error: Unclosed string literal", 1 });
                inSingleQuote = false;
            }
        }
        if (inDoubleQuote) {
            if (line.empty() || line.back() != L'\\') {
                diagnostics.push_back({ l, line.length() > 0 ? line.length() - 1 : 0, 1, L"JS Syntax Error: Unclosed string literal", 1 });
                inDoubleQuote = false;
            }
        }
    }

    if (!templateStack.empty()) {
        diagnostics.push_back({ endLine > 0 ? endLine - 1 : 0, 0, 1, L"JS Syntax Error: Unclosed template literal", 1 });
    }

    for (const auto& b : stack) {
        diagnostics.push_back({ b.line, b.col, 1, std::wstring(L"JS Syntax Error: Unclosed bracket '") + b.ch + L"'", 1 });
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateHtml(
    const Buffer& buffer,
    bool validateScripts,
    bool validateStyles
) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct TagInfo {
        std::wstring name;
        size_t line;
        size_t col;
    };
    std::vector<TagInfo> tagStack;
    std::unordered_map<std::wstring, std::pair<size_t, size_t>> seenIds;

    auto isVoidTag = [](const std::wstring& name) {
        std::wstring lower = name;
        for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
        if (lower.rfind(L"!", 0) == 0 || lower.rfind(L"?", 0) == 0) return true;
        return lower == L"area" || lower == L"base" || lower == L"br" || lower == L"col" ||
               lower == L"embed" || lower == L"hr" || lower == L"img" || lower == L"input" ||
               lower == L"link" || lower == L"meta" || lower == L"param" || lower == L"source" ||
               lower == L"track" || lower == L"wbr";
    };

    bool inComment = false;
    bool inPhp = false;
    bool phpInSingleQuote = false;
    bool phpInDoubleQuote = false;
    bool phpInBacktick = false;
    bool phpInBlockComment = false;
    std::wstring phpHeredocEnd;

    auto isAsciiIdentStart = [](wchar_t ch) noexcept -> bool {
        return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
    };
    auto isAsciiTagChar = [](wchar_t ch) noexcept -> bool {
        return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
               (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L':' || ch == L'_' || ch == L'.';
    };

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& curLineRef = buffer.GetLine(l);
        // Check heredoc/nowdoc closing marker at start of line while inside PHP heredoc
        if (inPhp && !phpHeredocEnd.empty()) {
            size_t ws = 0;
            while (ws < curLineRef.length() && (curLineRef[ws] == L' ' || curLineRef[ws] == L'\t')) ++ws;
            if (curLineRef.compare(ws, phpHeredocEnd.length(), phpHeredocEnd) == 0) {
                size_t after = ws + phpHeredocEnd.length();
                if (after >= curLineRef.length() || curLineRef[after] == L';' || curLineRef[after] == L',' ||
                    curLineRef[after] == L')' || iswspace(curLineRef[after])) {
                    phpHeredocEnd.clear();
                }
            }
            continue;
        }

        for (size_t c = 0; l < buffer.GetLineCount() && c < buffer.GetLine(l).length(); ++c) {
            const auto& line = buffer.GetLine(l);
            // Skip PHP code blocks completely in mixed PHP/HTML templates, tracking strings, heredocs, and comments
            // so a literal "?>", "<tag>", or array<string, mixed> inside PHP never breaks out into HTML mode!
            if (inPhp) {
                const wchar_t pch = line[c];
                if (phpInBlockComment) {
                    if (pch == L'*' && c + 1 < line.length() && line[c + 1] == L'/') {
                        phpInBlockComment = false;
                        c++;
                    }
                    continue;
                }
                if (phpInSingleQuote) {
                    if (pch == L'\\') { c++; continue; }
                    if (pch == L'\'') phpInSingleQuote = false;
                    continue;
                }
                if (phpInDoubleQuote) {
                    if (pch == L'\\') { c++; continue; }
                    if (pch == L'"') phpInDoubleQuote = false;
                    continue;
                }
                if (phpInBacktick) {
                    if (pch == L'\\') { c++; continue; }
                    if (pch == L'`') phpInBacktick = false;
                    continue;
                }
                // PHP closing tag (only valid outside strings & block comments)
                if (pch == L'?' && c + 1 < line.length() && line[c + 1] == L'>') {
                    inPhp = false;
                    c++;
                    continue;
                }
                // Single-line PHP comments // and # (excluding #[Attribute])
                if ((pch == L'/' && c + 1 < line.length() && line[c + 1] == L'/') ||
                    (pch == L'#' && !(c + 1 < line.length() && line[c + 1] == L'['))) {
                    break;
                }
                if (pch == L'/' && c + 1 < line.length() && line[c + 1] == L'*') {
                    phpInBlockComment = true;
                    c++;
                    continue;
                }
                if (pch == L'\'') { phpInSingleQuote = true; continue; }
                if (pch == L'"') { phpInDoubleQuote = true; continue; }
                if (pch == L'`') { phpInBacktick = true; continue; }
                // PHP Heredoc / Nowdoc: <<<EOD, <<<'EOD', <<<"EOD"
                if (pch == L'<' && c + 2 < line.length() && line[c + 1] == L'<' && line[c + 2] == L'<') {
                    size_t hp = c + 3;
                    while (hp < line.length() && (line[hp] == L' ' || line[hp] == L'\t')) ++hp;
                    wchar_t hq = L'\0';
                    if (hp < line.length() && (line[hp] == L'\'' || line[hp] == L'"')) hq = line[hp++];
                    size_t idStart = hp;
                    while (hp < line.length() && (iswalnum(line[hp]) || line[hp] == L'_')) ++hp;
                    if (hp > idStart && (hq == L'\0' || (hp < line.length() && line[hp] == hq))) {
                        phpHeredocEnd = line.substr(idStart, hp - idStart);
                        break; // remainder of line belongs to heredoc opener; body begins next line
                    }
                }
                continue;
            }

            if (inComment) {
                if (c + 2 < line.length() && line[c] == L'-' && line[c + 1] == L'-' && line[c + 2] == L'>') {
                    inComment = false;
                    c += 2;
                }
                continue;
            }

            // Detect PHP opening tag (<?php, <?=, or short <? except <?xml)
            if (line[c] == L'<' && c + 1 < line.length() && line[c + 1] == L'?') {
                bool isXmlPi = (c + 5 <= line.length() &&
                                _wcsnicmp(line.c_str() + c, L"<?xml", 5) == 0);
                if (!isXmlPi) {
                    inPhp = true;
                    phpInSingleQuote = false;
                    phpInDoubleQuote = false;
                    phpInBacktick = false;
                    phpInBlockComment = false;
                    phpHeredocEnd.clear();
                    c++;
                    continue;
                }
            }

            if (line[c] == L'<' && c + 3 < line.length() && line[c + 1] == L'!' && line[c + 2] == L'-' && line[c + 3] == L'-') {
                inComment = true;
                c += 3;
                continue;
            }

            // Skip quoted snippets and inline backticks in HTML text if they contain '<' safely within the same line
            // (e.g. `<code>'<nav>'</code>` or `Use "<Shop>"` without splitting `<p>"<b>hi"</b></p>`)
            if (line[c] == L'`' || line[c] == L'"' ||
                (line[c] == L'\'' && (c == 0 || (!iswalnum(line[c - 1]) && line[c - 1] != L'_')))) {
                const wchar_t q = line[c];
                size_t closeQ = std::wstring::npos;
                for (size_t k = c + 1; k < line.length(); ++k) {
                    if (line[k] == L'\\' && k + 1 < line.length()) { ++k; continue; }
                    if (line[k] == q) { closeQ = k; break; }
                }
                if (closeQ != std::wstring::npos) {
                    std::wstring inner = line.substr(c + 1, closeQ - (c + 1));
                    if (q == L'`' || inner.find(L'<') != std::wstring::npos) {
                        bool hasDanglingClose = false;
                        // Avoid skipping `"<b>hi"</b>` where a real closing tag sits right after the quote
                        size_t lt = inner.find(L'<');
                        while (lt != std::wstring::npos) {
                            if (lt + 1 < inner.length() && isAsciiIdentStart(inner[lt + 1])) {
                                size_t ne = lt + 1;
                                while (ne < inner.length() && isAsciiTagChar(inner[ne])) ++ne;
                                std::wstring tLower;
                                for (size_t x = lt + 1; x < ne; ++x) tLower.push_back(static_cast<wchar_t>(towlower(inner[x])));
                                std::wstring closeNeedle = L"</" + tLower + L">";
                                if (inner.find(closeNeedle, ne) == std::wstring::npos &&
                                    line.find(closeNeedle, closeQ + 1) != std::wstring::npos) {
                                    hasDanglingClose = true;
                                    break;
                                }
                            }
                            lt = inner.find(L'<', lt + 1);
                        }
                        if (!hasDanglingClose) {
                            c = closeQ;
                            continue;
                        }
                    }
                }
            }

            if (line[c] == L'<') {
                // In HTML, a tag bracket MUST be followed by an ASCII letter, '/', or '!'
                if (c + 1 >= line.length()) continue;
                wchar_t nextCh = line[c + 1];
                const bool isClosingCandidate = (nextCh == L'/' && c + 2 < line.length() && isAsciiIdentStart(line[c + 2]));
                const bool isOpeningCandidate = isAsciiIdentStart(nextCh) || nextCh == L'!';
                if (!isClosingCandidate && !isOpeningCandidate) {
                    continue;
                }
                size_t tagStartLine = l;
                size_t tagStartCol = c;

                // Find closing '>' within a sane tag window, aborting if an unquoted '<' or operator appears first
                bool foundClose = false;
                bool abortedByLt = false;
                size_t endLine = l;
                size_t endCol = 0;
                bool inQuotes = false;
                wchar_t quoteChar = L'\0';
                bool inTagPhp = false;
                bool inUnquotedAttrValue = false;

                size_t curScanL = l;
                size_t curScanC = c + 1;

                while (curScanL < buffer.GetLineCount() && (curScanL - l) <= 40) {
                    const auto& scanText = buffer.GetLine(curScanL);
                    for (size_t k = curScanC; k < scanText.length(); ++k) {
                        wchar_t ch = scanText[k];
                        if (inTagPhp) {
                            if (ch == L'?' && k + 1 < scanText.length() && scanText[k + 1] == L'>') {
                                inTagPhp = false;
                                k++;
                            }
                            continue;
                        }
                        if (ch == L'<' && k + 1 < scanText.length() && scanText[k + 1] == L'?') {
                            inTagPhp = true;
                            k++;
                            continue;
                        }
                        if (inQuotes) {
                            if (ch == quoteChar) inQuotes = false;
                        } else {
                            if (ch == L'"' || ch == L'\'' || ch == L'`') {
                                inQuotes = true;
                                quoteChar = ch;
                                inUnquotedAttrValue = false;
                            } else if (ch == L'=') {
                                inUnquotedAttrValue = true;
                            } else if (iswspace(ch)) {
                                inUnquotedAttrValue = false;
                            } else if (ch == L'<') {
                                abortedByLt = true;
                                break;
                            } else if (nextCh != L'!' && !inUnquotedAttrValue &&
                                       (ch == L'&' || ch == L'|' || ch == L';' || ch == L',' ||
                                        ch == L'+' || ch == L'?' || ch == L'~' || ch == L'^')) {
                                abortedByLt = true;
                                break;
                            } else if (ch == L'>') {
                                endLine = curScanL;
                                endCol = k;
                                foundClose = true;
                                break;
                            }
                        }
                    }
                    if (foundClose || abortedByLt) break;
                    curScanL++;
                    curScanC = 0;
                    inUnquotedAttrValue = false;
                }

                if (abortedByLt) {
                    // Literal '<' inside prose/expression (e.g. a<b && c</p>), NOT an HTML tag
                    continue;
                }
                if (!foundClose) {
                    // Only flag '<' as unclosed tag bracket when the scan reached EOF on an actual tag prefix without quotes
                    continue;
                }

                std::wstring tagContent;
                if (endLine == l) {
                    tagContent = line.substr(c + 1, endCol - (c + 1));
                    c = endCol;
                } else {
                    tagContent = line.substr(c + 1);
                    for (size_t nl = l + 1; nl < endLine; ++nl) {
                        tagContent += L" " + buffer.GetLine(nl);
                    }
                    tagContent += L" " + buffer.GetLine(endLine).substr(0, endCol);
                    l = endLine;
                    c = endCol;
                }

                size_t lastNonWs = tagContent.find_last_not_of(L" \t\r\n");
                if (lastNonWs != std::wstring::npos) {
                    tagContent = tagContent.substr(0, lastNonWs + 1);
                }

                if (tagContent.empty()) continue;
                // Validate extracted tag name consists strictly of valid ASCII tag chars followed by delimiter
                if (tagContent.front() != L'!' && tagContent.front() != L'?') {
                    size_t tnStart = (tagContent.front() == L'/') ? 1 : 0;
                    if (tnStart >= tagContent.length() || !isAsciiIdentStart(tagContent[tnStart])) continue;
                    size_t tnEnd = tnStart;
                    while (tnEnd < tagContent.length() && isAsciiTagChar(tagContent[tnEnd])) ++tnEnd;
                    if (tnEnd < tagContent.length() && tagContent[tnEnd] != L'/' && !iswspace(tagContent[tnEnd])) {
                        continue; // Reject non-tag forms like <string, mixed> or <a+b>
                    }
                }

                // Duplicate ID check: id="..."
                size_t idPos = tagContent.find(L"id=\"");
                if (idPos != std::wstring::npos) {
                    size_t idValStart = idPos + 4;
                    size_t idValEnd = tagContent.find(L'"', idValStart);
                    if (idValEnd != std::wstring::npos) {
                        std::wstring idVal = tagContent.substr(idValStart, idValEnd - idValStart);
                        if (!idVal.empty()) {
                            auto it = seenIds.find(idVal);
                            if (it != seenIds.end()) {
                                diagnostics.push_back({ tagStartLine, tagStartCol, idVal.length(),
                                    L"HTML Validation: Duplicate ID '" + idVal + L"'. Element IDs must be unique within an HTML document", 2 });
                            } else {
                                seenIds[idVal] = { tagStartLine, tagStartCol };
                            }
                        }
                    }
                }

                // Self-closing: e.g. <div /> or multiline <meta ... />
                if (!tagContent.empty() && tagContent.back() == L'/') {
                    continue;
                }

                // Closing tag: </tag>
                if (tagContent.front() == L'/') {
                    std::wstring closeName = tagContent.substr(1);
                    size_t sp = closeName.find_first_of(L" \t\r\n");
                    if (sp != std::wstring::npos) closeName = closeName.substr(0, sp);
                    for (auto& ch : closeName) ch = static_cast<wchar_t>(towlower(ch));

                    if (tagStack.empty()) {
                        diagnostics.push_back({ tagStartLine, tagStartCol, closeName.length() + 3,
                            L"Syntax Error: Unexpected closing tag </" + closeName + L">", 1 });
                    } else {
                        std::wstring expected = tagStack.back().name;
                        for (auto& ch : expected) ch = static_cast<wchar_t>(towlower(ch));
                        if (expected == closeName) {
                            tagStack.pop_back();
                        } else {
                            diagnostics.push_back({ tagStartLine, tagStartCol, closeName.length() + 3,
                                L"Syntax Error: Mismatched tag: expected </" + tagStack.back().name + L"> but found </" + closeName + L">", 1 });

                            int matchIdx = -1;
                            for (int i = static_cast<int>(tagStack.size()) - 1; i >= 0; --i) {
                                std::wstring curName = tagStack[i].name;
                                for (auto& ch : curName) ch = static_cast<wchar_t>(towlower(ch));
                                if (curName == closeName) {
                                    matchIdx = i;
                                    break;
                                }
                            }
                            if (matchIdx >= 0) {
                                tagStack.erase(tagStack.begin() + matchIdx, tagStack.end());
                            } else {
                                tagStack.pop_back();
                            }
                        }
                    }
                } else {
                    // Opening tag
                    size_t sp = tagContent.find_first_of(L" \t\r\n/");
                    std::wstring tagName = (sp == std::wstring::npos) ? tagContent : tagContent.substr(0, sp);
                    if (!tagName.empty() && !isVoidTag(tagName)) {
                        std::wstring lowerTag = tagName;
                        for (auto& ch : lowerTag) ch = static_cast<wchar_t>(towlower(ch));
                        if (lowerTag == L"script" || lowerTag == L"style" ||
                            lowerTag == L"textarea" || lowerTag == L"title") {
                            // Raw text element: scan until </script>, </style>, </textarea>, or </title>
                            std::wstring endPattern = L"</" + lowerTag;
                            bool foundRawEnd = false;
                            size_t blockStartLine = l + 1;

                            while (l < buffer.GetLineCount()) {
                                const auto& rawLine = buffer.GetLine(l);
                                std::wstring rawLower = rawLine;
                                for (auto& ch : rawLower) ch = static_cast<wchar_t>(towlower(ch));
                                size_t foundPos = rawLower.find(endPattern, (l == tagStartLine) ? c + 1 : 0);
                                if (foundPos != std::wstring::npos) {
                                    size_t closeGt = rawLine.find(L'>', foundPos);
                                    if (closeGt != std::wstring::npos) {
                                        c = closeGt;
                                        foundRawEnd = true;
                                        break;
                                    }
                                }
                                l++;
                                c = 0;
                            }
                            if (!foundRawEnd) {
                                diagnostics.push_back({ tagStartLine, tagStartCol, tagName.length() + 2,
                                    L"Syntax Error: Unclosed <" + tagName + L"> block (missing </" + tagName + L">)", 1 });
                            } else {
                                // Image 3: Production-Ready Validation on embedded styles and scripts!
                                if (lowerTag == L"style" && validateStyles && l > blockStartLine) {
                                    auto styleDiags = ValidateCss(buffer, blockStartLine, l);
                                    diagnostics.insert(diagnostics.end(), styleDiags.begin(), styleDiags.end());
                                } else if (lowerTag == L"script" && validateScripts && l > blockStartLine) {
                                    auto scriptDiags = ValidateJavaScript(buffer, blockStartLine, l);
                                    diagnostics.insert(diagnostics.end(), scriptDiags.begin(), scriptDiags.end());
                                }
                            }
                        } else {
                            tagStack.push_back({ tagName, tagStartLine, tagStartCol });
                        }
                    }
                }
            }
        }
    }

    while (!tagStack.empty()) {
        const auto& t = tagStack.back();
        diagnostics.push_back({ t.line, t.col, t.name.length() + 2,
            L"Syntax Error: Unclosed HTML tag <" + t.name + L"> (missing </" + t.name + L">)", 1 });
        tagStack.pop_back();
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateYaml(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& fullLine = buffer.GetLine(l);
        if (fullLine.empty()) continue;

        // In YAML, tab indentation is strictly prohibited!
        size_t firstNonSpace = fullLine.find_first_not_of(L" ");
        if (firstNonSpace != std::wstring::npos && fullLine[firstNonSpace] == L'\t') {
            diagnostics.push_back({ l, firstNonSpace, 1, L"Syntax Error: Tab characters are not allowed for indentation in YAML", 1 });
            continue;
        }

        // Strip comments that begin with '#' (preceded by whitespace or at start of line)
        // unless '#' is inside quotes
        std::wstring line = fullLine;
        bool inSq = false, inDq = false;
        size_t commentPos = std::wstring::npos;
        for (size_t i = 0; i < line.length(); ++i) {
            wchar_t c = line[i];
            if (c == L'\'' && !inDq) inSq = !inSq;
            else if (c == L'"' && !inSq && (i == 0 || line[i - 1] != L'\\')) inDq = !inDq;
            else if (c == L'#' && !inSq && !inDq) {
                if (i == 0 || iswspace(line[i - 1])) {
                    commentPos = i;
                    break;
                }
            }
        }
        if (commentPos != std::wstring::npos) {
            line = line.substr(0, commentPos);
        }

        size_t start = line.find_first_not_of(L" \t");
        if (start == std::wstring::npos) continue;

        // Skip list marker '- '
        if (line[start] == L'-' && start + 1 < line.length() && iswspace(line[start + 1])) {
            start = line.find_first_not_of(L" \t", start + 2);
            if (start == std::wstring::npos) continue;
        }

        // Helper to check for unclosed quotes ONLY when a scalar actually begins with a quote delimiter
        auto checkQuotedScalar = [&](size_t pos, const std::wstring& context) {
            if (pos >= line.length()) return;
            wchar_t q = line[pos];
            if (q == L'\'') {
                bool closed = false;
                for (size_t i = pos + 1; i < line.length(); ++i) {
                    if (line[i] == L'\'') {
                        if (i + 1 < line.length() && line[i + 1] == L'\'') {
                            i++; // Skip escaped quote ''
                        } else {
                            closed = true;
                            break;
                        }
                    }
                }
                if (!closed) {
                    diagnostics.push_back({ l, pos, line.length() - pos, L"Syntax Error: Unclosed single quote in YAML " + context, 1 });
                }
            } else if (q == L'"') {
                bool closed = false;
                for (size_t i = pos + 1; i < line.length(); ++i) {
                    if (line[i] == L'\\') {
                        i++; // Skip escaped char
                        continue;
                    }
                    if (line[i] == L'"') {
                        closed = true;
                        break;
                    }
                }
                if (!closed) {
                    diagnostics.push_back({ l, pos, line.length() - pos, L"Syntax Error: Unclosed double quote in YAML " + context, 1 });
                }
            }
        };

        // Check if key starts with quote
        if (line[start] == L'\'' || line[start] == L'"') {
            checkQuotedScalar(start, L"key");
        }

        // Find ':' delimiter for key-value pair
        size_t colon = line.find(L':');
        if (colon != std::wstring::npos) {
            size_t valStart = line.find_first_not_of(L" \t", colon + 1);
            if (valStart != std::wstring::npos) {
                // In YAML, apostrophes inside unquoted plain strings (e.g. "device's camera") are plain text!
                // Only inspect quotes if the value explicitly begins with a quote delimiter.
                if (line[valStart] == L'\'' || line[valStart] == L'"') {
                    checkQuotedScalar(valStart, L"value");
                }
            }
        }
    }

    return diagnostics;
}

namespace {

[[nodiscard]] std::wstring ToLowerSV(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    return s;
}

[[nodiscard]] std::wstring TrimSV(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

enum class JValKind { Unknown, String, Number, Boolean, Null, Object, Array };

struct TopJsonField {
    std::wstring key;
    size_t line = 0;
    size_t keyCol = 0;
    size_t valLine = 0;
    size_t valCol = 0;
    JValKind kind = JValKind::Unknown;
    std::wstring scalarValue;
    std::vector<TopJsonField> objectChildren;
};

struct JsonScanner {
    const std::vector<std::wstring>& lines;
    size_t l = 0;
    size_t c = 0;

    bool Eof() const { return l >= lines.size(); }

    void SkipWsAndComments(bool allowComments) {
        while (!Eof()) {
            const auto& ln = lines[l];
            if (c >= ln.size()) { ++l; c = 0; continue; }
            wchar_t ch = ln[c];
            if (ch == L' ' || ch == L'\t' || ch == L'\r') { ++c; continue; }
            if (allowComments && ch == L'/' && c + 1 < ln.size()) {
                if (ln[c + 1] == L'/') { ++l; c = 0; continue; }
                if (ln[c + 1] == L'*') {
                    c += 2;
                    while (!Eof()) {
                        const auto& cln = lines[l];
                        size_t endp = cln.find(L"*/", c);
                        if (endp != std::wstring::npos) { c = endp + 2; break; }
                        ++l; c = 0;
                    }
                    continue;
                }
            }
            break;
        }
    }

    wchar_t Peek() const {
        if (Eof() || c >= lines[l].size()) return 0;
        return lines[l][c];
    }

    bool ParseString(std::wstring& out) {
        out.clear();
        if (Peek() != L'"') return false;
        ++c;
        while (!Eof()) {
            const auto& ln = lines[l];
            if (c >= ln.size()) { ++l; c = 0; return true; }
            wchar_t ch = ln[c++];
            if (ch == L'\\') {
                if (c < ln.size()) out.push_back(ln[c++]);
                continue;
            }
            if (ch == L'"') return true;
            out.push_back(ch);
        }
        return false;
    }

    void SkipBalanced(wchar_t openCh, wchar_t closeCh, bool allowComments) {
        int depth = 1;
        ++c;
        bool inStr = false;
        while (!Eof() && depth > 0) {
            const auto& ln = lines[l];
            if (c >= ln.size()) { ++l; c = 0; inStr = false; continue; }
            wchar_t ch = ln[c++];
            if (inStr) {
                if (ch == L'\\') { ++c; continue; }
                if (ch == L'"') inStr = false;
                continue;
            }
            if (allowComments && ch == L'/' && c < ln.size() && ln[c] == L'/') {
                ++l; c = 0; continue;
            }
            if (ch == L'"') inStr = true;
            else if (ch == openCh) ++depth;
            else if (ch == closeCh) --depth;
        }
    }

    void ParseValueMeta(JValKind& outKind, std::wstring& outScalar, std::vector<TopJsonField>* outObjChildren,
                        int objDepth, bool allowComments) {
        SkipWsAndComments(allowComments);
        if (Eof()) { outKind = JValKind::Unknown; return; }
        wchar_t ch = Peek();
        if (ch == L'"') {
            outKind = JValKind::String;
            ParseString(outScalar);
        } else if (ch == L'{') {
            outKind = JValKind::Object;
            if (outObjChildren && objDepth < 2) {
                *outObjChildren = ParseObjectFields(allowComments, objDepth + 1);
            } else {
                SkipBalanced(L'{', L'}', allowComments);
            }
        } else if (ch == L'[') {
            outKind = JValKind::Array;
            SkipBalanced(L'[', L']', allowComments);
        } else {
            size_t startC = c;
            const auto& ln = lines[l];
            while (c < ln.size() && ln[c] != L',' && ln[c] != L'}' && ln[c] != L']' &&
                   ln[c] != L' ' && ln[c] != L'\t' && ln[c] != L'\r' && ln[c] != L'/') {
                ++c;
            }
            outScalar = ln.substr(startC, c - startC);
            if (outScalar == L"true" || outScalar == L"false") outKind = JValKind::Boolean;
            else if (outScalar == L"null") outKind = JValKind::Null;
            else if (!outScalar.empty() && (iswdigit(outScalar[0]) || outScalar[0] == L'-')) outKind = JValKind::Number;
            else outKind = JValKind::Unknown;
        }
    }

    std::vector<TopJsonField> ParseObjectFields(bool allowComments, int objDepth) {
        std::vector<TopJsonField> fields;
        SkipWsAndComments(allowComments);
        if (Peek() != L'{') return fields;
        ++c;
        while (!Eof() && fields.size() < 256) {
            SkipWsAndComments(allowComments);
            if (Eof()) break;
            wchar_t ch = Peek();
            if (ch == L'}') { ++c; break; }
            if (ch == L',') { ++c; continue; }
            if (ch != L'"') { ++c; continue; }
            TopJsonField f;
            f.line = l;
            f.keyCol = c;
            if (!ParseString(f.key)) break;
            SkipWsAndComments(allowComments);
            if (Peek() == L':') ++c;
            SkipWsAndComments(allowComments);
            f.valLine = l;
            f.valCol = c;
            ParseValueMeta(f.kind, f.scalarValue, &f.objectChildren, objDepth, allowComments);
            fields.push_back(std::move(f));
        }
        return fields;
    }
};

struct LocalSchemaSpec {
    std::vector<std::wstring> requiredKeys;
    struct PropSpec {
        std::wstring expectedType;
    };
    std::unordered_map<std::wstring, PropSpec> properties;
};

[[nodiscard]] bool ReadLocalSchemaFileBounded(const std::wstring& docPath, const std::wstring& schemaRef,
                                               LocalSchemaSpec& outSpec) {
    if (schemaRef.empty()) return false;
    std::wstring lowRef = ToLowerSV(schemaRef);
    if (lowRef.rfind(L"http://", 0) == 0 || lowRef.rfind(L"https://", 0) == 0) {
        return false;
    }
    std::error_code ec;
    std::filesystem::path schemaPath(schemaRef);
    if (schemaPath.is_relative() && !docPath.empty()) {
        schemaPath = std::filesystem::path(docPath).parent_path() / schemaPath;
    }
    if (!std::filesystem::exists(schemaPath, ec) || !std::filesystem::is_regular_file(schemaPath, ec)) {
        return false;
    }
    auto fsz = std::filesystem::file_size(schemaPath, ec);
    if (ec || fsz > 256 * 1024) return false;

    std::ifstream in(schemaPath, std::ios::binary);
    if (!in.is_open()) return false;
    std::string utf8((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (utf8.empty()) return false;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wtext(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wtext.data(), wlen);

    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= wtext.size()) {
        size_t nl = wtext.find(L'\n', start);
        if (nl == std::wstring::npos) {
            lines.push_back(wtext.substr(start));
            break;
        }
        std::wstring ln = wtext.substr(start, nl - start);
        if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
        lines.push_back(std::move(ln));
        start = nl + 1;
    }

    auto extractStringArray = [](const std::vector<std::wstring>& srcLines, const std::wstring& keyName) {
        std::vector<std::wstring> vals;
        std::wstring needle = L"\"" + keyName + L"\"";
        for (size_t i = 0; i < srcLines.size(); ++i) {
            size_t pos = srcLines[i].find(needle);
            if (pos == std::wstring::npos) continue;
            size_t lb = srcLines[i].find(L'[', pos + needle.size());
            size_t lineIdx = i;
            if (lb == std::wstring::npos && i + 1 < srcLines.size()) {
                lb = srcLines[i + 1].find(L'[');
                lineIdx = i + 1;
            }
            if (lb == std::wstring::npos) continue;
            size_t curC = lb + 1;
            for (size_t l = lineIdx; l < srcLines.size() && l < lineIdx + 64; ++l) {
                const auto& ln = srcLines[l];
                size_t p = (l == lineIdx) ? curC : 0;
                while (p < ln.size()) {
                    if (ln[p] == L']') return vals;
                    if (ln[p] == L'"') {
                        size_t endQ = p + 1;
                        while (endQ < ln.size() && ln[endQ] != L'"') {
                            if (ln[endQ] == L'\\') ++endQ;
                            ++endQ;
                        }
                        if (endQ < ln.size()) {
                            vals.push_back(ln.substr(p + 1, endQ - p - 1));
                            p = endQ + 1;
                            continue;
                        }
                    }
                    ++p;
                }
            }
        }
        return vals;
    };

    outSpec.requiredKeys = extractStringArray(lines, L"required");

    JsonScanner sc{ lines };
    auto rootFields = sc.ParseObjectFields(true, 0);
    for (const auto& rf : rootFields) {
        if (rf.key == L"properties") {
            for (const auto& pf : rf.objectChildren) {
                LocalSchemaSpec::PropSpec spec;
                for (size_t li = pf.line; li < lines.size() && li < pf.line + 12; ++li) {
                    size_t tp = lines[li].find(L"\"type\"");
                    if (tp != std::wstring::npos) {
                        size_t q1 = lines[li].find(L'"', tp + 6);
                        if (q1 != std::wstring::npos) {
                            size_t q2 = lines[li].find(L'"', q1 + 1);
                            if (q2 != std::wstring::npos) {
                                spec.expectedType = ToLowerSV(lines[li].substr(q1 + 1, q2 - q1 - 1));
                                break;
                            }
                        }
                    }
                }
                outSpec.properties[pf.key] = std::move(spec);
            }
        }
    }
    return !outSpec.requiredKeys.empty() || !outSpec.properties.empty();
}

[[nodiscard]] bool JsonKindMatchesSchemaType(JValKind k, const std::wstring& expected) {
    if (expected == L"string") return k == JValKind::String;
    if (expected == L"number") return k == JValKind::Number;
    if (expected == L"integer") return k == JValKind::Number;
    if (expected == L"boolean") return k == JValKind::Boolean;
    if (expected == L"object") return k == JValKind::Object;
    if (expected == L"array") return k == JValKind::Array;
    if (expected == L"null") return k == JValKind::Null;
    return true;
}

} // namespace

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateJsonSchema(const std::wstring& filePath, const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;
    if (buffer.GetLineCount() == 0) return diagnostics;

    const auto& lines = buffer.GetLines();
    JsonScanner sc{ lines };
    sc.SkipWsAndComments(true);
    if (sc.Peek() != L'{') return diagnostics;
    auto rootFields = sc.ParseObjectFields(true, 0);

    std::wstring schemaRef;
    std::unordered_map<std::wstring, const TopJsonField*> rootByKey;
    for (const auto& f : rootFields) {
        if (f.key == L"$schema") schemaRef = f.scalarValue;
        rootByKey[f.key] = &f;
    }

    std::wstring fileNameLow = ToLowerSV(std::filesystem::path(filePath).filename().wstring());
    std::wstring schemaLow = ToLowerSV(schemaRef);

    if (!schemaRef.empty()) {
        LocalSchemaSpec spec;
        if (ReadLocalSchemaFileBounded(filePath, schemaRef, spec)) {
            for (const auto& req : spec.requiredKeys) {
                if (rootByKey.find(req) == rootByKey.end()) {
                    diagnostics.push_back({ 0, 0, 1,
                        L"Schema: Missing required property \"" + req + L"\" (from " + schemaRef + L")", 1,
                        L"$schema", L"missing-required" });
                }
            }
            for (const auto& f : rootFields) {
                auto it = spec.properties.find(f.key);
                if (it != spec.properties.end() && !it->second.expectedType.empty() && f.kind != JValKind::Unknown) {
                    if (!JsonKindMatchesSchemaType(f.kind, it->second.expectedType)) {
                        diagnostics.push_back({ f.valLine, f.valCol, (std::max<size_t>)(1, f.scalarValue.size()),
                            L"Schema: Property \"" + f.key + L"\" must be of type \"" + it->second.expectedType + L"\"", 1,
                            L"$schema", L"schema-type" });
                    }
                }
            }
        }
    }

    bool isPackageJson = (fileNameLow == L"package.json") || (schemaLow.find(L"package.schema.json") != std::wstring::npos);
    if (isPackageJson) {
        auto checkObjType = [&](const wchar_t* k) {
            auto it = rootByKey.find(k);
            if (it != rootByKey.end() && it->second->kind != JValKind::Object && it->second->kind != JValKind::Unknown) {
                diagnostics.push_back({ it->second->valLine, it->second->valCol, 4,
                    std::wstring(L"Schema (package.json): \"") + k + L"\" must be a JSON object", 1,
                    L"schema:package.json", L"package-type" });
            }
        };
        auto checkArrType = [&](const wchar_t* k) {
            auto it = rootByKey.find(k);
            if (it != rootByKey.end() && it->second->kind != JValKind::Array && it->second->kind != JValKind::Unknown) {
                diagnostics.push_back({ it->second->valLine, it->second->valCol, 4,
                    std::wstring(L"Schema (package.json): \"") + k + L"\" must be a JSON array", 1,
                    L"schema:package.json", L"package-type" });
            }
        };

        bool isPrivate = false;
        if (auto it = rootByKey.find(L"private"); it != rootByKey.end()) {
            if (it->second->kind != JValKind::Boolean) {
                diagnostics.push_back({ it->second->valLine, it->second->valCol, 5,
                    L"Schema (package.json): \"private\" must be a boolean (true or false)", 1,
                    L"schema:package.json", L"package-type" });
            } else {
                isPrivate = (it->second->scalarValue == L"true");
            }
        }
        if (auto it = rootByKey.find(L"name"); it != rootByKey.end()) {
            if (it->second->kind != JValKind::String || it->second->scalarValue.empty()) {
                diagnostics.push_back({ it->second->valLine, it->second->valCol, 4,
                    L"Schema (package.json): \"name\" must be a non-empty lowercase string", 1,
                    L"schema:package.json", L"package-name" });
            } else {
                for (wchar_t ch : it->second->scalarValue) {
                    if (ch >= L'A' && ch <= L'Z') {
                        diagnostics.push_back({ it->second->valLine, it->second->valCol, it->second->scalarValue.size() + 2,
                            L"Schema (package.json): \"name\" cannot contain uppercase letters", 1,
                            L"schema:package.json", L"package-name-case" });
                        break;
                    }
                    if (ch == L' ') {
                        diagnostics.push_back({ it->second->valLine, it->second->valCol, it->second->scalarValue.size() + 2,
                            L"Schema (package.json): \"name\" cannot contain whitespace", 1,
                            L"schema:package.json", L"package-name-ws" });
                        break;
                    }
                }
            }
        } else if (!isPrivate) {
            diagnostics.push_back({ 0, 0, 1,
                L"Schema (package.json): Missing recommended \"name\" field (or set \"private\": true)", 2,
                L"schema:package.json", L"package-missing-name" });
        }
        if (auto it = rootByKey.find(L"version"); it != rootByKey.end()) {
            if (it->second->kind != JValKind::String) {
                diagnostics.push_back({ it->second->valLine, it->second->valCol, 4,
                    L"Schema (package.json): \"version\" must be a semver string", 1,
                    L"schema:package.json", L"package-version" });
            } else if (std::count(it->second->scalarValue.begin(), it->second->scalarValue.end(), L'.') < 2) {
                diagnostics.push_back({ it->second->valLine, it->second->valCol, it->second->scalarValue.size() + 2,
                    L"Schema (package.json): \"version\" should follow semver format (major.minor.patch)", 2,
                    L"schema:package.json", L"package-semver" });
            }
        }
        checkObjType(L"scripts");
        checkObjType(L"dependencies");
        checkObjType(L"devDependencies");
        checkObjType(L"peerDependencies");
        checkObjType(L"optionalDependencies");
        checkObjType(L"engines");
        checkArrType(L"keywords");
        checkArrType(L"files");
    }

    bool isTsConfig = (fileNameLow == L"tsconfig.json" || fileNameLow == L"jsconfig.json" ||
                       fileNameLow.rfind(L"tsconfig.", 0) == 0 ||
                       schemaLow.find(L"tsconfig") != std::wstring::npos);
    if (isTsConfig) {
        static const std::unordered_set<std::wstring> kValidTsTopKeys = {
            L"$schema", L"compilerOptions", L"files", L"include", L"exclude", L"extends",
            L"references", L"watchOptions", L"typeAcquisition", L"buildOptions", L"ts-node"
        };
        for (const auto& f : rootFields) {
            if (kValidTsTopKeys.find(f.key) == kValidTsTopKeys.end()) {
                diagnostics.push_back({ f.line, f.keyCol, f.key.size() + 2,
                    L"Schema (tsconfig.json): Unknown top-level property \"" + f.key + L"\"", 2,
                    L"schema:tsconfig.json", L"tsconfig-unknown-key" });
            }
        }
        if (auto it = rootByKey.find(L"compilerOptions"); it != rootByKey.end()) {
            const auto* co = it->second;
            if (co->kind != JValKind::Object) {
                diagnostics.push_back({ co->valLine, co->valCol, 4,
                    L"Schema (tsconfig.json): \"compilerOptions\" must be a JSON object", 1,
                    L"schema:tsconfig.json", L"tsconfig-type" });
            } else {
                static const std::unordered_set<std::wstring> kBoolOpts = {
                    L"strict", L"noemit", L"esmoduleinterop", L"skiplibcheck", L"allowjs", L"checkjs",
                    L"declaration", L"declarationmap", L"sourcemap", L"isolatedmodules",
                    L"forceconsistentcasinginfilenames", L"resolvejsonmodule", L"incremental",
                    L"nounusedlocals", L"nounusedparameters", L"noimplicitany", L"strictnullchecks"
                };
                static const std::unordered_set<std::wstring> kValidTargets = {
                    L"es3", L"es5", L"es6", L"es2015", L"es2016", L"es2017", L"es2018", L"es2019",
                    L"es2020", L"es2021", L"es2022", L"es2023", L"es2024", L"esnext"
                };
                static const std::unordered_set<std::wstring> kValidModules = {
                    L"none", L"commonjs", L"amd", L"umd", L"system", L"es6", L"es2015", L"es2020",
                    L"es2022", L"esnext", L"node16", L"nodenext", L"preserve"
                };
                static const std::unordered_set<std::wstring> kValidJsx = {
                    L"preserve", L"react", L"react-jsx", L"react-jsxdev", L"react-native"
                };
                for (const auto& opt : co->objectChildren) {
                    std::wstring keyLow = ToLowerSV(opt.key);
                    if (kBoolOpts.count(keyLow) > 0 && opt.kind != JValKind::Boolean && opt.kind != JValKind::Unknown) {
                        diagnostics.push_back({ opt.valLine, opt.valCol, 5,
                            L"Schema (tsconfig.json): \"compilerOptions." + opt.key + L"\" expects a boolean", 1,
                            L"schema:tsconfig.json", L"tsconfig-bool" });
                    } else if (keyLow == L"target" && opt.kind == JValKind::String) {
                        if (kValidTargets.count(ToLowerSV(opt.scalarValue)) == 0) {
                            diagnostics.push_back({ opt.valLine, opt.valCol, opt.scalarValue.size() + 2,
                                L"Schema (tsconfig.json): Unrecognized compilerOptions.target \"" + opt.scalarValue + L"\"", 2,
                                L"schema:tsconfig.json", L"tsconfig-target" });
                        }
                    } else if (keyLow == L"module" && opt.kind == JValKind::String) {
                        if (kValidModules.count(ToLowerSV(opt.scalarValue)) == 0) {
                            diagnostics.push_back({ opt.valLine, opt.valCol, opt.scalarValue.size() + 2,
                                L"Schema (tsconfig.json): Unrecognized compilerOptions.module \"" + opt.scalarValue + L"\"", 2,
                                L"schema:tsconfig.json", L"tsconfig-module" });
                        }
                    } else if (keyLow == L"jsx" && opt.kind == JValKind::String) {
                        if (kValidJsx.count(ToLowerSV(opt.scalarValue)) == 0) {
                            diagnostics.push_back({ opt.valLine, opt.valCol, opt.scalarValue.size() + 2,
                                L"Schema (tsconfig.json): Invalid compilerOptions.jsx \"" + opt.scalarValue + L"\"", 1,
                                L"schema:tsconfig.json", L"tsconfig-jsx" });
                        }
                    }
                }
            }
        }
    }
    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateYamlSchema(const std::wstring& filePath, const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;
    if (buffer.GetLineCount() == 0) return diagnostics;

    const auto& lines = buffer.GetLines();
    std::wstring schemaRef;
    for (size_t i = 0; i < (std::min<size_t>)(lines.size(), 12); ++i) {
        size_t pos = lines[i].find(L"$schema=");
        if (pos != std::wstring::npos && lines[i].find(L'#') < pos) {
            schemaRef = TrimSV(lines[i].substr(pos + 8));
            break;
        }
    }

    struct YamlTopKey {
        std::wstring key;
        size_t line = 0;
        std::wstring valueText;
        JValKind inferredKind = JValKind::Unknown;
    };
    std::vector<YamlTopKey> topKeys;
    std::unordered_map<std::wstring, YamlTopKey> topByKey;

    for (size_t l = 0; l < lines.size(); ++l) {
        const auto& ln = lines[l];
        if (ln.empty() || ln[0] == L' ' || ln[0] == L'\t' || ln[0] == L'#') continue;
        if (ln.rfind(L"---", 0) == 0 || ln.rfind(L"...", 0) == 0) continue;
        size_t colon = ln.find(L':');
        if (colon == std::wstring::npos) continue;
        std::wstring key = TrimSV(ln.substr(0, colon));
        if (key.empty()) continue;
        if ((key.front() == L'"' && key.back() == L'"') || (key.front() == L'\'' && key.back() == L'\'')) {
            if (key.size() >= 2) key = key.substr(1, key.size() - 2);
        }
        std::wstring val = TrimSV(ln.substr(colon + 1));
        JValKind kind = JValKind::Unknown;
        if (val.empty() || val[0] == L'#') {
            if (l + 1 < lines.size() && (lines[l + 1].rfind(L"  -", 0) == 0 || lines[l + 1].rfind(L"\t-", 0) == 0 || lines[l + 1].rfind(L"- ", 0) == 0)) {
                kind = JValKind::Array;
            } else if (l + 1 < lines.size() && (lines[l + 1].rfind(L"  ", 0) == 0 || lines[l + 1].rfind(L"\t", 0) == 0)) {
                kind = JValKind::Object;
            }
        } else if (val.front() == L'[') {
            kind = JValKind::Array;
        } else if (val.front() == L'{') {
            kind = JValKind::Object;
        } else if (val == L"true" || val == L"false") {
            kind = JValKind::Boolean;
        } else {
            bool allDigits = !val.empty();
            for (wchar_t ch : val) {
                if (!iswdigit(ch) && ch != L'.' && ch != L'-') { allDigits = false; break; }
            }
            kind = allDigits ? JValKind::Number : JValKind::String;
        }
        YamlTopKey yk{ key, l, val, kind };
        topByKey[key] = yk;
        topKeys.push_back(std::move(yk));
    }

    if (!schemaRef.empty()) {
        LocalSchemaSpec spec;
        if (ReadLocalSchemaFileBounded(filePath, schemaRef, spec)) {
            for (const auto& req : spec.requiredKeys) {
                if (topByKey.find(req) == topByKey.end()) {
                    diagnostics.push_back({ 0, 0, 1,
                        L"YAML Schema: Missing required top-level key \"" + req + L"\" (from " + schemaRef + L")", 1,
                        L"$schema", L"yaml-missing-required" });
                }
            }
            for (const auto& tk : topKeys) {
                auto it = spec.properties.find(tk.key);
                if (it != spec.properties.end() && !it->second.expectedType.empty() && tk.inferredKind != JValKind::Unknown) {
                    if (!JsonKindMatchesSchemaType(tk.inferredKind, it->second.expectedType)) {
                        diagnostics.push_back({ tk.line, 0, tk.key.size(),
                            L"YAML Schema: Key \"" + tk.key + L"\" must be of type \"" + it->second.expectedType + L"\"", 1,
                            L"$schema", L"yaml-schema-type" });
                    }
                }
            }
        }
    }

    std::wstring pathLow = ToLowerSV(filePath);
    std::replace(pathLow.begin(), pathLow.end(), L'\\', L'/');
    std::wstring baseNameLow = ToLowerSV(std::filesystem::path(filePath).filename().wstring());
    std::wstring schemaLow = ToLowerSV(schemaRef);

    bool isGhWorkflow = (pathLow.find(L".github/workflows/") != std::wstring::npos) ||
                        (schemaLow.find(L"github-workflow") != std::wstring::npos);
    if (isGhWorkflow) {
        static const std::unordered_set<std::wstring> kGhTopKeys = {
            L"name", L"run-name", L"on", L"permissions", L"env", L"defaults", L"concurrency", L"jobs"
        };
        for (const auto& tk : topKeys) {
            if (tk.key == L"true") {
                diagnostics.push_back({ tk.line, 0, 4,
                    L"YAML Schema (GitHub Workflow): Key \"true\" looks like unquoted YAML 1.1 boolean for \"on\"; wrap key in quotes or use \"on:\"", 2,
                    L"schema:github-workflow", L"gh-on-true" });
            } else if (kGhTopKeys.count(tk.key) == 0) {
                diagnostics.push_back({ tk.line, 0, tk.key.size(),
                    L"YAML Schema (GitHub Workflow): Unknown top-level key \"" + tk.key + L"\"", 2,
                    L"schema:github-workflow", L"gh-unknown-key" });
            }
        }
        if (topByKey.find(L"on") == topByKey.end() && topByKey.find(L"true") == topByKey.end()) {
            diagnostics.push_back({ 0, 0, 1,
                L"YAML Schema (GitHub Workflow): Missing required trigger key \"on:\"", 1,
                L"schema:github-workflow", L"gh-missing-on" });
        }
        if (topByKey.find(L"jobs") == topByKey.end()) {
            diagnostics.push_back({ 0, 0, 1,
                L"YAML Schema (GitHub Workflow): Missing required key \"jobs:\"", 1,
                L"schema:github-workflow", L"gh-missing-jobs" });
        }
    }

    bool isDockerCompose = (baseNameLow.rfind(L"docker-compose", 0) == 0 ||
                            baseNameLow == L"compose.yml" || baseNameLow == L"compose.yaml" ||
                            schemaLow.find(L"compose-spec") != std::wstring::npos);
    if (isDockerCompose) {
        if (topByKey.find(L"services") == topByKey.end() && topByKey.find(L"include") == topByKey.end()) {
            diagnostics.push_back({ 0, 0, 1,
                L"YAML Schema (Docker Compose): Missing top-level \"services:\" mapping", 1,
                L"schema:docker-compose", L"compose-missing-services" });
        }
        if (auto it = topByKey.find(L"version"); it != topByKey.end()) {
            diagnostics.push_back({ it->second.line, 0, 7,
                L"YAML Schema (Docker Compose): Top-level \"version:\" is obsolete in modern Compose v2 specs", 3,
                L"schema:docker-compose", L"compose-obsolete-version" });
        }
    }

    return diagnostics;
}

static size_t CalculateLevenshtein(const std::wstring& s1, const std::wstring& s2) {
    const size_t m = s1.length();
    const size_t n = s2.length();
    if (m == 0) return n;
    if (n == 0) return m;
    if (std::abs(static_cast<long long>(m) - static_cast<long long>(n)) > 3) return 99;

    std::vector<size_t> v0(n + 1);
    std::vector<size_t> v1(n + 1);
    for (size_t i = 0; i <= n; ++i) v0[i] = i;

    for (size_t i = 0; i < m; ++i) {
        v1[0] = i + 1;
        for (size_t j = 0; j < n; ++j) {
            size_t cost = (towlower(s1[i]) == towlower(s2[j])) ? 0 : 1;
            v1[j + 1] = (std::min)({ v1[j] + 1, v0[j + 1] + 1, v0[j] + cost });
        }
        v0 = v1;
    }
    return v0[n];
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidatePython(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct BracketInfo {
        wchar_t ch;
        size_t line;
        size_t col;
    };
    std::vector<BracketInfo> bracketStack;

    bool inTripleString = false;
    wchar_t tripleChar = 0;
    bool hasSeenTabs = false;
    bool hasSeenSpaces = false;

    std::vector<size_t> indentStack = { 0 };
    bool expectIndent = false;
    size_t colonLine = 0;
    bool inExplicitContinuation = false;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);

        // Check leading indentation and calculate visual indentation
        size_t indentCol = 0;
        size_t visualIndent = 0;
        bool lineHasTab = false;
        bool lineHasSpace = false;
        while (indentCol < line.length() && (line[indentCol] == L' ' || line[indentCol] == L'	')) {
            if (line[indentCol] == L'	') {
                lineHasTab = true;
                visualIndent = (visualIndent / 8 + 1) * 8;
            }
            if (line[indentCol] == L' ') {
                lineHasSpace = true;
                visualIndent++;
            }
            indentCol++;
        }

        if (indentCol < line.length() && line[indentCol] != L'#') {
            if (lineHasTab && lineHasSpace) {
                diagnostics.push_back({ l, 0, indentCol, L"Python: Inconsistent indentation (mixed tabs and spaces)", 1 });
            } else if (lineHasTab) {
                hasSeenTabs = true;
                if (hasSeenSpaces) {
                    diagnostics.push_back({ l, 0, indentCol, L"Python: TabError: inconsistent use of tabs and spaces in indentation", 1 });
                }
            } else if (lineHasSpace) {
                hasSeenSpaces = true;
                if (hasSeenTabs) {
                    diagnostics.push_back({ l, 0, indentCol, L"Python: TabError: inconsistent use of tabs and spaces in indentation", 1 });
                }
            }
        }

        bool startedInTripleString = inTripleString;
        bool startedInBracket = !bracketStack.empty();

        bool inString = false;
        wchar_t strChar = 0;
        size_t strStart = 0;
        size_t commentCol = std::wstring::npos;

        for (size_t c = indentCol; c < line.length(); ++c) {
            wchar_t ch = line[c];

            if (inTripleString) {
                if (ch == tripleChar && c + 2 < line.length() && line[c + 1] == tripleChar && line[c + 2] == tripleChar) {
                    inTripleString = false;
                    c += 2;
                }
                continue;
            }

            if (inString) {
                if (ch == L'\\') {
                    c++;
                    continue;
                }
                if (ch == strChar) {
                    inString = false;
                }
                continue;
            }

            if ((ch == L'"' || ch == L'\'') && c + 2 < line.length() && line[c + 1] == ch && line[c + 2] == ch) {
                inTripleString = true;
                tripleChar = ch;
                c += 2;
                continue;
            }

            if (ch == L'"' || ch == L'\'') {
                inString = true;
                strChar = ch;
                strStart = c;
                continue;
            }

            if (ch == L'#') {
                commentCol = c;
                break;
            }

            if (ch == L'(' || ch == L'[' || ch == L'{') {
                bracketStack.push_back({ ch, l, c });
            } else if (ch == L')' || ch == L']' || ch == L'}') {
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, std::wstring(L"Python: Unmatched closing '") + ch + L"'", 1 });
                } else {
                    wchar_t expected = (ch == L')') ? L'(' : (ch == L']') ? L'[' : L'{';
                    if (bracketStack.back().ch != expected) {
                        diagnostics.push_back({ l, c, 1, std::wstring(L"Python: Mismatched bracket: expected '") + (bracketStack.back().ch == L'(' ? L')' : bracketStack.back().ch == L'[' ? L']' : L'}') + L"' but found '" + ch + L"'", 1 });
                        bracketStack.pop_back();
                    } else {
                        bracketStack.pop_back();
                    }
                }
            }
        }

        if (inString && !inTripleString) {
            diagnostics.push_back({ l, strStart, line.length() - strStart, L"Python: EOL while scanning string literal", 1 });
        }

        std::wstring codePart = (commentCol != std::wstring::npos) ? line.substr(0, commentCol) : line;
        size_t codeStart = 0;
        while (codeStart < codePart.length() && (codePart[codeStart] == L' ' || codePart[codeStart] == L'	')) {
            codeStart++;
        }
        std::wstring trimmed = (codeStart < codePart.length()) ? codePart.substr(codeStart) : L"";
        while (!trimmed.empty() && iswspace(trimmed.back())) {
            trimmed.pop_back();
        }

        // Indentation validation (Python PEP 8 / official Python syntax rules)
        if (!startedInTripleString && !inExplicitContinuation && !startedInBracket && !trimmed.empty()) {
            if (expectIndent) {
                if (visualIndent <= indentStack.back()) {
                    diagnostics.push_back({ l, 0, (std::max)(line.length(), 1ULL), L"Python: IndentationError: expected an indented block", 1 });
                    expectIndent = false;
                } else {
                    indentStack.push_back(visualIndent);
                    expectIndent = false;
                }
            } else {
                if (visualIndent > indentStack.back()) {
                    diagnostics.push_back({ l, 0, (std::max)(line.length(), 1ULL), L"Python: IndentationError: unexpected indent", 1 });
                } else if (visualIndent < indentStack.back()) {
                    while (!indentStack.empty() && indentStack.back() > visualIndent) {
                        indentStack.pop_back();
                    }
                    if (indentStack.empty() || indentStack.back() != visualIndent) {
                        diagnostics.push_back({ l, 0, (std::max)(indentCol, 1ULL), L"Python: IndentationError: unindent does not match any outer indentation level", 1 });
                        indentStack.push_back(visualIndent);
                    }
                }
            }
        }

        if (!inTripleString && bracketStack.empty() && !trimmed.empty()) {
            static const std::vector<std::wstring> colonKws = {
                L"def ", L"class ", L"if ", L"elif ", L"else", L"for ", L"while ", L"try", L"except", L"finally", L"with ",
                L"async def ", L"async for ", L"async with ", L"match ", L"case "
            };
            for (const auto& kw : colonKws) {
                if (trimmed == kw || trimmed.rfind(kw, 0) == 0) {
                    if (trimmed.back() != L':') {
                        diagnostics.push_back({ l, indentCol + trimmed.length() - 1, 1, L"Python: SyntaxError: expected ':' at end of statement", 1 });
                    }
                    break;
                }
            }

            if (trimmed.back() == L':') {
                expectIndent = true;
                colonLine = l;
            }
        }

        if (!inTripleString && bracketStack.empty() && !trimmed.empty() && trimmed.back() == L'\\') {
            inExplicitContinuation = true;
        } else if (bracketStack.empty()) {
            inExplicitContinuation = false;
        }
    }

    if (expectIndent) {
        diagnostics.push_back({ colonLine, 0, (std::max)(buffer.GetLine(colonLine).length(), 1ULL), L"Python: IndentationError: expected an indented block", 1 });
    }

    if (inTripleString) {
        diagnostics.push_back({ buffer.GetLineCount() > 0 ? buffer.GetLineCount() - 1 : 0, 0, 1, L"Python: EOF while scanning triple-quoted string literal", 1 });
    }

    for (const auto& b : bracketStack) {
        diagnostics.push_back({ b.line, b.col, 1, std::wstring(L"Python: Unclosed bracket '") + b.ch + L"'", 1 });
    }

    // -------------------------------------------------------------
    // Real-time Python Scope & Undefined Variable Analysis (PEP 8 / Flake8 F821 / NameError)
    // -------------------------------------------------------------
    static const std::unordered_set<std::wstring> pyBuiltins = {
        L"abs", L"aiter", L"all", L"anext", L"any", L"ascii", L"bin", L"bool", L"breakpoint",
        L"bytearray", L"bytes", L"callable", L"chr", L"classmethod", L"compile", L"complex",
        L"delattr", L"dict", L"dir", L"divmod", L"enumerate", L"eval", L"exec", L"filter",
        L"float", L"format", L"frozenset", L"getattr", L"globals", L"hasattr", L"hash", L"help",
        L"hex", L"id", L"input", L"int", L"isinstance", L"issubclass", L"iter", L"len", L"list",
        L"locals", L"map", L"max", L"memoryview", L"min", L"next", L"object", L"oct", L"open",
        L"ord", L"pow", L"print", L"property", L"range", L"repr", L"reversed", L"round", L"set",
        L"setattr", L"slice", L"sorted", L"staticmethod", L"str", L"sum", L"super", L"tuple",
        L"type", L"vars", L"zip", L"__import__",
        L"True", L"False", L"None", L"Ellipsis", L"NotImplemented",
        L"__name__", L"__file__", L"__doc__", L"__package__", L"__annotations__", L"__builtins__",
        L"__debug__", L"__dict__", L"__class__", L"self", L"cls",
        L"BaseException", L"Exception", L"ArithmeticError", L"BufferError", L"LookupError",
        L"AssertionError", L"AttributeError", L"EOFError", L"FloatingPointError", L"GeneratorExit",
        L"ImportError", L"ModuleNotFoundError", L"IndexError", L"KeyError", L"KeyboardInterrupt",
        L"MemoryError", L"NameError", L"NotImplementedError", L"OSError", L"OverflowError",
        L"RecursionError", L"ReferenceError", L"RuntimeError", L"StopIteration", L"StopAsyncIteration",
        L"SyntaxError", L"IndentationError", L"TabError", L"SystemError", L"SystemExit", L"TypeError",
        L"UnboundLocalError", L"UnicodeError", L"UnicodeEncodeError", L"UnicodeDecodeError",
        L"UnicodeTranslateError", L"ValueError", L"ZeroDivisionError", L"EnvironmentError",
        L"IOError", L"WindowsError", L"BlockingIOError", L"ChildProcessError", L"ConnectionError",
        L"BrokenPipeError", L"ConnectionAbortedError", L"ConnectionRefusedError", L"ConnectionResetError",
        L"FileExistsError", L"FileNotFoundError", L"InterruptedError", L"IsADirectoryError",
        L"NotADirectoryError", L"PermissionError", L"ProcessLookupError", L"TimeoutError",
        L"Warning", L"UserWarning", L"DeprecationWarning", L"PendingDeprecationWarning",
        L"SyntaxWarning", L"RuntimeWarning", L"FutureWarning", L"ImportWarning", L"UnicodeWarning",
        L"BytesWarning", L"ResourceWarning"
    };

    static const std::unordered_set<std::wstring> pyKeywords = {
        L"and", L"as", L"assert", L"async", L"await", L"break", L"case", L"class", L"continue",
        L"def", L"del", L"elif", L"else", L"except", L"finally", L"for", L"from", L"global",
        L"if", L"import", L"in", L"is", L"lambda", L"match", L"nonlocal", L"not", L"or",
        L"pass", L"raise", L"return", L"try", L"while", L"with", L"yield"
    };

    std::unordered_set<std::wstring> definedSymbols;
    bool hasStarImport = false;

    auto isIdentChar = [](wchar_t c) {
        return iswalnum(c) || c == L'_';
    };
    auto isIdentStart = [](wchar_t c) {
        return iswalpha(c) || c == L'_';
    };

    // Pass 1: Collect defined symbols across the entire document
    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);
        std::wstring code = line;
        bool inStr = false;
        wchar_t qChar = 0;
        for (size_t i = 0; i < code.length(); ++i) {
            wchar_t ch = code[i];
            if (!inStr && (ch == L'"' || ch == L'\'')) {
                inStr = true;
                qChar = ch;
            } else if (inStr && ch == qChar && (i == 0 || code[i - 1] != L'\\')) {
                inStr = false;
            } else if (!inStr && ch == L'#') {
                code = code.substr(0, i);
                break;
            }
        }
        size_t start = 0;
        while (start < code.length() && (code[start] == L' ' || code[start] == L'\t')) start++;
        std::wstring trimmedCode = code.substr(start);
        if (trimmedCode.empty()) continue;

        if (trimmedCode.rfind(L"from ", 0) == 0) {
            size_t impPos = trimmedCode.find(L" import ");
            if (impPos != std::wstring::npos) {
                std::wstring fromMod = trimmedCode.substr(5, impPos - 5);
                size_t fm = 0;
                while (fm < fromMod.length()) {
                    while (fm < fromMod.length() && !isIdentStart(fromMod[fm])) fm++;
                    if (fm >= fromMod.length()) break;
                    size_t fStart = fm;
                    while (fm < fromMod.length() && isIdentChar(fromMod[fm])) fm++;
                    std::wstring mTok = fromMod.substr(fStart, fm - fStart);
                    if (!mTok.empty() && pyKeywords.find(mTok) == pyKeywords.end()) {
                        definedSymbols.insert(mTok);
                    }
                }
                std::wstring impPart = trimmedCode.substr(impPos + 8);
                if (impPart.find(L'*') != std::wstring::npos) {
                    hasStarImport = true;
                } else {
                    size_t p = 0;
                    while (p < impPart.length()) {
                        while (p < impPart.length() && (impPart[p] == L' ' || impPart[p] == L',' || impPart[p] == L'(' || impPart[p] == L')')) p++;
                        if (p >= impPart.length()) break;
                        size_t tokenStart = p;
                        while (p < impPart.length() && isIdentChar(impPart[p])) p++;
                        if (p == tokenStart) { p++; continue; }
                        std::wstring sym = impPart.substr(tokenStart, p - tokenStart);
                        if (!sym.empty() && pyKeywords.find(sym) == pyKeywords.end()) {
                            definedSymbols.insert(sym);
                        }
                        while (p < impPart.length() && impPart[p] == L' ') p++;
                        if (p + 3 <= impPart.length() && impPart.substr(p, 3) == L"as ") {
                            p += 3;
                            while (p < impPart.length() && impPart[p] == L' ') p++;
                            size_t aliasStart = p;
                            while (p < impPart.length() && isIdentChar(impPart[p])) p++;
                            std::wstring aliasSym = impPart.substr(aliasStart, p - aliasStart);
                            if (!aliasSym.empty() && pyKeywords.find(aliasSym) == pyKeywords.end()) {
                                definedSymbols.insert(aliasSym);
                            }
                        }
                    }
                }
            }
            continue;
        }

        if (trimmedCode.rfind(L"import ", 0) == 0) {
            std::wstring impPart = trimmedCode.substr(7);
            size_t p = 0;
            while (p < impPart.length()) {
                while (p < impPart.length() && (impPart[p] == L' ' || impPart[p] == L',')) p++;
                if (p >= impPart.length()) break;
                size_t tokenStart = p;
                while (p < impPart.length() && (isIdentChar(impPart[p]) || impPart[p] == L'.')) p++;
                if (p == tokenStart) { p++; continue; }
                std::wstring mod = impPart.substr(tokenStart, p - tokenStart);
                size_t dot = mod.find(L'.');
                std::wstring baseMod = (dot != std::wstring::npos) ? mod.substr(0, dot) : mod;
                if (!baseMod.empty() && pyKeywords.find(baseMod) == pyKeywords.end()) {
                    definedSymbols.insert(baseMod);
                }

                while (p < impPart.length() && impPart[p] == L' ') p++;
                if (p + 3 <= impPart.length() && impPart.substr(p, 3) == L"as ") {
                    p += 3;
                    while (p < impPart.length() && impPart[p] == L' ') p++;
                    size_t aliasStart = p;
                    while (p < impPart.length() && isIdentChar(impPart[p])) p++;
                    std::wstring aliasMod = impPart.substr(aliasStart, p - aliasStart);
                    if (!aliasMod.empty() && pyKeywords.find(aliasMod) == pyKeywords.end()) {
                        definedSymbols.insert(aliasMod);
                    }
                }
            }
            continue;
        }

        if (trimmedCode.rfind(L"def ", 0) == 0 || trimmedCode.rfind(L"async def ", 0) == 0) {
            size_t defOffset = (trimmedCode.rfind(L"async def ", 0) == 0) ? 10 : 4;
            size_t p = defOffset;
            while (p < trimmedCode.length() && trimmedCode[p] == L' ') p++;
            size_t fnStart = p;
            while (p < trimmedCode.length() && isIdentChar(trimmedCode[p])) p++;
            if (p > fnStart) {
                definedSymbols.insert(trimmedCode.substr(fnStart, p - fnStart));
            }
            size_t openP = trimmedCode.find(L'(', p);
            size_t closeP = trimmedCode.rfind(L')');
            if (openP != std::wstring::npos && closeP != std::wstring::npos && closeP > openP) {
                std::wstring params = trimmedCode.substr(openP + 1, closeP - openP - 1);
                size_t pp = 0;
                while (pp < params.length()) {
                    while (pp < params.length() && (params[pp] == L' ' || params[pp] == L'*' || params[pp] == L'/')) pp++;
                    if (pp >= params.length()) break;
                    if (isIdentStart(params[pp])) {
                        size_t pStart = pp;
                        while (pp < params.length() && isIdentChar(params[pp])) pp++;
                        std::wstring paramName = params.substr(pStart, pp - pStart);
                        if (!paramName.empty() && pyKeywords.find(paramName) == pyKeywords.end()) {
                            definedSymbols.insert(paramName);
                        }
                    }
                    while (pp < params.length() && params[pp] != L',') pp++;
                    if (pp < params.length() && params[pp] == L',') pp++;
                }
            }
            continue;
        }

        if (trimmedCode.rfind(L"class ", 0) == 0) {
            size_t p = 6;
            while (p < trimmedCode.length() && trimmedCode[p] == L' ') p++;
            size_t clStart = p;
            while (p < trimmedCode.length() && isIdentChar(trimmedCode[p])) p++;
            if (p > clStart) {
                definedSymbols.insert(trimmedCode.substr(clStart, p - clStart));
            }
            continue;
        }

        if (trimmedCode.rfind(L"for ", 0) == 0 || trimmedCode.rfind(L"async for ", 0) == 0) {
            size_t forOffset = (trimmedCode.rfind(L"async for ", 0) == 0) ? 10 : 4;
            size_t inPos = trimmedCode.find(L" in ", forOffset);
            if (inPos != std::wstring::npos) {
                std::wstring varsPart = trimmedCode.substr(forOffset, inPos - forOffset);
                size_t vp = 0;
                while (vp < varsPart.length()) {
                    while (vp < varsPart.length() && !isIdentStart(varsPart[vp])) vp++;
                    if (vp >= varsPart.length()) break;
                    size_t vStart = vp;
                    while (vp < varsPart.length() && isIdentChar(varsPart[vp])) vp++;
                    std::wstring vName = varsPart.substr(vStart, vp - vStart);
                    if (!vName.empty() && pyKeywords.find(vName) == pyKeywords.end()) {
                        definedSymbols.insert(vName);
                    }
                }
            }
            continue;
        }

        size_t asPos = trimmedCode.find(L" as ");
        if (asPos != std::wstring::npos && (trimmedCode.rfind(L"with ", 0) == 0 || trimmedCode.rfind(L"async with ", 0) == 0 || trimmedCode.rfind(L"except ", 0) == 0)) {
            size_t p = asPos + 4;
            while (p < trimmedCode.length() && trimmedCode[p] == L' ') p++;
            size_t varStart = p;
            while (p < trimmedCode.length() && isIdentChar(trimmedCode[p])) p++;
            if (p > varStart) {
                std::wstring asVar = trimmedCode.substr(varStart, p - varStart);
                if (!asVar.empty() && pyKeywords.find(asVar) == pyKeywords.end()) {
                    definedSymbols.insert(asVar);
                }
            }
        }

        size_t eqPos = std::wstring::npos;
        int bDepth = 0;
        for (size_t i = 0; i < code.length(); ++i) {
            wchar_t c = code[i];
            if (c == L'(' || c == L'[' || c == L'{') bDepth++;
            else if (c == L')' || c == L']' || c == L'}') { if (bDepth > 0) bDepth--; }
            else if (bDepth == 0 && c == L'=') {
                if (i + 1 < code.length() && code[i + 1] == L'=') { i++; continue; }
                if (i > 0 && (code[i - 1] == L'!' || code[i - 1] == L'<' || code[i - 1] == L'>')) continue;
                eqPos = i;
                break;
            }
        }
        if (eqPos != std::wstring::npos) {
            std::wstring lhs = code.substr(0, eqPos);
            while (!lhs.empty() && (lhs.back() == L'+' || lhs.back() == L'-' || lhs.back() == L'*' || lhs.back() == L'/' || lhs.back() == L':' || iswspace(lhs.back()))) {
                lhs.pop_back();
            }
            size_t lp = 0;
            while (lp < lhs.length()) {
                while (lp < lhs.length() && !isIdentStart(lhs[lp])) lp++;
                if (lp >= lhs.length()) break;
                bool isAttr = (lp > 0 && lhs[lp - 1] == L'.');
                size_t idStart = lp;
                while (lp < lhs.length() && isIdentChar(lhs[lp])) lp++;
                std::wstring idName = lhs.substr(idStart, lp - idStart);
                if (!isAttr && !idName.empty() && pyKeywords.find(idName) == pyKeywords.end()) {
                    definedSymbols.insert(idName);
                }
            }
        }
    }

    // Pass 2: Detect undefined names and typos
    if (!hasStarImport) {
        bool inTripleStr = false;
        wchar_t tChar = 0;
        for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
            const auto& line = buffer.GetLine(l);
            if (!inTripleStr) {
                size_t ls = 0;
                while (ls < line.length() && (line[ls] == L' ' || line[ls] == L'\t')) ls++;
                std::wstring trimmedLine = line.substr(ls);
                if (trimmedLine.rfind(L"import ", 0) == 0 || trimmedLine == L"import" ||
                    trimmedLine.rfind(L"from ", 0) == 0 || trimmedLine == L"from") {
                    continue;
                }
            }
            bool inSingleStr = false;
            wchar_t sChar = 0;
            size_t c = 0;
            int parenDepth = 0;

            while (c < line.length()) {
                wchar_t ch = line[c];

                if (inTripleStr) {
                    if (ch == tChar && c + 2 < line.length() && line[c + 1] == tChar && line[c + 2] == tChar) {
                        inTripleStr = false;
                        c += 3;
                    } else {
                        c++;
                    }
                    continue;
                }

                if (inSingleStr) {
                    if (ch == L'\\') { c += 2; continue; }
                    if (ch == sChar) inSingleStr = false;
                    c++;
                    continue;
                }

                if ((ch == L'"' || ch == L'\'') && c + 2 < line.length() && line[c + 1] == ch && line[c + 2] == ch) {
                    inTripleStr = true;
                    tChar = ch;
                    c += 3;
                    continue;
                }
                if (ch == L'"' || ch == L'\'') {
                    inSingleStr = true;
                    sChar = ch;
                    c++;
                    continue;
                }

                if (ch == L'#') {
                    break;
                }

                if (ch == L'(' || ch == L'[' || ch == L'{') { parenDepth++; c++; continue; }
                if (ch == L')' || ch == L']' || ch == L'}') { if (parenDepth > 0) parenDepth--; c++; continue; }

                if (isIdentStart(ch)) {
                    bool isAttr = false;
                    size_t backP = c;
                    while (backP > 0 && (line[backP - 1] == L' ' || line[backP - 1] == L'\t')) backP--;
                    if (backP > 0 && line[backP - 1] == L'.') {
                        isAttr = true;
                    }

                    size_t tokenStart = c;
                    while (c < line.length() && isIdentChar(line[c])) c++;
                    std::wstring sym = line.substr(tokenStart, c - tokenStart);

                    if (isAttr) {
                        continue;
                    }

                    if (parenDepth > 0) {
                        size_t nextP = c;
                        while (nextP < line.length() && (line[nextP] == L' ' || line[nextP] == L'\t')) nextP++;
                        if (nextP < line.length() && line[nextP] == L'=' && (nextP + 1 >= line.length() || line[nextP + 1] != L'=')) {
                            continue;
                        }
                    }

                    if (parenDepth == 0) {
                        size_t nextP = c;
                        while (nextP < line.length() && (line[nextP] == L' ' || line[nextP] == L'\t')) nextP++;
                        if (nextP < line.length() && line[nextP] == L'=' && (nextP + 1 >= line.length() || line[nextP + 1] != L'=')) {
                            continue;
                        }
                        if (nextP + 1 < line.length() && (line[nextP] == L'+' || line[nextP] == L'-' || line[nextP] == L'*' || line[nextP] == L'/') && line[nextP + 1] == L'=') {
                            continue;
                        }
                    }

                    if (pyKeywords.find(sym) != pyKeywords.end() ||
                        pyBuiltins.find(sym) != pyBuiltins.end() ||
                        definedSymbols.find(sym) != definedSymbols.end()) {
                        continue;
                    }

                    std::wstring bestMatch;
                    size_t bestDist = 3;

                    for (const auto& candidate : pyBuiltins) {
                        size_t d = CalculateLevenshtein(sym, candidate);
                        if (d < bestDist) {
                            bestDist = d;
                            bestMatch = candidate;
                        }
                    }
                    for (const auto& candidate : definedSymbols) {
                        size_t d = CalculateLevenshtein(sym, candidate);
                        if (d < bestDist) {
                            bestDist = d;
                            bestMatch = candidate;
                        }
                    }

                    std::wstring msg = L"Python: NameError: name '" + sym + L"' is not defined";
                    if (!bestMatch.empty() && bestDist <= 2) {
                        msg += L". Did you mean: '" + bestMatch + L"'?";
                    }

                    diagnostics.push_back({ l, tokenStart, sym.length(), msg, 1 });
                    continue;
                }

                c++;
            }
        }
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidatePhp(const Buffer& buffer) {
    std::vector<lsp::Diagnostic> diagnostics;

    struct BracketInfo {
        wchar_t ch;
        size_t line;
        size_t col;
    };
    std::vector<BracketInfo> bracketStack;

    bool inPhp = false;
    bool inBlockComment = false;
    size_t blockCommentLine = 0;
    size_t blockCommentCol = 0;

    // In PHP, single-quoted, double-quoted, backtick, and heredoc/nowdoc strings validly span multiple lines!
    bool inDoubleQuote = false;
    bool inSingleQuote = false;
    bool inBacktick = false;
    std::wstring heredocEnd;
    size_t quoteStartLine = 0;
    size_t quoteStartCol = 0;

    for (size_t l = 0; l < buffer.GetLineCount(); ++l) {
        const auto& line = buffer.GetLine(l);

        // If inside PHP heredoc/nowdoc, check for closing identifier at start of line and skip line content
        if (inPhp && !heredocEnd.empty()) {
            size_t ws = 0;
            while (ws < line.length() && (line[ws] == L' ' || line[ws] == L'\t')) ++ws;
            if (line.compare(ws, heredocEnd.length(), heredocEnd) == 0) {
                size_t after = ws + heredocEnd.length();
                if (after >= line.length() || line[after] == L';' || line[after] == L',' ||
                    line[after] == L')' || iswspace(line[after])) {
                    heredocEnd.clear();
                }
            }
            continue;
        }

        for (size_t c = 0; c < line.length(); ++c) {
            wchar_t ch = line[c];

            // Check PHP open/close tags
            if (!inPhp) {
                if (c + 4 < line.length() && line.substr(c, 5) == L"<?php") {
                    inPhp = true;
                    c += 4;
                    continue;
                } else if (c + 2 < line.length() && line.substr(c, 3) == L"<?=") {
                    inPhp = true;
                    c += 2;
                    continue;
                }
                continue; // Skip HTML outside PHP tags
            } else {
                if (!inDoubleQuote && !inSingleQuote && !inBacktick && !inBlockComment) {
                    if (c + 1 < line.length() && line.substr(c, 2) == L"?>") {
                        inPhp = false;
                        c += 1;
                        continue;
                    }
                }
            }

            // Inside multi-line comment /* ... */
            if (inBlockComment) {
                if (ch == L'*' && c + 1 < line.length() && line[c + 1] == L'/') {
                    inBlockComment = false;
                    c++;
                }
                continue;
            }

            // Inside string literals (can span multiple lines in PHP)
            if (inDoubleQuote) {
                if (ch == L'\\') { c++; continue; }
                if (ch == L'"') { inDoubleQuote = false; }
                continue;
            }
            if (inSingleQuote) {
                if (ch == L'\\') { c++; continue; }
                if (ch == L'\'') { inSingleQuote = false; }
                continue;
            }
            if (inBacktick) {
                if (ch == L'\\') { c++; continue; }
                if (ch == L'`') { inBacktick = false; }
                continue;
            }

            // Single line comments // or # (ignoring #[...] PHP 8 attributes)
            if ((ch == L'/' && c + 1 < line.length() && line[c + 1] == L'/') ||
                (ch == L'#' && !(c + 1 < line.length() && line[c + 1] == L'['))) {
                break;
            }

            // Block comment /*
            if (ch == L'/' && c + 1 < line.length() && line[c + 1] == L'*') {
                inBlockComment = true;
                blockCommentLine = l;
                blockCommentCol = c;
                c++;
                continue;
            }

            // Heredoc / Nowdoc start: <<<EOD, <<<'EOD', <<<"EOD"
            if (ch == L'<' && c + 2 < line.length() && line[c + 1] == L'<' && line[c + 2] == L'<') {
                size_t hp = c + 3;
                while (hp < line.length() && (line[hp] == L' ' || line[hp] == L'\t')) ++hp;
                wchar_t hq = L'\0';
                if (hp < line.length() && (line[hp] == L'\'' || line[hp] == L'"')) hq = line[hp++];
                size_t idStart = hp;
                while (hp < line.length() && (iswalnum(line[hp]) || line[hp] == L'_')) ++hp;
                if (hp > idStart && (hq == L'\0' || (hp < line.length() && line[hp] == hq))) {
                    heredocEnd = line.substr(idStart, hp - idStart);
                    quoteStartLine = l;
                    quoteStartCol = c;
                    break;
                }
            }

            // String start
            if (ch == L'"') {
                inDoubleQuote = true;
                quoteStartLine = l;
                quoteStartCol = c;
                continue;
            }
            if (ch == L'\'') {
                inSingleQuote = true;
                quoteStartLine = l;
                quoteStartCol = c;
                continue;
            }
            if (ch == L'`') {
                inBacktick = true;
                quoteStartLine = l;
                quoteStartCol = c;
                continue;
            }

            // Brackets
            if (ch == L'{' || ch == L'(' || ch == L'[') {
                bracketStack.push_back({ ch, l, c });
            } else if (ch == L'}' || ch == L')' || ch == L']') {
                if (bracketStack.empty()) {
                    diagnostics.push_back({ l, c, 1, std::wstring(L"PHP: Unmatched closing '") + ch + L"'", 1 });
                } else {
                    wchar_t openCh = bracketStack.back().ch;
                    bool match = (openCh == L'{' && ch == L'}') ||
                                 (openCh == L'(' && ch == L')') ||
                                 (openCh == L'[' && ch == L']');
                    if (match) {
                        bracketStack.pop_back();
                    } else {
                        diagnostics.push_back({ l, c, 1, std::wstring(L"PHP: Mismatched bracket: expected '") +
                            (openCh == L'{' ? L'}' : (openCh == L'(' ? L')' : L']')) +
                            L"', found '" + ch + L"'", 1 });
                        bracketStack.pop_back();
                    }
                }
            }
        }
    }

    if (inDoubleQuote || inSingleQuote || inBacktick || !heredocEnd.empty()) {
        const auto& qLine = buffer.GetLine(quoteStartLine);
        size_t len = (qLine.length() > quoteStartCol) ? (qLine.length() - quoteStartCol) : 1;
        diagnostics.push_back({ quoteStartLine, quoteStartCol, len, L"PHP: Unclosed string literal", 1 });
    }

    if (inBlockComment) {
        diagnostics.push_back({ blockCommentLine, blockCommentCol, 2, L"PHP: Unclosed block comment '/*'", 1 });
    }

    for (const auto& b : bracketStack) {
        diagnostics.push_back({ b.line, b.col, 1, std::wstring(L"PHP: Unclosed bracket '") + b.ch + L"'", 1 });
    }

    return diagnostics;
}

std::vector<lsp::Diagnostic> SyntaxValidator::ValidateReactAndESLint(const Buffer& buffer, bool isJsx) {
    std::vector<lsp::Diagnostic> diagnostics;
    size_t lineCount = buffer.GetLineCount();
    if (lineCount == 0) return diagnostics;

    static const std::unordered_set<std::wstring> reactHooks = {
        L"useState", L"useEffect", L"useContext", L"useReducer",
        L"useCallback", L"useMemo", L"useRef", L"useImperativeHandle",
        L"useLayoutEffect", L"useId", L"useDeferredValue",
        L"useTransition", L"useSyncExternalStore", L"useInsertionEffect"
    };

    std::unordered_set<std::wstring> importedSymbols;
    std::unordered_set<std::wstring> definedSymbols;
    bool hasReactImport = false;

    for (size_t l = 0; l < lineCount; ++l) {
        const std::wstring& rawLine = buffer.GetLine(l);
        size_t s = 0;
        while (s < rawLine.length() && iswspace(rawLine[s])) s++;
        if (s >= rawLine.length()) continue;
        std::wstring line = rawLine.substr(s);

        if ((line.starts_with(L"import ") || line.starts_with(L"import{") || line.find(L"require(") != std::wstring::npos) &&
            (line.find(L"'react'") != std::wstring::npos || line.find(L"\"react\"") != std::wstring::npos)) {
            hasReactImport = true;
        }

        if (line.starts_with(L"import ") || line.starts_with(L"import{")) {
            size_t bOpen = line.find(L'{');
            size_t bClose = (bOpen != std::wstring::npos) ? line.find(L'}', bOpen) : std::wstring::npos;
            if (bOpen != std::wstring::npos && bClose != std::wstring::npos) {
                std::wstring inside = line.substr(bOpen + 1, bClose - bOpen - 1);
                size_t p = 0;
                while (p < inside.length()) {
                    while (p < inside.length() && (inside[p] == L' ' || inside[p] == L'\t' || inside[p] == L',')) p++;
                    if (p >= inside.length()) break;
                    size_t tokenStart = p;
                    while (p < inside.length() && (iswalnum(inside[p]) || inside[p] == L'_' || inside[p] == L'$')) p++;
                    std::wstring sym = inside.substr(tokenStart, p - tokenStart);
                    while (p < inside.length() && (inside[p] == L' ' || inside[p] == L'\t')) p++;
                    if (p + 3 <= inside.length() && inside.substr(p, 3) == L"as ") {
                        p += 3;
                        while (p < inside.length() && (inside[p] == L' ' || inside[p] == L'\t')) p++;
                        size_t aliasStart = p;
                        while (p < inside.length() && (iswalnum(inside[p]) || inside[p] == L'_' || inside[p] == L'$')) p++;
                        sym = inside.substr(aliasStart, p - aliasStart);
                    }
                    if (!sym.empty()) {
                        importedSymbols.insert(sym);
                    }
                }

                size_t impEnd = 7;
                if (bOpen > impEnd) {
                    std::wstring prefixPart = line.substr(impEnd, bOpen - impEnd);
                    size_t comma = prefixPart.find(L',');
                    if (comma != std::wstring::npos) {
                        std::wstring defSym = prefixPart.substr(0, comma);
                        while (!defSym.empty() && iswspace(defSym.front())) defSym.erase(defSym.begin());
                        while (!defSym.empty() && iswspace(defSym.back())) defSym.pop_back();
                        if (!defSym.empty()) importedSymbols.insert(defSym);
                    }
                }
            } else {
                size_t fromPos = line.find(L"from");
                if (fromPos != std::wstring::npos && fromPos > 7) {
                    std::wstring middle = line.substr(7, fromPos - 7);
                    size_t asPos = middle.find(L"* as ");
                    if (asPos != std::wstring::npos) {
                        std::wstring starSym = middle.substr(asPos + 5);
                        while (!starSym.empty() && iswspace(starSym.front())) starSym.erase(starSym.begin());
                        while (!starSym.empty() && iswspace(starSym.back())) starSym.pop_back();
                        if (!starSym.empty()) importedSymbols.insert(starSym);
                    } else {
                        while (!middle.empty() && iswspace(middle.front())) middle.erase(middle.begin());
                        while (!middle.empty() && iswspace(middle.back())) middle.pop_back();
                        if (!middle.empty()) importedSymbols.insert(middle);
                    }
                }
            }
        } else if (line.find(L"require(") != std::wstring::npos) {
            size_t bOpen = line.find(L'{');
            size_t bClose = (bOpen != std::wstring::npos) ? line.find(L'}', bOpen) : std::wstring::npos;
            if (bOpen != std::wstring::npos && bClose != std::wstring::npos && bClose < line.find(L"require(")) {
                std::wstring inside = line.substr(bOpen + 1, bClose - bOpen - 1);
                size_t p = 0;
                while (p < inside.length()) {
                    while (p < inside.length() && (inside[p] == L' ' || inside[p] == L'\t' || inside[p] == L',')) p++;
                    if (p >= inside.length()) break;
                    size_t tokenStart = p;
                    while (p < inside.length() && (iswalnum(inside[p]) || inside[p] == L'_' || inside[p] == L'$')) p++;
                    std::wstring sym = inside.substr(tokenStart, p - tokenStart);
                    if (!sym.empty()) importedSymbols.insert(sym);
                }
            } else if (line.starts_with(L"const ") || line.starts_with(L"let ") || line.starts_with(L"var ")) {
                size_t eqPos = line.find(L'=');
                if (eqPos != std::wstring::npos && eqPos < line.find(L"require(")) {
                    size_t vStart = (line.starts_with(L"const ") ? 6 : 4);
                    std::wstring vName = line.substr(vStart, eqPos - vStart);
                    while (!vName.empty() && iswspace(vName.front())) vName.erase(vName.begin());
                    while (!vName.empty() && iswspace(vName.back())) vName.pop_back();
                    if (!vName.empty()) importedSymbols.insert(vName);
                }
            }
        }

        auto extractDecl = [&](const std::wstring& kw) {
            size_t kp = line.find(kw);
            if (kp != std::wstring::npos) {
                size_t p = kp + kw.length();
                while (p < line.length() && iswspace(line[p])) p++;
                size_t nameStart = p;
                while (p < line.length() && (iswalnum(line[p]) || line[p] == L'_' || line[p] == L'$')) p++;
                if (p > nameStart) {
                    definedSymbols.insert(line.substr(nameStart, p - nameStart));
                }
            }
        };
        extractDecl(L"const ");
        extractDecl(L"let ");
        extractDecl(L"var ");
        extractDecl(L"function ");
    }

    const bool checkReactComponents = isJsx || hasReactImport;
    int conditionDepth = 0;
    int loopDepth = 0;
    std::vector<int> controlStack; // 1 = condition (if/else/switch), 2 = loop (for/while)

    struct ComponentState {
        std::wstring name;
        size_t declLine = 0;
        int braceDepth = 0;
        int parenDepth = 0;
        bool hasOpenedBody = false;
        bool hasReturn = false;
        bool hasReturnedValue = false;
    };
    std::vector<ComponentState> componentStack;

    struct JsxTagInfo {
        std::wstring name;
        size_t line;
        size_t col;
        int exprCurlyDepth = 0; ///< Nested '{ ... }' depth while inside this JSX element's children
    };
    std::vector<JsxTagInfo> jsxStack;

    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"meta", L"param", L"source", L"track", L"wbr"
    };

    bool inBacktickAcrossLines = false;
    bool inBlockCommentAcrossLines = false;

    auto sanitizeLine = [&](const std::wstring& rawLine, std::wstring& outCode, std::wstring& outCleanCode) {
        outCode = rawLine;
        outCleanCode = rawLine;
        bool inS = false, inD = false, inB = inBacktickAcrossLines, inBlk = inBlockCommentAcrossLines;
        for (size_t i = 0; i < rawLine.length(); ++i) {
            wchar_t ch = rawLine[i];
            if (inBlk) {
                outCleanCode[i] = L' ';
                if (ch == L'*' && i + 1 < rawLine.length() && rawLine[i + 1] == L'/') {
                    outCleanCode[i + 1] = L' ';
                    inBlk = false;
                    ++i;
                }
                continue;
            }
            if (ch == L'\\' && (inS || inD || inB)) {
                outCleanCode[i] = L' ';
                if (i + 1 < rawLine.length()) { outCleanCode[i + 1] = L' '; ++i; }
                continue;
            }
            if (inB && ch == L'$' && i + 1 < rawLine.length() && rawLine[i + 1] == L'{') {
                outCleanCode[i] = L' ';
                outCleanCode[i + 1] = L' ';
                i += 2;
                int tplDepth = 1;
                bool tS = false, tD = false, tB = false;
                while (i < rawLine.length() && tplDepth > 0) {
                    wchar_t tc = rawLine[i];
                    if (tc == L'\\' && (tS || tD || tB) && i + 1 < rawLine.length()) {
                        outCleanCode[i] = L' '; outCleanCode[i + 1] = L' ';
                        i += 2; continue;
                    }
                    if (!tD && !tB && tc == L'\'') { tS = !tS; outCleanCode[i] = L' '; }
                    else if (!tS && !tB && tc == L'"') { tD = !tD; outCleanCode[i] = L' '; }
                    else if (!tS && !tD && tc == L'`') { tB = !tB; outCleanCode[i] = L' '; }
                    else if (tS || tD || tB) { outCleanCode[i] = L' '; }
                    else if (tc == L'{') { ++tplDepth; }
                    else if (tc == L'}') {
                        --tplDepth;
                        if (tplDepth == 0) { outCleanCode[i] = L' '; break; }
                    }
                    ++i;
                }
                continue;
            }
            if (!inS && !inD && !inB) {
                if (ch == L'/' && i + 1 < rawLine.length() && rawLine[i + 1] == L'*') {
                    inBlk = true;
                    outCleanCode[i] = L' ';
                    outCleanCode[i + 1] = L' ';
                    ++i;
                    continue;
                }
                if (ch == L'/' && IsJsRegexStart(rawLine, i)) {
                    size_t r = ScanJsRegexEnd(rawLine, i);
                    if (r != std::wstring::npos) {
                        for (size_t k = i; k <= r; ++k) outCleanCode[k] = L' ';
                        i = r;
                        continue;
                    }
                }
                if (ch == L'/' && i + 1 < rawLine.length() && rawLine[i + 1] == L'/') {
                    if (!(i > 0 && rawLine[i - 1] == L':') && rawLine.find(L"</", i + 2) == std::wstring::npos) {
                        outCode = rawLine.substr(0, i);
                        outCleanCode = outCleanCode.substr(0, i);
                        break;
                    }
                }
            }
            if (ch == L'\'' && !inD && !inB) {
                if (!inS && i > 0 && (iswalnum(rawLine[i - 1]) || rawLine[i - 1] == L'_')) {
                    continue; // Contraction / apostrophe in JSX prose
                }
                inS = !inS;
                outCleanCode[i] = L' ';
                continue;
            }
            if (ch == L'"' && !inS && !inB) { inD = !inD; outCleanCode[i] = L' '; continue; }
            if (ch == L'`' && !inS && !inD) { inB = !inB; outCleanCode[i] = L' '; continue; }
            if (inS || inD || inB) {
                outCleanCode[i] = L' ';
            }
        }
        inBacktickAcrossLines = inB;
        inBlockCommentAcrossLines = inBlk;
    };

    for (size_t l = 0; l < lineCount; ++l) {
        const std::wstring& line = buffer.GetLine(l);
        if (line.empty()) continue;

        std::wstring code;
        std::wstring cleanCode;
        sanitizeLine(line, code, cleanCode);

        auto findControlKeyword = [](const std::wstring& str) -> int {
            auto isWord = [&](size_t idx, const std::wstring& kw) -> bool {
                if (idx + kw.length() > str.length()) return false;
                if (str.substr(idx, kw.length()) != kw) return false;
                bool bOk = (idx == 0 || (!iswalnum(str[idx - 1]) && str[idx - 1] != L'_' && str[idx - 1] != L'$'));
                size_t aIdx = idx + kw.length();
                bool aOk = (aIdx >= str.length() || (!iswalnum(str[aIdx]) && str[aIdx] != L'_' && str[aIdx] != L'$'));
                return bOk && aOk;
            };
            for (int p = static_cast<int>(str.length()) - 1; p >= 0; --p) {
                if (isWord(p, L"if") || isWord(p, L"else") || isWord(p, L"switch")) return 1;
                if (isWord(p, L"for") || isWord(p, L"while")) return 2;
                if (str[p] == L';' || isWord(p, L"function") || isWord(p, L"return")) return 0;
            }
            return 0;
        };

        // 0. Component Declaration Detection: accurate identification of function Comp() or const Comp = ...
        if (checkReactComponents) {
            size_t fnPos = cleanCode.find(L"function ");
            if (fnPos != std::wstring::npos) {
                size_t nameStart = fnPos + 9;
                while (nameStart < cleanCode.length() && iswspace(cleanCode[nameStart])) nameStart++;
                size_t nameEnd = nameStart;
                while (nameEnd < cleanCode.length() && (iswalnum(cleanCode[nameEnd]) || cleanCode[nameEnd] == L'_' || cleanCode[nameEnd] == L'$')) nameEnd++;
                if (nameEnd > nameStart) {
                    std::wstring compName = cleanCode.substr(nameStart, nameEnd - nameStart);
                    if (iswupper(compName[0]) && compName != L"React" && compName != L"Component" && compName != L"PureComponent" && compName != L"Fragment") {
                        componentStack.push_back({ compName, l, 0, 0, false, false, false });
                    }
                }
            } else if ((cleanCode.find(L"const ") != std::wstring::npos || cleanCode.find(L"let ") != std::wstring::npos || cleanCode.find(L"export ") != std::wstring::npos) &&
                       cleanCode.find(L"=>") != std::wstring::npos) {
                size_t varPos = cleanCode.find(L"const ");
                if (varPos == std::wstring::npos) varPos = cleanCode.find(L"let ");
                if (varPos != std::wstring::npos) {
                    size_t nameStart = varPos + (cleanCode.substr(varPos, 4) == L"let " ? 4 : 6);
                    while (nameStart < cleanCode.length() && iswspace(cleanCode[nameStart])) nameStart++;
                    size_t nameEnd = nameStart;
                    while (nameEnd < cleanCode.length() && (iswalnum(cleanCode[nameEnd]) || cleanCode[nameEnd] == L'_' || cleanCode[nameEnd] == L'$')) nameEnd++;
                    if (nameEnd > nameStart) {
                        std::wstring compName = cleanCode.substr(nameStart, nameEnd - nameStart);
                        if (iswupper(compName[0]) && compName != L"React" && compName != L"Component" && compName != L"PureComponent" && compName != L"Fragment") {
                            componentStack.push_back({ compName, l, 0, 0, false, false, false });
                        }
                    }
                }
            }
        }

        // Track conditions, loops, and component scopes
        for (size_t i = 0; i < cleanCode.length(); ++i) {
            wchar_t cChar = cleanCode[i];
            if (cChar == L'(') {
                if (!componentStack.empty() && !componentStack.back().hasOpenedBody) {
                    componentStack.back().parenDepth++;
                }
            } else if (cChar == L')') {
                if (!componentStack.empty() && !componentStack.back().hasOpenedBody) {
                    if (componentStack.back().parenDepth > 0) componentStack.back().parenDepth--;
                }
            } else if (cChar == L'{') {
                std::wstring prefix = cleanCode.substr(0, i);
                int type = findControlKeyword(prefix);
                controlStack.push_back(type);
                if (type == 1) conditionDepth++;
                else if (type == 2) loopDepth++;
                if (!componentStack.empty()) {
                    auto& comp = componentStack.back();
                    if (!comp.hasOpenedBody) {
                        if (comp.parenDepth == 0) {
                            comp.hasOpenedBody = true;
                            comp.braceDepth = 1;
                        }
                    } else {
                        comp.braceDepth++;
                    }
                }
            } else if (cChar == L'}') {
                if (!controlStack.empty()) {
                    int topType = controlStack.back();
                    controlStack.pop_back();
                    if (topType == 1 && conditionDepth > 0) conditionDepth--;
                    else if (topType == 2 && loopDepth > 0) loopDepth--;
                }
                if (!componentStack.empty()) {
                    auto& comp = componentStack.back();
                    if (comp.hasOpenedBody) {
                        comp.braceDepth--;
                        if (comp.braceDepth <= 0) {
                            auto finishedComp = comp;
                            componentStack.pop_back();
                            if (!finishedComp.hasReturn || !finishedComp.hasReturnedValue) {
                                lsp::Diagnostic diag;
                                diag.line = l;
                                diag.col = i;
                                diag.length = 1;
                                diag.message = L"Component '" + finishedComp.name + L"' must return a valid React element, JSX, or null. (react/require-render-return)";
                                diag.severity = 1; // Error
                                diagnostics.push_back(std::move(diag));
                            }
                        }
                    }
                }
            }
        }

        // Return Statement Analysis: check for missing return expression in components & missing semicolons
        size_t retPos = 0;
        while ((retPos = cleanCode.find(L"return", retPos)) != std::wstring::npos) {
            bool validBefore = (retPos == 0 || (!iswalnum(cleanCode[retPos - 1]) && cleanCode[retPos - 1] != L'_' && cleanCode[retPos - 1] != L'$'));
            size_t afterIdx = retPos + 6;
            bool validAfter = (afterIdx >= cleanCode.length() || (!iswalnum(cleanCode[afterIdx]) && cleanCode[afterIdx] != L'_' && cleanCode[afterIdx] != L'$'));

            if (validBefore && validAfter) {
                if (!componentStack.empty()) {
                    componentStack.back().hasReturn = true;
                }

                std::wstring afterReturn = cleanCode.substr(afterIdx);
                size_t s = 0;
                while (s < afterReturn.length() && (afterReturn[s] == L' ' || afterReturn[s] == L'	')) s++;
                afterReturn = afterReturn.substr(s);

                size_t lastNonWs = code.find_last_not_of(L" \t\r\n");
                bool hasSemi = (lastNonWs != std::wstring::npos && code[lastNonWs] == L';');
                if (!afterReturn.empty() && afterReturn.back() == L';') {
                    hasSemi = true;
                    afterReturn.pop_back();
                    while (!afterReturn.empty() && (afterReturn.back() == L' ' || afterReturn.back() == L'	')) afterReturn.pop_back();
                }

                bool hasValOnSameLine = !afterReturn.empty();
                bool multilineContinuation = afterReturn.ends_with(L"(") || afterReturn.ends_with(L"{") || afterReturn.ends_with(L"[");

                bool hasValOnNextLine = false;
                if (!hasValOnSameLine) {
                    for (size_t nl = l + 1; nl < lineCount; ++nl) {
                        const std::wstring& nLine = buffer.GetLine(nl);
                        size_t ns = 0;
                        while (ns < nLine.length() && (nLine[ns] == L' ' || nLine[ns] == L'	')) ns++;
                        if (ns >= nLine.length()) continue;
                        if (nLine.substr(ns, 2) == L"//" || nLine.substr(ns, 2) == L"/*") continue;
                        if (nLine[ns] == L'}' || nLine[ns] == L')') {
                            hasValOnNextLine = false;
                        } else {
                            hasValOnNextLine = true;
                            multilineContinuation = true;
                        }
                        break;
                    }
                }

                if (hasValOnSameLine || hasValOnNextLine) {
                    if (!componentStack.empty()) {
                        componentStack.back().hasReturnedValue = true;
                    }
                } else {
                    if (checkReactComponents && !componentStack.empty()) {
                        lsp::Diagnostic diag;
                        diag.line = l;
                        diag.col = retPos;
                        diag.length = 6;
                        diag.message = L"Component '" + componentStack.back().name + L"' must return a valid React element, JSX, or null. (react/require-render-return)";
                        diag.severity = 1; // Error
                        diagnostics.push_back(std::move(diag));
                    }

                    if (!hasSemi) {
                        lsp::Diagnostic semiDiag;
                        semiDiag.line = l;
                        semiDiag.col = retPos + 6;
                        semiDiag.length = 1;
                        semiDiag.message = L"Missing semicolon. (semi)";
                        semiDiag.severity = 2; // Warning
                        diagnostics.push_back(std::move(semiDiag));
                    }
                }

                if (hasValOnSameLine && !hasSemi && !multilineContinuation) {
                    lsp::Diagnostic semiDiag;
                    semiDiag.line = l;
                    semiDiag.col = cleanCode.length();
                    semiDiag.length = 1;
                    semiDiag.message = L"Missing semicolon. (semi)";
                    semiDiag.severity = 2; // Warning
                    diagnostics.push_back(std::move(semiDiag));
                }
            }
            retPos += 6;
        }

        // 1. React Hook Rules: Cannot call hooks conditionally or inside loops
        for (const auto& hook : reactHooks) {
            size_t pos = 0;
            while ((pos = code.find(hook, pos)) != std::wstring::npos) {
                bool validBefore = (pos == 0 || (!iswalnum(code[pos - 1]) && code[pos - 1] != L'_' && code[pos - 1] != L'$'));
                size_t afterIdx = pos + hook.length();
                bool validAfter = (afterIdx >= code.length() || (!iswalnum(code[afterIdx]) && code[afterIdx] != L'_' && code[afterIdx] != L'$'));

                if (validBefore && validAfter) {
                    bool precededByDot = (pos > 0 && code[pos - 1] == L'.');
                    if (!precededByDot) {
                        // Bare hook call e.g. useEffect(...) -> verify it is imported!
                        if (importedSymbols.find(hook) == importedSymbols.end() &&
                            definedSymbols.find(hook) == definedSymbols.end()) {
                            lsp::Diagnostic diag;
                            diag.line = l;
                            diag.col = pos;
                            diag.length = hook.length();
                            diag.message = L"Cannot find name '" + hook + L"'. Did you mean to import it from 'react'?";
                            diag.severity = 1; // 1 = Error
                            diagnostics.push_back(std::move(diag));
                        }
                    } else if (pos >= 6 && code.substr(pos - 6, 6) == L"React.") {
                        if (importedSymbols.find(L"React") == importedSymbols.end() &&
                            definedSymbols.find(L"React") == definedSymbols.end()) {
                            lsp::Diagnostic diag;
                            diag.line = l;
                            diag.col = pos - 6;
                            diag.length = 5;
                            diag.message = L"Cannot find name 'React'. Did you mean to import it from 'react'?";
                            diag.severity = 1; // Error
                            diagnostics.push_back(std::move(diag));
                        }
                    }

                    if (conditionDepth > 0 || loopDepth > 0) {
                        lsp::Diagnostic diag;
                        diag.line = l;
                        diag.col = pos;
                        diag.length = hook.length();
                        diag.message = L"React Hook \"" + hook + L"\" is called conditionally. React Hooks must be called in the exact same order in every component render. (react-hooks/rules-of-hooks)";
                        diag.severity = 1; // Error
                        diagnostics.push_back(std::move(diag));
                    }

                    if (hook == L"useEffect" || hook == L"useCallback" || hook == L"useMemo") {
                        size_t callParen = code.find(L'(', afterIdx);
                        if (callParen != std::wstring::npos) {
                            if (hook == L"useEffect" || hook == L"useCallback" || hook == L"useMemo") {
                                // Multi-line scanner tracking parenDepth from callParen
                                int parenDepth = 1;
                                int curBraceDepth = 0;
                                bool hasCommaAtTopLevel = false;
                                bool inQ = false;
                                wchar_t qC = 0;

                                size_t scanL = l;
                                size_t scanC = callParen + 1;
                                while (scanL < buffer.GetLineCount() && scanL - l <= 60) {
                                    const std::wstring& sLine = buffer.GetLine(scanL);
                                    for (; scanC < sLine.length(); ++scanC) {
                                        wchar_t sc = sLine[scanC];
                                        if (inQ) {
                                            if (sc == L'\\' && scanC + 1 < sLine.length()) {
                                                scanC++;
                                            } else if (sc == qC) {
                                                inQ = false;
                                            }
                                        } else {
                                            if (sc == L'"' || sc == L'\'' || sc == L'`') {
                                                inQ = true;
                                                qC = sc;
                                            } else if (sc == L'(') {
                                                parenDepth++;
                                            } else if (sc == L')') {
                                                parenDepth--;
                                                if (parenDepth == 0) break;
                                            } else if (sc == L'{') {
                                                curBraceDepth++;
                                            } else if (sc == L'}') {
                                                if (curBraceDepth > 0) curBraceDepth--;
                                            } else if (sc == L',' && parenDepth == 1 && curBraceDepth == 0) {
                                                hasCommaAtTopLevel = true;
                                                break;
                                            }
                                        }
                                    }
                                    if (hasCommaAtTopLevel || parenDepth == 0) break;
                                    scanL++;
                                    scanC = 0;
                                }

                                if (parenDepth == 0 && !hasCommaAtTopLevel) {
                                    lsp::Diagnostic diag;
                                    diag.line = l;
                                    diag.col = pos;
                                    diag.length = hook.length();
                                    diag.message = L"React Hook " + hook + L" has no dependency array. It will re-run on every render. (react-hooks/exhaustive-deps)";
                                    diag.severity = 3; // Information
                                    diagnostics.push_back(std::move(diag));
                                }
                            }
                        }
                    }
                }
                pos += hook.length();
            }
        }

        // 2. Direct State Mutation: this.state.x = ...
        size_t statePos = code.find(L"this.state.");
        if (statePos != std::wstring::npos) {
            size_t eqPos = code.find(L'=', statePos + 11);
            if (eqPos != std::wstring::npos && (eqPos + 1 >= code.length() || code[eqPos + 1] != L'=')) {
                lsp::Diagnostic diag;
                diag.line = l;
                diag.col = statePos;
                diag.length = (eqPos - statePos + 1);
                diag.message = L"Do not mutate state directly. Use setState(). (react/no-direct-mutation-state)";
                diag.severity = 1; // Error
                diagnostics.push_back(std::move(diag));
            }
        }

        // 3. JSX Validation: duplicate props and tag balancing
        if (isJsx) {
            static const std::unordered_set<std::wstring> tsTypeKeywords = {
                L"any", L"string", L"number", L"boolean", L"unknown", L"never",
                L"void", L"null", L"undefined", L"object", L"symbol", L"bigint",
                L"typeof", L"keyof", L"readonly", L"infer", L"unique",
                L"T", L"K", L"V", L"U", L"P", L"R", L"E"
            };

            size_t p = 0;
            while (p < code.length()) {
                // Skip strings ("...", '...', `...`), regex literals, and comments when outside JSX children text
                // (either outside all JSX tags OR inside an active `{ ... }` interpolation expression).
                const bool inJsxExpr = (!jsxStack.empty() && jsxStack.back().exprCurlyDepth > 0);
                if (jsxStack.empty() || inJsxExpr) {
                    if (p < cleanCode.length() && cleanCode[p] == L' ' && code[p] != L' ') {
                        p++;
                        continue;
                    }
                    if (!jsxStack.empty() && code[p] == L'{') {
                        jsxStack.back().exprCurlyDepth++;
                        p++;
                        continue;
                    }
                    if (!jsxStack.empty() && code[p] == L'}') {
                        if (jsxStack.back().exprCurlyDepth > 0) jsxStack.back().exprCurlyDepth--;
                        p++;
                        continue;
                    }
                    if (code[p] == L'/') {
                        if (IsJsRegexStart(code, p)) {
                            size_t r = ScanJsRegexEnd(code, p);
                            if (r != std::wstring::npos) {
                                p = r + 1;
                                continue;
                            }
                        }
                    }
                    if (code[p] == L'"' || code[p] == L'\'' || code[p] == L'`') {
                        wchar_t q = code[p++];
                        while (p < code.length()) {
                            if (code[p] == L'\\' && p + 1 < code.length()) {
                                p += 2;
                            } else if (code[p] == q) {
                                p++;
                                break;
                            } else {
                                p++;
                            }
                        }
                        continue;
                    }
                } else {
                    if (code[p] == L'{') {
                        jsxStack.back().exprCurlyDepth = 1;
                        p++;
                        continue;
                    }
                }

                if (code[p] == L'<') {
                    // Double check: if outside JSX prose (or inside JSX expression) and cleanCode masked this position (quote/regex/comment), skip!
                    if ((jsxStack.empty() || (!jsxStack.empty() && jsxStack.back().exprCurlyDepth > 0)) &&
                        p < cleanCode.length() && cleanCode[p] == L' ' && code[p] != L' ') {
                        p++;
                        continue;
                    }
                    // In JS/TSX, an opening JSX tag outside JSX children cannot immediately follow an identifier or dollar sign without whitespace.
                    // e.g. `useRef<...>`, `ReturnType<...>`, `LayoutProps<...>`, `KeyboardEvent<...>` are generics!
                    // But inside JSX children, text can precede a tag (e.g. `<span>Cleave<span className=...>DB</span></span>`).
                    if (jsxStack.empty() && p + 1 < code.length() && code[p + 1] != L'/' && p > 0 && (iswalnum(code[p - 1]) || code[p - 1] == L'_' || code[p - 1] == L'$')) {
                        p++;
                        continue;
                    }

                    if (p + 1 < code.length() && (code[p + 1] == L'/' || code[p + 1] == L'>' || iswalpha(code[p + 1]))) {
                        // Safely find the matching '>' across single-line or multi-line JSX tags
                        // without stopping on '>' inside string attributes or expressions
                        size_t tagEndLine = l;
                        size_t tagEndCol = std::wstring::npos;
                        bool abortTag = false;
                        bool inQuote = false;
                        wchar_t qChar = 0;
                        int braceDepth = 0;
                        std::wstring accumulatedTag = L"";

                        size_t scanLine = l;
                        size_t scanCol = p + 1;
                        const std::wstring* currLineStr = &code;

                        while (scanLine < buffer.GetLineCount()) {
                            for (size_t scan = scanCol; scan < currLineStr->length(); ++scan) {
                                wchar_t sc = (*currLineStr)[scan];
                                if (inQuote) {
                                    if (sc == L'\\' && scan + 1 < currLineStr->length()) {
                                        accumulatedTag += sc;
                                        scan++;
                                        if (scan < currLineStr->length()) accumulatedTag += (*currLineStr)[scan];
                                    } else if (sc == qChar) {
                                        inQuote = false;
                                        accumulatedTag += sc;
                                    } else {
                                        accumulatedTag += sc;
                                    }
                                } else {
                                    if (sc == L'/' && braceDepth > 0 && IsJsRegexStart(*currLineStr, scan)) {
                                        size_t r = ScanJsRegexEnd(*currLineStr, scan);
                                        if (r != std::wstring::npos) {
                                            for (size_t k = scan; k <= r; ++k) accumulatedTag += (*currLineStr)[k];
                                            scan = r;
                                            continue;
                                        }
                                    }
                                    if (sc == L'"' || sc == L'\'' || sc == L'`') {
                                        inQuote = true;
                                        qChar = sc;
                                        accumulatedTag += sc;
                                    } else if (sc == L'{') {
                                        braceDepth++;
                                        accumulatedTag += sc;
                                    } else if (sc == L'}') {
                                        if (braceDepth > 0) braceDepth--;
                                        accumulatedTag += sc;
                                    } else if (sc == L'>' && braceDepth == 0) {
                                        if (scan > 0 && (*currLineStr)[scan - 1] == L'=') {
                                            accumulatedTag += sc; // Arrow function '=>' inside attribute, not closing tag!
                                            continue;
                                        }
                                        tagEndLine = scanLine;
                                        tagEndCol = scan;
                                        break;
                                    } else if (sc == L'<' && braceDepth == 0) {
                                        // A new '<' outside of expression brackets means current tag was unclosed or broken!
                                        // Immediately abort scanning this tag so subsequent tags across lines are not swallowed
                                        abortTag = true;
                                        break;
                                    } else {
                                        accumulatedTag += sc;
                                    }
                                }
                            }

                            if (tagEndCol != std::wstring::npos || abortTag) {
                                break;
                            }

                            scanLine++;
                            if (scanLine < buffer.GetLineCount() && scanLine - l <= 25) {
                                currLineStr = &buffer.GetLine(scanLine);
                                scanCol = 0;
                                accumulatedTag += L" ";
                            } else {
                                break;
                            }
                        }

                        if (tagEndCol != std::wstring::npos) {
                            std::wstring tagStr = accumulatedTag;
                            size_t tagEnd = tagEndCol;
                            // Trim trailing whitespace from tagStr
                            while (!tagStr.empty() && iswspace(tagStr.back())) tagStr.pop_back();

                            bool isClose = (!tagStr.empty() && tagStr[0] == L'/');
                            bool isSelfClose = (!tagStr.empty() && tagStr.back() == L'/');

                            // A generic type parameter in a call cannot have JSX attributes (e.g. key=, className=, onClick=)
                            bool hasJsxAttrs = (tagStr.find(L'=') != std::wstring::npos);
                            // Inside JSX children prose (!jsxStack.empty() && exprCurlyDepth == 0), <tag> is always JSX markup (e.g. <code>.exe</code>), never a TS generic type parameter
                            bool canBeTsGeneric = (jsxStack.empty() || jsxStack.back().exprCurlyDepth > 0);

                            auto advanceToTagEndLine = [&]() {
                                if (tagEndLine > l) {
                                    for (size_t adv = l + 1; adv <= tagEndLine; ++adv) {
                                        sanitizeLine(buffer.GetLine(adv), code, cleanCode);
                                    }
                                    l = tagEndLine;
                                }
                            };

                            // If tagStr contains type operators or generic syntax (; | & , { [ typeof keyof extends as) and has NO attributes, this is a TS generic type parameter
                            if (canBeTsGeneric && !isClose && !hasJsxAttrs && (tagStr.find(L';') != std::wstring::npos ||
                                                             tagStr.find(L'|') != std::wstring::npos ||
                                                             tagStr.find(L'&') != std::wstring::npos ||
                                                             tagStr.find(L',') != std::wstring::npos ||
                                                             tagStr.find(L'{') != std::wstring::npos ||
                                                             tagStr.find(L'[') != std::wstring::npos ||
                                                             tagStr.find(L"typeof ") != std::wstring::npos ||
                                                             tagStr.find(L"keyof ") != std::wstring::npos ||
                                                             tagStr.find(L" extends ") != std::wstring::npos ||
                                                             tagStr.find(L" as ") != std::wstring::npos)) {
                                advanceToTagEndLine();
                                p = tagEnd + 1;
                                continue;
                            }

                            size_t nStart = isClose ? 1 : 0;
                            while (nStart < tagStr.length() && iswspace(tagStr[nStart])) nStart++;
                            size_t nEnd = nStart;
                            while (nEnd < tagStr.length() && !iswspace(tagStr[nEnd]) && tagStr[nEnd] != L'/') nEnd++;
                            std::wstring tagName = tagStr.substr(nStart, nEnd - nStart);
                            if (tagName.empty() || tagName == L"/") {
                                tagName = L"<>";
                            }

                            // Skip TypeScript primitive types (e.g. useRef<any>(null), useState<number>)
                            if (canBeTsGeneric && !isClose && !hasJsxAttrs && tsTypeKeywords.count(tagName) > 0) {
                                advanceToTagEndLine();
                                p = tagEnd + 1;
                                continue;
                            }

                            // Check what follows '>'. If followed by '(', ')', ';', ',', '=', '.', or ':' outside JSX prose, it's a generic call or type annotation
                            if (canBeTsGeneric && !isClose && !hasJsxAttrs) {
                                size_t nextCharIdx = tagEnd + 1;
                                const std::wstring& targetLine = (tagEndLine > l) ? buffer.GetLine(tagEndLine) : code;
                                while (nextCharIdx < targetLine.length() && iswspace(targetLine[nextCharIdx])) nextCharIdx++;
                                if (nextCharIdx < targetLine.length() && (targetLine[nextCharIdx] == L'(' || targetLine[nextCharIdx] == L')' ||
                                                                    targetLine[nextCharIdx] == L';' || targetLine[nextCharIdx] == L',' ||
                                                                    targetLine[nextCharIdx] == L'=' || targetLine[nextCharIdx] == L'.' ||
                                                                    targetLine[nextCharIdx] == L':')) {
                                    advanceToTagEndLine();
                                    p = tagEnd + 1;
                                    continue;
                                }
                            }

                            if (!isClose && !isSelfClose && voidTags.count(tagName) == 0 && !tagName.empty()) {
                                jsxStack.push_back({ tagName, l, p, 0 });

                                std::unordered_set<std::wstring> propNames;
                                size_t aIdx = nEnd;
                                while (aIdx < tagStr.length()) {
                                    while (aIdx < tagStr.length() && iswspace(tagStr[aIdx])) aIdx++;
                                    if (aIdx >= tagStr.length() || tagStr[aIdx] == L'/' || tagStr[aIdx] == L'>') break;

                                    // Skip spread attributes: {...props}
                                    if (tagStr[aIdx] == L'{') {
                                        int depth = 1;
                                        aIdx++;
                                        bool inQ = false;
                                        wchar_t qC = 0;
                                        while (aIdx < tagStr.length() && depth > 0) {
                                            wchar_t ch = tagStr[aIdx];
                                            if (inQ) {
                                                if (ch == L'\\' && aIdx + 1 < tagStr.length()) aIdx++;
                                                else if (ch == qC) inQ = false;
                                            } else {
                                                if (ch == L'"' || ch == L'\'' || ch == L'`') { inQ = true; qC = ch; }
                                                else if (ch == L'{') depth++;
                                                else if (ch == L'}') depth--;
                                            }
                                            aIdx++;
                                        }
                                        continue;
                                    }

                                    if (!iswalpha(tagStr[aIdx]) && tagStr[aIdx] != L'_' && tagStr[aIdx] != L'$') {
                                        aIdx++;
                                        continue;
                                    }

                                    size_t nameStart = aIdx;
                                    while (aIdx < tagStr.length() && (iswalnum(tagStr[aIdx]) || tagStr[aIdx] == L'-' || tagStr[aIdx] == L':' || tagStr[aIdx] == L'_' || tagStr[aIdx] == L'$')) {
                                        aIdx++;
                                    }
                                    std::wstring propName = tagStr.substr(nameStart, aIdx - nameStart);

                                    while (aIdx < tagStr.length() && iswspace(tagStr[aIdx])) aIdx++;

                                    if (aIdx < tagStr.length() && tagStr[aIdx] == L'=') {
                                        aIdx++;
                                        while (aIdx < tagStr.length() && iswspace(tagStr[aIdx])) aIdx++;
                                        if (aIdx < tagStr.length()) {
                                            if (tagStr[aIdx] == L'"' || tagStr[aIdx] == L'\'' || tagStr[aIdx] == L'`') {
                                                wchar_t q = tagStr[aIdx++];
                                                while (aIdx < tagStr.length()) {
                                                    if (tagStr[aIdx] == L'\\' && aIdx + 1 < tagStr.length()) aIdx += 2;
                                                    else if (tagStr[aIdx] == q) { aIdx++; break; }
                                                    else aIdx++;
                                                }
                                            } else if (tagStr[aIdx] == L'{') {
                                                int depth = 1;
                                                aIdx++;
                                                bool inQ = false;
                                                wchar_t qC = 0;
                                                while (aIdx < tagStr.length() && depth > 0) {
                                                    wchar_t ch = tagStr[aIdx];
                                                    if (inQ) {
                                                        if (ch == L'\\' && aIdx + 1 < tagStr.length()) aIdx++;
                                                        else if (ch == qC) inQ = false;
                                                    } else {
                                                        if (ch == L'"' || ch == L'\'' || ch == L'`') { inQ = true; qC = ch; }
                                                        else if (ch == L'{') depth++;
                                                        else if (ch == L'}') depth--;
                                                    }
                                                    aIdx++;
                                                }
                                            }
                                        }
                                    }

                                    if (!propName.empty()) {
                                        if (propNames.count(propName) > 0) {
                                            lsp::Diagnostic diag;
                                            diag.line = l;
                                            diag.col = p;
                                            diag.length = tagEnd - p + 1;
                                            diag.message = L"No duplicate props allowed: \"" + propName + L"\" (react/jsx-no-duplicate-props)";
                                            diag.severity = 2; // Warning
                                            diagnostics.push_back(std::move(diag));
                                        } else {
                                            propNames.insert(propName);
                                        }
                                    }
                                }
                            } else if (isClose && !tagName.empty()) {
                                if (!jsxStack.empty()) {
                                    if (jsxStack.back().name == tagName || _wcsicmp(jsxStack.back().name.c_str(), tagName.c_str()) == 0) {
                                        jsxStack.pop_back();
                                    } else {
                                        // Look backward down jsxStack for a matching ancestor
                                        auto matchIt = jsxStack.end();
                                        for (auto it = jsxStack.rbegin(); it != jsxStack.rend(); ++it) {
                                            if (it->name == tagName || _wcsicmp(it->name.c_str(), tagName.c_str()) == 0) {
                                                matchIt = (it + 1).base(); // iterator pointing to matching item
                                                break;
                                            }
                                        }

                                        if (matchIt != jsxStack.end()) {
                                            // Every item above matchIt was never closed! Report them at their opening tag
                                            for (auto it = matchIt + 1; it != jsxStack.end(); ++it) {
                                                lsp::Diagnostic diag;
                                                diag.line = it->line;
                                                diag.col = it->col;
                                                diag.length = it->name.length() + 1;
                                                std::wstring disp = (it->name == L"<>" ? L"<>" : (L"<" + it->name + L">"));
                                                diag.message = L"JSX: Unclosed JSX element '" + disp + L"'";
                                                diag.severity = 1; // Error
                                                diagnostics.push_back(std::move(diag));
                                            }
                                            // Pop all items from matchIt through the end
                                            jsxStack.erase(matchIt, jsxStack.end());
                                        } else {
                                            // No matching opening tag exists anywhere in the stack; report unexpected closing tag without corrupting stack
                                            lsp::Diagnostic diag;
                                            diag.line = l;
                                            diag.col = p;
                                            diag.length = tagEnd - p + 1;
                                            std::wstring disp = (tagName == L"<>" ? L"</>" : (L"</" + tagName + L">"));
                                            diag.message = L"JSX: Unexpected closing tag '" + disp + L"', no matching opening tag found";
                                            diag.severity = 1; // Error
                                            diagnostics.push_back(std::move(diag));
                                        }
                                    }
                                } else {
                                    lsp::Diagnostic diag;
                                    diag.line = l;
                                    diag.col = p;
                                    diag.length = tagEnd - p + 1;
                                    std::wstring disp = (tagName == L"<>" ? L"</>" : (L"</" + tagName + L">"));
                                    diag.message = L"JSX: Unexpected closing tag '" + disp + L"', no matching opening tag found";
                                    diag.severity = 1; // Error
                                    diagnostics.push_back(std::move(diag));
                                }
                            }
                            advanceToTagEndLine();
                            p = tagEnd + 1;
                            continue;
                        }
                    }
                }
                p++;
            }
        }
    }

    for (const auto& tag : jsxStack) {
        lsp::Diagnostic diag;
        diag.line = tag.line;
        diag.col = tag.col;
        diag.length = tag.name.length() + 1;
        std::wstring disp = (tag.name == L"<>" ? L"<>" : (L"<" + tag.name + L">"));
        diag.message = L"JSX: Unclosed JSX element '" + disp + L"'";
        diag.severity = 1; // Error
        diagnostics.push_back(std::move(diag));
    }

    return diagnostics;
}

} // namespace phide::editor
