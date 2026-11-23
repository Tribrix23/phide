/**
 * @file SqlEngine.cpp
 * @brief Native SQLite3 DLL + MySQL/PostgreSQL/SQLServer CLI execution & schema inspector (#42).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winsvc.h>
#include <iphlpapi.h>
#include <tlhelp32.h>

#include "phide/db/SqlEngine.hpp"
#include "phide/app/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_set>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace phide::db {

namespace fs = std::filesystem;

namespace {

constexpr int SQLITE_OK_CODE = 0;
constexpr int SQLITE_ROW_CODE = 100;
constexpr int SQLITE_DONE_CODE = 101;
constexpr int SQLITE_NULL_TYPE = 5;
constexpr int SQLITE_OPEN_READWRITE_CREATE = 0x00000002 | 0x00000004;

struct SqliteApi {
    HMODULE mod = nullptr;
    int (*open_v2)(const char*, void**, int, const char*) = nullptr;
    int (*close_fn)(void*) = nullptr;
    int (*prepare_v2)(void*, const char*, int, void**, const char**) = nullptr;
    int (*step)(void*) = nullptr;
    int (*column_count)(void*) = nullptr;
    const char* (*column_name)(void*, int) = nullptr;
    const unsigned char* (*column_text)(void*, int) = nullptr;
    int (*column_type)(void*, int) = nullptr;
    int (*finalize)(void*) = nullptr;
    int (*changes)(void*) = nullptr;
    const char* (*errmsg)(void*) = nullptr;
};

void EnsureWsaInitialized() {
    static const bool s_wsaReady = []() {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)s_wsaReady;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (n <= 0) return L"";
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring TrimW(std::wstring s) {
    while (!s.empty() && std::iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && std::iswspace(s.back())) s.pop_back();
    return s;
}

std::string TrimA(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::wstring GetExeDirW() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().wstring();
}

std::wstring NormalizeConnectHost(const std::wstring& host) {
    std::wstring h = TrimW(host);
    std::wstring low = ToLowerW(h);
    if (h.empty() || low == L"localhost" || low == L"127.0.0.1") {
        return L"127.0.0.1";
    }
    return h;
}

std::wstring SanitizeCliArgW(const std::wstring& val) {
    std::wstring out;
    out.reserve(val.size());
    for (wchar_t c : val) {
        if (c == L'"' || c == L'\r' || c == L'\n') continue;
        out.push_back(c);
    }
    return out;
}

const SqliteApi& GetSqliteApi() {
    static SqliteApi api;
    static bool initialized = false;
    if (initialized) return api;
    initialized = true;

    std::wstring dllPath = SqlEngine::FindNativeSqliteDllPath();
    if (!dllPath.empty()) {
        api.mod = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (!api.mod) api.mod = LoadLibraryW(L"sqlite3.dll");
    if (!api.mod) api.mod = LoadLibraryW(L"libsqlite3.dll");
    if (!api.mod) return api;

    api.open_v2 = reinterpret_cast<decltype(api.open_v2)>(GetProcAddress(api.mod, "sqlite3_open_v2"));
    api.close_fn = reinterpret_cast<decltype(api.close_fn)>(GetProcAddress(api.mod, "sqlite3_close"));
    api.prepare_v2 = reinterpret_cast<decltype(api.prepare_v2)>(GetProcAddress(api.mod, "sqlite3_prepare_v2"));
    api.step = reinterpret_cast<decltype(api.step)>(GetProcAddress(api.mod, "sqlite3_step"));
    api.column_count = reinterpret_cast<decltype(api.column_count)>(GetProcAddress(api.mod, "sqlite3_column_count"));
    api.column_name = reinterpret_cast<decltype(api.column_name)>(GetProcAddress(api.mod, "sqlite3_column_name"));
    api.column_text = reinterpret_cast<decltype(api.column_text)>(GetProcAddress(api.mod, "sqlite3_column_text"));
    api.column_type = reinterpret_cast<decltype(api.column_type)>(GetProcAddress(api.mod, "sqlite3_column_type"));
    api.finalize = reinterpret_cast<decltype(api.finalize)>(GetProcAddress(api.mod, "sqlite3_finalize"));
    api.changes = reinterpret_cast<decltype(api.changes)>(GetProcAddress(api.mod, "sqlite3_changes"));
    api.errmsg = reinterpret_cast<decltype(api.errmsg)>(GetProcAddress(api.mod, "sqlite3_errmsg"));
    return api;
}

bool RunProcessCapture(const std::wstring& exePath,
                       const std::wstring& args,
                       const std::string& stdinBytes,
                       std::string& outStdout,
                       DWORD& outExitCode,
                       const std::vector<std::pair<std::wstring, std::wstring>>& extraEnvs = {},
                       DWORD timeoutMs = 4500) {
    outStdout.clear();
    outExitCode = 1;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hInR = nullptr, hInW = nullptr, hOutR = nullptr, hOutW = nullptr;
    if (!CreatePipe(&hInR, &hInW, &sa, 0)) return false;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0)) {
        CloseHandle(hInR);
        CloseHandle(hInW);
        return false;
    }
    SetHandleInformation(hInW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);

    // Build a child-only Unicode environment block so concurrent threads never mutate process-global env vars.
    std::vector<wchar_t> envBlock;
    void* lpEnv = nullptr;
    if (!extraEnvs.empty()) {
        LPWCH baseEnv = GetEnvironmentStringsW();
        if (baseEnv) {
            std::vector<std::wstring> entries;
            for (const wchar_t* p = baseEnv; *p != L'\0'; p += wcslen(p) + 1) {
                std::wstring entry(p);
                size_t eq = entry.find(L'=');
                std::wstring key = (eq != std::wstring::npos) ? entry.substr(0, eq) : entry;
                bool overridden = false;
                for (const auto& kv : extraEnvs) {
                    if (!kv.first.empty() && _wcsicmp(key.c_str(), kv.first.c_str()) == 0) {
                        overridden = true;
                        break;
                    }
                }
                if (!overridden) {
                    entries.push_back(std::move(entry));
                }
            }
            FreeEnvironmentStringsW(baseEnv);
            for (const auto& kv : extraEnvs) {
                if (!kv.first.empty() && !kv.second.empty()) {
                    entries.push_back(kv.first + L"=" + kv.second);
                }
            }
            for (const auto& entry : entries) {
                envBlock.insert(envBlock.end(), entry.begin(), entry.end());
                envBlock.push_back(L'\0');
            }
            envBlock.push_back(L'\0');
            lpEnv = envBlock.data();
        }
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInR;
    si.hStdOutput = hOutW;
    si.hStdError = hOutW;

    std::wstring cmd = L"\"" + exePath + L"\" " + args;
    PROCESS_INFORMATION pi{};
    DWORD createFlags = CREATE_NO_WINDOW | (lpEnv ? CREATE_UNICODE_ENVIRONMENT : 0);
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, createFlags, lpEnv, nullptr, &si, &pi);
    CloseHandle(hInR);
    CloseHandle(hOutW);

    if (!ok) {
        CloseHandle(hInW);
        CloseHandle(hOutR);
        return false;
    }

    if (!stdinBytes.empty()) {
        DWORD written = 0;
        WriteFile(hInW, stdinBytes.data(), static_cast<DWORD>(stdinBytes.size()), &written, nullptr);
    }
    CloseHandle(hInW);

    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    char buf[4096];
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(hOutR, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD rd = 0;
            while (ReadFile(hOutR, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
                outStdout.append(buf, rd);
                avail = 0;
                if (!PeekNamedPipe(hOutR, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            }
        }
        DWORD waitRc = WaitForSingleObject(pi.hProcess, 15);
        if (waitRc == WAIT_OBJECT_0) {
            // Drain any remaining bytes on the pipe
            DWORD rd = 0;
            while (ReadFile(hOutR, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
                outStdout.append(buf, rd);
            }
            break;
        }
        if (GetTickCount64() >= deadline) {
            TerminateProcess(pi.hProcess, 1);
            outExitCode = WAIT_TIMEOUT;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hOutR);
            return false;
        }
    }

    GetExitCodeProcess(pi.hProcess, &outExitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hOutR);
    return true;
}

std::wstring FindToolExe(const std::wstring& exeName,
                         const std::vector<std::wstring>& knownPaths,
                         const std::vector<std::wstring>& versionedParents = {}) {
    std::error_code ec;
    for (const auto& p : knownPaths) {
        if (!p.empty() && fs::exists(p, ec)) return p;
    }
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, exeName.c_str(), nullptr, MAX_PATH, found, nullptr) > 0) {
        return found;
    }
    for (const auto& parent : versionedParents) {
        if (parent.empty() || !fs::exists(parent, ec) || !fs::is_directory(parent, ec)) continue;
        std::vector<std::wstring> matches;
        for (const auto& ent : fs::directory_iterator(parent, fs::directory_options::skip_permission_denied, ec)) {
            if (!ent.is_directory(ec)) continue;
            fs::path cand = ent.path() / L"bin" / exeName;
            if (fs::exists(cand, ec)) matches.push_back(cand.wstring());
        }
        if (!matches.empty()) {
            std::sort(matches.rbegin(), matches.rend());
            return matches.front();
        }
    }
    return L"";
}

std::wstring FindMysqlToolExe(const wchar_t* exeName) {
    fs::path exeDir(GetExeDirW());
    return FindToolExe(
        exeName,
        {
            (fs::path(L"C:\\Program Files\\phide\\runtimes\\mysql\\bin") / exeName).wstring(),
            (fs::path(L"C:\\Program Files\\phide\\runtimes\\mysql") / exeName).wstring(),
            (exeDir / L"runtimes" / L"mysql" / L"bin" / exeName).wstring(),
            (exeDir / L"runtimes" / L"mysql" / exeName).wstring(),
            (exeDir.parent_path() / L"runtimes" / L"mysql" / L"bin" / exeName).wstring(),
            (exeDir / L"installer" / L"payload" / L"runtimes" / L"mysql" / L"bin" / exeName).wstring(),
            (exeDir.parent_path() / L"installer" / L"payload" / L"runtimes" / L"mysql" / L"bin" / exeName).wstring(),
            (fs::path(L"C:\\xampp\\mysql\\bin") / exeName).wstring()
        },
        {
            L"C:\\laragon\\bin\\mysql",
            L"C:\\Program Files\\MySQL",
            L"C:\\Program Files\\MariaDB"
        });
}

std::wstring FindMysqlExe() { return FindMysqlToolExe(L"mysql.exe"); }
std::wstring FindMysqldExe() { return FindMysqlToolExe(L"mysqld.exe"); }
std::wstring FindMysqlAdminExe() { return FindMysqlToolExe(L"mysqladmin.exe"); }

std::wstring FindPsqlExe() {
    return FindToolExe(
        L"psql.exe",
        {
            L"C:\\xampp\\pgsql\\bin\\psql.exe"
        },
        {
            L"C:\\Program Files\\PostgreSQL",
            L"C:\\Program Files (x86)\\PostgreSQL",
            L"C:\\laragon\\bin\\postgresql"
        });
}

std::wstring FindPgCtlExe() {
    std::wstring psql = FindPsqlExe();
    std::vector<std::wstring> known = { L"C:\\xampp\\pgsql\\bin\\pg_ctl.exe" };
    if (!psql.empty()) {
        known.insert(known.begin(), (fs::path(psql).parent_path() / L"pg_ctl.exe").wstring());
    }
    return FindToolExe(
        L"pg_ctl.exe",
        known,
        {
            L"C:\\Program Files\\PostgreSQL",
            L"C:\\Program Files (x86)\\PostgreSQL",
            L"C:\\laragon\\bin\\postgresql"
        });
}

std::wstring FindSqlCmdExe() {
    return FindToolExe(
        L"sqlcmd.exe",
        {
            L"C:\\Program Files\\Microsoft SQL Server\\Client SDK\\ODBC\\180\\Tools\\Binn\\sqlcmd.exe",
            L"C:\\Program Files\\Microsoft SQL Server\\Client SDK\\ODBC\\170\\Tools\\Binn\\sqlcmd.exe",
            L"C:\\Program Files\\Microsoft SQL Server\\110\\Tools\\Binn\\sqlcmd.exe"
        });
}

bool IsLocalHostAddress(const std::wstring& host) {
    std::wstring low = ToLowerW(TrimW(host));
    return low.empty() || low == L"localhost" || low == L"127.0.0.1" || low == L"::1";
}

struct ScmServiceMatch {
    std::wstring serviceName;
    DWORD currentState = 0;
};

std::vector<ScmServiceMatch> FindWindowsServicesByPrefix(const std::vector<std::wstring>& prefixes) {
    std::vector<ScmServiceMatch> matches;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return matches;

    DWORD bytesNeeded = 0;
    DWORD count = 0;
    DWORD resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &bytesNeeded, &count, &resume, nullptr);
    if (GetLastError() == ERROR_MORE_DATA && bytesNeeded > 0) {
        std::vector<BYTE> buf(bytesNeeded);
        resume = 0;
        if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                  buf.data(), bytesNeeded, &bytesNeeded, &count, &resume, nullptr)) {
            const auto* entries = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
            for (DWORD i = 0; i < count; ++i) {
                std::wstring name = entries[i].lpServiceName ? entries[i].lpServiceName : L"";
                std::wstring low = ToLowerW(name);
                for (const auto& pfx : prefixes) {
                    if (low.rfind(ToLowerW(pfx), 0) == 0) {
                        matches.push_back({ name, entries[i].ServiceStatusProcess.dwCurrentState });
                        break;
                    }
                }
            }
        }
    }
    CloseServiceHandle(scm);
    return matches;
}

bool ControlWindowsServiceState(const std::wstring& serviceName, bool start, DWORD timeoutMs, std::wstring& outErr) {
    outErr.clear();
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        outErr = L"Failed to open Windows Service Control Manager.";
        return false;
    }
    const DWORD access = start ? (SERVICE_START | SERVICE_QUERY_STATUS) : (SERVICE_STOP | SERVICE_QUERY_STATUS);
    SC_HANDLE svc = OpenServiceW(scm, serviceName.c_str(), access);
    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        outErr = (err == ERROR_ACCESS_DENIED)
            ? (L"Permission denied for service '" + serviceName + L"'. Run Phide as Administrator.")
            : (L"Could not open Windows service '" + serviceName + L"'.");
        return false;
    }

    SERVICE_STATUS_PROCESS ssp{};
    DWORD bytesNeeded = 0;
    bool ok = false;
    if (start) {
        if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded) &&
            ssp.dwCurrentState == SERVICE_RUNNING) {
            ok = true;
        } else if (StartServiceW(svc, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
            ULONGLONG deadline = GetTickCount64() + timeoutMs;
            while (GetTickCount64() < deadline) {
                if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
                    if (ssp.dwCurrentState == SERVICE_RUNNING) {
                        ok = true;
                        break;
                    }
                    if (ssp.dwCurrentState == SERVICE_STOPPED) break;
                }
                Sleep(80);
            }
        } else {
            DWORD err = GetLastError();
            outErr = (err == ERROR_ACCESS_DENIED)
                ? (L"Permission denied starting service '" + serviceName + L"'.")
                : (L"Failed to start service '" + serviceName + L"' (code " + std::to_wstring(err) + L").");
        }
    } else {
        if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded) &&
            ssp.dwCurrentState == SERVICE_STOPPED) {
            ok = true;
        } else {
            SERVICE_STATUS st{};
            if (ControlService(svc, SERVICE_CONTROL_STOP, &st) || GetLastError() == ERROR_SERVICE_NOT_ACTIVE) {
                ULONGLONG deadline = GetTickCount64() + timeoutMs;
                while (GetTickCount64() < deadline) {
                    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
                        if (ssp.dwCurrentState == SERVICE_STOPPED) {
                            ok = true;
                            break;
                        }
                    }
                    Sleep(80);
                }
            } else {
                DWORD err = GetLastError();
                outErr = (err == ERROR_ACCESS_DENIED)
                    ? (L"Permission denied stopping service '" + serviceName + L"'.")
                    : (L"Failed to stop service '" + serviceName + L"' (code " + std::to_wstring(err) + L").");
            }
        }
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

bool TerminateLocalProcessListeningOnPort(int port, const std::vector<std::wstring>& allowedExeNames) {
    if (port <= 0 || port >= 65536) return false;
    ULONG size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (size == 0) return false;
    std::vector<BYTE> buf(size);
    if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) {
        return false;
    }
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buf.data());
    std::vector<DWORD> pids;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        DWORD p = table->table[i].dwLocalPort;
        int localPort = static_cast<int>(((p & 0xFF) << 8) | ((p >> 8) & 0xFF));
        if (localPort == port && table->table[i].dwOwningPid > 4) {
            pids.push_back(table->table[i].dwOwningPid);
        }
    }
    if (pids.empty()) return false;

    bool terminated = false;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (std::find(pids.begin(), pids.end(), pe.th32ProcessID) == pids.end()) continue;
                std::wstring exeLow = ToLowerW(pe.szExeFile);
                bool match = false;
                for (const auto& allowed : allowedExeNames) {
                    if (exeLow == ToLowerW(allowed)) { match = true; break; }
                }
                if (!match) continue;
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    if (TerminateProcess(hProc, 0)) {
                        WaitForSingleObject(hProc, 1500);
                        terminated = true;
                    }
                    CloseHandle(hProc);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return terminated;
}

int ReadPortFile(const fs::path& portFile) {
    std::error_code ec;
    if (!fs::exists(portFile, ec)) return 0;
    std::ifstream in(portFile);
    if (!in.is_open()) return 0;
    int p = 0;
    in >> p;
    return (p > 0 && p < 65536) ? p : 0;
}

void PersistMysqlPort(int port) {
    if (port <= 0 || port >= 65536) return;
    SetEnvironmentVariableW(L"PHIDE_MYSQL_PORT", std::to_wstring(port).c_str());
    std::error_code ec;
    wchar_t localApp[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0 && localApp[0] != 0) {
        fs::path dir = fs::path(localApp) / L"phide";
        fs::create_directories(dir, ec);
        std::ofstream out(dir / L"mysql_port.txt", std::ios::trunc);
        if (out.is_open()) out << port;
    }
    fs::path exeDir(GetExeDirW());
    fs::path cwd = fs::current_path(ec);
    std::vector<fs::path> roots = {
        exeDir,
        exeDir.parent_path(),
        exeDir.parent_path().parent_path(),
        cwd,
        fs::path(L"C:\\Program Files\\phide")
    };
    for (const auto& r : roots) {
        if (r.empty()) continue;
        fs::path pmaDir = r / L"include" / L"phide" / L"db" / L"phpMyAdmin";
        if (fs::exists(pmaDir, ec) && fs::is_directory(pmaDir, ec)) {
            std::ofstream out(pmaDir / L"mysql_port.txt", std::ios::trunc);
            if (out.is_open()) out << port;
        }
    }
}

int ReadMysqlPortFromPmaConfig() {
    std::error_code ec;
    wchar_t localApp[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0 && localApp[0] != 0) {
        int p = ReadPortFile(fs::path(localApp) / L"phide" / L"mysql_port.txt");
        if (p > 0) return p;
    }
    fs::path exeDir(GetExeDirW());
    fs::path cwd = fs::current_path(ec);
    std::vector<fs::path> roots = {
        exeDir,
        exeDir.parent_path(),
        exeDir.parent_path().parent_path(),
        cwd,
        fs::path(L"C:\\Program Files\\phide")
    };
    for (const auto& r : roots) {
        if (r.empty()) continue;
        int p = ReadPortFile(r / L"include" / L"phide" / L"db" / L"phpMyAdmin" / L"mysql_port.txt");
        if (p > 0) return p;
    }
    return 0;
}

struct WorkspaceEnvDbHints {
    std::wstring connection;
    std::wstring host;
    int port = 0;
    std::wstring database;
    std::wstring user;
    std::wstring password;
    int pgPort = 0;
    std::wstring pgUser;
    std::wstring pgPass;
    std::wstring pgDb;
};

WorkspaceEnvDbHints ParseWorkspaceEnvDbHints(const std::wstring& workspaceRoot) {
    WorkspaceEnvDbHints hints;
    if (workspaceRoot.empty()) return hints;
    std::error_code ec;
    fs::path envPath = fs::path(workspaceRoot) / L".env";
    if (!fs::exists(envPath, ec) || !fs::is_regular_file(envPath, ec)) return hints;

    std::ifstream in(envPath, std::ios::binary);
    if (!in.is_open()) return hints;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = TrimA(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = TrimA(t.substr(0, eq));
        std::string val = TrimA(t.substr(eq + 1));
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }
        std::wstring wval = Utf8ToWide(val);
        if (key == "DB_CONNECTION") hints.connection = ToLowerW(wval);
        else if (key == "DB_HOST") hints.host = wval;
        else if (key == "DB_PORT") {
            try { int p = std::stoi(val); if (p > 0 && p < 65536) hints.port = p; } catch (...) {}
        } else if (key == "DB_DATABASE") hints.database = wval;
        else if (key == "DB_USERNAME" || key == "DB_USER") hints.user = wval;
        else if (key == "DB_PASSWORD") hints.password = wval;
        else if (key == "PGPORT") {
            try { int p = std::stoi(val); if (p > 0 && p < 65536) hints.pgPort = p; } catch (...) {}
        } else if (key == "PGUSER") hints.pgUser = wval;
        else if (key == "PGPASSWORD") hints.pgPass = wval;
        else if (key == "PGDATABASE") hints.pgDb = wval;
    }
    return hints;
}

std::wstring QuoteSqlIdent(const std::wstring& id) {
    std::wstring out = L"\"";
    for (wchar_t c : id) {
        if (c == L'"') out += L"\"\"";
        else out.push_back(c);
    }
    out += L"\"";
    return out;
}

std::wstring EscapeSqlLiteral(const std::wstring& val) {
    std::wstring out;
    out.reserve(val.size());
    for (wchar_t c : val) {
        if (c == L'\'') out += L"''";
        else if (c == L'\\') out += L"\\\\";
        else out.push_back(c);
    }
    return out;
}

std::vector<std::wstring> SplitTabRow(const std::string& line) {
    std::vector<std::wstring> cols;
    size_t start = 0;
    while (true) {
        size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            std::string cell = line.substr(start);
            cols.push_back(cell == "\\N" ? L"NULL" : Utf8ToWide(cell));
            break;
        }
        std::string cell = line.substr(start, tab - start);
        cols.push_back(cell == "\\N" ? L"NULL" : Utf8ToWide(cell));
        start = tab + 1;
    }
    return cols;
}

bool SqlTextStartsWithResultQuery(const std::wstring& sql) {
    std::wstring up = ToLowerW(TrimW(sql));
    return up.rfind(L"select", 0) == 0 ||
           up.rfind(L"show", 0) == 0 ||
           up.rfind(L"describe", 0) == 0 ||
           up.rfind(L"desc ", 0) == 0 ||
           up.rfind(L"explain", 0) == 0 ||
           up.rfind(L"with ", 0) == 0 ||
           up.rfind(L"values", 0) == 0 ||
           up.rfind(L"table ", 0) == 0 ||
           up.rfind(L"pragma ", 0) == 0 ||
           up.rfind(L"\\", 0) == 0;
}

} // namespace

bool SqlEngine::IsTcpPortOpen(const std::wstring& host, int port, int timeoutMs) {
    if (port <= 0 || port > 65535) return false;
    EnsureWsaInitialized();

    std::wstring connectHost = NormalizeConnectHost(host);
    std::string hostA = WideToUtf8(connectHost);

    auto tryConnectSock = [&](int af, const void* addrPtr, int addrLen) -> bool {
        SOCKET s = socket(af, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return false;
        u_long nonBlock = 1;
        ioctlsocket(s, FIONBIO, &nonBlock);

        int rc = connect(s, reinterpret_cast<const sockaddr*>(addrPtr), addrLen);
        bool open = false;
        if (rc == 0) {
            open = true;
        } else {
            int wsaErr = WSAGetLastError();
            if (wsaErr == WSAEWOULDBLOCK || wsaErr == WSAEINPROGRESS || wsaErr == WSAEINVAL) {
                fd_set writeSet;
                fd_set errSet;
                FD_ZERO(&writeSet);
                FD_ZERO(&errSet);
                FD_SET(s, &writeSet);
                FD_SET(s, &errSet);
                timeval tv{};
                tv.tv_sec = timeoutMs / 1000;
                tv.tv_usec = (timeoutMs % 1000) * 1000;
                int sel = select(0, nullptr, &writeSet, &errSet, &tv);
                if (sel > 0 && FD_ISSET(s, &writeSet) && !FD_ISSET(s, &errSet)) {
                    int soErr = 0;
                    int len = sizeof(soErr);
                    if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len) == 0 && soErr == 0) {
                        open = true;
                    }
                }
            }
        }
        closesocket(s);
        return open;
    };

    sockaddr_in sa4{};
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, hostA.c_str(), &sa4.sin_addr) == 1) {
        if (tryConnectSock(AF_INET, &sa4, sizeof(sa4))) return true;
        if (connectHost == L"127.0.0.1") {
            sockaddr_in6 sa6{};
            sa6.sin6_family = AF_INET6;
            sa6.sin6_port = htons(static_cast<u_short>(port));
            inet_pton(AF_INET6, "::1", &sa6.sin6_addr);
            if (tryConnectSock(AF_INET6, &sa6, sizeof(sa6))) return true;
        }
        return false;
    }

    sockaddr_in6 sa6{};
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET6, hostA.c_str(), &sa6.sin6_addr) == 1) {
        return tryConnectSock(AF_INET6, &sa6, sizeof(sa6));
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(hostA.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        return false;
    }
    bool anyOpen = false;
    for (addrinfo* cur = res; cur != nullptr; cur = cur->ai_next) {
        if (tryConnectSock(cur->ai_family, cur->ai_addr, static_cast<int>(cur->ai_addrlen))) {
            anyOpen = true;
            break;
        }
    }
    freeaddrinfo(res);
    return anyOpen;
}

bool SqlEngine::IsPortAvailableForBind(int port) {
    if (port <= 0 || port > 65535) return false;
    EnsureWsaInitialized();

    if (IsTcpPortOpen(L"127.0.0.1", port, 25)) {
        return false;
    }

    auto hasListenerOnPort = [&](ULONG family) -> bool {
        DWORD dwSize = 0;
        DWORD rc = GetExtendedTcpTable(nullptr, &dwSize, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        for (int retry = 0; retry < 4 && (rc == ERROR_INSUFFICIENT_BUFFER || rc == NO_ERROR) && dwSize > 0; ++retry) {
            std::vector<BYTE> buffer(dwSize);
            rc = GetExtendedTcpTable(buffer.data(), &dwSize, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
            if (rc != NO_ERROR) continue;
            if (family == AF_INET) {
                const auto* tbl = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
                for (DWORD i = 0; i < tbl->dwNumEntries; ++i) {
                    DWORD p = tbl->table[i].dwLocalPort;
                    int lp = static_cast<int>(((p & 0xFF) << 8) | ((p >> 8) & 0xFF));
                    if (lp == port) return true;
                }
            } else {
                const auto* tbl6 = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
                for (DWORD i = 0; i < tbl6->dwNumEntries; ++i) {
                    DWORD p = tbl6->table[i].dwLocalPort;
                    int lp = static_cast<int>(((p & 0xFF) << 8) | ((p >> 8) & 0xFF));
                    if (lp == port) return true;
                }
            }
            return false;
        }
        return false;
    };

    if (hasListenerOnPort(AF_INET) || hasListenerOnPort(AF_INET6)) {
        return false;
    }

    // Probe exclusive socket bind on IPv4 0.0.0.0 to catch TIME_WAIT, BOUND sockets, and Hyper-V/WinNAT excluded ports
    {
        SOCKET s4 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s4 == INVALID_SOCKET) return false;
        BOOL excl = TRUE;
        setsockopt(s4, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof(excl));
        sockaddr_in sa4{};
        sa4.sin_family = AF_INET;
        sa4.sin_addr.s_addr = htonl(INADDR_ANY);
        sa4.sin_port = htons(static_cast<u_short>(port));
        int bindRc = bind(s4, reinterpret_cast<const sockaddr*>(&sa4), sizeof(sa4));
        closesocket(s4);
        if (bindRc != 0) return false;
    }

    // Probe IPv6 :: with IPV6_V6ONLY=1 when IPv6 stack is enabled (mysqld/php may bind dual-stack '::')
    {
        SOCKET s6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (s6 != INVALID_SOCKET) {
            DWORD v6only = 1;
            setsockopt(s6, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));
            BOOL excl = TRUE;
            setsockopt(s6, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof(excl));
            sockaddr_in6 sa6{};
            sa6.sin6_family = AF_INET6;
            sa6.sin6_addr = in6addr_any;
            sa6.sin6_port = htons(static_cast<u_short>(port));
            int bindRc6 = bind(s6, reinterpret_cast<const sockaddr*>(&sa6), sizeof(sa6));
            int wsaErr6 = (bindRc6 != 0) ? WSAGetLastError() : 0;
            closesocket(s6);
            if (bindRc6 != 0 && (wsaErr6 == WSAEADDRINUSE || wsaErr6 == WSAEACCES)) {
                return false;
            }
        }
    }

    return true;
}

int SqlEngine::ResolveDynamicMysqlPort(int preferredPort, const std::wstring& workspaceRoot) {
    std::vector<int> candidates;
    auto pushCand = [&](int p) {
        if (p > 0 && p < 65536 && std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
            candidates.push_back(p);
        }
    };

    wchar_t envBuf[32] = {};
    int envPort = 0;
    if (GetEnvironmentVariableW(L"PHIDE_MYSQL_PORT", envBuf, 32) > 0) {
        try { envPort = std::stoi(envBuf); } catch (...) {}
    }
    int pmaFilePort = ReadMysqlPortFromPmaConfig();
    WorkspaceEnvDbHints envHints = ParseWorkspaceEnvDbHints(workspaceRoot);
    int dotEnvPort = (envHints.connection.empty() || envHints.connection == L"mysql" || envHints.connection == L"mariadb")
        ? envHints.port : 0;

    pushCand(envPort);
    pushCand(pmaFilePort);
    if (preferredPort > 0 && preferredPort != 3306) {
        pushCand(preferredPort);
    }
    pushCand(dotEnvPort);
    pushCand(preferredPort);
    pushCand(3306);
    pushCand(3307);
    pushCand(3308);
    pushCand(3309);
    pushCand(33060);

    for (int p : candidates) {
        if (IsTcpPortOpen(L"127.0.0.1", p, 35)) {
            return p;
        }
    }

    if (pmaFilePort > 0) return pmaFilePort;
    if (envPort > 0) return envPort;
    if (dotEnvPort > 0) return dotEnvPort;
    return (preferredPort > 0 && preferredPort < 65536) ? preferredPort : 3306;
}

std::wstring SqlEngine::FindNativeSqliteDllPath() {
    std::error_code ec;
    std::wstring exeDir = GetExeDirW();
    std::vector<fs::path> candidates = {
        fs::path(exeDir) / L"sqlite3.dll",
        fs::path(exeDir) / L"libsqlite3.dll",
        fs::path(exeDir) / L"runtimes" / L"php" / L"libsqlite3.dll",
        fs::path(exeDir).parent_path() / L"runtimes" / L"php" / L"libsqlite3.dll",
        fs::path(L"C:\\Program Files\\phide\\runtimes\\php\\libsqlite3.dll"),
        fs::path(L"C:\\Program Files\\phide\\runtimes\\php\\sqlite3.dll")
    };
    for (const auto& c : candidates) {
        if (fs::exists(c, ec)) return c.wstring();
    }
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"sqlite3.dll", nullptr, MAX_PATH, found, nullptr) > 0) return found;
    if (SearchPathW(nullptr, L"libsqlite3.dll", nullptr, MAX_PATH, found, nullptr) > 0) return found;
    return L"";
}

std::vector<SqlConnectionProfile> SqlEngine::DiscoverWorkspaceDatabases(
    const std::wstring& workspaceRoot,
    const std::wstring& mysqlHost,
    int mysqlPort,
    const std::wstring& mysqlUser,
    const std::wstring& mysqlPass,
    const std::wstring& mysqlDb) {
    std::vector<SqlConnectionProfile> profiles;
    std::error_code ec;

    // 1. Scan workspace on disk for real .sqlite / .sqlite3 / .db files (never invent :memory:)
    if (!workspaceRoot.empty() && fs::exists(workspaceRoot, ec)) {
        static const std::vector<std::wstring> kSkipDirs = {
            L".git", L".phide", L"node_modules", L".venv", L"venv", L"vendor", L"__pycache__", L"bin", L"obj"
        };
        auto it = fs::recursive_directory_iterator(workspaceRoot, fs::directory_options::skip_permission_denied, ec);
        for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
            const fs::path& p = it->path();
            if (it->is_directory(ec)) {
                std::wstring dn = ToLowerW(p.filename().wstring());
                if (std::find(kSkipDirs.begin(), kSkipDirs.end(), dn) != kSkipDirs.end()) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            std::wstring ext = ToLowerW(p.extension().wstring());
            if (ext == L".sqlite" || ext == L".sqlite3" || ext == L".db") {
                SqlConnectionProfile prof;
                prof.dialect = SqlDialect::SQLite;
                prof.sqlitePath = p.wstring();
                prof.name = L"SQLite: " + p.filename().wstring();
                prof.isConnected = true;
                profiles.push_back(std::move(prof));
            }
        }
    }

    WorkspaceEnvDbHints envHints = ParseWorkspaceEnvDbHints(workspaceRoot);

    std::wstring effMysqlHost = !envHints.host.empty() && (envHints.connection.empty() || envHints.connection == L"mysql" || envHints.connection == L"mariadb")
        ? envHints.host : (mysqlHost.empty() ? L"localhost" : mysqlHost);
    std::wstring effMysqlUser = (!mysqlUser.empty() ? mysqlUser : (!envHints.user.empty() ? envHints.user : L"root"));
    std::wstring effMysqlPass = (!mysqlPass.empty() ? mysqlPass : envHints.password);
    std::wstring effMysqlDb   = (!mysqlDb.empty() ? mysqlDb : ((envHints.connection.empty() || envHints.connection == L"mysql" || envHints.connection == L"mariadb") ? envHints.database : L""));

    // 2. Dynamically detect live MySQL / MariaDB instances on local ports
    int primaryMysqlPort = ResolveDynamicMysqlPort(mysqlPort, workspaceRoot);
    std::vector<int> mysqlPortsToProbe;
    auto pushMysqlPort = [&](int p) {
        if (p > 0 && p < 65536 && std::find(mysqlPortsToProbe.begin(), mysqlPortsToProbe.end(), p) == mysqlPortsToProbe.end()) {
            mysqlPortsToProbe.push_back(p);
        }
    };
    pushMysqlPort(primaryMysqlPort);
    if (mysqlPort > 0) pushMysqlPort(mysqlPort);
    pushMysqlPort(3306);
    pushMysqlPort(3307);
    pushMysqlPort(3308);
    pushMysqlPort(3309);

    bool addedLiveMysql = false;
    for (int p : mysqlPortsToProbe) {
        if (IsTcpPortOpen(effMysqlHost, p, 35)) {
            SqlConnectionProfile mysqlProf;
            mysqlProf.dialect = SqlDialect::MySQL;
            mysqlProf.host = effMysqlHost;
            mysqlProf.port = p;
            mysqlProf.user = effMysqlUser;
            mysqlProf.password = effMysqlPass;
            mysqlProf.database = effMysqlDb;
            mysqlProf.name = L"MySQL (" + effMysqlHost + L":" + std::to_wstring(p) + L")";
            mysqlProf.isConnected = true;
            profiles.push_back(std::move(mysqlProf));
            addedLiveMysql = true;
        }
    }
    if (!addedLiveMysql) {
        SqlConnectionProfile mysqlProf;
        mysqlProf.dialect = SqlDialect::MySQL;
        mysqlProf.host = effMysqlHost;
        mysqlProf.port = primaryMysqlPort;
        mysqlProf.user = effMysqlUser;
        mysqlProf.password = effMysqlPass;
        mysqlProf.database = effMysqlDb;
        mysqlProf.name = L"MySQL (" + effMysqlHost + L":" + std::to_wstring(primaryMysqlPort) + L")";
        mysqlProf.isConnected = false;
        profiles.push_back(std::move(mysqlProf));
    }

    // 3. Dynamically detect running PostgreSQL instances (ports 5432, 5433, 5434 or .env PGPORT)
    std::vector<int> pgPorts;
    auto pushPgPort = [&](int p) {
        if (p > 0 && p < 65536 && std::find(pgPorts.begin(), pgPorts.end(), p) == pgPorts.end()) {
            pgPorts.push_back(p);
        }
    };
    if (envHints.pgPort > 0) pushPgPort(envHints.pgPort);
    if (envHints.connection == L"pgsql" && envHints.port > 0) pushPgPort(envHints.port);
    pushPgPort(5432);
    pushPgPort(5433);
    pushPgPort(5434);

    std::wstring pgHost = (!envHints.host.empty() && envHints.connection == L"pgsql") ? envHints.host : L"localhost";
    std::wstring pgUser = !envHints.pgUser.empty() ? envHints.pgUser
                        : ((!envHints.user.empty() && envHints.connection == L"pgsql") ? envHints.user : L"postgres");
    std::wstring pgPass = !envHints.pgPass.empty() ? envHints.pgPass
                        : ((envHints.connection == L"pgsql") ? envHints.password : L"");
    std::wstring pgDb   = !envHints.pgDb.empty() ? envHints.pgDb
                        : ((!envHints.database.empty() && envHints.connection == L"pgsql") ? envHints.database : L"postgres");

    bool addedPg = false;
    for (int p : pgPorts) {
        if (IsTcpPortOpen(pgHost, p, 35)) {
            SqlConnectionProfile pgProf;
            pgProf.dialect = SqlDialect::PostgreSQL;
            pgProf.host = pgHost;
            pgProf.port = p;
            pgProf.user = pgUser;
            pgProf.password = pgPass;
            pgProf.database = pgDb;
            pgProf.name = L"PostgreSQL (" + pgHost + L":" + std::to_wstring(p) + L")";
            pgProf.isConnected = true;
            profiles.push_back(std::move(pgProf));
            addedPg = true;
        }
    }
    auto pgServices = FindWindowsServicesByPrefix({ L"postgresql", L"postgres" });
    if (!addedPg && (envHints.connection == L"pgsql" || !FindPsqlExe().empty() || !pgServices.empty())) {
        int fallbackPg = !pgPorts.empty() ? pgPorts.front() : 5432;
        SqlConnectionProfile pgProf;
        pgProf.dialect = SqlDialect::PostgreSQL;
        pgProf.host = pgHost;
        pgProf.port = fallbackPg;
        pgProf.user = pgUser;
        pgProf.password = pgPass;
        pgProf.database = pgDb;
        pgProf.name = L"PostgreSQL (" + pgHost + L":" + std::to_wstring(fallbackPg) + L")";
        pgProf.isConnected = false;
        profiles.push_back(std::move(pgProf));
    }

    // 4. Dynamically detect SQL Server (1433) if listening, installed service exists, or .env configures sqlsrv
    auto msServices = FindWindowsServicesByPrefix({ L"mssqlserver", L"mssql$" });
    if (IsTcpPortOpen(L"127.0.0.1", 1433, 35) || !msServices.empty() || envHints.connection == L"sqlsrv") {
        SqlConnectionProfile msProf;
        msProf.dialect = SqlDialect::SQLServer;
        msProf.host = L"localhost";
        msProf.port = 1433;
        msProf.user = (!envHints.user.empty() && envHints.connection == L"sqlsrv") ? envHints.user : L"sa";
        msProf.password = (envHints.connection == L"sqlsrv") ? envHints.password : L"";
        msProf.database = (envHints.connection == L"sqlsrv" && !envHints.database.empty()) ? envHints.database : L"master";
        msProf.name = L"SQL Server (localhost:1433)";
        msProf.isConnected = IsTcpPortOpen(L"127.0.0.1", 1433, 35);
        profiles.push_back(std::move(msProf));
    }

    return profiles;
}

bool SqlEngine::StartDatabaseServer(const SqlConnectionProfile& profile, std::wstring& outMessage, int* outResolvedPort) {
    outMessage.clear();
    if (outResolvedPort) *outResolvedPort = profile.port;

    if (profile.dialect == SqlDialect::SQLite) {
        outMessage = L"SQLite is file-based and does not use a server.";
        return true;
    }
    if (!IsLocalHostAddress(profile.host)) {
        outMessage = L"Cannot start remote host '" + profile.host + L"'. Start the server directly on that machine.";
        PHIDE_LOG_WARN(Server, L"SqlEngine::StartDatabaseServer skipped for remote host: " + profile.host);
        return false;
    }
    PHIDE_LOG_INFO(Server, L"SqlEngine starting database server on " + profile.host + L":" + std::to_wstring(profile.port));

    const std::wstring connectHost = NormalizeConnectHost(profile.host);

    if (profile.dialect == SqlDialect::PostgreSQL) {
        const int effPort = (profile.port > 0 && profile.port < 65536) ? profile.port : 5432;
        if (outResolvedPort) *outResolvedPort = effPort;
        if (IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"PostgreSQL is already running (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }

        std::wstring svcErr;
        auto pgServices = FindWindowsServicesByPrefix({ L"postgresql", L"postgres" });
        for (const auto& svc : pgServices) {
            ControlWindowsServiceState(svc.serviceName, true, 5000, svcErr);
        }

        ULONGLONG deadline = GetTickCount64() + 4500;
        while (GetTickCount64() < deadline) {
            if (IsTcpPortOpen(connectHost, effPort, 35)) break;
            Sleep(100);
        }

        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            std::wstring pgCtl = FindPgCtlExe();
            if (!pgCtl.empty()) {
                fs::path dataDir;
                wchar_t pgDataEnv[MAX_PATH] = {};
                if (GetEnvironmentVariableW(L"PGDATA", pgDataEnv, MAX_PATH) > 0 && pgDataEnv[0] != 0) {
                    dataDir = pgDataEnv;
                } else {
                    dataDir = fs::path(pgCtl).parent_path().parent_path() / L"data";
                }
                std::error_code ec;
                if (fs::exists(dataDir, ec) && fs::is_directory(dataDir, ec)) {
                    std::string out;
                    DWORD code = 1;
                    RunProcessCapture(pgCtl, L"start -D \"" + dataDir.wstring() + L"\" -w -t 5", "", out, code, {}, 6000);
                }
            }
        }

        deadline = GetTickCount64() + 2000;
        while (GetTickCount64() < deadline) {
            if (IsTcpPortOpen(connectHost, effPort, 35)) break;
            Sleep(80);
        }

        if (IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"Started PostgreSQL (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }
        outMessage = !svcErr.empty()
            ? svcErr
            : (L"Failed to start PostgreSQL on port " + std::to_wstring(effPort) + L". Check the Windows PostgreSQL service.");
        return false;
    }

    if (profile.dialect == SqlDialect::MySQL) {
        auto isListenerMysqlExe = [](int port) -> bool {
            auto scanFamily = [&](ULONG family) -> DWORD {
                DWORD sz = 0;
                DWORD rc = GetExtendedTcpTable(nullptr, &sz, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
                for (int r = 0; r < 3 && (rc == ERROR_INSUFFICIENT_BUFFER || rc == NO_ERROR) && sz > 0; ++r) {
                    std::vector<BYTE> buf(sz);
                    rc = GetExtendedTcpTable(buf.data(), &sz, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
                    if (rc != NO_ERROR) continue;
                    if (family == AF_INET) {
                        const auto* t = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buf.data());
                        for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                            DWORD p = t->table[i].dwLocalPort;
                            if (static_cast<int>(((p & 0xFF) << 8) | ((p >> 8) & 0xFF)) == port && t->table[i].dwOwningPid > 4) {
                                return t->table[i].dwOwningPid;
                            }
                        }
                    } else {
                        const auto* t6 = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buf.data());
                        for (DWORD i = 0; i < t6->dwNumEntries; ++i) {
                            DWORD p = t6->table[i].dwLocalPort;
                            if (static_cast<int>(((p & 0xFF) << 8) | ((p >> 8) & 0xFF)) == port && t6->table[i].dwOwningPid > 4) {
                                return t6->table[i].dwOwningPid;
                            }
                        }
                    }
                    return 0;
                }
                return 0;
            };
            DWORD pid = scanFamily(AF_INET);
            if (pid == 0) pid = scanFamily(AF_INET6);
            if (pid <= 4) return false;
            HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!hp) return false;
            wchar_t img[MAX_PATH] = {};
            DWORD len = MAX_PATH;
            bool isSql = false;
            if (QueryFullProcessImageNameW(hp, 0, img, &len) && len > 0) {
                std::wstring fn = ToLowerW(fs::path(img).filename().wstring());
                isSql = (fn == L"mysqld.exe" || fn == L"mariadbd.exe");
            }
            CloseHandle(hp);
            return isSql;
        };

        int effPort = (profile.port > 0 && profile.port < 65536) ? profile.port : ResolveDynamicMysqlPort(3306);
        const bool isLocalTarget = (connectHost == L"127.0.0.1" || connectHost == L"::1" || ToLowerW(connectHost) == L"localhost");
        if (IsTcpPortOpen(connectHost, effPort, 35) && (!isLocalTarget || isListenerMysqlExe(effPort))) {
            PersistMysqlPort(effPort);
            if (outResolvedPort) *outResolvedPort = effPort;
            outMessage = L"MySQL is already running (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }

        // 1. Try Windows service if installed (MySQL / MariaDB)
        std::wstring svcErr;
        auto mysqlServices = FindWindowsServicesByPrefix({ L"mysql", L"mariadb" });
        for (const auto& svc : mysqlServices) {
            ControlWindowsServiceState(svc.serviceName, true, 4500, svcErr);
        }
        if (!mysqlServices.empty()) {
            std::vector<int> probePorts = { effPort, 3306, 3307, 3308 };
            ULONGLONG svcDeadline = GetTickCount64() + 3000;
            while (GetTickCount64() < svcDeadline) {
                for (int p : probePorts) {
                    if (IsTcpPortOpen(connectHost, p, 35) && isListenerMysqlExe(p)) {
                        PersistMysqlPort(p);
                        if (outResolvedPort) *outResolvedPort = p;
                        outMessage = L"Started MySQL (" + profile.host + L":" + std::to_wstring(p) + L")";
                        return true;
                    }
                }
                Sleep(100);
            }
        }

        // 2. Spawn bundled or local mysqld.exe on next bindable port (skipping any occupied 3306/3307/etc.)
        std::wstring mysqldExe = FindMysqldExe();
        if (mysqldExe.empty()) {
            outMessage = !svcErr.empty()
                ? svcErr
                : L"mysqld.exe was not found in Phide runtimes, XAMPP, or Windows services.";
            return false;
        }

        std::error_code ec;
        fs::path mysqldPath(mysqldExe);
        fs::path binDir = mysqldPath.parent_path();
        fs::path mysqlDir = (ToLowerW(binDir.filename().wstring()) == L"bin") ? binDir.parent_path() : binDir;

        if (!fs::exists(mysqlDir / L"data", ec) && fs::exists(mysqlDir / L"initial_data", ec)) {
            fs::copy(mysqlDir / L"initial_data", mysqlDir / L"data", fs::copy_options::recursive, ec);
        }

        auto isDirWritable = [&](const fs::path& dir) -> bool {
            if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;
            fs::path probe = dir / (L".phide_wtest_" + std::to_wstring(GetCurrentProcessId()));
            HANDLE hf = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (hf == INVALID_HANDLE_VALUE) return false;
            CloseHandle(hf);
            return true;
        };

        fs::path pidDir = mysqlDir / L"data";
        fs::path appDataPhide;
        wchar_t localApp[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0 && localApp[0] != 0) {
            appDataPhide = fs::path(localApp) / L"phide";
            fs::create_directories(appDataPhide, ec);
            if (fs::exists(appDataPhide, ec)) pidDir = appDataPhide;
        }

        fs::path dataDir = mysqlDir / L"data";
        if ((!fs::exists(dataDir, ec) || !isDirWritable(dataDir)) && !appDataPhide.empty()) {
            fs::path localData = appDataPhide / L"mysql_data";
            if (!fs::exists(localData, ec)) {
                fs::create_directories(localData, ec);
                if (fs::exists(mysqlDir / L"initial_data", ec)) {
                    fs::copy(mysqlDir / L"initial_data", localData, fs::copy_options::recursive, ec);
                } else if (fs::exists(dataDir, ec)) {
                    fs::copy(dataDir, localData, fs::copy_options::recursive, ec);
                }
            }
            if (fs::exists(localData, ec)) dataDir = localData;
        }

        fs::path myIni = binDir / L"my.ini";
        if (!fs::exists(myIni, ec)) myIni = mysqlDir / L"my.ini";

        int candidatePort = effPort;
        for (int portTry = 0; portTry < 6 && candidatePort < 65535; ++portTry) {
            while (!IsPortAvailableForBind(candidatePort) && candidatePort < 65535) {
                ++candidatePort;
            }
            if (candidatePort >= 65535) break;

            fs::path pidFile = pidDir / (L"mysql_" + std::to_wstring(candidatePort) + L".pid");
            std::wostringstream cmd;
            cmd << L"\"" << mysqldExe << L"\"";
            if (fs::exists(myIni, ec)) {
                cmd << L" --defaults-file=\"" << myIni.wstring() << L"\"";
            }
            cmd << L" --port=" << candidatePort
                << L" --pid-file=\"" << pidFile.wstring() << L"\""
                << L" --skip-grant-tables --standalone --console";
            if (fs::exists(dataDir, ec)) {
                std::wstring dataStr = dataDir.wstring();
                cmd << L" --datadir=\"" << dataStr << L"\""
                    << L" --innodb-data-home-dir=\"" << dataStr << L"\""
                    << L" --innodb-log-group-home-dir=\"" << dataStr << L"\"";
            }
            cmd << L" --basedir=\"" << mysqlDir.wstring() << L"\"";

            std::wstring cmdStr = cmd.str();
            std::vector<wchar_t> cmdBuf(cmdStr.begin(), cmdStr.end());
            cmdBuf.push_back(L'\0');

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                nullptr, mysqlDir.wstring().c_str(), &si, &pi)) {
                outMessage = L"Failed to launch mysqld.exe (error " + std::to_wstring(GetLastError()) + L").";
                return false;
            }
            CloseHandle(pi.hThread);

            bool exitedEarly = false;
            ULONGLONG deadline = GetTickCount64() + 4000;
            while (GetTickCount64() < deadline) {
                if (IsTcpPortOpen(L"127.0.0.1", candidatePort, 35)) break;
                if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
                    exitedEarly = true;
                    break;
                }
                Sleep(60);
            }
            CloseHandle(pi.hProcess);

            if (IsTcpPortOpen(L"127.0.0.1", candidatePort, 35)) {
                PersistMysqlPort(candidatePort);
                if (outResolvedPort) *outResolvedPort = candidatePort;
                outMessage = L"Started MySQL (localhost:" + std::to_wstring(candidatePort) + L")";
                return true;
            }
            if (!exitedEarly) {
                break;
            }
            ++candidatePort;
        }
        outMessage = L"MySQL did not start listening on port " + std::to_wstring(candidatePort) + L". Check port or runtime data folder.";
        return false;
    }

    if (profile.dialect == SqlDialect::SQLServer) {
        const int effPort = (profile.port > 0 && profile.port < 65536) ? profile.port : 1433;
        if (outResolvedPort) *outResolvedPort = effPort;
        if (IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"SQL Server is already running (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }
        std::wstring svcErr;
        auto services = FindWindowsServicesByPrefix({ L"mssqlserver", L"mssql$" });
        for (const auto& svc : services) {
            ControlWindowsServiceState(svc.serviceName, true, 5000, svcErr);
        }
        ULONGLONG deadline = GetTickCount64() + 4500;
        while (GetTickCount64() < deadline) {
            if (IsTcpPortOpen(connectHost, effPort, 35)) break;
            Sleep(100);
        }
        if (IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"Started SQL Server (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }
        outMessage = !svcErr.empty() ? svcErr : L"Failed to start SQL Server service.";
        return false;
    }

    return false;
}

bool SqlEngine::StopDatabaseServer(const SqlConnectionProfile& profile, std::wstring& outMessage) {
    outMessage.clear();
    if (profile.dialect == SqlDialect::SQLite) {
        outMessage = L"SQLite is file-based and does not use a server.";
        return true;
    }
    if (!IsLocalHostAddress(profile.host)) {
        outMessage = L"Cannot stop remote host '" + profile.host + L"'.";
        return false;
    }

    const std::wstring connectHost = NormalizeConnectHost(profile.host);

    if (profile.dialect == SqlDialect::PostgreSQL) {
        const int effPort = (profile.port > 0 && profile.port < 65536) ? profile.port : 5432;
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"PostgreSQL is already stopped (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }

        std::wstring svcErr;
        auto pgServices = FindWindowsServicesByPrefix({ L"postgresql", L"postgres" });
        for (const auto& svc : pgServices) {
            if (svc.currentState == SERVICE_RUNNING || svc.currentState == SERVICE_START_PENDING) {
                ControlWindowsServiceState(svc.serviceName, false, 5000, svcErr);
            }
        }

        if (IsTcpPortOpen(connectHost, effPort, 35)) {
            std::wstring pgCtl = FindPgCtlExe();
            if (!pgCtl.empty()) {
                fs::path dataDir;
                wchar_t pgDataEnv[MAX_PATH] = {};
                if (GetEnvironmentVariableW(L"PGDATA", pgDataEnv, MAX_PATH) > 0 && pgDataEnv[0] != 0) {
                    dataDir = pgDataEnv;
                } else {
                    dataDir = fs::path(pgCtl).parent_path().parent_path() / L"data";
                }
                std::error_code ec;
                if (fs::exists(dataDir, ec) && fs::is_directory(dataDir, ec)) {
                    std::string out;
                    DWORD code = 1;
                    RunProcessCapture(pgCtl, L"stop -D \"" + dataDir.wstring() + L"\" -m fast -t 5", "", out, code, {}, 6000);
                }
            }
        }

        ULONGLONG deadline = GetTickCount64() + 4500;
        while (GetTickCount64() < deadline) {
            if (!IsTcpPortOpen(connectHost, effPort, 35)) break;
            Sleep(100);
        }

        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"Stopped PostgreSQL (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }
        outMessage = !svcErr.empty()
            ? svcErr
            : (L"PostgreSQL is still listening on port " + std::to_wstring(effPort) + L".");
        return false;
    }

    if (profile.dialect == SqlDialect::MySQL) {
        const int effPort = (profile.port > 0 && profile.port < 65536) ? profile.port : 3306;
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"MySQL is already stopped (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }

        std::wstring adminExe = FindMysqlAdminExe();
        if (!adminExe.empty()) {
            std::wostringstream args;
            args << L"--host=\"" << SanitizeCliArgW(connectHost) << L"\" "
                 << L"--port=" << effPort << L" "
                 << L"--protocol=TCP "
                 << L"--user=\"" << SanitizeCliArgW(profile.user.empty() ? L"root" : profile.user) << L"\" "
                 << L"shutdown";
            std::vector<std::pair<std::wstring, std::wstring>> envs;
            if (!profile.password.empty()) envs.emplace_back(L"MYSQL_PWD", profile.password);
            std::string out;
            DWORD code = 1;
            RunProcessCapture(adminExe, args.str(), "", out, code, envs, 4000);
        }

        ULONGLONG deadline = GetTickCount64() + 2500;
        while (GetTickCount64() < deadline) {
            if (!IsTcpPortOpen(connectHost, effPort, 35)) break;
            Sleep(80);
        }

        std::wstring svcErr;
        if (IsTcpPortOpen(connectHost, effPort, 35)) {
            auto mysqlServices = FindWindowsServicesByPrefix({ L"mysql", L"mariadb" });
            for (const auto& svc : mysqlServices) {
                if (svc.currentState == SERVICE_RUNNING || svc.currentState == SERVICE_START_PENDING) {
                    ControlWindowsServiceState(svc.serviceName, false, 4500, svcErr);
                }
            }
        }

        if (IsTcpPortOpen(connectHost, effPort, 35)) {
            TerminateLocalProcessListeningOnPort(effPort, { L"mysqld.exe", L"mariadbd.exe" });
            deadline = GetTickCount64() + 2000;
            while (GetTickCount64() < deadline) {
                if (!IsTcpPortOpen(connectHost, effPort, 35)) break;
                Sleep(80);
            }
        }

        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"Stopped MySQL (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }
        outMessage = !svcErr.empty()
            ? svcErr
            : (L"MySQL is still listening on port " + std::to_wstring(effPort) + L".");
        return false;
    }

    if (profile.dialect == SqlDialect::SQLServer) {
        const int effPort = (profile.port > 0 && profile.port < 65536) ? profile.port : 1433;
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"SQL Server is already stopped (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }
        std::wstring svcErr;
        auto services = FindWindowsServicesByPrefix({ L"mssqlserver", L"mssql$" });
        for (const auto& svc : services) {
            if (svc.currentState == SERVICE_RUNNING || svc.currentState == SERVICE_START_PENDING) {
                ControlWindowsServiceState(svc.serviceName, false, 5000, svcErr);
            }
        }
        ULONGLONG deadline = GetTickCount64() + 4500;
        while (GetTickCount64() < deadline) {
            if (!IsTcpPortOpen(connectHost, effPort, 35)) break;
            Sleep(100);
        }
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outMessage = L"Stopped SQL Server (" + profile.host + L":" + std::to_wstring(effPort) + L")";
            return true;
        }
        outMessage = !svcErr.empty() ? svcErr : L"Failed to stop SQL Server service.";
        return false;
    }

    return false;
}

std::vector<std::wstring> SqlEngine::ListDatabases(const SqlConnectionProfile& profile, std::wstring& outError) {
    outError.clear();
    std::vector<std::wstring> dbs;

    if (profile.dialect == SqlDialect::SQLite) {
        if (!profile.sqlitePath.empty()) {
            dbs.push_back(fs::path(profile.sqlitePath).filename().wstring());
        }
        return dbs;
    }

    if (profile.dialect == SqlDialect::MySQL) {
        int effPort = profile.port > 0 ? profile.port : 3306;
        std::wstring connectHost = NormalizeConnectHost(profile.host);
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outError = L"MySQL server is not reachable at " + profile.host + L":" + std::to_wstring(effPort);
            return dbs;
        }
        std::wstring mysqlExe = FindMysqlExe();
        if (mysqlExe.empty()) {
            outError = L"mysql.exe not found in runtimes or PATH.";
            return dbs;
        }
        std::wostringstream args;
        args << L"--host=\"" << SanitizeCliArgW(connectHost) << L"\" "
             << L"--port=" << effPort << L" "
             << L"--protocol=TCP --connect-timeout=2 "
             << L"--user=\"" << SanitizeCliArgW(profile.user.empty() ? L"root" : profile.user) << L"\" "
             << L"-N --batch --raw --default-character-set=utf8mb4";
        std::string out;
        DWORD exitCode = 1;
        std::vector<std::pair<std::wstring, std::wstring>> envs;
        if (!profile.password.empty()) envs.emplace_back(L"MYSQL_PWD", profile.password);
        const std::string q = "SELECT SCHEMA_NAME FROM information_schema.schemata "
                              "WHERE SCHEMA_NAME NOT IN ('information_schema','performance_schema','mysql','sys','phpmyadmin') "
                              "ORDER BY SCHEMA_NAME;";
        if (!RunProcessCapture(mysqlExe, args.str(), q, out, exitCode, envs) || exitCode != 0) {
            outError = out.empty() ? L"Failed to list MySQL databases." : TrimW(Utf8ToWide(out));
            return dbs;
        }
        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::wstring w = TrimW(Utf8ToWide(line));
            if (!w.empty()) dbs.push_back(std::move(w));
        }
        return dbs;
    }

    if (profile.dialect == SqlDialect::PostgreSQL) {
        int effPort = profile.port > 0 ? profile.port : 5432;
        std::wstring connectHost = NormalizeConnectHost(profile.host);
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outError = L"PostgreSQL server is not reachable at " + profile.host + L":" + std::to_wstring(effPort);
            return dbs;
        }
        std::wstring psqlExe = FindPsqlExe();
        if (psqlExe.empty()) {
            outError = L"psql.exe not found in Program Files or PATH.";
            return dbs;
        }
        std::wostringstream args;
        args << L"-h \"" << SanitizeCliArgW(connectHost) << L"\" "
             << L"-p " << effPort << L" "
             << L"-U \"" << SanitizeCliArgW(profile.user.empty() ? L"postgres" : profile.user) << L"\" "
             << L"-d \"" << SanitizeCliArgW(profile.database.empty() ? L"postgres" : profile.database) << L"\" "
             << L"-w -X -t -A -P footer=off";
        std::string out;
        DWORD exitCode = 1;
        std::vector<std::pair<std::wstring, std::wstring>> envs = {
            { L"PGCONNECT_TIMEOUT", L"2" }
        };
        if (!profile.password.empty()) envs.emplace_back(L"PGPASSWORD", profile.password);
        const std::string q = "SELECT datname FROM pg_database WHERE datistemplate = false ORDER BY datname;\n";
        if (!RunProcessCapture(psqlExe, args.str(), q, out, exitCode, envs) || exitCode != 0) {
            outError = out.empty() ? L"Failed to list PostgreSQL databases." : TrimW(Utf8ToWide(out));
            return dbs;
        }
        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::wstring w = TrimW(Utf8ToWide(line));
            if (!w.empty()) dbs.push_back(std::move(w));
        }
        return dbs;
    }

    if (profile.dialect == SqlDialect::SQLServer) {
        int effPort = profile.port > 0 ? profile.port : 1433;
        std::wstring connectHost = NormalizeConnectHost(profile.host);
        if (!IsTcpPortOpen(connectHost, effPort, 35)) return dbs;
        std::wstring sqlcmdExe = FindSqlCmdExe();
        if (sqlcmdExe.empty()) return dbs;
        std::wostringstream args;
        args << L"-S \"" << SanitizeCliArgW(connectHost) << L"," << effPort << L"\" -h -1 -W -l 2 ";
        if (profile.user.empty()) args << L"-E ";
        else args << L"-U \"" << SanitizeCliArgW(profile.user) << L"\" -P \"" << SanitizeCliArgW(profile.password) << L"\" ";
        args << L"-Q \"SET NOCOUNT ON; SELECT name FROM sys.databases WHERE database_id > 4 ORDER BY name;\"";
        std::string out;
        DWORD exitCode = 1;
        if (RunProcessCapture(sqlcmdExe, args.str(), "", out, exitCode) && exitCode == 0) {
            std::istringstream iss(out);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                std::wstring w = TrimW(Utf8ToWide(line));
                if (!w.empty()) dbs.push_back(std::move(w));
            }
        }
    }

    return dbs;
}

SqlQueryResult SqlEngine::ExecuteQuery(const SqlConnectionProfile& profile, const std::wstring& sqlText) {
    SqlQueryResult res;
    auto t0 = std::chrono::steady_clock::now();

    std::wstring trimmed = TrimW(sqlText);
    if (trimmed.empty()) {
        res.errorMessage = L"Empty SQL query.";
        return res;
    }

    if (profile.dialect == SqlDialect::SQLite) {
        const SqliteApi& api = GetSqliteApi();
        if (!api.open_v2 || !api.prepare_v2 || !api.step || !api.finalize || !api.close_fn) {
            res.errorMessage = L"Native sqlite3 / libsqlite3.dll could not be loaded.";
            return res;
        }

        std::string dbUtf8 = profile.sqlitePath.empty() ? ":memory:" : WideToUtf8(profile.sqlitePath);
        void* db = nullptr;
        if (api.open_v2(dbUtf8.c_str(), &db, SQLITE_OPEN_READWRITE_CREATE, nullptr) != SQLITE_OK_CODE || !db) {
            res.errorMessage = (db && api.errmsg) ? Utf8ToWide(api.errmsg(db)) : L"Failed to open SQLite database.";
            if (db) api.close_fn(db);
            return res;
        }

        std::string sqlUtf8 = WideToUtf8(sqlText);
        const char* cursor = sqlUtf8.c_str();
        const char* endPtr = cursor + sqlUtf8.size();
        int64_t totalChanges = 0;

        while (cursor && cursor < endPtr) {
            while (cursor < endPtr && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')) ++cursor;
            if (cursor >= endPtr) break;

            void* stmt = nullptr;
            const char* tail = nullptr;
            int rc = api.prepare_v2(db, cursor, static_cast<int>(endPtr - cursor), &stmt, &tail);
            if (rc != SQLITE_OK_CODE) {
                res.success = false;
                res.errorMessage = api.errmsg ? Utf8ToWide(api.errmsg(db)) : L"SQL syntax error.";
                if (stmt) api.finalize(stmt);
                api.close_fn(db);
                auto t1 = std::chrono::steady_clock::now();
                res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
                return res;
            }
            if (!stmt) {
                cursor = (tail && tail > cursor) ? tail : endPtr;
                continue;
            }

            int colCount = api.column_count ? api.column_count(stmt) : 0;
            std::vector<std::wstring> stmtCols;
            std::vector<std::vector<std::wstring>> stmtRows;
            if (colCount > 0) {
                for (int c = 0; c < colCount; ++c) {
                    const char* cn = api.column_name ? api.column_name(stmt, c) : nullptr;
                    stmtCols.push_back(cn ? Utf8ToWide(cn) : (L"col" + std::to_wstring(c)));
                }
            }

            int stepRc = SQLITE_DONE_CODE;
            size_t safetyCap = 0;
            while ((stepRc = api.step(stmt)) == SQLITE_ROW_CODE && safetyCap < 10000) {
                ++safetyCap;
                std::vector<std::wstring> row;
                row.reserve(colCount);
                for (int c = 0; c < colCount; ++c) {
                    int ctype = api.column_type ? api.column_type(stmt, c) : 0;
                    if (ctype == SQLITE_NULL_TYPE) {
                        row.push_back(L"NULL");
                    } else {
                        const unsigned char* txt = api.column_text ? api.column_text(stmt, c) : nullptr;
                        row.push_back(txt ? Utf8ToWide(reinterpret_cast<const char*>(txt)) : L"");
                    }
                }
                stmtRows.push_back(std::move(row));
            }

            if (stepRc != SQLITE_DONE_CODE && stepRc != SQLITE_ROW_CODE) {
                res.success = false;
                res.errorMessage = api.errmsg ? Utf8ToWide(api.errmsg(db)) : L"SQLite execution error.";
                api.finalize(stmt);
                api.close_fn(db);
                auto t1 = std::chrono::steady_clock::now();
                res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
                return res;
            }

            if (colCount > 0) {
                res.isSelect = true;
                res.columns = std::move(stmtCols);
                res.rows = std::move(stmtRows);
            } else if (api.changes) {
                totalChanges += api.changes(db);
            }

            api.finalize(stmt);
            cursor = (tail && tail > cursor) ? tail : endPtr;
        }

        api.close_fn(db);
        res.success = true;
        res.affectedRows = totalChanges;
        auto t1 = std::chrono::steady_clock::now();
        res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (res.isSelect) {
            res.statusMessage = L"Returned " + std::to_wstring(res.rows.size()) + L" row(s)";
        } else {
            res.statusMessage = L"Executed OK (" + std::to_wstring(res.affectedRows) + L" row(s) affected)";
        }
        return res;
    }

    if (profile.dialect == SqlDialect::MySQL) {
        std::wstring connectHost = NormalizeConnectHost(profile.host);
        int effPort = profile.port > 0 ? profile.port : ResolveDynamicMysqlPort(3306);
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            auto t1 = std::chrono::steady_clock::now();
            res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            res.errorMessage = L"MySQL server is not running on " +
                               (profile.host.empty() ? L"localhost" : profile.host) +
                               L":" + std::to_wstring(effPort) +
                               L". Click 'Detect Servers' or start MySQL.";
            return res;
        }

        std::wstring mysqlExe = FindMysqlExe();
        if (mysqlExe.empty()) {
            res.errorMessage = L"mysql.exe not found in runtimes or PATH.";
            return res;
        }

        auto runMysql = [&](const std::wstring& dbArg, std::string& output, DWORD& exitCode) -> bool {
            std::wostringstream args;
            args << L"--host=\"" << SanitizeCliArgW(connectHost) << L"\" "
                 << L"--port=" << effPort << L" "
                 << L"--protocol=TCP --connect-timeout=2 "
                 << L"--user=\"" << SanitizeCliArgW(profile.user.empty() ? L"root" : profile.user) << L"\" "
                 << L"--batch --raw --default-character-set=utf8mb4 ";
            if (!dbArg.empty()) args << L"\"" << SanitizeCliArgW(dbArg) << L"\"";
            std::vector<std::pair<std::wstring, std::wstring>> envs;
            if (!profile.password.empty()) envs.emplace_back(L"MYSQL_PWD", profile.password);
            return RunProcessCapture(mysqlExe, args.str(), WideToUtf8(sqlText), output, exitCode, envs);
        };

        std::string output;
        DWORD exitCode = 1;
        bool launched = runMysql(profile.database, output, exitCode);

        // Retry without bad DB argument on ERROR 1049 (Unknown database), or with an existing schema on ERROR 1046 (No database selected)
        if (launched && exitCode != 0) {
            if (!profile.database.empty() && output.find("ERROR 1049") != std::string::npos) {
                output.clear();
                launched = runMysql(L"", output, exitCode);
            }
            if (launched && exitCode != 0 && output.find("ERROR 1046") != std::string::npos) {
                std::wstring listErr;
                auto dbs = ListDatabases(profile, listErr);
                if (!dbs.empty()) {
                    output.clear();
                    launched = runMysql(dbs.front(), output, exitCode);
                }
            }
        }

        auto t1 = std::chrono::steady_clock::now();
        res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (!launched) {
            res.errorMessage = L"Failed to invoke mysql.exe process or timed out.";
            return res;
        }
        if (exitCode != 0) {
            res.errorMessage = TrimW(Utf8ToWide(output));
            return res;
        }

        std::istringstream iss(output);
        std::string line;
        bool gotHeader = false;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            auto cells = SplitTabRow(line);
            if (!gotHeader) {
                res.columns = std::move(cells);
                gotHeader = true;
                res.isSelect = true;
            } else {
                res.rows.push_back(std::move(cells));
            }
        }
        res.success = true;
        res.statusMessage = res.isSelect
            ? (L"Returned " + std::to_wstring(res.rows.size()) + L" row(s)")
            : L"Executed OK on MySQL";
        return res;
    }

    if (profile.dialect == SqlDialect::PostgreSQL) {
        std::wstring connectHost = NormalizeConnectHost(profile.host);
        int effPort = profile.port > 0 ? profile.port : 5432;
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            auto t1 = std::chrono::steady_clock::now();
            res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            res.errorMessage = L"PostgreSQL server is not running on " +
                               (profile.host.empty() ? L"localhost" : profile.host) +
                               L":" + std::to_wstring(effPort) + L".";
            return res;
        }

        std::wstring psqlExe = FindPsqlExe();
        if (psqlExe.empty()) {
            res.errorMessage = L"psql.exe not found in Program Files or PATH.";
            return res;
        }
        std::wostringstream args;
        args << L"-h \"" << SanitizeCliArgW(connectHost) << L"\" "
             << L"-p " << effPort << L" "
             << L"-U \"" << SanitizeCliArgW(profile.user.empty() ? L"postgres" : profile.user) << L"\" "
             << L"-d \"" << SanitizeCliArgW(profile.database.empty() ? L"postgres" : profile.database) << L"\" "
             << L"-w -X -A -F \"\t\" -P footer=off";

        std::string output;
        DWORD exitCode = 1;
        std::vector<std::pair<std::wstring, std::wstring>> envs = {
            { L"PGCONNECT_TIMEOUT", L"2" }
        };
        if (!profile.password.empty()) envs.emplace_back(L"PGPASSWORD", profile.password);
        std::string sqlUtf8 = WideToUtf8(sqlText);
        if (!sqlUtf8.empty() && sqlUtf8.back() != '\n') sqlUtf8.push_back('\n');

        bool launched = RunProcessCapture(psqlExe, args.str(), sqlUtf8, output, exitCode, envs);
        auto t1 = std::chrono::steady_clock::now();
        res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (!launched || exitCode != 0) {
            std::wstring errW = TrimW(Utf8ToWide(output));
            if (errW.find(L"no password supplied") != std::wstring::npos ||
                errW.find(L"password authentication failed") != std::wstring::npos) {
                errW += L" (Configure PGPASSWORD / .env DB_PASSWORD)";
            }
            res.errorMessage = errW.empty() ? L"psql execution failed." : errW;
            return res;
        }

        const bool expectResult = SqlTextStartsWithResultQuery(trimmed);
        std::istringstream iss(output);
        std::string line;
        bool gotHeader = false;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (!expectResult && line.find('\t') == std::string::npos) {
                res.statusMessage = TrimW(Utf8ToWide(line));
                continue;
            }
            auto cells = SplitTabRow(line);
            if (!gotHeader) {
                res.columns = std::move(cells);
                gotHeader = true;
                res.isSelect = true;
            } else {
                res.rows.push_back(std::move(cells));
            }
        }
        res.success = true;
        if (res.isSelect) {
            res.statusMessage = L"Returned " + std::to_wstring(res.rows.size()) + L" row(s)";
        } else if (res.statusMessage.empty()) {
            res.statusMessage = L"Executed OK on PostgreSQL";
        }
        return res;
    }

    if (profile.dialect == SqlDialect::SQLServer) {
        std::wstring connectHost = NormalizeConnectHost(profile.host);
        int effPort = profile.port > 0 ? profile.port : 1433;
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            auto t1 = std::chrono::steady_clock::now();
            res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            res.errorMessage = L"SQL Server is not running on " +
                               (profile.host.empty() ? L"localhost" : profile.host) +
                               L":" + std::to_wstring(effPort) + L".";
            return res;
        }
        std::wstring sqlcmdExe = FindSqlCmdExe();
        if (sqlcmdExe.empty()) {
            res.errorMessage = L"sqlcmd.exe not found.";
            return res;
        }
        std::wostringstream args;
        args << L"-S \"" << SanitizeCliArgW(connectHost) << L"," << effPort << L"\" -s \"\t\" -W -l 2 ";
        if (profile.user.empty()) args << L"-E ";
        else args << L"-U \"" << SanitizeCliArgW(profile.user) << L"\" -P \"" << SanitizeCliArgW(profile.password) << L"\" ";
        if (!profile.database.empty()) args << L"-d \"" << SanitizeCliArgW(profile.database) << L"\" ";
        args << L"-Q \"" << SanitizeCliArgW(sqlText) << L"\"";

        std::string output;
        DWORD exitCode = 1;
        bool launched = RunProcessCapture(sqlcmdExe, args.str(), "", output, exitCode);
        auto t1 = std::chrono::steady_clock::now();
        res.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (!launched || exitCode != 0) {
            res.errorMessage = output.empty() ? L"sqlcmd execution failed." : TrimW(Utf8ToWide(output));
            return res;
        }
        std::istringstream iss(output);
        std::string line;
        bool gotHeader = false;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line.find("---") == 0 && line.find_first_not_of("-\t ") == std::string::npos) continue;
            auto cells = SplitTabRow(line);
            if (!gotHeader) {
                res.columns = std::move(cells);
                gotHeader = true;
                res.isSelect = true;
            } else {
                res.rows.push_back(std::move(cells));
            }
        }
        res.success = true;
        res.statusMessage = res.isSelect
            ? (L"Returned " + std::to_wstring(res.rows.size()) + L" row(s)")
            : L"Executed OK on SQL Server";
        return res;
    }

    res.errorMessage = L"Unsupported SQL dialect.";
    return res;
}

namespace {

// Marks a column `isUnique = true` in a table using case-insensitive matching.
static void MarkColumnUnique(SqlTableInfo& tbl, const std::wstring& colName) {
    for (auto& col : tbl.columns) {
        if (_wcsicmp(col.name.c_str(), colName.c_str()) == 0) {
            col.isUnique = true;
            break;
        }
    }
}

// Marks a column `isPrimaryKey = true` (and implies `notNull = true`) in a table.
static void MarkColumnPrimaryKey(SqlTableInfo& tbl, const std::wstring& colName) {
    for (auto& col : tbl.columns) {
        if (_wcsicmp(col.name.c_str(), colName.c_str()) == 0) {
            col.isPrimaryKey = true;
            col.notNull = true;
            break;
        }
    }
}

} // namespace

std::vector<SqlTableInfo> SqlEngine::InspectSchema(const SqlConnectionProfile& profile, std::wstring& outError) {
    outError.clear();
    std::vector<SqlTableInfo> tables;

    if (profile.dialect == SqlDialect::SQLite) {
        SqlQueryResult tRes = ExecuteQuery(
            profile,
            L"SELECT name FROM sqlite_master WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' ORDER BY name;");
        if (!tRes.success) {
            outError = tRes.errorMessage;
            return tables;
        }
        for (const auto& r : tRes.rows) {
            if (r.empty()) continue;
            SqlTableInfo t;
            t.name = r[0];
            SqlQueryResult cRes = ExecuteQuery(profile, L"PRAGMA table_info(" + QuoteSqlIdent(t.name) + L");");
            if (cRes.success) {
                for (const auto& cr : cRes.rows) {
                    if (cr.size() >= 6) {
                        SqlColumnInfo ci;
                        ci.name = cr[1];
                        ci.type = cr[2];
                        ci.notNull = (cr[3] == L"1");
                        ci.isPrimaryKey = (cr[5] != L"0");
                        t.columns.push_back(std::move(ci));
                    }
                }
            }
            // Detect single-column UNIQUE indexes/constraints (never composite multi-column indexes)
            SqlQueryResult idxListRes = ExecuteQuery(profile, L"PRAGMA index_list(" + QuoteSqlIdent(t.name) + L");");
            if (idxListRes.success) {
                for (const auto& ir : idxListRes.rows) {
                    if (ir.size() >= 3 && ir[2] == L"1") {
                        SqlQueryResult idxInfoRes = ExecuteQuery(profile, L"PRAGMA index_info(" + QuoteSqlIdent(ir[1]) + L");");
                        if (idxInfoRes.success && idxInfoRes.rows.size() == 1 && idxInfoRes.rows[0].size() >= 3) {
                            MarkColumnUnique(t, idxInfoRes.rows[0][2]);
                        }
                    }
                }
            }
            SqlQueryResult fkRes = ExecuteQuery(profile, L"PRAGMA foreign_key_list(" + QuoteSqlIdent(t.name) + L");");
            if (fkRes.success) {
                for (const auto& fkr : fkRes.rows) {
                    if (fkr.size() >= 5) {
                        SqlForeignKeyInfo fk;
                        fk.refTable = fkr[2];
                        fk.column = fkr[3];
                        fk.refColumn = fkr[4];
                        t.foreignKeys.push_back(std::move(fk));
                    }
                }
            }
            SqlQueryResult cntRes = ExecuteQuery(profile, L"SELECT COUNT(*) FROM " + QuoteSqlIdent(t.name) + L";");
            if (cntRes.success && !cntRes.rows.empty() && !cntRes.rows[0].empty()) {
                try { t.rowCount = std::stoll(cntRes.rows[0][0]); } catch (...) {}
            }
            tables.push_back(std::move(t));
        }
        return tables;
    }

    if (profile.dialect == SqlDialect::MySQL) {
        std::wstring connectHost = NormalizeConnectHost(profile.host);
        int effPort = profile.port > 0 ? profile.port : 3306;
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outError = L"Can't connect to MySQL server on " +
                       (profile.host.empty() ? L"localhost" : profile.host) +
                       L":" + std::to_wstring(effPort);
            return tables;
        }

        // Query without forcing profile.database in CLI args so missing default DB never blocks inspection
        SqlConnectionProfile serverProf = profile;
        serverProf.database.clear();

        std::wostringstream sql;
        sql << L"SELECT t.TABLE_SCHEMA, t.TABLE_NAME, IFNULL(t.TABLE_ROWS, 0), "
            << L"IFNULL(c.COLUMN_NAME, ''), IFNULL(c.COLUMN_TYPE, ''), IFNULL(c.IS_NULLABLE, 'YES'), IFNULL(c.COLUMN_KEY, '') "
            << L"FROM information_schema.tables t "
            << L"LEFT JOIN information_schema.columns c "
            << L"  ON c.TABLE_SCHEMA = t.TABLE_SCHEMA AND c.TABLE_NAME = t.TABLE_NAME "
            << L"WHERE t.TABLE_SCHEMA NOT IN ('information_schema','performance_schema','mysql','sys','phpmyadmin') ";
        if (!profile.database.empty()) {
            sql << L"AND t.TABLE_SCHEMA = '" << EscapeSqlLiteral(profile.database) << L"' ";
        }
        sql << L"ORDER BY t.TABLE_SCHEMA, t.TABLE_NAME, c.ORDINAL_POSITION LIMIT 8000;";

        SqlQueryResult qRes = ExecuteQuery(serverProf, sql.str());
        if (!qRes.success) {
            outError = qRes.errorMessage;
            return tables;
        }

        // Only fall back to inspecting all schemas if the requested database does not exist at all
        // (if the selected database exists and legitimately has 0 tables, return an empty table list).
        if (qRes.rows.empty() && !profile.database.empty()) {
            bool dbExists = false;
            for (const auto& avail : profile.availableDatabases) {
                if (_wcsicmp(avail.c_str(), profile.database.c_str()) == 0) {
                    dbExists = true;
                    break;
                }
            }
            if (!dbExists) {
                SqlQueryResult chkRes = ExecuteQuery(
                    serverProf,
                    L"SELECT SCHEMA_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = '" +
                    EscapeSqlLiteral(profile.database) + L"' LIMIT 1;");
                if (chkRes.success && !chkRes.rows.empty()) {
                    dbExists = true;
                }
            }
            if (!dbExists) {
                SqlQueryResult allRes = ExecuteQuery(
                    serverProf,
                    L"SELECT t.TABLE_SCHEMA, t.TABLE_NAME, IFNULL(t.TABLE_ROWS, 0), "
                    L"IFNULL(c.COLUMN_NAME, ''), IFNULL(c.COLUMN_TYPE, ''), IFNULL(c.IS_NULLABLE, 'YES'), IFNULL(c.COLUMN_KEY, '') "
                    L"FROM information_schema.tables t "
                    L"LEFT JOIN information_schema.columns c "
                    L"  ON c.TABLE_SCHEMA = t.TABLE_SCHEMA AND c.TABLE_NAME = t.TABLE_NAME "
                    L"WHERE t.TABLE_SCHEMA NOT IN ('information_schema','performance_schema','mysql','sys','phpmyadmin') "
                    L"ORDER BY t.TABLE_SCHEMA, t.TABLE_NAME, c.ORDINAL_POSITION LIMIT 8000;");
                if (allRes.success) qRes = std::move(allRes);
            }
        }

        std::map<std::pair<std::wstring, std::wstring>, size_t> tableIndexMap;
        for (const auto& r : qRes.rows) {
            if (r.size() < 7) continue;
            const std::wstring& schemaName = r[0];
            const std::wstring& tblName = r[1];
            if (tblName.empty()) continue;

            std::pair<std::wstring, std::wstring> key{ schemaName, tblName };
            auto it = tableIndexMap.find(key);
            if (it == tableIndexMap.end()) {
                SqlTableInfo t;
                t.schemaName = schemaName;
                t.name = tblName;
                try { t.rowCount = std::stoll(r[2]); } catch (...) { t.rowCount = 0; }
                size_t idx = tables.size();
                tableIndexMap.emplace(std::move(key), idx);
                tables.push_back(std::move(t));
                it = tableIndexMap.find({ schemaName, tblName });
            }

            if (!r[3].empty()) {
                SqlColumnInfo ci;
                ci.name = r[3];
                ci.type = r[4];
                ci.notNull = (ToLowerW(r[5]) == L"no");
                ci.isPrimaryKey = (r[6] == L"PRI");
                ci.isUnique = (r[6] == L"UNI");
                tables[it->second].columns.push_back(std::move(ci));
            }
        }
        // Single-column UNIQUE indexes from STATISTICS (catches UNIQUE indexes not tagged UNI in COLUMN_KEY)
        if (!tables.empty()) {
            std::wostringstream uqSql;
            uqSql << L"SELECT TABLE_SCHEMA, TABLE_NAME, MAX(COLUMN_NAME) "
                  << L"FROM information_schema.STATISTICS "
                  << L"WHERE NON_UNIQUE = 0 AND INDEX_NAME <> 'PRIMARY' "
                  << L"  AND TABLE_SCHEMA NOT IN ('information_schema','performance_schema','mysql','sys','phpmyadmin') ";
            if (!profile.database.empty()) {
                uqSql << L"  AND TABLE_SCHEMA = '" << EscapeSqlLiteral(profile.database) << L"' ";
            }
            uqSql << L"GROUP BY TABLE_SCHEMA, TABLE_NAME, INDEX_NAME HAVING COUNT(*) = 1 LIMIT 4000;";
            SqlQueryResult uqRes = ExecuteQuery(serverProf, uqSql.str());
            if (uqRes.success) {
                for (const auto& ur : uqRes.rows) {
                    if (ur.size() < 3) continue;
                    auto it = tableIndexMap.find({ ur[0], ur[1] });
                    if (it != tableIndexMap.end()) {
                        MarkColumnUnique(tables[it->second], ur[2]);
                    }
                }
            }
        }
        // Foreign Keys for MySQL
        {
            std::wostringstream fkSql;
            fkSql << L"SELECT TABLE_SCHEMA, TABLE_NAME, COLUMN_NAME, REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME "
                  << L"FROM information_schema.KEY_COLUMN_USAGE "
                  << L"WHERE REFERENCED_TABLE_NAME IS NOT NULL ";
            if (!profile.database.empty()) {
                fkSql << L"AND TABLE_SCHEMA = '" << EscapeSqlLiteral(profile.database) << L"' ";
            }
            fkSql << L"LIMIT 2000;";
            SqlQueryResult fkRes = ExecuteQuery(serverProf, fkSql.str());
            if (fkRes.success) {
                for (const auto& fkr : fkRes.rows) {
                    if (fkr.size() < 5) continue;
                    auto it = tableIndexMap.find({ fkr[0], fkr[1] });
                    if (it != tableIndexMap.end()) {
                        tables[it->second].foreignKeys.push_back({ fkr[2], fkr[3], fkr[4] });
                    }
                }
            }
        }
        return tables;
    }

    if (profile.dialect == SqlDialect::PostgreSQL) {
        std::wstring connectHost = NormalizeConnectHost(profile.host);
        int effPort = profile.port > 0 ? profile.port : 5432;
        if (!IsTcpPortOpen(connectHost, effPort, 35)) {
            outError = L"Can't connect to PostgreSQL server on " +
                       (profile.host.empty() ? L"localhost" : profile.host) +
                       L":" + std::to_wstring(effPort);
            return tables;
        }

        const std::wstring sql =
            L"SELECT c.table_schema, c.table_name, COALESCE(s.n_live_tup, 0), "
            L"c.column_name, c.data_type, c.is_nullable, "
            L"CASE WHEN EXISTS ("
            L"  SELECT 1 FROM information_schema.table_constraints tc "
            L"  JOIN information_schema.key_column_usage kcu "
            L"    ON tc.constraint_name = kcu.constraint_name "
            L"   AND tc.table_schema = kcu.table_schema "
            L"   AND tc.table_name = kcu.table_name "
            L"  WHERE tc.constraint_type = 'PRIMARY KEY' "
            L"    AND tc.table_schema = c.table_schema "
            L"    AND tc.table_name = c.table_name "
            L"    AND kcu.column_name = c.column_name"
            L") THEN 'PRI' ELSE '' END "
            L"FROM information_schema.columns c "
            L"LEFT JOIN pg_stat_user_tables s "
            L"  ON s.schemaname = c.table_schema AND s.relname = c.table_name "
            L"WHERE c.table_schema NOT IN ('pg_catalog','information_schema') "
            L"ORDER BY c.table_schema, c.table_name, c.ordinal_position LIMIT 4000;";

        SqlQueryResult qRes = ExecuteQuery(profile, sql);
        if (!qRes.success) {
            outError = qRes.errorMessage;
            return tables;
        }

        std::map<std::pair<std::wstring, std::wstring>, size_t> tableIndexMap;
        for (const auto& r : qRes.rows) {
            if (r.size() < 7) continue;
            const std::wstring& schemaName = r[0];
            const std::wstring& tblName = r[1];
            if (tblName.empty()) continue;
            std::pair<std::wstring, std::wstring> key{ schemaName, tblName };
            auto it = tableIndexMap.find(key);
            if (it == tableIndexMap.end()) {
                SqlTableInfo t;
                t.schemaName = schemaName;
                t.name = tblName;
                try { t.rowCount = std::stoll(r[2]); } catch (...) { t.rowCount = 0; }
                size_t idx = tables.size();
                tableIndexMap.emplace(std::move(key), idx);
                tables.push_back(std::move(t));
                it = tableIndexMap.find({ schemaName, tblName });
            }
            if (!r[3].empty()) {
                SqlColumnInfo ci;
                ci.name = r[3];
                ci.type = r[4];
                ci.notNull = (ToLowerW(r[5]) == L"no");
                ci.isPrimaryKey = (r[6] == L"PRI");
                tables[it->second].columns.push_back(std::move(ci));
            }
        }
        // Single-column UNIQUE indexes/constraints for PostgreSQL (for accurate 1:1 detection)
        if (!tables.empty()) {
            const std::wstring uqSql =
                L"SELECT n.nspname, cls.relname, a.attname "
                L"FROM pg_index ix "
                L"JOIN pg_class cls ON cls.oid = ix.indrelid "
                L"JOIN pg_namespace n ON n.oid = cls.relnamespace "
                L"JOIN pg_attribute a ON a.attrelid = cls.oid AND a.attnum = ix.indkey[0] "
                L"WHERE ix.indisunique AND NOT ix.indisprimary AND ix.indnatts = 1 "
                L"  AND n.nspname NOT IN ('pg_catalog','information_schema') LIMIT 4000;";
            SqlQueryResult uqRes = ExecuteQuery(profile, uqSql);
            if (uqRes.success) {
                for (const auto& ur : uqRes.rows) {
                    if (ur.size() < 3) continue;
                    auto it = tableIndexMap.find({ ur[0], ur[1] });
                    if (it != tableIndexMap.end()) {
                        MarkColumnUnique(tables[it->second], ur[2]);
                    }
                }
            }
        }
        // Foreign Keys for PostgreSQL
        {
            const std::wstring fkSql =
                L"SELECT tc.table_schema, tc.table_name, kcu.column_name, ccu.table_name, ccu.column_name "
                L"FROM information_schema.table_constraints tc "
                L"JOIN information_schema.key_column_usage kcu ON tc.constraint_name = kcu.constraint_name AND tc.table_schema = kcu.table_schema "
                L"JOIN information_schema.constraint_column_usage ccu ON ccu.constraint_name = tc.constraint_name AND ccu.table_schema = tc.table_schema "
                L"WHERE tc.constraint_type = 'FOREIGN KEY' AND tc.table_schema NOT IN ('pg_catalog','information_schema') LIMIT 2000;";
            SqlQueryResult fkRes = ExecuteQuery(profile, fkSql);
            if (fkRes.success) {
                for (const auto& fkr : fkRes.rows) {
                    if (fkr.size() < 5) continue;
                    auto it = tableIndexMap.find({ fkr[0], fkr[1] });
                    if (it != tableIndexMap.end()) {
                        tables[it->second].foreignKeys.push_back({ fkr[2], fkr[3], fkr[4] });
                    }
                }
            }
        }
        return tables;
    }

    if (profile.dialect == SqlDialect::SQLServer) {
        SqlQueryResult qRes = ExecuteQuery(
            profile,
            L"SET NOCOUNT ON; "
            L"SELECT TABLE_SCHEMA, TABLE_NAME, COLUMN_NAME, DATA_TYPE, IS_NULLABLE "
            L"FROM INFORMATION_SCHEMA.COLUMNS ORDER BY TABLE_SCHEMA, TABLE_NAME, ORDINAL_POSITION;");
        if (!qRes.success) {
            outError = qRes.errorMessage;
            return tables;
        }
        std::map<std::pair<std::wstring, std::wstring>, size_t> tableIndexMap;
        for (const auto& r : qRes.rows) {
            if (r.size() < 5) continue;
            std::pair<std::wstring, std::wstring> key{ r[0], r[1] };
            auto it = tableIndexMap.find(key);
            if (it == tableIndexMap.end()) {
                SqlTableInfo t;
                t.schemaName = r[0];
                t.name = r[1];
                size_t idx = tables.size();
                tableIndexMap.emplace(std::move(key), idx);
                tables.push_back(std::move(t));
                it = tableIndexMap.find(key);
            }
            SqlColumnInfo ci;
            ci.name = r[2];
            ci.type = r[3];
            ci.notNull = (ToLowerW(r[4]) == L"no");
            tables[it->second].columns.push_back(std::move(ci));
        }
        // Populate Primary Keys & single-column UNIQUE constraints for SQL Server
        if (!tables.empty()) {
            SqlQueryResult pkRes = ExecuteQuery(
                profile,
                L"SET NOCOUNT ON; "
                L"SELECT tc.TABLE_SCHEMA, tc.TABLE_NAME, kcu.COLUMN_NAME "
                L"FROM INFORMATION_SCHEMA.TABLE_CONSTRAINTS tc "
                L"JOIN INFORMATION_SCHEMA.KEY_COLUMN_USAGE kcu "
                L"  ON tc.CONSTRAINT_NAME = kcu.CONSTRAINT_NAME AND tc.TABLE_SCHEMA = kcu.TABLE_SCHEMA "
                L"WHERE tc.CONSTRAINT_TYPE = 'PRIMARY KEY';");
            if (pkRes.success) {
                for (const auto& pr : pkRes.rows) {
                    if (pr.size() < 3) continue;
                    auto it = tableIndexMap.find({ pr[0], pr[1] });
                    if (it != tableIndexMap.end()) MarkColumnPrimaryKey(tables[it->second], pr[2]);
                }
            }
            SqlQueryResult uqRes = ExecuteQuery(
                profile,
                L"SET NOCOUNT ON; "
                L"SELECT tc.TABLE_SCHEMA, tc.TABLE_NAME, MAX(kcu.COLUMN_NAME) "
                L"FROM INFORMATION_SCHEMA.TABLE_CONSTRAINTS tc "
                L"JOIN INFORMATION_SCHEMA.KEY_COLUMN_USAGE kcu "
                L"  ON tc.CONSTRAINT_NAME = kcu.CONSTRAINT_NAME AND tc.TABLE_SCHEMA = kcu.TABLE_SCHEMA "
                L"WHERE tc.CONSTRAINT_TYPE = 'UNIQUE' "
                L"GROUP BY tc.TABLE_SCHEMA, tc.TABLE_NAME, tc.CONSTRAINT_NAME HAVING COUNT(*) = 1;");
            if (uqRes.success) {
                for (const auto& ur : uqRes.rows) {
                    if (ur.size() < 3) continue;
                    auto it = tableIndexMap.find({ ur[0], ur[1] });
                    if (it != tableIndexMap.end()) MarkColumnUnique(tables[it->second], ur[2]);
                }
            }
            SqlQueryResult fkRes = ExecuteQuery(
                profile,
                L"SET NOCOUNT ON; "
                L"SELECT fk_kcu.TABLE_SCHEMA, fk_kcu.TABLE_NAME, fk_kcu.COLUMN_NAME, pk_kcu.TABLE_NAME, pk_kcu.COLUMN_NAME "
                L"FROM INFORMATION_SCHEMA.REFERENTIAL_CONSTRAINTS rc "
                L"JOIN INFORMATION_SCHEMA.KEY_COLUMN_USAGE fk_kcu "
                L"  ON rc.CONSTRAINT_NAME = fk_kcu.CONSTRAINT_NAME AND rc.CONSTRAINT_SCHEMA = fk_kcu.CONSTRAINT_SCHEMA "
                L"JOIN INFORMATION_SCHEMA.KEY_COLUMN_USAGE pk_kcu "
                L"  ON rc.UNIQUE_CONSTRAINT_NAME = pk_kcu.CONSTRAINT_NAME AND rc.UNIQUE_CONSTRAINT_SCHEMA = pk_kcu.CONSTRAINT_SCHEMA "
                L"  AND fk_kcu.ORDINAL_POSITION = pk_kcu.ORDINAL_POSITION;");
            if (fkRes.success) {
                for (const auto& fr : fkRes.rows) {
                    if (fr.size() < 5) continue;
                    auto it = tableIndexMap.find({ fr[0], fr[1] });
                    if (it != tableIndexMap.end()) {
                        tables[it->second].foreignKeys.push_back({ fr[2], fr[3], fr[4] });
                    }
                }
            }
        }
        return tables;
    }

    return tables;
}

std::vector<SqlTableInfo> SqlEngine::InspectAllServerSchemas(const SqlConnectionProfile& profile, std::wstring& outError) {
    // Clearing profile.database instructs MySQL InspectSchema to scan all non-system schemas in a single query.
    if (profile.dialect == SqlDialect::MySQL) {
        SqlConnectionProfile allProf = profile;
        allProf.database.clear();
        return InspectSchema(allProf, outError);
    }
    // For PostgreSQL, iterate available databases on the server if multiple exist (bounded for responsiveness).
    if (profile.dialect == SqlDialect::PostgreSQL && profile.availableDatabases.size() > 1) {
        std::vector<SqlTableInfo> combined;
        const size_t limitDbs = (std::min<size_t>)(profile.availableDatabases.size(), 8);
        for (size_t i = 0; i < limitDbs; ++i) {
            SqlConnectionProfile dbProf = profile;
            dbProf.database = profile.availableDatabases[i];
            std::wstring dbErr;
            auto part = InspectSchema(dbProf, dbErr);
            for (auto& t : part) {
                if (t.schemaName.empty() || t.schemaName == L"public") {
                    t.schemaName = dbProf.database;
                }
                combined.push_back(std::move(t));
            }
            if (outError.empty() && !dbErr.empty()) outError = dbErr;
        }
        if (!combined.empty()) return combined;
    }
    return InspectSchema(profile, outError);
}

bool SqlEngine::ExportCsv(const SqlQueryResult& res, const std::wstring& outFilePath) {
    if (outFilePath.empty()) return false;
    std::ofstream ofs(fs::path(outFilePath), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;

    auto writeCell = [&](const std::wstring& cell) {
        std::string u = WideToUtf8(cell);
        bool needQuotes = (u.find(',') != std::string::npos || u.find('"') != std::string::npos ||
                           u.find('\n') != std::string::npos || u.find('\r') != std::string::npos);
        if (!needQuotes) {
            ofs << u;
            return;
        }
        ofs << '"';
        for (char c : u) {
            if (c == '"') ofs << "\"\"";
            else ofs << c;
        }
        ofs << '"';
    };

    for (size_t i = 0; i < res.columns.size(); ++i) {
        if (i > 0) ofs << ',';
        writeCell(res.columns[i]);
    }
    ofs << "\r\n";

    for (const auto& row : res.rows) {
        for (size_t i = 0; i < res.columns.size(); ++i) {
            if (i > 0) ofs << ',';
            writeCell(i < row.size() ? row[i] : L"");
        }
        ofs << "\r\n";
    }
    return true;
}

bool SqlEngine::ExportJson(const SqlQueryResult& res, const std::wstring& outFilePath) {
    if (outFilePath.empty()) return false;
    std::ofstream ofs(fs::path(outFilePath), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;

    auto writeJsonStr = [&](const std::wstring& w) {
        std::string u = WideToUtf8(w);
        ofs << '"';
        for (unsigned char c : u) {
            if (c == '"') ofs << "\\\"";
            else if (c == '\\') ofs << "\\\\";
            else if (c == '\n') ofs << "\\n";
            else if (c == '\r') ofs << "\\r";
            else if (c == '\t') ofs << "\\t";
            else if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                ofs << buf;
            } else {
                ofs << static_cast<char>(c);
            }
        }
        ofs << '"';
    };

    ofs << "[\n";
    for (size_t r = 0; r < res.rows.size(); ++r) {
        ofs << "  {";
        const auto& row = res.rows[r];
        for (size_t c = 0; c < res.columns.size(); ++c) {
            if (c > 0) ofs << ", ";
            writeJsonStr(res.columns[c]);
            ofs << ": ";
            std::wstring val = (c < row.size()) ? row[c] : L"";
            if (val == L"NULL") ofs << "null";
            else writeJsonStr(val);
        }
        ofs << "}";
        if (r + 1 < res.rows.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";
    return true;
}

static std::wstring QuoteIdentForDialect(SqlDialect dialect, const std::wstring& ident) {
    if (dialect == SqlDialect::MySQL) {
        std::wstring out = L"`";
        for (wchar_t c : ident) {
            if (c == L'`') out += L"``";
            else out.push_back(c);
        }
        out += L"`";
        return out;
    }
    return QuoteSqlIdent(ident);
}

static bool HasExplicitSqlPagingClause(const std::wstring& sqlLower) {
    return sqlLower.find(L" limit ") != std::wstring::npos ||
           sqlLower.find(L"\nlimit ") != std::wstring::npos ||
           sqlLower.find(L"\tlimit ") != std::wstring::npos ||
           sqlLower.find(L" offset ") != std::wstring::npos ||
           sqlLower.find(L" fetch next ") != std::wstring::npos ||
           sqlLower.find(L" fetch first ") != std::wstring::npos;
}

std::wstring SqlEngine::BuildPagedSelectQuery(const SqlConnectionProfile& profile,
                                              const SqlTableInfo& table,
                                              size_t pageIndex,
                                              size_t pageSize) {
    const size_t safeSize = (std::clamp<size_t>)(pageSize, 10, 500);
    const size_t offset = pageIndex * safeSize;
    std::wstring qualified = (!table.schemaName.empty() && profile.dialect != SqlDialect::SQLite)
        ? (QuoteIdentForDialect(profile.dialect, table.schemaName) + L"." + QuoteIdentForDialect(profile.dialect, table.name))
        : QuoteIdentForDialect(profile.dialect, table.name);
    if (profile.dialect == SqlDialect::SQLServer) {
        return L"SELECT * FROM " + qualified + L" ORDER BY (SELECT NULL) OFFSET " +
               std::to_wstring(offset) + L" ROWS FETCH NEXT " + std::to_wstring(safeSize) + L" ROWS ONLY;";
    }
    return L"SELECT * FROM " + qualified + L" LIMIT " + std::to_wstring(safeSize) +
           L" OFFSET " + std::to_wstring(offset) + L";";
}

SqlQueryResult SqlEngine::ExecuteQueryWithPagination(const SqlConnectionProfile& profile,
                                                     const std::wstring& sqlText,
                                                     size_t pageIndex,
                                                     size_t pageSize,
                                                     int64_t& outTotalRows,
                                                     bool& outUsedServerPaging) {
    outTotalRows = -1;
    outUsedServerPaging = false;
    std::wstring trimmed = TrimW(sqlText);
    while (!trimmed.empty() && trimmed.back() == L';') {
        trimmed.pop_back();
        trimmed = TrimW(trimmed);
    }
    std::wstring lower = ToLowerW(trimmed);
    const size_t semi = trimmed.find(L';');
    const bool isSingleSelect = (lower.rfind(L"select ", 0) == 0 || lower.rfind(L"select\t", 0) == 0 || lower.rfind(L"select\n", 0) == 0) &&
                                (semi == std::wstring::npos);

    if (!isSingleSelect || HasExplicitSqlPagingClause(lower) || lower.find(L"into ") != std::wstring::npos) {
        SqlQueryResult r = ExecuteQuery(profile, sqlText);
        if (r.success && r.isSelect) outTotalRows = static_cast<int64_t>(r.rows.size());
        return r;
    }

    const size_t safeSize = (std::clamp<size_t>)(pageSize, 10, 500);
    const size_t offset = pageIndex * safeSize;

    // Count total rows on server cleanly when single SELECT
    std::wstring countSql = L"SELECT COUNT(*) FROM (" + trimmed + L") AS _phide_cnt;";
    SqlQueryResult cntRes = ExecuteQuery(profile, countSql);
    if (cntRes.success && !cntRes.rows.empty() && !cntRes.rows[0].empty()) {
        try { outTotalRows = std::stoll(cntRes.rows[0][0]); } catch (...) {}
    }

    std::wstring pagedSql;
    if (profile.dialect == SqlDialect::SQLServer) {
        pagedSql = trimmed + L" ORDER BY (SELECT NULL) OFFSET " + std::to_wstring(offset) +
                   L" ROWS FETCH NEXT " + std::to_wstring(safeSize) + L" ROWS ONLY;";
    } else {
        pagedSql = trimmed + L" LIMIT " + std::to_wstring(safeSize) + L" OFFSET " + std::to_wstring(offset) + L";";
    }
    SqlQueryResult r = ExecuteQuery(profile, pagedSql);
    if (r.success) {
        outUsedServerPaging = true;
        return r;
    }
    // Fallback to raw execution if dialect syntax rejected appended LIMIT
    return ExecuteQuery(profile, sqlText);
}

SqlQueryResult SqlEngine::UpdateTableCell(const SqlConnectionProfile& profile,
                                          const std::wstring& schemaName,
                                          const std::wstring& tableName,
                                          const std::wstring& targetCol,
                                          const std::wstring& newValue,
                                          const std::vector<std::pair<std::wstring, std::wstring>>& pkColumnsAndValues) {
    SqlQueryResult res;
    if (tableName.empty() || targetCol.empty() || pkColumnsAndValues.empty()) {
        res.errorMessage = L"Inline cell update requires a valid table, column, and primary key.";
        return res;
    }
    std::wstring qualified = (!schemaName.empty() && profile.dialect != SqlDialect::SQLite)
        ? (QuoteIdentForDialect(profile.dialect, schemaName) + L"." + QuoteIdentForDialect(profile.dialect, tableName))
        : QuoteIdentForDialect(profile.dialect, tableName);

    std::wostringstream sql;
    sql << L"UPDATE " << qualified << L" SET " << QuoteIdentForDialect(profile.dialect, targetCol) << L" = ";
    if (newValue == L"NULL") sql << L"NULL";
    else sql << L"'" << EscapeSqlLiteral(newValue) << L"'";
    sql << L" WHERE ";
    for (size_t i = 0; i < pkColumnsAndValues.size(); ++i) {
        if (i > 0) sql << L" AND ";
        sql << QuoteIdentForDialect(profile.dialect, pkColumnsAndValues[i].first);
        if (pkColumnsAndValues[i].second == L"NULL") {
            sql << L" IS NULL";
        } else {
            sql << L" = '" << EscapeSqlLiteral(pkColumnsAndValues[i].second) << L"'";
        }
    }
    sql << L";";
    return ExecuteQuery(profile, sql.str());
}

} // namespace phide::db
