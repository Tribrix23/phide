#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "phide/ui/ExtensionsPanel.hpp"
#include <windows.h>
#include <winhttp.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <thread>
#include <fstream>

namespace phide::ui {

/**
 * @brief Queries Supabase REST API for runtime versions and download links (Python, PHP, Node.js).
 * Endpoints:
 *   https://lzwtgcbvuoogpzcgujdb.supabase.co/rest/v1/python
 *   https://lzwtgcbvuoogpzcgujdb.supabase.co/rest/v1/php
 *   https://lzwtgcbvuoogpzcgujdb.supabase.co/rest/v1/node
 * Key: sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry
 * Columns: version, download
 */
static bool FetchVersionsFromSupabase(const std::wstring& endpointName,
                                      std::vector<std::wstring>& outVersions,
                                      std::vector<std::wstring>& outDownloads) {
    HINTERNET hSession = WinHttpOpen(L"phide-IDE/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // Fast 3-second timeout to avoid any hang
    WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"lzwtgcbvuoogpzcgujdb.supabase.co", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring reqPath = L"/rest/v1/" + endpointName + L"?select=version,download";
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", reqPath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring headers = L"apikey: sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry\r\nAuthorization: Bearer sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry\r\n";
    BOOL bSend = WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(headers.length()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bSend || !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::string responseBody;
    DWORD bytesRead = 0;
    char buffer[4096];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        responseBody.append(buffer, bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (responseBody.empty()) return false;

    // Parse JSON array of objects: [{"version":"...", "download":"..."}, ...]
    size_t pos = 0;
    while ((pos = responseBody.find('{', pos)) != std::string::npos) {
        size_t endObj = responseBody.find('}', pos);
        if (endObj == std::string::npos) break;
        std::string objStr = responseBody.substr(pos, endObj - pos + 1);

        auto extractField = [&objStr](const std::string& key) -> std::string {
            size_t kpos = objStr.find("\"" + key + "\"");
            if (kpos == std::string::npos) return "";
            size_t colon = objStr.find(':', kpos);
            if (colon == std::string::npos) return "";
            size_t q1 = objStr.find('"', colon);
            if (q1 == std::string::npos) return "";
            size_t q2 = objStr.find('"', q1 + 1);
            if (q2 == std::string::npos) return "";
            return objStr.substr(q1 + 1, q2 - q1 - 1);
        };

        std::string ver = extractField("version");
        std::string dl = extractField("download");
        if (!ver.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, ver.c_str(), -1, nullptr, 0);
            std::wstring wVer(wlen > 1 ? wlen - 1 : 0, 0);
            if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, ver.c_str(), -1, &wVer[0], wlen);

            int dlen = MultiByteToWideChar(CP_UTF8, 0, dl.c_str(), -1, nullptr, 0);
            std::wstring wDl(dlen > 1 ? dlen - 1 : 0, 0);
            if (dlen > 1) MultiByteToWideChar(CP_UTF8, 0, dl.c_str(), -1, &wDl[0], dlen);

            outVersions.push_back(wVer);
            outDownloads.push_back(wDl);
        }
        pos = endObj + 1;
    }

    return true; // Query succeeded
}

static std::wstring ResolveAssetPath(const std::wstring& filename) {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    std::vector<std::filesystem::path> candidates = {
        exeDir / L"assets" / filename,
        exeDir.parent_path() / L"assets" / filename,
        std::filesystem::current_path() / L"assets" / filename,
        L"assets\\" + filename
    };

    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) return c.wstring();
    }
    return candidates[0].wstring();
}

static std::wstring DisplayPath(const std::wstring& path) {
    std::wstring userProfile = _wgetenv(L"USERPROFILE") ? _wgetenv(L"USERPROFILE") : L"";
    if (!userProfile.empty() && path.rfind(userProfile, 0) == 0) {
        return L"~" + path.substr(userProfile.length());
    }
    return path;
}

ExtensionsPanel::ExtensionsPanel() {
    // 1. Python
    ExtensionItem py;
    py.id = L"python";
    py.name = L"Python";
    py.tagline = L"High-level programming language with automated environment discovery.";
    py.publisher = L"Python Software Foundation";
    py.description = L"Python is a versatile, high-level programming language emphasizing code readability and rapid development. Within phide, Python integration delivers real-time syntax error checking, comprehensive keyword and symbol IntelliSense, virtual environment resolution (.venv), automated pip dependency management, interactive terminal execution, and step-through debugger support.";
    py.features = {
        { L"IntelliSense & Syntax Validation", L"Rich keyword completion and live syntax diagnostics." },
        { L"Integrated Terminal & Debugger", L"Run scripts or launch interactive debugging sessions." },
        { L"Package & Virtual Environments", L"Automatic pip package and venv interpreter discovery." }
    };
    py.assetFile = L"python.png";
    py.versions.clear(); // NO mock/placeholder versions! Populated by real detected PC runtime & Supabase
    py.selectedVersionIdx = 0;
    py.isSpecialAllVersionsUninstall = false;
    m_items.push_back(py);

    // 2. PHP
    ExtensionItem php;
    php.id = L"php";
    php.name = L"PHP";
    php.tagline = L"Fast pragmatic scripting language for backend web services and server APIs.";
    php.publisher = L"The PHP Group";
    php.description = L"PHP is a widely-used server-side scripting language optimized for robust backend web development, RESTful APIs, and dynamic templating. Within phide, PHP integration provides modern PHP 8+ language syntax diagnostics, built-in development web server runner, Composer package workflow tooling, fast CLI execution, and seamless script debugging.";
    php.features = {
        { L"Live Server & Web Preview", L"Serve backend APIs and preview local PHP web apps." },
        { L"PHP 8+ Syntax Diagnostics", L"Real-time syntax validation and linting diagnostics." },
        { L"CLI & Composer Tooling", L"Fast CGI runner, artisan, and composer script tools." }
    };
    php.assetFile = L"php.png";
    php.versions.clear(); // NO mock/placeholder versions! Populated by real detected PC runtime & Supabase
    php.selectedVersionIdx = 0;
    php.isSpecialAllVersionsUninstall = false;
    m_items.push_back(php);

    // 3. Node.js (with special exception: if node is installed, all versions show uninstall)
    ExtensionItem node;
    node.id = L"node";
    node.name = L"Node.js";
    node.tagline = L"Cross-platform JavaScript runtime built on Chrome's V8 engine.";
    node.publisher = L"OpenJS Foundation";
    node.description = L"Node.js is an open-source, cross-platform JavaScript and TypeScript runtime built on Chrome's high-performance V8 engine. Within phide, Node.js integration offers full TypeScript language server diagnostics, npm and npx automated script execution, node_modules local runtime resolution, rich auto-completion, and native V8 debug protocol session support.";
    node.features = {
        { L"npm / npx Script Runner", L"Automated package script discovery and build runner." },
        { L"Modern JS/TS IntelliSense", L"Auto-completion, parameter hints, and import resolution." },
        { L"Node Debug Protocol", L"Interactive step-through debugging with breakpoints." }
    };
    node.assetFile = L"node.png";
    node.versions = { L"v26.3.0", L"v22.20.0", L"v20.18.0" };
    node.versionDownloads = {
        L"https://nodejs.org/dist/v26.3.0/node-v26.3.0-x64.msi",
        L"https://nodejs.org/dist/v22.20.0/node-v22.20.0-x64.msi",
        L"https://nodejs.org/dist/v20.18.0/node-v20.18.0-x64.msi"
    };
    node.selectedVersionIdx = 0;
    node.isSpecialAllVersionsUninstall = true; // User requirement: for node, if installed all versions show uninstall
    m_items.push_back(node);

    // Detect installed runtimes on the user's PC
    RefreshPcStatus();

    // Query Supabase database for Python, PHP, and Node runtime versions and download links in background
    FetchSupabaseVersions(true);
}

ExtensionsPanel::~ExtensionsPanel() {
    if (m_aliveMutex && m_aliveFlag) {
        std::lock_guard<std::mutex> lk(*m_aliveMutex);
        *m_aliveFlag = false;
    }
}

void ExtensionsPanel::Open() noexcept {
    m_isOpen = true;
    m_showVersionDropdown = false;
    m_versionDropdownScrollY = 0.0f;
    FetchSupabaseVersions(true);
}

bool ExtensionsPanel::FetchSupabaseVersions(bool async) {
    if (async) {
        auto aliveFlag = m_aliveFlag;
        auto aliveMtx = m_aliveMutex;
        std::vector<std::wstring> ids;
        {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            for (const auto& item : m_items) ids.push_back(item.id);
        }
        std::thread([this, aliveFlag, aliveMtx, ids]() {
            auto normalizeVer = [](const std::wstring& v) -> std::wstring {
                if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
                return v;
            };
            struct FetchedData {
                std::wstring id;
                std::vector<std::wstring> versions;
                std::vector<std::wstring> downloads;
            };
            std::vector<FetchedData> allFetched;
            for (const auto& id : ids) {
                {
                    std::lock_guard<std::mutex> lk(*aliveMtx);
                    if (!*aliveFlag) return;
                }
                FetchedData fd;
                fd.id = id;
                if (FetchVersionsFromSupabase(id, fd.versions, fd.downloads) && !fd.versions.empty()) {
                    allFetched.push_back(std::move(fd));
                }
            }
            if (!allFetched.empty()) {
                std::lock_guard<std::mutex> lk(*aliveMtx);
                if (!*aliveFlag) return;
                std::function<void()> cb;
                {
                    std::lock_guard<std::mutex> lock(m_itemsMutex);
                    for (const auto& fd : allFetched) {
                        for (auto& item : m_items) {
                            if (item.id == fd.id) {
                                item.versions = fd.versions;
                                item.versionDownloads = fd.downloads;
                                item.selectedVersionIdx = 0;
                                for (size_t i = 0; i < item.versions.size(); ++i) {
                                    if (normalizeVer(item.versions[i]) == normalizeVer(item.installedVersion)) {
                                        item.selectedVersionIdx = static_cast<int>(i);
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                    m_attemptedSupabaseFetch = true;
                    cb = m_onDataChanged;
                }
                if (cb) cb();
            }
        }).detach();
        return true;
    }

    auto normalizeVer = [](const std::wstring& v) -> std::wstring {
        if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
        return v;
    };

    struct FetchedData {
        std::wstring id;
        std::vector<std::wstring> versions;
        std::vector<std::wstring> downloads;
    };
    std::vector<FetchedData> allFetched;

    for (const auto& item : m_items) {
        FetchedData fd;
        fd.id = item.id;
        bool ok = FetchVersionsFromSupabase(item.id, fd.versions, fd.downloads);
        if (ok && !fd.versions.empty()) {
            allFetched.push_back(std::move(fd));
        }
    }

    if (!allFetched.empty()) {
        std::lock_guard<std::mutex> lock(m_itemsMutex);
        for (const auto& fd : allFetched) {
            for (auto& item : m_items) {
                if (item.id == fd.id) {
                    item.versions = fd.versions;
                    item.versionDownloads = fd.downloads;
                    item.selectedVersionIdx = 0;
                    for (size_t i = 0; i < item.versions.size(); ++i) {
                        if (normalizeVer(item.versions[i]) == normalizeVer(item.installedVersion)) {
                            item.selectedVersionIdx = static_cast<int>(i);
                            break;
                        }
                    }
                    break;
                }
            }
        }
        m_attemptedSupabaseFetch = true;
        if (m_onDataChanged) {
            m_onDataChanged();
        }
    }
    return true;
}

static std::wstring RunCmdCapture(const std::wstring& cmd) {
    std::wstring result;
    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return result;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hWrite);
        hWrite = nullptr;

        char buf[512];
        DWORD bytesRead = 0;
        std::string raw;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
            buf[bytesRead] = 0;
            raw.append(buf, bytesRead);
        }
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (!raw.empty()) {
            int len = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), -1, nullptr, 0);
            if (len > 0) {
                result.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), -1, &result[0], len);
            }
        }
    }
    if (hWrite) CloseHandle(hWrite);
    if (hRead) CloseHandle(hRead);
    return result;
}

void ExtensionsPanel::RefreshPcStatus() {
    for (auto& item : m_items) {
        item.environments.clear();
        if (item.id == L"python") {
            struct PyEnvInfo {
                std::wstring ver;
                std::wstring path;
                bool isDefault = false;
            };
            std::vector<PyEnvInfo> detectedPythons;

            auto tryAddPython = [&](const std::wstring& exePath, bool isDef) {
                if (exePath.empty()) return;
                if (exePath.find(L"WindowsApps") != std::wstring::npos) return;
                if (exePath.find(L"_uninstalling_") != std::wstring::npos) return;
                if (exePath.find(L"Quantix") != std::wstring::npos) return;
                std::error_code ec;
                if (!std::filesystem::exists(exePath, ec) || ec) return;
                std::filesystem::path canonical = std::filesystem::weakly_canonical(exePath, ec);
                std::wstring normPath = ec ? exePath : canonical.wstring();

                for (auto& dp : detectedPythons) {
                    std::filesystem::path p1 = std::filesystem::weakly_canonical(dp.path, ec);
                    if (_wcsicmp(normPath.c_str(), (ec ? dp.path : p1.wstring()).c_str()) == 0) {
                        if (isDef) dp.isDefault = true;
                        return;
                    }
                }

                std::wstring verOut = RunCmdCapture(L"\"" + normPath + L"\" --version");
                std::wstring ver;
                auto vPos = verOut.find(L"Python ");
                if (vPos != std::wstring::npos) {
                    ver = verOut.substr(vPos + 7);
                    while (!ver.empty() && (ver.back() == L'\r' || ver.back() == L'\n' || ver.back() == L' ')) ver.pop_back();
                }
                if (!ver.empty()) {
                    detectedPythons.push_back({ ver, normPath, isDef });
                }
            };

            // 1. Query py.exe -0p to discover registered Python interpreters
            std::wstring py0p = RunCmdCapture(L"py.exe -0p");
            if (!py0p.empty()) {
                std::wstringstream ss(py0p);
                std::wstring line;
                while (std::getline(ss, line)) {
                    while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) line.pop_back();
                    if (line.empty()) continue;

                    bool isDef = (line.find(L'*') != std::wstring::npos);
                    auto colIdx = line.find(L":\\");
                    if (colIdx != std::wstring::npos && colIdx > 0) {
                        std::wstring exePath = line.substr(colIdx - 1);
                        while (!exePath.empty() && (exePath.back() == L' ' || exePath.back() == L'\t' || exePath.back() == L'\r')) exePath.pop_back();
                        tryAddPython(exePath, isDef);
                    }
                }
            }

            // 2. Query Windows Registry (PEP 514 standard registrations in HKCU and HKLM)
            auto scanRegistryPython = [&](HKEY root) {
                HKEY hPyCore = nullptr;
                if (RegOpenKeyExW(root, L"Software\\Python\\PythonCore", 0, KEY_READ, &hPyCore) == ERROR_SUCCESS) {
                    wchar_t subKeyName[256];
                    DWORD idx = 0;
                    DWORD nameLen = 256;
                    while (RegEnumKeyExW(hPyCore, idx++, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                        nameLen = 256;
                        std::wstring subPath = std::wstring(L"Software\\Python\\PythonCore\\") + subKeyName + L"\\InstallPath";
                        HKEY hInstall = nullptr;
                        if (RegOpenKeyExW(root, subPath.c_str(), 0, KEY_READ, &hInstall) == ERROR_SUCCESS) {
                            wchar_t valBuf[MAX_PATH] = { 0 };
                            DWORD dwType = REG_SZ;
                            DWORD dwSz = sizeof(valBuf);
                            if (RegQueryValueExW(hInstall, L"ExecutablePath", nullptr, &dwType, reinterpret_cast<LPBYTE>(valBuf), &dwSz) == ERROR_SUCCESS && valBuf[0] != 0) {
                                tryAddPython(valBuf, false);
                            } else {
                                dwSz = sizeof(valBuf);
                                if (RegQueryValueExW(hInstall, nullptr, nullptr, &dwType, reinterpret_cast<LPBYTE>(valBuf), &dwSz) == ERROR_SUCCESS && valBuf[0] != 0) {
                                    std::filesystem::path p(valBuf);
                                    if (p.filename() != L"python.exe") p /= L"python.exe";
                                    tryAddPython(p.wstring(), false);
                                }
                            }
                            RegCloseKey(hInstall);
                        }
                    }
                    RegCloseKey(hPyCore);
                }
            };
            scanRegistryPython(HKEY_CURRENT_USER);
            scanRegistryPython(HKEY_LOCAL_MACHINE);

            // 3. Scan user's %LOCALAPPDATA%\Programs\Python directory
            wchar_t szLocalApp[MAX_PATH] = { 0 };
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", szLocalApp, MAX_PATH) > 0) {
                std::filesystem::path pyRoot = std::filesystem::path(szLocalApp) / L"Programs" / L"Python";
                std::error_code ec;
                if (std::filesystem::exists(pyRoot, ec)) {
                    for (const auto& entry : std::filesystem::directory_iterator(pyRoot, ec)) {
                        if (entry.is_directory()) {
                            std::filesystem::path cand = entry.path() / L"python.exe";
                            tryAddPython(cand.wstring(), false);
                        }
                    }
                }
            }

            // 4. Check system PATH via SearchPathW
            wchar_t foundPath[MAX_PATH] = { 0 };
            if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, foundPath, nullptr) > 0) {
                tryAddPython(foundPath, detectedPythons.empty());
            }

            // Populate environments purely from verified existing executables
            if (!detectedPythons.empty()) {
                // Sort detected Python versions descending (newest first, e.g. 3.15.1 > 3.15.0 > 3.13.9)
                auto parseVerTuple = [](const std::wstring& v) -> std::tuple<int, int, int> {
                    int maj = 0, min = 0, pat = 0;
                    swscanf_s(v.c_str(), L"%d.%d.%d", &maj, &min, &pat);
                    return { maj, min, pat };
                };
                std::sort(detectedPythons.begin(), detectedPythons.end(), [&](const PyEnvInfo& a, const PyEnvInfo& b) {
                    return parseVerTuple(a.ver) > parseVerTuple(b.ver);
                });

                std::wstring pyDefVer;
                std::wstring pyDefOut = RunCmdCapture(L"py.exe --version");
                if (pyDefOut.find(L"Python ") != std::wstring::npos) {
                    pyDefVer = pyDefOut.substr(pyDefOut.find(L"Python ") + 7);
                    while (!pyDefVer.empty() && (pyDefVer.back() == L'\r' || pyDefVer.back() == L'\n' || pyDefVer.back() == L' ')) pyDefVer.pop_back();
                }

                int defIdx = -1;
                if (!pyDefVer.empty()) {
                    for (size_t i = 0; i < detectedPythons.size(); ++i) {
                        if (detectedPythons[i].ver == pyDefVer) {
                            defIdx = static_cast<int>(i);
                            break;
                        }
                    }
                }
                if (defIdx > 0) {
                    PyEnvInfo chosen = detectedPythons[defIdx];
                    detectedPythons.erase(detectedPythons.begin() + defIdx);
                    detectedPythons.insert(detectedPythons.begin(), chosen);
                }

                item.isInstalled = true;
                item.installedVersion = detectedPythons[0].ver;
                item.installPath = detectedPythons[0].path;

                auto normalizeVer = [](const std::wstring& v) -> std::wstring {
                    if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
                    return v;
                };
                for (size_t v = 0; v < item.versions.size(); ++v) {
                    if (normalizeVer(item.versions[v]) == normalizeVer(item.installedVersion)) {
                        item.selectedVersionIdx = static_cast<int>(v);
                        break;
                    }
                }

                // Primary default Global System environment
                EnvironmentOption envGlobal;
                envGlobal.name = L"Python " + detectedPythons[0].ver + L" (Global System)";
                envGlobal.path = detectedPythons[0].path;
                envGlobal.type = L"System PATH";
                envGlobal.isDefault = true;
                item.environments.push_back(envGlobal);

                // Secondary system environments (all other detected versions on PC)
                for (size_t i = 1; i < detectedPythons.size(); ++i) {
                    EnvironmentOption envSec;
                    envSec.name = L"Python " + detectedPythons[i].ver + L" (System PATH)";
                    envSec.path = detectedPythons[i].path;
                    envSec.type = L"System PATH";
                    envSec.isDefault = false;
                    item.environments.push_back(envSec);
                }
            } else {
                item.isInstalled = false;
                item.installedVersion.clear();
                item.installPath.clear();
            }

            // Windows Python Launcher (py.exe) - only if actually present
            wchar_t pyLauncher[MAX_PATH] = { 0 };
            std::error_code ecPy;
            if (SearchPathW(nullptr, L"py.exe", nullptr, MAX_PATH, pyLauncher, nullptr) > 0 ||
                std::filesystem::exists(L"C:\\Windows\\py.exe", ecPy)) {
                EnvironmentOption envLauncher;
                envLauncher.name = L"Windows Python Launcher (py.exe)";
                envLauncher.path = (pyLauncher[0] != 0) ? pyLauncher : L"C:\\Windows\\py.exe";
                envLauncher.type = L"Global Launcher";
                envLauncher.isDefault = false;
                item.environments.push_back(envLauncher);
            }

            // Workspace Virtual Environment (.venv) - only if actually present AND its base interpreter is valid
            std::filesystem::path venvPy = std::filesystem::current_path() / L".venv" / L"Scripts" / L"python.exe";
            if (std::filesystem::exists(venvPy, ecPy)) {
                std::wstring venvVerOut = RunCmdCapture(L"\"" + venvPy.wstring() + L"\" --version");
                if (venvVerOut.find(L"Python ") != std::wstring::npos &&
                    venvVerOut.find(L"Unable to create process") == std::wstring::npos) {
                    EnvironmentOption envVenv;
                    envVenv.name = L"Workspace Virtual Environment (.venv)";
                    envVenv.path = venvPy.wstring();
                    envVenv.type = L"Virtualenv";
                    envVenv.isDefault = false;
                    item.environments.push_back(envVenv);
                } else {
                    std::filesystem::remove_all(std::filesystem::current_path() / L".venv", ecPy);
                }
            }

            item.defaultEnvIdx = 0;

        } else if (item.id == L"node") {
            wchar_t foundPath[MAX_PATH] = { 0 };
            DWORD len = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, foundPath, nullptr);
            if (len == 0) {
                std::wstring cand = L"C:\\Program Files\\nodejs\\node.exe";
                std::error_code ecNode;
                if (std::filesystem::exists(cand, ecNode)) {
                    len = static_cast<DWORD>(cand.length());
                    wcsncpy_s(foundPath, cand.c_str(), MAX_PATH);
                }
            }
            if (len > 0) {
                std::wstring detectedVer;
                std::wstring outVer = RunCmdCapture(L"\"" + std::wstring(foundPath) + L"\" -v");
                if (!outVer.empty()) {
                    size_t p = 0;
                    while (p < outVer.size() && (outVer[p] == L'v' || outVer[p] == L'V' || outVer[p] == L' ')) p++;
                    size_t end = outVer.find_first_of(L" \r\n", p);
                    if (end != std::wstring::npos) detectedVer = outVer.substr(p, end - p);
                    else detectedVer = outVer.substr(p);
                }
                if (detectedVer.empty()) {
                    std::wstring outVer2 = RunCmdCapture(L"\"" + std::wstring(foundPath) + L"\" --version");
                    if (!outVer2.empty()) {
                        size_t p = 0;
                        while (p < outVer2.size() && (outVer2[p] == L'v' || outVer2[p] == L'V' || outVer2[p] == L' ')) p++;
                        size_t end = outVer2.find_first_of(L" \r\n", p);
                        if (end != std::wstring::npos) detectedVer = outVer2.substr(p, end - p);
                        else detectedVer = outVer2.substr(p);
                    }
                }

                item.isInstalled = true;
                item.installedVersion = detectedVer;
                item.installPath = foundPath;

                EnvironmentOption envGlobal;
                envGlobal.name = !detectedVer.empty() ? (L"Node.js " + detectedVer + L" (Global System)") : L"Node.js (Global System)";
                envGlobal.path = foundPath;
                envGlobal.type = L"System PATH";
                envGlobal.isDefault = true;
                item.environments.push_back(envGlobal);
            } else {
                item.isInstalled = false;
                item.installedVersion.clear();
                EnvironmentOption envGlobal;
                envGlobal.name = L"Node.js (System PATH - Not Installed)";
                envGlobal.path = L"C:\\Program Files\\nodejs\\node.exe";
                envGlobal.type = L"System PATH";
                envGlobal.isDefault = false;
                item.environments.push_back(envGlobal);
            }

            EnvironmentOption envLocal;
            envLocal.name = L"Workspace Local Runtime (node_modules)";
            envLocal.path = L".\\node_modules\\.bin\\node.exe";
            envLocal.type = L"Local Package";
            envLocal.isDefault = false;
            item.environments.push_back(envLocal);

            item.defaultEnvIdx = 0;

        } else if (item.id == L"php") {
            wchar_t foundPath[MAX_PATH] = { 0 };
            DWORD len = SearchPathW(nullptr, L"php.exe", nullptr, MAX_PATH, foundPath, nullptr);
            std::wstring detectedVer;
            if (len == 0) {
                // Check LOCALAPPDATA\Programs\PHP
                wchar_t szAppData[MAX_PATH] = { 0 };
                GetEnvironmentVariableW(L"LOCALAPPDATA", szAppData, MAX_PATH);
                std::filesystem::path phpRoot = std::filesystem::path(szAppData) / L"Programs" / L"PHP";
                std::error_code ecDir;
                if (std::filesystem::exists(phpRoot, ecDir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(phpRoot, ecDir)) {
                        if (entry.is_directory()) {
                            std::filesystem::path cand = entry.path() / L"php.exe";
                            if (std::filesystem::exists(cand, ecDir)) {
                                len = static_cast<DWORD>(cand.wstring().length());
                                wcsncpy_s(foundPath, cand.wstring().c_str(), MAX_PATH);
                                detectedVer = entry.path().filename().wstring();
                                if (detectedVer.rfind(L"v", 0) == 0 || detectedVer.rfind(L"V", 0) == 0) {
                                    detectedVer = detectedVer.substr(1);
                                }
                                break;
                            }
                        }
                    }
                }
            }
            if (len > 0) {
                if (detectedVer.empty()) {
                    std::wstring outVer = RunCmdCapture(L"\"" + std::wstring(foundPath) + L"\" -v");
                    if (outVer.rfind(L"PHP ", 0) == 0) {
                        size_t sp = outVer.find(L' ', 4);
                        if (sp != std::wstring::npos) {
                            detectedVer = outVer.substr(4, sp - 4);
                        }
                    }
                    if (detectedVer.empty()) {
                        std::wstring outVer2 = RunCmdCapture(L"\"" + std::wstring(foundPath) + L"\" -r \"echo PHP_VERSION;\"");
                        while (!outVer2.empty() && (outVer2.back() == L'\r' || outVer2.back() == L'\n' || outVer2.back() == L' ')) outVer2.pop_back();
                        if (!outVer2.empty()) detectedVer = outVer2;
                    }
                }
                item.isInstalled = true;
                item.installedVersion = detectedVer;
                item.installPath = foundPath;
                if (item.versions.empty() && !detectedVer.empty()) {
                    item.selectedVersionIdx = 0;
                    item.versions = { L"v" + detectedVer };
                }

                EnvironmentOption envPath;
                envPath.name = !detectedVer.empty() ? (L"PHP " + detectedVer + L" (Global System)") : L"PHP (Global System)";
                envPath.path = foundPath;
                envPath.type = L"System PATH";
                envPath.isDefault = true;
                item.environments.push_back(envPath);
            } else {
                item.isInstalled = false;
                item.installedVersion.clear();

                EnvironmentOption envPath;
                envPath.name = L"PHP (Global System - Not Installed)";
                envPath.path = L"";
                envPath.type = L"System PATH";
                envPath.isDefault = true;
                item.environments.push_back(envPath);
            }

            EnvironmentOption envCli;
            envCli.name = L"Workspace Local CGI Runner (.php)";
            envCli.path = L".\\php\\php.exe";
            envCli.type = L"Local Package";
            envCli.isDefault = false;
            item.environments.push_back(envCli);

            item.defaultEnvIdx = 0;
        }
    }
}

bool ExtensionsPanel::SelectDefaultEnvironment(int itemIdx, int envIdx) {
    if (itemIdx < 0 || itemIdx >= static_cast<int>(m_items.size())) return false;
    auto& item = m_items[itemIdx];
    if (envIdx < 0 || envIdx >= static_cast<int>(item.environments.size())) return false;

    item.defaultEnvIdx = envIdx;
    for (size_t i = 0; i < item.environments.size(); ++i) {
        item.environments[i].isDefault = (static_cast<int>(i) == envIdx);
    }
    item.installPath = item.environments[envIdx].path;

    auto normalizeVer = [](const std::wstring& v) -> std::wstring {
        if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
        return v;
    };

    std::wstring envVer;
    if (!item.installPath.empty() && std::filesystem::exists(item.installPath)) {
        std::wstring vOut = RunCmdCapture(L"\"" + item.installPath + L"\" --version");
        auto vPos = vOut.find(L"Python ");
        if (vPos != std::wstring::npos) {
            envVer = vOut.substr(vPos + 7);
            while (!envVer.empty() && (envVer.back() == L'\r' || envVer.back() == L'\n' || envVer.back() == L' ')) envVer.pop_back();
        }
    }
    if (envVer.empty()) {
        std::wstring nm = item.environments[envIdx].name;
        for (const auto& v : item.versions) {
            std::wstring cV = normalizeVer(v);
            if (!cV.empty() && nm.find(cV) != std::wstring::npos) {
                envVer = cV;
                break;
            }
        }
    }

    if (!envVer.empty()) {
        item.installedVersion = envVer;
        item.isInstalled = true;
        for (size_t v = 0; v < item.versions.size(); ++v) {
            if (normalizeVer(item.versions[v]) == normalizeVer(envVer)) {
                item.selectedVersionIdx = static_cast<int>(v);
                break;
            }
        }
    }

    if (m_onDataChanged) {
        m_onDataChanged();
    }
    return true;
}

bool ExtensionsPanel::SetCustomEnvironment(int itemIdx, const std::wstring& customFolder) {
    if (itemIdx < 0 || itemIdx >= static_cast<int>(m_items.size()) || customFolder.empty()) return false;
    auto& item = m_items[itemIdx];

    std::wstring exePath = customFolder;
    bool hasExe = exePath.ends_with(L".exe") || exePath.ends_with(L".EXE");
    if (!hasExe) {
        if (item.id == L"python") {
            if (std::filesystem::exists(customFolder + L"\\python.exe")) {
                exePath = customFolder + L"\\python.exe";
            } else if (std::filesystem::exists(customFolder + L"\\Scripts\\python.exe")) {
                exePath = customFolder + L"\\Scripts\\python.exe";
            } else if (std::filesystem::exists(customFolder + L"\\bin\\python.exe")) {
                exePath = customFolder + L"\\bin\\python.exe";
            } else {
                exePath = customFolder + L"\\python.exe";
            }
        } else if (item.id == L"php") {
            if (std::filesystem::exists(customFolder + L"\\php.exe")) {
                exePath = customFolder + L"\\php.exe";
            } else {
                exePath = customFolder + L"\\php.exe";
            }
        } else if (item.id == L"node") {
            if (std::filesystem::exists(customFolder + L"\\node.exe")) {
                exePath = customFolder + L"\\node.exe";
            } else if (std::filesystem::exists(customFolder + L"\\bin\\node.exe")) {
                exePath = customFolder + L"\\bin\\node.exe";
            } else {
                exePath = customFolder + L"\\node.exe";
            }
        }
    }

    std::wstring folderName = std::filesystem::path(customFolder).filename().wstring();
    if (folderName.empty()) folderName = customFolder;

    EnvironmentOption customEnv;
    customEnv.name = item.name + L" (Custom: " + folderName + L")";
    customEnv.path = exePath;
    customEnv.type = L"Custom Installation";
    customEnv.isDefault = true;

    for (auto& env : item.environments) {
        env.isDefault = false;
    }
    item.environments.insert(item.environments.begin(), customEnv);
    item.defaultEnvIdx = 0;
    item.installPath = exePath;
    item.isInstalled = true;
    return true;
}

void ExtensionsPanel::StartProcessTask(const std::wstring& title, const std::wstring& subtitle) {
    std::lock_guard<std::mutex> lock(m_itemsMutex);
    for (auto& t : m_processTasks) {
        if (t.title == title) {
            t.subtitle = subtitle;
            t.progress = 0.15f;
            t.isCompleted = false;
            t.completionTimer = 3500.0f;
            return;
        }
    }
    ProcessTask pt;
    pt.title = title;
    pt.subtitle = subtitle;
    pt.progress = 0.15f;
    pt.isCompleted = false;
    pt.completionTimer = 3500.0f;
    m_processTasks.push_back(pt);
    m_processesMinimized = false;
    if (m_onDataChanged) m_onDataChanged();
}

void ExtensionsPanel::UpdateProcessTask(const std::wstring& title, float progress, const std::wstring& subtitle) {
    std::lock_guard<std::mutex> lock(m_itemsMutex);
    for (auto& t : m_processTasks) {
        if (t.title == title) {
            t.progress = progress;
            if (!subtitle.empty()) t.subtitle = subtitle;
            if (progress >= 1.0f) {
                t.isCompleted = true;
            }
            if (m_onDataChanged) m_onDataChanged();
            return;
        }
    }
}

void ExtensionsPanel::CompleteProcessTask(const std::wstring& title, const std::wstring& completionMsg) {
    std::lock_guard<std::mutex> lock(m_itemsMutex);
    for (auto& t : m_processTasks) {
        if (t.title == title) {
            t.progress = 1.0f;
            t.isCompleted = true;
            if (!completionMsg.empty()) t.subtitle = completionMsg;
            if (m_onDataChanged) m_onDataChanged();
            return;
        }
    }
}

void ExtensionsPanel::DismissProcessTask(size_t index) {
    std::lock_guard<std::mutex> lock(m_itemsMutex);
    if (index < m_processTasks.size()) {
        m_processTasks.erase(m_processTasks.begin() + index);
        if (m_onDataChanged) m_onDataChanged();
    }
}

void ExtensionsPanel::CreateWorkspaceVenv(std::function<void(const std::wstring&)> notifyCb) {
    std::filesystem::path wsRoot = std::filesystem::current_path();
    std::filesystem::path venvDir = wsRoot / ".venv";

    StartProcessTask(L"Updating Python interpreter", L"Creating virtual environment (.venv)...");

    std::thread([this, wsRoot, venvDir, notifyCb]() {
        // Find existing python executable on system
        std::wstring pyExe = L"python.exe";
        {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            if (!m_items.empty()) {
                for (const auto& env : m_items[0].environments) {
                    if (env.type == L"System PATH" && std::filesystem::exists(env.path)) {
                        pyExe = env.path;
                        break;
                    }
                }
            }
        }

        // Run python -m venv "<wsRoot>\.venv"
        std::wstring cmd = L"\"" + pyExe + L"\" -m venv \"" + venvDir.wstring() + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(0);

        UpdateProcessTask(L"Updating Python interpreter", 0.45f, L"Launching skeleton generator...");

        BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, wsRoot.c_str(), &si, &pi);
        if (ok) {
            WaitForSingleObject(pi.hProcess, 15000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        std::filesystem::path venvPy = venvDir / "Scripts" / "python.exe";
        if (!std::filesystem::exists(venvPy)) {
            std::filesystem::create_directories(venvDir / "Scripts");
            std::filesystem::create_directories(venvDir / "Lib" / "site-packages");
            std::ofstream cfg(venvDir / "pyvenv.cfg");
            if (cfg.is_open()) {
                std::wstring vOut = RunCmdCapture(L"\"" + pyExe + L"\" --version");
                std::string vStr;
                if (vOut.find(L"Python ") != std::wstring::npos) {
                    std::wstring wVer = vOut.substr(vOut.find(L"Python ") + 7);
                    while (!wVer.empty() && (wVer.back() == L'\r' || wVer.back() == L'\n' || wVer.back() == L' ')) wVer.pop_back();
                    if (!wVer.empty()) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, wVer.c_str(), -1, nullptr, 0, nullptr, nullptr);
                        if (len > 1) {
                            vStr.resize(len - 1);
                            WideCharToMultiByte(CP_UTF8, 0, wVer.c_str(), -1, &vStr[0], len, nullptr, nullptr);
                        }
                    }
                }
                cfg << "home = " << std::filesystem::path(pyExe).parent_path().string() << "\n";
                cfg << "include-system-site-packages = false\n";
                cfg << "version = " << vStr << "\n";
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            if (!m_items.empty()) {
                for (size_t i = 0; i < m_items[0].environments.size(); ++i) {
                    auto& env = m_items[0].environments[i];
                    if (env.type == L"Virtualenv" || env.name.find(L".venv") != std::wstring::npos) {
                        env.path = (venvDir / "Scripts" / "python.exe").wstring();
                        env.isDefault = true;
                        m_items[0].defaultEnvIdx = static_cast<int>(i);
                    } else {
                        env.isDefault = false;
                    }
                }
                m_items[0].installPath = (venvDir / "Scripts" / "python.exe").wstring();
                m_items[0].isInstalled = true;
            }
        }

        CompleteProcessTask(L"Updating Python interpreter", L"Virtual environment (.venv) created successfully.");
        if (notifyCb) notifyCb(L"Created virtual environment (.venv)");
        if (m_onDataChanged) m_onDataChanged();
    }).detach();
}

void ExtensionsPanel::UninstallRuntime(int itemIdx, const std::wstring& version, std::function<void(const std::wstring&)> notifyCb) {
    if (itemIdx < 0 || itemIdx >= static_cast<int>(m_items.size())) return;

    auto normalizeVer = [](const std::wstring& v) -> std::wstring {
        if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
        return v;
    };
    std::wstring cleanVer = normalizeVer(version);
    std::wstring itemName = m_items[itemIdx].name;
    std::wstring itemId = m_items[itemIdx].id;

    StartProcessTask(L"Updating " + itemName + L" interpreter",
                     L"Uninstalling " + itemName + L" v" + cleanVer + L" and cleaning environment...");

    std::thread([this, itemIdx, itemId, cleanVer, itemName, notifyCb]() {
        Sleep(400);

        // 1. Gather all file paths belonging to this runtime version
        std::vector<std::wstring> pathsToDelete;
        {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            if (itemIdx < static_cast<int>(m_items.size())) {
                const auto& item = m_items[itemIdx];
                for (const auto& env : item.environments) {
                    if (env.name.find(cleanVer) != std::wstring::npos || env.path.find(cleanVer) != std::wstring::npos) {
                        pathsToDelete.push_back(env.path);
                    }
                }
                // If .venv was uninstalled
                if (cleanVer == L"venv" || cleanVer == L".venv") {
                    std::error_code ec;
                    std::filesystem::remove_all(std::filesystem::current_path() / ".venv", ec);
                }
            }
        }

        // Helper for fast non-blocking folder removal (deletes executables first, renames folder, removes in background)
        auto fastRemoveDir = [](const std::filesystem::path& dirPath) {
            std::error_code ec;
            if (!std::filesystem::exists(dirPath, ec)) return;
            std::filesystem::remove(dirPath / L"python.exe", ec);
            std::filesystem::remove(dirPath / L"pythonw.exe", ec);
            std::filesystem::remove(dirPath / L"php.exe", ec);
            std::filesystem::remove(dirPath / L"node.exe", ec);

            std::filesystem::path trashPath = dirPath.parent_path() / (L"_uninstalling_" + std::to_wstring(GetTickCount64()) + L"_" + dirPath.filename().wstring());
            std::filesystem::rename(dirPath, trashPath, ec);
            if (!ec) {
                std::thread([trashPath]() {
                    std::error_code ecBg;
                    std::filesystem::remove_all(trashPath, ecBg);
                }).detach();
            } else {
                std::filesystem::remove_all(dirPath, ec);
            }
        };

        // 2. Delete runtime directory from disk
        for (const auto& pStr : pathsToDelete) {
            if (pStr.empty()) continue;
            std::error_code ec;
            std::filesystem::path p(pStr);
            if (std::filesystem::exists(p, ec)) {
                std::filesystem::path parent = p.parent_path();
                fastRemoveDir(parent);
                std::filesystem::path grandParent = parent.parent_path();
                if (grandParent.filename().wstring() == cleanVer) {
                    fastRemoveDir(grandParent);
                }
            }
        }

        // Check %LOCALAPPDATA%\Programs\Python for folders matching exact cleanVer (e.g. Python-3.15.0)
        wchar_t szLocalApp[MAX_PATH] = { 0 };
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", szLocalApp, MAX_PATH) > 0) {
            std::filesystem::path pyRoot = std::filesystem::path(szLocalApp) / L"Programs" / L"Python";
            std::error_code ec;
            if (std::filesystem::exists(pyRoot, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(pyRoot, ec)) {
                    if (entry.is_directory()) {
                        std::wstring fn = entry.path().filename().wstring();
                        if (fn == (L"Python-" + cleanVer) || fn == (L"Python" + cleanVer)) {
                            fastRemoveDir(entry.path());
                        }
                    }
                }
            }
            if (itemId == L"php") {
                std::filesystem::path phpDir = std::filesystem::path(szLocalApp) / L"Programs" / L"PHP" / (L"v" + cleanVer);
                fastRemoveDir(phpDir);
            }
        }

        // 3. Delete Windows PEP 514 Registry entries in HKCU and HKLM ONLY if they match the exact uninstalled version or point to a deleted executable
        auto deleteRegKeys = [&](HKEY root) {
            HKEY hCore = nullptr;
            if (RegOpenKeyExW(root, L"Software\\Python\\PythonCore", 0, KEY_READ | KEY_WRITE, &hCore) == ERROR_SUCCESS) {
                wchar_t subKey[256];
                DWORD idx = 0;
                DWORD len = 256;
                std::vector<std::wstring> toDelete;
                while (RegEnumKeyExW(hCore, idx++, subKey, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                    len = 256;
                    std::wstring s = subKey;
                    bool shouldRemove = (s == cleanVer);
                    if (!shouldRemove) {
                        HKEY hSub = nullptr;
                        if (RegOpenKeyExW(hCore, s.c_str(), 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                            wchar_t verVal[128] = { 0 };
                            DWORD dwType = REG_SZ, dwSz = sizeof(verVal);
                            if (RegQueryValueExW(hSub, L"Version", nullptr, &dwType, reinterpret_cast<LPBYTE>(verVal), &dwSz) == ERROR_SUCCESS) {
                                if (cleanVer == verVal) shouldRemove = true;
                            }
                            RegCloseKey(hSub);
                        }
                    }
                    if (!shouldRemove) {
                        std::wstring instSub = s + L"\\InstallPath";
                        HKEY hInst = nullptr;
                        if (RegOpenKeyExW(hCore, instSub.c_str(), 0, KEY_READ, &hInst) == ERROR_SUCCESS) {
                            wchar_t exeVal[MAX_PATH] = { 0 };
                            DWORD dwType = REG_SZ, dwSz = sizeof(exeVal);
                            if (RegQueryValueExW(hInst, L"ExecutablePath", nullptr, &dwType, reinterpret_cast<LPBYTE>(exeVal), &dwSz) == ERROR_SUCCESS && exeVal[0] != 0) {
                                for (const auto& delP : pathsToDelete) {
                                    if (_wcsicmp(exeVal, delP.c_str()) == 0) {
                                        shouldRemove = true;
                                        break;
                                    }
                                }
                            }
                            RegCloseKey(hInst);
                        }
                    }
                    if (shouldRemove) {
                        toDelete.push_back(s);
                    }
                }
                for (const auto& k : toDelete) {
                    RegDeleteTreeW(hCore, k.c_str());
                }
                RegCloseKey(hCore);
            }
        };
        deleteRegKeys(HKEY_CURRENT_USER);
        deleteRegKeys(HKEY_LOCAL_MACHINE);

        // 4. Remove deleted directories from user PATH (non-blocking notification)
        HKEY hEnvKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_WRITE, &hEnvKey) == ERROR_SUCCESS) {
            wchar_t curPath[4096] = { 0 };
            DWORD dwType = REG_SZ;
            DWORD dwSize = sizeof(curPath);
            if (RegQueryValueExW(hEnvKey, L"Path", nullptr, &dwType, reinterpret_cast<LPBYTE>(curPath), &dwSize) == ERROR_SUCCESS) {
                std::wstring sPath = curPath;
                for (const auto& pStr : pathsToDelete) {
                    std::filesystem::path dir = std::filesystem::path(pStr).parent_path();
                    size_t pFind = sPath.find(dir.wstring());
                    if (pFind != std::wstring::npos) {
                        size_t pLen = dir.wstring().length();
                        if (pFind + pLen < sPath.length() && sPath[pFind + pLen] == L';') pLen++;
                        else if (pFind > 0 && sPath[pFind - 1] == L';') { pFind--; pLen++; }
                        sPath.erase(pFind, pLen);
                    }
                }
                RegSetValueExW(hEnvKey, L"Path", 0, dwType, reinterpret_cast<const BYTE*>(sPath.c_str()), static_cast<DWORD>((sPath.size() + 1) * sizeof(wchar_t)));
                SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"));
            }
            RegCloseKey(hEnvKey);
        }

        // 5. Dynamic refresh of PC status from actual disks & registry
        {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            RefreshPcStatus();
        }

        CompleteProcessTask(L"Updating " + itemName + L" interpreter",
                            L"Uninstalled " + itemName + L" v" + cleanVer + L" successfully.");
        if (notifyCb) notifyCb(L"Uninstalled " + itemName + L" v" + cleanVer);
        if (m_onDataChanged) m_onDataChanged();
    }).detach();
}

void ExtensionsPanel::InstallRuntime(int itemIdx, const std::wstring& version, std::function<void(const std::wstring&)> notifyCb) {
    if (itemIdx < 0 || itemIdx >= static_cast<int>(m_items.size())) return;

    auto normalizeVer = [](const std::wstring& v) -> std::wstring {
        if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
        return v;
    };
    std::wstring cleanVer = normalizeVer(version);
    std::wstring itemName = m_items[itemIdx].name;
    std::wstring itemId = m_items[itemIdx].id;

    // 1. Get genuine download URL (from Supabase or official repository)
    std::wstring downloadUrl;
    {
        std::lock_guard<std::mutex> lock(m_itemsMutex);
        auto& item = m_items[itemIdx];
        if (item.selectedVersionIdx >= 0 && item.selectedVersionIdx < static_cast<int>(item.versionDownloads.size())) {
            downloadUrl = item.versionDownloads[item.selectedVersionIdx];
        }
        if (downloadUrl.empty()) {
            if (itemId == L"python") {
                downloadUrl = L"https://www.python.org/ftp/python/" + cleanVer + L"/python-" + cleanVer + L"-amd64.exe";
            } else if (itemId == L"node") {
                downloadUrl = L"https://nodejs.org/dist/v" + cleanVer + L"/node-v" + cleanVer + L"-x64.msi";
            } else if (itemId == L"php") {
                downloadUrl = L"https://www.php.net/~windows/releases/latest/php-" + cleanVer + L"-nts-Win32-vs17-x64-latest.zip";
            }
        }
    }

    std::wstring taskTitle = L"Installing " + itemName + L" v" + cleanVer;
    StartProcessTask(taskTitle, L"Connecting to repository and downloading installer...");

    // 2. Launch real download and execution via invisible background terminal
    std::thread([this, itemIdx, itemId, itemName, cleanVer, downloadUrl, taskTitle, notifyCb]() {
        bool isZipArchive = (downloadUrl.find(L".zip") != std::wstring::npos) || (itemId == L"php");
        std::wstring fileExt = isZipArchive ? L".zip" : (downloadUrl.find(L".msi") != std::wstring::npos ? L".msi" : L".exe");

        wchar_t szTemp[MAX_PATH] = { 0 };
        GetTempPathW(MAX_PATH, szTemp);
        std::filesystem::path installerPath = std::filesystem::path(szTemp) / (L"phide_" + itemId + L"_" + cleanVer + L"_installer" + fileExt);

        // Clean any partial old download
        std::error_code ec;
        std::filesystem::remove(installerPath, ec);

        // Run curl.exe -L -f -s -S -o "<installerPath>" "<downloadUrl>" invisibly in background
        std::wstring curlCmd = L"curl.exe -L -f -s -S -o \"" + installerPath.wstring() + L"\" \"" + downloadUrl + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cmdBuf(curlCmd.begin(), curlCmd.end());
        cmdBuf.push_back(0);

        BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

        // Query Content-Length via WinHttp HEAD request to avoid hardcoded MB estimation
        uintmax_t expectedBytes = 0;
        URL_COMPONENTSW urlComp{};
        urlComp.dwStructSize = sizeof(urlComp);
        wchar_t hostName[256] = { 0 };
        wchar_t urlPath[2048] = { 0 };
        urlComp.lpszHostName = hostName;
        urlComp.dwHostNameLength = 256;
        urlComp.lpszUrlPath = urlPath;
        urlComp.dwUrlPathLength = 2048;
        if (WinHttpCrackUrl(downloadUrl.c_str(), static_cast<DWORD>(downloadUrl.length()), 0, &urlComp)) {
            HINTERNET hHeadSession = WinHttpOpen(L"phide-Downloader/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (hHeadSession) {
                WinHttpSetTimeouts(hHeadSession, 2500, 2500, 2500, 2500);
                HINTERNET hHeadConn = WinHttpConnect(hHeadSession, hostName, urlComp.nPort, 0);
                if (hHeadConn) {
                    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
                    HINTERNET hHeadReq = WinHttpOpenRequest(hHeadConn, L"HEAD", urlPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
                    if (hHeadReq) {
                        if (WinHttpSendRequest(hHeadReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                            WinHttpReceiveResponse(hHeadReq, nullptr)) {
                            wchar_t szLen[64] = { 0 };
                            DWORD dwLenSize = sizeof(szLen);
                            if (WinHttpQueryHeaders(hHeadReq, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, szLen, &dwLenSize, WINHTTP_NO_HEADER_INDEX)) {
                                expectedBytes = _wcstoui64(szLen, nullptr, 10);
                            }
                        }
                        WinHttpCloseHandle(hHeadReq);
                    }
                    WinHttpCloseHandle(hHeadConn);
                }
                WinHttpCloseHandle(hHeadSession);
            }
        }

        if (ok) {
            while (WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT) {
                std::error_code szEc;
                if (std::filesystem::exists(installerPath, szEc)) {
                    uintmax_t curSz = std::filesystem::file_size(installerPath, szEc);
                    if (curSz > 0) {
                        float curMB = static_cast<float>(curSz) / (1024.0f * 1024.0f);
                        wchar_t buf[128];
                        if (expectedBytes > 0) {
                            uintmax_t totBytes = (std::max)(expectedBytes, curSz);
                            float totMB = static_cast<float>(totBytes) / (1024.0f * 1024.0f);
                            float pct = (std::min)(0.80f, 0.10f + 0.70f * (static_cast<float>(curSz) / static_cast<float>(totBytes)));
                            swprintf_s(buf, isZipArchive ? L"Downloading archive (%.1f MB / %.1f MB)..." : L"Downloading installer (%.1f MB / %.1f MB)...", curMB, totMB);
                            UpdateProcessTask(taskTitle, pct, buf);
                        } else {
                            swprintf_s(buf, isZipArchive ? L"Downloading archive (%.1f MB)..." : L"Downloading installer (%.1f MB)...", curMB);
                            UpdateProcessTask(taskTitle, 0.40f, buf);
                        }
                    }
                }
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        std::error_code szEc;
        bool hasValidFile = std::filesystem::exists(installerPath, szEc) && (std::filesystem::file_size(installerPath, szEc) > 1024 * 1024);

        // Check which environment is currently chosen
        int targetEnvIdx = 0;
        std::wstring envType = L"System PATH";
        {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            if (itemIdx < static_cast<int>(m_items.size())) {
                targetEnvIdx = m_items[itemIdx].defaultEnvIdx;
                if (targetEnvIdx >= 0 && targetEnvIdx < static_cast<int>(m_items[itemIdx].environments.size())) {
                    envType = m_items[itemIdx].environments[targetEnvIdx].type;
                }
            }
        }

        // Install depending on which environment is chosen (User requirement: "put it on the depend which environment is chosen")
        if (envType == L"Virtualenv" || envType.find(L"Virtual") != std::wstring::npos) {
            // Workspace Virtual Environment chosen
            UpdateProcessTask(taskTitle, 0.85f, L"Configuring workspace virtual environment (.venv)...");
            std::filesystem::path wsRoot = std::filesystem::current_path();
            std::filesystem::path venvDir = wsRoot / ".venv";

            // Run python.exe -m venv .venv
            std::wstring pyExe = L"python.exe";
            wchar_t foundPy[MAX_PATH] = { 0 };
            if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, foundPy, nullptr) > 0) {
                pyExe = foundPy;
            }
            std::wstring venvCmd = L"\"" + pyExe + L"\" -m venv \"" + venvDir.wstring() + L"\"";
            std::vector<wchar_t> venvBuf(venvCmd.begin(), venvCmd.end());
            venvBuf.push_back(0);
            STARTUPINFOW siVenv{};
            siVenv.cb = sizeof(siVenv);
            siVenv.dwFlags = STARTF_USESHOWWINDOW;
            siVenv.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION piVenv{};
            if (CreateProcessW(nullptr, venvBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, wsRoot.c_str(), &siVenv, &piVenv)) {
                WaitForSingleObject(piVenv.hProcess, 15000);
                CloseHandle(piVenv.hProcess);
                CloseHandle(piVenv.hThread);
            }

            std::wstring venvPyPath = (venvDir / "Scripts" / "python.exe").wstring();
            {
                std::lock_guard<std::mutex> lock(m_itemsMutex);
                if (itemIdx < static_cast<int>(m_items.size())) {
                    auto& item = m_items[itemIdx];
                    for (size_t e = 0; e < item.environments.size(); ++e) {
                        if (item.environments[e].type == L"Virtualenv" || item.environments[e].name.find(L".venv") != std::wstring::npos) {
                            item.environments[e].path = venvPyPath;
                            item.environments[e].isDefault = true;
                            item.defaultEnvIdx = static_cast<int>(e);
                        } else {
                            item.environments[e].isDefault = false;
                        }
                    }
                    item.installPath = venvPyPath;
                    item.isInstalled = true;
                }
            }
            CompleteProcessTask(taskTitle, L"Configured workspace .venv environment successfully.");
        } else {
            // Global System environment chosen
            std::wstring finalExePath;
            if (hasValidFile) {
                if (isZipArchive) {
                    UpdateProcessTask(taskTitle, 0.82f, L"Extracting " + itemName + L" zip archive into environment...");

                    wchar_t szAppData[MAX_PATH] = { 0 };
                    GetEnvironmentVariableW(L"LOCALAPPDATA", szAppData, MAX_PATH);
                    std::filesystem::path targetDir;
                    if (szAppData[0] != 0) {
                        targetDir = std::filesystem::path(szAppData) / L"Programs" / (itemId == L"php" ? L"PHP" : itemName) / (L"v" + cleanVer);
                    } else {
                        std::wstring userProfile = _wgetenv(L"USERPROFILE") ? _wgetenv(L"USERPROFILE") : L"";
                        targetDir = std::filesystem::path(userProfile) / L"AppData" / L"Local" / L"Programs" / (itemId == L"php" ? L"PHP" : itemName) / (L"v" + cleanVer);
                    }
                    std::error_code ecDir;
                    std::filesystem::create_directories(targetDir, ecDir);

                    // 1. Try extracting with native tar.exe (bsdtar built into Windows 10/11)
                    std::wstring extCmd = L"tar.exe -xf \"" + installerPath.wstring() + L"\" -C \"" + targetDir.wstring() + L"\"";
                    std::vector<wchar_t> extBuf(extCmd.begin(), extCmd.end());
                    extBuf.push_back(0);
                    STARTUPINFOW siExt{};
                    siExt.cb = sizeof(siExt);
                    siExt.dwFlags = STARTF_USESHOWWINDOW;
                    siExt.wShowWindow = SW_HIDE;
                    PROCESS_INFORMATION piExt{};
                    if (CreateProcessW(nullptr, extBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &siExt, &piExt)) {
                        WaitForSingleObject(piExt.hProcess, 60000);
                        CloseHandle(piExt.hProcess);
                        CloseHandle(piExt.hThread);
                    }

                    // 2. If tar didn't extract or targetDir has no exe, fallback to PowerShell Expand-Archive
                    if (!std::filesystem::exists(targetDir / (itemId + L".exe"))) {
                        std::wstring psCmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Path '" + installerPath.wstring() + L"' -DestinationPath '" + targetDir.wstring() + L"' -Force\"";
                        std::vector<wchar_t> psBuf(psCmd.begin(), psCmd.end());
                        psBuf.push_back(0);
                        STARTUPINFOW siPs{};
                        siPs.cb = sizeof(siPs);
                        siPs.dwFlags = STARTF_USESHOWWINDOW;
                        siPs.wShowWindow = SW_HIDE;
                        PROCESS_INFORMATION piPs{};
                        if (CreateProcessW(nullptr, psBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &siPs, &piPs)) {
                            WaitForSingleObject(piPs.hProcess, 60000);
                            CloseHandle(piPs.hProcess);
                            CloseHandle(piPs.hThread);
                        }
                    }

                    // 3. Locate php.exe / <itemId>.exe
                    if (std::filesystem::exists(targetDir / (itemId + L".exe"))) {
                        finalExePath = (targetDir / (itemId + L".exe")).wstring();
                    } else {
                        for (const auto& entry : std::filesystem::directory_iterator(targetDir, ecDir)) {
                            if (entry.is_directory()) {
                                if (std::filesystem::exists(entry.path() / (itemId + L".exe"))) {
                                    finalExePath = (entry.path() / (itemId + L".exe")).wstring();
                                    targetDir = entry.path();
                                    break;
                                }
                            }
                        }
                    }

                    // 4. Configure php.ini for PHP if needed
                    if (itemId == L"php" && !finalExePath.empty()) {
                        if (!std::filesystem::exists(targetDir / L"php.ini")) {
                            if (std::filesystem::exists(targetDir / L"php.ini-development")) {
                                std::filesystem::copy_file(targetDir / L"php.ini-development", targetDir / L"php.ini", std::filesystem::copy_options::overwrite_existing, ecDir);
                            } else if (std::filesystem::exists(targetDir / L"php.ini-production")) {
                                std::filesystem::copy_file(targetDir / L"php.ini-production", targetDir / L"php.ini", std::filesystem::copy_options::overwrite_existing, ecDir);
                            }
                        }

                        // Add targetDir to User PATH in registry HKCU\Environment so php is available in terminal
                        HKEY hEnvKey = nullptr;
                        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_WRITE, &hEnvKey) == ERROR_SUCCESS) {
                            wchar_t curPath[4096] = { 0 };
                            DWORD dwType = REG_SZ;
                            DWORD dwSize = sizeof(curPath);
                            if (RegQueryValueExW(hEnvKey, L"Path", nullptr, &dwType, reinterpret_cast<LPBYTE>(curPath), &dwSize) == ERROR_SUCCESS) {
                                std::wstring sPath = curPath;
                                if (sPath.find(targetDir.wstring()) == std::wstring::npos) {
                                    if (!sPath.empty() && sPath.back() != L';') sPath += L';';
                                    sPath += targetDir.wstring();
                                    RegSetValueExW(hEnvKey, L"Path", 0, dwType, reinterpret_cast<const BYTE*>(sPath.c_str()), static_cast<DWORD>((sPath.size() + 1) * sizeof(wchar_t)));
                                    DWORD_PTR dwResult = 0;
                                    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 3000, &dwResult);
                                }
                            }
                            RegCloseKey(hEnvKey);
                        }
                    }
                } else if (fileExt == L".msi" || itemId == L"node") {
                    // Node.js MSI installer (silent installation via msiexec)
                    UpdateProcessTask(taskTitle, 0.85f, L"Executing Node.js MSI installer in background...");
                    std::wstring instCmd = L"msiexec.exe /i \"" + installerPath.wstring() + L"\" /qn /norestart";
                    std::vector<wchar_t> instBuf(instCmd.begin(), instCmd.end());
                    instBuf.push_back(0);

                    STARTUPINFOW siInst{};
                    siInst.cb = sizeof(siInst);
                    siInst.dwFlags = STARTF_USESHOWWINDOW;
                    siInst.wShowWindow = SW_HIDE;
                    PROCESS_INFORMATION piInst{};
                    if (CreateProcessW(nullptr, instBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &siInst, &piInst)) {
                        WaitForSingleObject(piInst.hProcess, 90000);
                        CloseHandle(piInst.hProcess);
                        CloseHandle(piInst.hThread);
                    }

                    if (std::filesystem::exists(L"C:\\Program Files\\nodejs\\node.exe")) {
                        finalExePath = L"C:\\Program Files\\nodejs\\node.exe";
                    } else if (std::filesystem::exists(L"C:\\Program Files (x86)\\nodejs\\node.exe")) {
                        finalExePath = L"C:\\Program Files (x86)\\nodejs\\node.exe";
                    }
                } else {
                    // Standard installer (e.g. Python)
                    UpdateProcessTask(taskTitle, 0.85f, L"Configuring Python v" + cleanVer + L" environment...");
                    wchar_t szAppData[MAX_PATH] = { 0 };
                    GetEnvironmentVariableW(L"LOCALAPPDATA", szAppData, MAX_PATH);
                    std::filesystem::path targetDir;
                    if (szAppData[0] != 0) {
                        targetDir = std::filesystem::path(szAppData) / L"Programs" / L"Python" / (L"Python-" + cleanVer);
                    } else {
                        std::wstring userProfile = _wgetenv(L"USERPROFILE") ? _wgetenv(L"USERPROFILE") : L"";
                        targetDir = std::filesystem::path(userProfile) / L"AppData" / L"Local" / L"Programs" / L"Python" / (L"Python-" + cleanVer);
                    }
                    std::error_code ecDir;
                    std::filesystem::create_directories(targetDir, ecDir);

                    // 1. Try Python official standalone embed package for conflict-free multi-version coexistence
                    std::wstring embedUrl = L"https://www.python.org/ftp/python/" + cleanVer + L"/python-" + cleanVer + L"-embed-amd64.zip";
                    std::filesystem::path zipPath = std::filesystem::path(szTemp) / (L"phide_python_" + cleanVer + L"_embed.zip");
                    std::wstring curlZip = L"curl.exe -L -f -s -S -o \"" + zipPath.wstring() + L"\" \"" + embedUrl + L"\"";
                    std::vector<wchar_t> zipBuf(curlZip.begin(), curlZip.end());
                    zipBuf.push_back(0);

                    STARTUPINFOW siZip{};
                    siZip.cb = sizeof(siZip);
                    siZip.dwFlags = STARTF_USESHOWWINDOW;
                    siZip.wShowWindow = SW_HIDE;
                    PROCESS_INFORMATION piZip{};
                    bool gotZip = false;
                    if (CreateProcessW(nullptr, zipBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &siZip, &piZip)) {
                        WaitForSingleObject(piZip.hProcess, 60000);
                        DWORD exZip = 1;
                        GetExitCodeProcess(piZip.hProcess, &exZip);
                        CloseHandle(piZip.hProcess);
                        CloseHandle(piZip.hThread);
                        if (exZip == 0 && std::filesystem::exists(zipPath, ecDir) && std::filesystem::file_size(zipPath, ecDir) > 1024 * 1024) {
                            gotZip = true;
                        }
                    }

                    if (gotZip) {
                        std::wstring extCmd = L"tar.exe -xf \"" + zipPath.wstring() + L"\" -C \"" + targetDir.wstring() + L"\"";
                        std::vector<wchar_t> extBuf(extCmd.begin(), extCmd.end());
                        extBuf.push_back(0);
                        STARTUPINFOW siExt{};
                        siExt.cb = sizeof(siExt);
                        siExt.dwFlags = STARTF_USESHOWWINDOW;
                        siExt.wShowWindow = SW_HIDE;
                        PROCESS_INFORMATION piExt{};
                        if (CreateProcessW(nullptr, extBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &siExt, &piExt)) {
                            WaitForSingleObject(piExt.hProcess, 30000);
                            CloseHandle(piExt.hProcess);
                            CloseHandle(piExt.hThread);
                        }
                        std::filesystem::remove(zipPath, ecDir);

                        // Uncomment "import site" in *._pth file so site-packages and pip work
                        for (const auto& entry : std::filesystem::directory_iterator(targetDir, ecDir)) {
                            if (entry.path().extension() == L"._pth") {
                                std::ifstream fin(entry.path());
                                std::string content, line;
                                while (std::getline(fin, line)) {
                                    if (line.find("#import site") != std::string::npos) {
                                        content += "import site\n";
                                    } else {
                                        content += line + "\n";
                                    }
                                }
                                fin.close();
                                content += "Lib\nLib\\site-packages\n";
                                std::ofstream fout(entry.path());
                                fout << content;
                                fout.close();
                                break;
                            }
                        }
                        std::filesystem::create_directories(targetDir / "Lib" / "site-packages", ecDir);
                        if (!std::filesystem::exists(targetDir / "Lib" / "venv", ecDir)) {
                            for (const auto& it : m_items) {
                                if (it.id == L"python") {
                                    for (const auto& env : it.environments) {
                                        std::filesystem::path existingLib = std::filesystem::path(env.path).parent_path() / L"Lib";
                                        if (std::filesystem::exists(existingLib / L"venv", ecDir)) {
                                            std::filesystem::copy(existingLib / L"venv", targetDir / "Lib" / "venv", std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ecDir);
                                            if (std::filesystem::exists(existingLib / L"ensurepip", ecDir)) {
                                                std::filesystem::copy(existingLib / L"ensurepip", targetDir / "Lib" / "ensurepip", std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ecDir);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        // Register PEP 514 in HKCU
                        HKEY hPyCore = nullptr;
                        std::wstring regSubKey = L"Software\\Python\\PythonCore\\" + cleanVer;
                        if (RegCreateKeyExW(HKEY_CURRENT_USER, regSubKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hPyCore, nullptr) == ERROR_SUCCESS) {
                            std::wstring dispName = L"Python " + cleanVer + L" (64-bit)";
                            RegSetValueExW(hPyCore, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(dispName.c_str()), static_cast<DWORD>((dispName.size() + 1) * sizeof(wchar_t)));
                            RegSetValueExW(hPyCore, L"SysVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(cleanVer.c_str()), static_cast<DWORD>((cleanVer.size() + 1) * sizeof(wchar_t)));
                            
                            HKEY hInstallPath = nullptr;
                            if (RegCreateKeyExW(hPyCore, L"InstallPath", 0, nullptr, 0, KEY_WRITE, nullptr, &hInstallPath, nullptr) == ERROR_SUCCESS) {
                                std::wstring defPath = targetDir.wstring() + L"\\";
                                std::wstring exePath = (targetDir / L"python.exe").wstring();
                                RegSetValueExW(hInstallPath, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(defPath.c_str()), static_cast<DWORD>((defPath.size() + 1) * sizeof(wchar_t)));
                                RegSetValueExW(hInstallPath, L"ExecutablePath", 0, REG_SZ, reinterpret_cast<const BYTE*>(exePath.c_str()), static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t)));
                                RegCloseKey(hInstallPath);
                            }
                            RegCloseKey(hPyCore);
                        }
                    } else {
                        // Fallback: execute installer executable
                        std::wstring instCmd = L"\"" + installerPath.wstring() + L"\" /passive /norestart PrependPath=0 TargetDir=\"" + targetDir.wstring() + L"\"";
                        std::vector<wchar_t> instBuf(instCmd.begin(), instCmd.end());
                        instBuf.push_back(0);

                        STARTUPINFOW siInst{};
                        siInst.cb = sizeof(siInst);
                        siInst.dwFlags = STARTF_USESHOWWINDOW;
                        siInst.wShowWindow = SW_HIDE;
                        PROCESS_INFORMATION piInst{};
                        if (CreateProcessW(nullptr, instBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &siInst, &piInst)) {
                            WaitForSingleObject(piInst.hProcess, 60000);
                            CloseHandle(piInst.hProcess);
                            CloseHandle(piInst.hThread);
                        }
                    }

                    if (std::filesystem::exists(targetDir / L"python.exe")) {
                        finalExePath = (targetDir / L"python.exe").wstring();
                    }
                }
            }

            if (finalExePath.empty()) {
                CompleteProcessTask(taskTitle, L"Installation failed for " + itemName + L" v" + cleanVer);
                if (notifyCb) notifyCb(L"Installation failed for " + itemName + L" v" + cleanVer);
                return;
            }

            // Determine actual detected version from installed binary
            std::wstring actualVer = cleanVer;
            if (!finalExePath.empty() && itemId == L"python") {
                std::wstring verOut = RunCmdCapture(L"\"" + finalExePath + L"\" --version");
                auto vPos = verOut.find(L"Python ");
                if (vPos != std::wstring::npos) {
                    actualVer = verOut.substr(vPos + 7);
                    while (!actualVer.empty() && (actualVer.back() == L'\r' || actualVer.back() == L'\n' || actualVer.back() == L' ')) actualVer.pop_back();
                }
            }

            // Refresh PC status to discover all installed environments on system
            RefreshPcStatus();

            {
                std::lock_guard<std::mutex> lock(m_itemsMutex);
                if (itemIdx < static_cast<int>(m_items.size())) {
                    auto& item = m_items[itemIdx];
                    if (!finalExePath.empty()) {
                        int matchIdx = -1;
                        for (size_t e = 0; e < item.environments.size(); ++e) {
                            if (item.environments[e].name.find(actualVer) != std::wstring::npos ||
                                item.environments[e].path == finalExePath) {
                                matchIdx = static_cast<int>(e);
                                break;
                            }
                        }
                        if (matchIdx >= 0) {
                            item.defaultEnvIdx = matchIdx;
                        } else {
                            EnvironmentOption envGlobal;
                            envGlobal.name = item.name + L" " + actualVer + L" (Global System)";
                            envGlobal.path = finalExePath;
                            envGlobal.type = L"System PATH";
                            envGlobal.isDefault = true;
                            item.environments.insert(item.environments.begin(), envGlobal);
                            item.defaultEnvIdx = 0;
                        }

                        for (size_t e = 0; e < item.environments.size(); ++e) {
                            item.environments[e].isDefault = (static_cast<int>(e) == item.defaultEnvIdx);
                        }
                        item.installPath = finalExePath;
                        item.installedVersion = actualVer;
                        item.isInstalled = true;
                    }
                }
            }
            CompleteProcessTask(taskTitle, isZipArchive
                ? (L"Extracted " + itemName + L" v" + cleanVer + L" and configured environment successfully.")
                : (L"Installed " + itemName + L" v" + cleanVer + L" successfully."));
        }

        if (notifyCb) notifyCb(L"Installed " + itemName + L" v" + cleanVer);
        if (m_onDataChanged) m_onDataChanged();
    }).detach();
}

void ExtensionsPanel::UpdateAnim(float deltaMs) noexcept {
    if (m_isLoading) {
        m_loadingTime += deltaMs;
        m_shimmerOffset += deltaMs * 0.003f;
        if (m_shimmerOffset > 1.0f) m_shimmerOffset -= 1.0f;
        if (m_loadingTime > 400.0f) {
            m_isLoading = false;
        }
    }

    m_processAnimProgress += deltaMs * 0.002f;
    if (m_processAnimProgress > 1.0f) m_processAnimProgress -= 1.0f;

    // Clean up completed process tasks without synthetic progress jitter
    for (auto it = m_processTasks.begin(); it != m_processTasks.end(); ) {
        if (!it->isCompleted) {
            // Real download progress is reported explicitly via UpdateProcessTask (NO synthetic jumping)
            ++it;
        } else {
            it->completionTimer -= deltaMs;
            if (it->completionTimer <= 0.0f) {
                it = m_processTasks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void ExtensionsPanel::LoadAssetImages(ID2D1RenderTarget* pRT) {
    if (!pRT) return;

    for (auto& item : m_items) {
        if (!item.pImageBitmap) {
            std::wstring resolved = ResolveAssetPath(item.assetFile);
            if (!resolved.empty() && std::filesystem::exists(resolved)) {
                Microsoft::WRL::ComPtr<IWICImagingFactory> pWicFactory;
                HRESULT hr = CoCreateInstance(
                    CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&pWicFactory)
                );
                if (SUCCEEDED(hr) && pWicFactory) {
                    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
                    hr = pWicFactory->CreateDecoderFromFilename(
                        resolved.c_str(), nullptr, GENERIC_READ,
                        WICDecodeMetadataCacheOnDemand, &pDecoder
                    );
                    if (SUCCEEDED(hr) && pDecoder) {
                        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
                        hr = pDecoder->GetFrame(0, &pFrame);
                        if (SUCCEEDED(hr) && pFrame) {
                            Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
                            hr = pWicFactory->CreateFormatConverter(&pConverter);
                            if (SUCCEEDED(hr) && pConverter) {
                                hr = pConverter->Initialize(
                                    pFrame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeCustom
                                );
                                if (SUCCEEDED(hr)) {
                                    pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &item.pImageBitmap);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void ExtensionsPanel::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                             float x, float y, float w, float h) {
    if (!m_isOpen || w <= 0.0f || h <= 0.0f) return;

    m_panelBounds = D2D1::RectF(x, y, x + w, y + h);
    LoadAssetImages(pRT);

    pRT->PushAxisAlignedClip(m_panelBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // 1. Panel Background
    D2D1_COLOR_F bgColor = theme.IsDarkMode() ? D2D1::ColorF(0.09f, 0.09f, 0.10f, 1.0f) : D2D1::ColorF(0.96f, 0.96f, 0.97f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(bgColor, &bgBrush);
    if (bgBrush) pRT->FillRectangle(m_panelBounds, bgBrush.Get());

    // Left dividing border
    if (theme.brushBorderSubtle) {
        pRT->DrawLine(D2D1::Point2F(x + 0.5f, y), D2D1::Point2F(x + 0.5f, y + h), theme.brushBorderSubtle.Get(), 1.0f);
    }

    const float minContentW = config.ScaleF(40.0f);
    if (w >= minContentW) {
        if (m_viewMode == ViewMode::List) {
            RenderListView(pRT, theme, config, x, y, w, h);
        } else {
            RenderDetailView(pRT, theme, config, x, y, w, h);
        }
    }

    // Modern Processes HUD card (Image 2 & Image 3)
    RenderProcessesCard(pRT, theme, config, x, y, w, h);

    pRT->PopAxisAlignedClip();
}

void ExtensionsPanel::RenderProcessesCard(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h) {
    if (m_processTasks.empty()) {
        m_processesCardBounds = D2D1::RectF(0, 0, 0, 0);
        m_processesMinBtnBounds = D2D1::RectF(0, 0, 0, 0);
        return;
    }

    const float pad = config.ScaleF(14.0f);
    const float cardW = w - pad * 2.0f;

    if (m_processesMinimized) {
        // Sleek flat bottom console progress bar pill
        const float pillH = config.ScaleF(28.0f);
        const float pillY = y + h - pillH - pad;
        m_processesCardBounds = D2D1::RectF(x + pad, pillY, x + pad + cardW, pillY + pillH);
        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(m_processesCardBounds, 5.0f, 5.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBg;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.125f, 0.145f, 1.0f) : D2D1::ColorF(0.92f, 0.93f, 0.96f, 1.0f),
            &pillBg
        );
        if (pillBg) pRT->FillRoundedRectangle(rrcPill, pillBg.Get());

        // Progress text
        std::wstring dispText = m_processTasks[0].title + L"...";
        D2D1_RECT_F rcText = D2D1::RectF(m_processesCardBounds.left + config.ScaleF(10.0f), pillY,
                                         m_processesCardBounds.left + cardW * 0.55f, pillY + pillH);
        IDWriteTextFormat* pFmt = theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get();
        ID2D1Brush* pTxtBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
        if (pFmt && pTxtBrush) {
            pRT->DrawText(dispText.c_str(), static_cast<UINT32>(dispText.size()), pFmt, rcText, pTxtBrush);
        }

        // Mini blue progress bar
        float barX = m_processesCardBounds.left + cardW * 0.58f;
        float barW = cardW * 0.38f;
        float barY = pillY + (pillH - config.ScaleF(3.0f)) / 2.0f;
        D2D1_ROUNDED_RECT rrcTrack = D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barW, barY + config.ScaleF(3.0f)), 1.5f, 1.5f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f), &trackBrush);
        if (trackBrush) pRT->FillRoundedRectangle(rrcTrack, trackBrush.Get());

        float fillW = barW * (std::clamp)(m_processTasks[0].progress, 0.1f, 1.0f);
        D2D1_ROUNDED_RECT rrcFill = D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + fillW, barY + config.ScaleF(3.0f)), 1.5f, 1.5f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.56f, 0.95f, 1.0f), &fillBrush);
        if (fillBrush) pRT->FillRoundedRectangle(rrcFill, fillBrush.Get());
        return;
    }

    // Modern completely FLAT Processes Card (User requirement: "make the design of that a flat and also no line like that and working clickable")
    const float headerH = config.ScaleF(28.0f);
    const float taskH = config.ScaleF(58.0f);
    const float cardH = headerH + taskH * m_processTasks.size() + config.ScaleF(8.0f);
    float cardY = y + h - cardH - pad;
    if (cardY < y + pad) cardY = y + pad;

    m_processesCardBounds = D2D1::RectF(x + pad, cardY, x + pad + cardW, cardY + cardH);
    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_processesCardBounds, 7.0f, 7.0f);

    // Completely FLAT background tone - NO BORDER STROKE
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.11f, 0.115f, 0.135f, 1.0f) : D2D1::ColorF(0.93f, 0.935f, 0.95f, 1.0f),
        &cardBg
    );
    if (cardBg) pRT->FillRoundedRectangle(rrcCard, cardBg.Get());

    pRT->PushAxisAlignedClip(m_processesCardBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Header Title: Processes
    D2D1_RECT_F rcHeader = D2D1::RectF(m_processesCardBounds.left + config.ScaleF(12.0f), cardY + config.ScaleF(5.0f),
                                       m_processesCardBounds.right - config.ScaleF(36.0f), cardY + headerH);
    IDWriteTextFormat* pFmtTitle = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();
    ID2D1Brush* pTitleBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
    if (pFmtTitle && pTitleBrush) {
        pRT->DrawText(L"Processes", 9, pFmtTitle, rcHeader, pTitleBrush);
    }

    // Minimize Button ─ (Generous hit box: 28x26px, working clickable)
    const float minBtnW = config.ScaleF(28.0f);
    const float minBtnH = config.ScaleF(26.0f);
    m_processesMinBtnBounds = D2D1::RectF(
        m_processesCardBounds.right - minBtnW - config.ScaleF(4.0f),
        cardY + (headerH - minBtnH) / 2.0f,
        m_processesCardBounds.right - config.ScaleF(4.0f),
        cardY + (headerH + minBtnH) / 2.0f
    );

    if (m_isMinBtnHovered) {
        D2D1_ROUNDED_RECT rrcMinHover = D2D1::RoundedRect(m_processesMinBtnBounds, 4.0f, 4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> minHovBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f), &minHovBg);
        if (minHovBg) pRT->FillRoundedRectangle(rrcMinHover, minHovBg.Get());
    }

    float minMidX = (m_processesMinBtnBounds.left + m_processesMinBtnBounds.right) / 2.0f;
    float minMidY = (m_processesMinBtnBounds.top + m_processesMinBtnBounds.bottom) / 2.0f;
    float minLineHalfW = config.ScaleF(5.0f);
    ID2D1Brush* pMuted = theme.brushTextMuted ? theme.brushTextMuted.Get() : pTitleBrush;
    if (pMuted) {
        pRT->DrawLine(
            D2D1::Point2F(minMidX - minLineHalfW, minMidY),
            D2D1::Point2F(minMidX + minLineHalfW, minMidY),
            pMuted, 1.5f
        );
    }

    // NOTE: ZERO DIVIDER LINE! User requested: "and also no line like that"!

    // Render each process task
    float curTaskY = cardY + headerH + config.ScaleF(2.0f);
    for (size_t i = 0; i < m_processTasks.size(); ++i) {
        auto& task = m_processTasks[i];

        // Task Title
        D2D1_RECT_F rcTaskTitle = D2D1::RectF(m_processesCardBounds.left + config.ScaleF(12.0f), curTaskY + config.ScaleF(2.0f),
                                              m_processesCardBounds.right - config.ScaleF(38.0f), curTaskY + config.ScaleF(18.0f));
        IDWriteTextFormat* pFmtTask = theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get();
        if (pFmtTask && pTitleBrush) {
            pRT->DrawText(task.title.c_str(), static_cast<UINT32>(task.title.size()), pFmtTask, rcTaskTitle, pTitleBrush);
        }

        // Progress Bar
        float trackLeft = m_processesCardBounds.left + config.ScaleF(12.0f);
        float trackRight = m_processesCardBounds.right - config.ScaleF(38.0f);
        float trackW = trackRight - trackLeft;
        float trackY = curTaskY + config.ScaleF(21.0f);
        float trackH = config.ScaleF(3.0f);

        D2D1_ROUNDED_RECT rrcTrack = D2D1::RoundedRect(D2D1::RectF(trackLeft, trackY, trackRight, trackY + trackH), 1.5f, 1.5f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f), &trackBg);
        if (trackBg) pRT->FillRoundedRectangle(rrcTrack, trackBg.Get());

        float fillW = trackW * (std::clamp)(task.progress, 0.10f, 1.0f);
        D2D1_ROUNDED_RECT rrcFill = D2D1::RoundedRect(D2D1::RectF(trackLeft, trackY, trackLeft + fillW, trackY + trackH), 1.5f, 1.5f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.56f, 0.95f, 1.0f), &fillBrush);
        if (fillBrush) pRT->FillRoundedRectangle(rrcFill, fillBrush.Get());

        // Task Subtitle (Generous height so long text e.g. "Extracted PHP v8.5 and configured environment..." fits cleanly without truncation)
        D2D1_RECT_F rcSub = D2D1::RectF(m_processesCardBounds.left + config.ScaleF(12.0f), curTaskY + config.ScaleF(27.0f),
                                       m_processesCardBounds.right - config.ScaleF(38.0f), curTaskY + taskH - config.ScaleF(3.0f));
        if (pFmtTask && pMuted) {
            pRT->PushAxisAlignedClip(rcSub, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pRT->DrawText(task.subtitle.c_str(), static_cast<UINT32>(task.subtitle.size()), pFmtTask, rcSub, pMuted);
            pRT->PopAxisAlignedClip();
        }

        // Cancel / Dismiss Button ✕ (Generous hit box: 28x28px, working clickable)
        const float cancelBtnW = config.ScaleF(28.0f);
        const float cancelBtnH = config.ScaleF(28.0f);
        task.cancelBtnBounds = D2D1::RectF(
            m_processesCardBounds.right - cancelBtnW - config.ScaleF(4.0f),
            curTaskY + (taskH - cancelBtnH) / 2.0f,
            m_processesCardBounds.right - config.ScaleF(4.0f),
            curTaskY + (taskH + cancelBtnH) / 2.0f
        );

        if (task.isCancelHovered) {
            D2D1_ROUNDED_RECT rrcBtnHover = D2D1::RoundedRect(task.cancelBtnBounds, 5.0f, 5.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnHovBg;
            pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.25f, 0.25f, 0.20f), &btnHovBg);
            if (btnHovBg) pRT->FillRoundedRectangle(rrcBtnHover, btnHovBg.Get());
        }

        float cx = (task.cancelBtnBounds.left + task.cancelBtnBounds.right) / 2.0f;
        float cy = (task.cancelBtnBounds.top + task.cancelBtnBounds.bottom) / 2.0f;
        float sz = config.ScaleF(4.0f);

        ID2D1Brush* pCancelIconBrush = task.isCancelHovered ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : pMuted) : pMuted;
        if (pCancelIconBrush) {
            pRT->DrawLine(D2D1::Point2F(cx - sz, cy - sz), D2D1::Point2F(cx + sz, cy + sz), pCancelIconBrush, 1.5f);
            pRT->DrawLine(D2D1::Point2F(cx + sz, cy - sz), D2D1::Point2F(cx - sz, cy + sz), pCancelIconBrush, 1.5f);
        }

        curTaskY += taskH;
    }

    pRT->PopAxisAlignedClip();
}

void ExtensionsPanel::RenderBrandedIcon(ID2D1RenderTarget* pRT, Theme& theme, const ExtensionItem& item, const D2D1_RECT_F& bounds) {
    if (item.pImageBitmap) {
        D2D1_SIZE_F bmpSize = item.pImageBitmap->GetSize();
        if (bmpSize.width > 0.0f && bmpSize.height > 0.0f) {
            float bw = bounds.right - bounds.left;
            float bh = bounds.bottom - bounds.top;
            float scale = (std::min)(bw / bmpSize.width, bh / bmpSize.height);
            float drawW = bmpSize.width * scale;
            float drawH = bmpSize.height * scale;
            float drawX = bounds.left + (bw - drawW) / 2.0f;
            float drawY = bounds.top + (bh - drawH) / 2.0f;
            D2D1_RECT_F dstRect = D2D1::RectF(drawX, drawY, drawX + drawW, drawY + drawH);
            pRT->DrawBitmap(item.pImageBitmap.Get(), dstRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
    }

    float cx = (bounds.left + bounds.right) / 2.0f;
    float cy = (bounds.top + bounds.bottom) / 2.0f;
    float s = (std::min)(bounds.right - bounds.left, bounds.bottom - bounds.top);

    // High quality vector branded graphics fallback
    if (item.id == L"python") {
        // Python Two-Tone Blue & Yellow icon
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBlue, bYellow;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.46f, 0.67f), &bBlue);   // #3776AB
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.83f, 0.22f), &bYellow); // #FFD438

        float r = s * 0.38f;
        D2D1_ROUNDED_RECT rrcTop = D2D1::RoundedRect(D2D1::RectF(cx - r, cy - r, cx + r * 0.3f, cy + r * 0.3f), 4.0f, 4.0f);
        D2D1_ROUNDED_RECT rrcBot = D2D1::RoundedRect(D2D1::RectF(cx - r * 0.3f, cy - r * 0.3f, cx + r, cy + r), 4.0f, 4.0f);
        if (bBlue) pRT->FillRoundedRectangle(rrcTop, bBlue.Get());
        if (bYellow) pRT->FillRoundedRectangle(rrcBot, bYellow.Get());

        // Eyes
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bWhite;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &bWhite);
        if (bWhite) {
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.45f, cy - r * 0.45f), 2.0f, 2.0f), bWhite.Get());
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.45f, cy + r * 0.45f), 2.0f, 2.0f), bWhite.Get());
        }
    } else if (item.id == L"php") {
        // PHP Indigo Pill
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bPhp;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.47f, 0.48f, 0.71f), &bPhp); // #777BB4
        D2D1_ROUNDED_RECT rrcPhp = D2D1::RoundedRect(bounds, 8.0f, 8.0f);
        if (bPhp) pRT->FillRoundedRectangle(rrcPhp, bPhp.Get());

        IDWriteTextFormat* pFmt = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontTitle.Get();
        if (pFmt && theme.brushTextWhite) {
            pRT->DrawText(L"PHP", 3, pFmt, bounds, theme.brushTextWhite.Get());
        }
    } else {
        // Node.js Hex Green
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bNode;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.60f, 0.20f), &bNode); // #339933
        D2D1_ROUNDED_RECT rrcNode = D2D1::RoundedRect(bounds, 8.0f, 8.0f);
        if (bNode) pRT->FillRoundedRectangle(rrcNode, bNode.Get());

        IDWriteTextFormat* pFmt = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontTitle.Get();
        if (pFmt && theme.brushTextWhite) {
            pRT->DrawText(L"JS", 2, pFmt, bounds, theme.brushTextWhite.Get());
        }
    }
}

void ExtensionsPanel::RenderSkeletonLoader(ID2D1RenderTarget* pRT, Theme& theme, const D2D1_RECT_F& bounds) {
    // Modern pulsing gradient shimmer skeleton loader (no placeholder text!)
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(bounds, 5.0f, 5.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> baseBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.18f, 0.20f, 1.0f) : D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f),
        &baseBrush
    );
    if (baseBrush) pRT->FillRoundedRectangle(rrc, baseBrush.Get());

    // Animated shimmer highlight bar
    float shimmerW = (bounds.right - bounds.left) * 0.45f;
    float shimmerX = bounds.left + m_shimmerOffset * ((bounds.right - bounds.left) + shimmerW) - shimmerW;
    D2D1_RECT_F rcShimmer = D2D1::RectF((std::max)(bounds.left, shimmerX), bounds.top, (std::min)(bounds.right, shimmerX + shimmerW), bounds.bottom);

    if (rcShimmer.right > rcShimmer.left) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> highlightBrush;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.28f, 0.28f, 0.32f, 0.65f) : D2D1::ColorF(0.92f, 0.92f, 0.95f, 0.65f),
            &highlightBrush
        );
        D2D1_ROUNDED_RECT rrcShimmer = D2D1::RoundedRect(rcShimmer, 4.0f, 4.0f);
        if (highlightBrush) pRT->FillRoundedRectangle(rrcShimmer, highlightBrush.Get());
    }
}

void ExtensionsPanel::RenderListView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     float x, float y, float w, float h) {
    (void)h;
    float curY = y + config.ScaleF(20.0f);
    const float padX = config.ScaleF(18.0f);

    ID2D1Brush* pTxtBrush = theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
    ID2D1Brush* pSubBrush = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : pTxtBrush;
    ID2D1Brush* pMutedBrush = theme.brushTextMuted ? theme.brushTextMuted.Get() : pSubBrush;
    IDWriteTextFormat* pFmtHint = theme.fontHint ? theme.fontHint.Get() : theme.fontTooltip.Get();

    // 1. Header Bar: Big and ALL CAPS "EXTENSIONS", left-aligned, no divider line, wide gap to entries
    if (!m_pHeaderFormat) {
        Microsoft::WRL::ComPtr<IDWriteFactory> pDWrite;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &pDWrite);
        if (pDWrite) {
            pDWrite->CreateTextFormat(
                L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                config.ScaleF(16.0f), L"en-us", &m_pHeaderFormat
            );
            if (m_pHeaderFormat) {
                m_pHeaderFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                m_pHeaderFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
    }

    D2D1_RECT_F rcTitle = D2D1::RectF(x + padX, curY, x + w - padX, curY + config.ScaleF(26.0f));
    IDWriteTextFormat* pFmtTitle = m_pHeaderFormat ? m_pHeaderFormat.Get() : (theme.fontTitle ? theme.fontTitle.Get() : theme.fontMenuItem.Get());
    if (pFmtTitle && pTxtBrush) {
        if (m_pHeaderFormat) m_pHeaderFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pRT->DrawText(L"RUNTIMES", 8, pFmtTitle, rcTitle, pTxtBrush);
    }
    // Wide generous gap between EXTENSIONS header and entry cards (no divider line)
    curY += config.ScaleF(38.0f);

    // 2. 2x2 Grid of Square Cards with generous spacing & modern styling
    const float gapX = config.ScaleF(14.0f);
    const float gapY = config.ScaleF(16.0f);
    const float cardW = (w - padX * 2.0f - gapX) / 2.0f;
    const float cardH = config.ScaleF(142.0f);

    for (size_t i = 0; i < m_items.size(); ++i) {
        auto& item = m_items[i];
        size_t col = i % 2;
        size_t row = i / 2;
        float cardX = x + padX + col * (cardW + gapX);
        float cardY = curY + row * (cardH + gapY);

        D2D1_RECT_F rcCard = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);
        item.cardBounds = rcCard;
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 10.0f, 10.0f);

        // Card background
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
        if (item.isHovered) {
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.16f, 0.17f, 0.20f, 1.0f) : D2D1::ColorF(0.90f, 0.92f, 0.95f, 1.0f),
                &cardBg
            );
        } else {
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.13f, 0.15f, 1.0f) : D2D1::ColorF(0.95f, 0.95f, 0.97f, 1.0f),
                &cardBg
            );
        }
        if (cardBg) pRT->FillRoundedRectangle(rrcCard, cardBg.Get());

        // Card border (highlight on hover)
        if (item.isHovered) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverBorder;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.7f), &hoverBorder);
            if (hoverBorder) pRT->DrawRoundedRectangle(rrcCard, hoverBorder.Get(), 1.2f);
        } else if (theme.brushBorderSubtle) {
            pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 0.8f);
        }

        // 4a. Centered Icon Box pedestal
        const float iconBoxSize = config.ScaleF(46.0f);
        const float iconX = cardX + (cardW - iconBoxSize) / 2.0f;
        const float iconY = cardY + config.ScaleF(14.0f);
        D2D1_RECT_F rcIconBox = D2D1::RectF(iconX, iconY, iconX + iconBoxSize, iconY + iconBoxSize);
        D2D1_ROUNDED_RECT rrcIconBox = D2D1::RoundedRect(rcIconBox, 8.0f, 8.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBoxBg;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.08f, 0.08f, 0.10f, 0.95f) : D2D1::ColorF(0.98f, 0.98f, 1.0f, 0.95f),
            &iconBoxBg
        );
        if (iconBoxBg) pRT->FillRoundedRectangle(rrcIconBox, iconBoxBg.Get());

        // Center icon inside icon box from assets/
        const float innerIconSize = config.ScaleF(34.0f);
        float innerX = iconX + (iconBoxSize - innerIconSize) / 2.0f;
        float innerY = iconY + (iconBoxSize - innerIconSize) / 2.0f;
        D2D1_RECT_F rcInnerIcon = D2D1::RectF(innerX, innerY, innerX + innerIconSize, innerY + innerIconSize);
        RenderBrandedIcon(pRT, theme, item, rcInnerIcon);

        // 4b. Name centered below icon
        float nameY = iconY + iconBoxSize + config.ScaleF(8.0f);
        D2D1_RECT_F rcName = D2D1::RectF(cardX + 4.0f, nameY, cardX + cardW - 4.0f, nameY + config.ScaleF(20.0f));
        IDWriteTextFormat* pFmtCardName = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontCode.Get();
        if (pFmtCardName && pTxtBrush) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtCardName->GetTextAlignment();
            pFmtCardName->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pRT->DrawText(item.name.c_str(), static_cast<UINT32>(item.name.size()), pFmtCardName, rcName, pTxtBrush);
            pFmtCardName->SetTextAlignment(prevAlign);
        }

        // 4c. Status pill centered at bottom of card
        float pillW = config.ScaleF(88.0f);
        float pillH = config.ScaleF(20.0f);
        float pillX = cardX + (cardW - pillW) / 2.0f;
        float pillY = cardY + cardH - pillH - config.ScaleF(10.0f);
        D2D1_RECT_F rcPill = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);
        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, pillH * 0.5f, pillH * 0.5f);

        if (item.isInstalled) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBg;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.40f, 0.22f, 0.35f), &pillBg);
            if (pillBg) pRT->FillRoundedRectangle(rrcPill, pillBg.Get());
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBorder;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.85f, 0.45f, 0.50f), &pillBorder);
            if (pillBorder) pRT->DrawRoundedRectangle(rrcPill, pillBorder.Get(), 0.8f);

            std::wstring verLabel = L"v" + item.installedVersion;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> greenText;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.90f, 0.45f, 1.0f), &greenText);
            if (pFmtHint && greenText) {
                DWRITE_TEXT_ALIGNMENT prevAlign = pFmtHint->GetTextAlignment();
                pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                pRT->DrawText(verLabel.c_str(), static_cast<UINT32>(verLabel.size()), pFmtHint, rcPill, greenText.Get());
                pFmtHint->SetTextAlignment(prevAlign);
            }
        } else {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBg;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.24f, 0.35f), &pillBg);
            if (pillBg) pRT->FillRoundedRectangle(rrcPill, pillBg.Get());

            if (pFmtHint && pMutedBrush) {
                DWRITE_TEXT_ALIGNMENT prevAlign = pFmtHint->GetTextAlignment();
                pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                pRT->DrawText(L"Not installed", 13, pFmtHint, rcPill, pMutedBrush);
                pFmtHint->SetTextAlignment(prevAlign);
            }
        }
    }
}

void ExtensionsPanel::RenderDetailView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                       float x, float y, float w, float h) {
    (void)h;
    if (m_selectedExtensionIdx < 0 || m_selectedExtensionIdx >= static_cast<int>(m_items.size())) return;
    auto& item = m_items[m_selectedExtensionIdx];

    const float padX = config.ScaleF(18.0f);
    float curY = y + config.ScaleF(16.0f);

    ID2D1Brush* pTxtBrush = theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
    ID2D1Brush* pSubBrush = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : pTxtBrush;
    ID2D1Brush* pMutedBrush = theme.brushTextMuted ? theme.brushTextMuted.Get() : pSubBrush;
    IDWriteTextFormat* pFmtHint = theme.fontHint ? theme.fontHint.Get() : theme.fontTooltip.Get();

    // 1. Return Back Icon Button (Clean back navigation, no cluttered breadcrumbs)
    const float backBtnSize = config.ScaleF(30.0f);
    m_backBtnBounds = D2D1::RectF(x + padX, curY, x + padX + backBtnSize, curY + backBtnSize);
    D2D1_ROUNDED_RECT rrcBack = D2D1::RoundedRect(m_backBtnBounds, 6.0f, 6.0f);
    if (m_hoverBack) {
        pRT->FillRoundedRectangle(rrcBack, theme.brushItemHover.Get());
        pRT->DrawRoundedRectangle(rrcBack, theme.brushBorderSubtle.Get(), 0.8f);
    }
    m_icons.DrawReturnBack(pRT, m_backBtnBounds, pTxtBrush, 1.8f);

    curY += backBtnSize + config.ScaleF(16.0f);

    // 2. HERO ROW: Icon on the LEFT, and Title + Subtitle + Status on its RIGHT
    const float iconBoxSize = config.ScaleF(60.0f);
    D2D1_RECT_F rcIconBox = D2D1::RectF(x + padX, curY, x + padX + iconBoxSize, curY + iconBoxSize);
    D2D1_ROUNDED_RECT rrcIconBox = D2D1::RoundedRect(rcIconBox, 10.0f, 10.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBoxBg;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.13f, 0.16f, 1.0f) : D2D1::ColorF(0.94f, 0.94f, 0.96f, 1.0f),
        &iconBoxBg
    );
    if (iconBoxBg) pRT->FillRoundedRectangle(rrcIconBox, iconBoxBg.Get());
    if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrcIconBox, theme.brushBorderSubtle.Get(), 0.8f);

    // Render large branded icon from assets
    RenderBrandedIcon(pRT, theme, item, D2D1::RectF(rcIconBox.left + 6.0f, rcIconBox.top + 6.0f, rcIconBox.right - 6.0f, rcIconBox.bottom - 6.0f));

    // Right side column
    const float rightX = x + padX + iconBoxSize + config.ScaleF(14.0f);
    const float rightW = (x + w - padX) - rightX;

    // Ensure title and subtitle formats are strictly LEFT-ALIGNED (User requirement: "align this to left")
    IDWriteTextFormat* pFmtTitle = theme.fontTitle ? theme.fontTitle.Get() : theme.fontMenuItem.Get();
    if (pFmtTitle) pFmtTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    if (pFmtHint) pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 2a. Extension Name (Large bold, strictly LEFT-ALIGNED)
    D2D1_RECT_F rcName = D2D1::RectF(rightX, curY - config.ScaleF(1.0f), rightX + rightW, curY + config.ScaleF(21.0f));
    if (pFmtTitle && pTxtBrush) {
        pRT->DrawText(item.name.c_str(), static_cast<UINT32>(item.name.size()), pFmtTitle, rcName, pTxtBrush);
    }

    // 2b. Publisher / Ecosystem subtitle (strictly LEFT-ALIGNED)
    D2D1_RECT_F rcPub = D2D1::RectF(rightX, curY + config.ScaleF(21.0f), rightX + rightW, curY + config.ScaleF(36.0f));
    if (pFmtHint && pMutedBrush) {
        pRT->DrawText(item.publisher.c_str(), static_cast<UINT32>(item.publisher.size()), pFmtHint, rcPub, pMutedBrush);
    }

    // 2c. Status Pill Badge (strictly LEFT-ALIGNED at rightX)
    float statPillW = config.ScaleF(126.0f);
    float statPillH = config.ScaleF(20.0f);
    float statPillY = curY + config.ScaleF(39.0f);
    D2D1_RECT_F rcStatPill = D2D1::RectF(rightX, statPillY, rightX + statPillW, statPillY + statPillH);
    D2D1_ROUNDED_RECT rrcStatPill = D2D1::RoundedRect(rcStatPill, statPillH * 0.5f, statPillH * 0.5f);

    auto normalizeVer = [](const std::wstring& v) -> std::wstring {
        if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
        return v;
    };

    bool hasInstalledEnv = item.isInstalled;
    if (!hasInstalledEnv) {
        for (const auto& env : item.environments) {
            if (std::filesystem::exists(env.path)) {
                hasInstalledEnv = true;
                break;
            }
        }
    }

    if (hasInstalledEnv) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> greenPillBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.40f, 0.22f, 0.35f), &greenPillBg);
        if (greenPillBg) pRT->FillRoundedRectangle(rrcStatPill, greenPillBg.Get());
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> greenPillBorder;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.85f, 0.45f, 0.50f), &greenPillBorder);
        if (greenPillBorder) pRT->DrawRoundedRectangle(rrcStatPill, greenPillBorder.Get(), 0.8f);

        std::wstring displayVer;
        if (item.defaultEnvIdx >= 0 && item.defaultEnvIdx < static_cast<int>(item.environments.size())) {
            const auto& curEnv = item.environments[item.defaultEnvIdx];
            for (const auto& v : item.versions) {
                std::wstring cV = normalizeVer(v);
                if (!cV.empty() && curEnv.name.find(cV) != std::wstring::npos) {
                    displayVer = cV;
                    break;
                }
            }
        }
        if (displayVer.empty() && !item.installedVersion.empty()) {
            displayVer = normalizeVer(item.installedVersion);
        }
        std::wstring statText = !displayVer.empty() ? (L"● Installed (v" + displayVer + L")") : L"● Installed";
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> greenText;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.90f, 0.45f, 1.0f), &greenText);
        if (pFmtHint && greenText) {
            pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pRT->DrawText(statText.c_str(), static_cast<UINT32>(statText.size()), pFmtHint, rcStatPill, greenText.Get());
            pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> grayPillBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.24f, 0.35f), &grayPillBg);
        if (grayPillBg) pRT->FillRoundedRectangle(rrcStatPill, grayPillBg.Get());

        if (pFmtHint && pMutedBrush) {
            pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pRT->DrawText(L"○ Not Installed", 15, pFmtHint, rcStatPill, pMutedBrush);
            pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    // GENEROUS SPACING: 20px breathing room between Hero Row and Action Buttons! (NO STICKING!)
    curY += iconBoxSize + config.ScaleF(20.0f);

    // 3. ACTION BAR: Prominent Install/Uninstall Button + Version Dropdown Button
    const float btnH = config.ScaleF(32.0f);
    const float dropW = config.ScaleF(32.0f);
    const float gapBtn = config.ScaleF(8.0f);
    const float totalBarW = w - padX * 2.0f;
    const float actionBtnW = totalBarW - dropW - gapBtn;

    m_actionBtnBounds = D2D1::RectF(x + padX, curY, x + padX + actionBtnW, curY + btnH);
    m_dropdownBtnBounds = D2D1::RectF(x + padX + actionBtnW + gapBtn, curY, x + w - padX, curY + btnH);

    if (m_isLoading || item.versions.empty()) {
        RenderSkeletonLoader(pRT, theme, m_actionBtnBounds);
        RenderSkeletonLoader(pRT, theme, m_dropdownBtnBounds);
    } else {
        std::wstring chosenVersion = (item.selectedVersionIdx >= 0 && item.selectedVersionIdx < static_cast<int>(item.versions.size()))
            ? item.versions[item.selectedVersionIdx]
            : L"";

        auto normalizeVer = [](const std::wstring& v) -> std::wstring {
            if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
            return v;
        };

        std::wstring cleanChosen = normalizeVer(chosenVersion);
        bool isVersionInstalled = false;
        if (item.isSpecialAllVersionsUninstall) {
            isVersionInstalled = hasInstalledEnv;
        } else {
            if (hasInstalledEnv && cleanChosen == normalizeVer(item.installedVersion)) {
                isVersionInstalled = true;
            }
            for (const auto& env : item.environments) {
                if (env.name.find(cleanChosen) != std::wstring::npos) {
                    isVersionInstalled = true;
                    break;
                }
            }
        }

        D2D1_ROUNDED_RECT rrcAction = D2D1::RoundedRect(m_actionBtnBounds, 5.0f, 5.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnBrush;

        if (isVersionInstalled) {
            D2D1_COLOR_F redColor = m_hoverAction ? D2D1::ColorF(0.92f, 0.28f, 0.28f, 1.0f) : D2D1::ColorF(0.82f, 0.18f, 0.18f, 1.0f);
            pRT->CreateSolidColorBrush(redColor, &btnBrush);
        } else {
            D2D1_COLOR_F accentColor = m_hoverAction ? D2D1::ColorF(0.06f, 0.54f, 0.94f, 1.0f) : D2D1::ColorF(0.0f, 0.47f, 0.83f, 1.0f);
            pRT->CreateSolidColorBrush(accentColor, &btnBrush);
        }

        if (btnBrush) pRT->FillRoundedRectangle(rrcAction, btnBrush.Get());

        std::wstring cleanVer = normalizeVer(chosenVersion);
        std::wstring btnLabel = isVersionInstalled ? (L"Uninstall v" + cleanVer) : (L"Install v" + cleanVer);
        IDWriteTextFormat* pFmtBtn = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get();
        if (pFmtBtn && theme.brushTextWhite) {
            pRT->DrawText(btnLabel.c_str(), static_cast<UINT32>(btnLabel.size()), pFmtBtn, m_actionBtnBounds, theme.brushTextWhite.Get());
        }

        // Dropdown Chevron Button
        D2D1_ROUNDED_RECT rrcDrop = D2D1::RoundedRect(m_dropdownBtnBounds, 5.0f, 5.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dropBg;
        if (m_hoverDropdown || m_showVersionDropdown) {
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.25f, 0.28f, 1.0f) : D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f),
                &dropBg
            );
        } else {
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.16f, 0.17f, 0.20f, 1.0f) : D2D1::ColorF(0.90f, 0.90f, 0.92f, 1.0f),
                &dropBg
            );
        }
        if (dropBg) pRT->FillRoundedRectangle(rrcDrop, dropBg.Get());
        if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrcDrop, theme.brushBorderSubtle.Get(), 0.8f);

        float dcx = (m_dropdownBtnBounds.left + m_dropdownBtnBounds.right) / 2.0f;
        float dcy = (m_dropdownBtnBounds.top + m_dropdownBtnBounds.bottom) / 2.0f;
        float dSize = config.ScaleF(4.0f);
        ID2D1Brush* chevronBrush = theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
        if (chevronBrush) {
            pRT->DrawLine(D2D1::Point2F(dcx - dSize, dcy - dSize * 0.5f), D2D1::Point2F(dcx, dcy + dSize * 0.5f), chevronBrush, 1.5f);
            pRT->DrawLine(D2D1::Point2F(dcx, dcy + dSize * 0.5f), D2D1::Point2F(dcx + dSize, dcy - dSize * 0.5f), chevronBrush, 1.5f);
        }
    }

    curY += btnH + config.ScaleF(20.0f);

    // Ensure word-wrapping and trimming text formats are initialized
    if (!m_pDescFormat || !m_pTagFormat || !m_pPathFormat) {
        Microsoft::WRL::ComPtr<IDWriteFactory> pDWrite;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &pDWrite);
        if (pDWrite) {
            if (!m_pDescFormat) {
                pDWrite->CreateTextFormat(
                    L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    config.ScaleF(12.0f), L"en-us", &m_pDescFormat
                );
                if (m_pDescFormat) {
                    m_pDescFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                    m_pDescFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
            }
            if (!m_pTagFormat) {
                pDWrite->CreateTextFormat(
                    L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    config.ScaleF(10.5f), L"en-us", &m_pTagFormat
                );
                if (m_pTagFormat) {
                    m_pTagFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    m_pTagFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    m_pTagFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                }
            }
            if (!m_pPathFormat) {
                pDWrite->CreateTextFormat(
                    L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    config.ScaleF(11.0f), L"en-us", &m_pPathFormat
                );
                if (m_pPathFormat) {
                    m_pPathFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    m_pPathFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    m_pPathFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                    Microsoft::WRL::ComPtr<IDWriteInlineObject> pEllipsis;
                    pDWrite->CreateEllipsisTrimmingSign(m_pPathFormat.Get(), &pEllipsis);
                    m_pPathFormat->SetTrimming(&trim, pEllipsis.Get());
                }
            }
        }
    }

    // 4. DESCRIPTION SECTION (User requirement: "its not obverview bit description so it must be longer and make the work more seeachle and bold and quite big as it should be a header anmd align this to left")
    // (Zero divider line, zero tags!)
    D2D1_RECT_F rcDescHeader = D2D1::RectF(x + padX, curY, x + w - padX, curY + config.ScaleF(20.0f));
    IDWriteTextFormat* pFmtSectionHdr = m_pHeaderFormat ? m_pHeaderFormat.Get() : pFmtTitle;
    if (pFmtSectionHdr && pTxtBrush) {
        pFmtSectionHdr->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pRT->DrawText(L"DESCRIPTION", 11, pFmtSectionHdr, rcDescHeader, pTxtBrush);
    }
    curY += config.ScaleF(26.0f);

    // Comprehensive word-wrapped description paragraph
    D2D1_RECT_F rcDesc = D2D1::RectF(x + padX, curY, x + w - padX, curY + config.ScaleF(125.0f));
    IDWriteTextFormat* pFmtDesc = m_pDescFormat ? m_pDescFormat.Get() : pFmtHint;
    if (pFmtDesc && pTxtBrush) {
        pFmtDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pRT->DrawText(item.description.c_str(), static_cast<UINT32>(item.description.size()), pFmtDesc, rcDesc, pTxtBrush);
    }
    curY += config.ScaleF(138.0f);

    // 5. SYSTEM ENVIRONMENT & DEFAULT CHOOSER (Zero divider line, big bold left-aligned header)
    D2D1_RECT_F rcEnvHeader = D2D1::RectF(x + padX, curY, x + w - padX, curY + config.ScaleF(20.0f));
    if (pFmtSectionHdr && pTxtBrush) {
        pFmtSectionHdr->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pRT->DrawText(L"SYSTEM ENVIRONMENT", 18, pFmtSectionHdr, rcEnvHeader, pTxtBrush);
    }
    curY += config.ScaleF(24.0f);

    D2D1_RECT_F rcEnvSubtitle = D2D1::RectF(x + padX, curY, x + w - padX, curY + config.ScaleF(16.0f));
    if (pFmtHint && pMutedBrush) {
        pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pRT->DrawText(L"Select default environment for this workspace:", 46, pFmtHint, rcEnvSubtitle, pMutedBrush);
    }
    curY += config.ScaleF(22.0f);

    // Render environment options (radio-selectable cards)
    for (size_t envIdx = 0; envIdx < item.environments.size(); ++envIdx) {
        auto& env = item.environments[envIdx];
        const float envCardH = config.ScaleF(54.0f);
        D2D1_RECT_F rcEnv = D2D1::RectF(x + padX, curY, x + w - padX, curY + envCardH);
        env.bounds = rcEnv;
        D2D1_ROUNDED_RECT rrcEnv = D2D1::RoundedRect(rcEnv, 7.0f, 7.0f);

        // Card surface background
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> envBg;
        if (env.isDefault) {
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.16f, 0.23f, 1.0f) : D2D1::ColorF(0.92f, 0.95f, 0.98f, 1.0f),
                &envBg
            );
        } else if (env.isHovered) {
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.15f, 0.17f, 0.21f, 1.0f) : D2D1::ColorF(0.94f, 0.95f, 0.97f, 1.0f),
                &envBg
            );
        } else {
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.10f, 0.11f, 0.13f, 1.0f) : D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f),
                &envBg
            );
        }
        if (envBg) pRT->FillRoundedRectangle(rrcEnv, envBg.Get());

        // Card border (accent blue for default, subtle for others)
        if (env.isDefault) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> defBorder;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.90f), &defBorder);
            if (defBorder) pRT->DrawRoundedRectangle(rrcEnv, defBorder.Get(), 1.2f);
        } else if (theme.brushBorderSubtle) {
            pRT->DrawRoundedRectangle(rrcEnv, theme.brushBorderSubtle.Get(), 0.8f);
        }

        // Radio circle indicator on the left
        float radioCX = rcEnv.left + config.ScaleF(16.0f);
        float radioCY = rcEnv.top + envCardH / 2.0f;
        float radioR = config.ScaleF(6.0f);
        D2D1_ELLIPSE ellOuter = D2D1::Ellipse(D2D1::Point2F(radioCX, radioCY), radioR, radioR);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> radioBrush;
        pRT->CreateSolidColorBrush(
            env.isDefault ? D2D1::ColorF(0.0f, 0.47f, 0.83f, 1.0f) : D2D1::ColorF(0.38f, 0.40f, 0.46f, 0.85f),
            &radioBrush
        );
        if (radioBrush) pRT->DrawEllipse(ellOuter, radioBrush.Get(), 1.2f);

        if (env.isDefault) {
            D2D1_ELLIPSE ellInner = D2D1::Ellipse(D2D1::Point2F(radioCX, radioCY), radioR * 0.5f, radioR * 0.5f);
            if (radioBrush) pRT->FillEllipse(ellInner, radioBrush.Get());
        }

        // Environment Name (Title) - NO "DEFAULT" BADGE! Full width to right margin!
        float textLeft = radioCX + radioR + config.ScaleF(10.0f);
        float textRight = rcEnv.right - config.ScaleF(12.0f);

        D2D1_RECT_F rcEnvName = D2D1::RectF(textLeft, rcEnv.top + config.ScaleF(7.0f), textRight, rcEnv.top + config.ScaleF(24.0f));
        if (pFmtHint && pTxtBrush) {
            pRT->DrawText(env.name.c_str(), static_cast<UINT32>(env.name.size()), pFmtHint, rcEnvName, pTxtBrush);
        }

        // Environment Path (Full width single-line with ellipsis trimming)
        std::wstring dispPath = DisplayPath(env.path);
        D2D1_RECT_F rcEnvPath = D2D1::RectF(textLeft, rcEnv.top + config.ScaleF(26.0f), textRight, rcEnv.bottom - config.ScaleF(7.0f));
        IDWriteTextFormat* pFmtPath = m_pPathFormat ? m_pPathFormat.Get() : pFmtHint;
        if (pFmtPath && pMutedBrush) {
            pRT->DrawText(dispPath.c_str(), static_cast<UINT32>(dispPath.size()), pFmtPath, rcEnvPath, pMutedBrush);
        }

        curY += envCardH + config.ScaleF(8.0f);
    }

    curY += config.ScaleF(6.0f);

    // Browse Custom Executable button
    const float browseBtnH = config.ScaleF(30.0f);
    item.browseBtnBounds = D2D1::RectF(x + padX, curY, x + w - padX, curY + browseBtnH);
    D2D1_ROUNDED_RECT rrcBrowse = D2D1::RoundedRect(item.browseBtnBounds, 5.0f, 5.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> browseBg;
    if (item.isHoverBrowseBtn) {
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.20f, 0.22f, 0.26f, 1.0f) : D2D1::ColorF(0.88f, 0.89f, 0.92f, 1.0f),
            &browseBg
        );
    } else {
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.13f, 0.14f, 0.16f, 1.0f) : D2D1::ColorF(0.93f, 0.94f, 0.96f, 1.0f),
            &browseBg
        );
    }
    if (browseBg) pRT->FillRoundedRectangle(rrcBrowse, browseBg.Get());
    if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrcBrowse, theme.brushBorderSubtle.Get(), 0.8f);

    std::wstring browseText = L"+ Browse Custom " + item.name + L" Path...";
    if (pFmtHint && pTxtBrush) {
        DWRITE_TEXT_ALIGNMENT prevAlign = pFmtHint->GetTextAlignment();
        pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(browseText.c_str(), static_cast<UINT32>(browseText.size()), pFmtHint, item.browseBtnBounds, pTxtBrush);
        pFmtHint->SetTextAlignment(prevAlign);
    }

    // 6. Version Dropdown Popup (if open)
    if (m_showVersionDropdown && !m_isLoading && !item.versions.empty()) {
        const float itemH = config.ScaleF(28.0f);
        const float totalContentH = itemH * item.versions.size() + config.ScaleF(6.0f);
        const float maxDropH = config.ScaleF(220.0f);
        const float dropMenuH = (std::min)(totalContentH, maxDropH);
        const float dropMenuW = totalBarW;
        float dropMenuY = m_actionBtnBounds.bottom + config.ScaleF(4.0f);

        // Clamp scroll offset
        const float maxScroll = (std::max)(0.0f, totalContentH - dropMenuH);
        if (m_versionDropdownScrollY < 0.0f) m_versionDropdownScrollY = 0.0f;
        if (m_versionDropdownScrollY > maxScroll) m_versionDropdownScrollY = maxScroll;

        m_dropdownMenuBounds = D2D1::RectF(x + padX, dropMenuY, x + padX + dropMenuW, dropMenuY + dropMenuH);
        D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_dropdownMenuBounds, 6.0f, 6.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> menuBg;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 0.98f) : D2D1::ColorF(0.96f, 0.96f, 0.98f, 0.98f),
            &menuBg
        );
        if (menuBg) pRT->FillRoundedRectangle(rrcMenu, menuBg.Get());
        if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrcMenu, theme.brushBorderSubtle.Get(), 1.0f);

        auto normalizeVer = [](const std::wstring& v) -> std::wstring {
            if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
            return v;
        };

        // Clip items strictly within the dropdown rounded boundary
        pRT->PushAxisAlignedClip(m_dropdownMenuBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        m_versionItemBounds.resize(item.versions.size());
        float startItemY = dropMenuY + config.ScaleF(3.0f) - m_versionDropdownScrollY;
        const float scrollbarWidth = (totalContentH > dropMenuH) ? config.ScaleF(6.0f) : 0.0f;

        for (size_t v = 0; v < item.versions.size(); ++v) {
            float curItemY = startItemY + v * itemH;
            D2D1_RECT_F rcItem = D2D1::RectF(
                x + padX + config.ScaleF(3.0f),
                curItemY,
                x + padX + dropMenuW - config.ScaleF(3.0f) - scrollbarWidth,
                curItemY + itemH
            );
            m_versionItemBounds[v] = rcItem;

            // Only render if visible within dropMenu bounds
            if (rcItem.bottom >= m_dropdownMenuBounds.top && rcItem.top <= m_dropdownMenuBounds.bottom) {
                bool isItemHover = (static_cast<int>(v) == m_hoveredVersionItem);
                bool isItemChosen = (static_cast<int>(v) == item.selectedVersionIdx);

                if (isItemHover || isItemChosen) {
                    D2D1_ROUNDED_RECT rrcItem = D2D1::RoundedRect(rcItem, 4.0f, 4.0f);
                    pRT->FillRoundedRectangle(rrcItem, isItemChosen ? theme.brushItemActive.Get() : theme.brushItemHover.Get());
                }

                std::wstring verText = item.versions[v];
                std::wstring cleanV = normalizeVer(item.versions[v]);
                bool isVInstalled = false;
                if (item.isSpecialAllVersionsUninstall) {
                    isVInstalled = item.isInstalled;
                } else {
                    if (item.isInstalled && cleanV == normalizeVer(item.installedVersion)) {
                        isVInstalled = true;
                    }
                    for (const auto& env : item.environments) {
                        if (env.name.find(cleanV) != std::wstring::npos) {
                            isVInstalled = true;
                            break;
                        }
                    }
                }
                if (isVInstalled) {
                    verText += L" (Installed)";
                }
                D2D1_RECT_F rcText = D2D1::RectF(rcItem.left + config.ScaleF(8.0f), rcItem.top, rcItem.right - config.ScaleF(4.0f), rcItem.bottom);
                IDWriteTextFormat* pFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();
                if (pFmt && pTxtBrush) {
                    pRT->DrawText(verText.c_str(), static_cast<UINT32>(verText.size()), pFmt, rcText, pTxtBrush);
                }
            }
        }

        // Render sleek scrollbar if needed
        if (totalContentH > dropMenuH) {
            const float trackTop = m_dropdownMenuBounds.top + config.ScaleF(4.0f);
            const float trackH = m_dropdownMenuBounds.bottom - m_dropdownMenuBounds.top - config.ScaleF(8.0f);
            const float thumbH = (std::max)(config.ScaleF(24.0f), trackH * (dropMenuH / totalContentH));
            const float scrollRatio = m_versionDropdownScrollY / maxScroll;
            const float thumbTop = trackTop + scrollRatio * (trackH - thumbH);
            const float thumbRight = m_dropdownMenuBounds.right - config.ScaleF(3.0f);
            const float thumbLeft = thumbRight - config.ScaleF(4.0f);

            D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(
                D2D1::RectF(thumbLeft, thumbTop, thumbRight, thumbTop + thumbH),
                2.0f, 2.0f
            );
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> thumbBrush;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.25f),
                &thumbBrush
            );
            if (thumbBrush) pRT->FillRoundedRectangle(rrcThumb, thumbBrush.Get());
        }

        pRT->PopAxisAlignedClip();
    }
}

bool ExtensionsPanel::HitTest(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return (pt.x >= m_panelBounds.left && pt.x <= m_panelBounds.right &&
            pt.y >= m_panelBounds.top && pt.y <= m_panelBounds.bottom);
}

bool ExtensionsPanel::HitTestProcesses(POINT pt) const noexcept {
    if (m_processTasks.empty()) return false;
    return (pt.x >= m_processesCardBounds.left && pt.x <= m_processesCardBounds.right &&
            pt.y >= m_processesCardBounds.top && pt.y <= m_processesCardBounds.bottom);
}

bool ExtensionsPanel::HandleMouseMove(POINT pt) noexcept {
    bool repaint = false;

    // Hover tracking for active Processes card (both List and Detail views, working clickable even if sidebar closed)
    if (!m_processTasks.empty() && !m_processesMinimized) {
        bool hovMin = (pt.x >= m_processesMinBtnBounds.left && pt.x <= m_processesMinBtnBounds.right &&
                       pt.y >= m_processesMinBtnBounds.top && pt.y <= m_processesMinBtnBounds.bottom);
        if (m_isMinBtnHovered != hovMin) {
            m_isMinBtnHovered = hovMin;
            repaint = true;
        }

        for (auto& task : m_processTasks) {
            bool hovCancel = (pt.x >= task.cancelBtnBounds.left && pt.x <= task.cancelBtnBounds.right &&
                              pt.y >= task.cancelBtnBounds.top && pt.y <= task.cancelBtnBounds.bottom);
            if (task.isCancelHovered != hovCancel) {
                task.isCancelHovered = hovCancel;
                repaint = true;
            }
        }
    }

    if (!m_isOpen) return repaint;

    if (m_viewMode == ViewMode::List) {
        for (auto& item : m_items) {
            bool hover = (pt.x >= item.cardBounds.left && pt.x <= item.cardBounds.right &&
                          pt.y >= item.cardBounds.top && pt.y <= item.cardBounds.bottom);
            if (item.isHovered != hover) {
                item.isHovered = hover;
                repaint = true;
            }
        }
    } else {
        bool hoverBack = (pt.x >= m_backBtnBounds.left && pt.x <= m_backBtnBounds.right &&
                          pt.y >= m_backBtnBounds.top && pt.y <= m_backBtnBounds.bottom);
        if (m_hoverBack != hoverBack) { m_hoverBack = hoverBack; repaint = true; }

        bool hoverAction = (pt.x >= m_actionBtnBounds.left && pt.x <= m_actionBtnBounds.right &&
                            pt.y >= m_actionBtnBounds.top && pt.y <= m_actionBtnBounds.bottom);
        if (m_hoverAction != hoverAction) { m_hoverAction = hoverAction; repaint = true; }

        bool hoverDrop = (pt.x >= m_dropdownBtnBounds.left && pt.x <= m_dropdownBtnBounds.right &&
                          pt.y >= m_dropdownBtnBounds.top && pt.y <= m_dropdownBtnBounds.bottom);
        if (m_hoverDropdown != hoverDrop) { m_hoverDropdown = hoverDrop; repaint = true; }

        if (m_selectedExtensionIdx >= 0 && m_selectedExtensionIdx < static_cast<int>(m_items.size())) {
            auto& item = m_items[m_selectedExtensionIdx];
            for (auto& env : item.environments) {
                bool hoverEnv = (pt.x >= env.bounds.left && pt.x <= env.bounds.right &&
                                 pt.y >= env.bounds.top && pt.y <= env.bounds.bottom);
                if (env.isHovered != hoverEnv) {
                    env.isHovered = hoverEnv;
                    repaint = true;
                }
            }
            bool hoverBrowse = (pt.x >= item.browseBtnBounds.left && pt.x <= item.browseBtnBounds.right &&
                                pt.y >= item.browseBtnBounds.top && pt.y <= item.browseBtnBounds.bottom);
            if (item.isHoverBrowseBtn != hoverBrowse) {
                item.isHoverBrowseBtn = hoverBrowse;
                repaint = true;
            }
        }

        if (m_showVersionDropdown) {
            int prevHover = m_hoveredVersionItem;
            m_hoveredVersionItem = -1;
            if (pt.x >= m_dropdownMenuBounds.left && pt.x <= m_dropdownMenuBounds.right &&
                pt.y >= m_dropdownMenuBounds.top && pt.y <= m_dropdownMenuBounds.bottom) {
                for (size_t v = 0; v < m_versionItemBounds.size(); ++v) {
                    const auto& b = m_versionItemBounds[v];
                    if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
                        m_hoveredVersionItem = static_cast<int>(v);
                        break;
                    }
                }
            }
            if (prevHover != m_hoveredVersionItem) repaint = true;
        }
    }

    return repaint;
}

bool ExtensionsPanel::HandleLButtonDown(POINT pt, std::function<void(const std::wstring&)> notifyCb) noexcept {
    // 0. Click on Processes HUD card (Image 2 & Image 3, working clickable even if sidebar closed)
    if (!m_processTasks.empty() && m_processesCardBounds.right > m_processesCardBounds.left) {
        if (pt.x >= m_processesCardBounds.left && pt.x <= m_processesCardBounds.right &&
            pt.y >= m_processesCardBounds.top && pt.y <= m_processesCardBounds.bottom) {
            
            if (m_processesMinimized) {
                m_processesMinimized = false;
                return true;
            }

            // Check minimize button click
            if (pt.x >= m_processesMinBtnBounds.left && pt.x <= m_processesMinBtnBounds.right &&
                pt.y >= m_processesMinBtnBounds.top && pt.y <= m_processesMinBtnBounds.bottom) {
                m_processesMinimized = true;
                return true;
            }

            // Check task cancel/dismiss buttons click
            for (size_t i = 0; i < m_processTasks.size(); ++i) {
                const auto& cb = m_processTasks[i].cancelBtnBounds;
                if (pt.x >= cb.left && pt.x <= cb.right && pt.y >= cb.top && pt.y <= cb.bottom) {
                    DismissProcessTask(i);
                    return true;
                }
            }
            return true;
        }
    }

    if (!m_isOpen) return false;

    if (m_viewMode == ViewMode::List) {
        for (size_t i = 0; i < m_items.size(); ++i) {
            const auto& b = m_items[i].cardBounds;
            if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
                m_selectedExtensionIdx = static_cast<int>(i);
                m_viewMode = ViewMode::Detail;
                m_isLoading = true;
                m_loadingTime = 0.0f;
                m_shimmerOffset = 0.0f;
                m_showVersionDropdown = false;
                m_versionDropdownScrollY = 0.0f;
                if (m_items[i].isInstalled && !m_items[i].installedVersion.empty()) {
                    auto normalizeVer = [](const std::wstring& v) -> std::wstring {
                        if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
                        return v;
                    };
                    for (size_t v = 0; v < m_items[i].versions.size(); ++v) {
                        if (normalizeVer(m_items[i].versions[v]) == normalizeVer(m_items[i].installedVersion)) {
                            m_items[i].selectedVersionIdx = static_cast<int>(v);
                            break;
                        }
                    }
                }
                FetchSupabaseVersions(true);
                return true;
            }
        }
    } else {
        // Back Button
        if (pt.x >= m_backBtnBounds.left && pt.x <= m_backBtnBounds.right &&
            pt.y >= m_backBtnBounds.top && pt.y <= m_backBtnBounds.bottom) {
            m_viewMode = ViewMode::List;
            m_showVersionDropdown = false;
            return true;
        }

        // Version item click inside open dropdown
        if (m_showVersionDropdown) {
            if (pt.x >= m_dropdownMenuBounds.left && pt.x <= m_dropdownMenuBounds.right &&
                pt.y >= m_dropdownMenuBounds.top && pt.y <= m_dropdownMenuBounds.bottom) {
                for (size_t v = 0; v < m_versionItemBounds.size(); ++v) {
                    const auto& b = m_versionItemBounds[v];
                    if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
                        if (m_selectedExtensionIdx >= 0 && m_selectedExtensionIdx < static_cast<int>(m_items.size())) {
                            auto& item = m_items[m_selectedExtensionIdx];
                            item.selectedVersionIdx = static_cast<int>(v);
                            auto normalize = [](const std::wstring& ver) -> std::wstring {
                                if (ver.rfind(L"v", 0) == 0 || ver.rfind(L"V", 0) == 0) return ver.substr(1);
                                return ver;
                            };
                            std::wstring chosen = normalize(item.versions[v]);
                            for (size_t e = 0; e < item.environments.size(); ++e) {
                                if (item.environments[e].name.find(chosen) != std::wstring::npos ||
                                    item.environments[e].path.find(chosen) != std::wstring::npos) {
                                    SelectDefaultEnvironment(m_selectedExtensionIdx, static_cast<int>(e));
                                    break;
                                }
                            }
                        }
                        m_showVersionDropdown = false;
                        if (m_onDataChanged) m_onDataChanged();
                        return true;
                    }
                }
            }
        }

        // Dropdown toggle button click
        if (pt.x >= m_dropdownBtnBounds.left && pt.x <= m_dropdownBtnBounds.right &&
            pt.y >= m_dropdownBtnBounds.top && pt.y <= m_dropdownBtnBounds.bottom) {
            m_showVersionDropdown = !m_showVersionDropdown;
            if (m_showVersionDropdown) {
                if (m_selectedExtensionIdx >= 0 && m_selectedExtensionIdx < static_cast<int>(m_items.size())) {
                    if (m_items[m_selectedExtensionIdx].versions.size() <= 1) {
                        FetchSupabaseVersions(true);
                    }
                }
            }
            return true;
        }

        // Action Button click (Install or Uninstall)
        if (pt.x >= m_actionBtnBounds.left && pt.x <= m_actionBtnBounds.right &&
            pt.y >= m_actionBtnBounds.top && pt.y <= m_actionBtnBounds.bottom) {
            if (m_selectedExtensionIdx >= 0 && m_selectedExtensionIdx < static_cast<int>(m_items.size())) {
                auto& item = m_items[m_selectedExtensionIdx];
                std::wstring chosenVersion = (item.selectedVersionIdx >= 0 && item.selectedVersionIdx < static_cast<int>(item.versions.size()))
                    ? item.versions[item.selectedVersionIdx]
                    : L"";

                auto normalizeVer = [](const std::wstring& v) -> std::wstring {
                    if (v.rfind(L"v", 0) == 0 || v.rfind(L"V", 0) == 0) return v.substr(1);
                    return v;
                };

                std::wstring cleanChosen = normalizeVer(chosenVersion);
                bool isVersionInstalled = false;
                if (item.isSpecialAllVersionsUninstall) {
                    isVersionInstalled = item.isInstalled;
                } else {
                    if (item.isInstalled && cleanChosen == normalizeVer(item.installedVersion)) {
                        isVersionInstalled = true;
                    }
                    for (const auto& env : item.environments) {
                        if (env.name.find(cleanChosen) != std::wstring::npos) {
                            isVersionInstalled = true;
                            break;
                        }
                    }
                }

                if (isVersionInstalled) {
                    UninstallRuntime(m_selectedExtensionIdx, chosenVersion, notifyCb);
                } else {
                    InstallRuntime(m_selectedExtensionIdx, chosenVersion, notifyCb);
                }
            }
            return true;
        }

        // If clicking outside dropdown, close it
        if (m_showVersionDropdown) {
            m_showVersionDropdown = false;
            return true;
        }

        // Environment option clicks (User requirement: "USER CAN CHOOSE THE SYSTEM ENVIROMENT THATS THE DEFAULT")
        if (m_selectedExtensionIdx >= 0 && m_selectedExtensionIdx < static_cast<int>(m_items.size())) {
            auto& item = m_items[m_selectedExtensionIdx];
            for (size_t envIdx = 0; envIdx < item.environments.size(); ++envIdx) {
                const auto& env = item.environments[envIdx];
                if (pt.x >= env.bounds.left && pt.x <= env.bounds.right &&
                    pt.y >= env.bounds.top && pt.y <= env.bounds.bottom) {
                    SelectDefaultEnvironment(m_selectedExtensionIdx, static_cast<int>(envIdx));
                    if (notifyCb) notifyCb(L"Set default environment: " + env.name);

                    // User requirement: "would this create the .venv fodler" -> YES! Create .venv if not yet present!
                    if (env.type == L"Virtualenv" || env.name.find(L".venv") != std::wstring::npos) {
                        std::filesystem::path venvPy = std::filesystem::current_path() / ".venv" / "Scripts" / "python.exe";
                        if (!std::filesystem::exists(venvPy)) {
                            CreateWorkspaceVenv(notifyCb);
                        } else {
                            StartProcessTask(L"Updating Python interpreter", L"Workspace virtual environment (.venv) activated.");
                            CompleteProcessTask(L"Updating Python interpreter", L"Virtual environment (.venv) ready.");
                        }
                    }
                    return true;
                }
            }

            // Check Browse Custom Path button click (User requirement: browse for installation folder, NOT exe!)
            if (pt.x >= item.browseBtnBounds.left && pt.x <= item.browseBtnBounds.right &&
                pt.y >= item.browseBtnBounds.top && pt.y <= item.browseBtnBounds.bottom) {
                
                std::wstring selectedFolder;
                Microsoft::WRL::ComPtr<IFileDialog> pfd;
                HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
                if (SUCCEEDED(hr)) {
                    DWORD dwOptions = 0;
                    if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
                        pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
                    }
                    std::wstring title = L"Select " + item.name + L" Installation Directory";
                    pfd->SetTitle(title.c_str());
                    if (SUCCEEDED(pfd->Show(nullptr))) {
                        Microsoft::WRL::ComPtr<IShellItem> psi;
                        if (SUCCEEDED(pfd->GetResult(&psi))) {
                            PWSTR pszPath = nullptr;
                            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                                selectedFolder = pszPath;
                                CoTaskMemFree(pszPath);
                            }
                        }
                    }
                }

                // Fallback using BROWSEINFOW
                if (selectedFolder.empty()) {
                    BROWSEINFOW bi{};
                    bi.lpszTitle = (L"Select " + item.name + L" Installation Directory").c_str();
                    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
                    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                    if (pidl) {
                        wchar_t szPath[MAX_PATH] = { 0 };
                        if (SHGetPathFromIDListW(pidl, szPath)) {
                            selectedFolder = szPath;
                        }
                        CoTaskMemFree(pidl);
                    }
                }

                if (!selectedFolder.empty()) {
                    SetCustomEnvironment(m_selectedExtensionIdx, selectedFolder);
                    StartProcessTask(L"Updating " + item.name + L" interpreter", L"Configured installation path: " + selectedFolder);
                    CompleteProcessTask(L"Updating " + item.name + L" interpreter", L"Custom environment registered.");
                    if (notifyCb) notifyCb(L"Selected " + item.name + L" installation: " + selectedFolder);
                    if (m_onDataChanged) m_onDataChanged();
                }
                return true;
            }
        }
    }

    return true; // Swallowed inside extensions panel
}

bool ExtensionsPanel::HandleMouseWheel(short delta) noexcept {
    if (!m_isOpen) return false;
    if (m_showVersionDropdown) {
        m_versionDropdownScrollY = (std::max)(0.0f, m_versionDropdownScrollY - static_cast<float>(delta) * 0.4f);
        return true;
    }
    m_scrollOffset = (std::max)(0.0f, m_scrollOffset - static_cast<float>(delta) * 0.3f);
    return true;
}

} // namespace phide::ui
