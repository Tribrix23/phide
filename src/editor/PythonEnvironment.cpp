#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef SearchPath
#undef SearchPath
#endif

#include "phide/editor/PythonEnvironment.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace phide::editor::pyenv {

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

bool IsIdentifier(const std::wstring& s) {
    if (s.empty() || !(iswalpha(s[0]) || s[0] == L'_')) return false;
    for (wchar_t c : s) {
        if (!(iswalnum(c) || c == L'_')) return false;
    }
    return true;
}

std::wstring PathKey(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    return ToLower((ec ? p : c).wstring());
}

// ---------------------------------------------------------------------------------------------
// Interpreter discovery
// ---------------------------------------------------------------------------------------------

struct ProjectEnv {
    fs::path root;
    fs::path interpreter;
    fs::path sitePackages;
    fs::path baseHome;            ///< `home` from pyvenv.cfg (base interpreter folder)
    bool includeSystemSite = false;
};

struct BaseInterpreter {
    fs::path exe;
    std::wstring versionLabel;    ///< e.g. L"3.15"
};

std::unordered_map<std::wstring, std::wstring> ReadPyvenvCfg(const fs::path& cfg) {
    std::unordered_map<std::wstring, std::wstring> kv;
    std::ifstream in(cfg, std::ios::binary);
    if (!in.is_open()) return kv;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::wstring k = ToLower(Trim(Utf8ToWide(line.substr(0, eq))));
        std::wstring v = Trim(Utf8ToWide(line.substr(eq + 1)));
        if (!k.empty()) kv[k] = v;
    }
    return kv;
}

std::vector<ProjectEnv> FindProjectEnvs(const fs::path& projectRoot) {
    std::vector<ProjectEnv> envs;
    std::error_code ec;
    if (projectRoot.empty() || !fs::is_directory(projectRoot, ec)) return envs;
    for (fs::directory_iterator it(projectRoot, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        fs::path cfg = it->path() / L"pyvenv.cfg";
        if (!fs::exists(cfg, ec)) continue;

        ProjectEnv env;
        env.root = it->path();
        auto kv = ReadPyvenvCfg(cfg);
        if (kv.count(L"home")) env.baseHome = kv[L"home"];
        if (kv.count(L"include-system-site-packages")) {
            env.includeSystemSite = (ToLower(kv[L"include-system-site-packages"]) == L"true");
        }

        if (fs::exists(env.root / L"Scripts" / L"python.exe", ec)) env.interpreter = env.root / L"Scripts" / L"python.exe";
        else if (fs::exists(env.root / L"bin" / L"python", ec)) env.interpreter = env.root / L"bin" / L"python";

        if (fs::is_directory(env.root / L"Lib" / L"site-packages", ec)) {
            env.sitePackages = env.root / L"Lib" / L"site-packages";
        } else if (fs::is_directory(env.root / L"lib", ec)) {
            for (fs::directory_iterator lit(env.root / L"lib", ec); !ec && lit != fs::directory_iterator(); lit.increment(ec)) {
                if (lit->path().filename().wstring().rfind(L"python", 0) == 0 && fs::is_directory(lit->path() / L"site-packages", ec)) {
                    env.sitePackages = lit->path() / L"site-packages";
                    break;
                }
            }
        }
        if (!env.sitePackages.empty() || !env.interpreter.empty()) envs.push_back(std::move(env));
    }
    return envs;
}

std::wstring DetectVersionLabel(const fs::path& interpreterDir) {
    // 1. Folder name like "Python315" / "Python3.12" / "Python-3.11"
    std::wstring dir = interpreterDir.filename().wstring();
    std::wstring digits;
    for (wchar_t c : dir) {
        if (iswdigit(c)) digits.push_back(c);
    }
    std::wstring lowerDir = ToLower(dir);
    if (lowerDir.rfind(L"python", 0) == 0 && digits.size() >= 2) {
        return digits.substr(0, 1) + L"." + digits.substr(1);
    }
    // 2. python3XY.dll next to the interpreter
    std::error_code ec;
    for (fs::directory_iterator it(interpreterDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::wstring fn = ToLower(it->path().filename().wstring());
        if (fn.rfind(L"python3", 0) == 0 && fn.size() > 11 && fn.substr(fn.size() - 4) == L".dll") {
            std::wstring d = fn.substr(6, fn.size() - 10); // "3XY"
            if (d.size() >= 2 && std::all_of(d.begin(), d.end(), iswdigit)) {
                return d.substr(0, 1) + L"." + d.substr(1);
            }
        }
    }
    return L"";
}

std::vector<BaseInterpreter> FindBaseInterpreters(const std::vector<ProjectEnv>& envs) {
    std::vector<BaseInterpreter> out;
    std::unordered_set<std::wstring> seen;
    std::error_code ec;

    auto add = [&](const fs::path& exe) {
        if (exe.empty() || !fs::exists(exe, ec)) return;
        std::wstring lower = ToLower(exe.wstring());
        if (lower.find(L"windowsapps") != std::wstring::npos) return; // Microsoft Store alias stub
        // Skip interpreters that are themselves virtual environments
        if (fs::exists(exe.parent_path() / L"pyvenv.cfg", ec) || fs::exists(exe.parent_path().parent_path() / L"pyvenv.cfg", ec)) return;
        std::wstring key = PathKey(exe);
        if (!seen.insert(key).second) return;
        out.push_back({ exe, DetectVersionLabel(exe.parent_path()) });
    };

    // a) Base interpreters referenced by project virtual environments
    for (const auto& env : envs) {
        if (!env.baseHome.empty()) add(env.baseHome / L"python.exe");
    }

    // b) Every python.exe on PATH
    DWORD need = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (need > 0) {
        std::wstring pathVar(need, L'\0');
        DWORD got = GetEnvironmentVariableW(L"PATH", pathVar.data(), need);
        pathVar.resize(got);
        size_t s = 0;
        while (s <= pathVar.size()) {
            size_t e = pathVar.find(L';', s);
            if (e == std::wstring::npos) e = pathVar.size();
            std::wstring part = Trim(pathVar.substr(s, e - s));
            if (!part.empty()) add(fs::path(part) / L"python.exe");
            s = e + 1;
        }
    }

    // c) Standard per-user / machine-wide install roots
    auto scanRoot = [&](const fs::path& root) {
        if (!fs::is_directory(root, ec)) return;
        for (fs::directory_iterator it(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (it->is_directory(ec) && ToLower(it->path().filename().wstring()).rfind(L"python", 0) == 0) {
                add(it->path() / L"python.exe");
            }
        }
    };
    wchar_t buf[4096];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, 4096) > 0) scanRoot(fs::path(buf) / L"Programs" / L"Python");
    if (GetEnvironmentVariableW(L"ProgramFiles", buf, 4096) > 0) scanRoot(fs::path(buf));
    return out;
}

// ---------------------------------------------------------------------------------------------
// Caches
// ---------------------------------------------------------------------------------------------

struct EnvSnapshot {
    Clock::time_point stamp;
    std::vector<SearchPath> paths;
    fs::path interpreter;
};

std::mutex g_envMutex;
std::unordered_map<std::wstring, EnvSnapshot> g_envCache;

struct DirListing {
    fs::file_time_type mtime;
    std::vector<std::wstring> names;
};
std::mutex g_listMutex;
std::unordered_map<std::wstring, DirListing> g_listCache;

enum class IntrospectState { Pending, Done, Failed };
struct IntrospectEntry {
    IntrospectState state = IntrospectState::Pending;
    Clock::time_point stamp;
    std::vector<ModuleMember> members;
};
std::mutex g_introMutex;
std::condition_variable g_introCv;
std::unordered_map<std::wstring, IntrospectEntry> g_introCache;

EnvSnapshot ResolveEnv(const fs::path& projectRoot) {
    std::wstring key = PathKey(projectRoot);
    std::lock_guard<std::mutex> lock(g_envMutex);
    auto it = g_envCache.find(key);
    if (it != g_envCache.end() && Clock::now() - it->second.stamp < std::chrono::seconds(4)) {
        return it->second;
    }

    EnvSnapshot snap;
    snap.stamp = Clock::now();
    std::error_code ec;

    auto envs = FindProjectEnvs(projectRoot);
    auto bases = FindBaseInterpreters(envs);

    for (const auto& env : envs) {
        if (!env.sitePackages.empty()) {
            snap.paths.push_back({ env.sitePackages, L"Project " + env.root.filename().wstring(), PackageSource::ProjectEnv });
        }
        if (snap.interpreter.empty() && !env.interpreter.empty()) snap.interpreter = env.interpreter;
    }

    for (const auto& base : bases) {
        std::wstring label = base.versionLabel.empty() ? L"Python Global" : (L"Python " + base.versionLabel + L" Global");
        fs::path dir = base.exe.parent_path();
        if (fs::is_directory(dir / L"Lib" / L"site-packages", ec)) {
            snap.paths.push_back({ dir / L"Lib" / L"site-packages", label, PackageSource::External });
        }
        // pip install --user location: %APPDATA%\Python\PythonXY\site-packages
        wchar_t appData[4096];
        if (!base.versionLabel.empty() && GetEnvironmentVariableW(L"APPDATA", appData, 4096) > 0) {
            std::wstring ver = base.versionLabel;
            ver.erase(std::remove(ver.begin(), ver.end(), L'.'), ver.end());
            fs::path userSite = fs::path(appData) / L"Python" / (L"Python" + ver) / L"site-packages";
            if (fs::is_directory(userSite, ec)) {
                snap.paths.push_back({ userSite, label + L" (user)", PackageSource::External });
            }
        }
        if (snap.interpreter.empty()) snap.interpreter = base.exe;
    }

    // Standard library of the first base interpreter (the venv's base when one exists)
    if (!bases.empty()) {
        fs::path dir = bases.front().exe.parent_path();
        std::wstring label = bases.front().versionLabel.empty() ? L"Python" : (L"Python " + bases.front().versionLabel);
        if (fs::is_directory(dir / L"Lib", ec)) snap.paths.push_back({ dir / L"Lib", label, PackageSource::StdLib });
        if (fs::is_directory(dir / L"DLLs", ec)) snap.paths.push_back({ dir / L"DLLs", label, PackageSource::StdLib });
    }

    auto& slot = g_envCache[key];
    slot = std::move(snap);
    return slot;
}

std::vector<std::wstring> ListImportableNames(const fs::path& dir, bool isStdLib) {
    std::error_code ec;
    auto mtime = fs::last_write_time(dir, ec);
    std::wstring key = PathKey(dir);
    {
        std::lock_guard<std::mutex> lock(g_listMutex);
        auto it = g_listCache.find(key);
        if (it != g_listCache.end() && !ec && it->second.mtime == mtime) return it->second.names;
    }

    std::set<std::wstring> names;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::wstring fn = it->path().filename().wstring();
        if (it->is_directory(ec)) {
            if (fn == L"__pycache__" || fn == L"site-packages") continue;
            if (fn.find(L".dist-info") != std::wstring::npos || fn.find(L".egg-info") != std::wstring::npos) continue;
            if (!IsIdentifier(fn)) continue; // filters numpy.libs, bin, etc.
            // In the stdlib only real packages count (skip data folders without __init__.py)
            if (isStdLib && !fs::exists(it->path() / L"__init__.py", ec)) continue;
            names.insert(fn);
        } else {
            std::wstring ext = ToLower(it->path().extension().wstring());
            if (ext != L".py" && ext != L".pyd") continue;
            std::wstring stem = fn.substr(0, fn.find(L'.'));
            if (IsIdentifier(stem)) names.insert(stem);
        }
    }
    std::vector<std::wstring> result(names.begin(), names.end());
    std::lock_guard<std::mutex> lock(g_listMutex);
    g_listCache[key] = { mtime, result };
    return result;
}

// ---------------------------------------------------------------------------------------------
// Interpreter introspection
// ---------------------------------------------------------------------------------------------

const char* kIntrospectScript =
    "import sys, os, importlib, inspect\n"
    "for p in sys.argv[2:]:\n"
    "    if p and p not in sys.path:\n"
    "        sys.path.append(p)\n"
    "real_out = sys.stdout\n"
    "sys.stdout = open(os.devnull, 'w')\n"
    "sys.stderr = open(os.devnull, 'w')\n"
    "try:\n"
    "    mod = importlib.import_module(sys.argv[1])\n"
    "except BaseException:\n"
    "    sys.exit(3)\n"
    "rows = []\n"
    "names = set()\n"
    "for n in dir(mod):\n"
    "    if n.startswith('_'):\n"
    "        continue\n"
    "    try:\n"
    "        o = getattr(mod, n)\n"
    "    except BaseException:\n"
    "        continue\n"
    "    if inspect.ismodule(o):\n"
    "        k = 'm'\n"
    "    elif inspect.isclass(o):\n"
    "        k = 'c'\n"
    "    elif callable(o):\n"
    "        k = 'f'\n"
    "    else:\n"
    "        k = 'v'\n"
    "    sig = ''\n"
    "    if k in 'cf':\n"
    "        try:\n"
    "            sig = str(inspect.signature(o))\n"
    "        except BaseException:\n"
    "            sig = ''\n"
    "    names.add(n)\n"
    "    rows.append(n + '\\t' + k + '\\t' + sig.replace('\\n', ' ').replace('\\t', ' '))\n"
    "if hasattr(mod, '__path__'):\n"
    "    try:\n"
    "        import pkgutil\n"
    "        for info in pkgutil.iter_modules(mod.__path__):\n"
    "            if not info.name.startswith('_') and info.name not in names:\n"
    "                rows.append(info.name + '\\tm\\t')\n"
    "    except BaseException:\n"
    "        pass\n"
    "real_out.buffer.write(('\\n'.join(rows) + '\\n').encode('utf-8', 'replace'))\n"
    "real_out.flush()\n";

fs::path EnsureIntrospectScript() {
    static std::once_flag once;
    static fs::path scriptPath;
    std::call_once(once, [] {
        wchar_t tmp[MAX_PATH + 1] = { 0 };
        if (GetTempPathW(MAX_PATH, tmp) == 0) return;
        fs::path p = fs::path(tmp) / L"phide_py_introspect_v2.py";
        std::ofstream outFile(p, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) return;
        outFile << kIntrospectScript;
        outFile.close();
        scriptPath = p;
    });
    return scriptPath;
}

bool RunIntrospection(const fs::path& interpreter, const std::wstring& module,
                      const std::vector<fs::path>& extraPaths, std::vector<ModuleMember>& out) {
    fs::path script = EnsureIntrospectScript();
    if (script.empty() || interpreter.empty()) return false;

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = nullptr;

    std::wstring cmd = L"\"" + interpreter.wstring() + L"\" -X utf8 \"" + script.wstring() + L"\" " + module;
    for (const auto& ep : extraPaths) {
        cmd += L" \"" + ep.wstring() + L"\"";
    }
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             interpreter.parent_path().c_str(), &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return false;
    }

    std::string raw;
    auto started = Clock::now();
    char chunk[4096];
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD got = 0;
            if (ReadFile(hRead, chunk, (std::min)(avail, static_cast<DWORD>(sizeof(chunk))), &got, nullptr) && got > 0) {
                raw.append(chunk, got);
                continue;
            }
        }
        if (WaitForSingleObject(pi.hProcess, 30) == WAIT_OBJECT_0) {
            // Drain anything left
            while (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD got = 0;
                if (!ReadFile(hRead, chunk, (std::min)(avail, static_cast<DWORD>(sizeof(chunk))), &got, nullptr) || got == 0) break;
                raw.append(chunk, got);
            }
            break;
        }
        if (Clock::now() - started > std::chrono::seconds(25)) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
    if (exitCode != 0) return false;

    std::wstring text = Utf8ToWide(raw);
    size_t s = 0;
    while (s < text.size()) {
        size_t e = text.find(L'\n', s);
        if (e == std::wstring::npos) e = text.size();
        std::wstring line = text.substr(s, e - s);
        s = e + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t t1 = line.find(L'\t');
        if (t1 == std::wstring::npos) continue;
        size_t t2 = line.find(L'\t', t1 + 1);
        ModuleMember m;
        m.name = line.substr(0, t1);
        m.kind = (t1 + 1 < line.size()) ? line[t1 + 1] : L'v';
        m.signature = (t2 != std::wstring::npos) ? line.substr(t2 + 1) : L"";
        if (IsIdentifier(m.name)) out.push_back(std::move(m));
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

std::vector<SearchPath> GetSearchPaths(const fs::path& projectRoot) {
    return ResolveEnv(projectRoot).paths;
}

fs::path GetPreferredInterpreter(const fs::path& projectRoot) {
    return ResolveEnv(projectRoot).interpreter;
}

std::vector<ModuleEntry> ListTopLevelModules(const fs::path& projectRoot) {
    std::vector<ModuleEntry> result;
    std::unordered_set<std::wstring> seen;
    for (const auto& sp : GetSearchPaths(projectRoot)) {
        for (const auto& name : ListImportableNames(sp.path, sp.source == PackageSource::StdLib)) {
            if (sp.source == PackageSource::StdLib && !name.empty() && name[0] == L'_') continue;
            if (seen.insert(name).second) result.push_back({ name, sp.label, sp.source });
        }
    }
    return result;
}

std::vector<ModuleEntry> ListSubModules(const fs::path& projectRoot, const std::wstring& dottedPackage) {
    std::vector<ModuleEntry> result;
    if (dottedPackage.empty()) return result;
    fs::path rel;
    size_t s = 0;
    while (s <= dottedPackage.size()) {
        size_t e = dottedPackage.find(L'.', s);
        if (e == std::wstring::npos) e = dottedPackage.size();
        rel /= dottedPackage.substr(s, e - s);
        s = e + 1;
    }
    std::error_code ec;
    std::unordered_set<std::wstring> seen;
    for (const auto& sp : GetSearchPaths(projectRoot)) {
        fs::path pkgDir = sp.path / rel;
        if (!fs::is_directory(pkgDir, ec)) continue;
        for (const auto& name : ListImportableNames(pkgDir, false)) {
            if (name.empty() || name[0] == L'_') continue;
            if (seen.insert(name).second) result.push_back({ name, sp.label, sp.source });
        }
        break; // first match on the search path wins, like Python's import system
    }
    return result;
}

void PrefetchModule(const fs::path& projectRoot, const std::wstring& dottedModule) {
    std::vector<ModuleMember> dummy;
    GetModuleMembers(projectRoot, dottedModule, dummy, 0);
}

bool GetModuleMembers(const fs::path& projectRoot, const std::wstring& dottedModule, std::vector<ModuleMember>& out, int maxWaitMs) {
    if (dottedModule.empty()) return false;
    for (wchar_t c : dottedModule) {
        if (!(iswalnum(c) || c == L'_' || c == L'.')) return false; // never pass arbitrary text to the interpreter
    }
    auto snap = ResolveEnv(projectRoot);
    fs::path interpreter = snap.interpreter;
    if (interpreter.empty()) return false;

    std::vector<fs::path> extraPaths;
    for (const auto& sp : snap.paths) {
        if (sp.source == PackageSource::ProjectEnv || sp.source == PackageSource::External) {
            extraPaths.push_back(sp.path);
        }
    }

    std::wstring key = PathKey(interpreter) + L"|" + dottedModule;
    bool needSpawn = false;
    {
        std::unique_lock<std::mutex> lock(g_introMutex);
        auto it = g_introCache.find(key);
        if (it != g_introCache.end()) {
            if (it->second.state == IntrospectState::Done) {
                out = it->second.members;
                return true;
            }
            if (it->second.state == IntrospectState::Pending) {
                if (maxWaitMs > 0 && g_introCv.wait_for(lock, std::chrono::milliseconds(maxWaitMs), [&] {
                        auto cur = g_introCache.find(key);
                        return cur != g_introCache.end() && cur->second.state != IntrospectState::Pending;
                    })) {
                    auto cur = g_introCache.find(key);
                    if (cur != g_introCache.end() && cur->second.state == IntrospectState::Done) {
                        out = cur->second.members;
                        return true;
                    }
                }
                return false;
            }
            // Failed: retry after a while (e.g. the package has just been installed)
            if (Clock::now() - it->second.stamp < std::chrono::seconds(15)) return false;
        }
        auto& entry = g_introCache[key];
        entry.state = IntrospectState::Pending;
        entry.stamp = Clock::now();
        entry.members.clear();
        needSpawn = true;
    }

    if (needSpawn) {
        std::thread([key, interpreter, dottedModule, extraPaths]() {
            std::vector<ModuleMember> members;
            bool ok = RunIntrospection(interpreter, dottedModule, extraPaths, members);
            {
                std::lock_guard<std::mutex> lock(g_introMutex);
                auto& entry = g_introCache[key];
                entry.state = ok ? IntrospectState::Done : IntrospectState::Failed;
                entry.stamp = Clock::now();
                entry.members = std::move(members);
            }
            g_introCv.notify_all();
        }).detach();

        if (maxWaitMs > 0) {
            std::unique_lock<std::mutex> lock(g_introMutex);
            if (g_introCv.wait_for(lock, std::chrono::milliseconds(maxWaitMs), [&] {
                    auto cur = g_introCache.find(key);
                    return cur != g_introCache.end() && cur->second.state != IntrospectState::Pending;
                })) {
                auto cur = g_introCache.find(key);
                if (cur != g_introCache.end() && cur->second.state == IntrospectState::Done) {
                    out = cur->second.members;
                    return true;
                }
            }
        }
    }
    return false;
}

void InvalidateCaches() {
    {
        std::lock_guard<std::mutex> lock(g_envMutex);
        g_envCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_listMutex);
        g_listCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_introMutex);
        for (auto it = g_introCache.begin(); it != g_introCache.end();) {
            if (it->second.state == IntrospectState::Pending) ++it;
            else it = g_introCache.erase(it);
        }
    }
}

} // namespace phide::editor::pyenv
