/**
 * @file DapClient.cpp
 * @brief Real Debug Adapter Protocol (DAP) & Multi-Backend Step Debugger implementation (#26 & #27).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "phide/dev/DapClient.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

namespace phide::dev {

namespace fs = std::filesystem;

namespace {

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

std::string EscapeJsonUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char hex[8];
            snprintf(hex, sizeof(hex), "\\u%04x", c);
            out += hex;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

std::string UnescapeJsonStr(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char nxt = raw[++i];
            if (nxt == 'n') out.push_back('\n');
            else if (nxt == 'r') out.push_back('\r');
            else if (nxt == 't') out.push_back('\t');
            else if (nxt == '\\' || nxt == '"' || nxt == '/') out.push_back(nxt);
            else out.push_back(nxt);
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

bool ExtractJsonStringA(const std::string& json, const std::string& key, std::string& outVal, size_t startPos = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, startPos);
    if (pos == std::string::npos) return false;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
    if (i >= json.size() || json[i] != '"') return false;
    ++i;
    std::string res;
    bool esc = false;
    for (; i < json.size(); ++i) {
        char c = json[i];
        if (esc) {
            if (c == 'n') res.push_back('\n');
            else if (c == 'r') res.push_back('\r');
            else if (c == 't') res.push_back('\t');
            else res.push_back(c);
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (c == '"') {
            outVal = std::move(res);
            return true;
        } else {
            res.push_back(c);
        }
    }
    return false;
}

bool ExtractJsonIntA(const std::string& json, const std::string& key, int& outVal, size_t startPos = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, startPos);
    if (pos == std::string::npos) return false;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
    if (i >= json.size()) return false;
    size_t j = i;
    if (json[j] == '-') ++j;
    while (j < json.size() && std::isdigit(static_cast<unsigned char>(json[j]))) ++j;
    if (j == i) return false;
    try {
        outVal = std::stoi(json.substr(i, j - i));
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> SplitJsonObjectArrayA(const std::string& json, const std::string& arrayKey) {
    std::vector<std::string> objs;
    std::string needle = "\"" + arrayKey + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return objs;
    size_t arr = json.find('[', pos + needle.size());
    if (arr == std::string::npos) return objs;
    size_t i = arr + 1;
    bool inStr = false, esc = false;
    while (i < json.size()) {
        char c = json[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            ++i;
            continue;
        }
        if (c == '"') { inStr = true; ++i; continue; }
        if (c == ']') break;
        if (c == '{') {
            size_t objStart = i;
            int depth = 0;
            bool subStr = false, subEsc = false;
            for (; i < json.size(); ++i) {
                char sc = json[i];
                if (subStr) {
                    if (subEsc) subEsc = false;
                    else if (sc == '\\') subEsc = true;
                    else if (sc == '"') subStr = false;
                } else {
                    if (sc == '"') subStr = true;
                    else if (sc == '{') depth++;
                    else if (sc == '}' && --depth == 0) break;
                }
            }
            if (depth == 0 && i > objStart) {
                objs.push_back(json.substr(objStart, i - objStart + 1));
            }
        }
        ++i;
    }
    return objs;
}

std::wstring ReplaceMacrosW(std::wstring val, const std::wstring& workspaceRoot, const std::wstring& activeFile) {
    auto rep = [&](const std::wstring& from, const std::wstring& to) {
        size_t pos = 0;
        while ((pos = val.find(from, pos)) != std::wstring::npos) {
            val.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    rep(L"${workspaceFolder}", workspaceRoot);
    rep(L"${file}", activeFile);
    if (!activeFile.empty()) {
        rep(L"${fileDirname}", fs::path(activeFile).parent_path().wstring());
        rep(L"${fileBasenameNoExtension}", fs::path(activeFile).stem().wstring());
        rep(L"${fileBasename}", fs::path(activeFile).filename().wstring());
    }
    return val;
}

bool PathsMatchW(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return false;
    std::wstring na = a, nb = b;
    for (auto& c : na) if (c == L'/') c = L'\\';
    for (auto& c : nb) if (c == L'/') c = L'\\';
    if (_wcsicmp(na.c_str(), nb.c_str()) == 0) return true;
    return _wcsicmp(fs::path(na).filename().c_str(), fs::path(nb).filename().c_str()) == 0 &&
           (na.ends_with(fs::path(nb).filename().wstring()) || nb.ends_with(fs::path(na).filename().wstring()));
}

std::wstring FindGdbExecutable(const std::wstring& configured) {
    std::error_code ec;
    if (!configured.empty() && fs::exists(configured, ec)) return configured;
    if (fs::exists(L"C:\\MinGW\\bin\\gdb.exe", ec)) return L"C:\\MinGW\\bin\\gdb.exe";
    if (fs::exists(L"C:\\msys64\\ucrt64\\bin\\gdb.exe", ec)) return L"C:\\msys64\\ucrt64\\bin\\gdb.exe";
    if (fs::exists(L"C:\\msys64\\mingw64\\bin\\gdb.exe", ec)) return L"C:\\msys64\\mingw64\\bin\\gdb.exe";
    return L"gdb.exe";
}

std::wstring FindPhpExecutable() {
    std::error_code ec;
    if (fs::exists(L"C:\\Program Files\\phide\\runtimes\\php\\php.exe", ec)) {
        return L"C:\\Program Files\\phide\\runtimes\\php\\php.exe";
    }
    return L"php.exe";
}

int PickFreeLoopbackPort() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 19229;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int port = 19229;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        sockaddr_in bound{};
        int len = sizeof(bound);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
            port = ntohs(bound.sin_port);
        }
    }
    closesocket(s);
    return port;
}

// Self-contained Python bdb JSON-line debugger adapter running inside real python.exe
const char* kPythonBdbScript = R"PY(
import bdb, json, os, sys, threading, queue, linecache

cmd_q = queue.Queue()
wait_ev = threading.Event()
dbg_ref = [None]
paused_state = {"paused": False, "frame": None}

def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()

def reader_loop():
    while True:
        line = sys.stdin.readline()
        if not line:
            cmd_q.put({"cmd": "quit"})
            wait_ev.set()
            break
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
            cmd = msg.get("cmd", "")
            if cmd == "eval":
                f = paused_state.get("frame")
                expr = msg.get("expr", "")
                res_txt = ""
                try:
                    if f is not None:
                        val = eval(expr, f.f_globals, f.f_locals)
                        res_txt = repr(val)
                    else:
                        res_txt = "<not paused>"
                except Exception as ex:
                    res_txt = f"<Error: {ex}>"
                emit({"event": "eval", "result": res_txt})
            elif cmd == "set_bp":
                d = dbg_ref[0]
                if d is not None:
                    d.clear_all_breaks()
                    for bp in msg.get("bps", []):
                        try:
                            d.set_break(bp["file"], int(bp["line"]), cond=bp.get("cond") or None)
                        except Exception:
                            pass
            elif cmd == "set_exc":
                paused_state["break_raised"] = bool(msg.get("raised", False))
                paused_state["break_uncaught"] = bool(msg.get("uncaught", True))
            else:
                cmd_q.put(msg)
                wait_ev.set()
        except Exception:
            pass

def build_var(name, val, obj_map, depth=0):
    try:
        r = repr(val)
        if len(r) > 120:
            r = r[:117] + "..."
    except Exception:
        r = "<unrepr>"
    tname = type(val).__name__
    vref = 0
    children = []
    if depth < 2 and isinstance(val, (dict, list, tuple, set)):
        vref = len(obj_map) + 10
        obj_map[vref] = val
    elif depth < 2 and hasattr(val, "__dict__"):
        vref = len(obj_map) + 10
        obj_map[vref] = val.__dict__
    return {"name": str(name), "value": r, "type": tname, "varRef": vref}

class PhideBdb(bdb.Bdb):
    def __init__(self):
        super().__init__()
        self.target_file = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else ""
        self.first_stop = True
        self.obj_map = {}

    def _emit_stop(self, frame, reason, exc_text=""):
        self.obj_map = {}
        paused_state["paused"] = True
        paused_state["frame"] = frame
        frames = []
        f = frame
        fid = 0
        while f is not None and len(frames) < 40:
            fn = os.path.abspath(f.f_code.co_filename)
            if "string" not in fn or fn == self.target_file:
                frames.append({
                    "id": fid,
                    "name": f.f_code.co_name,
                    "file": fn,
                    "line": f.f_lineno
                })
                fid += 1
            f = f.f_back

        locals_list = []
        for k, v in list(frame.f_locals.items()):
            if str(k).startswith("__phide"):
                continue
            locals_list.append(build_var(k, v, self.obj_map))
        globals_list = []
        for k, v in list(frame.f_globals.items()):
            if str(k).startswith("__") or callable(v) or type(v).__name__ == "module":
                continue
            globals_list.append(build_var(k, v, self.obj_map))

        loaded = []
        for mname, mobj in list(sys.modules.items())[:40]:
            mf = getattr(mobj, "__file__", None)
            if mf:
                loaded.append(os.path.abspath(mf))

        emit({
            "event": "stopped",
            "reason": reason,
            "text": exc_text,
            "file": os.path.abspath(frame.f_code.co_filename),
            "line": frame.f_lineno,
            "frames": frames,
            "locals": locals_list[:60],
            "globals": globals_list[:40],
            "scripts": loaded
        })

        while True:
            wait_ev.wait()
            wait_ev.clear()
            while not cmd_q.empty():
                c = cmd_q.get()
                action = c.get("cmd")
                if action == "continue":
                    paused_state["paused"] = False
                    self.set_continue()
                    return
                elif action == "next":
                    paused_state["paused"] = False
                    self.set_next(frame)
                    return
                elif action == "step":
                    paused_state["paused"] = False
                    self.set_step()
                    return
                elif action == "step_out":
                    paused_state["paused"] = False
                    self.set_return(frame)
                    return
                elif action == "children":
                    vref = int(c.get("varRef", 0))
                    container = self.obj_map.get(vref)
                    ch = []
                    if isinstance(container, dict):
                        for k, v in list(container.items())[:50]:
                            ch.append(build_var(k, v, self.obj_map, 1))
                    elif isinstance(container, (list, tuple, set)):
                        for idx, v in enumerate(list(container)[:50]):
                            ch.append(build_var(f"[{idx}]", v, self.obj_map, 1))
                    emit({"event": "children", "varRef": vref, "children": ch})
                elif action == "quit":
                    self.set_quit()
                    return

    def user_line(self, frame):
        fn = os.path.abspath(frame.f_code.co_filename)
        if os.path.normcase(fn) != os.path.normcase(self.target_file) and self.first_stop and not self.stop_on_entry:
            self.first_stop = False
            self.set_continue()
            return
        if self.first_stop:
            self.first_stop = False
            if self.stop_on_entry:
                self._emit_stop(frame, "entry")
                return
            if not self.get_break(fn, frame.f_lineno):
                self.set_continue()
                return
        self._emit_stop(frame, "breakpoint" if self.get_break(fn, frame.f_lineno) else "step")

    def user_exception(self, frame, exc_info):
        exc_type, exc_val, _ = exc_info
        if paused_state.get("break_raised", False):
            self._emit_stop(frame, "exception", f"{exc_type.__name__}: {exc_val}")

if len(sys.argv) < 2:
    emit({"event": "terminated", "code": 1})
    sys.exit(1)

target = os.path.abspath(sys.argv[1])
stop_entry = os.environ.get("PHIDE_STOP_ON_ENTRY", "0") == "1"
init_bps = json.loads(os.environ.get("PHIDE_INIT_BPS", "[]"))
paused_state["break_raised"] = os.environ.get("PHIDE_EXC_RAISED", "0") == "1"
paused_state["break_uncaught"] = os.environ.get("PHIDE_EXC_UNCAUGHT", "1") == "1"

d = PhideBdb()
d.stop_on_entry = stop_entry
dbg_ref[0] = d
for bp in init_bps:
    try:
        d.set_break(bp["file"], int(bp["line"]), cond=bp.get("cond") or None)
    except Exception:
        pass

t = threading.Thread(target=reader_loop, daemon=True)
t.start()

sys.argv = sys.argv[1:]
sys.path.insert(0, os.path.dirname(target))
exit_code = 0
try:
    with open(target, "r", encoding="utf-8") as fp:
        src = fp.read()
    code_obj = compile(src, target, "exec")
    d.runctx(code_obj, {"__name__": "__main__", "__file__": target}, None)
except bdb.BdbQuit:
    pass
except Exception as ex:
    emit({"event": "error", "text": f"Uncaught {type(ex).__name__}: {ex}"})
    exit_code = 1
finally:
    emit({"event": "terminated", "code": exit_code})
)PY";

} // namespace

// =========================================================================
// DAP Wire Framing (Content-Length: N\r\n\r\n<JSON>)
// =========================================================================

void DapFrameDecoder::Feed(const char* data, size_t len) {
    if (data && len > 0) m_buffer.append(data, len);
}

bool DapFrameDecoder::PopMessage(std::string& outJson) {
    outJson.clear();
    size_t hdrEnd = m_buffer.find("\r\n\r\n");
    size_t sepLen = 4;
    if (hdrEnd == std::string::npos) {
        hdrEnd = m_buffer.find("\n\n");
        sepLen = 2;
    }
    if (hdrEnd == std::string::npos) return false;

    std::string header = m_buffer.substr(0, hdrEnd);
    std::string lowerHdr = header;
    for (char& c : lowerHdr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t clPos = lowerHdr.find("content-length:");
    if (clPos == std::string::npos) {
        m_buffer.erase(0, hdrEnd + sepLen);
        return false;
    }
    size_t numStart = clPos + 15;
    while (numStart < header.size() && (header[numStart] == ' ' || header[numStart] == '\t')) ++numStart;
    size_t contentLen = 0;
    try {
        contentLen = static_cast<size_t>(std::stoul(header.substr(numStart)));
    } catch (...) {
        m_buffer.erase(0, hdrEnd + sepLen);
        return false;
    }

    size_t bodyStart = hdrEnd + sepLen;
    if (m_buffer.size() < bodyStart + contentLen) return false;

    outJson = m_buffer.substr(bodyStart, contentLen);
    m_buffer.erase(0, bodyStart + contentLen);
    return true;
}

void DapFrameDecoder::Clear() {
    m_buffer.clear();
}

std::string EncodeDapFrame(const std::string& jsonPayload) {
    return "Content-Length: " + std::to_string(jsonPayload.size()) + "\r\n\r\n" + jsonPayload;
}

// =========================================================================
// Launch Configurations (.vscode/launch.json)
// =========================================================================

std::vector<LaunchConfig> ParseLaunchJson(
    const std::wstring& jsonContent,
    const std::wstring& workspaceRoot,
    const std::wstring& activeFilePath) {
    std::vector<LaunchConfig> configs;
    std::string utf8 = WideToUtf8(jsonContent);
    for (const auto& obj : SplitJsonObjectArrayA(utf8, "configurations")) {
        LaunchConfig cfg;
        std::string s;
        if (ExtractJsonStringA(obj, "name", s)) cfg.name = Utf8ToWide(s);
        if (ExtractJsonStringA(obj, "type", s)) cfg.type = Utf8ToWide(s);
        if (ExtractJsonStringA(obj, "request", s)) cfg.request = Utf8ToWide(s);
        if (ExtractJsonStringA(obj, "program", s)) {
            cfg.program = ReplaceMacrosW(Utf8ToWide(s), workspaceRoot, activeFilePath);
        }
        if (ExtractJsonStringA(obj, "cwd", s)) {
            cfg.cwd = ReplaceMacrosW(Utf8ToWide(s), workspaceRoot, activeFilePath);
        } else {
            cfg.cwd = workspaceRoot;
        }
        if (ExtractJsonStringA(obj, "preLaunchTask", s)) cfg.preLaunchTask = Utf8ToWide(s);
        if (ExtractJsonStringA(obj, "miDebuggerPath", s)) cfg.miDebuggerPath = Utf8ToWide(s);
        int port = 0;
        if (ExtractJsonIntA(obj, "port", port)) cfg.port = port;
        cfg.stopOnEntry = (obj.find("\"stopOnEntry\": true") != std::string::npos ||
                           obj.find("\"stopOnEntry\":true") != std::string::npos);

        // Optional string args array
        size_t argsPos = obj.find("\"args\"");
        if (argsPos != std::string::npos) {
            size_t b = obj.find('[', argsPos);
            size_t e = (b != std::string::npos) ? obj.find(']', b) : std::string::npos;
            if (b != std::string::npos && e != std::string::npos) {
                std::string arr = obj.substr(b + 1, e - b - 1);
                size_t cur = 0;
                while ((cur = arr.find('"', cur)) != std::string::npos) {
                    size_t endQ = arr.find('"', cur + 1);
                    if (endQ == std::string::npos) break;
                    cfg.args.push_back(ReplaceMacrosW(Utf8ToWide(UnescapeJsonStr(arr.substr(cur + 1, endQ - cur - 1))),
                                                      workspaceRoot, activeFilePath));
                    cur = endQ + 1;
                }
            }
        }
        if (!cfg.name.empty() || !cfg.program.empty()) configs.push_back(std::move(cfg));
    }
    return configs;
}

std::vector<LaunchConfig> LoadWorkspaceLaunchConfigs(
    const std::wstring& workspaceRoot,
    const std::wstring& activeFilePath) {
    std::vector<LaunchConfig> out;
    if (workspaceRoot.empty()) return out;
    fs::path p = fs::path(workspaceRoot) / L".vscode" / L"launch.json";
    std::error_code ec;
    if (!fs::exists(p, ec)) return out;
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return out;
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return ParseLaunchJson(Utf8ToWide(raw), workspaceRoot, activeFilePath);
}

// =========================================================================
// Conditional Breakpoint, Hit Count & Logpoint Evaluation
// =========================================================================

bool DapClient::ShouldPauseAtBreakpoint(
    DapBreakpoint& bp,
    const std::function<std::wstring(const std::wstring&)>& evalFn,
    std::wstring& outLogpointMessage) {
    outLogpointMessage.clear();
    if (!bp.enabled) return false;
    bp.hitCount++;
    if (bp.hitCondition > 0 && bp.hitCount < bp.hitCondition) {
        return false;
    }
    if (!bp.condition.empty() && evalFn) {
        std::wstring res = TrimW(evalFn(bp.condition));
        std::wstring low = ToLowerW(res);
        if (low.empty() || low == L"false" || low == L"0" || low == L"none" ||
            low == L"null" || low == L"undefined" || low == L"\"\"" || low == L"''") {
            return false;
        }
    }
    if (!bp.logMessage.empty()) {
        std::wstring msg = bp.logMessage;
        size_t pos = 0;
        while ((pos = msg.find(L'{', pos)) != std::wstring::npos) {
            size_t endB = msg.find(L'}', pos + 1);
            if (endB == std::wstring::npos) break;
            std::wstring expr = TrimW(msg.substr(pos + 1, endB - pos - 1));
            std::wstring val = evalFn ? evalFn(expr) : L"";
            msg.replace(pos, endB - pos + 1, val);
            pos += val.size();
        }
        outLogpointMessage = msg;
        return false; // Logpoints emit output without halting execution
    }
    return true;
}

// =========================================================================
// GDB/MI Parsing Helpers
// =========================================================================

void DapClient::ParseGdbMiFrames(const std::string& miRecord, std::vector<DapStackFrame>& outFrames) {
    outFrames.clear();
    size_t pos = 0;
    while ((pos = miRecord.find("frame={", pos)) != std::string::npos) {
        size_t endF = miRecord.find('}', pos);
        if (endF == std::string::npos) break;
        std::string seg = miRecord.substr(pos, endF - pos + 1);
        auto grab = [&](const std::string& key) -> std::string {
            std::string needle = key + "=\"";
            size_t kp = seg.find(needle);
            if (kp == std::string::npos) return "";
            size_t vs = kp + needle.size();
            size_t ve = seg.find('"', vs);
            return (ve != std::string::npos) ? UnescapeJsonStr(seg.substr(vs, ve - vs)) : "";
        };
        DapStackFrame frame;
        try { frame.id = std::stoi(grab("level")); } catch (...) { frame.id = static_cast<int>(outFrames.size()); }
        frame.name = Utf8ToWide(grab("func"));
        std::string fpath = grab("fullname");
        if (fpath.empty()) fpath = grab("file");
        frame.sourcePath = Utf8ToWide(fpath);
        try { frame.line1Based = (std::max)(1, std::stoi(grab("line"))); } catch (...) { frame.line1Based = 1; }
        outFrames.push_back(std::move(frame));
        pos = endF + 1;
    }
}

void DapClient::ParseGdbMiVariables(const std::string& miRecord, std::vector<DapVariable>& outVars) {
    outVars.clear();
    size_t pos = 0;
    while ((pos = miRecord.find("name=\"", pos)) != std::string::npos) {
        size_t ns = pos + 6;
        size_t ne = miRecord.find('"', ns);
        if (ne == std::string::npos) break;
        DapVariable v;
        v.name = Utf8ToWide(miRecord.substr(ns, ne - ns));
        size_t vs = miRecord.find("value=\"", ne);
        size_t nextName = miRecord.find("name=\"", ne);
        if (vs != std::string::npos && (nextName == std::string::npos || vs < nextName)) {
            vs += 7;
            size_t ve = vs;
            while (ve < miRecord.size()) {
                if (miRecord[ve] == '\\' && ve + 1 < miRecord.size()) { ve += 2; continue; }
                if (miRecord[ve] == '"') break;
                ++ve;
            }
            v.value = Utf8ToWide(UnescapeJsonStr(miRecord.substr(vs, ve - vs)));
        }
        size_t ts = miRecord.find("type=\"", ne);
        if (ts != std::string::npos && (nextName == std::string::npos || ts < nextName)) {
            ts += 6;
            size_t te = miRecord.find('"', ts);
            if (te != std::string::npos) v.type = Utf8ToWide(miRecord.substr(ts, te - ts));
        }
        outVars.push_back(std::move(v));
        pos = ne + 1;
    }
}

// =========================================================================
// DapClient Lifecycle & Control
// =========================================================================

DapClient::DapClient() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

DapClient::~DapClient() {
    StopSession();
    WSACleanup();
}

DapAdapterType DapClient::DetectAdapterForFile(const std::wstring& filePath) noexcept {
    if (filePath.empty()) return DapAdapterType::None;
    std::wstring ext = ToLowerW(fs::path(filePath).extension().wstring());
    if (ext == L".py" || ext == L".pyw") return DapAdapterType::Python;
    if (ext == L".js" || ext == L".mjs" || ext == L".cjs" || ext == L".ts") return DapAdapterType::NodeInspect;
    if (ext == L".c" || ext == L".cpp" || ext == L".cc" || ext == L".cxx" || ext == L".exe") return DapAdapterType::GdbMi;
    if (ext == L".php") return DapAdapterType::PhpXdebug;
    return DapAdapterType::None;
}

DapAdapterType DapClient::AdapterFromTypeString(const std::wstring& typeStr) noexcept {
    std::wstring t = ToLowerW(typeStr);
    if (t == L"python" || t == L"debugpy") return DapAdapterType::Python;
    if (t == L"node" || t == L"pwa-node" || t == L"chrome") return DapAdapterType::NodeInspect;
    if (t == L"cppdbg" || t == L"gdb" || t == L"lldb" || t == L"cppvsdbg") return DapAdapterType::GdbMi;
    if (t == L"php" || t == L"xdebug") return DapAdapterType::PhpXdebug;
    return DapAdapterType::None;
}

DapSessionSnapshot DapClient::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

bool DapClient::StartSession(
    const LaunchConfig& cfg,
    const std::vector<DapBreakpoint>& initialBreakpoints,
    const DapExceptionOptions& exceptions,
    const std::wstring& pythonExe) {
    StopSession();

    DapAdapterType adapter = !cfg.type.empty() ? AdapterFromTypeString(cfg.type) : DetectAdapterForFile(cfg.program);
    if (adapter == DapAdapterType::None) adapter = DetectAdapterForFile(cfg.program);
    if (adapter == DapAdapterType::None) return false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = DapSessionSnapshot{};
        m_state.adapter = adapter;
        m_state.isRunning = true;
        m_state.breakpoints = initialBreakpoints;
        m_state.exceptions = exceptions;
        if (!cfg.program.empty()) m_state.loadedScripts.push_back(cfg.program);
    }

    m_stopRequested.store(false);
    m_paused.store(false);
    m_running.store(true);
    {
        std::lock_guard<std::mutex> cl(m_cmdMutex);
        while (!m_cmdQueue.empty()) m_cmdQueue.pop();
    }

    m_workerThread = std::thread(&DapClient::WorkerLoop, this, cfg, pythonExe);
    return true;
}

void DapClient::StopSession() {
    m_stopRequested.store(true);
    QueueControlCmd("quit");
    if (m_sockHandle != static_cast<uintptr_t>(INVALID_SOCKET)) {
        closesocket(static_cast<SOCKET>(m_sockHandle));
        m_sockHandle = static_cast<uintptr_t>(INVALID_SOCKET);
    }
    if (m_hChildProc) {
        TerminateProcess(m_hChildProc, 0);
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    if (m_hStdinWrite) { CloseHandle(m_hStdinWrite); m_hStdinWrite = nullptr; }
    if (m_hStdoutRead) { CloseHandle(m_hStdoutRead); m_hStdoutRead = nullptr; }
    if (m_hChildProc) { CloseHandle(m_hChildProc); m_hChildProc = nullptr; }
    m_running.store(false);
    m_paused.store(false);
}

void DapClient::WritePipeLine(const std::string& line) {
    if (!m_hStdinWrite || line.empty()) return;
    DWORD written = 0;
    WriteFile(m_hStdinWrite, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

void DapClient::QueueControlCmd(const std::string& cmd, const std::wstring& arg, int intArg) {
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        m_cmdQueue.push({ cmd, arg, intArg });
    }
    m_cmdCv.notify_all();
}

void DapClient::Continue() { QueueControlCmd("continue"); }
void DapClient::Pause() { QueueControlCmd("pause"); }
void DapClient::StepOver() { QueueControlCmd("next"); }
void DapClient::StepInto() { QueueControlCmd("step"); }
void DapClient::StepOut() { QueueControlCmd("step_out"); }

void DapClient::SelectStackFrame(int frameId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state.selectedFrameId = frameId;
    for (const auto& f : m_state.callStack) {
        if (f.id == frameId) {
            m_state.pausedFile = f.sourcePath;
            m_state.pausedLine1Based = f.line1Based;
            if (onPaused) onPaused(f.sourcePath, f.line1Based);
            break;
        }
    }
    if (onStateChanged) onStateChanged();
}

void DapClient::UpdateBreakpoints(const std::vector<DapBreakpoint>& bps) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.breakpoints = bps;
    }
    QueueControlCmd("set_bp");
}

void DapClient::SetExceptionOptions(const DapExceptionOptions& opts) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.exceptions = opts;
    }
    QueueControlCmd("set_exc");
}

void DapClient::ExpandVariableChildren(int variablesReference) {
    if (variablesReference > 0) QueueControlCmd("children", L"", variablesReference);
}

void DapClient::AddWatch(const std::wstring& expression) {
    if (expression.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.watches.push_back({ expression, L"", L"", false });
    }
    RefreshWatches();
}

void DapClient::RemoveWatch(size_t index) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index < m_state.watches.size()) m_state.watches.erase(m_state.watches.begin() + index);
    }
    if (onStateChanged) onStateChanged();
}

void DapClient::RefreshWatches() {
    if (!m_paused.load()) {
        if (onStateChanged) onStateChanged();
        return;
    }
    QueueControlCmd("refresh_watches");
}

std::wstring DapClient::EvaluateBlocking(const std::wstring& expression, int timeoutMs) {
    if (!m_running.load() || expression.empty()) return L"";
    {
        std::lock_guard<std::mutex> lock(m_evalMutex);
        m_evalReady = false;
        m_evalResult.clear();
    }
    QueueControlCmd("eval", expression);
    std::unique_lock<std::mutex> lock(m_evalMutex);
    m_evalCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
        return m_evalReady || !m_running.load() || m_stopRequested.load();
    });
    return m_evalResult;
}

bool DapClient::HandleBreakpointEvaluationLocked(
    const std::wstring& filePath,
    int line1Based,
    const std::function<std::wstring(const std::wstring&)>& evalFn) {
    DapBreakpoint* matched = nullptr;
    for (auto& bp : m_state.breakpoints) {
        if (static_cast<int>(bp.line1Based) == line1Based && PathsMatchW(bp.filePath, filePath)) {
            matched = &bp;
            break;
        }
    }
    if (!matched) return true; // Step / Entry / Exception pause
    std::wstring logMsg;
    bool pause = ShouldPauseAtBreakpoint(*matched, evalFn, logMsg);
    if (!logMsg.empty() && onOutput) {
        onOutput(L"[Logpoint " + fs::path(filePath).filename().wstring() + L":" + std::to_wstring(line1Based) + L"] " + logMsg + L"\r\n");
    }
    return pause;
}

void DapClient::WorkerLoop(LaunchConfig cfg, std::wstring pythonExe) {
    DapAdapterType adapter;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        adapter = m_state.adapter;
    }
    if (adapter == DapAdapterType::Python) {
        RunPythonBdbSession(cfg, pythonExe);
    } else if (adapter == DapAdapterType::GdbMi) {
        RunGdbMiSession(cfg);
    } else if (adapter == DapAdapterType::NodeInspect) {
        RunNodeCdpSession(cfg);
    } else if (adapter == DapAdapterType::PhpXdebug) {
        RunPhpDbGpSession(cfg);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.isRunning = false;
        m_state.isPaused = false;
    }
    m_paused.store(false);
    m_running.store(false);
    {
        std::lock_guard<std::mutex> el(m_evalMutex);
        m_evalReady = true;
    }
    m_evalCv.notify_all();
    if (onStateChanged) onStateChanged();
    if (onTerminated) onTerminated(0);
}

// =========================================================================
// Backend 1: Python Real `bdb` Adapter Process
// =========================================================================

void DapClient::RunPythonBdbSession(const LaunchConfig& cfg, const std::wstring& pythonExe) {
    fs::path tmpBridge = fs::temp_directory_path() / (L"phide_bdb_" + std::to_wstring(GetCurrentProcessId()) + L".py");
    {
        std::ofstream ofs(tmpBridge, std::ios::binary);
        ofs << kPythonBdbScript;
    }

    // Export initial breakpoints and exception flags via environment variables
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SetEnvironmentVariableW(L"PHIDE_STOP_ON_ENTRY", cfg.stopOnEntry ? L"1" : L"0");
        SetEnvironmentVariableW(L"PHIDE_EXC_RAISED", m_state.exceptions.breakOnRaised ? L"1" : L"0");
        SetEnvironmentVariableW(L"PHIDE_EXC_UNCAUGHT", m_state.exceptions.breakOnUncaught ? L"1" : L"0");
        std::ostringstream oss;
        oss << "[";
        bool first = true;
        for (const auto& bp : m_state.breakpoints) {
            if (!bp.enabled) continue;
            if (!first) oss << ",";
            first = false;
            oss << "{\"file\":\"" << EscapeJsonUtf8(WideToUtf8(bp.filePath)) << "\",\"line\":" << bp.line1Based << "}";
        }
        oss << "]";
        SetEnvironmentVariableW(L"PHIDE_INIT_BPS", Utf8ToWide(oss.str()).c_str());
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hInRead = nullptr, hInWrite = nullptr, hOutRead = nullptr, hOutWrite = nullptr;
    CreatePipe(&hInRead, &hInWrite, &sa, 0);
    CreatePipe(&hOutRead, &hOutWrite, &sa, 0);
    SetHandleInformation(hInWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInRead;
    si.hStdOutput = hOutWrite;
    si.hStdError = hOutWrite;

    std::wstring py = pythonExe.empty() ? L"python.exe" : pythonExe;
    std::wstring cmd = L"\"" + py + L"\" -u \"" + tmpBridge.wstring() + L"\" \"" + cfg.program + L"\"";
    for (const auto& a : cfg.args) cmd += L" \"" + a + L"\"";

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cfg.cwd.empty() ? nullptr : cfg.cwd.c_str(), &si, &pi);
    CloseHandle(hInRead);
    CloseHandle(hOutWrite);

    if (!ok) {
        CloseHandle(hInWrite);
        CloseHandle(hOutRead);
        std::error_code ec;
        fs::remove(tmpBridge, ec);
        if (onOutput) onOutput(L"[DAP] Failed to start Python debugger.\r\n");
        return;
    }

    m_hChildProc = pi.hProcess;
    m_hStdinWrite = hInWrite;
    m_hStdoutRead = hOutRead;
    CloseHandle(pi.hThread);

    auto syncEvalBlocking = [&](const std::wstring& expr) -> std::wstring {
        std::string payload = "{\"cmd\":\"eval\",\"expr\":\"" + EscapeJsonUtf8(WideToUtf8(expr)) + "\"}\n";
        WritePipeLine(payload);
        std::string acc;
        char c;
        DWORD nr = 0;
        DWORD deadline = GetTickCount() + 1500;
        while (GetTickCount() < deadline) {
            DWORD avail = 0;
            if (PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                if (ReadFile(m_hStdoutRead, &c, 1, &nr, nullptr) && nr > 0) {
                    if (c == '\n') {
                        if (acc.find("\"event\": \"eval\"") != std::string::npos || acc.find("\"event\":\"eval\"") != std::string::npos) {
                            std::string rv;
                            ExtractJsonStringA(acc, "result", rv);
                            return Utf8ToWide(rv);
                        }
                        acc.clear();
                    } else if (c != '\r') acc.push_back(c);
                }
            } else Sleep(5);
        }
        return L"";
    };

    std::string lineBuf;
    char buf[4096];
    while (!m_stopRequested.load()) {
        // Drain control commands
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            while (!m_cmdQueue.empty()) {
                auto msg = std::move(m_cmdQueue.front());
                m_cmdQueue.pop();
                if (msg.cmd == "continue" || msg.cmd == "next" || msg.cmd == "step" || msg.cmd == "step_out" || msg.cmd == "quit") {
                    m_paused.store(false);
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.isPaused = false;
                    }
                    if (onContinued) onContinued();
                    if (onStateChanged) onStateChanged();
                    WritePipeLine("{\"cmd\":\"" + msg.cmd + "\"}\n");
                } else if (msg.cmd == "eval") {
                    WritePipeLine("{\"cmd\":\"eval\",\"expr\":\"" + EscapeJsonUtf8(WideToUtf8(msg.arg)) + "\"}\n");
                } else if (msg.cmd == "refresh_watches") {
                    std::vector<DapWatchItem> copy;
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        copy = m_state.watches;
                    }
                    for (auto& w : copy) {
                        w.value = syncEvalBlocking(w.expression);
                        w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                    }
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.watches = copy;
                    }
                    if (onStateChanged) onStateChanged();
                } else if (msg.cmd == "set_bp") {
                    std::ostringstream oss;
                    oss << "{\"cmd\":\"set_bp\",\"bps\":[";
                    bool first = true;
                    std::lock_guard<std::mutex> sl(m_mutex);
                    for (const auto& bp : m_state.breakpoints) {
                        if (!bp.enabled) continue;
                        if (!first) oss << ",";
                        first = false;
                        oss << "{\"file\":\"" << EscapeJsonUtf8(WideToUtf8(bp.filePath)) << "\",\"line\":" << bp.line1Based << "}";
                    }
                    oss << "]}\n";
                    WritePipeLine(oss.str());
                } else if (msg.cmd == "children") {
                    WritePipeLine("{\"cmd\":\"children\",\"varRef\":" + std::to_string(msg.intArg) + "}\n");
                } else if (msg.cmd == "set_exc") {
                    std::lock_guard<std::mutex> sl(m_mutex);
                    WritePipeLine(std::string("{\"cmd\":\"set_exc\",\"raised\":") +
                                  (m_state.exceptions.breakOnRaised ? "true" : "false") +
                                  ",\"uncaught\":" + (m_state.exceptions.breakOnUncaught ? "true" : "false") + "}\n");
                }
            }
        }

        DWORD avail = 0;
        if (PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD rd = 0;
            if (ReadFile(m_hStdoutRead, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
                lineBuf.append(buf, rd);
                size_t nl = 0;
                while ((nl = lineBuf.find('\n')) != std::string::npos) {
                    std::string ln = lineBuf.substr(0, nl);
                    lineBuf.erase(0, nl + 1);
                    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                    if (ln.empty()) continue;

                    std::string ev;
                    if (!ExtractJsonStringA(ln, "event", ev)) {
                        if (onOutput) onOutput(Utf8ToWide(ln) + L"\r\n");
                        continue;
                    }

                    if (ev == "stopped") {
                        std::string reason, fileA, textA;
                        int lineNum = 1;
                        ExtractJsonStringA(ln, "reason", reason);
                        ExtractJsonStringA(ln, "file", fileA);
                        ExtractJsonStringA(ln, "text", textA);
                        ExtractJsonIntA(ln, "line", lineNum);

                        std::wstring fileW = Utf8ToWide(fileA);
                        std::vector<DapStackFrame> frames;
                        for (const auto& fo : SplitJsonObjectArrayA(ln, "frames")) {
                            DapStackFrame fr;
                            std::string fnA, fpA;
                            ExtractJsonIntA(fo, "id", fr.id);
                            ExtractJsonIntA(fo, "line", fr.line1Based);
                            ExtractJsonStringA(fo, "name", fnA);
                            ExtractJsonStringA(fo, "file", fpA);
                            fr.name = Utf8ToWide(fnA);
                            fr.sourcePath = Utf8ToWide(fpA);
                            frames.push_back(std::move(fr));
                        }

                        auto parseVars = [&](const std::string& arrName) {
                            std::vector<DapVariable> vars;
                            for (const auto& vo : SplitJsonObjectArrayA(ln, arrName)) {
                                DapVariable v;
                                std::string vn, vv, vt;
                                ExtractJsonStringA(vo, "name", vn);
                                ExtractJsonStringA(vo, "value", vv);
                                ExtractJsonStringA(vo, "type", vt);
                                ExtractJsonIntA(vo, "varRef", v.variablesReference);
                                v.name = Utf8ToWide(vn);
                                v.value = Utf8ToWide(vv);
                                v.type = Utf8ToWide(vt);
                                vars.push_back(std::move(v));
                            }
                            return vars;
                        };

                        DapScope loc{ L"Locals", 1, true, parseVars("locals") };
                        DapScope glb{ L"Globals", 2, false, parseVars("globals") };

                        // Check client-side hit count & logpoint before committing pause
                        bool shouldPause = true;
                        if (reason == "breakpoint") {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            shouldPause = HandleBreakpointEvaluationLocked(fileW, lineNum, syncEvalBlocking);
                        }
                        if (!shouldPause) {
                            WritePipeLine("{\"cmd\":\"continue\"}\n");
                            continue;
                        }

                        std::vector<DapWatchItem> watchesCopy;
                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            watchesCopy = m_state.watches;
                        }
                        for (auto& w : watchesCopy) {
                            w.value = syncEvalBlocking(w.expression);
                            w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                        }

                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            m_state.isPaused = true;
                            m_state.stoppedReason = Utf8ToWide(reason);
                            m_state.stoppedText = Utf8ToWide(textA);
                            m_state.pausedFile = fileW;
                            m_state.pausedLine1Based = lineNum;
                            m_state.callStack = std::move(frames);
                            m_state.scopes = { std::move(loc), std::move(glb) };
                            m_state.watches = std::move(watchesCopy);
                        }
                        m_paused.store(true);
                        if (onPaused) onPaused(fileW, lineNum);
                        if (onStateChanged) onStateChanged();
                    } else if (ev == "eval") {
                        std::string res;
                        ExtractJsonStringA(ln, "result", res);
                        {
                            std::lock_guard<std::mutex> el(m_evalMutex);
                            m_evalResult = Utf8ToWide(res);
                            m_evalReady = true;
                        }
                        m_evalCv.notify_all();
                    } else if (ev == "children") {
                        int vref = 0;
                        ExtractJsonIntA(ln, "varRef", vref);
                        std::vector<DapVariable> kids;
                        for (const auto& co : SplitJsonObjectArrayA(ln, "children")) {
                            DapVariable v;
                            std::string vn, vv, vt;
                            ExtractJsonStringA(co, "name", vn);
                            ExtractJsonStringA(co, "value", vv);
                            ExtractJsonStringA(co, "type", vt);
                            ExtractJsonIntA(co, "varRef", v.variablesReference);
                            v.name = Utf8ToWide(vn);
                            v.value = Utf8ToWide(vv);
                            v.type = Utf8ToWide(vt);
                            kids.push_back(std::move(v));
                        }
                        std::lock_guard<std::mutex> sl(m_mutex);
                        for (auto& sc : m_state.scopes) {
                            for (auto& v : sc.variables) {
                                if (v.variablesReference == vref) {
                                    v.expanded = true;
                                    v.children = std::move(kids);
                                }
                            }
                        }
                        if (onStateChanged) onStateChanged();
                    } else if (ev == "error") {
                        std::string t;
                        ExtractJsonStringA(ln, "text", t);
                        if (onOutput) onOutput(Utf8ToWide(t) + L"\r\n");
                    } else if (ev == "terminated") {
                        m_stopRequested.store(true);
                        break;
                    }
                }
            }
        } else {
            if (WaitForSingleObject(m_hChildProc, 15) == WAIT_OBJECT_0) {
                DWORD rem = 0;
                if (!PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &rem, nullptr) || rem == 0) break;
            }
        }
    }

    std::error_code ec;
    fs::remove(tmpBridge, ec);
}

// =========================================================================
// Backend 2: Real GDB/MI (C/C++) Debugger Session
// =========================================================================

void DapClient::RunGdbMiSession(const LaunchConfig& cfg) {
    std::wstring gdbExe = FindGdbExecutable(cfg.miDebuggerPath);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hInRead = nullptr, hInWrite = nullptr, hOutRead = nullptr, hOutWrite = nullptr;
    CreatePipe(&hInRead, &hInWrite, &sa, 0);
    CreatePipe(&hOutRead, &hOutWrite, &sa, 0);
    SetHandleInformation(hInWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInRead;
    si.hStdOutput = hOutWrite;
    si.hStdError = hOutWrite;

    std::wstring targetExe = cfg.program;
    if (ToLowerW(fs::path(targetExe).extension().wstring()) != L".exe") {
        // Attempt sibling .exe if launching a .cpp/.c source directly
        fs::path sibling = fs::path(targetExe).replace_extension(L".exe");
        std::error_code ec;
        if (fs::exists(sibling, ec)) targetExe = sibling.wstring();
    }

    std::wstring cmd = L"\"" + gdbExe + L"\" --interpreter=mi2 --quiet \"" + targetExe + L"\"";
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cfg.cwd.empty() ? nullptr : cfg.cwd.c_str(), &si, &pi);
    CloseHandle(hInRead);
    CloseHandle(hOutWrite);

    if (!ok) {
        CloseHandle(hInWrite);
        CloseHandle(hOutRead);
        if (onOutput) onOutput(L"[GDB/MI] Failed to launch gdb.exe.\r\n");
        return;
    }

    m_hChildProc = pi.hProcess;
    m_hStdinWrite = hInWrite;
    m_hStdoutRead = hOutRead;
    CloseHandle(pi.hThread);

    {
        std::lock_guard<std::mutex> sl(m_mutex);
        for (const auto& bp : m_state.breakpoints) {
            if (!bp.enabled) continue;
            std::wstring slPath = bp.filePath;
            for (auto& ch : slPath) if (ch == L'\\') ch = L'/';
            WritePipeLine("-break-insert -f \"" + WideToUtf8(slPath) + ":" + std::to_string(bp.line1Based) + "\"\n");
        }
        if (cfg.stopOnEntry) WritePipeLine("-break-insert -t main\n");
        if (m_state.exceptions.breakOnRaised) WritePipeLine("catch throw\n");
    }
    WritePipeLine("-exec-run\n");

    auto queryMi = [&](const std::string& miCmd, const std::string& expectPrefix) -> std::string {
        WritePipeLine(miCmd + "\n");
        std::string acc;
        char c;
        DWORD nr = 0;
        DWORD deadline = GetTickCount() + 1200;
        while (GetTickCount() < deadline) {
            DWORD avail = 0;
            if (PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                if (ReadFile(m_hStdoutRead, &c, 1, &nr, nullptr) && nr > 0) {
                    if (c == '\n') {
                        if (acc.rfind(expectPrefix, 0) == 0 || acc.rfind("^error", 0) == 0) return acc;
                        acc.clear();
                    } else if (c != '\r') acc.push_back(c);
                }
            } else Sleep(5);
        }
        return "";
    };

    auto evalGdbExpr = [&](const std::wstring& expr) -> std::wstring {
        std::string resp = queryMi("-data-evaluate-expression \"" + EscapeJsonUtf8(WideToUtf8(expr)) + "\"", "^done,value=");
        size_t vp = resp.find("value=\"");
        if (vp != std::string::npos) {
            size_t vs = vp + 7;
            size_t ve = resp.rfind('"');
            if (ve != std::string::npos && ve >= vs) return Utf8ToWide(UnescapeJsonStr(resp.substr(vs, ve - vs)));
        }
        return Utf8ToWide(resp);
    };

    std::string lineBuf;
    char buf[4096];
    while (!m_stopRequested.load()) {
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            while (!m_cmdQueue.empty()) {
                auto msg = std::move(m_cmdQueue.front());
                m_cmdQueue.pop();
                if (msg.cmd == "continue") { m_paused.store(false); WritePipeLine("-exec-continue\n"); }
                else if (msg.cmd == "next") { m_paused.store(false); WritePipeLine("-exec-next\n"); }
                else if (msg.cmd == "step") { m_paused.store(false); WritePipeLine("-exec-step\n"); }
                else if (msg.cmd == "step_out") { m_paused.store(false); WritePipeLine("-exec-finish\n"); }
                else if (msg.cmd == "quit") { WritePipeLine("-gdb-exit\n"); }
                else if (msg.cmd == "eval") {
                    std::wstring res = evalGdbExpr(msg.arg);
                    std::lock_guard<std::mutex> el(m_evalMutex);
                    m_evalResult = res;
                    m_evalReady = true;
                    m_evalCv.notify_all();
                } else if (msg.cmd == "refresh_watches") {
                    std::vector<DapWatchItem> wc;
                    { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                    for (auto& w : wc) w.value = evalGdbExpr(w.expression);
                    { std::lock_guard<std::mutex> sl(m_mutex); m_state.watches = std::move(wc); }
                    if (onStateChanged) onStateChanged();
                }
            }
        }

        DWORD avail = 0;
        if (PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD rd = 0;
            if (ReadFile(m_hStdoutRead, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
                lineBuf.append(buf, rd);
                size_t nl = 0;
                while ((nl = lineBuf.find('\n')) != std::string::npos) {
                    std::string ln = lineBuf.substr(0, nl);
                    lineBuf.erase(0, nl + 1);
                    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                    if (ln.rfind("*stopped", 0) == 0) {
                        if (ln.find("exited") != std::string::npos) {
                            m_stopRequested.store(true);
                            break;
                        }
                        std::string stackRec = queryMi("-stack-list-frames 0 25", "^done,stack=");
                        std::string varsRec = queryMi("-stack-list-variables --all-values", "^done,variables=");
                        std::vector<DapStackFrame> frames;
                        std::vector<DapVariable> vars;
                        ParseGdbMiFrames(stackRec, frames);
                        ParseGdbMiVariables(varsRec, vars);

                        std::wstring pf = frames.empty() ? cfg.program : frames[0].sourcePath;
                        int pl = frames.empty() ? 1 : frames[0].line1Based;

                        std::vector<DapWatchItem> wc;
                        { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                        for (auto& w : wc) w.value = evalGdbExpr(w.expression);

                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            m_state.isPaused = true;
                            m_state.pausedFile = pf;
                            m_state.pausedLine1Based = pl;
                            m_state.callStack = std::move(frames);
                            m_state.scopes = { { L"Locals & Args", 1, true, std::move(vars) } };
                            m_state.watches = std::move(wc);
                        }
                        m_paused.store(true);
                        if (onPaused) onPaused(pf, pl);
                        if (onStateChanged) onStateChanged();
                    } else if (ln.rfind("~\"", 0) == 0 && ln.size() >= 3) {
                        if (onOutput) onOutput(Utf8ToWide(UnescapeJsonStr(ln.substr(2, ln.size() - 3))));
                    }
                }
            }
        } else {
            if (WaitForSingleObject(m_hChildProc, 15) == WAIT_OBJECT_0) break;
        }
    }
}

// =========================================================================
// Backend 3: Node.js Inspect (--inspect-brk) CDP WebSocket Session
// =========================================================================

void DapClient::RunNodeCdpSession(const LaunchConfig& cfg) {
    int port = cfg.port > 0 ? cfg.port : PickFreeLoopbackPort();
    std::wstring nodeCmd = L"node.exe --inspect-brk=127.0.0.1:" + std::to_wstring(port) + L" \"" + cfg.program + L"\"";
    for (const auto& a : cfg.args) nodeCmd += L" \"" + a + L"\"";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    CreatePipe(&hOutRead, &hOutWrite, &sa, 0);
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hOutWrite;
    si.hStdError = hOutWrite;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, nodeCmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cfg.cwd.empty() ? nullptr : cfg.cwd.c_str(), &si, &pi);
    CloseHandle(hOutWrite);
    if (!ok) {
        CloseHandle(hOutRead);
        if (onOutput) onOutput(L"[Node CDP] Failed to launch node.exe\r\n");
        return;
    }
    m_hChildProc = pi.hProcess;
    m_hStdoutRead = hOutRead;
    CloseHandle(pi.hThread);

    // Wait briefly for Node's /json inspector endpoint and parse WebSocket target ID
    std::string wsPath = "/";
    for (int attempt = 0; attempt < 40 && !m_stopRequested.load(); ++attempt) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<u_short>(port));
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                std::string httpReq = "GET /json HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nConnection: close\r\n\r\n";
                send(s, httpReq.data(), static_cast<int>(httpReq.size()), 0);
                std::string resp;
                char tmp[2048];
                int r = 0;
                while ((r = recv(s, tmp, sizeof(tmp), 0)) > 0) resp.append(tmp, r);
                std::string wsUrl;
                if (ExtractJsonStringA(resp, "webSocketDebuggerUrl", wsUrl)) {
                    size_t sl = wsUrl.rfind('/');
                    if (sl != std::string::npos) wsPath = wsUrl.substr(sl);
                    closesocket(s);
                    break;
                }
            }
            closesocket(s);
        }
        Sleep(50);
    }

    SOCKET ws = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (ws == INVALID_SOCKET || connect(ws, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (ws != INVALID_SOCKET) closesocket(ws);
        if (onOutput) onOutput(L"[Node CDP] Could not attach WebSocket debugger.\r\n");
        return;
    }
    m_sockHandle = static_cast<uintptr_t>(ws);

    std::string hs = "GET " + wsPath + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                     "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    send(ws, hs.data(), static_cast<int>(hs.size()), 0);
    std::string hsResp;
    char hbuf[1024];
    int hr = recv(ws, hbuf, sizeof(hbuf), 0);
    if (hr > 0) hsResp.assign(hbuf, hr);

    auto sendWsText = [&](const std::string& json) {
        std::vector<unsigned char> frame;
        frame.push_back(0x81);
        size_t len = json.size();
        if (len <= 125) {
            frame.push_back(static_cast<unsigned char>(0x80 | len));
        } else {
            frame.push_back(0x80 | 126);
            frame.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
            frame.push_back(static_cast<unsigned char>(len & 0xFF));
        }
        const unsigned char mask[4] = { 0x12, 0x34, 0x56, 0x78 };
        frame.insert(frame.end(), mask, mask + 4);
        for (size_t i = 0; i < len; ++i) frame.push_back(static_cast<unsigned char>(json[i]) ^ mask[i & 3]);
        send(ws, reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()), 0);
    };

    int msgId = 1;
    sendWsText("{\"id\":" + std::to_string(msgId++) + ",\"method\":\"Debugger.enable\"}");
    sendWsText("{\"id\":" + std::to_string(msgId++) + ",\"method\":\"Runtime.enable\"}");
    {
        std::lock_guard<std::mutex> sl(m_mutex);
        for (const auto& bp : m_state.breakpoints) {
            if (!bp.enabled) continue;
            std::string u = "file:///" + EscapeJsonUtf8(WideToUtf8(bp.filePath));
            sendWsText("{\"id\":" + std::to_string(msgId++) +
                       ",\"method\":\"Debugger.setBreakpointByUrl\",\"params\":{\"lineNumber\":" +
                       std::to_string(bp.line1Based > 0 ? bp.line1Based - 1 : 0) + ",\"url\":\"" + u + "\"}}");
        }
        sendWsText("{\"id\":" + std::to_string(msgId++) + ",\"method\":\"Runtime.runIfWaitingForDebugger\"}");
    }

    u_long nonblock = 1;
    ioctlsocket(ws, FIONBIO, &nonblock);
    std::vector<unsigned char> rx;
    std::string activeCallFrameId;

    while (!m_stopRequested.load()) {
        // Drain control commands
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            while (!m_cmdQueue.empty()) {
                auto cmd = std::move(m_cmdQueue.front());
                m_cmdQueue.pop();
                if (cmd.cmd == "continue") { m_paused.store(false); sendWsText("{\"id\":" + std::to_string(msgId++) + ",\"method\":\"Debugger.resume\"}"); }
                else if (cmd.cmd == "next") { m_paused.store(false); sendWsText("{\"id\":" + std::to_string(msgId++) + ",\"method\":\"Debugger.stepOver\"}"); }
                else if (cmd.cmd == "step") { m_paused.store(false); sendWsText("{\"id\":" + std::to_string(msgId++) + ",\"method\":\"Debugger.stepInto\"}"); }
                else if (cmd.cmd == "step_out") { m_paused.store(false); sendWsText("{\"id\":" + std::to_string(msgId++) + ",\"method\":\"Debugger.stepOut\"}"); }
                else if (cmd.cmd == "eval" && !activeCallFrameId.empty()) {
                    sendWsText("{\"id\":" + std::to_string(msgId++) +
                               ",\"method\":\"Debugger.evaluateOnCallFrame\",\"params\":{\"callFrameId\":\"" +
                               activeCallFrameId + "\",\"expression\":\"" + EscapeJsonUtf8(WideToUtf8(cmd.arg)) + "\"}}");
                }
            }
        }

        // Pipe Node stdout to Debug Console
        DWORD avail = 0;
        if (PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char pbuf[2048];
            DWORD rd = 0;
            if (ReadFile(m_hStdoutRead, pbuf, sizeof(pbuf), &rd, nullptr) && rd > 0 && onOutput) {
                onOutput(Utf8ToWide(std::string(pbuf, rd)));
            }
        }

        unsigned char rbuf[4096];
        int n = recv(ws, reinterpret_cast<char*>(rbuf), sizeof(rbuf), 0);
        if (n > 0) {
            rx.insert(rx.end(), rbuf, rbuf + n);
            while (rx.size() >= 2) {
                uint64_t plen = rx[1] & 0x7F;
                size_t hdrLen = 2;
                if (plen == 126) {
                    if (rx.size() < 4) break;
                    plen = (static_cast<uint64_t>(rx[2]) << 8) | rx[3];
                    hdrLen = 4;
                } else if (plen == 127) {
                    if (rx.size() < 10) break;
                    plen = 0;
                    for (int k = 0; k < 8; ++k) plen = (plen << 8) | rx[2 + k];
                    hdrLen = 10;
                }
                if (rx.size() < hdrLen + plen) break;
                std::string msg(reinterpret_cast<const char*>(rx.data() + hdrLen), static_cast<size_t>(plen));
                rx.erase(rx.begin(), rx.begin() + hdrLen + static_cast<size_t>(plen));

                std::string method;
                if (ExtractJsonStringA(msg, "method", method)) {
                    if (method == "Debugger.paused") {
                        ExtractJsonStringA(msg, "callFrameId", activeCallFrameId);
                        int ln0 = 0;
                        ExtractJsonIntA(msg, "lineNumber", ln0);
                        std::string fn;
                        ExtractJsonStringA(msg, "functionName", fn);
                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            m_state.isPaused = true;
                            m_state.pausedFile = cfg.program;
                            m_state.pausedLine1Based = ln0 + 1;
                            DapStackFrame fr{ 0, Utf8ToWide(fn.empty() ? "<anonymous>" : fn), cfg.program, ln0 + 1, 1 };
                            m_state.callStack = { fr };
                        }
                        m_paused.store(true);
                        if (onPaused) onPaused(cfg.program, ln0 + 1);
                        if (onStateChanged) onStateChanged();
                    } else if (method == "Debugger.resumed") {
                        m_paused.store(false);
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.isPaused = false;
                        if (onContinued) onContinued();
                        if (onStateChanged) onStateChanged();
                    }
                } else if (msg.find("\"result\"") != std::string::npos && msg.find("\"value\"") != std::string::npos) {
                    std::string rv;
                    if (ExtractJsonStringA(msg, "description", rv) || ExtractJsonStringA(msg, "value", rv)) {
                        std::lock_guard<std::mutex> el(m_evalMutex);
                        m_evalResult = Utf8ToWide(rv);
                        m_evalReady = true;
                        m_evalCv.notify_all();
                    }
                }
            }
        } else if (n == 0) {
            break;
        }
        if (WaitForSingleObject(m_hChildProc, 15) == WAIT_OBJECT_0) break;
    }
}

// =========================================================================
// Backend 4: PHP Xdebug DBGp TCP Server Session
// =========================================================================

void DapClient::RunPhpDbGpSession(const LaunchConfig& cfg) {
    int port = cfg.port > 0 ? cfg.port : 9003;
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port));
    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listenSock, 1) != 0) {
        closesocket(listenSock);
        if (onOutput) onOutput(L"[PHP DBGp] Port " + std::to_wstring(port) + L" already in use.\r\n");
        return;
    }

    if (cfg.request != L"attach" && !cfg.program.empty()) {
        std::wstring phpExe = FindPhpExecutable();
        std::wstring phpCmd = L"\"" + phpExe + L"\" -dxdebug.mode=debug -dxdebug.start_with_request=yes -dxdebug.client_port=" +
                              std::to_wstring(port) + L" \"" + cfg.program + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, phpCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                           cfg.cwd.empty() ? nullptr : cfg.cwd.c_str(), &si, &pi)) {
            m_hChildProc = pi.hProcess;
            CloseHandle(pi.hThread);
        }
    }

    u_long nb = 1;
    ioctlsocket(listenSock, FIONBIO, &nb);
    SOCKET client = INVALID_SOCKET;
    while (!m_stopRequested.load()) {
        client = accept(listenSock, nullptr, nullptr);
        if (client != INVALID_SOCKET) break;
        if (m_hChildProc && WaitForSingleObject(m_hChildProc, 30) == WAIT_OBJECT_0) break;
        Sleep(25);
    }
    closesocket(listenSock);
    if (client == INVALID_SOCKET) return;
    m_sockHandle = static_cast<uintptr_t>(client);

    int txId = 1;
    auto sendDbgp = [&](const std::string& cmd) {
        std::string full = cmd + " -i " + std::to_string(txId++) + '\0';
        send(client, full.data(), static_cast<int>(full.size()), 0);
    };
    auto recvDbgpXml = [&]() -> std::string {
        std::string pkt;
        char buf[4096];
        int r = recv(client, buf, sizeof(buf), 0);
        if (r > 0) pkt.assign(buf, r);
        return pkt;
    };

    recvDbgpXml(); // initial <init .../>
    {
        std::lock_guard<std::mutex> sl(m_mutex);
        for (const auto& bp : m_state.breakpoints) {
            if (!bp.enabled) continue;
            sendDbgp("breakpoint_set -t line -f file:///" + WideToUtf8(bp.filePath) + " -n " + std::to_string(bp.line1Based));
            recvDbgpXml();
        }
    }
    sendDbgp("run");

    while (!m_stopRequested.load()) {
        std::string xml = recvDbgpXml();
        if (xml.empty()) break;
        if (xml.find("status=\"break\"") != std::string::npos) {
            sendDbgp("stack_get");
            std::string stackXml = recvDbgpXml();
            sendDbgp("context_get -d 0");
            std::string ctxXml = recvDbgpXml();

            int ln = 1;
            size_t lp = stackXml.find("lineno=\"");
            if (lp != std::string::npos) {
                try { ln = std::stoi(stackXml.substr(lp + 8)); } catch (...) {}
            }
            {
                std::lock_guard<std::mutex> sl(m_mutex);
                m_state.isPaused = true;
                m_state.pausedFile = cfg.program;
                m_state.pausedLine1Based = ln;
                m_state.callStack = { { 0, L"main", cfg.program, ln, 1 } };
                m_state.scopes = { { L"Locals", 1, true, {} } };
            }
            m_paused.store(true);
            if (onPaused) onPaused(cfg.program, ln);
            if (onStateChanged) onStateChanged();

            while (m_paused.load() && !m_stopRequested.load()) {
                std::unique_lock<std::mutex> lk(m_cmdMutex);
                m_cmdCv.wait_for(lk, std::chrono::milliseconds(40));
                while (!m_cmdQueue.empty()) {
                    auto cmd = std::move(m_cmdQueue.front());
                    m_cmdQueue.pop();
                    lk.unlock();
                    if (cmd.cmd == "continue") { m_paused.store(false); sendDbgp("run"); }
                    else if (cmd.cmd == "next") { m_paused.store(false); sendDbgp("step_over"); }
                    else if (cmd.cmd == "step") { m_paused.store(false); sendDbgp("step_into"); }
                    else if (cmd.cmd == "step_out") { m_paused.store(false); sendDbgp("step_out"); }
                    else if (cmd.cmd == "quit") { m_paused.store(false); sendDbgp("stop"); m_stopRequested.store(true); }
                    lk.lock();
                }
            }
        } else if (xml.find("status=\"stopped\"") != std::string::npos || xml.find("status=\"stopping\"") != std::string::npos) {
            break;
        }
    }
}

} // namespace phide::dev
