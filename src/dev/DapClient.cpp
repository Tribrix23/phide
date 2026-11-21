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
#include "phide/app/Logger.hpp"

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

size_t FindBalancedBraceEnd(const std::string& s, size_t openPos, char openCh = '{', char closeCh = '}') {
    if (openPos >= s.size() || s[openPos] != openCh) return std::string::npos;
    int depth = 0;
    bool inStr = false, esc = false;
    for (size_t i = openPos; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == openCh) ++depth;
            else if (c == closeCh && --depth == 0) return i;
        }
    }
    return std::string::npos;
}

bool ExtractJsonBoolA(const std::string& json, const std::string& key, bool& outVal, size_t startPos = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, startPos);
    if (pos == std::string::npos) return false;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
    if (json.compare(i, 4, "true") == 0) { outVal = true; return true; }
    if (json.compare(i, 5, "false") == 0) { outVal = false; return true; }
    return false;
}

bool ExtractJsonStringArrayA(const std::string& json, const std::string& arrayKey, std::vector<std::string>& outArr) {
    outArr.clear();
    std::string needle = "\"" + arrayKey + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    size_t b = json.find('[', colon + 1);
    if (b == std::string::npos) return false;
    size_t e = FindBalancedBraceEnd(json, b, '[', ']');
    if (e == std::string::npos) return false;
    size_t i = b + 1;
    while (i < e) {
        if (json[i] == '"') {
            std::string item;
            ++i;
            while (i < e) {
                if (json[i] == '\\' && i + 1 < e) {
                    item.push_back(json[i]);
                    item.push_back(json[i + 1]);
                    i += 2;
                    continue;
                }
                if (json[i] == '"') { ++i; break; }
                item.push_back(json[i++]);
            }
            outArr.push_back(UnescapeJsonStr(item));
        } else {
            ++i;
        }
    }
    return true;
}

std::string Base64EncodeA(const std::string& in) {
    static const char kTbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    for (size_t k = 0; k < in.size(); k += 3) {
        uint32_t octetA = static_cast<unsigned char>(in[k]);
        bool haveB = (k + 1 < in.size());
        bool haveC = (k + 2 < in.size());
        uint32_t octetB = haveB ? static_cast<unsigned char>(in[k + 1]) : 0;
        uint32_t octetC = haveC ? static_cast<unsigned char>(in[k + 2]) : 0;
        uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;
        out.push_back(kTbl[(triple >> 18) & 0x3F]);
        out.push_back(kTbl[(triple >> 12) & 0x3F]);
        out.push_back(haveB ? kTbl[(triple >> 6) & 0x3F] : '=');
        out.push_back(haveC ? kTbl[triple & 0x3F] : '=');
    }
    return out;
}

std::string Base64DecodeA(const std::string& in) {
    static const int kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::string out;
    uint32_t val = 0;
    int valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        int d = kDec[c];
        if (d < 0) continue;
        val = (val << 6) + static_cast<uint32_t>(d);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string DecodeXmlEntitiesA(std::string s) {
    auto rep = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    rep("&lt;", "<");
    rep("&gt;", ">");
    rep("&quot;", "\"");
    rep("&apos;", "'");
    rep("&amp;", "&");
    return s;
}

std::string ExtractXmlAttrA(const std::string& tag, const std::string& attrName) {
    auto search = [&](char quote) -> std::string {
        std::string needle = " " + attrName + "=" + quote;
        size_t pos = tag.find(needle);
        if (pos == std::string::npos) {
            needle = attrName + "=" + quote;
            pos = tag.find(needle);
        }
        if (pos == std::string::npos) return "";
        size_t vs = pos + needle.size();
        size_t ve = tag.find(quote, vs);
        if (ve == std::string::npos) return "";
        return DecodeXmlEntitiesA(tag.substr(vs, ve - vs));
    };
    std::string r = search('"');
    if (r.empty()) r = search('\'');
    return r;
}

std::wstring FileUrlToWindowsPathW(std::string s) {
    if (s.rfind("file:///", 0) == 0) s = s.substr(8);
    else if (s.rfind("file://", 0) == 0) s = s.substr(7);
    std::string decoded;
    decoded.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            int hex = std::stoi(s.substr(i + 1, 2), nullptr, 16);
            decoded.push_back(static_cast<char>(hex));
            i += 2;
        } else {
            decoded.push_back(s[i]);
        }
    }
    // Strip leading slash before Windows drive letter: /C:/foo -> C:/foo
    if (decoded.size() >= 3 && decoded[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(decoded[1])) && decoded[2] == ':') {
        decoded.erase(0, 1);
    }
    for (char& ch : decoded) if (ch == '/') ch = '\\';
    return Utf8ToWide(decoded);
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
import bdb, json, os, sys, threading, queue

cmd_q = queue.Queue()
wait_ev = threading.Event()
dbg_ref = [None]
paused_state = {
    "paused": False,
    "pause_req": False,
    "frame": None,
    "frames": {},
    "break_raised": False,
    "break_uncaught": True,
}

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
            elif cmd == "pause":
                paused_state["pause_req"] = True
                d = dbg_ref[0]
                if d is not None and not paused_state.get("paused"):
                    try:
                        d.set_step()
                    except Exception:
                        pass
            else:
                cmd_q.put(msg)
                wait_ev.set()
        except Exception:
            pass

def build_var(name, val, obj_map, obj_depths, depth=0):
    try:
        r = repr(val)
        if len(r) > 120:
            r = r[:117] + "..."
    except Exception:
        r = "<unrepr>"
    tname = type(val).__name__
    vref = 0
    if depth < 8 and len(obj_map) < 2000:
        if isinstance(val, (dict, list, tuple, set)):
            vref = len(obj_map) + 10
            obj_map[vref] = val
            obj_depths[vref] = depth + 1
        elif hasattr(val, "__dict__"):
            vref = len(obj_map) + 10
            obj_map[vref] = val.__dict__
            obj_depths[vref] = depth + 1
    return {"name": str(name), "value": r, "type": tname, "varRef": vref}

def extract_frame_vars(frame, obj_map, obj_depths):
    locals_list = []
    for k, v in list(frame.f_locals.items()):
        if str(k).startswith("__phide"):
            continue
        locals_list.append(build_var(k, v, obj_map, obj_depths, 0))
    globals_list = []
    for k, v in list(frame.f_globals.items()):
        if str(k).startswith("__") or callable(v) or type(v).__name__ == "module":
            continue
        globals_list.append(build_var(k, v, obj_map, obj_depths, 0))
    return locals_list[:60], globals_list[:40]

class PhideBdb(bdb.Bdb):
    def __init__(self, target_file):
        super().__init__()
        self.target_file = target_file
        self.first_stop = True
        self.stop_on_entry = False
        self.obj_map = {}
        self.obj_depths = {}

    def set_continue(self):
        # Keep trace attached on botframe so Pause works even with 0 breakpoints
        if hasattr(self, "_set_stopinfo"):
            self._set_stopinfo(self.botframe, None, -1)
        else:
            self.stopframe = self.botframe
            self.returnframe = None
            self.quitting = False
            self.stoplineno = -1

    def trace_dispatch(self, frame, event, arg):
        if paused_state.get("pause_req") and not paused_state.get("paused"):
            self.set_step()
        return super().trace_dispatch(frame, event, arg)

    def _emit_stop(self, frame, reason, exc_text=""):
        self.obj_map = {}
        self.obj_depths = {}
        paused_state["paused"] = True
        paused_state["pause_req"] = False
        paused_state["frame"] = frame
        paused_state["frames"] = {}
        bridge_abs = os.path.normcase(os.path.abspath(__file__))
        frames = []
        f = frame
        fid = 0
        while f is not None and len(frames) < 64:
            fn = os.path.abspath(f.f_code.co_filename)
            is_bridge = (os.path.normcase(fn) == bridge_abs) or os.path.basename(fn).startswith("phide_bdb_")
            if not is_bridge and ("string" not in fn or fn == self.target_file):
                paused_state["frames"][fid] = f
                frames.append({
                    "id": fid,
                    "name": f.f_code.co_name,
                    "file": fn,
                    "line": f.f_lineno
                })
                fid += 1
            f = f.f_back

        active_frame = paused_state["frames"].get(0, frame)
        stop_file = frames[0]["file"] if frames else self.target_file
        stop_line = frames[0]["line"] if frames else 1
        locals_list, globals_list = extract_frame_vars(active_frame, self.obj_map, self.obj_depths)

        loaded = []
        for mname, mobj in list(sys.modules.items())[:80]:
            mf = getattr(mobj, "__file__", None)
            if mf:
                af = os.path.abspath(mf)
                if os.path.normcase(af) != bridge_abs and not os.path.basename(af).startswith("phide_bdb_"):
                    loaded.append(af)

        emit({
            "event": "stopped",
            "reason": reason,
            "text": exc_text,
            "file": stop_file,
            "line": stop_line,
            "frames": frames,
            "locals": locals_list,
            "globals": globals_list,
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
                elif action == "select_frame":
                    fid_sel = int(c.get("frameId", 0))
                    sf = paused_state["frames"].get(fid_sel)
                    if sf is not None:
                        paused_state["frame"] = sf
                        locs, glbs = extract_frame_vars(sf, self.obj_map, self.obj_depths)
                        emit({"event": "frame_vars", "frameId": fid_sel, "locals": locs, "globals": glbs})
                elif action == "children":
                    vref = int(c.get("varRef", 0))
                    container = self.obj_map.get(vref)
                    cdepth = self.obj_depths.get(vref, 1)
                    ch = []
                    if isinstance(container, dict):
                        for k, v in list(container.items())[:50]:
                            ch.append(build_var(k, v, self.obj_map, self.obj_depths, cdepth))
                    elif isinstance(container, (list, tuple, set)):
                        for idx, v in enumerate(list(container)[:50]):
                            ch.append(build_var(f"[{idx}]", v, self.obj_map, self.obj_depths, cdepth))
                    emit({"event": "children", "varRef": vref, "children": ch})
                elif action == "quit":
                    self.set_quit()
                    return

    def user_line(self, frame):
        if paused_state.pop("pause_req", False):
            self._emit_stop(frame, "pause")
            return
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

cfg_data = {}
argv_rest = []
idx = 1
while idx < len(sys.argv):
    if sys.argv[idx] == "--phide-cfg" and idx + 1 < len(sys.argv):
        cfg_path = sys.argv[idx + 1]
        try:
            with open(cfg_path, "r", encoding="utf-8") as cf:
                cfg_data = json.load(cf)
            os.remove(cfg_path)
        except Exception:
            pass
        idx += 2
    else:
        argv_rest.append(sys.argv[idx])
        idx += 1

if not argv_rest:
    emit({"event": "terminated", "code": 1})
    sys.exit(1)

target = os.path.abspath(argv_rest[0])
stop_entry = bool(cfg_data.get("stop_on_entry", False))
init_bps = cfg_data.get("bps", [])
paused_state["break_raised"] = bool(cfg_data.get("break_raised", False))
paused_state["break_uncaught"] = bool(cfg_data.get("break_uncaught", True))

d = PhideBdb(target)
d.stop_on_entry = stop_entry
dbg_ref[0] = d
for bp in init_bps:
    try:
        d.set_break(bp["file"], int(bp["line"]), cond=bp.get("cond") or None)
    except Exception:
        pass

t = threading.Thread(target=reader_loop, daemon=True)
t.start()

sys.argv = argv_rest
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
    if paused_state.get("break_uncaught", True):
        bridge_abs = os.path.normcase(os.path.abspath(__file__))
        best_tb = None
        tb = ex.__traceback__
        while tb is not None:
            fn = os.path.abspath(tb.tb_frame.f_code.co_filename)
            if os.path.normcase(fn) != bridge_abs and not os.path.basename(fn).startswith("phide_bdb_"):
                best_tb = tb
            tb = tb.tb_next
        if best_tb is not None:
            d._emit_stop(best_tb.tb_frame, "exception", f"Uncaught {type(ex).__name__}: {ex}")
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
        if (ExtractJsonStringA(obj, "adapterCommand", s)) {
            cfg.adapterCommand = ReplaceMacrosW(Utf8ToWide(s), workspaceRoot, activeFilePath);
        }
        if (ExtractJsonStringA(obj, "adapter", s) && cfg.adapterCommand.empty()) {
            cfg.adapterCommand = ReplaceMacrosW(Utf8ToWide(s), workspaceRoot, activeFilePath);
        }
        {
            std::vector<std::string> aargs;
            if (ExtractJsonStringArrayA(obj, "adapterArgs", aargs)) {
                for (const auto& a : aargs) {
                    cfg.adapterArgs.push_back(ReplaceMacrosW(Utf8ToWide(a), workspaceRoot, activeFilePath));
                }
            }
        }
        int port = 0;
        if (ExtractJsonIntA(obj, "port", port)) cfg.port = port;
        cfg.stopOnEntry = (obj.find("\"stopOnEntry\": true") != std::string::npos ||
                           obj.find("\"stopOnEntry\":true") != std::string::npos);

        // Optional string args array
        std::vector<std::string> rawArgs;
        if (ExtractJsonStringArrayA(obj, "args", rawArgs)) {
            for (const auto& a : rawArgs) {
                cfg.args.push_back(ReplaceMacrosW(Utf8ToWide(a), workspaceRoot, activeFilePath));
            }
        }
        if (!cfg.name.empty() || !cfg.program.empty() || !cfg.adapterCommand.empty()) configs.push_back(std::move(cfg));
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
    while ((pos = miRecord.find("frame={", pos)) != std::string::npos && outFrames.size() < 64) {
        size_t endF = FindBalancedBraceEnd(miRecord, pos + 6, '{', '}');
        if (endF == std::string::npos) break;
        std::string seg = miRecord.substr(pos + 6, endF - (pos + 6) + 1);
        auto grab = [&](const std::string& key) -> std::string {
            std::string needle = key + "=\"";
            size_t kp = seg.find(needle);
            if (kp == std::string::npos) return "";
            size_t vs = kp + needle.size();
            size_t ve = vs;
            while (ve < seg.size()) {
                if (seg[ve] == '\\' && ve + 1 < seg.size()) { ve += 2; continue; }
                if (seg[ve] == '"') break;
                ++ve;
            }
            return UnescapeJsonStr(seg.substr(vs, ve - vs));
        };
        DapStackFrame frame;
        try { frame.id = std::stoi(grab("level")); } catch (...) { frame.id = static_cast<int>(outFrames.size()); }
        std::string fn = grab("func");
        frame.name = Utf8ToWide(fn.empty() ? "??" : fn);
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

namespace {

bool ExtractNestedObjectA(const std::string& json, const std::string& key, std::string& outObj, size_t startPos = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = startPos;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        size_t colon = json.find(':', pos + needle.size());
        if (colon != std::string::npos) {
            size_t brace = colon + 1;
            while (brace < json.size() && (json[brace] == ' ' || json[brace] == '\t' || json[brace] == '\r' || json[brace] == '\n')) ++brace;
            if (brace < json.size() && json[brace] == '{') {
                size_t endB = FindBalancedBraceEnd(json, brace, '{', '}');
                if (endB != std::string::npos) {
                    outObj = json.substr(brace, endB - brace + 1);
                    return true;
                }
            }
        }
        pos += needle.size();
    }
    return false;
}

std::string ExtractJsonPrimitiveA(const std::string& json, const std::string& key) {
    std::string strVal;
    if (ExtractJsonStringA(json, key, strVal)) return strVal;
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return "";
    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
    size_t j = i;
    while (j < json.size() && json[j] != ',' && json[j] != '}' && json[j] != ']' &&
           json[j] != '\r' && json[j] != '\n') ++j;
    std::string tok = json.substr(i, j - i);
    while (!tok.empty() && tok.back() == ' ') tok.pop_back();
    return tok;
}

std::string UnwrapXmlCdataAndDecode(std::string raw, const std::string& encoding) {
    // Trim leading/trailing whitespace
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '\r' || raw.front() == '\n')) raw.erase(0, 1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) raw.pop_back();
    const std::string cStart = "<![CDATA[";
    const std::string cEnd = "]]>";
    if (raw.rfind(cStart, 0) == 0 && raw.size() >= cStart.size() + cEnd.size()) {
        size_t ep = raw.rfind(cEnd);
        if (ep != std::string::npos && ep >= cStart.size()) {
            raw = raw.substr(cStart.size(), ep - cStart.size());
        }
    }
    if (encoding == "base64") return Base64DecodeA(raw);
    return DecodeXmlEntitiesA(raw);
}

} // namespace

bool DapClient::AssignVariableChildrenRecursive(
    std::vector<DapVariable>& vars,
    int targetVarRef,
    std::vector<DapVariable>&& children,
    int depth) {
    if (depth > 16 || targetVarRef <= 0) return false;
    for (auto& v : vars) {
        if (v.variablesReference == targetVarRef) {
            v.expanded = true;
            v.children = std::move(children);
            return true;
        }
        if (!v.children.empty()) {
            if (AssignVariableChildrenRecursive(v.children, targetVarRef, std::move(children), depth + 1)) {
                return true;
            }
        }
    }
    return false;
}

void DapClient::ParsePhpDbgpStack(
    const std::string& stackXml,
    const std::wstring& fallbackProgram,
    std::vector<DapStackFrame>& outFrames) {
    outFrames.clear();
    size_t pos = 0;
    while ((pos = stackXml.find("<stack", pos)) != std::string::npos && outFrames.size() < 64) {
        size_t endT = stackXml.find('>', pos);
        if (endT == std::string::npos) break;
        std::string tag = stackXml.substr(pos, endT - pos + 1);
        DapStackFrame fr;
        std::string lvl = ExtractXmlAttrA(tag, "level");
        try { fr.id = lvl.empty() ? static_cast<int>(outFrames.size()) : std::stoi(lvl); }
        catch (...) { fr.id = static_cast<int>(outFrames.size()); }
        std::string where = ExtractXmlAttrA(tag, "where");
        if (where.empty()) where = ExtractXmlAttrA(tag, "type");
        if (where.empty()) where = "main";
        fr.name = Utf8ToWide(where);
        std::string fname = ExtractXmlAttrA(tag, "filename");
        fr.sourcePath = !fname.empty() ? FileUrlToWindowsPathW(fname) : fallbackProgram;
        std::string lno = ExtractXmlAttrA(tag, "lineno");
        try { fr.line1Based = (std::max)(1, std::stoi(lno)); } catch (...) { fr.line1Based = 1; }
        outFrames.push_back(std::move(fr));
        pos = endT + 1;
    }
}

void DapClient::ParsePhpDbgpContextVars(
    const std::string& ctxXml,
    std::vector<DapVariable>& outVars,
    const std::function<int(const std::string&)>& allocVarRef) {
    outVars.clear();
    size_t pos = 0;
    int depth = 0;
    while (pos < ctxXml.size() && outVars.size() < 60) {
        size_t lt = ctxXml.find('<', pos);
        if (lt == std::string::npos) break;
        if (ctxXml.compare(lt, 9, "<![CDATA[") == 0) {
            size_t ce = ctxXml.find("]]>", lt + 9);
            pos = (ce != std::string::npos) ? (ce + 3) : ctxXml.size();
            continue;
        }
        if (ctxXml.compare(lt, 9, "<property") == 0 &&
            (lt + 9 < ctxXml.size() && (ctxXml[lt + 9] == ' ' || ctxXml[lt + 9] == '>' || ctxXml[lt + 9] == '/'))) {
            size_t gt = ctxXml.find('>', lt);
            if (gt == std::string::npos) break;
            bool selfClosing = (gt > lt && ctxXml[gt - 1] == '/');
            std::string tag = ctxXml.substr(lt, gt - lt + 1);
            if (depth == 0) {
                std::string name = ExtractXmlAttrA(tag, "name");
                std::string fullname = ExtractXmlAttrA(tag, "fullname");
                if (name.empty()) name = fullname;
                std::string type = ExtractXmlAttrA(tag, "type");
                std::string encoding = ExtractXmlAttrA(tag, "encoding");
                std::string childrenFlag = ExtractXmlAttrA(tag, "children");
                std::string numChildren = ExtractXmlAttrA(tag, "numchildren");
                std::string clsName = ExtractXmlAttrA(tag, "classname");

                std::string rawInner;
                if (!selfClosing) {
                    size_t nextTag = ctxXml.find('<', gt + 1);
                    if (nextTag != std::string::npos) {
                        if (ctxXml.compare(nextTag, 9, "<![CDATA[") == 0) {
                            size_t ce = ctxXml.find("]]>", nextTag + 9);
                            if (ce != std::string::npos) {
                                rawInner = ctxXml.substr(nextTag, (ce + 3) - nextTag);
                            }
                        } else if (nextTag > gt + 1) {
                            rawInner = ctxXml.substr(gt + 1, nextTag - (gt + 1));
                        }
                    }
                }
                std::string val = UnwrapXmlCdataAndDecode(rawInner, encoding);
                if (val.empty()) {
                    if (type == "null") val = "null";
                    else if (!numChildren.empty()) {
                        val = (!clsName.empty() ? (clsName + " ") : "") + type + "(" + numChildren + ")";
                    } else if (childrenFlag == "1") {
                        val = (!clsName.empty() ? clsName : type) + " {...}";
                    }
                }
                DapVariable v;
                v.name = Utf8ToWide(name);
                v.value = Utf8ToWide(val);
                v.type = Utf8ToWide(!clsName.empty() ? clsName : type);
                if ((childrenFlag == "1" || (!numChildren.empty() && numChildren != "0")) && allocVarRef) {
                    v.variablesReference = allocVarRef(fullname.empty() ? name : fullname);
                }
                if (!v.name.empty()) outVars.push_back(std::move(v));
            }
            if (!selfClosing) ++depth;
            pos = gt + 1;
        } else if (ctxXml.compare(lt, 11, "</property>") == 0) {
            if (depth > 0) --depth;
            pos = lt + 11;
        } else {
            pos = lt + 1;
        }
    }
}

std::wstring DapClient::DecodePhpDbgpEvalResult(const std::string& evalXml) {
    size_t errPos = evalXml.find("<error");
    if (errPos != std::string::npos) {
        size_t ms = evalXml.find("<message>", errPos);
        size_t me = (ms != std::string::npos) ? evalXml.find("</message>", ms) : std::string::npos;
        if (ms != std::string::npos && me != std::string::npos) {
            return L"<Error: " + Utf8ToWide(UnwrapXmlCdataAndDecode(evalXml.substr(ms + 9, me - (ms + 9)), "")) + L">";
        }
        return L"<Error>";
    }
    size_t pPos = evalXml.find("<property");
    if (pPos == std::string::npos) return L"";
    size_t gt = evalXml.find('>', pPos);
    if (gt == std::string::npos) return L"";
    std::string tag = evalXml.substr(pPos, gt - pPos + 1);
    std::string type = ExtractXmlAttrA(tag, "type");
    std::string enc = ExtractXmlAttrA(tag, "encoding");
    std::string numCh = ExtractXmlAttrA(tag, "numchildren");
    if (gt > pPos && evalXml[gt - 1] == '/') {
        if (type == "null") return L"null";
        return Utf8ToWide(type);
    }
    size_t endP = evalXml.find("</property>", gt + 1);
    if (endP == std::string::npos) return Utf8ToWide(type);
    std::string inner = evalXml.substr(gt + 1, endP - (gt + 1));
    // If nested properties, return summary
    size_t subP = inner.find("<property");
    if (subP != std::string::npos) inner = inner.substr(0, subP);
    std::string decoded = UnwrapXmlCdataAndDecode(inner, enc);
    if (decoded.empty() && !numCh.empty()) decoded = type + "(" + numCh + ")";
    if (decoded.empty() && type == "null") decoded = "null";
    return Utf8ToWide(decoded);
}

void DapClient::ParseNodeCdpFrames(
    const std::string& pausedMsgJson,
    const std::wstring& fallbackProgram,
    const std::vector<std::pair<std::string, std::wstring>>& scriptIdToPath,
    std::vector<DapStackFrame>& outFrames,
    std::vector<std::string>& outCallFrameIds,
    std::vector<std::string>& outScopeObjIds) {
    outFrames.clear();
    outCallFrameIds.clear();
    outScopeObjIds.clear();

    for (const auto& cfObj : SplitJsonObjectArrayA(pausedMsgJson, "callFrames")) {
        if (outFrames.size() >= 64) break;
        std::string cfId, fnName, cfUrl;
        ExtractJsonStringA(cfObj, "callFrameId", cfId);
        ExtractJsonStringA(cfObj, "functionName", fnName);
        ExtractJsonStringA(cfObj, "url", cfUrl);

        std::string locObj;
        std::string scriptId;
        int ln0 = 0;
        int col0 = 0;
        if (ExtractNestedObjectA(cfObj, "location", locObj)) {
            ExtractJsonStringA(locObj, "scriptId", scriptId);
            ExtractJsonIntA(locObj, "lineNumber", ln0);
            ExtractJsonIntA(locObj, "columnNumber", col0);
        }

        std::wstring resolvedPath;
        if (!scriptId.empty()) {
            for (const auto& kv : scriptIdToPath) {
                if (kv.first == scriptId && !kv.second.empty()) {
                    resolvedPath = kv.second;
                    break;
                }
            }
        }
        if (resolvedPath.empty() && !cfUrl.empty() && cfUrl.rfind("node:", 0) != 0) {
            resolvedPath = FileUrlToWindowsPathW(cfUrl);
        }
        if (resolvedPath.empty()) resolvedPath = fallbackProgram;

        DapStackFrame fr;
        fr.id = static_cast<int>(outFrames.size());
        fr.name = Utf8ToWide(fnName.empty() ? "<anonymous>" : fnName);
        fr.sourcePath = resolvedPath;
        fr.line1Based = (std::max)(1, ln0 + 1);
        fr.col1Based = (std::max)(1, col0 + 1);
        outFrames.push_back(std::move(fr));
        outCallFrameIds.push_back(std::move(cfId));

        std::string preferredObjId;
        std::string fallbackObjId;
        for (const auto& scObj : SplitJsonObjectArrayA(cfObj, "scopeChain")) {
            std::string scType;
            ExtractJsonStringA(scObj, "type", scType);
            std::string objWrap;
            std::string objId;
            if (ExtractNestedObjectA(scObj, "object", objWrap)) {
                ExtractJsonStringA(objWrap, "objectId", objId);
            }
            if (objId.empty()) continue;
            if (scType == "local" && preferredObjId.empty()) {
                preferredObjId = objId;
                break;
            }
            if ((scType == "closure" || scType == "block" || scType == "script") && fallbackObjId.empty()) {
                fallbackObjId = objId;
            }
        }
        outScopeObjIds.push_back(!preferredObjId.empty() ? preferredObjId : fallbackObjId);
    }
}

void DapClient::ParseNodeCdpProperties(
    const std::string& propsJson,
    std::vector<DapVariable>& outVars,
    const std::function<int(const std::string&)>& allocVarRef) {
    outVars.clear();
    for (const auto& item : SplitJsonObjectArrayA(propsJson, "result")) {
        if (outVars.size() >= 60) break;
        std::string name;
        if (!ExtractJsonStringA(item, "name", name) || name == "__proto__") continue;
        std::string valObj;
        if (!ExtractNestedObjectA(item, "value", valObj)) continue;

        std::string vType, vSubtype, vDesc, objId;
        ExtractJsonStringA(valObj, "type", vType);
        ExtractJsonStringA(valObj, "subtype", vSubtype);
        ExtractJsonStringA(valObj, "description", vDesc);
        ExtractJsonStringA(valObj, "objectId", objId);

        std::string disp;
        if (!vDesc.empty()) {
            disp = vDesc;
        } else {
            disp = ExtractJsonPrimitiveA(valObj, "value");
            if (disp.empty()) disp = (vType == "undefined") ? "undefined" : vType;
        }

        DapVariable v;
        v.name = Utf8ToWide(name);
        v.value = Utf8ToWide(disp);
        v.type = Utf8ToWide(!vSubtype.empty() ? vSubtype : vType);
        if (!objId.empty() && (vType == "object" || vType == "function") && allocVarRef) {
            v.variablesReference = allocVarRef(objId);
        }
        outVars.push_back(std::move(v));
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
    if (ext == L".js" || ext == L".mjs" || ext == L".cjs" || ext == L".ts" || ext == L".tsx" || ext == L".jsx") return DapAdapterType::NodeInspect;
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
    if (t == L"dap" || t == L"external" || t == L"external_dap" || t == L"external-dap" || t == L"custom") return DapAdapterType::ExternalDap;
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
    if (adapter == DapAdapterType::None && !cfg.adapterCommand.empty()) adapter = DapAdapterType::ExternalDap;
    if (adapter == DapAdapterType::None) adapter = DetectAdapterForFile(cfg.program);
    if (adapter == DapAdapterType::None) {
        PHIDE_LOG_ERROR(Server, L"DAP failed to resolve debug adapter for program: " + cfg.program);
        return false;
    }
    PHIDE_LOG_INFO(Server, L"DAP debug session starting (type=" + cfg.type + L", program=" + cfg.program + L")");

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
    m_childPid = 0;
    m_tempBridgeFile.clear();
    m_tempBridgeCfgFile.clear();
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
    m_evalCv.notify_all();
    if (m_sockHandle != static_cast<uintptr_t>(INVALID_SOCKET)) {
        closesocket(static_cast<SOCKET>(m_sockHandle));
        m_sockHandle = static_cast<uintptr_t>(INVALID_SOCKET);
    }
    if (m_hChildProc) {
        if (WaitForSingleObject(m_hChildProc, 220) == WAIT_TIMEOUT) {
            TerminateProcess(m_hChildProc, 0);
        }
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    if (m_hStdinWrite) { CloseHandle(m_hStdinWrite); m_hStdinWrite = nullptr; }
    if (m_hStdoutRead) { CloseHandle(m_hStdoutRead); m_hStdoutRead = nullptr; }
    if (m_hChildProc) { CloseHandle(m_hChildProc); m_hChildProc = nullptr; }
    m_childPid = 0;
    if (!m_tempBridgeFile.empty()) {
        std::error_code ec;
        fs::remove(fs::path(m_tempBridgeFile), ec);
        m_tempBridgeFile.clear();
    }
    if (!m_tempBridgeCfgFile.empty()) {
        std::error_code ec;
        fs::remove(fs::path(m_tempBridgeCfgFile), ec);
        m_tempBridgeCfgFile.clear();
    }
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
    std::function<void(const std::wstring&, int)> pausedCb;
    std::function<void()> stateCb;
    std::wstring matchedFile;
    int matchedLine = 0;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.selectedFrameId = frameId;
        for (const auto& f : m_state.callStack) {
            if (f.id == frameId) {
                m_state.pausedFile = f.sourcePath;
                m_state.pausedLine1Based = f.line1Based;
                matchedFile = f.sourcePath;
                matchedLine = f.line1Based;
                found = true;
                break;
            }
        }
        pausedCb = onPaused;
        stateCb = onStateChanged;
    }
    QueueControlCmd("select_frame", L"", frameId);
    if (found && pausedCb) pausedCb(matchedFile, matchedLine);
    if (stateCb) stateCb();
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
    std::lock_guard<std::mutex> callLock(m_evalCallMutex);
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
    } else if (adapter == DapAdapterType::ExternalDap) {
        RunExternalDapSession(cfg);
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
    static std::atomic<uint64_t> s_seq{1};
    uint64_t seq = s_seq.fetch_add(1);
    std::wstring suffix = std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(seq);
    fs::path tmpBridge = fs::temp_directory_path() / (L"phide_bdb_" + suffix + L".py");
    fs::path tmpCfg = fs::temp_directory_path() / (L"phide_bdb_cfg_" + suffix + L".json");
    m_tempBridgeFile = tmpBridge.wstring();
    m_tempBridgeCfgFile = tmpCfg.wstring();
    {
        std::ofstream ofs(tmpBridge, std::ios::binary);
        ofs << kPythonBdbScript;
    }

    // Pass initial breakpoints and exception flags via private temp JSON file (no process-wide env pollution)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream oss;
        oss << "{\"stop_on_entry\":" << (cfg.stopOnEntry ? "true" : "false")
            << ",\"break_raised\":" << (m_state.exceptions.breakOnRaised ? "true" : "false")
            << ",\"break_uncaught\":" << (m_state.exceptions.breakOnUncaught ? "true" : "false")
            << ",\"bps\":[";
        bool first = true;
        for (const auto& bp : m_state.breakpoints) {
            if (!bp.enabled) continue;
            if (!first) oss << ",";
            first = false;
            oss << "{\"file\":\"" << EscapeJsonUtf8(WideToUtf8(bp.filePath))
                << "\",\"line\":" << bp.line1Based
                << ",\"cond\":\"" << EscapeJsonUtf8(WideToUtf8(bp.condition)) << "\"}";
        }
        oss << "]}";
        std::ofstream cofs(tmpCfg, std::ios::binary);
        cofs << oss.str();
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
    std::wstring cmd = L"\"" + py + L"\" -u \"" + tmpBridge.wstring() + L"\" --phide-cfg \"" + tmpCfg.wstring() + L"\" \"" + cfg.program + L"\"";
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
        fs::remove(tmpCfg, ec);
        m_tempBridgeFile.clear();
        m_tempBridgeCfgFile.clear();
        if (onOutput) onOutput(L"[DAP] Failed to start Python debugger.\r\n");
        return;
    }

    m_hChildProc = pi.hProcess;
    m_childPid = pi.dwProcessId;
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
        while (GetTickCount() < deadline && !m_stopRequested.load()) {
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

    auto parsePyVars = [](const std::string& ln, const std::string& arrName) {
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
                        m_state.stoppedReason.clear();
                        m_state.stoppedText.clear();
                    }
                    if (onContinued) onContinued();
                    if (onStateChanged) onStateChanged();
                    WritePipeLine("{\"cmd\":\"" + msg.cmd + "\"}\n");
                } else if (msg.cmd == "pause") {
                    WritePipeLine("{\"cmd\":\"pause\"}\n");
                } else if (msg.cmd == "select_frame") {
                    WritePipeLine("{\"cmd\":\"select_frame\",\"frameId\":" + std::to_string(msg.intArg) + "}\n");
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
                        m_state.watches = std::move(copy);
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
                        oss << "{\"file\":\"" << EscapeJsonUtf8(WideToUtf8(bp.filePath))
                            << "\",\"line\":" << bp.line1Based
                            << ",\"cond\":\"" << EscapeJsonUtf8(WideToUtf8(bp.condition)) << "\"}";
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

                        DapScope loc{ L"Locals", 1, true, parsePyVars(ln, "locals") };
                        DapScope glb{ L"Globals", 2, false, parsePyVars(ln, "globals") };

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
                            m_state.selectedFrameId = frames.empty() ? 0 : frames[0].id;
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
                    } else if (ev == "frame_vars") {
                        DapScope loc{ L"Locals", 1, true, parsePyVars(ln, "locals") };
                        DapScope glb{ L"Globals", 2, false, parsePyVars(ln, "globals") };
                        std::vector<DapWatchItem> wc;
                        { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                        for (auto& w : wc) {
                            w.value = syncEvalBlocking(w.expression);
                            w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                        }
                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            m_state.scopes = { std::move(loc), std::move(glb) };
                            m_state.watches = std::move(wc);
                        }
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
                        std::vector<DapVariable> kids = parsePyVars(ln, "children");
                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            for (auto& sc : m_state.scopes) {
                                if (AssignVariableChildrenRecursive(sc.variables, vref, std::move(kids))) break;
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
    fs::remove(tmpCfg, ec);
    m_tempBridgeFile.clear();
    m_tempBridgeCfgFile.clear();
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
    m_childPid = pi.dwProcessId;
    m_hStdinWrite = hInWrite;
    m_hStdoutRead = hOutRead;
    CloseHandle(pi.hThread);

    // Enable async MI so -exec-interrupt can pause running inferior cleanly when supported
    WritePipeLine("-gdb-set mi-async on\n");

    auto queryMi = [&](const std::string& miCmd, const std::string& expectPrefix) -> std::string {
        WritePipeLine(miCmd + "\n");
        std::string acc;
        char c;
        DWORD nr = 0;
        DWORD deadline = GetTickCount() + 1200;
        while (GetTickCount() < deadline && !m_stopRequested.load()) {
            DWORD avail = 0;
            if (PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                if (ReadFile(m_hStdoutRead, &c, 1, &nr, nullptr) && nr > 0) {
                    if (c == '\n') {
                        if (!acc.empty() && acc.back() == '\r') acc.pop_back();
                        if (acc.rfind(expectPrefix, 0) == 0 || acc.rfind("^done", 0) == 0 || acc.rfind("^error", 0) == 0) {
                            return acc;
                        }
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
            size_t ve = vs;
            while (ve < resp.size()) {
                if (resp[ve] == '\\' && ve + 1 < resp.size()) { ve += 2; continue; }
                if (resp[ve] == '"') break;
                ++ve;
            }
            return Utf8ToWide(UnescapeJsonStr(resp.substr(vs, ve - vs)));
        }
        if (resp.rfind("^error,msg=\"", 0) == 0) {
            size_t ms = 12;
            size_t me = resp.rfind('"');
            if (me != std::string::npos && me > ms) return L"<Error: " + Utf8ToWide(resp.substr(ms, me - ms)) + L">";
        }
        return Utf8ToWide(resp);
    };

    std::vector<int> gdbBpNums;
    auto syncBreakpoints = [&]() {
        for (int bnum : gdbBpNums) {
            queryMi("-break-delete " + std::to_string(bnum), "^done");
        }
        gdbBpNums.clear();
        std::vector<DapBreakpoint> bps;
        { std::lock_guard<std::mutex> sl(m_mutex); bps = m_state.breakpoints; }
        for (const auto& bp : bps) {
            if (!bp.enabled) continue;
            std::wstring slPath = bp.filePath;
            for (auto& ch : slPath) if (ch == L'\\') ch = L'/';
            std::string rec = queryMi("-break-insert -f \"" + EscapeJsonUtf8(WideToUtf8(slPath)) + ":" + std::to_string(bp.line1Based) + "\"", "^done");
            size_t np = rec.find("number=\"");
            if (np != std::string::npos) {
                try { gdbBpNums.push_back(std::stoi(rec.substr(np + 8))); } catch (...) {}
            }
        }
    };

    std::vector<std::string> gdbVarNames;
    auto allocGdbVarRef = [&](const std::string& miVarName) -> int {
        if (gdbVarNames.size() >= 2000) return 0;
        gdbVarNames.push_back(miVarName);
        return 100 + static_cast<int>(gdbVarNames.size()) - 1;
    };
    auto clearGdbVarObjs = [&]() {
        for (const auto& vn : gdbVarNames) {
            if (vn.find('.') == std::string::npos) {
                WritePipeLine("-var-delete \"" + vn + "\"\n");
            }
        }
        gdbVarNames.clear();
    };
    auto attachVarObjs = [&](std::vector<DapVariable>& vars, int limit) {
        int created = 0;
        for (auto& v : vars) {
            if (created >= limit || v.name.empty()) break;
            std::string rec = queryMi("-var-create - * \"" + EscapeJsonUtf8(WideToUtf8(v.name)) + "\"", "^done,name=");
            if (rec.rfind("^done,", 0) != 0) continue;
            std::string vname, numChStr;
            size_t np = rec.find("name=\"");
            if (np != std::string::npos) {
                size_t ne = rec.find('"', np + 6);
                if (ne != std::string::npos) vname = rec.substr(np + 6, ne - (np + 6));
            }
            size_t cp = rec.find("numchild=\"");
            if (cp != std::string::npos) {
                size_t ce = rec.find('"', cp + 10);
                if (ce != std::string::npos) numChStr = rec.substr(cp + 10, ce - (cp + 10));
            }
            int numCh = 0;
            try { numCh = std::stoi(numChStr); } catch (...) {}
            if (!vname.empty() && numCh > 0) {
                v.variablesReference = allocGdbVarRef(vname);
                ++created;
            }
        }
    };

    auto parseMiVarChildren = [&](const std::string& rec) {
        std::vector<DapVariable> kids;
        size_t pos = 0;
        while ((pos = rec.find("child={", pos)) != std::string::npos && kids.size() < 50) {
            size_t endC = FindBalancedBraceEnd(rec, pos + 6, '{', '}');
            if (endC == std::string::npos) break;
            std::string seg = rec.substr(pos + 6, endC - (pos + 6) + 1);
            auto grab = [&](const std::string& k) -> std::string {
                std::string nd = k + "=\"";
                size_t kp = seg.find(nd);
                if (kp == std::string::npos) return "";
                size_t vs = kp + nd.size();
                size_t ve = vs;
                while (ve < seg.size()) {
                    if (seg[ve] == '\\' && ve + 1 < seg.size()) { ve += 2; continue; }
                    if (seg[ve] == '"') break;
                    ++ve;
                }
                return UnescapeJsonStr(seg.substr(vs, ve - vs));
            };
            DapVariable dv;
            std::string vname = grab("name");
            std::string expName = grab("exp");
            dv.name = Utf8ToWide(!expName.empty() ? expName : vname);
            dv.value = Utf8ToWide(grab("value"));
            dv.type = Utf8ToWide(grab("type"));
            int numCh = 0;
            try { numCh = std::stoi(grab("numchild")); } catch (...) {}
            if (numCh > 0 && !vname.empty()) {
                dv.variablesReference = allocGdbVarRef(vname);
            }
            kids.push_back(std::move(dv));
            pos = endC + 1;
        }
        return kids;
    };

    syncBreakpoints();
    if (cfg.stopOnEntry) WritePipeLine("-break-insert -t main\n");
    {
        std::lock_guard<std::mutex> sl(m_mutex);
        if (m_state.exceptions.breakOnRaised) WritePipeLine("catch throw\n");
    }
    WritePipeLine("-exec-run\n");

    std::string lineBuf;
    char buf[4096];
    while (!m_stopRequested.load()) {
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            while (!m_cmdQueue.empty()) {
                auto msg = std::move(m_cmdQueue.front());
                m_cmdQueue.pop();
                if (msg.cmd == "continue" || msg.cmd == "next" || msg.cmd == "step" || msg.cmd == "step_out") {
                    m_paused.store(false);
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.isPaused = false;
                        m_state.stoppedReason.clear();
                        m_state.stoppedText.clear();
                    }
                    if (onContinued) onContinued();
                    if (onStateChanged) onStateChanged();
                    if (msg.cmd == "continue") WritePipeLine("-exec-continue\n");
                    else if (msg.cmd == "next") WritePipeLine("-exec-next\n");
                    else if (msg.cmd == "step") WritePipeLine("-exec-step\n");
                    else if (msg.cmd == "step_out") WritePipeLine("-exec-finish\n");
                } else if (msg.cmd == "pause") {
                    WritePipeLine("-exec-interrupt\n");
                } else if (msg.cmd == "quit") {
                    WritePipeLine("-gdb-exit\n");
                } else if (msg.cmd == "set_bp") {
                    syncBreakpoints();
                } else if (msg.cmd == "set_exc") {
                    bool br = false;
                    { std::lock_guard<std::mutex> sl(m_mutex); br = m_state.exceptions.breakOnRaised; }
                    if (br) WritePipeLine("catch throw\n");
                } else if (msg.cmd == "select_frame") {
                    clearGdbVarObjs();
                    WritePipeLine("-stack-select-frame " + std::to_string(msg.intArg) + "\n");
                    std::string varsRec = queryMi("-stack-list-variables --all-values", "^done,variables=");
                    std::vector<DapVariable> vars;
                    ParseGdbMiVariables(varsRec, vars);
                    attachVarObjs(vars, 40);
                    std::vector<DapWatchItem> wc;
                    { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                    for (auto& w : wc) {
                        w.value = evalGdbExpr(w.expression);
                        w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                    }
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.scopes = { { L"Locals & Args", 1, true, std::move(vars) } };
                        m_state.watches = std::move(wc);
                    }
                    if (onStateChanged) onStateChanged();
                } else if (msg.cmd == "children") {
                    int idx = msg.intArg - 100;
                    if (idx >= 0 && idx < static_cast<int>(gdbVarNames.size())) {
                        std::string vname = gdbVarNames[idx];
                        std::string chRec = queryMi("-var-list-children --all-values \"" + EscapeJsonUtf8(vname) + "\"", "^done");
                        std::vector<DapVariable> kids = parseMiVarChildren(chRec);
                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            for (auto& sc : m_state.scopes) {
                                if (AssignVariableChildrenRecursive(sc.variables, msg.intArg, std::move(kids))) break;
                            }
                        }
                        if (onStateChanged) onStateChanged();
                    }
                } else if (msg.cmd == "eval") {
                    std::wstring res = evalGdbExpr(msg.arg);
                    std::lock_guard<std::mutex> el(m_evalMutex);
                    m_evalResult = res;
                    m_evalReady = true;
                    m_evalCv.notify_all();
                } else if (msg.cmd == "refresh_watches") {
                    std::vector<DapWatchItem> wc;
                    { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                    for (auto& w : wc) {
                        w.value = evalGdbExpr(w.expression);
                        w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                    }
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
                        std::string stackRec = queryMi("-stack-list-frames 0 40", "^done,stack=");
                        std::vector<DapStackFrame> frames;
                        ParseGdbMiFrames(stackRec, frames);

                        std::wstring pf = frames.empty() ? cfg.program : frames[0].sourcePath;
                        int pl = frames.empty() ? 1 : frames[0].line1Based;

                        bool isBp = (ln.find("breakpoint-hit") != std::string::npos);
                        if (isBp) {
                            bool shouldPause = true;
                            {
                                std::lock_guard<std::mutex> sl(m_mutex);
                                shouldPause = HandleBreakpointEvaluationLocked(pf, pl, evalGdbExpr);
                            }
                            if (!shouldPause) {
                                WritePipeLine("-exec-continue\n");
                                continue;
                            }
                        }

                        clearGdbVarObjs();
                        std::string varsRec = queryMi("-stack-list-variables --all-values", "^done,variables=");
                        std::vector<DapVariable> vars;
                        ParseGdbMiVariables(varsRec, vars);
                        attachVarObjs(vars, 40);

                        std::vector<DapWatchItem> wc;
                        { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                        for (auto& w : wc) {
                            w.value = evalGdbExpr(w.expression);
                            w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                        }

                        std::wstring reason = L"step";
                        if (isBp) reason = L"breakpoint";
                        else if (ln.find("signal-received") != std::string::npos) reason = L"pause";

                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            m_state.isPaused = true;
                            m_state.selectedFrameId = frames.empty() ? 0 : frames[0].id;
                            m_state.stoppedReason = reason;
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
    clearGdbVarObjs();
}

// =========================================================================
// Backend 3: Node.js Inspect (--inspect-brk) CDP WebSocket Session
// =========================================================================

void DapClient::RunNodeCdpSession(const LaunchConfig& cfg) {
    int port = cfg.port > 0 ? cfg.port : PickFreeLoopbackPort();
    std::wstring ext = ToLowerW(fs::path(cfg.program).extension().wstring());
    std::wstring extraFlags;
    if (ext == L".ts" || ext == L".mts" || ext == L".cts") {
        extraFlags = L" --experimental-strip-types";
    } else if ((ext == L".tsx" || ext == L".jsx") && !cfg.cwd.empty()) {
        std::error_code tec;
        fs::path tsxLoader = fs::path(cfg.cwd) / L"node_modules" / L"tsx" / L"dist" / L"esm" / L"index.mjs";
        if (!fs::exists(tsxLoader, tec)) {
            tsxLoader = fs::path(cfg.cwd) / L"node_modules" / L"tsx" / L"dist" / L"loader.mjs";
        }
        if (fs::exists(tsxLoader, tec)) {
            extraFlags = L" --import \"" + tsxLoader.wstring() + L"\"";
        } else if (onOutput) {
            onOutput(L"[Node CDP] Note: Node.js cannot execute raw JSX/TSX without a transpiler/loader (e.g. 'npm i -D tsx'). Attempting execution...\r\n");
        }
    }
    std::wstring nodeCmd = L"node.exe" + extraFlags + L" --inspect-brk=127.0.0.1:" + std::to_wstring(port) + L" \"" + cfg.program + L"\"";
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
    m_childPid = pi.dwProcessId;
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

    u_long nonblock = 1;
    ioctlsocket(ws, FIONBIO, &nonblock);

    auto sendWsFrame = [&](uint8_t opcode, const std::string& payload) {
        std::vector<unsigned char> frame;
        frame.reserve(payload.size() + 16);
        frame.push_back(static_cast<unsigned char>(0x80 | (opcode & 0x0F)));
        uint64_t len = payload.size();
        if (len <= 125) {
            frame.push_back(static_cast<unsigned char>(0x80 | len));
        } else if (len <= 65535) {
            frame.push_back(0x80 | 126);
            frame.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
            frame.push_back(static_cast<unsigned char>(len & 0xFF));
        } else {
            frame.push_back(0x80 | 127);
            for (int shift = 56; shift >= 0; shift -= 8) {
                frame.push_back(static_cast<unsigned char>((len >> shift) & 0xFF));
            }
        }
        uint32_t tick = GetTickCount();
        const unsigned char mask[4] = {
            static_cast<unsigned char>(0x37 ^ (tick & 0xFF)),
            static_cast<unsigned char>(0x9A ^ ((tick >> 8) & 0xFF)),
            0x5C, 0xE1
        };
        frame.insert(frame.end(), mask, mask + 4);
        for (size_t i = 0; i < payload.size(); ++i) {
            frame.push_back(static_cast<unsigned char>(payload[i]) ^ mask[i & 3]);
        }
        size_t sent = 0;
        while (sent < frame.size() && !m_stopRequested.load()) {
            int n = send(ws, reinterpret_cast<const char*>(frame.data() + sent), static_cast<int>(frame.size() - sent), 0);
            if (n > 0) sent += static_cast<size_t>(n);
            else if (WSAGetLastError() == WSAEWOULDBLOCK) Sleep(1);
            else break;
        }
    };

    std::vector<unsigned char> rx;
    std::string fragAccum;
    std::vector<std::string> queuedMessages;
    bool wsClosed = false;

    auto drainWsSocket = [&](int waitMs) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(ws, &rfds);
        timeval tv{ waitMs / 1000, (waitMs % 1000) * 1000 };
        if (select(0, &rfds, nullptr, nullptr, &tv) > 0) {
            unsigned char rbuf[8192];
            for (;;) {
                int n = recv(ws, reinterpret_cast<char*>(rbuf), sizeof(rbuf), 0);
                if (n > 0) {
                    rx.insert(rx.end(), rbuf, rbuf + n);
                    if (rx.size() > 8 * 1024 * 1024) break;
                } else if (n == 0) {
                    wsClosed = true;
                    break;
                } else {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) wsClosed = true;
                    break;
                }
            }
        }
        while (rx.size() >= 2) {
            bool fin = (rx[0] & 0x80) != 0;
            uint8_t opcode = rx[0] & 0x0F;
            bool masked = (rx[1] & 0x80) != 0;
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
            if (masked) hdrLen += 4;
            if (plen > 8 * 1024 * 1024) { rx.clear(); break; }
            if (rx.size() < hdrLen + plen) break;

            std::string msg(reinterpret_cast<const char*>(rx.data() + hdrLen), static_cast<size_t>(plen));
            if (masked) {
                const unsigned char* mk = rx.data() + (hdrLen - 4);
                for (size_t i = 0; i < msg.size(); ++i) msg[i] ^= mk[i & 3];
            }
            rx.erase(rx.begin(), rx.begin() + hdrLen + static_cast<size_t>(plen));

            if (opcode == 0x9) { // Ping -> Pong
                sendWsFrame(0xA, msg);
            } else if (opcode == 0x8) { // Close
                sendWsFrame(0x8, "");
                wsClosed = true;
            } else if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0) {
                if (fragAccum.size() + msg.size() <= 8 * 1024 * 1024) {
                    fragAccum.append(msg);
                }
                if (fin) {
                    queuedMessages.push_back(std::move(fragAccum));
                    fragAccum.clear();
                }
            }
        }
    };

    int msgId = 1;
    auto sendCdpAsync = [&](const std::string& method, const std::string& paramsJson = "") -> int {
        int id = msgId++;
        std::string payload = "{\"id\":" + std::to_string(id) + ",\"method\":\"" + method + "\"";
        if (!paramsJson.empty()) payload += ",\"params\":" + paramsJson;
        payload += "}";
        sendWsFrame(0x1, payload);
        return id;
    };

    auto sendCdpWait = [&](const std::string& method, const std::string& paramsJson, int timeoutMs = 1500) -> std::string {
        int wantId = sendCdpAsync(method, paramsJson);
        DWORD deadline = GetTickCount() + static_cast<DWORD>(timeoutMs);
        while (GetTickCount() < deadline && !m_stopRequested.load() && !wsClosed) {
            drainWsSocket(10);
            for (size_t i = 0; i < queuedMessages.size(); ++i) {
                int rid = -1;
                if (ExtractJsonIntA(queuedMessages[i], "id", rid) && rid == wantId) {
                    std::string out = std::move(queuedMessages[i]);
                    queuedMessages.erase(queuedMessages.begin() + i);
                    return out;
                }
            }
        }
        return "";
    };

    std::vector<std::pair<std::string, std::wstring>> scriptMap;
    std::vector<std::string> activeCdpBpIds;
    std::vector<std::string> frameCallIds;
    std::vector<std::string> frameScopeObjIds;
    std::vector<std::string> nodeObjIds; // varRef = 100 + idx
    std::string activeCallFrameId;

    auto allocNodeVarRef = [&](const std::string& objId) -> int {
        if (objId.empty() || nodeObjIds.size() >= 2000) return 0;
        nodeObjIds.push_back(objId);
        return 100 + static_cast<int>(nodeObjIds.size()) - 1;
    };

    auto syncNodeBreakpoints = [&]() {
        for (const auto& bid : activeCdpBpIds) {
            sendCdpAsync("Debugger.removeBreakpoint", "{\"breakpointId\":\"" + EscapeJsonUtf8(bid) + "\"}");
        }
        activeCdpBpIds.clear();
        std::vector<DapBreakpoint> bps;
        { std::lock_guard<std::mutex> sl(m_mutex); bps = m_state.breakpoints; }
        for (const auto& bp : bps) {
            if (!bp.enabled) continue;
            std::wstring slPath = bp.filePath;
            for (auto& ch : slPath) if (ch == L'\\') ch = L'/';
            std::string u = "file:///" + EscapeJsonUtf8(WideToUtf8(slPath));
            std::string resp = sendCdpWait(
                "Debugger.setBreakpointByUrl",
                "{\"lineNumber\":" + std::to_string(bp.line1Based > 0 ? bp.line1Based - 1 : 0) +
                ",\"url\":\"" + u + "\"}",
                800);
            std::string bid;
            if (ExtractJsonStringA(resp, "breakpointId", bid) && !bid.empty()) {
                activeCdpBpIds.push_back(std::move(bid));
            }
        }
    };

    auto syncNodeExceptions = [&]() {
        DapExceptionOptions ex;
        { std::lock_guard<std::mutex> sl(m_mutex); ex = m_state.exceptions; }
        std::string mode = ex.breakOnRaised ? "all" : (ex.breakOnUncaught ? "uncaught" : "none");
        sendCdpAsync("Debugger.setPauseOnExceptions", "{\"state\":\"" + mode + "\"}");
    };

    auto evalNodeExpr = [&](const std::wstring& expr) -> std::wstring {
        if (activeCallFrameId.empty()) return L"<not paused>";
        std::string resp = sendCdpWait(
            "Debugger.evaluateOnCallFrame",
            "{\"callFrameId\":\"" + EscapeJsonUtf8(activeCallFrameId) +
            "\",\"expression\":\"" + EscapeJsonUtf8(WideToUtf8(expr)) +
            "\",\"returnByValue\":false,\"generatePreview\":true}",
            1500);
        std::string resWrap;
        if (ExtractNestedObjectA(resp, "exceptionDetails", resWrap)) {
            std::string errTxt;
            ExtractJsonStringA(resWrap, "text", errTxt);
            return L"<Error: " + Utf8ToWide(errTxt.empty() ? "eval exception" : errTxt) + L">";
        }
        std::string valObj;
        if (ExtractNestedObjectA(resp, "result", valObj)) {
            std::string desc;
            if (ExtractJsonStringA(valObj, "description", desc)) return Utf8ToWide(desc);
            std::string prim = ExtractJsonPrimitiveA(valObj, "value");
            if (!prim.empty()) return Utf8ToWide(prim);
            std::string t;
            ExtractJsonStringA(valObj, "type", t);
            return Utf8ToWide(t);
        }
        return L"";
    };

    auto fetchScopeVars = [&](const std::string& scopeObjId) {
        std::vector<DapVariable> vars;
        if (scopeObjId.empty()) return vars;
        std::string resp = sendCdpWait(
            "Runtime.getProperties",
            "{\"objectId\":\"" + EscapeJsonUtf8(scopeObjId) +
            "\",\"ownProperties\":true,\"generatePreview\":true}",
            1500);
        std::string bodyObj;
        if (!ExtractNestedObjectA(resp, "result", bodyObj)) bodyObj = resp;
        ParseNodeCdpProperties(bodyObj, vars, allocNodeVarRef);
        return vars;
    };

    sendCdpAsync("Debugger.enable");
    sendCdpAsync("Runtime.enable");
    syncNodeExceptions();
    syncNodeBreakpoints();
    sendCdpAsync("Runtime.runIfWaitingForDebugger");

    // Drain Node stdout/stderr pipes cleanly without dropping buffered chunks
    auto drainNodePipe = [&]() {
        DWORD avail = 0;
        while (m_hStdoutRead && PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char pbuf[2048];
            DWORD rd = 0;
            if (ReadFile(m_hStdoutRead, pbuf, sizeof(pbuf), &rd, nullptr) && rd > 0 && onOutput) {
                onOutput(Utf8ToWide(std::string(pbuf, rd)));
            } else {
                break;
            }
        }
    };

    bool firstBreakHandled = false;
    while (!m_stopRequested.load() && !wsClosed) {
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            while (!m_cmdQueue.empty()) {
                auto cmd = std::move(m_cmdQueue.front());
                m_cmdQueue.pop();
                if (cmd.cmd == "continue") { m_paused.store(false); sendCdpAsync("Debugger.resume"); }
                else if (cmd.cmd == "pause") { sendCdpAsync("Debugger.pause"); }
                else if (cmd.cmd == "next") { m_paused.store(false); sendCdpAsync("Debugger.stepOver"); }
                else if (cmd.cmd == "step") { m_paused.store(false); sendCdpAsync("Debugger.stepInto"); }
                else if (cmd.cmd == "step_out") { m_paused.store(false); sendCdpAsync("Debugger.stepOut"); }
                else if (cmd.cmd == "set_bp") { syncNodeBreakpoints(); }
                else if (cmd.cmd == "set_exc") { syncNodeExceptions(); }
                else if (cmd.cmd == "select_frame") {
                    if (cmd.intArg >= 0 && cmd.intArg < static_cast<int>(frameCallIds.size())) {
                        activeCallFrameId = frameCallIds[cmd.intArg];
                        nodeObjIds.clear();
                        std::string sobj = (cmd.intArg < static_cast<int>(frameScopeObjIds.size()))
                            ? frameScopeObjIds[cmd.intArg] : "";
                        std::vector<DapVariable> vars = fetchScopeVars(sobj);
                        std::vector<DapWatchItem> wc;
                        { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                        for (auto& w : wc) {
                            w.value = evalNodeExpr(w.expression);
                            w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                        }
                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            m_state.scopes = { { L"Local", 1, true, std::move(vars) } };
                            m_state.watches = std::move(wc);
                        }
                        if (onStateChanged) onStateChanged();
                    }
                } else if (cmd.cmd == "children") {
                    int idx = cmd.intArg - 100;
                    if (idx >= 0 && idx < static_cast<int>(nodeObjIds.size())) {
                        std::vector<DapVariable> kids = fetchScopeVars(nodeObjIds[idx]);
                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            for (auto& sc : m_state.scopes) {
                                if (AssignVariableChildrenRecursive(sc.variables, cmd.intArg, std::move(kids))) break;
                            }
                        }
                        if (onStateChanged) onStateChanged();
                    }
                } else if (cmd.cmd == "refresh_watches") {
                    std::vector<DapWatchItem> wc;
                    { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                    for (auto& w : wc) {
                        w.value = evalNodeExpr(w.expression);
                        w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                    }
                    { std::lock_guard<std::mutex> sl(m_mutex); m_state.watches = std::move(wc); }
                    if (onStateChanged) onStateChanged();
                } else if (cmd.cmd == "eval") {
                    std::wstring res = evalNodeExpr(cmd.arg);
                    std::lock_guard<std::mutex> el(m_evalMutex);
                    m_evalResult = res;
                    m_evalReady = true;
                    m_evalCv.notify_all();
                }
            }
        }

        drainNodePipe();

        bool execContextEnded = false;
        drainWsSocket(15);
        while (!queuedMessages.empty()) {
            std::string msg = std::move(queuedMessages.front());
            queuedMessages.erase(queuedMessages.begin());

            std::string method;
            if (!ExtractJsonStringA(msg, "method", method)) continue;
            if (method == "Runtime.consoleAPICalled") {
                std::string ctype;
                ExtractJsonStringA(msg, "type", ctype);
                std::string argVal;
                if (!ExtractJsonStringA(msg, "value", argVal)) {
                    ExtractJsonStringA(msg, "description", argVal);
                }
                if (argVal.empty()) {
                    argVal = ExtractJsonPrimitiveA(msg, "value");
                }
                if (!argVal.empty() && onOutput) {
                    std::wstring prefix = (ctype == "error") ? L"[Error] " : ((ctype == "warn" || ctype == "warning") ? L"[Warn] " : L"");
                    onOutput(prefix + Utf8ToWide(argVal) + L"\r\n");
                }
            } else if (method == "Runtime.exceptionThrown") {
                std::string exWrap, exObj, desc, exText, exUrl;
                int exLine0 = -1;
                ExtractNestedObjectA(msg, "exceptionDetails", exWrap);
                ExtractJsonStringA(exWrap.empty() ? msg : exWrap, "text", exText);
                ExtractJsonStringA(exWrap.empty() ? msg : exWrap, "url", exUrl);
                ExtractJsonIntA(exWrap.empty() ? msg : exWrap, "lineNumber", exLine0);
                if (ExtractNestedObjectA(exWrap.empty() ? msg : exWrap, "exception", exObj)) {
                    ExtractJsonStringA(exObj, "description", desc);
                    if (desc.empty()) ExtractJsonStringA(exObj, "value", desc);
                }
                std::wstring errLine = L"Uncaught Exception";
                if (!desc.empty()) errLine = Utf8ToWide(desc);
                else if (!exText.empty()) errLine = Utf8ToWide(exText);
                if (!exUrl.empty() && exLine0 >= 0) {
                    std::wstring loc = FileUrlToWindowsPathW(exUrl);
                    errLine += L" (" + fs::path(loc).filename().wstring() + L":" + std::to_wstring(exLine0 + 1) + L")";
                }
                if (onOutput) onOutput(errLine + L"\r\n");
            } else if (method == "Runtime.executionContextDestroyed") {
                int ctxId = 0;
                if (!ExtractJsonIntA(msg, "executionContextId", ctxId) || ctxId <= 1) {
                    execContextEnded = true;
                }
            } else if (method == "Debugger.scriptParsed") {
                std::string sid, surl;
                ExtractJsonStringA(msg, "scriptId", sid);
                ExtractJsonStringA(msg, "url", surl);
                if (!sid.empty() && !surl.empty() && surl.rfind("node:", 0) != 0) {
                    std::wstring wpath = FileUrlToWindowsPathW(surl);
                    bool known = false;
                    for (auto& kv : scriptMap) {
                        if (kv.first == sid) { kv.second = wpath; known = true; break; }
                    }
                    if (!known && scriptMap.size() < 200) scriptMap.emplace_back(sid, wpath);
                    std::lock_guard<std::mutex> sl(m_mutex);
                    if (m_state.loadedScripts.size() < 200 &&
                        std::find(m_state.loadedScripts.begin(), m_state.loadedScripts.end(), wpath) == m_state.loadedScripts.end()) {
                        m_state.loadedScripts.push_back(wpath);
                    }
                }
            } else if (method == "Debugger.paused") {
                if (!firstBreakHandled) {
                    firstBreakHandled = true;
                    if (!cfg.stopOnEntry) {
                        sendCdpAsync("Debugger.resume");
                        continue;
                    }
                }
                std::vector<DapStackFrame> frames;
                ParseNodeCdpFrames(msg, cfg.program, scriptMap, frames, frameCallIds, frameScopeObjIds);
                activeCallFrameId = frameCallIds.empty() ? "" : frameCallIds[0];
                std::wstring pf = frames.empty() ? cfg.program : frames[0].sourcePath;
                int pl = frames.empty() ? 1 : frames[0].line1Based;

                std::string reason;
                ExtractJsonStringA(msg, "reason", reason);
                {
                    bool shouldPause = true;
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        shouldPause = HandleBreakpointEvaluationLocked(pf, pl, evalNodeExpr);
                    }
                    if (!shouldPause) {
                        sendCdpAsync("Debugger.resume");
                        continue;
                    }
                }

                nodeObjIds.clear();
                std::string topScope = frameScopeObjIds.empty() ? "" : frameScopeObjIds[0];
                std::vector<DapVariable> locVars = fetchScopeVars(topScope);
                std::vector<DapWatchItem> wc;
                { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                for (auto& w : wc) {
                    w.value = evalNodeExpr(w.expression);
                    w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                }
                {
                    std::lock_guard<std::mutex> sl(m_mutex);
                    m_state.isPaused = true;
                    m_state.selectedFrameId = frames.empty() ? 0 : frames[0].id;
                    m_state.stoppedReason = Utf8ToWide(reason.empty() ? "breakpoint" : reason);
                    m_state.pausedFile = pf;
                    m_state.pausedLine1Based = pl;
                    m_state.callStack = std::move(frames);
                    m_state.scopes = { { L"Local", 1, true, std::move(locVars) } };
                    m_state.watches = std::move(wc);
                }
                m_paused.store(true);
                if (onPaused) onPaused(pf, pl);
                if (onStateChanged) onStateChanged();
            } else if (method == "Debugger.resumed") {
                m_paused.store(false);
                {
                    std::lock_guard<std::mutex> sl(m_mutex);
                    m_state.isPaused = false;
                    m_state.stoppedReason.clear();
                    m_state.stoppedText.clear();
                }
                if (onContinued) onContinued();
                if (onStateChanged) onStateChanged();
            }
        }
        if (execContextEnded) {
            // Detach inspector cleanly so Node exits instead of hanging at "Waiting for debugger to disconnect"
            sendWsFrame(0x8, "");
            closesocket(ws);
            m_sockHandle = static_cast<uintptr_t>(INVALID_SOCKET);
            WaitForSingleObject(m_hChildProc, 600);
            drainNodePipe();
            break;
        }
        if (WaitForSingleObject(m_hChildProc, 5) == WAIT_OBJECT_0) {
            drainNodePipe();
            break;
        }
    }
    if (m_hStdoutRead) drainNodePipe();
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
            m_childPid = pi.dwProcessId;
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
    ioctlsocket(client, FIONBIO, &nb);

    int txId = 1;
    std::string rx;
    std::vector<std::string> pktQueue;
    bool sockClosed = false;

    auto sendDbgpRaw = [&](const std::string& cmd) -> int {
        int id = txId++;
        std::string full = cmd + " -i " + std::to_string(id) + '\0';
        size_t sent = 0;
        while (sent < full.size() && !m_stopRequested.load()) {
            int n = send(client, full.data() + sent, static_cast<int>(full.size() - sent), 0);
            if (n > 0) sent += static_cast<size_t>(n);
            else if (WSAGetLastError() == WSAEWOULDBLOCK) Sleep(1);
            else break;
        }
        return id;
    };

    auto pumpDbgp = [&](int waitMs) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client, &rfds);
        timeval tv{ waitMs / 1000, (waitMs % 1000) * 1000 };
        if (select(0, &rfds, nullptr, nullptr, &tv) > 0) {
            char buf[8192];
            for (;;) {
                int r = recv(client, buf, sizeof(buf), 0);
                if (r > 0) {
                    if (rx.size() + static_cast<size_t>(r) <= 8 * 1024 * 1024) {
                        rx.append(buf, r);
                    }
                } else if (r == 0) {
                    sockClosed = true;
                    break;
                } else {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) sockClosed = true;
                    break;
                }
            }
        }
        // Parse length\0xml\0 packets cleanly
        for (;;) {
            size_t nul1 = rx.find('\0');
            if (nul1 == std::string::npos) break;
            std::string lenStr = rx.substr(0, nul1);
            size_t pktLen = 0;
            bool isDigits = !lenStr.empty();
            for (char c : lenStr) {
                if (c < '0' || c > '9') { isDigits = false; break; }
            }
            if (!isDigits) {
                // Fallback if non-framed raw XML arrived
                pktQueue.push_back(lenStr);
                rx.erase(0, nul1 + 1);
                continue;
            }
            try { pktLen = static_cast<size_t>(std::stoull(lenStr)); } catch (...) { rx.erase(0, nul1 + 1); continue; }
            if (pktLen > 8 * 1024 * 1024) { rx.clear(); break; }
            size_t totalNeeded = nul1 + 1 + pktLen + 1;
            if (rx.size() < totalNeeded) break;
            pktQueue.push_back(rx.substr(nul1 + 1, pktLen));
            rx.erase(0, totalNeeded);
        }
    };

    auto sendDbgpWait = [&](const std::string& cmd, int timeoutMs = 1500) -> std::string {
        int wantTx = sendDbgpRaw(cmd);
        std::string needle1 = "transaction_id=\"" + std::to_string(wantTx) + "\"";
        std::string needle2 = "transaction_id='" + std::to_string(wantTx) + "'";
        DWORD deadline = GetTickCount() + static_cast<DWORD>(timeoutMs);
        while (GetTickCount() < deadline && !m_stopRequested.load() && !sockClosed) {
            pumpDbgp(10);
            for (size_t i = 0; i < pktQueue.size(); ++i) {
                if (pktQueue[i].find(needle1) != std::string::npos || pktQueue[i].find(needle2) != std::string::npos) {
                    std::string out = std::move(pktQueue[i]);
                    pktQueue.erase(pktQueue.begin() + i);
                    return out;
                }
            }
        }
        return "";
    };

    // Wait briefly for initial <init .../> packet
    for (int i = 0; i < 40 && pktQueue.empty() && !m_stopRequested.load() && !sockClosed; ++i) {
        pumpDbgp(25);
    }
    if (!pktQueue.empty()) pktQueue.erase(pktQueue.begin());

    std::vector<std::string> activeBpIds;
    std::vector<std::string> phpVarNames; // varRef = 100 + idx
    int activeFrameDepth = 0;

    auto allocPhpVarRef = [&](const std::string& fullname) -> int {
        if (fullname.empty() || phpVarNames.size() >= 2000) return 0;
        phpVarNames.push_back(fullname);
        return 100 + static_cast<int>(phpVarNames.size()) - 1;
    };

    auto syncPhpBreakpoints = [&]() {
        for (const auto& bid : activeBpIds) {
            sendDbgpWait("breakpoint_remove -d " + bid, 600);
        }
        activeBpIds.clear();
        std::vector<DapBreakpoint> bps;
        { std::lock_guard<std::mutex> sl(m_mutex); bps = m_state.breakpoints; }
        for (const auto& bp : bps) {
            if (!bp.enabled) continue;
            std::wstring slPath = bp.filePath;
            for (auto& ch : slPath) if (ch == L'\\') ch = L'/';
            std::string resp = sendDbgpWait(
                "breakpoint_set -t line -f file:///" + WideToUtf8(slPath) + " -n " + std::to_string(bp.line1Based),
                800);
            std::string bid = ExtractXmlAttrA(resp, "id");
            if (!bid.empty()) activeBpIds.push_back(std::move(bid));
        }
    };

    auto evalPhpExpr = [&](const std::wstring& expr) -> std::wstring {
        if (expr.empty()) return L"";
        std::string b64 = Base64EncodeA(WideToUtf8(expr));
        std::string resp = sendDbgpWait("eval -d " + std::to_string(activeFrameDepth) + " -- " + b64, 1500);
        return DecodePhpDbgpEvalResult(resp);
    };

    auto loadPhpScopes = [&](int depth) {
        phpVarNames.clear();
        std::string locXml = sendDbgpWait("context_get -d " + std::to_string(depth) + " -c 0", 1200);
        std::string glbXml = sendDbgpWait("context_get -d " + std::to_string(depth) + " -c 1", 1200);
        std::vector<DapVariable> locs, glbs;
        ParsePhpDbgpContextVars(locXml, locs, allocPhpVarRef);
        ParsePhpDbgpContextVars(glbXml, glbs, allocPhpVarRef);
        return std::vector<DapScope>{
            { L"Locals", 1, true, std::move(locs) },
            { L"Superglobals", 2, false, std::move(glbs) }
        };
    };

    syncPhpBreakpoints();
    if (cfg.stopOnEntry) {
        // DBGp starts paused before first line; step_into enters line 1 immediately
        sendDbgpRaw("step_into");
    } else {
        sendDbgpRaw("run");
    }

    while (!m_stopRequested.load() && !sockClosed) {
        // Drain control commands whether paused OR running (allows live Pause and dynamic breakpoints)
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            while (!m_cmdQueue.empty()) {
                auto cmd = std::move(m_cmdQueue.front());
                m_cmdQueue.pop();
                if (cmd.cmd == "continue" || cmd.cmd == "next" || cmd.cmd == "step" || cmd.cmd == "step_out") {
                    m_paused.store(false);
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.isPaused = false;
                        m_state.stoppedReason.clear();
                        m_state.stoppedText.clear();
                    }
                    if (onContinued) onContinued();
                    if (onStateChanged) onStateChanged();
                    if (cmd.cmd == "continue") sendDbgpRaw("run");
                    else if (cmd.cmd == "next") sendDbgpRaw("step_over");
                    else if (cmd.cmd == "step") sendDbgpRaw("step_into");
                    else if (cmd.cmd == "step_out") sendDbgpRaw("step_out");
                } else if (cmd.cmd == "pause") {
                    sendDbgpRaw("break");
                } else if (cmd.cmd == "quit") {
                    m_paused.store(false);
                    sendDbgpRaw("stop");
                    m_stopRequested.store(true);
                } else if (cmd.cmd == "set_bp") {
                    syncPhpBreakpoints();
                } else if (cmd.cmd == "select_frame") {
                    activeFrameDepth = (std::max)(0, cmd.intArg);
                    std::vector<DapScope> scopes = loadPhpScopes(activeFrameDepth);
                    std::vector<DapWatchItem> wc;
                    { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                    for (auto& w : wc) {
                        w.value = evalPhpExpr(w.expression);
                        w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                    }
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.scopes = std::move(scopes);
                        m_state.watches = std::move(wc);
                    }
                    if (onStateChanged) onStateChanged();
                } else if (cmd.cmd == "children") {
                    int idx = cmd.intArg - 100;
                    if (idx >= 0 && idx < static_cast<int>(phpVarNames.size())) {
                        std::string resp = sendDbgpWait(
                            "property_get -d " + std::to_string(activeFrameDepth) + " -n " + phpVarNames[idx], 1200);
                        // Unwrap outer <property> element so child <property> elements sit at depth 0
                        std::string innerXml = resp;
                        size_t pOpen = resp.find("<property");
                        if (pOpen != std::string::npos) {
                            size_t gt = resp.find('>', pOpen);
                            size_t pClose = resp.rfind("</property>");
                            if (gt != std::string::npos && pClose != std::string::npos && pClose > gt) {
                                innerXml = resp.substr(gt + 1, pClose - (gt + 1));
                            }
                        }
                        std::vector<DapVariable> kids;
                        ParsePhpDbgpContextVars(innerXml, kids, allocPhpVarRef);
                        {
                            std::lock_guard<std::mutex> sl(m_mutex);
                            for (auto& sc : m_state.scopes) {
                                if (AssignVariableChildrenRecursive(sc.variables, cmd.intArg, std::move(kids))) break;
                            }
                        }
                        if (onStateChanged) onStateChanged();
                    }
                } else if (cmd.cmd == "refresh_watches") {
                    std::vector<DapWatchItem> wc;
                    { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                    for (auto& w : wc) {
                        w.value = evalPhpExpr(w.expression);
                        w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                    }
                    { std::lock_guard<std::mutex> sl(m_mutex); m_state.watches = std::move(wc); }
                    if (onStateChanged) onStateChanged();
                } else if (cmd.cmd == "eval") {
                    std::wstring res = evalPhpExpr(cmd.arg);
                    std::lock_guard<std::mutex> el(m_evalMutex);
                    m_evalResult = res;
                    m_evalReady = true;
                    m_evalCv.notify_all();
                }
            }
        }

        pumpDbgp(20);
        while (!pktQueue.empty()) {
            std::string xml = std::move(pktQueue.front());
            pktQueue.erase(pktQueue.begin());
            if (xml.find("status=\"break\"") != std::string::npos) {
                std::string stackXml = sendDbgpWait("stack_get", 1200);
                std::vector<DapStackFrame> frames;
                ParsePhpDbgpStack(stackXml, cfg.program, frames);
                activeFrameDepth = frames.empty() ? 0 : frames[0].id;
                std::wstring pf = frames.empty() ? cfg.program : frames[0].sourcePath;
                int pl = frames.empty() ? 1 : frames[0].line1Based;

                {
                    bool shouldPause = true;
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        shouldPause = HandleBreakpointEvaluationLocked(pf, pl, evalPhpExpr);
                    }
                    if (!shouldPause) {
                        sendDbgpRaw("run");
                        continue;
                    }
                }

                std::vector<DapScope> scopes = loadPhpScopes(activeFrameDepth);
                std::vector<DapWatchItem> wc;
                { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                for (auto& w : wc) {
                    w.value = evalPhpExpr(w.expression);
                    w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                }

                {
                    std::lock_guard<std::mutex> sl(m_mutex);
                    m_state.isPaused = true;
                    m_state.selectedFrameId = activeFrameDepth;
                    m_state.stoppedReason = L"breakpoint";
                    m_state.pausedFile = pf;
                    m_state.pausedLine1Based = pl;
                    m_state.callStack = std::move(frames);
                    m_state.scopes = std::move(scopes);
                    m_state.watches = std::move(wc);
                }
                m_paused.store(true);
                if (onPaused) onPaused(pf, pl);
                if (onStateChanged) onStateChanged();
            } else if (xml.find("status=\"stopped\"") != std::string::npos || xml.find("status=\"stopping\"") != std::string::npos) {
                m_stopRequested.store(true);
                break;
            }
        }
    }
}

// =========================================================================
// Backend 5: External Stdio DAP Adapter Session
// =========================================================================

void DapClient::RunExternalDapSession(const LaunchConfig& cfg) {
    if (cfg.adapterCommand.empty()) {
        if (onOutput) onOutput(L"[DAP] No adapterCommand specified for external DAP configuration.\r\n");
        return;
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

    std::wstring cmd = L"\"" + cfg.adapterCommand + L"\"";
    for (const auto& a : cfg.adapterArgs) cmd += L" \"" + a + L"\"";

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cfg.cwd.empty() ? nullptr : cfg.cwd.c_str(), &si, &pi);
    CloseHandle(hInRead);
    CloseHandle(hOutWrite);
    if (!ok) {
        CloseHandle(hInWrite);
        CloseHandle(hOutRead);
        if (onOutput) onOutput(L"[DAP] Failed to start external adapter: " + cfg.adapterCommand + L"\r\n");
        return;
    }

    m_hChildProc = pi.hProcess;
    m_childPid = pi.dwProcessId;
    m_hStdinWrite = hInWrite;
    m_hStdoutRead = hOutRead;
    CloseHandle(pi.hThread);

    DapFrameDecoder decoder;
    std::vector<std::string> pendingEvents;
    int nextSeq = 1;
    int activeThreadId = 1;
    int activeFrameId = 0;

    auto sendDapReqAsync = [&](const std::string& command, const std::string& argsJson = "{}") -> int {
        int seq = nextSeq++;
        std::string payload = "{\"seq\":" + std::to_string(seq) +
                              ",\"type\":\"request\",\"command\":\"" + EscapeJsonUtf8(command) + "\"" +
                              (argsJson.empty() ? "" : (",\"arguments\":" + argsJson)) + "}";
        WritePipeLine(EncodeDapFrame(payload));
        return seq;
    };

    auto pumpDecoder = [&](int waitMs) {
        DWORD avail = 0;
        if (PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char buf[8192];
            DWORD rd = 0;
            if (ReadFile(m_hStdoutRead, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
                decoder.Feed(buf, rd);
            }
        } else if (waitMs > 0) {
            Sleep(static_cast<DWORD>(waitMs));
        }
        std::string msg;
        while (decoder.PopMessage(msg)) {
            pendingEvents.push_back(std::move(msg));
        }
    };

    auto sendDapReqWait = [&](const std::string& command, const std::string& argsJson, int timeoutMs = 2000) -> std::string {
        int wantSeq = sendDapReqAsync(command, argsJson);
        DWORD deadline = GetTickCount() + static_cast<DWORD>(timeoutMs);
        while (GetTickCount() < deadline && !m_stopRequested.load()) {
            pumpDecoder(5);
            for (size_t i = 0; i < pendingEvents.size(); ++i) {
                std::string mtype;
                int rseq = 0;
                if (ExtractJsonStringA(pendingEvents[i], "type", mtype) && mtype == "response" &&
                    ExtractJsonIntA(pendingEvents[i], "request_seq", rseq) && rseq == wantSeq) {
                    std::string out = std::move(pendingEvents[i]);
                    pendingEvents.erase(pendingEvents.begin() + i);
                    return out;
                }
            }
        }
        return "";
    };

    auto syncExtBreakpoints = [&]() {
        std::vector<DapBreakpoint> bps;
        { std::lock_guard<std::mutex> sl(m_mutex); bps = m_state.breakpoints; }
        std::vector<std::wstring> uniqueFiles;
        for (const auto& bp : bps) {
            if (!bp.enabled) continue;
            bool seen = false;
            for (const auto& f : uniqueFiles) if (PathsMatchW(f, bp.filePath)) { seen = true; break; }
            if (!seen) uniqueFiles.push_back(bp.filePath);
        }
        if (uniqueFiles.empty() && !cfg.program.empty()) uniqueFiles.push_back(cfg.program);

        for (const auto& fp : uniqueFiles) {
            std::ostringstream oss;
            oss << "{\"source\":{\"path\":\"" << EscapeJsonUtf8(WideToUtf8(fp)) << "\"},\"breakpoints\":[";
            bool first = true;
            for (const auto& bp : bps) {
                if (!bp.enabled || !PathsMatchW(bp.filePath, fp)) continue;
                if (!first) oss << ",";
                first = false;
                oss << "{\"line\":" << bp.line1Based;
                if (!bp.condition.empty()) oss << ",\"condition\":\"" << EscapeJsonUtf8(WideToUtf8(bp.condition)) << "\"";
                if (!bp.logMessage.empty()) oss << ",\"logMessage\":\"" << EscapeJsonUtf8(WideToUtf8(bp.logMessage)) << "\"";
                oss << "}";
            }
            oss << "]}";
            sendDapReqWait("setBreakpoints", oss.str(), 1000);
        }
    };

    auto syncExtExceptions = [&]() {
        DapExceptionOptions ex;
        { std::lock_guard<std::mutex> sl(m_mutex); ex = m_state.exceptions; }
        std::ostringstream oss;
        oss << "{\"filters\":[";
        bool first = true;
        if (ex.breakOnRaised) { oss << "\"raised\""; first = false; }
        if (ex.breakOnUncaught) { if (!first) oss << ","; oss << "\"uncaught\""; }
        oss << "]}";
        sendDapReqWait("setExceptionBreakpoints", oss.str(), 800);
    };

    auto parseExtVars = [](const std::string& varsRespJson) {
        std::vector<DapVariable> vars;
        std::string bodyObj;
        const std::string& src = ExtractNestedObjectA(varsRespJson, "body", bodyObj) ? bodyObj : varsRespJson;
        for (const auto& vo : SplitJsonObjectArrayA(src, "variables")) {
            if (vars.size() >= 60) break;
            std::string vn, vv, vt;
            ExtractJsonStringA(vo, "name", vn);
            ExtractJsonStringA(vo, "value", vv);
            ExtractJsonStringA(vo, "type", vt);
            int vref = 0;
            ExtractJsonIntA(vo, "variablesReference", vref);
            DapVariable dv;
            dv.name = Utf8ToWide(vn);
            dv.value = Utf8ToWide(vv);
            dv.type = Utf8ToWide(vt);
            dv.variablesReference = vref;
            vars.push_back(std::move(dv));
        }
        return vars;
    };

    auto loadExtScopes = [&](int frameId) {
        std::vector<DapScope> scopes;
        std::string scResp = sendDapReqWait("scopes", "{\"frameId\":" + std::to_string(frameId) + "}", 1500);
        std::string bodyObj;
        const std::string& src = ExtractNestedObjectA(scResp, "body", bodyObj) ? bodyObj : scResp;
        for (const auto& so : SplitJsonObjectArrayA(src, "scopes")) {
            std::string sname;
            int sref = 0;
            bool expv = false;
            ExtractJsonStringA(so, "name", sname);
            ExtractJsonIntA(so, "variablesReference", sref);
            ExtractJsonBoolA(so, "expensive", expv);
            DapScope sc;
            sc.name = Utf8ToWide(sname.empty() ? "Locals" : sname);
            sc.variablesReference = sref;
            if (!expv && sref > 0) {
                std::string vr = sendDapReqWait("variables", "{\"variablesReference\":" + std::to_string(sref) + "}", 1200);
                sc.variables = parseExtVars(vr);
            }
            scopes.push_back(std::move(sc));
        }
        return scopes;
    };

    auto evalExtExpr = [&](const std::wstring& expr) -> std::wstring {
        if (expr.empty()) return L"";
        std::ostringstream oss;
        oss << "{\"expression\":\"" << EscapeJsonUtf8(WideToUtf8(expr)) << "\",\"frameId\":" << activeFrameId << ",\"context\":\"repl\"}";
        std::string resp = sendDapReqWait("evaluate", oss.str(), 1500);
        std::string bodyObj;
        const std::string& src = ExtractNestedObjectA(resp, "body", bodyObj) ? bodyObj : resp;
        std::string res;
        if (ExtractJsonStringA(src, "result", res)) return Utf8ToWide(res);
        std::string msg;
        if (ExtractJsonStringA(resp, "message", msg)) return L"<Error: " + Utf8ToWide(msg) + L">";
        return L"";
    };

    // Standard DAP startup handshake
    sendDapReqWait("initialize",
        "{\"clientID\":\"phide\",\"adapterID\":\"" + EscapeJsonUtf8(WideToUtf8(cfg.type.empty() ? L"dap" : cfg.type)) +
        "\",\"linesStartAt1\":true,\"columnsStartAt1\":true,\"pathFormat\":\"path\",\"supportsVariableType\":true}",
        3000);

    {
        std::ostringstream args;
        args << "{\"program\":\"" << EscapeJsonUtf8(WideToUtf8(cfg.program))
             << "\",\"cwd\":\"" << EscapeJsonUtf8(WideToUtf8(cfg.cwd))
             << "\",\"stopOnEntry\":" << (cfg.stopOnEntry ? "true" : "false") << ",\"args\":[";
        for (size_t i = 0; i < cfg.args.size(); ++i) {
            if (i > 0) args << ",";
            args << "\"" << EscapeJsonUtf8(WideToUtf8(cfg.args[i])) << "\"";
        }
        args << "]}";
        sendDapReqAsync(cfg.request == L"attach" ? "attach" : "launch", args.str());
    }

    syncExtBreakpoints();
    syncExtExceptions();
    sendDapReqWait("configurationDone", "{}", 1500);

    while (!m_stopRequested.load()) {
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            while (!m_cmdQueue.empty()) {
                auto cmd = std::move(m_cmdQueue.front());
                m_cmdQueue.pop();
                if (cmd.cmd == "continue" || cmd.cmd == "next" || cmd.cmd == "step" || cmd.cmd == "step_out") {
                    m_paused.store(false);
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.isPaused = false;
                        m_state.stoppedReason.clear();
                        m_state.stoppedText.clear();
                    }
                    if (onContinued) onContinued();
                    if (onStateChanged) onStateChanged();
                    std::string dc = (cmd.cmd == "continue") ? "continue" :
                                     (cmd.cmd == "next") ? "next" :
                                     (cmd.cmd == "step") ? "stepIn" : "stepOut";
                    sendDapReqAsync(dc, "{\"threadId\":" + std::to_string(activeThreadId) + "}");
                } else if (cmd.cmd == "pause") {
                    sendDapReqAsync("pause", "{\"threadId\":" + std::to_string(activeThreadId) + "}");
                } else if (cmd.cmd == "quit") {
                    sendDapReqWait("disconnect", "{\"terminateDebuggee\":true}", 500);
                    m_stopRequested.store(true);
                } else if (cmd.cmd == "set_bp") {
                    syncExtBreakpoints();
                } else if (cmd.cmd == "set_exc") {
                    syncExtExceptions();
                } else if (cmd.cmd == "select_frame") {
                    activeFrameId = cmd.intArg;
                    std::vector<DapScope> scopes = loadExtScopes(activeFrameId);
                    std::vector<DapWatchItem> wc;
                    { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                    for (auto& w : wc) {
                        w.value = evalExtExpr(w.expression);
                        w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                    }
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        m_state.scopes = std::move(scopes);
                        m_state.watches = std::move(wc);
                    }
                    if (onStateChanged) onStateChanged();
                } else if (cmd.cmd == "children") {
                    std::string vr = sendDapReqWait("variables", "{\"variablesReference\":" + std::to_string(cmd.intArg) + "}", 1200);
                    std::vector<DapVariable> kids = parseExtVars(vr);
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        for (auto& sc : m_state.scopes) {
                            if (AssignVariableChildrenRecursive(sc.variables, cmd.intArg, std::move(kids))) break;
                        }
                    }
                    if (onStateChanged) onStateChanged();
                } else if (cmd.cmd == "refresh_watches") {
                    std::vector<DapWatchItem> wc;
                    { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                    for (auto& w : wc) {
                        w.value = evalExtExpr(w.expression);
                        w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                    }
                    { std::lock_guard<std::mutex> sl(m_mutex); m_state.watches = std::move(wc); }
                    if (onStateChanged) onStateChanged();
                } else if (cmd.cmd == "eval") {
                    std::wstring res = evalExtExpr(cmd.arg);
                    std::lock_guard<std::mutex> el(m_evalMutex);
                    m_evalResult = res;
                    m_evalReady = true;
                    m_evalCv.notify_all();
                }
            }
        }

        pumpDecoder(15);
        while (!pendingEvents.empty()) {
            std::string msg = std::move(pendingEvents.front());
            pendingEvents.erase(pendingEvents.begin());
            std::string mtype;
            if (!ExtractJsonStringA(msg, "type", mtype) || mtype != "event") continue;
            std::string evName;
            ExtractJsonStringA(msg, "event", evName);
            std::string bodyObj;
            ExtractNestedObjectA(msg, "body", bodyObj);

            if (evName == "output") {
                std::string txt;
                if (ExtractJsonStringA(bodyObj, "output", txt) && !txt.empty() && onOutput) {
                    onOutput(Utf8ToWide(txt));
                }
            } else if (evName == "stopped") {
                std::string reason, text;
                ExtractJsonStringA(bodyObj, "reason", reason);
                ExtractJsonStringA(bodyObj, "text", text);
                ExtractJsonIntA(bodyObj, "threadId", activeThreadId);

                std::string stResp = sendDapReqWait(
                    "stackTrace", "{\"threadId\":" + std::to_string(activeThreadId) + ",\"startFrame\":0,\"levels\":40}", 1500);
                std::string stBody;
                const std::string& stSrc = ExtractNestedObjectA(stResp, "body", stBody) ? stBody : stResp;
                std::vector<DapStackFrame> frames;
                for (const auto& fo : SplitJsonObjectArrayA(stSrc, "stackFrames")) {
                    if (frames.size() >= 64) break;
                    DapStackFrame fr;
                    std::string fn, srcPath;
                    ExtractJsonIntA(fo, "id", fr.id);
                    ExtractJsonStringA(fo, "name", fn);
                    ExtractJsonIntA(fo, "line", fr.line1Based);
                    ExtractJsonIntA(fo, "column", fr.col1Based);
                    std::string srcObj;
                    if (ExtractNestedObjectA(fo, "source", srcObj)) {
                        ExtractJsonStringA(srcObj, "path", srcPath);
                    }
                    fr.name = Utf8ToWide(fn.empty() ? "frame" : fn);
                    fr.sourcePath = !srcPath.empty() ? Utf8ToWide(srcPath) : cfg.program;
                    fr.line1Based = (std::max)(1, fr.line1Based);
                    frames.push_back(std::move(fr));
                }

                activeFrameId = frames.empty() ? 0 : frames[0].id;
                std::wstring pf = frames.empty() ? cfg.program : frames[0].sourcePath;
                int pl = frames.empty() ? 1 : frames[0].line1Based;

                if (reason == "breakpoint") {
                    bool shouldPause = true;
                    {
                        std::lock_guard<std::mutex> sl(m_mutex);
                        shouldPause = HandleBreakpointEvaluationLocked(pf, pl, evalExtExpr);
                    }
                    if (!shouldPause) {
                        sendDapReqAsync("continue", "{\"threadId\":" + std::to_string(activeThreadId) + "}");
                        continue;
                    }
                }

                std::vector<DapScope> scopes = loadExtScopes(activeFrameId);
                std::vector<DapWatchItem> wc;
                { std::lock_guard<std::mutex> sl(m_mutex); wc = m_state.watches; }
                for (auto& w : wc) {
                    w.value = evalExtExpr(w.expression);
                    w.hasError = w.value.rfind(L"<Error:", 0) == 0;
                }

                {
                    std::lock_guard<std::mutex> sl(m_mutex);
                    m_state.isPaused = true;
                    m_state.selectedFrameId = activeFrameId;
                    m_state.stoppedReason = Utf8ToWide(reason);
                    m_state.stoppedText = Utf8ToWide(text);
                    m_state.pausedFile = pf;
                    m_state.pausedLine1Based = pl;
                    m_state.callStack = std::move(frames);
                    m_state.scopes = std::move(scopes);
                    m_state.watches = std::move(wc);
                }
                m_paused.store(true);
                if (onPaused) onPaused(pf, pl);
                if (onStateChanged) onStateChanged();
            } else if (evName == "continued") {
                m_paused.store(false);
                {
                    std::lock_guard<std::mutex> sl(m_mutex);
                    m_state.isPaused = false;
                }
                if (onContinued) onContinued();
                if (onStateChanged) onStateChanged();
            } else if (evName == "terminated" || evName == "exited") {
                m_stopRequested.store(true);
                break;
            }
        }
        if (WaitForSingleObject(m_hChildProc, 5) == WAIT_OBJECT_0) break;
    }
}

} // namespace phide::dev
