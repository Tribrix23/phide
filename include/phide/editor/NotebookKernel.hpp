#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace phide::editor {

/**
 * @struct KernelExecutionResult
 * @brief Output and execution status from a kernel cell run.
 */
struct KernelExecutionResult {
    std::wstring output;
    int exitCode = 0;
    uint64_t elapsedMs = 0;
    bool hasError = false;
    std::vector<std::string> base64Pngs;
};

/**
 * @class NotebookKernel
 * @brief Persistent, stateful Python kernel process manager (Jupyter / Colab execution model).
 *
 * Keeps a single Python process running with a persistent global namespace dictionary.
 * When cells run in ANY order, variables, functions, and imports are retained in memory.
 */
class NotebookKernel {
public:
    NotebookKernel() = default;
    ~NotebookKernel() {
        Shutdown();
    }

    NotebookKernel(const NotebookKernel&) = delete;
    NotebookKernel& operator=(const NotebookKernel&) = delete;

    /**
     * @brief Ensures the persistent Python kernel process is running.
     * @param pythonExe Path to the Python executable.
     * @param workingDir Optional project working directory.
     * @return true if running or successfully launched.
     */
    bool EnsureStarted(const std::wstring& pythonExe, const std::wstring& workingDir = L"") {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (IsProcessAlive()) {
            return true;
        }

        CleanupProcess();

        m_pythonPath = pythonExe;
        std::wstring driverPath = EnsureDriverScriptCreated();
        if (driverPath.empty()) {
            return false;
        }

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        HANDLE hStdoutRead = NULL;
        HANDLE hStdoutWrite = NULL;
        if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
            return false;
        }
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

        HANDLE hStdinRead = NULL;
        HANDLE hStdinWrite = NULL;
        if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) {
            CloseHandle(hStdoutRead);
            CloseHandle(hStdoutWrite);
            return false;
        }
        SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = hStdinRead;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStdoutWrite;

        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + pythonExe + L"\" -u \"" + driverPath + L"\"";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');

        BOOL ok = CreateProcessW(
            NULL, cmdBuf.data(), NULL, NULL, TRUE,
            CREATE_NO_WINDOW, NULL,
            workingDir.empty() ? NULL : workingDir.c_str(),
            &si, &pi
        );

        CloseHandle(hStdoutWrite);
        CloseHandle(hStdinRead);

        if (!ok) {
            CloseHandle(hStdoutRead);
            CloseHandle(hStdinWrite);
            return false;
        }

        CloseHandle(pi.hThread);
        m_hProcess.store(pi.hProcess);
        m_hStdinWrite.store(hStdinWrite);
        m_hStdoutRead.store(hStdoutRead);
        m_isRunning.store(true);
        m_shouldInterrupt.store(false);
        return true;
    }

    /**
     * @brief Executes cell code in the persistent kernel namespace.
     * @param code Wide string containing the Python cell code.
     * @param pythonExe Path to Python executable.
     * @param workingDir Optional project working directory.
     * @return KernelExecutionResult containing clean stdout/stderr and exit code.
     */
    KernelExecutionResult Execute(const std::wstring& code, const std::wstring& pythonExe, const std::wstring& workingDir = L"",
                                  std::function<void(const std::wstring& chunk, bool isPrompt, const std::wstring& promptText)> onOutputChunk = nullptr) {
        KernelExecutionResult res;
        auto startTime = std::chrono::high_resolution_clock::now();

        // Fast-path: whitespace-only or empty code cells complete instantly in 0 ms
        bool isWhitespaceOnly = true;
        for (wchar_t ch : code) {
            if (!iswspace(ch)) {
                isWhitespaceOnly = false;
                break;
            }
        }
        if (isWhitespaceOnly) {
            res.exitCode = 0;
            res.hasError = false;
            res.output = L"";
            res.elapsedMs = 0;
            return res;
        }

        if (pythonExe.empty()) {
            res.exitCode = 1;
            res.hasError = true;
            res.output = L"Python is not detected. Please install Python and select a Python interpreter to run notebook cells.\n";
            return res;
        }

        if (!EnsureStarted(pythonExe, workingDir)) {
            res.exitCode = 1;
            res.hasError = true;
            res.output = L"Error: Failed to launch Python kernel process. Please ensure Python is installed.\n";
            return res;
        }

        m_shouldInterrupt.store(false);
        m_isBusy.store(true);

        std::string u8Code = WideToUtf8(code);
        std::string b64 = Base64Encode(u8Code);
        std::string line = "EXEC_B64 " + b64 + "\n";

        HANDLE hWrite = m_hStdinWrite.load();
        if (!hWrite || hWrite == INVALID_HANDLE_VALUE) {
            m_isBusy.store(false);
            CleanupProcess();
            res.exitCode = 1;
            res.hasError = true;
            res.output = L"Error: Kernel stdin pipe unavailable.\n";
            return res;
        }

        DWORD written = 0;
        if (!WriteFile(hWrite, line.data(), static_cast<DWORD>(line.size()), &written, NULL)) {
            m_isBusy.store(false);
            CleanupProcess();
            res.exitCode = 1;
            res.hasError = true;
            res.output = L"Error: Failed to write cell code to kernel stdin pipe.\n";
            return res;
        }

        // Read stdout pipe until delimiter sentinel: __PHIDE_NB_END__<exitCode>__
        std::string rawOutput;
        char buffer[4096];
        DWORD bytesRead = 0;
        const std::string sentinelPrefix = "__PHIDE_NB_END__";
        const std::string promptPrefix = "__PHIDE_INPUT_PROMPT__";

        HANDLE hRead = m_hStdoutRead.load();
        HANDLE hProc = m_hProcess.load();
        while (!m_shouldInterrupt.load() && hRead && hRead != INVALID_HANDLE_VALUE) {
            DWORD avail = 0;
            if (!PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL)) {
                DWORD code = 1;
                if (hProc) GetExitCodeProcess(hProc, &code);
                res.exitCode = (code != 0) ? code : 1;
                res.hasError = true;
                CleanupProcess();
                break;
            }
            if (avail == 0) {
                if (hProc && WaitForSingleObject(hProc, 0) == WAIT_OBJECT_0) {
                    DWORD code = 1;
                    GetExitCodeProcess(hProc, &code);
                    res.exitCode = (code != 0) ? code : 1;
                    res.hasError = true;
                    DWORD rem = 0;
                    if (PeekNamedPipe(hRead, NULL, 0, NULL, &rem, NULL) && rem > 0) {
                        BOOL rOk = ReadFile(hRead, buffer, (std::min)(rem, static_cast<DWORD>(sizeof(buffer) - 1)), &bytesRead, NULL);
                        if (rOk && bytesRead > 0) {
                            buffer[bytesRead] = '\0';
                            rawOutput.append(buffer, bytesRead);
                        }
                    }
                    CleanupProcess();
                    break;
                }
                Sleep(20);
                continue;
            }

            BOOL readOk = ReadFile(hRead, buffer, (std::min)(avail, static_cast<DWORD>(sizeof(buffer) - 1)), &bytesRead, NULL);
            if (!readOk || bytesRead == 0 || m_shouldInterrupt.load()) {
                if (m_shouldInterrupt.load()) {
                    rawOutput.clear();
                    res.hasError = false;
                    res.exitCode = 0;
                }
                CleanupProcess();
                break;
            }
            buffer[bytesRead] = '\0';
            rawOutput.append(buffer, bytesRead);

            // Check for interactive input() prompt sentinel
            size_t pPos = rawOutput.find(promptPrefix);
            if (pPos != std::string::npos) {
                size_t pEnd = rawOutput.find("__", pPos + promptPrefix.size());
                if (pEnd != std::string::npos) {
                    std::string pText = rawOutput.substr(pPos + promptPrefix.size(), pEnd - (pPos + promptPrefix.size()));
                    std::string displayChunk = rawOutput.substr(0, pPos);
                    rawOutput.erase(0, pEnd + 2);
                    if (onOutputChunk) {
                        onOutputChunk(Utf8ToWide(displayChunk), true, Utf8ToWide(pText));
                    }
                }
            } else if (onOutputChunk && !rawOutput.empty()) {
                if (rawOutput.find("__PHIDE") == std::string::npos &&
                    rawOutput.find("PHIDE_IMG_PNG:") == std::string::npos &&
                    rawOutput.find("PHIDE_VARS_JSON:") == std::string::npos) {
                    onOutputChunk(Utf8ToWide(rawOutput), false, L"");
                    rawOutput.clear();
                }
            }

            size_t sPos = rawOutput.find(sentinelPrefix);
            if (sPos != std::string::npos) {
                size_t sEnd = rawOutput.find("__", sPos + sentinelPrefix.size());
                if (sEnd != std::string::npos) {
                    std::string codeStr = rawOutput.substr(sPos + sentinelPrefix.size(), sEnd - (sPos + sentinelPrefix.size()));
                    try {
                        res.exitCode = std::stoi(codeStr);
                    } catch (...) {
                        res.exitCode = 0;
                    }
                    rawOutput.erase(sPos);
                    break;
                }
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        res.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        res.hasError = (res.exitCode != 0);

        // Extract PHIDE_IMG_PNG:<b64> lines cleanly (bounded <= 4 images per cell, <= ~2.8MB b64 each)
        {
            std::string cleanText;
            cleanText.reserve(rawOutput.size());
            size_t pos = 0;
            const size_t n = rawOutput.size();
            static constexpr size_t kMaxB64Len = (2u * 1024u * 1024u * 4u) / 3u + 64u;
            while (pos < n) {
                size_t eol = rawOutput.find('\n', pos);
                std::string line = (eol == std::string::npos)
                    ? rawOutput.substr(pos)
                    : rawOutput.substr(pos, eol - pos);
                pos = (eol == std::string::npos) ? n : (eol + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("PHIDE_IMG_PNG:", 0) == 0) {
                    if (res.base64Pngs.size() < 4u) {
                        std::string b64 = line.substr(14);
                        if (!b64.empty() && b64.size() <= kMaxB64Len) {
                            res.base64Pngs.push_back(std::move(b64));
                        }
                    }
                    continue;
                }
                cleanText += line;
                if (eol != std::string::npos) cleanText += '\n';
            }
            rawOutput = std::move(cleanText);
        }

        // Strip leading/trailing excess whitespace while preserving internal structure
        while (!rawOutput.empty() && (rawOutput.front() == '\r' || rawOutput.front() == '\n')) {
            rawOutput.erase(rawOutput.begin());
        }
        while (!rawOutput.empty() && (rawOutput.back() == '\r' || rawOutput.back() == '\n' || rawOutput.back() == ' ')) {
            rawOutput.pop_back();
        }

        if (!rawOutput.empty()) {
            rawOutput += "\n";
        }
        res.output = Utf8ToWide(rawOutput);
        if (res.hasError && res.output.empty()) {
            res.output = L"Error: Python kernel process exited unexpectedly (exit code " +
                         std::to_wstring(res.exitCode) +
                         L"). Please ensure Python is installed and working properly.\n";
        }

        m_isBusy.store(false);
        return res;
    }

    bool SendStdin(const std::wstring& text) {
        HANDLE hWrite = m_hStdinWrite.load();
        if (!hWrite || hWrite == INVALID_HANDLE_VALUE) return false;
        std::string u8 = WideToUtf8(text) + "\n";
        DWORD written = 0;
        return WriteFile(hWrite, u8.data(), static_cast<DWORD>(u8.size()), &written, NULL) != FALSE;
    }

    /**
     * @brief Immediately interrupts execution without locking or freezing the UI thread.
     */
    void Interrupt() {
        m_shouldInterrupt.store(true);
        HANDLE hRead = m_hStdoutRead.load();
        if (hRead && hRead != INVALID_HANDLE_VALUE) {
            CancelIoEx(hRead, NULL);
        }
        HANDLE hProc = m_hProcess.load();
        if (hProc && hProc != INVALID_HANDLE_VALUE) {
            TerminateProcess(hProc, 1);
        }
        HANDLE hWrite = m_hStdinWrite.load();
        if (hWrite && hWrite != INVALID_HANDLE_VALUE) {
            CloseHandle(hWrite);
            m_hStdinWrite.store(NULL);
        }
        m_isRunning.store(false);
        m_isBusy.store(false);
    }

    /**
     * @brief Restarts the kernel with a clean empty namespace.
     */
    void Restart() {
        Interrupt();
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupProcess();
    }

    /**
     * @brief Cleanly shuts down the kernel process.
     */
    void Shutdown() {
        HANDLE hWrite = m_hStdinWrite.load();
        HANDLE hProc = m_hProcess.load();
        if (hWrite && IsProcessAlive()) {
            DWORD written = 0;
            WriteFile(hWrite, "QUIT\n", 5, &written, NULL);
            if (hProc) WaitForSingleObject(hProc, 200);
        }
        Interrupt();
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupProcess();
    }

    [[nodiscard]] bool IsBusy() const noexcept { return m_isBusy.load(); }
    [[nodiscard]] bool IsRunning() const noexcept { return m_isRunning.load(); }

    /**
     * @brief Queries non-dunder variables from the live Python namespace without interrupting cells.
     */
    bool QueryVariablesJson(std::string& outJson) {
        outJson.clear();
        std::unique_lock<std::mutex> lk(m_mutex, std::try_to_lock);
        if (!lk.owns_lock() || m_isBusy.load()) return false;
        HANDLE hWrite = m_hStdinWrite.load();
        HANDLE hRead = m_hStdoutRead.load();
        if (!hWrite || !hRead || !IsProcessAlive()) return false;

        DWORD written = 0;
        const char* cmd = "LIST_VARS\n";
        if (!WriteFile(hWrite, cmd, 10, &written, NULL)) return false;

        std::string raw;
        char buf[4096];
        auto t0 = std::chrono::steady_clock::now();
        while (raw.size() < 262144) {
            DWORD avail = 0;
            if (!PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL)) break;
            if (avail > 0) {
                DWORD rd = 0;
                if (ReadFile(hRead, buf, (std::min)(avail, static_cast<DWORD>(sizeof(buf) - 1)), &rd, NULL) && rd > 0) {
                    raw.append(buf, rd);
                    if (raw.find("__PHIDE_NB_END__") != std::string::npos) break;
                } else {
                    break;
                }
            } else {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count() > 800) {
                    break;
                }
                Sleep(10);
            }
        }
        size_t pos = raw.find("PHIDE_VARS_JSON:");
        if (pos == std::string::npos) return false;
        pos += 16;
        size_t eol = raw.find_first_of("\r\n", pos);
        outJson = (eol == std::string::npos) ? raw.substr(pos) : raw.substr(pos, eol - pos);
        return !outJson.empty();
    }

private:
    std::atomic<HANDLE> m_hProcess{ NULL };
    std::atomic<HANDLE> m_hStdinWrite{ NULL };
    std::atomic<HANDLE> m_hStdoutRead{ NULL };
    std::mutex m_mutex;
    std::atomic<bool> m_isRunning{ false };
    std::atomic<bool> m_isBusy{ false };
    std::atomic<bool> m_shouldInterrupt{ false };
    std::wstring m_pythonPath;

    bool IsProcessAlive() const {
        HANDLE hProc = m_hProcess.load();
        if (!hProc || hProc == INVALID_HANDLE_VALUE) return false;
        DWORD code = 0;
        if (GetExitCodeProcess(hProc, &code)) {
            return (code == STILL_ACTIVE);
        }
        return false;
    }

    void CleanupProcess() {
        HANDLE hProc = m_hProcess.exchange(NULL);
        if (hProc && hProc != INVALID_HANDLE_VALUE) {
            DWORD code = 0;
            if (GetExitCodeProcess(hProc, &code) && code == STILL_ACTIVE) {
                TerminateProcess(hProc, 0);
            }
            CloseHandle(hProc);
        }
        HANDLE hWrite = m_hStdinWrite.exchange(NULL);
        if (hWrite && hWrite != INVALID_HANDLE_VALUE) {
            CloseHandle(hWrite);
        }
        HANDLE hRead = m_hStdoutRead.exchange(NULL);
        if (hRead && hRead != INVALID_HANDLE_VALUE) {
            CloseHandle(hRead);
        }
        m_isRunning.store(false);
        m_isBusy.store(false);
    }

    std::wstring EnsureDriverScriptCreated() {
        wchar_t tempDir[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring scriptPath = std::wstring(tempDir) + L"phide_nb_kernel_driver.py";

        // Always write or verify driver script
        std::ofstream ofs(scriptPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!ofs.is_open()) return L"";

        const char* driverCode =
            "import sys, os, site, base64, traceback, ast, io, json\n\n"
            "# 1. Ensure current working directory and local virtual environments are configured\n"
            "cwd = os.getcwd()\n"
            "if cwd and cwd not in sys.path:\n"
            "    sys.path.insert(0, cwd)\n\n"
            "# 2. Auto-detect project virtual environments and register site-packages\n"
            "for vname in ('.venv', 'venv', 'env', '.env'):\n"
            "    candidate = os.path.join(cwd, vname)\n"
            "    if os.path.isdir(candidate):\n"
            "        for sub in ('Lib/site-packages', 'lib/site-packages'):\n"
            "            sp = os.path.normpath(os.path.join(candidate, sub))\n"
            "            if os.path.isdir(sp):\n"
            "                site.addsitedir(sp)\n\n"
            "# 3. Ensure user global site-packages is available\n"
            "try:\n"
            "    usp = site.getusersitepackages()\n"
            "    if os.path.isdir(usp) and usp not in sys.path:\n"
            "        site.addsitedir(usp)\n"
            "except Exception:\n"
            "    pass\n\n"
            "# 4. Non-interactive Matplotlib Agg backend & bounded PNG emitter\n"
            "try:\n"
            "    os.environ.setdefault('MPLBACKEND', 'Agg')\n"
            "    import matplotlib\n"
            "    matplotlib.use('Agg')\n"
            "except Exception:\n"
            "    pass\n\n"
            "def _phide_flush_figs():\n"
            "    try:\n"
            "        plt = sys.modules.get('matplotlib.pyplot')\n"
            "        if plt is None:\n"
            "            return\n"
            "        fnums = list(plt.get_fignums())[:4]\n"
            "        for fn in fnums:\n"
            "            fig = plt.figure(fn)\n"
            "            buf = io.BytesIO()\n"
            "            fig.savefig(buf, format='png', bbox_inches='tight', dpi=120)\n"
            "            data = buf.getvalue()\n"
            "            buf.close()\n"
            "            if 0 < len(data) <= 2 * 1024 * 1024:\n"
            "                b64 = base64.b64encode(data).decode('ascii')\n"
            "                sys.stdout.write('\\nPHIDE_IMG_PNG:' + b64 + '\\n')\n"
            "        plt.close('all')\n"
            "    except Exception:\n"
            "        pass\n\n"
            "def _phide_ensure_plt_hook():\n"
            "    plt = sys.modules.get('matplotlib.pyplot')\n"
            "    if plt is not None and not getattr(plt, '_phide_hooked', False):\n"
            "        def _phide_show(*args, **kwargs):\n"
            "            _phide_flush_figs()\n"
            "        plt.show = _phide_show\n"
            "        plt._phide_hooked = True\n\n"
            "# 5. Interactive input() redirection (Colab/Jupyter standard)\n"
            "import builtins\n"
            "def _phide_input(prompt=''):\n"
            "    p_str = str(prompt) if prompt is not None else ''\n"
            "    sys.stdout.write(p_str)\n"
            "    sys.stdout.flush()\n"
            "    sys.stdout.write(f'\\n__PHIDE_INPUT_PROMPT__{p_str}__\\n')\n"
            "    sys.stdout.flush()\n"
            "    inp = sys.stdin.readline()\n"
            "    if not inp:\n"
            "        return ''\n"
            "    return inp.rstrip('\\r\\n')\n"
            "builtins.input = _phide_input\n\n"
            "_ns = {'__name__': '__main__'}\n\n"
            "while True:\n"
            "    try:\n"
            "        line = sys.stdin.readline()\n"
            "        if not line:\n"
            "            break\n"
            "        line = line.strip()\n"
            "        if line in ('EXIT', 'QUIT'):\n"
            "            break\n"
            "        if line == 'LIST_VARS':\n"
            "            items = []\n"
            "            for k, v in list(_ns.items()):\n"
            "                if k.startswith('_'):\n"
            "                    continue\n"
            "                if type(v).__name__ == 'module':\n"
            "                    continue\n"
            "                try:\n"
            "                    tname = type(v).__name__\n"
            "                    r = repr(v)\n"
            "                    if len(r) > 120:\n"
            "                        r = r[:117] + '...'\n"
            "                    items.append({'name': str(k), 'type': tname, 'value': r})\n"
            "                except Exception:\n"
            "                    pass\n"
            "                if len(items) >= 64:\n"
            "                    break\n"
            "            sys.stdout.write('PHIDE_VARS_JSON:' + json.dumps(items) + '\\n')\n"
            "            sys.stdout.flush()\n"
            "            print('__PHIDE_NB_END__0__', flush=True)\n"
            "            continue\n"
            "        if not line.startswith('EXEC_B64'):\n"
            "            continue\n"
            "        b64_str = line[8:].strip()\n"
            "        code_str = ''\n"
            "        if b64_str:\n"
            "            try:\n"
            "                code_str = base64.b64decode(b64_str.encode('ascii')).decode('utf-8', errors='replace')\n"
            "            except Exception:\n"
            "                code_str = ''\n\n"
            "        exit_code = 0\n"
            "        try:\n"
            "            _phide_ensure_plt_hook()\n"
            "            if code_str.strip():\n"
            "                tree = ast.parse(code_str, '<cell>', 'exec')\n"
            "                _phide_ensure_plt_hook()\n"
            "                if tree.body and isinstance(tree.body[-1], ast.Expr):\n"
            "                    last_expr = tree.body.pop()\n"
            "                    if tree.body:\n"
            "                        exec(compile(ast.Module(body=tree.body, type_ignores=[]), '<cell>', 'exec'), _ns)\n"
            "                    _phide_ensure_plt_hook()\n"
            "                    val = eval(compile(ast.Expression(body=last_expr.value), '<cell>', 'eval'), _ns)\n"
            "                    if val is not None:\n"
            "                        sys.stdout.write(repr(val) + '\\n')\n"
            "                else:\n"
            "                    exec(compile(tree, '<cell>', 'exec'), _ns)\n"
            "        except Exception:\n"
            "            etype, evalue, tb = sys.exc_info()\n"
            "            cur = tb\n"
            "            while cur is not None and cur.tb_frame.f_code.co_filename != '<cell>':\n"
            "                cur = cur.tb_next\n"
            "            if cur is not None:\n"
            "                traceback.print_exception(etype, evalue, cur)\n"
            "            else:\n"
            "                traceback.print_exc()\n"
            "            exit_code = 1\n"
            "        except SystemExit as se:\n"
            "            exit_code = se.code if isinstance(se.code, int) else 1\n"
            "        finally:\n"
            "            _phide_flush_figs()\n\n"
            "        sys.stdout.flush()\n"
            "        sys.stderr.flush()\n"
            "        print(f'\\n__PHIDE_NB_END__{exit_code}__', flush=True)\n"
            "    except Exception:\n"
            "        sys.stdout.flush()\n"
            "        sys.stderr.flush()\n"
            "        print('\\n__PHIDE_NB_END__1__', flush=True)\n";

        ofs.write(driverCode, strlen(driverCode));
        ofs.close();
        return scriptPath;
    }

    static std::string Base64Encode(const std::string& in) {
        static const char b64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        int val = 0, valb = -6;
        for (unsigned char c : in) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                out.push_back(b64Chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) out.push_back(b64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }

    std::string WideToUtf8(const std::wstring& w) const {
        if (w.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string s(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
        if (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }

    std::wstring Utf8ToWide(const std::string& u) const {
        if (u.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
        if (len <= 0) return L"";
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), len);
        if (!w.empty() && w.back() == L'\0') w.pop_back();
        return w;
    }
};

} // namespace phide::editor
