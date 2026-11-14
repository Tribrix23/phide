/**
 * @file DevWorkflowEngines.cpp
 * @brief Implementations for ProblemMatcherEngine (#30), TaskRunnerEngine (#29), and TestExplorerEngine (#28).
 */

#include "phide/dev/DevWorkflowEngines.hpp"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

namespace phide::dev {

namespace fs = std::filesystem;

namespace {

std::wstring ToLowerW(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}

std::wstring TrimW(std::wstring s) {
    while (!s.empty() && std::iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && std::iswspace(s.back())) s.pop_back();
    return s;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (n <= 0) return L"";
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
        return out;
    }
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

bool ReadFileWideBounded(const fs::path& p, std::wstring& out, size_t maxBytes = 512 * 1024) {
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return false;
    auto sz = fs::file_size(p, ec);
    if (ec || sz > maxBytes) return false;
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::string raw(static_cast<size_t>(sz), '\0');
    if (sz > 0) ifs.read(raw.data(), static_cast<std::streamsize>(sz));
    out = Utf8ToWide(raw);
    return true;
}

std::vector<std::wstring> SplitLinesW(const std::wstring& s) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find(L'\n', start);
        if (nl == std::wstring::npos) {
            std::wstring tail = s.substr(start);
            if (!tail.empty() && tail.back() == L'\r') tail.pop_back();
            lines.push_back(std::move(tail));
            break;
        }
        std::wstring line = s.substr(start, nl - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        start = nl + 1;
        if (start == s.size()) break;
    }
    return lines;
}

std::wstring ReplaceAllW(std::wstring text, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::wstring StripJsonComments(const std::wstring& input) {
    std::wstring out;
    out.reserve(input.size());
    bool inStr = false;
    bool escaped = false;
    for (size_t i = 0; i < input.size(); ++i) {
        wchar_t c = input[i];
        if (inStr) {
            out.push_back(c);
            if (escaped) escaped = false;
            else if (c == L'\\') escaped = true;
            else if (c == L'"') inStr = false;
            continue;
        }
        if (c == L'"') {
            inStr = true;
            out.push_back(c);
            continue;
        }
        if (c == L'/' && i + 1 < input.size()) {
            if (input[i + 1] == L'/') {
                while (i + 1 < input.size() && input[i + 1] != L'\n') ++i;
                continue;
            }
            if (input[i + 1] == L'*') {
                i += 2;
                while (i + 1 < input.size() && !(input[i] == L'*' && input[i + 1] == L'/')) ++i;
                if (i + 1 < input.size()) ++i;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

bool ExtractJsonStringField(const std::wstring& obj, const std::wstring& key, std::wstring& outVal) {
    std::wstring needle = L"\"" + key + L"\"";
    size_t pos = obj.find(needle);
    if (pos == std::wstring::npos) return false;
    size_t colon = obj.find(L':', pos + needle.size());
    if (colon == std::wstring::npos) return false;
    size_t i = colon + 1;
    while (i < obj.size() && std::iswspace(obj[i])) ++i;
    if (i >= obj.size() || obj[i] != L'"') return false;
    ++i;
    std::wstring res;
    bool esc = false;
    for (; i < obj.size(); ++i) {
        wchar_t c = obj[i];
        if (esc) {
            if (c == L'n') res.push_back(L'\n');
            else if (c == L't') res.push_back(L'\t');
            else res.push_back(c);
            esc = false;
        } else if (c == L'\\') {
            esc = true;
        } else if (c == L'"') {
            outVal = std::move(res);
            return true;
        } else {
            res.push_back(c);
        }
    }
    return false;
}

std::vector<std::wstring> ExtractJsonStringArrayField(const std::wstring& obj, const std::wstring& key) {
    std::vector<std::wstring> out;
    std::wstring needle = L"\"" + key + L"\"";
    size_t pos = obj.find(needle);
    if (pos == std::wstring::npos) return out;
    size_t colon = obj.find(L':', pos + needle.size());
    if (colon == std::wstring::npos) return out;
    size_t i = colon + 1;
    while (i < obj.size() && std::iswspace(obj[i])) ++i;
    if (i >= obj.size()) return out;
    if (obj[i] == L'"') {
        std::wstring single;
        if (ExtractJsonStringField(obj.substr(pos), key, single) && !single.empty()) out.push_back(single);
        return out;
    }
    if (obj[i] != L'[') return out;
    ++i;
    while (i < obj.size() && obj[i] != L']') {
        while (i < obj.size() && (std::iswspace(obj[i]) || obj[i] == L',')) ++i;
        if (i < obj.size() && obj[i] == L'"') {
            ++i;
            std::wstring elem;
            bool esc = false;
            for (; i < obj.size(); ++i) {
                wchar_t c = obj[i];
                if (esc) { elem.push_back(c); esc = false; }
                else if (c == L'\\') esc = true;
                else if (c == L'"') { ++i; break; }
                else elem.push_back(c);
            }
            if (!elem.empty()) out.push_back(std::move(elem));
        } else if (i < obj.size() && obj[i] != L']') {
            ++i;
        }
    }
    return out;
}

bool LooksLikePathPrefix(const std::wstring& token) {
    if (token.empty() || token.size() > 260) return false;
    if (token.rfind(L"http://", 0) == 0 || token.rfind(L"https://", 0) == 0) return false;
    if (token.find(L'.') != std::wstring::npos) return true;
    if (token.find(L'\\') != std::wstring::npos || token.find(L'/') != std::wstring::npos) return true;
    return false;
}

} // namespace

// =========================================================================
// ProblemMatcherEngine (#30)
// =========================================================================

std::wstring ProblemMatcherEngine::StripAnsi(const std::wstring& input) {
    std::wstring out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == L'\x1B' && i + 1 < input.size() && input[i + 1] == L'[') {
            i += 2;
            while (i < input.size() && !(input[i] >= L'@' && input[i] <= L'~')) ++i;
            continue;
        }
        out.push_back(input[i]);
    }
    return out;
}

std::wstring ProblemMatcherEngine::ResolvePath(const std::wstring& fileToken, const std::wstring& workspaceRoot) {
    std::wstring cleaned = TrimW(fileToken);
    if (cleaned.size() >= 2 && cleaned.front() == L'"' && cleaned.back() == L'"') {
        cleaned = cleaned.substr(1, cleaned.size() - 2);
    }
    if (cleaned.empty()) return cleaned;
    std::error_code ec;
    fs::path p(cleaned);
    if (p.is_absolute()) {
        return fs::weakly_canonical(p, ec).wstring();
    }
    if (!workspaceRoot.empty()) {
        fs::path joined = fs::path(workspaceRoot) / p;
        if (fs::exists(joined, ec)) {
            return fs::weakly_canonical(joined, ec).wstring();
        }
        return joined.lexically_normal().wstring();
    }
    return cleaned;
}

bool ProblemMatcherEngine::ParseLine(const std::wstring& rawLine,
                                     const std::wstring& workspaceRoot,
                                     ProblemMatch& outMatch) {
    ProblemMatcherState dummy;
    return ParseLine(rawLine, workspaceRoot, dummy, outMatch);
}

bool ProblemMatcherEngine::ParseLine(const std::wstring& rawLine,
                                     const std::wstring& workspaceRoot,
                                     ProblemMatcherState& state,
                                     ProblemMatch& outMatch) {
    outMatch = {};
    std::wstring line = StripAnsi(rawLine);
    while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
    if (line.empty()) return false;

    std::wstring trimmed = TrimW(line);

    // Track PHPStan section headers: " ------ <file.php>"
    if (trimmed.rfind(L"------", 0) == 0 && trimmed.size() > 7) {
        std::wstring rest = TrimW(trimmed.substr(6));
        if (!rest.empty() && rest.find(L".php") != std::wstring::npos && rest.find(L"Line") == std::wstring::npos) {
            state.activePhpstanFile = ResolvePath(rest, workspaceRoot);
            return false;
        }
    }

    // Track ESLint stylish standalone file line (e.g., C:\src\app.ts or src/app.js)
    if (trimmed.find(L' ') == std::wstring::npos &&
        (trimmed.ends_with(L".ts") || trimmed.ends_with(L".tsx") || trimmed.ends_with(L".js") ||
         trimmed.ends_with(L".jsx") || trimmed.ends_with(L".mjs"))) {
        state.activeEslintFile = ResolvePath(trimmed, workspaceRoot);
        return false;
    }

    // 1. MSVC & tsc: path(line[,col]): (fatal error|error|warning|note) CODE: msg
    {
        static const std::wregex kMsvcTscRe(
            LR"(^\s*([^\r\n\(\)]+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning|note)\s+([A-Za-z]+\d+)\s*:\s*(.+)$)",
            std::regex_constants::icase);
        std::wsmatch m;
        if (std::regex_match(line, m, kMsvcTscRe)) {
            std::wstring f = TrimW(m[1].str());
            if (LooksLikePathPrefix(f)) {
                outMatch.filePath = ResolvePath(f, workspaceRoot);
                try { outMatch.line1Based = (std::max)(1, std::stoi(m[2].str())); } catch (...) {}
                try { if (m[3].matched) outMatch.col1Based = (std::max)(1, std::stoi(m[3].str())); } catch (...) {}
                std::wstring sev = ToLowerW(m[4].str());
                outMatch.severity = (sev.find(L"warn") != std::wstring::npos) ? L"warning"
                                  : (sev.find(L"note") != std::wstring::npos ? L"info" : L"error");
                outMatch.code = m[5].str();
                outMatch.message = TrimW(m[6].str());
                outMatch.matcherName = (ToLowerW(outMatch.code).rfind(L"ts", 0) == 0) ? L"tsc" : L"msvc";
                outMatch.linkStart = static_cast<size_t>(m.position(1));
                size_t closeParen = line.find(L')', static_cast<size_t>(m.position(2)));
                outMatch.linkLength = (closeParen != std::wstring::npos && closeParen >= outMatch.linkStart)
                    ? (closeParen - outMatch.linkStart + 1) : f.size();
                return true;
            }
        }
    }

    // 2. tsc pretty format: path:line:col - error TSxxxx: msg
    {
        static const std::wregex kTscPrettyRe(
            LR"(^\s*(.+?):(\d+):(\d+)\s*-\s*(error|warning)\s+(TS\d+)\s*:\s*(.+)$)",
            std::regex_constants::icase);
        std::wsmatch m;
        if (std::regex_match(line, m, kTscPrettyRe)) {
            std::wstring f = TrimW(m[1].str());
            if (LooksLikePathPrefix(f)) {
                outMatch.filePath = ResolvePath(f, workspaceRoot);
                try { outMatch.line1Based = (std::max)(1, std::stoi(m[2].str())); } catch (...) {}
                try { outMatch.col1Based = (std::max)(1, std::stoi(m[3].str())); } catch (...) {}
                outMatch.severity = ToLowerW(m[4].str());
                outMatch.code = m[5].str();
                outMatch.message = TrimW(m[6].str());
                outMatch.matcherName = L"tsc";
                outMatch.linkStart = static_cast<size_t>(m.position(1));
                outMatch.linkLength = static_cast<size_t>(m.position(3) + m.length(3)) - outMatch.linkStart;
                return true;
            }
        }
    }

    // 3. GCC / Clang: path:line:col: error/warning/note: msg (handles C:\foo.cpp:12:4: error: ...)
    {
        static const std::wregex kGccClangRe(
            LR"(^\s*([A-Za-z]:[^:\r\n]+|[^:\r\n]+):(\d+)(?::(\d+))?:\s*(fatal error|error|warning|note)\s*:\s*(.+)$)",
            std::regex_constants::icase);
        std::wsmatch m;
        if (std::regex_match(line, m, kGccClangRe)) {
            std::wstring f = TrimW(m[1].str());
            if (LooksLikePathPrefix(f)) {
                outMatch.filePath = ResolvePath(f, workspaceRoot);
                try { outMatch.line1Based = (std::max)(1, std::stoi(m[2].str())); } catch (...) {}
                try { if (m[3].matched) outMatch.col1Based = (std::max)(1, std::stoi(m[3].str())); } catch (...) {}
                std::wstring sev = ToLowerW(m[4].str());
                outMatch.severity = (sev == L"warning") ? L"warning" : (sev == L"note" ? L"info" : L"error");
                outMatch.message = TrimW(m[5].str());
                outMatch.matcherName = L"gcc";
                outMatch.linkStart = static_cast<size_t>(m.position(1));
                size_t endGroup = m[3].matched ? static_cast<size_t>(m.position(3) + m.length(3))
                                               : static_cast<size_t>(m.position(2) + m.length(2));
                outMatch.linkLength = endGroup - outMatch.linkStart;
                return true;
            }
        }
    }

    // 4. ESLint stylish line: "  14:7   error   'foo' is not defined   no-undef"
    if (!state.activeEslintFile.empty()) {
        static const std::wregex kEslintRowRe(
            LR"(^\s*(\d+):(\d+)\s+(error|warning)\s+(.+?)\s{2,}([A-Za-z0-9_\-\/@]+)\s*$)",
            std::regex_constants::icase);
        std::wsmatch m;
        if (std::regex_match(line, m, kEslintRowRe)) {
            outMatch.filePath = state.activeEslintFile;
            try { outMatch.line1Based = (std::max)(1, std::stoi(m[1].str())); } catch (...) {}
            try { outMatch.col1Based = (std::max)(1, std::stoi(m[2].str())); } catch (...) {}
            outMatch.severity = ToLowerW(m[3].str());
            outMatch.message = TrimW(m[4].str());
            outMatch.code = m[5].str();
            outMatch.matcherName = L"eslint";
            outMatch.linkStart = static_cast<size_t>(m.position(1));
            outMatch.linkLength = static_cast<size_t>(m.position(2) + m.length(2)) - outMatch.linkStart;
            return true;
        }
    }

    // 5. ESLint unix / PHPStan compact: "file.php:27:Call to an undefined method ..."
    {
        static const std::wregex kFileLineMsgRe(
            LR"(^\s*([A-Za-z]:[^:\r\n]+|[^:\r\n]+\.(?:php|ts|tsx|js|jsx|py|cpp|c|h|hpp)):(\d+):(.+)$)",
            std::regex_constants::icase);
        std::wsmatch m;
        if (std::regex_match(line, m, kFileLineMsgRe)) {
            std::wstring f = TrimW(m[1].str());
            std::wstring msg = TrimW(m[3].str());
            if (!msg.empty()) {
                outMatch.filePath = ResolvePath(f, workspaceRoot);
                try { outMatch.line1Based = (std::max)(1, std::stoi(m[2].str())); } catch (...) {}
                std::wstring ext = ToLowerW(fs::path(f).extension().wstring());
                outMatch.matcherName = (ext == L".php") ? L"phpstan" : L"compiler";
                outMatch.severity = (ToLowerW(msg).find(L"warning") != std::wstring::npos) ? L"warning" : L"error";
                outMatch.message = msg;
                outMatch.linkStart = static_cast<size_t>(m.position(1));
                outMatch.linkLength = static_cast<size_t>(m.position(2) + m.length(2)) - outMatch.linkStart;
                return true;
            }
        }
    }

    // 6. PHPStan table row: "  42     Parameter $x of function foo() expects int, string given."
    if (!state.activePhpstanFile.empty()) {
        static const std::wregex kPhpstanRowRe(LR"(^\s*(\d+)\s{2,}(.+)$)");
        std::wsmatch m;
        if (std::regex_match(line, m, kPhpstanRowRe)) {
            std::wstring msg = TrimW(m[2].str());
            if (!msg.empty() && msg.find(L"------") == std::wstring::npos) {
                outMatch.filePath = state.activePhpstanFile;
                try { outMatch.line1Based = (std::max)(1, std::stoi(m[1].str())); } catch (...) {}
                outMatch.severity = L"error";
                outMatch.message = msg;
                outMatch.matcherName = L"phpstan";
                outMatch.linkStart = static_cast<size_t>(m.position(1));
                outMatch.linkLength = static_cast<size_t>(m.length(1));
                return true;
            }
        }
    }

    return false;
}

// =========================================================================
// TaskRunnerEngine (#29)
// =========================================================================

std::vector<WorkspaceTaskDef> TaskRunnerEngine::ParseVscodeTasksJson(
    const std::wstring& jsonContent,
    const std::wstring& workspaceRoot) {
    std::vector<WorkspaceTaskDef> out;
    std::wstring clean = StripJsonComments(jsonContent);
    size_t tasksPos = clean.find(L"\"tasks\"");
    if (tasksPos == std::wstring::npos) return out;
    size_t arrStart = clean.find(L'[', tasksPos);
    if (arrStart == std::wstring::npos) return out;

    size_t i = arrStart + 1;
    bool inStr = false, esc = false;
    while (i < clean.size()) {
        wchar_t c = clean[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == L'\\') esc = true;
            else if (c == L'"') inStr = false;
            ++i;
            continue;
        }
        if (c == L'"') { inStr = true; ++i; continue; }
        if (c == L']') break;
        if (c == L'{') {
            int depth = 0;
            size_t objStart = i;
            bool subInStr = false, subEsc = false;
            for (; i < clean.size(); ++i) {
                wchar_t sc = clean[i];
                if (subInStr) {
                    if (subEsc) subEsc = false;
                    else if (sc == L'\\') subEsc = true;
                    else if (sc == L'"') subInStr = false;
                } else {
                    if (sc == L'"') subInStr = true;
                    else if (sc == L'{') depth++;
                    else if (sc == L'}') {
                        depth--;
                        if (depth == 0) break;
                    }
                }
            }
            if (depth == 0 && i > objStart) {
                std::wstring obj = clean.substr(objStart, i - objStart + 1);
                WorkspaceTaskDef task;
                task.source = L".vscode/tasks.json";
                task.cwd = workspaceRoot;
                ExtractJsonStringField(obj, L"label", task.label);
                ExtractJsonStringField(obj, L"type", task.type);
                ExtractJsonStringField(obj, L"command", task.command);
                ExtractJsonStringField(obj, L"problemMatcher", task.problemMatcher);
                if (task.problemMatcher.empty()) {
                    auto pms = ExtractJsonStringArrayField(obj, L"problemMatcher");
                    if (!pms.empty()) task.problemMatcher = pms[0];
                }
                task.args = ExtractJsonStringArrayField(obj, L"args");
                task.dependsOn = ExtractJsonStringArrayField(obj, L"dependsOn");
                std::wstring optCwd;
                if (ExtractJsonStringField(obj, L"cwd", optCwd) && !optCwd.empty()) {
                    task.cwd = ReplaceAllW(optCwd, L"${workspaceFolder}", workspaceRoot);
                }
                if (ExtractJsonStringField(obj, L"kind", task.group)) {
                    task.isDefault = (obj.find(L"\"isDefault\": true") != std::wstring::npos ||
                                      obj.find(L"\"isDefault\":true") != std::wstring::npos);
                } else {
                    ExtractJsonStringField(obj, L"group", task.group);
                }
                if (!task.label.empty() || !task.command.empty()) {
                    if (task.label.empty()) task.label = task.command;
                    out.push_back(std::move(task));
                }
            }
        }
        ++i;
    }
    return out;
}

std::vector<WorkspaceTaskDef> TaskRunnerEngine::DiscoverAllTasks(const std::wstring& workspaceRoot) {
    std::vector<WorkspaceTaskDef> tasks;
    if (workspaceRoot.empty()) return tasks;
    std::error_code ec;
    fs::path root(workspaceRoot);
    if (!fs::exists(root, ec)) return tasks;

    // 1. .vscode/tasks.json
    {
        std::wstring txt;
        if (ReadFileWideBounded(root / L".vscode" / L"tasks.json", txt)) {
            auto parsed = ParseVscodeTasksJson(txt, workspaceRoot);
            tasks.insert(tasks.end(), parsed.begin(), parsed.end());
        }
    }

    // 2. package.json scripts
    {
        std::wstring pkgText;
        if (ReadFileWideBounded(root / L"package.json", pkgText)) {
            size_t scriptsPos = pkgText.find(L"\"scripts\"");
            if (scriptsPos != std::wstring::npos) {
                size_t braceStart = pkgText.find(L'{', scriptsPos);
                if (braceStart != std::wstring::npos) {
                    int depth = 0;
                    size_t braceEnd = braceStart;
                    for (; braceEnd < pkgText.size(); ++braceEnd) {
                        if (pkgText[braceEnd] == L'{') depth++;
                        else if (pkgText[braceEnd] == L'}' && --depth == 0) break;
                    }
                    std::wstring block = pkgText.substr(braceStart, braceEnd - braceStart + 1);
                    static const std::wregex kScriptRe(LR"(\"([A-Za-z0-9_:\-\.]+)\"\s*:\s*\"([^\"]+)\")");
                    for (auto it = std::wsregex_iterator(block.begin(), block.end(), kScriptRe);
                         it != std::wsregex_iterator(); ++it) {
                        WorkspaceTaskDef t;
                        t.label = L"npm: " + (*it)[1].str();
                        t.type = L"npm";
                        t.command = L"npm run " + (*it)[1].str();
                        t.cwd = workspaceRoot;
                        t.source = L"package.json";
                        if ((*it)[1].str() == L"build") t.group = L"build";
                        else if ((*it)[1].str() == L"test") t.group = L"test";
                        tasks.push_back(std::move(t));
                    }
                }
            }
        }
    }

    // 3. composer.json scripts
    {
        std::wstring compText;
        if (ReadFileWideBounded(root / L"composer.json", compText)) {
            size_t scriptsPos = compText.find(L"\"scripts\"");
            if (scriptsPos != std::wstring::npos) {
                size_t braceStart = compText.find(L'{', scriptsPos);
                size_t braceEnd = (braceStart != std::wstring::npos) ? compText.find(L'}', braceStart) : std::wstring::npos;
                if (braceStart != std::wstring::npos && braceEnd != std::wstring::npos) {
                    std::wstring block = compText.substr(braceStart, braceEnd - braceStart + 1);
                    static const std::wregex kCompRe(LR"(\"([A-Za-z0-9_:\-\.]+)\"\s*:)");
                    for (auto it = std::wsregex_iterator(block.begin(), block.end(), kCompRe);
                         it != std::wsregex_iterator(); ++it) {
                        WorkspaceTaskDef t;
                        t.label = L"composer: " + (*it)[1].str();
                        t.type = L"composer";
                        t.command = L"composer run-script " + (*it)[1].str();
                        t.cwd = workspaceRoot;
                        t.source = L"composer.json";
                        tasks.push_back(std::move(t));
                    }
                }
            }
        }
    }

    // 4. Makefile targets
    {
        std::wstring mkText;
        if (ReadFileWideBounded(root / L"Makefile", mkText)) {
            static const std::wregex kTargetRe(LR"(^([A-Za-z0-9_\-\.]+)\s*:)");
            for (const auto& ln : SplitLinesW(mkText)) {
                std::wsmatch m;
                if (std::regex_search(ln, m, kTargetRe) && m.size() >= 2) {
                    std::wstring tgt = m[1].str();
                    if (!tgt.empty() && tgt[0] != L'.') {
                        WorkspaceTaskDef t;
                        t.label = L"make: " + tgt;
                        t.type = L"make";
                        t.command = L"make " + tgt;
                        t.cwd = workspaceRoot;
                        t.source = L"Makefile";
                        if (tgt == L"all" || tgt == L"build") t.group = L"build";
                        else if (tgt == L"test") t.group = L"test";
                        tasks.push_back(std::move(t));
                    }
                }
            }
        }
    }

    // 5. CMakeLists.txt targets
    {
        std::wstring cmakeText;
        if (ReadFileWideBounded(root / L"CMakeLists.txt", cmakeText)) {
            static const std::wregex kCmakeRe(LR"(\b(?:add_executable|add_library|add_custom_target)\s*\(\s*([A-Za-z0-9_\-]+))",
                                              std::regex_constants::icase);
            std::set<std::wstring> seen;
            for (auto it = std::wsregex_iterator(cmakeText.begin(), cmakeText.end(), kCmakeRe);
                 it != std::wsregex_iterator(); ++it) {
                std::wstring tgt = (*it)[1].str();
                if (seen.insert(tgt).second) {
                    WorkspaceTaskDef t;
                    t.label = L"cmake: build " + tgt;
                    t.type = L"cmake";
                    t.command = L"cmake --build build --target " + tgt;
                    t.cwd = workspaceRoot;
                    t.group = L"build";
                    t.source = L"CMakeLists.txt";
                    tasks.push_back(std::move(t));
                }
            }
        }
    }

    // 6. scripts\*.bat / *.ps1
    {
        fs::path scriptsDir = root / L"scripts";
        if (fs::exists(scriptsDir, ec) && fs::is_directory(scriptsDir, ec)) {
            for (const auto& entry : fs::directory_iterator(scriptsDir, ec)) {
                if (entry.is_regular_file(ec)) {
                    std::wstring fn = entry.path().filename().wstring();
                    std::wstring ext = ToLowerW(entry.path().extension().wstring());
                    if (ext == L".bat" || ext == L".cmd" || ext == L".ps1") {
                        WorkspaceTaskDef t;
                        t.label = L"script: " + fn;
                        t.type = L"script";
                        t.command = (ext == L".ps1")
                            ? (L"powershell -ExecutionPolicy Bypass -File scripts\\" + fn)
                            : (L"cmd /c scripts\\" + fn);
                        t.cwd = workspaceRoot;
                        t.source = L"scripts/" + fn;
                        tasks.push_back(std::move(t));
                    }
                }
            }
        }
    }

    return tasks;
}

std::vector<WorkspaceTaskDef> TaskRunnerEngine::ResolveExecutionOrder(
    const std::vector<WorkspaceTaskDef>& allTasks,
    const std::wstring& targetLabel) {
    std::unordered_map<std::wstring, const WorkspaceTaskDef*> byLabel;
    for (const auto& t : allTasks) {
        byLabel[t.label] = &t;
    }
    std::vector<WorkspaceTaskDef> order;
    std::set<std::wstring> visiting;
    std::set<std::wstring> visited;

    std::function<void(const std::wstring&)> dfs = [&](const std::wstring& lbl) {
        if (visited.count(lbl) || visiting.count(lbl)) return;
        auto it = byLabel.find(lbl);
        if (it == byLabel.end()) return;
        visiting.insert(lbl);
        for (const auto& dep : it->second->dependsOn) {
            dfs(dep);
        }
        visiting.erase(lbl);
        visited.insert(lbl);
        order.push_back(*it->second);
    };

    dfs(targetLabel);
    return order;
}

std::wstring TaskRunnerEngine::FormatCommandLine(
    const WorkspaceTaskDef& task,
    const std::wstring& workspaceRoot) {
    std::wstring cmd = ReplaceAllW(task.command, L"${workspaceFolder}", workspaceRoot);
    for (const auto& a : task.args) {
        std::wstring arg = ReplaceAllW(a, L"${workspaceFolder}", workspaceRoot);
        cmd += L" ";
        if (arg.find(L' ') != std::wstring::npos && arg.front() != L'"') {
            cmd += L"\"" + arg + L"\"";
        } else {
            cmd += arg;
        }
    }
    return cmd;
}

// =========================================================================
// TestExplorerEngine (#28)
// =========================================================================

std::wstring TestExplorerEngine::FrameworkDisplayName(TestFramework fw) {
    switch (fw) {
    case TestFramework::Pytest:   return L"Pytest";
    case TestFramework::Unittest: return L"Python unittest";
    case TestFramework::Jest:     return L"Jest";
    case TestFramework::Vitest:   return L"Vitest";
    case TestFramework::PhpUnit:  return L"PHPUnit";
    case TestFramework::CTest:    return L"CTest / CMake";
    default:                      return L"Unknown";
    }
}

std::vector<TestItem> TestExplorerEngine::DiscoverTests(const std::wstring& workspaceRoot, size_t maxFiles) {
    std::vector<TestItem> items;
    if (workspaceRoot.empty()) return items;
    std::error_code ec;
    fs::path root(workspaceRoot);
    if (!fs::exists(root, ec)) return items;

    // Detect JS framework hint from package.json
    TestFramework jsFramework = TestFramework::Jest;
    {
        std::wstring pkg;
        if (ReadFileWideBounded(root / L"package.json", pkg)) {
            std::wstring lp = ToLowerW(pkg);
            if (lp.find(L"\"vitest\"") != std::wstring::npos) jsFramework = TestFramework::Vitest;
        }
    }

    static const std::set<std::wstring> kSkipDirs = {
        L".git", L".phide", L"node_modules", L".venv", L"venv", L"__pycache__",
        L"vendor", L"bin", L"obj", L".vs", L"installer"
    };

    size_t filesScanned = 0;
    auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::path p = it->path();
        if (it->is_directory(ec)) {
            if (kSkipDirs.count(ToLowerW(p.filename().wstring()))) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        if (++filesScanned > maxFiles) break;

        std::wstring fname = ToLowerW(p.filename().wstring());
        std::wstring ext = ToLowerW(p.extension().wstring());
        std::wstring fullPath = p.wstring();

        // 1. Python (Pytest / Unittest)
        if (ext == L".py" && (fname.rfind(L"test_", 0) == 0 || fname.ends_with(L"_test.py") || fname == L"tests.py")) {
            std::wstring content;
            if (!ReadFileWideBounded(p, content)) continue;
            TestFramework pyFw = (content.find(L"unittest.TestCase") != std::wstring::npos)
                ? TestFramework::Unittest : TestFramework::Pytest;
            auto lines = SplitLinesW(content);
            std::wstring currentClass;
            static const std::wregex kPyClassRe(LR"(^\s*class\s+([A-Za-z0-9_]+)\b)");
            static const std::wregex kPyDefRe(LR"(^\s*(?:async\s+)?def\s+(test_[A-Za-z0-9_]+)\s*\()");
            for (size_t li = 0; li < lines.size(); ++li) {
                std::wsmatch m;
                if (std::regex_search(lines[li], m, kPyClassRe)) {
                    currentClass = m[1].str();
                } else if (std::regex_search(lines[li], m, kPyDefRe)) {
                    TestItem item;
                    item.name = m[1].str();
                    item.suiteName = currentClass.empty() ? p.filename().wstring() : currentClass;
                    item.filePath = fullPath;
                    item.line0Based = li;
                    item.framework = pyFw;
                    item.id = fullPath + L"::" + (currentClass.empty() ? L"" : currentClass + L"::") + item.name;
                    items.push_back(std::move(item));
                }
            }
            continue;
        }

        // 2. JS/TS (Jest / Vitest)
        if ((ext == L".js" || ext == L".ts" || ext == L".jsx" || ext == L".tsx") &&
            (fname.find(L".test.") != std::wstring::npos || fname.find(L".spec.") != std::wstring::npos)) {
            std::wstring content;
            if (!ReadFileWideBounded(p, content)) continue;
            auto lines = SplitLinesW(content);
            std::wstring currentSuite = p.filename().wstring();
            static const std::wregex kDescRe(LR"(\bdescribe\s*\(\s*['"`]([^'"`]+)['"`])");
            static const std::wregex kTestRe(LR"(\b(?:test|it)\s*\(\s*['"`]([^'"`]+)['"`])");
            for (size_t li = 0; li < lines.size(); ++li) {
                std::wsmatch m;
                if (std::regex_search(lines[li], m, kDescRe)) {
                    currentSuite = m[1].str();
                } else if (std::regex_search(lines[li], m, kTestRe)) {
                    TestItem item;
                    item.name = m[1].str();
                    item.suiteName = currentSuite;
                    item.filePath = fullPath;
                    item.line0Based = li;
                    item.framework = jsFramework;
                    item.id = fullPath + L"::" + item.name;
                    items.push_back(std::move(item));
                }
            }
            continue;
        }

        // 3. PHP (PHPUnit)
        if (ext == L".php" && (fname.ends_with(L"test.php") || fname.find(L"test_") != std::wstring::npos)) {
            std::wstring content;
            if (!ReadFileWideBounded(p, content)) continue;
            auto lines = SplitLinesW(content);
            static const std::wregex kPhpMethodRe(LR"(function\s+(test[A-Za-z0-9_]+)\s*\()");
            for (size_t li = 0; li < lines.size(); ++li) {
                std::wsmatch m;
                if (std::regex_search(lines[li], m, kPhpMethodRe)) {
                    TestItem item;
                    item.name = m[1].str();
                    item.suiteName = p.stem().wstring();
                    item.filePath = fullPath;
                    item.line0Based = li;
                    item.framework = TestFramework::PhpUnit;
                    item.id = fullPath + L"::" + item.name;
                    items.push_back(std::move(item));
                }
            }
            continue;
        }

        // 4. CTest (CMakeLists.txt or CTestTestfile.cmake)
        if (fname == L"cmakelists.txt" || fname == L"ctesttestfile.cmake") {
            std::wstring content;
            if (!ReadFileWideBounded(p, content)) continue;
            auto lines = SplitLinesW(content);
            static const std::wregex kAddTestNamedRe(LR"(\badd_test\s*\(\s*NAME\s+([^\s\)]+))", std::regex_constants::icase);
            static const std::wregex kAddTestPosRe(LR"(\badd_test\s*\(\s*([A-Za-z0-9_\.\-]+)\s+)", std::regex_constants::icase);
            for (size_t li = 0; li < lines.size(); ++li) {
                std::wsmatch m;
                std::wstring testName;
                if (std::regex_search(lines[li], m, kAddTestNamedRe)) testName = m[1].str();
                else if (std::regex_search(lines[li], m, kAddTestPosRe)) testName = m[1].str();
                if (!testName.empty() && ToLowerW(testName) != L"name") {
                    TestItem item;
                    item.name = testName;
                    item.suiteName = L"CTest";
                    item.filePath = fullPath;
                    item.line0Based = li;
                    item.framework = TestFramework::CTest;
                    item.id = fullPath + L"::" + testName;
                    items.push_back(std::move(item));
                }
            }
        }
    }

    return items;
}

std::wstring TestExplorerEngine::BuildRunCommand(
    const std::wstring& workspaceRoot,
    TestFramework fw,
    const std::wstring& filterName,
    const std::wstring& filePath) {
    std::error_code ec;
    fs::path root(workspaceRoot);
    auto pyExe = [&]() -> std::wstring {
        fs::path venv = root / L".venv" / L"Scripts" / L"python.exe";
        if (fs::exists(venv, ec)) return L"\"" + venv.wstring() + L"\"";
        return L"python";
    };

    switch (fw) {
    case TestFramework::Pytest: {
        std::wstring cmd = pyExe() + L" -m pytest -v";
        if (!filePath.empty()) cmd += L" \"" + filePath + L"\"";
        if (!filterName.empty()) cmd += L" -k \"" + filterName + L"\"";
        return cmd;
    }
    case TestFramework::Unittest: {
        std::wstring cmd;
        if (!filePath.empty()) cmd = pyExe() + L" -m unittest -v \"" + filePath + L"\"";
        else cmd = pyExe() + L" -m unittest discover -v";
        if (!filterName.empty()) cmd += L" -k \"" + filterName + L"\"";
        return cmd;
    }
    case TestFramework::Jest: {
        std::wstring cmd = L"npx jest --verbose";
        if (!filePath.empty()) cmd += L" \"" + filePath + L"\"";
        if (!filterName.empty()) cmd += L" -t \"" + filterName + L"\"";
        return cmd;
    }
    case TestFramework::Vitest: {
        std::wstring cmd = L"npx vitest run";
        if (!filePath.empty()) cmd += L" \"" + filePath + L"\"";
        if (!filterName.empty()) cmd += L" -t \"" + filterName + L"\"";
        return cmd;
    }
    case TestFramework::PhpUnit: {
        fs::path localBin = root / L"vendor" / L"bin" / L"phpunit.bat";
        std::wstring bin = fs::exists(localBin, ec) ? (L"\"" + localBin.wstring() + L"\"") : L"phpunit";
        if (!filePath.empty()) bin += L" \"" + filePath + L"\"";
        if (!filterName.empty()) bin += L" --filter \"" + filterName + L"\"";
        return bin;
    }
    case TestFramework::CTest: {
        std::wstring buildDir = fs::exists(root / L"build" / L"CTestTestfile.cmake", ec) ? L"build" : L".";
        std::wstring cmd = L"ctest --test-dir \"" + buildDir + L"\" --output-on-failure";
        if (!filterName.empty()) cmd += L" -R \"" + filterName + L"\"";
        return cmd;
    }
    default:
        return L"";
    }
}

TestRunSummary TestExplorerEngine::ParseTestOutput(
    TestFramework fw,
    const std::wstring& outputText,
    std::vector<TestItem>& items,
    int exitCode) {
    TestRunSummary summary;
    summary.exitCode = exitCode;
    summary.rawOutput = outputText;
    auto lines = SplitLinesW(outputText);

    auto markMatch = [&](const std::wstring& nameFragment, TestStatus st, double durMs, const std::wstring& msg) {
        if (nameFragment.empty()) return;
        for (auto& it : items) {
            if (it.framework != fw && fw != TestFramework::Unknown) continue;
            if (it.name == nameFragment || nameFragment.find(it.name) != std::wstring::npos ||
                it.id.find(nameFragment) != std::wstring::npos) {
                it.status = st;
                if (durMs > 0.0) it.durationMs = durMs;
                if (!msg.empty()) it.failureMessage = msg;
            }
        }
    };

    for (const auto& ln : lines) {
        // Pytest -v: "path.py::test_fn PASSED [ 50%]" / FAILED / SKIPPED
        {
            static const std::wregex kPytestRe(LR"(^(.+?::([A-Za-z0-9_:\.\[\]]+))\s+(PASSED|FAILED|SKIPPED|ERROR)\b)");
            std::wsmatch m;
            if (std::regex_search(ln, m, kPytestRe)) {
                std::wstring stStr = m[3].str();
                TestStatus st = (stStr == L"PASSED") ? TestStatus::Passed
                              : (stStr == L"SKIPPED" ? TestStatus::Skipped : TestStatus::Failed);
                markMatch(m[2].str(), st, 0.0, st == TestStatus::Failed ? ln : L"");
                continue;
            }
        }
        // Unittest -v: "test_fn (mod.Cls) ... ok" / FAIL / ERROR / skipped
        {
            static const std::wregex kUnitRe(LR"(^([A-Za-z0-9_]+)\s*\([^\)]+\)\s*\.\.\.\s*(ok|FAIL|ERROR|skipped)\b)",
                                             std::regex_constants::icase);
            std::wsmatch m;
            if (std::regex_search(ln, m, kUnitRe)) {
                std::wstring stStr = ToLowerW(m[2].str());
                TestStatus st = (stStr == L"ok") ? TestStatus::Passed
                              : (stStr == L"skipped" ? TestStatus::Skipped : TestStatus::Failed);
                markMatch(m[1].str(), st, 0.0, st == TestStatus::Failed ? ln : L"");
                continue;
            }
        }
        // Jest/Vitest: "✓ test name (12 ms)" or "✕ test name (5 ms)" or ASCII "PASS"/"FAIL"
        {
            static const std::wregex kJestRe(LR"(^\s*(?:\x2713|\u2713|\u221A|PASS|\x2715|\u2715|\u00D7|FAIL)\s+(.+?)(?:\s+\((\d+(?:\.\d+)?)\s*ms\))?\s*$)");
            std::wsmatch m;
            if (std::regex_match(ln, m, kJestRe)) {
                bool isFail = (ln.find(L'\u2715') != std::wstring::npos || ln.find(L'\u00D7') != std::wstring::npos ||
                               ln.find(L"FAIL") != std::wstring::npos);
                double ms = 0.0;
                try { if (m[2].matched) ms = std::stod(m[2].str()); } catch (...) {}
                markMatch(TrimW(m[1].str()), isFail ? TestStatus::Failed : TestStatus::Passed, ms, isFail ? ln : L"");
                continue;
            }
        }
        // PHPUnit: " ✔ testName" / " ✘ testName"
        {
            if (ln.find(L'\u2714') != std::wstring::npos || ln.find(L"OK (") != std::wstring::npos) {
                static const std::wregex kPhpOkRe(LR"(\u2714\s+([A-Za-z0-9_ ]+))");
                std::wsmatch m;
                if (std::regex_search(ln, m, kPhpOkRe)) {
                    markMatch(TrimW(m[1].str()), TestStatus::Passed, 0.0, L"");
                }
            }
        }
        // CTest: "1/2 Test #1: MyTest .... Passed 0.02 sec" or "***Failed 0.01 sec"
        {
            static const std::wregex kCtestRe(LR"(Test\s+#\d+:\s+([A-Za-z0-9_\.\-]+)\s+\.+\s*(Passed|\*\*\*Failed|\*\*\*Not Run)\s+(\d+(?:\.\d+)?)\s+sec)");
            std::wsmatch m;
            if (std::regex_search(ln, m, kCtestRe)) {
                std::wstring stStr = m[2].str();
                TestStatus st = (stStr == L"Passed") ? TestStatus::Passed
                              : (stStr.find(L"Not Run") != std::wstring::npos ? TestStatus::Skipped : TestStatus::Failed);
                double sec = 0.0;
                try { sec = std::stod(m[3].str()); } catch (...) {}
                markMatch(m[1].str(), st, sec * 1000.0, st == TestStatus::Failed ? ln : L"");
            }
        }
    }

    for (const auto& it : items) {
        if (fw != TestFramework::Unknown && it.framework != fw) continue;
        summary.total++;
        if (it.status == TestStatus::Passed) summary.passed++;
        else if (it.status == TestStatus::Failed) summary.failed++;
        else if (it.status == TestStatus::Skipped) summary.skipped++;
    }
    return summary;
}

TestRunSummary TestExplorerEngine::ExecuteTestsSync(
    const std::wstring& workspaceRoot,
    TestFramework fw,
    std::vector<TestItem>& items,
    const std::wstring& filterName,
    const std::wstring& filePath,
    int timeoutMs) {
    std::wstring cmd = BuildRunCommand(workspaceRoot, fw, filterName, filePath);
    if (cmd.empty()) {
        TestRunSummary s;
        s.exitCode = -1;
        return s;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        TestRunSummary s;
        s.exitCode = -1;
        return s;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring fullCmd = L"cmd.exe /c " + cmd;
    BOOL ok = CreateProcessW(nullptr, fullCmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             workspaceRoot.empty() ? nullptr : workspaceRoot.c_str(), &si, &pi);
    CloseHandle(hWrite);

    std::string rawBytes;
    if (ok) {
        DWORD startTick = GetTickCount();
        char buf[4096];
        for (;;) {
            DWORD avail = 0;
            if (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD readBytes = 0;
                if (ReadFile(hRead, buf, sizeof(buf), &readBytes, nullptr) && readBytes > 0) {
                    rawBytes.append(buf, readBytes);
                }
            } else {
                DWORD waitRes = WaitForSingleObject(pi.hProcess, 25);
                if (waitRes == WAIT_OBJECT_0) {
                    while (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                        DWORD readBytes = 0;
                        if (ReadFile(hRead, buf, sizeof(buf), &readBytes, nullptr) && readBytes > 0) {
                            rawBytes.append(buf, readBytes);
                        } else break;
                    }
                    break;
                }
                if (static_cast<int>(GetTickCount() - startTick) >= timeoutMs) {
                    TerminateProcess(pi.hProcess, 1);
                    break;
                }
            }
        }
    }

    DWORD exitCode = 1;
    if (ok) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(hRead);

    std::wstring outW = Utf8ToWide(rawBytes);
    return ParseTestOutput(fw, outW, items, static_cast<int>(exitCode));
}

} // namespace phide::dev
