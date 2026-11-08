/**
 * @file PluginManager.cpp
 * @brief Implementation of the Unified Extension, Plugin, and Modding Engine for Phide.
 */

#include "phide/plugin/PluginManager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <thread>
#include <cwctype>

namespace phide::plugin {

namespace {

std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::wstring TrimW(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (wlen <= 0) return std::wstring(str.begin(), str.end());
    std::wstring out(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), out.data(), wlen);
    return out;
}

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return "";
    std::string out(ulen, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), out.data(), ulen, nullptr, nullptr);
    return out;
}

std::wstring ReadFileWide(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return L"";
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    return Utf8ToWide(raw);
}

/**
 * @brief Executes a CLI command silently with CREATE_NO_WINDOW, feeding stdinText and capturing stdout.
 */
bool RunSilentProcessWithPipe(
    const std::wstring& cmdLine,
    const std::wstring& workingDir,
    const std::wstring& stdinText,
    std::wstring& outStdout,
    DWORD timeoutMs = 4000
) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hStdInRead = nullptr, hStdInWrite = nullptr;
    HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) return false;
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hStdInRead, &hStdInWrite, &sa, 0)) {
        CloseHandle(hStdOutRead);
        CloseHandle(hStdOutWrite);
        return false;
    }
    SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hStdInRead;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdOutWrite;

    PROCESS_INFORMATION pi{};
    std::wstring fullCmd = L"cmd.exe /c " + cmdLine;
    std::vector<wchar_t> cmdBuf(fullCmd.begin(), fullCmd.end());
    cmdBuf.push_back(L'\0');

    LPCWSTR lpCwd = workingDir.empty() ? nullptr : workingDir.c_str();
    BOOL created = CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        lpCwd,
        &si,
        &pi
    );

    CloseHandle(hStdInRead);
    CloseHandle(hStdOutWrite);

    if (!created) {
        CloseHandle(hStdInWrite);
        CloseHandle(hStdOutRead);
        return false;
    }

    if (!stdinText.empty()) {
        std::string u8In = WideToUtf8(stdinText);
        DWORD written = 0;
        WriteFile(hStdInWrite, u8In.data(), static_cast<DWORD>(u8In.size()), &written, nullptr);
    }
    CloseHandle(hStdInWrite);

    std::string captured;
    DWORD startTick = GetTickCount();
    char buf[4096];
    while (true) {
        DWORD bytesAvail = 0;
        if (PeekNamedPipe(hStdOutRead, nullptr, 0, nullptr, &bytesAvail, nullptr) && bytesAvail > 0) {
            DWORD bytesRead = 0;
            if (ReadFile(hStdOutRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
                captured.append(buf, bytesRead);
                continue;
            }
        }
        DWORD waitRes = WaitForSingleObject(pi.hProcess, 15);
        if (waitRes == WAIT_OBJECT_0) {
            while (PeekNamedPipe(hStdOutRead, nullptr, 0, nullptr, &bytesAvail, nullptr) && bytesAvail > 0) {
                DWORD bytesRead = 0;
                if (ReadFile(hStdOutRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
                    captured.append(buf, bytesRead);
                } else {
                    break;
                }
            }
            break;
        }
        if (GetTickCount() - startTick > timeoutMs) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdOutRead);

    outStdout = Utf8ToWide(captured);
    return (exitCode == 0);
}

// --- Lightweight Recursive JSON Value Parser for Plugin/Mod Manifests ---
struct JsonVal {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::wstring strVal;
    std::vector<JsonVal> arrVal;
    std::vector<std::pair<std::wstring, JsonVal>> objVal;

    const JsonVal* Get(const std::wstring& key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : objVal) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    std::wstring GetString(const std::wstring& key, const std::wstring& def = L"") const {
        const auto* v = Get(key);
        if (!v) return def;
        if (v->type == Type::String) return v->strVal;
        if (v->type == Type::Number) {
            long long iv = static_cast<long long>(v->numVal);
            if (static_cast<double>(iv) == v->numVal) return std::to_wstring(iv);
            return std::to_wstring(v->numVal);
        }
        if (v->type == Type::Bool) return v->boolVal ? L"true" : L"false";
        return def;
    }

    int GetInt(const std::wstring& key, int def = 0) const {
        const auto* v = Get(key);
        if (!v) return def;
        if (v->type == Type::Number) return static_cast<int>(v->numVal);
        if (v->type == Type::String && !v->strVal.empty()) return _wtoi(v->strVal.c_str());
        return def;
    }

    bool GetBool(const std::wstring& key, bool def = false) const {
        const auto* v = Get(key);
        if (!v) return def;
        if (v->type == Type::Bool) return v->boolVal;
        if (v->type == Type::String) return (ToLowerW(v->strVal) == L"true" || v->strVal == L"1");
        if (v->type == Type::Number) return (v->numVal != 0.0);
        return def;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::wstring& src) : m_src(src), m_pos(0) {}

    JsonVal Parse() {
        SkipWhitespaceAndComments();
        return ParseValue();
    }

private:
    const std::wstring& m_src;
    size_t m_pos;

    void SkipWhitespaceAndComments() {
        while (m_pos < m_src.size()) {
            wchar_t c = m_src[m_pos];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
                ++m_pos;
            } else if (c == L'/' && m_pos + 1 < m_src.size() && m_src[m_pos + 1] == L'/') {
                m_pos += 2;
                while (m_pos < m_src.size() && m_src[m_pos] != L'\n') ++m_pos;
            } else if (c == L'/' && m_pos + 1 < m_src.size() && m_src[m_pos + 1] == L'*') {
                m_pos += 2;
                while (m_pos + 1 < m_src.size() && !(m_src[m_pos] == L'*' && m_src[m_pos + 1] == L'/')) ++m_pos;
                if (m_pos + 1 < m_src.size()) m_pos += 2;
            } else {
                break;
            }
        }
    }

    JsonVal ParseValue() {
        SkipWhitespaceAndComments();
        if (m_pos >= m_src.size()) return {};
        wchar_t c = m_src[m_pos];
        if (c == L'{') return ParseObject();
        if (c == L'[') return ParseArray();
        if (c == L'"') {
            JsonVal v;
            v.type = JsonVal::Type::String;
            v.strVal = ParseString();
            return v;
        }
        if (c == L't' || c == L'f') return ParseBool();
        if (c == L'n') return ParseNull();
        if (c == L'-' || (c >= L'0' && c <= L'9')) return ParseNumber();
        return {};
    }

    std::wstring ParseString() {
        if (m_pos >= m_src.size() || m_src[m_pos] != L'"') return L"";
        ++m_pos;
        std::wstring out;
        while (m_pos < m_src.size()) {
            wchar_t c = m_src[m_pos++];
            if (c == L'"') break;
            if (c == L'\\' && m_pos < m_src.size()) {
                wchar_t esc = m_src[m_pos++];
                switch (esc) {
                    case L'"': out.push_back(L'"'); break;
                    case L'\\': out.push_back(L'\\'); break;
                    case L'/': out.push_back(L'/'); break;
                    case L'b': out.push_back(L'\b'); break;
                    case L'f': out.push_back(L'\f'); break;
                    case L'n': out.push_back(L'\n'); break;
                    case L'r': out.push_back(L'\r'); break;
                    case L't': out.push_back(L'\t'); break;
                    case L'u': {
                        if (m_pos + 4 <= m_src.size()) {
                            std::wstring hex = m_src.substr(m_pos, 4);
                            m_pos += 4;
                            wchar_t cp = static_cast<wchar_t>(wcstoul(hex.c_str(), nullptr, 16));
                            out.push_back(cp);
                        }
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    JsonVal ParseObject() {
        JsonVal v;
        v.type = JsonVal::Type::Object;
        if (m_pos < m_src.size() && m_src[m_pos] == L'{') ++m_pos;
        while (m_pos < m_src.size()) {
            SkipWhitespaceAndComments();
            if (m_pos < m_src.size() && m_src[m_pos] == L'}') {
                ++m_pos;
                break;
            }
            if (m_src[m_pos] != L'"') {
                ++m_pos;
                continue;
            }
            std::wstring key = ParseString();
            SkipWhitespaceAndComments();
            if (m_pos < m_src.size() && m_src[m_pos] == L':') ++m_pos;
            JsonVal val = ParseValue();
            v.objVal.push_back({ std::move(key), std::move(val) });
            SkipWhitespaceAndComments();
            if (m_pos < m_src.size() && m_src[m_pos] == L',') ++m_pos;
        }
        return v;
    }

    JsonVal ParseArray() {
        JsonVal v;
        v.type = JsonVal::Type::Array;
        if (m_pos < m_src.size() && m_src[m_pos] == L'[') ++m_pos;
        while (m_pos < m_src.size()) {
            SkipWhitespaceAndComments();
            if (m_pos < m_src.size() && m_src[m_pos] == L']') {
                ++m_pos;
                break;
            }
            size_t before = m_pos;
            v.arrVal.push_back(ParseValue());
            SkipWhitespaceAndComments();
            if (m_pos < m_src.size() && m_src[m_pos] == L',') ++m_pos;
            if (m_pos == before) ++m_pos;
        }
        return v;
    }

    JsonVal ParseBool() {
        JsonVal v;
        v.type = JsonVal::Type::Bool;
        if (m_src.compare(m_pos, 4, L"true") == 0) {
            v.boolVal = true;
            m_pos += 4;
        } else if (m_src.compare(m_pos, 5, L"false") == 0) {
            v.boolVal = false;
            m_pos += 5;
        } else {
            ++m_pos;
        }
        return v;
    }

    JsonVal ParseNull() {
        JsonVal v;
        v.type = JsonVal::Type::Null;
        if (m_src.compare(m_pos, 4, L"null") == 0) m_pos += 4;
        else ++m_pos;
        return v;
    }

    JsonVal ParseNumber() {
        JsonVal v;
        v.type = JsonVal::Type::Number;
        size_t start = m_pos;
        if (m_src[m_pos] == L'-') ++m_pos;
        while (m_pos < m_src.size() && ((m_src[m_pos] >= L'0' && m_src[m_pos] <= L'9') || m_src[m_pos] == L'.' || m_src[m_pos] == L'e' || m_src[m_pos] == L'E' || m_src[m_pos] == L'+' || m_src[m_pos] == L'-')) {
            ++m_pos;
        }
        v.numVal = _wtof(m_src.substr(start, m_pos - start).c_str());
        return v;
    }
};

// --- C-ABI Thunks Routing into PluginManager::Instance() ---
void ABI_RegisterCommand(const wchar_t* id, const wchar_t* title, const wchar_t* shortcut, PhideCommandCallback callback, void* userData) {
    if (!id || !title) return;
    PluginCommand cmd;
    cmd.id = id;
    cmd.title = title;
    if (shortcut) cmd.shortcut = shortcut;
    cmd.nativeCallback = callback;
    cmd.nativeUserData = userData;
    PluginManager::Instance().RegisterCommand(cmd);
}

void ABI_RegisterCompletion(const wchar_t* languageExt, const wchar_t* label, const wchar_t* insertText, const wchar_t* detail, const wchar_t* documentation, int kind, int priority) {
    if (!label) return;
    editor::IntelliSenseItem item;
    item.label = label;
    item.insertText = (insertText && insertText[0]) ? insertText : label;
    if (detail) item.detail = detail;
    if (documentation) item.documentation = documentation;
    if (kind >= 0 && kind <= static_cast<int>(editor::CompletionKind::Color)) {
        item.kind = static_cast<editor::CompletionKind>(kind);
    } else {
        item.kind = editor::CompletionKind::Function;
    }
    item.score = (priority > 0) ? priority : 85;
    item.origin = L"plugin";
    PluginManager::Instance().RegisterCompletionItem(languageExt ? languageExt : L"*", item);
}

void ABI_RegisterSnippet(const wchar_t* prefix, const wchar_t* body, const wchar_t* description, const wchar_t* scope) {
    if (!prefix || !body) return;
    editor::Snippet snip;
    snip.name = (description && description[0]) ? description : prefix;
    snip.prefix = prefix;
    std::wstringstream ss(body);
    std::wstring line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        snip.body.push_back(line);
    }
    if (snip.body.empty()) snip.body.push_back(body);
    if (description) snip.description = description;
    snip.scope = (scope && scope[0]) ? scope : L"*";
    PluginManager::Instance().RegisterSnippet(snip);
}

void ABI_RegisterKeywords(const wchar_t* languageExt, const wchar_t* commaSeparatedWords, bool isType) {
    if (!commaSeparatedWords) return;
    std::vector<std::wstring> words;
    std::wstringstream ss(commaSeparatedWords);
    std::wstring token;
    while (std::getline(ss, token, L',')) {
        std::wstring trimmed = TrimW(token);
        if (!trimmed.empty()) words.push_back(trimmed);
    }
    PluginManager::Instance().RegisterLanguageKeywords(languageExt ? languageExt : L"*", words, isType);
}

void ABI_RegisterFormatter(const wchar_t* languageExt, PhideFormatCallback callback, void* userData) {
    if (!languageExt || !callback) return;
    PluginFormatter fmt;
    fmt.languageExt = languageExt;
    fmt.nativeCallback = callback;
    fmt.nativeUserData = userData;
    PluginManager::Instance().RegisterFormatter(fmt);
}

void ABI_RegisterLinter(const wchar_t* languageExt, PhideLintCallback callback, void* userData) {
    if (!languageExt || !callback) return;
    PluginLinter linter;
    linter.languageExt = languageExt;
    linter.nativeCallback = callback;
    linter.nativeUserData = userData;
    PluginManager::Instance().RegisterLinter(linter);
}

void ABI_RegisterStatusBarItem(const wchar_t* id, const wchar_t* text, const wchar_t* tooltip, const wchar_t* commandId, bool alignRight, int priority) {
    if (!id || !text) return;
    PluginStatusBarItem item;
    item.id = id;
    item.text = text;
    if (tooltip) item.tooltip = tooltip;
    if (commandId) item.commandId = commandId;
    item.alignRight = alignRight;
    item.priority = priority;
    PluginManager::Instance().RegisterStatusBarItem(item);
}

void ABI_UpdateStatusBarItem(const wchar_t* id, const wchar_t* text, const wchar_t* tooltip) {
    if (!id) return;
    PluginManager::Instance().UpdateStatusBarItem(id, text ? text : L"", tooltip ? tooltip : L"");
}

void ABI_RegisterThemeColorOverride(int themeMode, const wchar_t* colorKey, uint32_t rgbaHex) {
    if (!colorKey) return;
    float r = static_cast<float>((rgbaHex >> 24) & 0xFF) / 255.0f;
    float g = static_cast<float>((rgbaHex >> 16) & 0xFF) / 255.0f;
    float b = static_cast<float>((rgbaHex >> 8) & 0xFF) / 255.0f;
    float a = static_cast<float>(rgbaHex & 0xFF) / 255.0f;
    PluginManager::Instance().RegisterThemeColorOverride(themeMode, colorKey, D2D1::ColorF(r, g, b, a));
}

void ABI_RegisterFrameworkDetector(const wchar_t* id, const wchar_t* title, const wchar_t* markerFile, const wchar_t* command, int port, const wchar_t* customUrl) {
    if (!id || !title || !command) return;
    PluginFrameworkDetector det;
    det.id = id;
    det.title = title;
    if (markerFile) det.markerFile = markerFile;
    det.command = command;
    det.port = port;
    if (customUrl) det.customUrl = customUrl;
    PluginManager::Instance().RegisterFrameworkDetector(det);
}

void ABI_RegisterEventHook(const wchar_t* eventName, PhideEventCallback callback, void* userData) {
    if (!eventName || !callback) return;
    PluginEventHook hook;
    hook.eventName = eventName;
    hook.nativeCallback = callback;
    hook.nativeUserData = userData;
    PluginManager::Instance().RegisterEventHook(hook);
}

bool ABI_GetActiveFilePath(wchar_t* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return false;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.getActiveFilePath) return false;
    std::wstring path = cb.getActiveFilePath();
    wcsncpy_s(outBuf, maxLen, path.c_str(), _TRUNCATE);
    return true;
}

bool ABI_GetActiveDocumentText(wchar_t* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return false;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.getActiveDocumentText) return false;
    std::wstring text = cb.getActiveDocumentText();
    wcsncpy_s(outBuf, maxLen, text.c_str(), _TRUNCATE);
    return true;
}

bool ABI_SetActiveDocumentText(const wchar_t* newText) {
    if (!newText) return false;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.setActiveDocumentText) return false;
    return cb.setActiveDocumentText(newText);
}

bool ABI_InsertTextAtCursor(const wchar_t* text) {
    if (!text) return false;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.insertTextAtCursor) return false;
    return cb.insertTextAtCursor(text);
}

bool ABI_GetCursorPosition(size_t* outLine, size_t* outCol) {
    if (!outLine || !outCol) return false;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.getCursorPosition) return false;
    return cb.getCursorPosition(*outLine, *outCol);
}

bool ABI_SetCursorPosition(size_t line, size_t col) {
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.setCursorPosition) return false;
    return cb.setCursorPosition(line, col);
}

bool ABI_OpenFile(const wchar_t* filePath) {
    if (!filePath) return false;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.openFile) return false;
    return cb.openFile(filePath);
}

bool ABI_RunTerminalCommand(const wchar_t* command) {
    if (!command) return false;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.runTerminalCommand) return false;
    return cb.runTerminalCommand(command);
}

void ABI_ShowNotification(const wchar_t* message) {
    if (!message) return;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (cb.showNotification) cb.showNotification(message);
}

bool ABI_GetWorkspaceRoot(wchar_t* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return false;
    const auto& cb = PluginManager::Instance().GetHostCallbacks();
    if (!cb.getWorkspaceRoot) return false;
    std::wstring ws = cb.getWorkspaceRoot();
    wcsncpy_s(outBuf, maxLen, ws.c_str(), _TRUNCATE);
    return true;
}

} // anonymous namespace

PluginManager& PluginManager::Instance() {
    static PluginManager s_instance;
    return s_instance;
}

PluginManager::PluginManager() {
    InitHostApiTable();
}

PluginManager::~PluginManager() {
    Shutdown();
}

void PluginManager::InitHostApiTable() {
    m_hostApi.apiVersion = PHIDE_PLUGIN_API_VERSION;
    m_hostApi.RegisterCommand = ABI_RegisterCommand;
    m_hostApi.RegisterCompletion = ABI_RegisterCompletion;
    m_hostApi.RegisterSnippet = ABI_RegisterSnippet;
    m_hostApi.RegisterKeywords = ABI_RegisterKeywords;
    m_hostApi.RegisterFormatter = ABI_RegisterFormatter;
    m_hostApi.RegisterLinter = ABI_RegisterLinter;
    m_hostApi.RegisterStatusBarItem = ABI_RegisterStatusBarItem;
    m_hostApi.UpdateStatusBarItem = ABI_UpdateStatusBarItem;
    m_hostApi.RegisterThemeColorOverride = ABI_RegisterThemeColorOverride;
    m_hostApi.RegisterFrameworkDetector = ABI_RegisterFrameworkDetector;
    m_hostApi.RegisterEventHook = ABI_RegisterEventHook;
    m_hostApi.GetActiveFilePath = ABI_GetActiveFilePath;
    m_hostApi.GetActiveDocumentText = ABI_GetActiveDocumentText;
    m_hostApi.SetActiveDocumentText = ABI_SetActiveDocumentText;
    m_hostApi.InsertTextAtCursor = ABI_InsertTextAtCursor;
    m_hostApi.GetCursorPosition = ABI_GetCursorPosition;
    m_hostApi.SetCursorPosition = ABI_SetCursorPosition;
    m_hostApi.OpenFile = ABI_OpenFile;
    m_hostApi.RunTerminalCommand = ABI_RunTerminalCommand;
    m_hostApi.ShowNotification = ABI_ShowNotification;
    m_hostApi.GetWorkspaceRoot = ABI_GetWorkspaceRoot;
}

std::wstring PluginManager::NormalizeExt(const std::wstring& ext) {
    std::wstring lower = TrimW(ToLowerW(ext));
    if (lower.empty() || lower == L"*") return L"*";
    if (lower[0] != L'.') lower = L"." + lower;
    return lower;
}

bool PluginManager::ParseHexColor(const std::wstring& hexStr, D2D1_COLOR_F& outColor) {
    std::wstring s = TrimW(hexStr);
    if (s.empty()) return false;
    if (s[0] == L'#') s.erase(0, 1);
    else if (s.rfind(L"0x", 0) == 0 || s.rfind(L"0X", 0) == 0) s.erase(0, 2);

    if (s.size() == 6) {
        unsigned long val = wcstoul(s.c_str(), nullptr, 16);
        float r = static_cast<float>((val >> 16) & 0xFF) / 255.0f;
        float g = static_cast<float>((val >> 8) & 0xFF) / 255.0f;
        float b = static_cast<float>(val & 0xFF) / 255.0f;
        outColor = D2D1::ColorF(r, g, b, 1.0f);
        return true;
    } else if (s.size() == 8) {
        unsigned long val = wcstoul(s.c_str(), nullptr, 16);
        float r = static_cast<float>((val >> 24) & 0xFF) / 255.0f;
        float g = static_cast<float>((val >> 16) & 0xFF) / 255.0f;
        float b = static_cast<float>((val >> 8) & 0xFF) / 255.0f;
        float a = static_cast<float>(val & 0xFF) / 255.0f;
        outColor = D2D1::ColorF(r, g, b, a);
        return true;
    }
    return false;
}

std::vector<std::wstring> PluginManager::ParseShortcutTokens(const std::wstring& shortcut) {
    std::vector<std::wstring> tokens;
    if (shortcut.empty()) return tokens;
    std::wstringstream ss(shortcut);
    std::wstring part;
    while (std::getline(ss, part, L'+')) {
        std::wstring t = TrimW(part);
        if (!t.empty()) tokens.push_back(t);
    }
    return tokens;
}

void PluginManager::SetHostCallbacks(const HostCallbacks& callbacks) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_hostCallbacks = callbacks;
}

void PluginManager::Initialize(const std::wstring& exeDir) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!exeDir.empty()) {
        m_exeDir = exeDir;
    } else if (m_exeDir.empty()) {
        wchar_t szExe[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, szExe, MAX_PATH);
        m_exeDir = std::filesystem::path(szExe).parent_path().wstring();
    }

    // Register built-in Developer Command to hot-reload all plugins, extensions, and mods
    bool hasReloadCmd = false;
    for (const auto& c : m_commands) {
        if (c.id == L"cmd_reload_plugins") {
            hasReloadCmd = true;
            break;
        }
    }
    if (!hasReloadCmd) {
        PluginCommand reloadCmd;
        reloadCmd.id = L"cmd_reload_plugins";
        reloadCmd.title = L"Developer: Reload Plugins, Extensions & Mods";
        reloadCmd.shortcut = L"Ctrl+Alt+R";
        reloadCmd.keyTokens = ParseShortcutTokens(reloadCmd.shortcut);
        reloadCmd.sourcePluginId = L"phide.core.plugin_host";
        reloadCmd.cppCallback = [this]() {
            ReloadAll();
            if (m_hostCallbacks.showNotification) {
                m_hostCallbacks.showNotification(L"✓ Reloaded plugins, extensions & mods");
            }
        };
        m_commands.push_back(std::move(reloadCmd));
    }

    std::filesystem::path exePath(m_exeDir);
    std::filesystem::path rootPath = exePath.parent_path();

    // 1. Scan application-local plugin/extension/mod directories
    static const wchar_t* const subFolders[] = { L"plugins", L"extensions", L"mods" };
    for (const auto* sub : subFolders) {
        ScanDirectoryForPlugins(exePath / sub);
        if (rootPath != exePath) {
            ScanDirectoryForPlugins(rootPath / sub);
        }
    }

    // 2. Scan %LOCALAPPDATA%\phide\{plugins,extensions,mods}
    wchar_t* pLocalAppData = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&pLocalAppData, &len, L"LOCALAPPDATA") == 0 && pLocalAppData) {
        std::filesystem::path localPhide = std::filesystem::path(pLocalAppData) / L"phide";
        free(pLocalAppData);
        for (const auto* sub : subFolders) {
            std::error_code ec;
            std::filesystem::create_directories(localPhide / sub, ec);
            ScanDirectoryForPlugins(localPhide / sub);
        }
        std::error_code ecMod;
        if (std::filesystem::exists(localPhide / L"mods.json", ecMod)) {
            LoadPluginFromManifest(localPhide / L"mods.json");
        }
    }

    // 3. Scan %APPDATA%\phide\{plugins,extensions,mods}
    wchar_t* pAppData = nullptr;
    if (_wdupenv_s(&pAppData, &len, L"APPDATA") == 0 && pAppData) {
        std::filesystem::path roamingPhide = std::filesystem::path(pAppData) / L"phide";
        free(pAppData);
        for (const auto* sub : subFolders) {
            ScanDirectoryForPlugins(roamingPhide / sub);
        }
        std::error_code ecMod;
        if (std::filesystem::exists(roamingPhide / L"mods.json", ecMod)) {
            LoadPluginFromManifest(roamingPhide / L"mods.json");
        }
    }

    m_initialized = true;
}

void PluginManager::LoadWorkspacePlugins(const std::wstring& workspaceRoot) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (workspaceRoot.empty()) return;
    m_workspaceRoot = workspaceRoot;

    std::filesystem::path ws(workspaceRoot);
    std::filesystem::path dotPhide = ws / L".phide";
    static const wchar_t* const subFolders[] = { L"plugins", L"extensions", L"mods" };
    for (const auto* sub : subFolders) {
        ScanDirectoryForPlugins(dotPhide / sub);
    }

    static const wchar_t* const wsFiles[] = {
        L"plugin.json", L"extension.json", L"mod.json", L"mods.json", L"settings.mod.json"
    };
    for (const auto* f : wsFiles) {
        std::error_code ec;
        if (std::filesystem::exists(dotPhide / f, ec)) {
            LoadPluginFromManifest(dotPhide / f);
        }
    }
}

void PluginManager::ReloadAll() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring savedExe = m_exeDir;
    std::wstring savedWs = m_workspaceRoot;
    HostCallbacks savedCb = m_hostCallbacks;
    Shutdown();
    m_hostCallbacks = savedCb;
    Initialize(savedExe);
    if (!savedWs.empty()) {
        LoadWorkspacePlugins(savedWs);
    }
    DispatchEvent(L"onPluginsReloaded");
}

void PluginManager::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& p : m_plugins) {
        if (p.isNativeDll && p.hModule) {
            if (p.shutdownFn) {
                try { p.shutdownFn(); } catch (...) {}
            }
            FreeLibrary(p.hModule);
            p.hModule = nullptr;
        }
    }
    m_plugins.clear();
    m_loadedManifestPaths.clear();
    m_themeOverrides.clear();
    m_configOverrides.clear();
    m_customKeywords.clear();
    m_customTypes.clear();
    m_languageAliases.clear();
    m_completions.clear();
    m_snippets.clear();
    m_formatters.clear();
    m_linters.clear();
    m_commands.clear();
    m_frameworkDetectors.clear();
    m_statusBarItems.clear();
    m_eventHooks.clear();
    m_initialized = false;
}

void PluginManager::ScanDirectoryForPlugins(const std::filesystem::path& dirPath) {
    std::error_code ec;
    if (!std::filesystem::exists(dirPath, ec) || !std::filesystem::is_directory(dirPath, ec)) return;

    static const wchar_t* const manifestNames[] = {
        L"plugin.json", L"extension.json", L"mod.json", L"package.json"
    };

    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (entry.is_directory(ec)) {
            bool foundManifest = false;
            for (const auto* mName : manifestNames) {
                std::filesystem::path cand = entry.path() / mName;
                if (std::filesystem::exists(cand, ec)) {
                    LoadPluginFromManifest(cand);
                    foundManifest = true;
                    break;
                }
            }
            if (!foundManifest) {
                // Check for standalone .dll inside plugin subfolder
                for (const auto& subEntry : std::filesystem::directory_iterator(entry.path(), ec)) {
                    if (subEntry.is_regular_file(ec) && ToLowerW(subEntry.path().extension().wstring()) == L".dll") {
                        LoadNativeDllPlugin(subEntry.path());
                    }
                }
            }
        } else if (entry.is_regular_file(ec)) {
            std::wstring ext = ToLowerW(entry.path().extension().wstring());
            if (ext == L".json") {
                LoadPluginFromManifest(entry.path());
            } else if (ext == L".dll") {
                LoadNativeDllPlugin(entry.path());
            }
        }
    }
}

bool PluginManager::LoadNativeDllPlugin(const std::filesystem::path& dllPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::error_code ec;
    if (!std::filesystem::exists(dllPath, ec)) return false;

    std::wstring canonicalKey = ToLowerW(dllPath.wstring());
    if (m_loadedManifestPaths.count(canonicalKey) > 0) return true;

    HMODULE hMod = LoadLibraryW(dllPath.wstring().c_str());
    if (!hMod) return false;

    auto initFn = reinterpret_cast<PhidePluginInitFn>(GetProcAddress(hMod, "PhidePluginInitialize"));
    auto shutdownFn = reinterpret_cast<PhidePluginShutdownFn>(GetProcAddress(hMod, "PhidePluginShutdown"));
    if (!initFn) {
        FreeLibrary(hMod);
        return false;
    }

    PhidePluginInfo info{};
    info.apiVersion = PHIDE_PLUGIN_API_VERSION;
    bool ok = false;
    try {
        ok = initFn(&m_hostApi, &info);
    } catch (...) {
        ok = false;
    }

    if (!ok) {
        FreeLibrary(hMod);
        return false;
    }

    m_loadedManifestPaths.insert(canonicalKey);

    PluginMetadata meta;
    meta.id = (info.id && info.id[0]) ? info.id : dllPath.stem().wstring();
    meta.name = (info.name && info.name[0]) ? info.name : meta.id;
    meta.version = (info.version && info.version[0]) ? info.version : L"1.0.0";
    if (info.author) meta.author = info.author;
    if (info.description) meta.description = info.description;
    meta.rootDir = dllPath.parent_path().wstring();
    meta.mainDllPath = dllPath.wstring();
    meta.isNativeDll = true;
    meta.hModule = hMod;
    meta.shutdownFn = shutdownFn;
    m_plugins.push_back(std::move(meta));
    return true;
}

bool PluginManager::LoadPluginFromManifest(const std::filesystem::path& manifestPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath, ec)) return false;

    std::wstring canonicalKey = ToLowerW(manifestPath.wstring());
    if (m_loadedManifestPaths.count(canonicalKey) > 0) return true;

    std::wstring jsonText = ReadFileWide(manifestPath);
    if (jsonText.empty()) return false;

    if (LoadPluginFromJsonString(jsonText, manifestPath.parent_path().wstring())) {
        m_loadedManifestPaths.insert(canonicalKey);
        return true;
    }
    return false;
}

bool PluginManager::LoadPluginFromJsonString(const std::wstring& jsonContent, const std::wstring& baseDir) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (jsonContent.empty()) return false;

    JsonParser parser(jsonContent);
    JsonVal root = parser.Parse();
    if (root.type != JsonVal::Type::Object) return false;

    if (!root.GetBool(L"enabled", true)) return false;

    PluginMetadata meta;
    meta.id = root.GetString(L"id", root.GetString(L"name", L"custom.plugin"));
    meta.name = root.GetString(L"displayName", root.GetString(L"name", meta.id));
    meta.version = root.GetString(L"version", L"1.0.0");
    meta.author = root.GetString(L"author", root.GetString(L"publisher", L""));
    meta.description = root.GetString(L"description", L"");
    meta.rootDir = baseDir;

    // Support both top-level fields and VS Code-style "contributes" object
    const JsonVal* contrib = root.Get(L"contributes");
    auto getSection = [&](const std::wstring& key) -> const JsonVal* {
        if (const auto* top = root.Get(key)) return top;
        if (contrib) {
            if (const auto* sub = contrib->Get(key)) return sub;
        }
        return nullptr;
    };

    // 1. Optional Native DLL entrypoint ("main" or "nativeDll")
    std::wstring dllRel = root.GetString(L"nativeDll", root.GetString(L"main", L""));
    if (!dllRel.empty() && ToLowerW(dllRel).ends_with(L".dll") && !baseDir.empty()) {
        std::filesystem::path fullDll = std::filesystem::path(baseDir) / dllRel;
        LoadNativeDllPlugin(fullDll);
    }

    // 2. Theme Color Overrides ("colors", "themeColors", or "theme": { "colors": {...} })
    const JsonVal* colorsNode = getSection(L"colors");
    int defaultThemeMode = root.GetInt(L"themeMode", -1);
    if (!colorsNode) colorsNode = getSection(L"themeColors");
    if (!colorsNode) {
        if (const JsonVal* themeNode = getSection(L"theme")) {
            if (themeNode->type == JsonVal::Type::Object) {
                defaultThemeMode = themeNode->GetInt(L"mode", defaultThemeMode);
                colorsNode = themeNode->Get(L"colors");
            }
        }
    }
    if (colorsNode) {
        if (colorsNode->type == JsonVal::Type::Object) {
            int targetMode = defaultThemeMode;
            for (const auto& [colorKey, colorVal] : colorsNode->objVal) {
                if (colorVal.type == JsonVal::Type::String) {
                    D2D1_COLOR_F parsedColor{};
                    if (ParseHexColor(colorVal.strVal, parsedColor)) {
                        RegisterThemeColorOverride(targetMode, colorKey, parsedColor);
                    }
                } else if (colorVal.type == JsonVal::Type::Object) {
                    // Per-theme map e.g. "0": "#121212"
                    for (const auto& [k, v] : colorVal.objVal) {
                        D2D1_COLOR_F parsedColor{};
                        if (v.type == JsonVal::Type::String && ParseHexColor(v.strVal, parsedColor)) {
                            int mode = (k == L"*" || k == L"all") ? -1 : _wtoi(k.c_str());
                            RegisterThemeColorOverride(mode, colorKey, parsedColor);
                        }
                    }
                }
            }
        }
    }

    // 3. AppConfig & UI Geometry / Settings Modding ("config" or "settings" or "mods")
    const JsonVal* cfgNode = getSection(L"config");
    if (!cfgNode) cfgNode = getSection(L"settings");
    if (!cfgNode) cfgNode = getSection(L"mods");
    if (cfgNode && cfgNode->type == JsonVal::Type::Object) {
        for (const auto& [cfgKey, cfgVal] : cfgNode->objVal) {
            m_configOverrides[cfgKey] = cfgNode->GetString(cfgKey);
        }
    }

    // 4. Custom Languages, Keywords, and Types ("languages" / "keywords")
    if (const JsonVal* langsNode = getSection(L"languages")) {
        if (langsNode->type == JsonVal::Type::Array) {
            for (const auto& langObj : langsNode->arrVal) {
                if (langObj.type != JsonVal::Type::Object) continue;
                std::vector<std::wstring> exts;
                if (const auto* extArr = langObj.Get(L"extensions")) {
                    if (extArr->type == JsonVal::Type::Array) {
                        for (const auto& e : extArr->arrVal) {
                            if (e.type == JsonVal::Type::String && !e.strVal.empty()) {
                                exts.push_back(NormalizeExt(e.strVal));
                            }
                        }
                    }
                }
                std::wstring singleExt = langObj.GetString(L"extension");
                if (!singleExt.empty()) exts.push_back(NormalizeExt(singleExt));
                if (exts.empty()) exts.push_back(L"*");

                std::wstring aliasTarget = langObj.GetString(L"baseLanguage");
                if (!aliasTarget.empty()) {
                    for (const auto& e : exts) {
                        m_languageAliases[e] = NormalizeExt(aliasTarget);
                    }
                }

                auto parseWordList = [](const JsonVal* node, std::vector<std::wstring>& outList) {
                    if (!node) return;
                    if (node->type == JsonVal::Type::Array) {
                        for (const auto& kw : node->arrVal) {
                            if (kw.type == JsonVal::Type::String && !kw.strVal.empty()) {
                                outList.push_back(kw.strVal);
                            }
                        }
                    } else if (node->type == JsonVal::Type::String) {
                        std::wstringstream ss(node->strVal);
                        std::wstring tok;
                        while (std::getline(ss, tok, L',')) {
                            std::wstring t = TrimW(tok);
                            if (!t.empty()) outList.push_back(t);
                        }
                    }
                };
                std::vector<std::wstring> kwList;
                parseWordList(langObj.Get(L"keywords"), kwList);
                std::vector<std::wstring> typeList;
                parseWordList(langObj.Get(L"types"), typeList);

                for (const auto& e : exts) {
                    if (!kwList.empty()) RegisterLanguageKeywords(e, kwList, false);
                    if (!typeList.empty()) RegisterLanguageKeywords(e, typeList, true);
                }
            }
        }
    }

    // 5. Custom IntelliSense Completions ("completions")
    if (const JsonVal* compNode = getSection(L"completions")) {
        if (compNode->type == JsonVal::Type::Array) {
            for (const auto& cObj : compNode->arrVal) {
                if (cObj.type != JsonVal::Type::Object) continue;
                editor::IntelliSenseItem item;
                item.label = cObj.GetString(L"label");
                if (item.label.empty()) continue;
                item.insertText = cObj.GetString(L"insertText", item.label);
                item.detail = cObj.GetString(L"detail", meta.name);
                item.documentation = cObj.GetString(L"documentation");
                item.origin = meta.id;
                std::wstring kindStr = ToLowerW(cObj.GetString(L"kind", L"function"));
                if (kindStr == L"keyword") item.kind = editor::CompletionKind::Keyword;
                else if (kindStr == L"variable") item.kind = editor::CompletionKind::Variable;
                else if (kindStr == L"class" || kindStr == L"type") item.kind = editor::CompletionKind::Class;
                else if (kindStr == L"snippet") item.kind = editor::CompletionKind::Snippet;
                else if (kindStr == L"property") item.kind = editor::CompletionKind::Property;
                else if (kindStr == L"method") item.kind = editor::CompletionKind::Method;
                else item.kind = editor::CompletionKind::Function;
                item.score = cObj.GetInt(L"priority", 85);

                std::wstring ext = NormalizeExt(cObj.GetString(L"language", cObj.GetString(L"extension", L"*")));
                RegisterCompletionItem(ext, item);
            }
        }
    }

    // 6. Custom Snippets ("snippets")
    auto parseSnippetBody = [](const JsonVal* bodyVal, std::vector<std::wstring>& outLines) {
        if (!bodyVal) return;
        if (bodyVal->type == JsonVal::Type::String) {
            std::wstringstream ss(bodyVal->strVal);
            std::wstring l;
            while (std::getline(ss, l)) {
                if (!l.empty() && l.back() == L'\r') l.pop_back();
                outLines.push_back(l);
            }
            if (outLines.empty()) outLines.push_back(bodyVal->strVal);
        } else if (bodyVal->type == JsonVal::Type::Array) {
            for (const auto& elem : bodyVal->arrVal) {
                if (elem.type == JsonVal::Type::String) {
                    outLines.push_back(elem.strVal);
                }
            }
        }
    };
    if (const JsonVal* snipNode = getSection(L"snippets")) {
        if (snipNode->type == JsonVal::Type::Array) {
            for (const auto& sObj : snipNode->arrVal) {
                if (sObj.type != JsonVal::Type::Object) continue;
                editor::Snippet snip;
                snip.prefix = sObj.GetString(L"prefix");
                if (snip.prefix.empty()) continue;
                snip.name = sObj.GetString(L"name", sObj.GetString(L"description", snip.prefix));
                parseSnippetBody(sObj.Get(L"body"), snip.body);
                snip.description = sObj.GetString(L"description", meta.name);
                snip.scope = sObj.GetString(L"scope", L"");
                if (snip.scope == L"*") snip.scope.clear();
                RegisterSnippet(snip);
            }
        } else if (snipNode->type == JsonVal::Type::Object) {
            for (const auto& [sName, sObj] : snipNode->objVal) {
                if (sObj.type != JsonVal::Type::Object) continue;
                editor::Snippet snip;
                snip.name = sName;
                snip.prefix = sObj.GetString(L"prefix");
                if (snip.prefix.empty()) continue;
                parseSnippetBody(sObj.Get(L"body"), snip.body);
                snip.description = sObj.GetString(L"description", sName);
                snip.scope = sObj.GetString(L"scope", L"");
                if (snip.scope == L"*") snip.scope.clear();
                RegisterSnippet(snip);
            }
        }
    }

    // 7. Custom Formatters ("formatters")
    if (const JsonVal* fmtNode = getSection(L"formatters")) {
        if (fmtNode->type == JsonVal::Type::Array) {
            for (const auto& fObj : fmtNode->arrVal) {
                if (fObj.type != JsonVal::Type::Object) continue;
                PluginFormatter fmt;
                fmt.languageExt = NormalizeExt(fObj.GetString(L"extension", fObj.GetString(L"language", L"*")));
                fmt.cliCommand = fObj.GetString(L"command");
                fmt.useStdin = fObj.GetBool(L"stdin", true);
                fmt.sourcePluginId = meta.id;
                if (!fmt.cliCommand.empty()) {
                    RegisterFormatter(fmt);
                }
            }
        }
    }

    // 8. Custom Linters & Regex Diagnostics ("linters" / "diagnostics")
    const JsonVal* lintNode = getSection(L"linters");
    if (!lintNode) lintNode = getSection(L"diagnostics");
    if (lintNode && lintNode->type == JsonVal::Type::Array) {
        for (const auto& lObj : lintNode->arrVal) {
            if (lObj.type != JsonVal::Type::Object) continue;
            PluginLinter linter;
            linter.languageExt = NormalizeExt(lObj.GetString(L"extension", lObj.GetString(L"language", L"*")));
            linter.cliCommand = lObj.GetString(L"command");
            linter.sourcePluginId = meta.id;

            // Direct regex rule on the linter object or "rules" array
            std::wstring directPattern = lObj.GetString(L"pattern");
            if (!directPattern.empty()) {
                PluginRegexLintRule rule;
                rule.pattern = directPattern;
                rule.message = lObj.GetString(L"message", L"Plugin diagnostic warning");
                std::wstring sev = ToLowerW(lObj.GetString(L"severity", L"warning"));
                if (sev == L"error") rule.severity = 1;
                else if (sev == L"info") rule.severity = 3;
                else rule.severity = 2;
                linter.regexRules.push_back(std::move(rule));
            }
            if (const auto* rulesArr = lObj.Get(L"rules")) {
                if (rulesArr->type == JsonVal::Type::Array) {
                    for (const auto& rObj : rulesArr->arrVal) {
                        if (rObj.type != JsonVal::Type::Object) continue;
                        PluginRegexLintRule rule;
                        rule.pattern = rObj.GetString(L"pattern");
                        if (rule.pattern.empty()) continue;
                        rule.message = rObj.GetString(L"message", L"Plugin diagnostic");
                        std::wstring sev = ToLowerW(rObj.GetString(L"severity", L"warning"));
                        if (sev == L"error") rule.severity = 1;
                        else if (sev == L"info") rule.severity = 3;
                        else rule.severity = 2;
                        linter.regexRules.push_back(std::move(rule));
                    }
                }
            }
            if (!linter.cliCommand.empty() || !linter.regexRules.empty()) {
                RegisterLinter(linter);
            }
        }
    }

    // 9. Custom Commands & Keybindings ("commands")
    if (const JsonVal* cmdNode = getSection(L"commands")) {
        if (cmdNode->type == JsonVal::Type::Array) {
            for (const auto& cObj : cmdNode->arrVal) {
                if (cObj.type != JsonVal::Type::Object) continue;
                PluginCommand cmd;
                cmd.id = cObj.GetString(L"id", cObj.GetString(L"command"));
                cmd.title = cObj.GetString(L"title");
                if (cmd.id.empty() || cmd.title.empty()) continue;
                cmd.shortcut = cObj.GetString(L"shortcut", cObj.GetString(L"key"));
                cmd.keyTokens = ParseShortcutTokens(cmd.shortcut);
                cmd.when = cObj.GetString(L"when", L"global");
                cmd.shellCommand = cObj.GetString(L"exec", cObj.GetString(L"shellCommand"));
                cmd.insertText = cObj.GetString(L"insertText");
                cmd.runInTerminal = cObj.GetBool(L"runInTerminal", false);
                cmd.replaceDocumentWithStdout = cObj.GetBool(L"replaceDocument", false);
                cmd.sourcePluginId = meta.id;
                RegisterCommand(cmd);
            }
        }
    }

    // 10. Custom Framework Detectors & Runners ("frameworks" / "runners")
    const JsonVal* fwNode = getSection(L"frameworks");
    if (!fwNode) fwNode = getSection(L"runners");
    if (fwNode && fwNode->type == JsonVal::Type::Array) {
        for (const auto& fwObj : fwNode->arrVal) {
            if (fwObj.type != JsonVal::Type::Object) continue;
            PluginFrameworkDetector det;
            det.id = fwObj.GetString(L"id", meta.id);
            det.title = fwObj.GetString(L"title");
            det.markerFile = fwObj.GetString(L"markerFile");
            det.markerContains = fwObj.GetString(L"markerContains");
            det.fileExtension = NormalizeExt(fwObj.GetString(L"extension", L""));
            if (det.fileExtension == L"*") det.fileExtension.clear();
            det.command = fwObj.GetString(L"command");
            det.port = fwObj.GetInt(L"port", 0);
            det.customUrl = fwObj.GetString(L"url");
            if (!det.title.empty() && !det.command.empty()) {
                RegisterFrameworkDetector(det);
            }
        }
    }

    // 11. Custom Status Bar Items ("statusBar")
    if (const JsonVal* sbNode = getSection(L"statusBar")) {
        if (sbNode->type == JsonVal::Type::Array) {
            for (const auto& sbObj : sbNode->arrVal) {
                if (sbObj.type != JsonVal::Type::Object) continue;
                PluginStatusBarItem item;
                item.id = sbObj.GetString(L"id", meta.id + L".statusbar");
                item.text = sbObj.GetString(L"text");
                if (item.text.empty()) continue;
                item.tooltip = sbObj.GetString(L"tooltip", meta.name);
                item.commandId = sbObj.GetString(L"command");
                std::wstring align = ToLowerW(sbObj.GetString(L"alignment", L"right"));
                item.alignRight = (align != L"left");
                item.priority = sbObj.GetInt(L"priority", 0);
                RegisterStatusBarItem(item);
            }
        }
    }

    // 12. Lifecycle Event Hooks ("hooks" / "events")
    const JsonVal* hooksNode = getSection(L"hooks");
    if (!hooksNode) hooksNode = getSection(L"events");
    if (hooksNode && hooksNode->type == JsonVal::Type::Object) {
        for (const auto& [evName, evVal] : hooksNode->objVal) {
            if (evVal.type == JsonVal::Type::String && !evVal.strVal.empty()) {
                PluginEventHook hook;
                hook.eventName = evName;
                hook.shellCommand = evVal.strVal;
                RegisterEventHook(hook);
            }
        }
    }

    m_plugins.push_back(std::move(meta));
    return true;
}

// =============================================================================
// 1. Theme & UI Modding
// =============================================================================

void PluginManager::RegisterThemeColorOverride(int themeMode, const std::wstring& colorKey, D2D1_COLOR_F color) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_themeOverrides[themeMode][ToLowerW(TrimW(colorKey))] = color;
}

void PluginManager::ApplyThemeOverrides(ui::ThemeColors& colors, int currentThemeMode) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_themeOverrides.empty()) return;

    auto applyMap = [&](const std::unordered_map<std::wstring, D2D1_COLOR_F>& cmap) {
        for (const auto& [k, c] : cmap) {
            if (k == L"windowbg") colors.windowBg = c;
            else if (k == L"canvasbg") colors.canvasBg = c;
            else if (k == L"headerbg") colors.headerBg = c;
            else if (k == L"railbg") colors.railBg = c;
            else if (k == L"drawerbg") colors.drawerBg = c;
            else if (k == L"bordersubtle") colors.borderSubtle = c;
            else if (k == L"borderhover") colors.borderHover = c;
            else if (k == L"textprimary") colors.textPrimary = c;
            else if (k == L"textsecondary") colors.textSecondary = c;
            else if (k == L"textmuted") colors.textMuted = c;
            else if (k == L"textwhite") colors.textWhite = c;
            else if (k == L"itemhover") colors.itemHover = c;
            else if (k == L"itemactive") colors.itemActive = c;
            else if (k == L"treeselected") colors.treeSelected = c;
            else if (k == L"brandaccent" || k == L"accentprimary") colors.brandAccent = c;
            else if (k == L"rungreen") colors.runGreen = c;
            else if (k == L"tooltipbg") colors.tooltipBg = c;
            else if (k == L"tooltipborder") colors.tooltipBorder = c;
            else if (k == L"tooltiptext") colors.tooltipText = c;
            else if (k == L"menubg") colors.menuBg = c;
            else if (k == L"menuborder") colors.menuBorder = c;
            else if (k == L"menuhover") colors.menuHover = c;
            else if (k == L"menutext") colors.menuText = c;
            else if (k == L"tabbarbg") colors.tabBarBg = c;
            else if (k == L"tabactivebg") colors.tabActiveBg = c;
            else if (k == L"tabborder") colors.tabBorder = c;
            else if (k == L"currentlinebg") colors.currentLineBg = c;
            else if (k == L"linenumber") colors.lineNumber = c;
            else if (k == L"syntaxkeyword") colors.syntaxKeyword = c;
            else if (k == L"syntaxtype") colors.syntaxType = c;
            else if (k == L"syntaxstring") colors.syntaxString = c;
            else if (k == L"syntaxcomment") colors.syntaxComment = c;
            else if (k == L"syntaxnumber") colors.syntaxNumber = c;
            else if (k == L"syntaxoperator") colors.syntaxOperator = c;
            else if (k == L"syntaxtag") colors.syntaxTag = c;
            else if (k == L"syntaxattribute") colors.syntaxAttribute = c;
            else if (k == L"bracket1") colors.bracket1 = c;
            else if (k == L"bracket2") colors.bracket2 = c;
            else if (k == L"bracket3") colors.bracket3 = c;
        }
    };

    auto itGlobal = m_themeOverrides.find(-1);
    if (itGlobal != m_themeOverrides.end()) {
        applyMap(itGlobal->second);
    }
    auto itSpecific = m_themeOverrides.find(currentThemeMode);
    if (itSpecific != m_themeOverrides.end()) {
        applyMap(itSpecific->second);
    }
}

void PluginManager::ApplyAppConfigMods(app::AppConfig& config) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& [kRaw, val] : m_configOverrides) {
        std::wstring k = ToLowerW(kRaw);
        bool bVal = (ToLowerW(val) == L"true" || val == L"1");
        if (k == L"headerheight") config.headerHeight = (std::max)(28, _wtoi(val.c_str()));
        else if (k == L"railwidth") config.railWidth = (std::max)(36, _wtoi(val.c_str()));
        else if (k == L"drawerwidth") config.drawerWidth = (std::max)(180, _wtoi(val.c_str()));
        else if (k == L"rightsidebarwidth") config.rightSidebarWidth = (std::max)(200, _wtoi(val.c_str()));
        else if (k == L"fontsize") {
            float f = static_cast<float>(_wtof(val.c_str()));
            if (f >= 8.0f && f <= 42.0f) config.fontSize = f;
        }
        else if (k == L"lineheightmultiplier") {
            float f = static_cast<float>(_wtof(val.c_str()));
            if (f >= 1.0f && f <= 2.5f) config.lineHeightMultiplier = f;
        }
        else if (k == L"fontfamily" && !val.empty()) config.fontFamily = val;
        else if (k == L"tabsize") config.tabSize = (std::max)(1, _wtoi(val.c_str()));
        else if (k == L"insertspaces") config.insertSpaces = bVal;
        else if (k == L"cursorstyle" && !val.empty()) config.cursorStyle = val;
        else if (k == L"formatonsave") config.formatOnSave = bVal;
        else if (k == L"showminimap") config.showMinimap = bVal;
        else if (k == L"showbreadcrumbs") config.showBreadcrumbs = bVal;
        else if (k == L"showlinenumbers") config.showLineNumbers = bVal;
        else if (k == L"showstatusbar") config.showStatusBar = bVal;
        else if (k == L"enablecodelens") config.enableCodeLens = bVal;
        else if (k == L"enablegitlens") config.enableGitLens = bVal;
        else if (k == L"wordwrap") config.wordWrap = bVal;
    }
}

// =============================================================================
// 2. Syntax Lexer & Custom Language Modding
// =============================================================================

void PluginManager::RegisterLanguageKeywords(const std::wstring& ext, const std::vector<std::wstring>& words, bool isType) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring norm = NormalizeExt(ext);
    auto& targetSet = isType ? m_customTypes[norm] : m_customKeywords[norm];
    for (const auto& w : words) {
        if (!w.empty()) targetSet.insert(w);
    }
}

bool PluginManager::IsCustomKeyword(const std::wstring& ext, const std::wstring& word) const {
    if (word.empty()) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_customKeywords.empty()) return false;

    std::wstring norm = NormalizeExt(ext);
    auto it = m_customKeywords.find(norm);
    if (it != m_customKeywords.end() && it->second.count(word) > 0) return true;

    auto itWild = m_customKeywords.find(L"*");
    if (itWild != m_customKeywords.end() && itWild->second.count(word) > 0) return true;
    return false;
}

bool PluginManager::IsCustomType(const std::wstring& ext, const std::wstring& word) const {
    if (word.empty()) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_customTypes.empty()) return false;

    std::wstring norm = NormalizeExt(ext);
    auto it = m_customTypes.find(norm);
    if (it != m_customTypes.end() && it->second.count(word) > 0) return true;

    auto itWild = m_customTypes.find(L"*");
    if (itWild != m_customTypes.end() && itWild->second.count(word) > 0) return true;
    return false;
}

std::wstring PluginManager::ResolveLanguageAlias(const std::wstring& ext) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_languageAliases.empty()) return ext;
    std::wstring norm = NormalizeExt(ext);
    auto it = m_languageAliases.find(norm);
    if (it != m_languageAliases.end()) return it->second;
    return ext;
}

// =============================================================================
// 3. IntelliSense & Autocomplete
// =============================================================================

void PluginManager::RegisterCompletionItem(const std::wstring& ext, const editor::IntelliSenseItem& item) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring norm = NormalizeExt(ext);
    m_completions[norm].push_back(item);
}

std::vector<editor::IntelliSenseItem> PluginManager::QueryCompletions(
    const std::wstring& ext,
    const std::wstring& fileName,
    const std::wstring& currentLine,
    size_t cursorCol,
    const std::wstring& prefix
) const {
    (void)fileName;
    (void)currentLine;
    (void)cursorCol;
    (void)prefix;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<editor::IntelliSenseItem> out;
    if (m_completions.empty()) return out;

    std::wstring norm = NormalizeExt(ext);
    auto it = m_completions.find(norm);
    if (it != m_completions.end()) {
        out.insert(out.end(), it->second.begin(), it->second.end());
    }
    auto itWild = m_completions.find(L"*");
    if (itWild != m_completions.end()) {
        out.insert(out.end(), itWild->second.begin(), itWild->second.end());
    }
    return out;
}

// =============================================================================
// 4. Snippets
// =============================================================================

void PluginManager::RegisterSnippet(const editor::Snippet& snippet) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_snippets.push_back(snippet);
}

std::vector<editor::Snippet> PluginManager::GetContributedSnippets() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_snippets;
}

// =============================================================================
// 5. Formatters
// =============================================================================

void PluginManager::RegisterFormatter(const PluginFormatter& formatter) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring norm = NormalizeExt(formatter.languageExt);
    m_formatters[norm].push_back(formatter);
}

bool PluginManager::CanFormat(const std::wstring& ext) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_formatters.empty()) return false;
    std::wstring norm = NormalizeExt(ext);
    return (m_formatters.count(norm) > 0 || m_formatters.count(L"*") > 0);
}

bool PluginManager::TryFormat(const std::wstring& src, const std::wstring& ext, int tabSize, std::wstring& outFormatted) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_formatters.empty()) return false;

    std::wstring norm = NormalizeExt(ext);
    auto runList = [&](const std::vector<PluginFormatter>& list) -> bool {
        for (const auto& fmt : list) {
            if (fmt.cppCallback) {
                std::wstring res = fmt.cppCallback(src, tabSize);
                if (!res.empty()) {
                    outFormatted = std::move(res);
                    return true;
                }
            }
            if (fmt.nativeCallback) {
                size_t cap = (std::max)(src.size() * 2 + 1024, static_cast<size_t>(16384));
                std::vector<wchar_t> buf(cap, 0);
                if (fmt.nativeCallback(norm.c_str(), src.c_str(), tabSize, buf.data(), cap, fmt.nativeUserData)) {
                    outFormatted = buf.data();
                    return true;
                }
            }
            if (!fmt.cliCommand.empty()) {
                std::wstring cmd = ExpandVariables(fmt.cliCommand);
                std::wstring stdoutText;
                if (RunSilentProcessWithPipe(cmd, m_workspaceRoot, fmt.useStdin ? src : L"", stdoutText, 4000)) {
                    if (!stdoutText.empty()) {
                        outFormatted = std::move(stdoutText);
                        return true;
                    }
                }
            }
        }
        return false;
    };

    auto it = m_formatters.find(norm);
    if (it != m_formatters.end() && runList(it->second)) return true;

    auto itWild = m_formatters.find(L"*");
    if (itWild != m_formatters.end() && runList(itWild->second)) return true;

    return false;
}

// =============================================================================
// 6. Diagnostics & Linters
// =============================================================================

void PluginManager::RegisterLinter(const PluginLinter& linter) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring norm = NormalizeExt(linter.languageExt);
    m_linters[norm].push_back(linter);
}

std::vector<lsp::Diagnostic> PluginManager::RunDiagnostics(
    const std::wstring& filePath,
    const std::wstring& ext,
    const editor::Buffer& buffer
) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<lsp::Diagnostic> results;
    if (m_linters.empty()) return results;

    std::wstring norm = NormalizeExt(ext);
    const auto& lines = buffer.GetLines();

    auto runLinterList = [&](const std::vector<PluginLinter>& list) {
        for (const auto& linter : list) {
            if (linter.cppCallback) {
                auto sub = linter.cppCallback(filePath, buffer);
                results.insert(results.end(), sub.begin(), sub.end());
            }
            if (linter.nativeCallback) {
                std::wstring fullText = buffer.GetText();
                std::vector<PhideDiagnosticEntry> entries(64);
                size_t count = linter.nativeCallback(
                    filePath.c_str(), norm.c_str(), fullText.c_str(),
                    entries.data(), entries.size(), linter.nativeUserData
                );
                for (size_t i = 0; i < count && i < entries.size(); ++i) {
                    lsp::Diagnostic d;
                    d.line = entries[i].line;
                    d.col = entries[i].col;
                    d.length = (std::max)(static_cast<size_t>(1), entries[i].length);
                    d.message = entries[i].message;
                    if (entries[i].severity == 0) d.severity = 1; // Error
                    else if (entries[i].severity == 2) d.severity = 3; // Info
                    else d.severity = 2; // Warning
                    results.push_back(std::move(d));
                }
            }
            for (const auto& rule : linter.regexRules) {
                try {
                    std::wregex re(rule.pattern);
                    for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
                        std::wsmatch m;
                        const std::wstring& lineStr = lines[lineIdx];
                        if (std::regex_search(lineStr, m, re)) {
                            lsp::Diagnostic d;
                            d.line = lineIdx;
                            d.col = static_cast<size_t>(m.position(0));
                            d.length = (std::max)(static_cast<size_t>(1), static_cast<size_t>(m.length(0)));
                            d.message = rule.message;
                            d.severity = rule.severity;
                            results.push_back(std::move(d));
                        }
                    }
                } catch (...) {}
            }
        }
    };

    auto it = m_linters.find(norm);
    if (it != m_linters.end()) runLinterList(it->second);

    auto itWild = m_linters.find(L"*");
    if (itWild != m_linters.end()) runLinterList(itWild->second);

    return results;
}

// =============================================================================
// 7. Commands, Keybindings & Command Palette
// =============================================================================

void PluginManager::RegisterCommand(const PluginCommand& cmd) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    PluginCommand copy = cmd;
    if (copy.keyTokens.empty() && !copy.shortcut.empty()) {
        copy.keyTokens = ParseShortcutTokens(copy.shortcut);
    }
    for (auto& existing : m_commands) {
        if (existing.id == copy.id) {
            existing = std::move(copy);
            return;
        }
    }
    m_commands.push_back(std::move(copy));
}

std::vector<PluginCommand> PluginManager::GetCommands() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_commands;
}

std::wstring PluginManager::ExpandVariables(const std::wstring& templateStr) const {
    std::wstring out = templateStr;
    std::wstring activeFile = m_hostCallbacks.getActiveFilePath ? m_hostCallbacks.getActiveFilePath() : L"";
    std::wstring wsRoot = m_hostCallbacks.getWorkspaceRoot ? m_hostCallbacks.getWorkspaceRoot() : m_workspaceRoot;
    size_t line = 0, col = 0;
    if (m_hostCallbacks.getCursorPosition) {
        m_hostCallbacks.getCursorPosition(line, col);
    }

    auto replaceAll = [&](const std::wstring& token, const std::wstring& val) {
        size_t pos = 0;
        while ((pos = out.find(token, pos)) != std::wstring::npos) {
            out.replace(pos, token.size(), val);
            pos += val.size();
        }
    };

    replaceAll(L"{file}", activeFile);
    replaceAll(L"${file}", activeFile);
    replaceAll(L"{workspace}", wsRoot);
    replaceAll(L"${workspaceFolder}", wsRoot);
    replaceAll(L"{line}", std::to_wstring(line + 1));
    replaceAll(L"{col}", std::to_wstring(col + 1));
    return out;
}

bool PluginManager::ExecuteCommand(const std::wstring& commandId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& cmd : m_commands) {
        if (cmd.id == commandId) {
            if (cmd.cppCallback) {
                cmd.cppCallback();
            }
            if (cmd.nativeCallback) {
                cmd.nativeCallback(cmd.id.c_str(), cmd.nativeUserData);
            }
            if (!cmd.insertText.empty() && m_hostCallbacks.insertTextAtCursor) {
                m_hostCallbacks.insertTextAtCursor(ExpandVariables(cmd.insertText));
            }
            if (!cmd.shellCommand.empty()) {
                std::wstring expanded = ExpandVariables(cmd.shellCommand);
                if (cmd.runInTerminal && m_hostCallbacks.runTerminalCommand) {
                    m_hostCallbacks.runTerminalCommand(expanded);
                } else if (cmd.replaceDocumentWithStdout && m_hostCallbacks.getActiveDocumentText && m_hostCallbacks.setActiveDocumentText) {
                    std::wstring curText = m_hostCallbacks.getActiveDocumentText();
                    std::wstring outText;
                    if (RunSilentProcessWithPipe(expanded, m_workspaceRoot, curText, outText, 4000) && !outText.empty()) {
                        m_hostCallbacks.setActiveDocumentText(outText);
                    }
                } else {
                    std::wstring ws = m_workspaceRoot;
                    std::thread([expanded, ws]() {
                        std::wstring dummy;
                        RunSilentProcessWithPipe(expanded, ws, L"", dummy, 10000);
                    }).detach();
                }
            }
            return true;
        }
    }
    return false;
}

bool PluginManager::TryHandleKeyBinding(UINT vk, bool ctrl, bool shift, bool alt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_commands.empty()) return false;

    for (const auto& cmd : m_commands) {
        if (cmd.keyTokens.empty()) continue;
        bool needCtrl = false, needShift = false, needAlt = false;
        UINT targetVk = 0;

        for (const auto& tokRaw : cmd.keyTokens) {
            std::wstring tok = ToLowerW(tokRaw);
            if (tok == L"ctrl" || tok == L"control") needCtrl = true;
            else if (tok == L"shift") needShift = true;
            else if (tok == L"alt") needAlt = true;
            else if (tok.size() == 1) {
                wchar_t c = static_cast<wchar_t>(towupper(tok[0]));
                if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
                    targetVk = static_cast<UINT>(c);
                }
            } else if (tok.size() >= 2 && tok[0] == L'f' && iswdigit(tok[1])) {
                int fn = _wtoi(tok.substr(1).c_str());
                if (fn >= 1 && fn <= 12) targetVk = VK_F1 + (fn - 1);
            }
        }

        if (targetVk != 0 && targetVk == vk && needCtrl == ctrl && needShift == shift && needAlt == alt) {
            return ExecuteCommand(cmd.id);
        }
    }
    return false;
}

// =============================================================================
// 8. Project Framework Detectors
// =============================================================================

void PluginManager::RegisterFrameworkDetector(const PluginFrameworkDetector& detector) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_frameworkDetectors.push_back(detector);
}

bool PluginManager::TryDetectFramework(
    const std::wstring& rootDir,
    const std::wstring& activeFilePath,
    PluginFrameworkDetector& outMatched,
    std::wstring& outMarkerPath
) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_frameworkDetectors.empty()) return false;

    std::error_code ec;
    std::filesystem::path root(rootDir);

    for (const auto& det : m_frameworkDetectors) {
        if (!det.markerFile.empty() && !rootDir.empty()) {
            std::filesystem::path marker = root / det.markerFile;
            if (std::filesystem::exists(marker, ec)) {
                if (!det.markerContains.empty()) {
                    std::wstring content = ReadFileWide(marker);
                    if (content.find(det.markerContains) == std::wstring::npos) {
                        continue;
                    }
                }
                outMatched = det;
                outMatched.command = ExpandVariables(det.command);
                outMarkerPath = marker.wstring();
                return true;
            }
        }
        if (!det.fileExtension.empty() && !activeFilePath.empty()) {
            std::wstring ext = NormalizeExt(std::filesystem::path(activeFilePath).extension().wstring());
            if (ext == det.fileExtension) {
                outMatched = det;
                outMatched.command = ExpandVariables(det.command);
                outMarkerPath = activeFilePath;
                return true;
            }
        }
    }
    return false;
}

// =============================================================================
// 9. Status Bar Items
// =============================================================================

void PluginManager::RegisterStatusBarItem(const PluginStatusBarItem& item) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& existing : m_statusBarItems) {
        if (existing.id == item.id) {
            existing.text = item.text;
            existing.tooltip = item.tooltip;
            existing.commandId = item.commandId;
            existing.alignRight = item.alignRight;
            existing.priority = item.priority;
            return;
        }
    }
    m_statusBarItems.push_back(item);
}

void PluginManager::UpdateStatusBarItem(const std::wstring& id, const std::wstring& text, const std::wstring& tooltip) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& item : m_statusBarItems) {
        if (item.id == id) {
            item.text = text;
            if (!tooltip.empty()) item.tooltip = tooltip;
            return;
        }
    }
}

std::vector<PluginStatusBarItem>& PluginManager::GetStatusBarItemsMutable() {
    return m_statusBarItems;
}

std::vector<PluginStatusBarItem> PluginManager::GetStatusBarItems() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_statusBarItems;
}

// =============================================================================
// 10. Lifecycle Event Hooks
// =============================================================================

void PluginManager::RegisterEventHook(const PluginEventHook& hook) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_eventHooks[ToLowerW(TrimW(hook.eventName))].push_back(hook);
}

void PluginManager::DispatchEvent(const std::wstring& eventName, const std::wstring& payloadJson) {
    std::vector<PluginEventHook> hooksToRun;
    std::wstring wsRoot;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_eventHooks.empty()) return;
        auto it = m_eventHooks.find(ToLowerW(TrimW(eventName)));
        if (it == m_eventHooks.end()) return;
        hooksToRun = it->second;
        wsRoot = m_workspaceRoot;
    }

    for (const auto& hook : hooksToRun) {
        if (hook.cppCallback) {
            hook.cppCallback(payloadJson);
        }
        if (hook.nativeCallback) {
            hook.nativeCallback(eventName.c_str(), payloadJson.c_str(), hook.nativeUserData);
        }
        if (!hook.shellCommand.empty()) {
            std::wstring expanded = ExpandVariables(hook.shellCommand);
            std::thread([expanded, wsRoot, payloadJson]() {
                std::wstring dummy;
                RunSilentProcessWithPipe(expanded, wsRoot, payloadJson, dummy, 5000);
            }).detach();
        }
    }
}

std::vector<PluginMetadata> PluginManager::GetLoadedPlugins() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_plugins;
}

} // namespace phide::plugin
