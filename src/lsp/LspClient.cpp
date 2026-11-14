/**
 * @file LspClient.cpp
 * @brief Real Win32 anonymous-pipe JSON-RPC 2.0 Language Server Protocol client.
 */

#include "phide/lsp/LspClient.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <utility>

namespace phide::lsp {

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::wstring ToLowerW(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::string EscapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8]{};
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

std::string EnsureFileUriUtf8(const std::wstring& pathOrUri) {
    if (pathOrUri.rfind(L"file://", 0) == 0 || pathOrUri.rfind(L"FILE://", 0) == 0) {
        return WideToUtf8(pathOrUri);
    }
    return WideToUtf8(LspClient::PathToFileUri(pathOrUri));
}

// Lightweight self-contained UTF-8 JSON DOM parser for LSP responses/notifications
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::vector<std::pair<std::string, JsonValue>> objVal;

    const JsonValue* Get(const char* key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : objVal) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    std::string GetString(const char* key, const std::string& def = {}) const {
        const auto* v = Get(key);
        return (v && v->type == Type::String) ? v->strVal : def;
    }

    long long GetInt(const char* key, long long def = 0) const {
        const auto* v = Get(key);
        return (v && v->type == Type::Number) ? static_cast<long long>(v->numVal) : def;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : m_src(src) {}

    bool Parse(JsonValue& out) {
        SkipWs();
        if (!ParseValue(out, 0)) return false;
        return true;
    }

private:
    const std::string& m_src;
    size_t m_pos = 0;

    void SkipWs() {
        while (m_pos < m_src.size()) {
            char c = m_src[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++m_pos;
            else break;
        }
    }

    static void AppendUtf8Codepoint(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0x10FFFF) {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool ParseHex4(uint32_t& out) {
        if (m_pos + 4 > m_src.size()) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = m_src[m_pos++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }

    bool ParseString(std::string& out) {
        if (m_pos >= m_src.size() || m_src[m_pos] != '"') return false;
        ++m_pos;
        out.clear();
        while (m_pos < m_src.size()) {
            char c = m_src[m_pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (m_pos >= m_src.size()) return false;
                char esc = m_src[m_pos++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!ParseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && m_pos + 2 <= m_src.size() &&
                            m_src[m_pos] == '\\' && m_src[m_pos + 1] == 'u') {
                            m_pos += 2;
                            uint32_t lo = 0;
                            if (!ParseHex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                            }
                        }
                        AppendUtf8Codepoint(out, cp);
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseValue(JsonValue& out, int depth) {
        if (depth > 64) return false;
        SkipWs();
        if (m_pos >= m_src.size()) return false;
        char c = m_src[m_pos];
        if (c == 'n' && m_src.compare(m_pos, 4, "null") == 0) {
            out.type = JsonValue::Type::Null;
            m_pos += 4;
            return true;
        }
        if (c == 't' && m_src.compare(m_pos, 4, "true") == 0) {
            out.type = JsonValue::Type::Bool;
            out.boolVal = true;
            m_pos += 4;
            return true;
        }
        if (c == 'f' && m_src.compare(m_pos, 5, "false") == 0) {
            out.type = JsonValue::Type::Bool;
            out.boolVal = false;
            m_pos += 5;
            return true;
        }
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return ParseString(out.strVal);
        }
        if (c == '{') {
            out.type = JsonValue::Type::Object;
            ++m_pos;
            SkipWs();
            if (m_pos < m_src.size() && m_src[m_pos] == '}') { ++m_pos; return true; }
            while (m_pos < m_src.size()) {
                SkipWs();
                std::string key;
                if (!ParseString(key)) return false;
                SkipWs();
                if (m_pos >= m_src.size() || m_src[m_pos] != ':') return false;
                ++m_pos;
                JsonValue val;
                if (!ParseValue(val, depth + 1)) return false;
                out.objVal.emplace_back(std::move(key), std::move(val));
                SkipWs();
                if (m_pos >= m_src.size()) return false;
                if (m_src[m_pos] == '}') { ++m_pos; return true; }
                if (m_src[m_pos] != ',') return false;
                ++m_pos;
            }
            return false;
        }
        if (c == '[') {
            out.type = JsonValue::Type::Array;
            ++m_pos;
            SkipWs();
            if (m_pos < m_src.size() && m_src[m_pos] == ']') { ++m_pos; return true; }
            while (m_pos < m_src.size()) {
                JsonValue item;
                if (!ParseValue(item, depth + 1)) return false;
                out.arrVal.push_back(std::move(item));
                SkipWs();
                if (m_pos >= m_src.size()) return false;
                if (m_src[m_pos] == ']') { ++m_pos; return true; }
                if (m_src[m_pos] != ',') return false;
                ++m_pos;
            }
            return false;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            out.type = JsonValue::Type::Number;
            const char* start = m_src.c_str() + m_pos;
            char* endPtr = nullptr;
            out.numVal = std::strtod(start, &endPtr);
            if (endPtr == start) return false;
            m_pos += static_cast<size_t>(endPtr - start);
            return true;
        }
        return false;
    }
};

std::wstring ResolveExecutableOnSystem(const std::wstring& exeName, const std::vector<std::wstring>& extraDirs) {
    std::error_code ec;
    for (const auto& dir : extraDirs) {
        if (dir.empty()) continue;
        std::filesystem::path candidate = std::filesystem::path(dir) / exeName;
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
            return candidate.wstring();
        }
    }
    wchar_t foundBuf[MAX_PATH]{};
    DWORD n = SearchPathW(nullptr, exeName.c_str(), nullptr, MAX_PATH, foundBuf, nullptr);
    if (n > 0 && n < MAX_PATH) {
        return std::wstring(foundBuf, n);
    }
    return {};
}

std::wstring BuildLaunchCommandLine(const std::wstring& exePath, const std::wstring& extraArgs) {
    std::wstring lower = ToLowerW(exePath);
    bool isCmdBatch = (lower.size() >= 4 &&
                       (lower.compare(lower.size() - 4, 4, L".cmd") == 0 ||
                        lower.compare(lower.size() - 4, 4, L".bat") == 0));
    if (isCmdBatch) {
        std::wstring inner = L"\"" + exePath + L"\"";
        if (!extraArgs.empty()) inner += L" " + extraArgs;
        return L"cmd.exe /d /s /c \"" + inner + L"\"";
    }
    std::wstring cmd = L"\"" + exePath + L"\"";
    if (!extraArgs.empty()) cmd += L" " + extraArgs;
    return cmd;
}

std::wstring InferServerArgs(const std::wstring& exePath) {
    std::wstring lower = ToLowerW(exePath);
    if (lower.find(L"phpactor") != std::wstring::npos) {
        return L"language-server";
    }
    if (lower.find(L"intelephense") != std::wstring::npos ||
        lower.find(L"pyright") != std::wstring::npos ||
        lower.find(L"typescript-language-server") != std::wstring::npos) {
        return L"--stdio";
    }
    return L"";
}

} // namespace

LspClient::LspClient() = default;

LspClient::~LspClient() {
    Shutdown();
}

std::wstring LspClient::PathToFileUri(const std::wstring& path) {
    if (path.empty()) return L"file:///";
    if (path.rfind(L"file://", 0) == 0) return path;

    std::wstring fwd = path;
    for (wchar_t& c : fwd) {
        if (c == L'\\') c = L'/';
    }
    if (fwd.size() >= 2 && iswalpha(fwd[0]) && fwd[1] == L':') {
        fwd = L"/" + fwd;
    } else if (fwd.front() != L'/') {
        fwd = L"/" + fwd;
    }

    // Encode characters required for RFC 8089 file URIs (preserve drive colon and slashes)
    std::string utf8 = WideToUtf8(fwd);
    std::string encoded = "file://";
    encoded.reserve(utf8.size() + 16);
    for (size_t i = 0; i < utf8.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        bool isDriveColon = (i == 2 && c == ':' && encoded.size() >= 9);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || isDriveColon) {
            encoded.push_back(static_cast<char>(c));
        } else {
            char hex[4]{};
            snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned int>(c));
            encoded += hex;
        }
    }
    return Utf8ToWide(encoded);
}

std::wstring LspClient::FileUriToPath(const std::wstring& uri) {
    std::string s = WideToUtf8(uri);
    const std::string prefix = "file://";
    if (s.rfind(prefix, 0) != 0) return uri;
    s = s.substr(prefix.size());

    // Percent-decode
    std::string decoded;
    decoded.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hexVal = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return -1;
            };
            int hi = hexVal(s[i + 1]);
            int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(s[i]);
    }

    // Strip leading '/' before Windows drive letter e.g. /C:/foo -> C:\foo
    if (decoded.size() >= 3 && decoded[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(decoded[1])) && decoded[2] == ':') {
        decoded.erase(decoded.begin());
    }
    std::wstring w = Utf8ToWide(decoded);
    for (wchar_t& c : w) {
        if (c == L'/') c = L'\\';
    }
    return w;
}

void LspClient::SetDiagnosticsCallback(DiagnosticsCallback cb) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_diagnosticsCallback = std::move(cb);
}

void LspClient::ResetDiscoveryCache() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_failedDiscoveryKeys.clear();
}

bool LspClient::StartServer(const std::wstring& serverExecutable, const std::wstring& workspaceUri) {
    std::lock_guard<std::recursive_mutex> lifecycleLock(m_lifecycleMutex);
    Shutdown();
    if (serverExecutable.empty()) return false;

    std::wstring resolved = serverExecutable;
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec)) {
        resolved = ResolveExecutableOnSystem(serverExecutable, {});
        if (resolved.empty()) return false;
    }

    std::wstring rootPath = FileUriToPath(workspaceUri);
    std::wstring rootUri = PathToFileUri(rootPath);
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_currentServer = resolved;
        m_workspaceRoot = rootPath;
        m_docVersions.clear();
    }

    std::wstring cmdLine = BuildLaunchCommandLine(resolved, InferServerArgs(resolved));
    return LaunchProcessAndInitialize(cmdLine, rootPath, rootUri);
}

bool LspClient::EnsureServerForLanguage(const std::wstring& languageId, const std::wstring& workspaceRoot) {
    std::lock_guard<std::recursive_mutex> lifecycleLock(m_lifecycleMutex);
    std::wstring lang = ToLowerW(languageId);
    if (lang.empty()) return false;
    // Normalize aliases
    if (lang == L"javascriptreact") lang = L"javascript";
    else if (lang == L"typescriptreact") lang = L"typescript";
    else if (lang == L"c") lang = L"cpp";

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_isRunning.load() && m_currentLanguageId == lang &&
            (workspaceRoot.empty() || _wcsicmp(m_workspaceRoot.c_str(), workspaceRoot.c_str()) == 0)) {
            return true;
        }
        std::wstring cacheKey = lang + L"|" + ToLowerW(workspaceRoot);
        if (m_failedDiscoveryKeys.count(cacheKey) > 0) {
            return false;
        }
    }

    std::vector<std::wstring> extraDirs;
    if (!workspaceRoot.empty()) {
        std::filesystem::path ws(workspaceRoot);
        extraDirs.push_back((ws / L"vendor" / L"bin").wstring());
        extraDirs.push_back((ws / L"node_modules" / L".bin").wstring());
        extraDirs.push_back((ws / L".venv" / L"Scripts").wstring());
        extraDirs.push_back((ws / L"venv" / L"Scripts").wstring());
    }
    wchar_t appDataBuf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", appDataBuf, MAX_PATH) > 0) {
        extraDirs.push_back((std::filesystem::path(appDataBuf) / L"npm").wstring());
        extraDirs.push_back((std::filesystem::path(appDataBuf) / L"Composer" / L"vendor" / L"bin").wstring());
    }
    wchar_t localAppBuf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppBuf, MAX_PATH) > 0) {
        extraDirs.push_back((std::filesystem::path(localAppBuf) / L"Microsoft" / L"WindowsApps").wstring());
    }
    extraDirs.push_back(L"C:\\Program Files\\LLVM\\bin");

    struct Candidate {
        const wchar_t* exeName;
        const wchar_t* args;
    };
    std::vector<Candidate> candidates;
    if (lang == L"php") {
        candidates = {
            { L"intelephense.cmd", L"--stdio" },
            { L"intelephense.exe", L"--stdio" },
            { L"phpactor.bat", L"language-server" },
            { L"phpactor.exe", L"language-server" },
            { L"phpactor", L"language-server" }
        };
    } else if (lang == L"python") {
        candidates = {
            { L"pyright-langserver.cmd", L"--stdio" },
            { L"pyright-langserver.exe", L"--stdio" },
            { L"pylsp.exe", L"" },
            { L"pylsp", L"" }
        };
    } else if (lang == L"cpp") {
        candidates = {
            { L"clangd.exe", L"--background-index" },
            { L"clangd", L"--background-index" }
        };
    } else if (lang == L"typescript" || lang == L"javascript") {
        candidates = {
            { L"typescript-language-server.cmd", L"--stdio" },
            { L"typescript-language-server.exe", L"--stdio" }
        };
    } else {
        return false;
    }

    std::wstring foundExe;
    std::wstring foundArgs;
    for (const auto& c : candidates) {
        std::wstring p = ResolveExecutableOnSystem(c.exeName, extraDirs);
        if (!p.empty()) {
            foundExe = p;
            foundArgs = c.args;
            break;
        }
    }

    if (foundExe.empty()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_failedDiscoveryKeys.insert(lang + L"|" + ToLowerW(workspaceRoot));
        return false;
    }

    Shutdown();
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_currentServer = foundExe;
        m_currentLanguageId = lang;
        m_workspaceRoot = workspaceRoot;
        m_docVersions.clear();
    }

    std::wstring cmdLine = BuildLaunchCommandLine(foundExe, foundArgs);
    std::wstring rootUri = workspaceRoot.empty() ? L"" : PathToFileUri(workspaceRoot);
    if (!LaunchProcessAndInitialize(cmdLine, workspaceRoot, rootUri)) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_currentLanguageId.clear();
        m_failedDiscoveryKeys.insert(lang + L"|" + ToLowerW(workspaceRoot));
        return false;
    }
    return true;
}

bool LspClient::LaunchProcessAndInitialize(const std::wstring& commandLine, const std::wstring& workingDir, const std::wstring& rootUri) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hChildStdinRead = nullptr;
    HANDLE hChildStdinWrite = nullptr;
    HANDLE hChildStdoutRead = nullptr;
    HANDLE hChildStdoutWrite = nullptr;

    if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &sa, 0)) {
        return false;
    }
    if (!SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hChildStdinRead);
        CloseHandle(hChildStdinWrite);
        return false;
    }
    if (!CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &sa, 0)) {
        CloseHandle(hChildStdinRead);
        CloseHandle(hChildStdinWrite);
        return false;
    }
    if (!SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hChildStdinRead);
        CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead);
        CloseHandle(hChildStdoutWrite);
        return false;
    }

    HANDLE hNullErr = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    // Job object ensures language server subtree terminates if phide crashes or closes
    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hChildStdinRead;
    si.hStdOutput = hChildStdoutWrite;
    si.hStdError = (hNullErr != INVALID_HANDLE_VALUE) ? hNullErr : hChildStdoutWrite;

    std::vector<wchar_t> cmdBuf(commandLine.begin(), commandLine.end());
    cmdBuf.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    const wchar_t* cwdPtr = (!workingDir.empty() && std::filesystem::exists(workingDir)) ? workingDir.c_str() : nullptr;
    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr,
        cwdPtr,
        &si,
        &pi
    );

    // Parent must close child pipe ends immediately so EOF detection works
    CloseHandle(hChildStdinRead);
    CloseHandle(hChildStdoutWrite);
    if (hNullErr != INVALID_HANDLE_VALUE) CloseHandle(hNullErr);

    if (!ok) {
        CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead);
        if (hJob) CloseHandle(hJob);
        return false;
    }

    if (hJob) {
        AssignProcessToJobObject(hJob, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    m_stopRequested.store(false);
    m_hChildStdinWrite = hChildStdinWrite;
    m_hChildStdoutRead = hChildStdoutRead;
    m_hChildProcess = pi.hProcess;
    m_hJobObject = hJob;
    m_isRunning.store(true);

    m_readerThread = std::thread([this, hChildStdoutRead]() {
        ReaderThreadLoop(hChildStdoutRead);
    });

    // Perform LSP initialize -> initialized handshake
    DWORD pid = GetCurrentProcessId();
    std::ostringstream initParams;
    initParams << "{"
               << "\"processId\":" << pid << ","
               << "\"clientInfo\":{\"name\":\"phide\",\"version\":\"1.0\"},"
               << "\"rootUri\":" << (rootUri.empty() ? "null" : EscapeJsonString(WideToUtf8(rootUri))) << ","
               << "\"capabilities\":{"
               << "\"textDocument\":{"
               << "\"synchronization\":{\"dynamicRegistration\":false,\"willSave\":false,\"didSave\":true},"
               << "\"completion\":{\"completionItem\":{\"snippetSupport\":true,\"documentationFormat\":[\"plaintext\"]}},"
               << "\"definition\":{\"linkSupport\":false},"
               << "\"publishDiagnostics\":{\"relatedInformation\":false}"
               << "}"
               << "}"
               << "}";

    std::string initResp;
    if (!SendRequestAndWait("initialize", initParams.str(), initResp, 1800)) {
        Shutdown();
        return false;
    }

    SendNotification("initialized", "{}");
    return true;
}

void LspClient::Shutdown() {
    std::lock_guard<std::recursive_mutex> lifecycleLock(m_lifecycleMutex);
    bool wasRunning = m_isRunning.exchange(false);
    if (wasRunning) {
        // Send valid JSON-RPC shutdown request (with id) + exit notification before closing stdin
        SendJsonRpcFrame("{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"shutdown\",\"params\":null}");
        SendNotification("exit", "null");
    }

    m_stopRequested.store(true);

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        for (auto& kv : m_pendingRequests) {
            std::lock_guard<std::mutex> rl(kv.second->mtx);
            kv.second->done = true;
            kv.second->cv.notify_all();
        }
        m_pendingRequests.clear();
    }

    // Close stdin write pipe under m_writeMutex so servers reading EOF terminate promptly
    {
        std::lock_guard<std::mutex> wlock(m_writeMutex);
        if (m_hChildStdinWrite) {
            CloseHandle(static_cast<HANDLE>(m_hChildStdinWrite));
            m_hChildStdinWrite = nullptr;
        }
    }

    if (m_hChildProcess) {
        HANDLE hProc = static_cast<HANDLE>(m_hChildProcess);
        WaitForSingleObject(hProc, 150);
    }

    if (m_hJobObject) {
        CloseHandle(static_cast<HANDLE>(m_hJobObject));
        m_hJobObject = nullptr;
    }

    if (m_hChildProcess) {
        HANDLE hProc = static_cast<HANDLE>(m_hChildProcess);
        TerminateProcess(hProc, 0);
        CloseHandle(hProc);
        m_hChildProcess = nullptr;
    }

    if (m_readerThread.joinable()) {
        // Unblock synchronous pipe ReadFile cleanly while handle is still open
        CancelSynchronousIo(static_cast<HANDLE>(m_readerThread.native_handle()));
        m_readerThread.join();
    }

    if (m_hChildStdoutRead) {
        CloseHandle(static_cast<HANDLE>(m_hChildStdoutRead));
        m_hChildStdoutRead = nullptr;
    }

    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_docVersions.clear();
}

bool LspClient::SendJsonRpcFrame(const std::string& jsonBody) {
    std::lock_guard<std::mutex> lock(m_writeMutex);
    HANDLE hWrite = static_cast<HANDLE>(m_hChildStdinWrite);
    if (!hWrite || m_stopRequested.load()) return false;

    std::string frame = "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n\r\n" + jsonBody;
    size_t totalSent = 0;
    while (totalSent < frame.size()) {
        DWORD chunk = static_cast<DWORD>(frame.size() - totalSent);
        DWORD written = 0;
        if (!WriteFile(hWrite, frame.data() + totalSent, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        totalSent += written;
    }
    return true;
}

void LspClient::SendNotification(const std::string& method, const std::string& paramsJson) {
    if (!m_hChildStdinWrite) return;
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"method\":" << EscapeJsonString(method)
        << ",\"params\":" << (paramsJson.empty() ? "null" : paramsJson) << "}";
    SendJsonRpcFrame(oss.str());
}

bool LspClient::SendRequestAndWait(const std::string& method, const std::string& paramsJson,
                                     std::string& outResponseJson, int timeoutMs) {
    if (!m_isRunning.load() || !m_hChildStdinWrite) return false;
    const int reqId = m_nextRequestId.fetch_add(1);
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests[reqId] = pending;
    }

    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"id\":" << reqId
        << ",\"method\":" << EscapeJsonString(method)
        << ",\"params\":" << (paramsJson.empty() ? "null" : paramsJson) << "}";

    if (!SendJsonRpcFrame(oss.str())) {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.erase(reqId);
        return false;
    }

    std::unique_lock<std::mutex> waitLock(pending->mtx);
    bool signaled = pending->cv.wait_for(waitLock, std::chrono::milliseconds(timeoutMs),
                                         [&pending]() { return pending->done; });
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.erase(reqId);
    }

    if (!signaled || pending->responsePayload.empty()) {
        return false;
    }
    outResponseJson = std::move(pending->responsePayload);
    return true;
}

void LspClient::ReaderThreadLoop(void* hReadPipe) {
    HANDLE hRead = static_cast<HANDLE>(hReadPipe);
    std::string buffer;
    buffer.reserve(16384);
    char chunk[4096];

    while (!m_stopRequested.load()) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hRead, chunk, sizeof(chunk), &bytesRead, nullptr);
        if (!ok || bytesRead == 0) {
            break;
        }
        buffer.append(chunk, bytesRead);

        while (true) {
            size_t headerEnd = buffer.find("\r\n\r\n");
            size_t sepLen = 4;
            if (headerEnd == std::string::npos) {
                headerEnd = buffer.find("\n\n");
                sepLen = 2;
            }
            if (headerEnd == std::string::npos) break;

            std::string headers = buffer.substr(0, headerEnd);
            std::string headersLower = headers;
            for (char& ch : headersLower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            size_t clPos = headersLower.find("content-length:");
            if (clPos == std::string::npos) {
                buffer.erase(0, headerEnd + sepLen);
                continue;
            }
            size_t valStart = clPos + 15;
            while (valStart < headers.size() && (headers[valStart] == ' ' || headers[valStart] == '\t')) ++valStart;
            size_t contentLen = static_cast<size_t>(std::strtoull(headers.c_str() + valStart, nullptr, 10));
            constexpr size_t kMaxLspFrameBytes = 64ULL * 1024ULL * 1024ULL;
            if (contentLen > kMaxLspFrameBytes) {
                buffer.clear();
                break;
            }

            const size_t bodyStart = headerEnd + sepLen;
            if (buffer.size() < bodyStart + contentLen) {
                break; // Wait for remaining bytes of this JSON-RPC payload
            }

            std::string body = buffer.substr(bodyStart, contentLen);
            buffer.erase(0, bodyStart + contentLen);

            if (!m_stopRequested.load()) {
                HandleIncomingJsonMessage(body);
            }
        }
    }

    m_isRunning.store(false);
}

void LspClient::HandleIncomingJsonMessage(const std::string& jsonText) {
    JsonValue root;
    JsonParser parser(jsonText);
    if (!parser.Parse(root) || root.type != JsonValue::Type::Object) return;

    const JsonValue* idVal = root.Get("id");
    const JsonValue* methodVal = root.Get("method");

    // 1. Server-initiated request (e.g. client/registerCapability or workspace/configuration): acknowledge cleanly
    if (idVal && methodVal && methodVal->type == JsonValue::Type::String) {
        std::ostringstream ack;
        if (idVal->type == JsonValue::Type::Number) {
            ack << "{\"jsonrpc\":\"2.0\",\"id\":" << static_cast<long long>(idVal->numVal) << ",\"result\":null}";
        } else if (idVal->type == JsonValue::Type::String) {
            ack << "{\"jsonrpc\":\"2.0\",\"id\":" << EscapeJsonString(idVal->strVal) << ",\"result\":null}";
        }
        if (!ack.str().empty()) {
            SendJsonRpcFrame(ack.str());
        }
        return;
    }

    // 2. Response to one of our pending requests
    if (idVal && idVal->type == JsonValue::Type::Number && !methodVal) {
        int reqId = static_cast<int>(idVal->numVal);
        std::shared_ptr<PendingRequest> pending;
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            auto it = m_pendingRequests.find(reqId);
            if (it != m_pendingRequests.end()) {
                pending = it->second;
            }
        }
        if (pending) {
            std::lock_guard<std::mutex> rl(pending->mtx);
            pending->responsePayload = jsonText;
            pending->done = true;
            pending->cv.notify_all();
        }
        return;
    }

    // 3. Server notification: textDocument/publishDiagnostics
    if (methodVal && methodVal->type == JsonValue::Type::String &&
        methodVal->strVal == "textDocument/publishDiagnostics") {
        const JsonValue* params = root.Get("params");
        if (!params || params->type != JsonValue::Type::Object) return;

        std::string uriUtf8 = params->GetString("uri");
        if (uriUtf8.empty()) return;
        std::wstring filePath = FileUriToPath(Utf8ToWide(uriUtf8));

        std::vector<Diagnostic> diags;
        const JsonValue* diagArr = params->Get("diagnostics");
        if (diagArr && diagArr->type == JsonValue::Type::Array) {
            for (const auto& d : diagArr->arrVal) {
                if (d.type != JsonValue::Type::Object) continue;
                Diagnostic item;
                item.message = Utf8ToWide(d.GetString("message"));
                item.severity = static_cast<int>(d.GetInt("severity", 1));
                const JsonValue* range = d.Get("range");
                if (range) {
                    const JsonValue* start = range->Get("start");
                    const JsonValue* end = range->Get("end");
                    if (start) {
                        item.line = static_cast<size_t>((std::max)(0LL, start->GetInt("line", 0)));
                        item.col = static_cast<size_t>((std::max)(0LL, start->GetInt("character", 0)));
                    }
                    if (end && end->GetInt("line", 0) == static_cast<long long>(item.line)) {
                        long long endCol = end->GetInt("character", 0);
                        if (endCol > static_cast<long long>(item.col)) {
                            item.length = static_cast<size_t>(endCol - static_cast<long long>(item.col));
                        }
                    }
                }
                if (item.length == 0) item.length = 1;
                if (!item.message.empty()) {
                    diags.push_back(std::move(item));
                }
            }
        }

        DiagnosticsCallback cbCopy;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            cbCopy = m_diagnosticsCallback;
        }
        if (cbCopy) {
            cbCopy(filePath, std::move(diags));
        }
    }
}

void LspClient::NotifyDidOpen(const std::wstring& fileUri, const std::wstring& languageId, const std::wstring& text) {
    if (!m_isRunning.load()) return;
    std::string uri = EnsureFileUriUtf8(fileUri);
    std::wstring uriW = Utf8ToWide(uri);
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_docVersions.count(uriW) > 0) {
            // Already open on server; emit incremental didChange instead
        } else {
            m_docVersions[uriW] = 1;
            std::ostringstream params;
            params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri)
                   << ",\"languageId\":" << EscapeJsonString(WideToUtf8(languageId))
                   << ",\"version\":1"
                   << ",\"text\":" << EscapeJsonString(WideToUtf8(text)) << "}}";
            SendNotification("textDocument/didOpen", params.str());
            return;
        }
    }
    NotifyDidChange(fileUri, text);
}

void LspClient::NotifyDidChange(const std::wstring& fileUri, const std::wstring& text) {
    if (!m_isRunning.load()) return;
    std::string uri = EnsureFileUriUtf8(fileUri);
    std::wstring uriW = Utf8ToWide(uri);
    int nextVer = 1;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        auto it = m_docVersions.find(uriW);
        if (it == m_docVersions.end()) {
            return;
        }
        nextVer = ++(it->second);
    }

    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri) << ",\"version\":" << nextVer
           << "},\"contentChanges\":[{\"text\":" << EscapeJsonString(WideToUtf8(text)) << "}]}";
    SendNotification("textDocument/didChange", params.str());
}

void LspClient::NotifyDidSave(const std::wstring& fileUri, const std::wstring& text) {
    if (!m_isRunning.load()) return;
    std::string uri = EnsureFileUriUtf8(fileUri);
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri)
           << "},\"text\":" << EscapeJsonString(WideToUtf8(text)) << "}";
    SendNotification("textDocument/didSave", params.str());
}

void LspClient::NotifyDidClose(const std::wstring& fileUri) {
    if (!m_isRunning.load()) return;
    std::string uri = EnsureFileUriUtf8(fileUri);
    std::wstring uriW = Utf8ToWide(uri);
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_docVersions.erase(uriW);
    }
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri) << "}}";
    SendNotification("textDocument/didClose", params.str());
}

std::vector<CompletionItem> LspClient::RequestCompletions(const std::wstring& fileUri, size_t line, size_t col) {
    std::vector<CompletionItem> items;
    if (!m_isRunning.load()) return items;

    std::string uri = EnsureFileUriUtf8(fileUri);
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri)
           << "},\"position\":{\"line\":" << line << ",\"character\":" << col << "}}";

    std::string respJson;
    if (!SendRequestAndWait("textDocument/completion", params.str(), respJson, 500)) {
        return items;
    }

    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return items;

    const JsonValue* result = root.Get("result");
    if (!result) return items;

    const JsonValue* arr = nullptr;
    if (result->type == JsonValue::Type::Array) {
        arr = result;
    } else if (result->type == JsonValue::Type::Object) {
        arr = result->Get("items");
    }
    if (!arr || arr->type != JsonValue::Type::Array) return items;

    const size_t cap = (std::min)(arr->arrVal.size(), static_cast<size_t>(80));
    items.reserve(cap);
    for (size_t i = 0; i < cap; ++i) {
        const auto& el = arr->arrVal[i];
        if (el.type != JsonValue::Type::Object) continue;
        std::string label = el.GetString("label");
        if (label.empty()) continue;

        CompletionItem ci;
        ci.label = Utf8ToWide(label);
        ci.detail = Utf8ToWide(el.GetString("detail"));
        ci.kind = static_cast<int>(el.GetInt("kind", 1));
        std::string insertText = el.GetString("insertText");
        if (insertText.empty()) {
            const JsonValue* textEdit = el.Get("textEdit");
            if (textEdit) insertText = textEdit->GetString("newText");
        }
        ci.insertText = insertText.empty() ? ci.label : Utf8ToWide(insertText);
        items.push_back(std::move(ci));
    }
    return items;
}

static bool ParseLocationEntry(const JsonValue& locObj, Location& outLoc) {
    if (locObj.type != JsonValue::Type::Object) return false;
    std::string targetUri = locObj.GetString("uri");
    const JsonValue* range = locObj.Get("range");
    if (targetUri.empty()) {
        targetUri = locObj.GetString("targetUri");
        range = locObj.Get("targetSelectionRange");
        if (!range) range = locObj.Get("targetRange");
    }
    if (targetUri.empty()) return false;

    outLoc.filePath = LspClient::FileUriToPath(Utf8ToWide(targetUri));
    if (range) {
        const JsonValue* start = range->Get("start");
        if (start) {
            outLoc.line = static_cast<size_t>((std::max)(0LL, start->GetInt("line", 0)));
            outLoc.col = static_cast<size_t>((std::max)(0LL, start->GetInt("character", 0)));
        }
    }
    return !outLoc.filePath.empty();
}

static bool ParseLocationsResponse(const std::string& respJson, std::vector<Location>& outLocations) {
    outLocations.clear();
    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return false;

    const JsonValue* result = root.Get("result");
    if (!result || result->type == JsonValue::Type::Null) return false;

    if (result->type == JsonValue::Type::Object) {
        Location loc;
        if (ParseLocationEntry(*result, loc)) {
            outLocations.push_back(std::move(loc));
        }
    } else if (result->type == JsonValue::Type::Array) {
        for (const auto& item : result->arrVal) {
            Location loc;
            if (ParseLocationEntry(item, loc)) {
                outLocations.push_back(std::move(loc));
            }
        }
    }
    return !outLocations.empty();
}

static std::string BuildTextDocPosParams(const std::wstring& fileUri, size_t line, size_t col) {
    std::string uri = EnsureFileUriUtf8(fileUri);
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri)
           << "},\"position\":{\"line\":" << line << ",\"character\":" << col << "}}";
    return params.str();
}

bool LspClient::RequestDefinitionLocations(const std::wstring& fileUri, size_t line, size_t col,
                                           std::vector<Location>& outLocations) {
    outLocations.clear();
    if (!m_isRunning.load()) return false;
    std::string respJson;
    if (!SendRequestAndWait("textDocument/definition", BuildTextDocPosParams(fileUri, line, col), respJson, 700)) {
        return false;
    }
    return ParseLocationsResponse(respJson, outLocations);
}

bool LspClient::RequestTypeDefinition(const std::wstring& fileUri, size_t line, size_t col,
                                      std::vector<Location>& outLocations) {
    outLocations.clear();
    if (!m_isRunning.load()) return false;
    std::string respJson;
    if (!SendRequestAndWait("textDocument/typeDefinition", BuildTextDocPosParams(fileUri, line, col), respJson, 700)) {
        return false;
    }
    return ParseLocationsResponse(respJson, outLocations);
}

bool LspClient::RequestImplementation(const std::wstring& fileUri, size_t line, size_t col,
                                      std::vector<Location>& outLocations) {
    outLocations.clear();
    if (!m_isRunning.load()) return false;
    std::string respJson;
    if (!SendRequestAndWait("textDocument/implementation", BuildTextDocPosParams(fileUri, line, col), respJson, 700)) {
        return false;
    }
    return ParseLocationsResponse(respJson, outLocations);
}

bool LspClient::RequestDefinition(const std::wstring& fileUri, size_t line, size_t col, Location& outLocation) {
    outLocation = {};
    std::vector<Location> locs;
    if (!RequestDefinitionLocations(fileUri, line, col, locs) || locs.empty()) {
        return false;
    }
    outLocation = std::move(locs.front());
    return !outLocation.filePath.empty();
}

static bool ParseTextEditArray(const JsonValue* arr, std::vector<LspTextEdit>& outEdits) {
    if (!arr || arr->type != JsonValue::Type::Array || arr->arrVal.empty()) return false;
    for (const auto& el : arr->arrVal) {
        if (el.type != JsonValue::Type::Object) continue;
        const JsonValue* range = el.Get("range");
        if (!range) continue;
        const JsonValue* start = range->Get("start");
        const JsonValue* end = range->Get("end");
        if (!start || !end) continue;

        LspTextEdit te;
        te.startLine = static_cast<size_t>((std::max)(0LL, start->GetInt("line", 0)));
        te.startCol  = static_cast<size_t>((std::max)(0LL, start->GetInt("character", 0)));
        te.endLine   = static_cast<size_t>((std::max)(0LL, end->GetInt("line", 0)));
        te.endCol    = static_cast<size_t>((std::max)(0LL, end->GetInt("character", 0)));
        te.newText   = Utf8ToWide(el.GetString("newText"));
        outEdits.push_back(std::move(te));
    }
    return !outEdits.empty();
}

static bool ParseFormattingEditsResponse(const std::string& respJson, std::vector<LspTextEdit>& outEdits) {
    outEdits.clear();
    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return false;

    return ParseTextEditArray(root.Get("result"), outEdits);
}

static void CollectDocumentSymbolsRecursive(const JsonValue& arr, int depth, std::vector<DocumentSymbolItem>& out) {
    if (arr.type != JsonValue::Type::Array) return;
    for (const auto& el : arr.arrVal) {
        if (el.type != JsonValue::Type::Object) continue;
        DocumentSymbolItem item;
        item.name = Utf8ToWide(el.GetString("name"));
        item.detail = Utf8ToWide(el.GetString("detail"));
        item.kind = static_cast<int>(el.GetInt("kind", 12));
        item.depth = depth;

        const JsonValue* range = el.Get("selectionRange");
        if (!range) range = el.Get("range");
        if (!range) {
            const JsonValue* loc = el.Get("location");
            if (loc) range = loc->Get("range");
        }
        if (range) {
            const JsonValue* start = range->Get("start");
            const JsonValue* end = range->Get("end");
            if (start) {
                item.line = static_cast<size_t>((std::max)(0LL, start->GetInt("line", 0)));
                item.col = static_cast<size_t>((std::max)(0LL, start->GetInt("character", 0)));
            }
            if (end) {
                item.endLine = static_cast<size_t>((std::max)(0LL, end->GetInt("line", 0)));
            }
        }
        if (!item.name.empty()) {
            out.push_back(std::move(item));
        }
        if (const JsonValue* children = el.Get("children")) {
            CollectDocumentSymbolsRecursive(*children, depth + 1, out);
        }
    }
}

bool LspClient::RequestDocumentSymbols(const std::wstring& fileUri, std::vector<DocumentSymbolItem>& outSymbols) {
    outSymbols.clear();
    if (!m_isRunning.load()) return false;
    std::string uri = EnsureFileUriUtf8(fileUri);
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri) << "}}";

    std::string respJson;
    if (!SendRequestAndWait("textDocument/documentSymbol", params.str(), respJson, 700)) {
        return false;
    }

    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return false;
    const JsonValue* result = root.Get("result");
    if (!result || result->type != JsonValue::Type::Array) return false;

    CollectDocumentSymbolsRecursive(*result, 0, outSymbols);
    return !outSymbols.empty();
}

bool LspClient::RequestReferences(const std::wstring& fileUri, size_t line, size_t col,
                                  bool includeDeclaration, std::vector<Location>& outLocations) {
    outLocations.clear();
    if (!m_isRunning.load()) return false;

    std::string uri = EnsureFileUriUtf8(fileUri);
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri)
           << "},\"position\":{\"line\":" << line << ",\"character\":" << col
           << "},\"context\":{\"includeDeclaration\":" << (includeDeclaration ? "true" : "false") << "}}";

    std::string respJson;
    if (!SendRequestAndWait("textDocument/references", params.str(), respJson, 900)) {
        return false;
    }
    return ParseLocationsResponse(respJson, outLocations);
}

bool LspClient::PrepareRename(const std::wstring& fileUri, size_t line, size_t col,
                              size_t& outStartCol, size_t& outEndCol, std::wstring& outPlaceholder) {
    outStartCol = col;
    outEndCol = col;
    outPlaceholder.clear();
    if (!m_isRunning.load()) return false;

    std::string respJson;
    if (!SendRequestAndWait("textDocument/prepareRename", BuildTextDocPosParams(fileUri, line, col), respJson, 600)) {
        return false;
    }
    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return false;
    const JsonValue* result = root.Get("result");
    if (!result || result->type != JsonValue::Type::Object) return false;

    outPlaceholder = Utf8ToWide(result->GetString("placeholder"));
    const JsonValue* range = result->Get("range");
    if (!range) range = result;
    const JsonValue* start = range->Get("start");
    const JsonValue* end = range->Get("end");
    if (start && end) {
        outStartCol = static_cast<size_t>((std::max)(0LL, start->GetInt("character", 0)));
        outEndCol = static_cast<size_t>((std::max)(0LL, end->GetInt("character", 0)));
        return true;
    }
    return !outPlaceholder.empty();
}

bool LspClient::RequestRename(const std::wstring& fileUri, size_t line, size_t col,
                              const std::wstring& newName, WorkspaceEditResult& outEdit) {
    outEdit.fileChanges.clear();
    if (!m_isRunning.load() || newName.empty()) return false;

    std::string uri = EnsureFileUriUtf8(fileUri);
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri)
           << "},\"position\":{\"line\":" << line << ",\"character\":" << col
           << "},\"newName\":" << EscapeJsonString(WideToUtf8(newName)) << "}";

    std::string respJson;
    if (!SendRequestAndWait("textDocument/rename", params.str(), respJson, 1200)) {
        return false;
    }
    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return false;
    const JsonValue* result = root.Get("result");
    if (!result || result->type != JsonValue::Type::Object) return false;

    if (const JsonValue* changes = result->Get("changes"); changes && changes->type == JsonValue::Type::Object) {
        for (const auto& kv : changes->objVal) {
            std::wstring fpath = FileUriToPath(Utf8ToWide(kv.first));
            std::vector<LspTextEdit> edits;
            if (!fpath.empty() && ParseTextEditArray(&kv.second, edits)) {
                outEdit.fileChanges.emplace_back(std::move(fpath), std::move(edits));
            }
        }
    }
    if (outEdit.fileChanges.empty()) {
        if (const JsonValue* docChanges = result->Get("documentChanges");
            docChanges && docChanges->type == JsonValue::Type::Array) {
            for (const auto& dc : docChanges->arrVal) {
                if (dc.type != JsonValue::Type::Object) continue;
                const JsonValue* td = dc.Get("textDocument");
                if (!td) continue;
                std::wstring fpath = FileUriToPath(Utf8ToWide(td->GetString("uri")));
                std::vector<LspTextEdit> edits;
                if (!fpath.empty() && ParseTextEditArray(dc.Get("edits"), edits)) {
                    outEdit.fileChanges.emplace_back(std::move(fpath), std::move(edits));
                }
            }
        }
    }
    return !outEdit.fileChanges.empty();
}

static std::string SerializeCallHierarchyItemJson(const CallHierarchyItem& item) {
    std::string uri = EnsureFileUriUtf8(item.filePath);
    std::ostringstream os;
    os << "{\"name\":" << EscapeJsonString(WideToUtf8(item.name))
       << ",\"kind\":" << item.kind
       << ",\"uri\":" << EscapeJsonString(uri)
       << ",\"range\":{\"start\":{\"line\":" << item.line << ",\"character\":" << item.col
       << "},\"end\":{\"line\":" << item.line << ",\"character\":" << (item.col + 1) << "}}"
       << ",\"selectionRange\":{\"start\":{\"line\":" << item.line << ",\"character\":" << item.col
       << "},\"end\":{\"line\":" << item.line << ",\"character\":" << (item.col + 1) << "}}}";
    return os.str();
}

static CallHierarchyItem ParseCallHierarchyObject(const JsonValue& obj) {
    CallHierarchyItem ch;
    ch.name = Utf8ToWide(obj.GetString("name"));
    ch.detail = Utf8ToWide(obj.GetString("detail"));
    ch.kind = static_cast<int>(obj.GetInt("kind", 12));
    ch.filePath = LspClient::FileUriToPath(Utf8ToWide(obj.GetString("uri")));
    const JsonValue* range = obj.Get("selectionRange");
    if (!range) range = obj.Get("range");
    if (range) {
        if (const JsonValue* start = range->Get("start")) {
            ch.line = static_cast<size_t>((std::max)(0LL, start->GetInt("line", 0)));
            ch.col = static_cast<size_t>((std::max)(0LL, start->GetInt("character", 0)));
        }
    }
    return ch;
}

bool LspClient::PrepareCallHierarchy(const std::wstring& fileUri, size_t line, size_t col,
                                     std::vector<CallHierarchyItem>& outItems) {
    outItems.clear();
    if (!m_isRunning.load()) return false;
    std::string respJson;
    if (!SendRequestAndWait("textDocument/prepareCallHierarchy", BuildTextDocPosParams(fileUri, line, col), respJson, 700)) {
        return false;
    }
    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return false;
    const JsonValue* result = root.Get("result");
    if (!result || result->type != JsonValue::Type::Array) return false;
    for (const auto& el : result->arrVal) {
        if (el.type != JsonValue::Type::Object) continue;
        CallHierarchyItem ch = ParseCallHierarchyObject(el);
        if (!ch.name.empty()) {
            ch.rawItemJson = SerializeCallHierarchyItemJson(ch);
            outItems.push_back(std::move(ch));
        }
    }
    return !outItems.empty();
}

bool LspClient::RequestIncomingCalls(const CallHierarchyItem& item, std::vector<CallHierarchyItem>& outCallers) {
    outCallers.clear();
    if (!m_isRunning.load() || item.name.empty()) return false;
    std::string itemJson = item.rawItemJson.empty() ? SerializeCallHierarchyItemJson(item) : item.rawItemJson;
    std::string params = "{\"item\":" + itemJson + "}";
    std::string respJson;
    if (!SendRequestAndWait("callHierarchy/incomingCalls", params, respJson, 700)) {
        return false;
    }
    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return false;
    const JsonValue* result = root.Get("result");
    if (!result || result->type != JsonValue::Type::Array) return false;
    for (const auto& el : result->arrVal) {
        if (const JsonValue* fromObj = el.Get("from"); fromObj && fromObj->type == JsonValue::Type::Object) {
            CallHierarchyItem caller = ParseCallHierarchyObject(*fromObj);
            if (!caller.name.empty()) outCallers.push_back(std::move(caller));
        }
    }
    return !outCallers.empty();
}

bool LspClient::RequestOutgoingCalls(const CallHierarchyItem& item, std::vector<CallHierarchyItem>& outCallees) {
    outCallees.clear();
    if (!m_isRunning.load() || item.name.empty()) return false;
    std::string itemJson = item.rawItemJson.empty() ? SerializeCallHierarchyItemJson(item) : item.rawItemJson;
    std::string params = "{\"item\":" + itemJson + "}";
    std::string respJson;
    if (!SendRequestAndWait("callHierarchy/outgoingCalls", params, respJson, 700)) {
        return false;
    }
    JsonValue root;
    JsonParser parser(respJson);
    if (!parser.Parse(root)) return false;
    const JsonValue* result = root.Get("result");
    if (!result || result->type != JsonValue::Type::Array) return false;
    for (const auto& el : result->arrVal) {
        if (const JsonValue* toObj = el.Get("to"); toObj && toObj->type == JsonValue::Type::Object) {
            CallHierarchyItem callee = ParseCallHierarchyObject(*toObj);
            if (!callee.name.empty()) outCallees.push_back(std::move(callee));
        }
    }
    return !outCallees.empty();
}

bool LspClient::RequestFormatting(const std::wstring& fileUri, int tabSize, bool insertSpaces, std::vector<LspTextEdit>& outEdits) {
    outEdits.clear();
    if (!m_isRunning.load()) return false;

    std::string uri = EnsureFileUriUtf8(fileUri);
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri)
           << "},\"options\":{\"tabSize\":" << (tabSize > 0 ? tabSize : 4)
           << ",\"insertSpaces\":" << (insertSpaces ? "true" : "false") << "}}";

    std::string respJson;
    if (!SendRequestAndWait("textDocument/formatting", params.str(), respJson, 800)) {
        return false;
    }
    return ParseFormattingEditsResponse(respJson, outEdits);
}

bool LspClient::RequestRangeFormatting(
    const std::wstring& fileUri, size_t startLine, size_t startCol, size_t endLine, size_t endCol,
    int tabSize, bool insertSpaces, std::vector<LspTextEdit>& outEdits)
{
    outEdits.clear();
    if (!m_isRunning.load()) return false;

    std::string uri = EnsureFileUriUtf8(fileUri);
    std::ostringstream params;
    params << "{\"textDocument\":{\"uri\":" << EscapeJsonString(uri)
           << "},\"range\":{\"start\":{\"line\":" << startLine << ",\"character\":" << startCol
           << "},\"end\":{\"line\":" << endLine << ",\"character\":" << endCol
           << "}},\"options\":{\"tabSize\":" << (tabSize > 0 ? tabSize : 4)
           << ",\"insertSpaces\":" << (insertSpaces ? "true" : "false") << "}}";

    std::string respJson;
    if (!SendRequestAndWait("textDocument/rangeFormatting", params.str(), respJson, 800)) {
        return false;
    }
    return ParseFormattingEditsResponse(respJson, outEdits);
}

} // namespace phide::lsp
