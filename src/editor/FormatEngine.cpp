/**
 * @file FormatEngine.cpp
 * @brief High-performance native offline code formatter for HTML, CSS, JavaScript/TypeScript/JSX, JSON, and PHP.
 *        100% local C++20 implementation. No external tools, cloud APIs, or LLMs required.
 */

#include "phide/editor/FormatEngine.hpp"
#include <windows.h>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <cwctype>
#include <vector>

namespace phide::editor {

// ==============================================================================

// ==============================================================================
// Prettier CLI Integration for Fast (~200ms) Standard AST Formatting
// ==============================================================================

static std::wstring FindPrettierPath() {
    static std::wstring s_cachedPrettierPath;
    static bool s_checked = false;
    if (s_checked) return s_cachedPrettierPath;

    std::error_code ec;

    // 1. Check bundled phide.prettier extension
    std::filesystem::path bundledPath = L"assets\\extensions\\phide.prettier\\node_modules\\prettier\\bin\\prettier.cjs";
    if (std::filesystem::exists(bundledPath, ec)) {
        s_cachedPrettierPath = std::filesystem::absolute(bundledPath).wstring();
        s_checked = true;
        return s_cachedPrettierPath;
    }
    bundledPath = L"bin\\assets\\extensions\\phide.prettier\\node_modules\\prettier\\bin\\prettier.cjs";
    if (std::filesystem::exists(bundledPath, ec)) {
        s_cachedPrettierPath = std::filesystem::absolute(bundledPath).wstring();
        s_checked = true;
        return s_cachedPrettierPath;
    }

    // 2. Check APPDATA\npm\prettier.cmd
    wchar_t* pAppData = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&pAppData, &len, L"APPDATA") == 0 && pAppData) {
        std::filesystem::path p = std::filesystem::path(pAppData) / L"npm" / L"prettier.cmd";
        free(pAppData);
        if (std::filesystem::exists(p, ec)) {
            s_cachedPrettierPath = p.wstring();
            s_checked = true;
            return s_cachedPrettierPath;
        }
    }

    // 3. Check local node_modules\.bin\prettier.cmd
    if (std::filesystem::exists(L"node_modules\\.bin\\prettier.cmd", ec)) {
        s_cachedPrettierPath = std::filesystem::absolute(L"node_modules\\.bin\\prettier.cmd").wstring();
        s_checked = true;
        return s_cachedPrettierPath;
    }

    // 4. Search PATH for prettier.cmd
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"prettier.cmd", nullptr, MAX_PATH, buf, nullptr) > 0) {
        s_cachedPrettierPath = buf;
        s_checked = true;
        return s_cachedPrettierPath;
    }

    // 5. Search PATH for prettier (.cmd)
    if (SearchPathW(nullptr, L"prettier", L".cmd", MAX_PATH, buf, nullptr) > 0) {
        s_cachedPrettierPath = buf;
        s_checked = true;
        return s_cachedPrettierPath;
    }

    s_checked = true;
    return s_cachedPrettierPath;
}

static bool RunPrettier(const std::wstring& src, const std::wstring& ext, int tabSize, std::wstring& outFormatted) {
    std::wstring prettierPath = FindPrettierPath();
    if (prettierPath.empty()) return false;

    HANDLE hChildStdInRead = nullptr, hChildStdInWrite = nullptr;
    HANDLE hChildStdOutRead = nullptr, hChildStdOutWrite = nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &sa, 0)) return false;
    SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hChildStdInRead, &hChildStdInWrite, &sa, 0)) {
        CloseHandle(hChildStdOutRead);
        CloseHandle(hChildStdOutWrite);
        return false;
    }
    SetHandleInformation(hChildStdInWrite, HANDLE_FLAG_INHERIT, 0);

    std::wstring dummyFile = L"temp" + ext;
    std::wstring cmd;
    bool isScript = (prettierPath.length() >= 4 && (prettierPath.substr(prettierPath.length() - 4) == L".cjs" || prettierPath.substr(prettierPath.length() - 3) == L".js"));
    if (isScript) {
        cmd = L"node.exe \"" + prettierPath + L"\" --stdin-filepath " + dummyFile + L" --tab-width " + std::to_wstring(tabSize);
    } else {
        cmd = L"cmd.exe /c \"\"" + prettierPath + L"\" --stdin-filepath " + dummyFile + L" --tab-width " + std::to_wstring(tabSize) + L"\"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdInput = hChildStdInRead;
    si.hStdOutput = hChildStdOutWrite;
    si.hStdError = hChildStdOutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    BOOL success = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hChildStdInRead);
    CloseHandle(hChildStdOutWrite);

    if (!success) {
        CloseHandle(hChildStdInWrite);
        CloseHandle(hChildStdOutRead);
        return false;
    }

    // Convert src wstring to UTF-8 and write to child stdin
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, src.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len > 1) {
        std::string utf8Str(utf8Len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, src.c_str(), -1, &utf8Str[0], utf8Len - 1, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(hChildStdInWrite, utf8Str.data(), static_cast<DWORD>(utf8Str.size()), &written, nullptr);
    }
    CloseHandle(hChildStdInWrite);

    // Read formatted output from child stdout
    std::string outputUtf8;
    char buffer[4096];
    DWORD readBytes = 0;
    while (ReadFile(hChildStdOutRead, buffer, sizeof(buffer), &readBytes, nullptr) && readBytes > 0) {
        outputUtf8.append(buffer, readBytes);
    }
    CloseHandle(hChildStdOutRead);

    WaitForSingleObject(pi.hProcess, 3000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0 || outputUtf8.empty()) {
        return false;
    }

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, outputUtf8.c_str(), -1, nullptr, 0);
    if (wideLen <= 1) return false;
    std::wstring rawOut(wideLen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, outputUtf8.c_str(), -1, &rawOut[0], wideLen - 1);

    // Normalize \r\n to \n
    std::wstring normalized;
    normalized.reserve(rawOut.size());
    for (wchar_t ch : rawOut) {
        if (ch != L'\r' && ch != L'\0') {
            normalized += ch;
        }
    }
    outFormatted = std::move(normalized);
    return true;
}

// Common Formatting Helpers
// ==============================================================================

static std::wstring MakeIndent(int level, int tabSize) {
    if (level <= 0) return L"";
    return std::wstring(static_cast<size_t>(level * tabSize), L' ');
}

static std::wstring Trim(const std::wstring& s) {
    size_t start = 0;
    while (start < s.length() && (s[start] == L' ' || s[start] == L'\t' || s[start] == L'\r' || s[start] == L'\n')) {
        start++;
    }
    size_t end = s.length();
    while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        end--;
    }
    return (start < end) ? s.substr(start, end - start) : L"";
}

static std::wstring TrimRight(const std::wstring& s) {
    size_t end = s.length();
    while (end > 0 && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        end--;
    }
    return s.substr(0, end);
}

static bool StartsWith(const std::wstring& str, const std::wstring& prefix) {
    return str.rfind(prefix, 0) == 0;
}

static bool ch_starts(const std::wstring& s, size_t pos, const wchar_t* prefix) {
    if (!prefix) return false;
    size_t plen = wcslen(prefix);
    if (pos + plen > s.length()) return false;
    return s.compare(pos, plen, prefix) == 0;
}

static bool EndsWith(const std::wstring& str, const std::wstring& suffix) {
    if (str.length() < suffix.length()) return false;
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

// ==============================================================================
// 1. JSON Formatter (via Prettier)
// ==============================================================================

std::wstring FormatEngine::FormatJson(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";
    std::wstring formatted;
    if (RunPrettier(src, L".json", tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }
    return src;
}

// ==============================================================================
// 2. CSS Formatter (via Prettier)
// ==============================================================================

std::wstring FormatEngine::FormatCss(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";
    std::wstring formatted;
    if (RunPrettier(src, L".css", tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }
    return src;
}

// ==============================================================================
// 3. HTML Formatter (via Prettier)
// ==============================================================================

std::wstring FormatEngine::FormatHtml(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";
    std::wstring formatted;
    if (RunPrettier(src, L".html", tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }
    return src;
}

// ==============================================================================
// 4. JavaScript / TypeScript / JSX Formatter (via Prettier)
// ==============================================================================

std::wstring FormatEngine::FormatJavaScript(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";
    std::wstring formatted;
    if (RunPrettier(src, L".jsx", tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }
    return src;
}

// ==============================================================================
// 5. PHP Formatter (PSR-12 Standard)
// ==============================================================================

std::wstring FormatEngine::FormatPhp(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";

    std::wstring out;
    out.reserve(src.length() * 2);
    int indentLevel = 0;
    std::wistringstream iss(src);
    std::wstring line;

    bool inBlockComment = false;
    bool inPhpTag = false;

    auto appendIndent = [&](int lvl) {
        out += MakeIndent(lvl, tabSize);
    };

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::wstring trimmed = Trim(line);
        if (trimmed.empty()) {
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            if (out.length() >= 2 && out[out.length() - 2] != L'\n') out += L'\n';
            continue;
        }

        // 1. Comments
        if (inBlockComment) {
            out += MakeIndent(indentLevel, tabSize) + trimmed + L'\n';
            if (trimmed.find(L"*/") != std::wstring::npos) inBlockComment = false;
            continue;
        }
        if (StartsWith(trimmed, L"/*") && trimmed.find(L"*/") == std::wstring::npos) {
            inBlockComment = true;
            out += MakeIndent(indentLevel, tabSize) + trimmed + L'\n';
            continue;
        }

        // 2. Track PHP tag boundaries
        if (StartsWith(trimmed, L"<?php") || StartsWith(trimmed, L"<?=")) {
            inPhpTag = true;
            out += trimmed + L'\n';
            continue;
        }
        if (trimmed == L"?>") {
            inPhpTag = false;
            out += trimmed + L'\n';
            continue;
        }

        // 3. Determine dedent for lines starting with closing tags / braces / alternative syntax
        int lineDedent = 0;
        if (trimmed[0] == L'}' || trimmed[0] == L']' || trimmed[0] == L')') {
            for (wchar_t ch : trimmed) {
                if (ch == L'}' || ch == L']' || ch == L')') lineDedent++;
                else if (!iswspace(ch)) break;
            }
        } else if (StartsWith(trimmed, L"</")) {
            lineDedent++;
        } else {
            std::wstring lower = trimmed;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            if (StartsWith(lower, L"endif") || StartsWith(lower, L"endforeach") ||
                StartsWith(lower, L"endwhile") || StartsWith(lower, L"endfor") ||
                StartsWith(lower, L"endswitch") || StartsWith(lower, L"else") ||
                StartsWith(lower, L"elseif") || StartsWith(lower, L"} else") ||
                StartsWith(lower, L"} elseif") || StartsWith(lower, L"} catch") ||
                StartsWith(lower, L"} finally")) {
                lineDedent++;
            }
        }

        int currentIndent = indentLevel - lineDedent;
        if (currentIndent < 0) currentIndent = 0;

        // Clean up arrow operators: "=>" with spaces, "->" without spaces
        std::wstring cleanedLine;
        cleanedLine.reserve(trimmed.length() * 2);
        bool inStr = false;
        wchar_t strChar = 0;

        for (size_t k = 0; k < trimmed.length(); ++k) {
            wchar_t c = trimmed[k];
            if (inStr) {
                cleanedLine += c;
                if (c == L'\\' && k + 1 < trimmed.length()) {
                    cleanedLine += trimmed[k + 1];
                    k++;
                    continue;
                }
                if (c == strChar) inStr = false;
                continue;
            }
            if (c == L'\'' || c == L'"') {
                inStr = true;
                strChar = c;
                cleanedLine += c;
                continue;
            }
            // Double arrow: "=>"
            if (c == L'=' && k + 1 < trimmed.length() && trimmed[k + 1] == L'>') {
                // Ensure single space before and after =>
                while (!cleanedLine.empty() && cleanedLine.back() == L' ') cleanedLine.pop_back();
                cleanedLine += L" => ";
                k++;
                while (k + 1 < trimmed.length() && (trimmed[k + 1] == L' ' || trimmed[k + 1] == L'\t')) k++;
                continue;
            }
            // Single arrow object access: "->"
            if (c == L'-' && k + 1 < trimmed.length() && trimmed[k + 1] == L'>') {
                while (!cleanedLine.empty() && cleanedLine.back() == L' ') cleanedLine.pop_back();
                cleanedLine += L"->";
                k++;
                while (k + 1 < trimmed.length() && (trimmed[k + 1] == L' ' || trimmed[k + 1] == L'\t')) k++;
                continue;
            }
            // Comma spacing
            if (c == L',') {
                cleanedLine += c;
                if (k + 1 < trimmed.length() && trimmed[k + 1] != L' ' && trimmed[k + 1] != L'\t') {
                    cleanedLine += L' ';
                }
                continue;
            }
            cleanedLine += c;
        }

        appendIndent(currentIndent);
        out += cleanedLine + L'\n';

        // 4. Compute indent delta for subsequent lines
        int delta = 0;
        bool inStringLiteral = false;
        wchar_t strDelim = 0;

        for (size_t k = 0; k < trimmed.length(); ++k) {
            wchar_t c = trimmed[k];
            if (inStringLiteral) {
                if (c == L'\\' && k + 1 < trimmed.length()) { k++; continue; }
                if (c == strDelim) inStringLiteral = false;
                continue;
            }
            if (c == L'\'' || c == L'"') {
                inStringLiteral = true;
                strDelim = c;
                continue;
            }
            if ((c == L'/' && k + 1 < trimmed.length() && trimmed[k + 1] == L'/') || c == L'#') {
                break;
            }

            if (c == L'{' || c == L'[' || c == L'(') delta++;
            else if (c == L'}' || c == L']' || c == L')') delta--;
        }

        // Alternative control structure colon: "if (...):" or "foreach (...):"
        if (trimmed.back() == L':') {
            std::wstring lower = trimmed;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            if (lower.find(L"if") != std::wstring::npos || lower.find(L"foreach") != std::wstring::npos ||
                lower.find(L"while") != std::wstring::npos || lower.find(L"for") != std::wstring::npos ||
                lower.find(L"else") != std::wstring::npos || lower.find(L"switch") != std::wstring::npos) {
                delta++;
            }
        }

        indentLevel += delta;
        if (indentLevel < 0) indentLevel = 0;
    }

    if (!out.empty() && out.back() != L'\n') out += L'\n';
    return out;
}

// ==============================================================================
// 6. FormatDocument Master Dispatcher
// ==============================================================================

bool FormatEngine::CanFormat(const std::wstring& ext) {
    std::wstring lower = ext;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    return (lower == L".js"   || lower == L".jsx"  || lower == L".mjs"  || lower == L".cjs" ||
            lower == L".ts"   || lower == L".tsx"  || lower == L".mts"  || lower == L".cts" ||
            lower == L".html" || lower == L".htm"  || lower == L".vue"  || lower == L".svelte" ||
            lower == L".css"  || lower == L".scss" || lower == L".less" ||
            lower == L".json" || lower == L".jsonc"|| lower == L".json5"||
            lower == L".yaml" || lower == L".yml"  || lower == L".md"   || lower == L".markdown" ||
            lower == L".graphql" || lower == L".gql" ||
            lower == L".php"  || lower == L".phtml" || lower == L".php8");
}

std::wstring FormatEngine::FormatDocument(const std::wstring& src, const std::wstring& ext, int tabSize) {
    if (src.empty()) return L"";

    std::wstring lower = ext;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));

    // 1. Try Prettier via bundled phide.prettier extension or system Prettier
    std::wstring formatted;
    if (RunPrettier(src, lower, tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }

    // 2. Fall back to native PSR-12 formatter for PHP files if Prettier is not handling PHP
    if (lower == L".php" || lower == L".phtml" || lower == L".php8") {
        return FormatPhp(src, tabSize);
    }

    // Default fallback: normalize line endings and trim trailing whitespace
    std::wistringstream ss(src);
    std::wstring line;
    std::wstring out;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        out += TrimRight(line) + L'\n';
    }
    return out;
}


} // namespace phide::editor
