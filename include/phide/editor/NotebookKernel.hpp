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
     * @return true if running or successfully launched.
     */
    bool EnsureStarted(const std::wstring& pythonExe) {
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
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi
        );

        CloseHandle(hStdoutWrite);
        CloseHandle(hStdinRead);

        if (!ok) {
            CloseHandle(hStdoutRead);
            CloseHandle(hStdinWrite);
            return false;
        }

        CloseHandle(pi.hThread);
        m_hProcess = pi.hProcess;
        m_hStdinWrite = hStdinWrite;
        m_hStdoutRead = hStdoutRead;
        m_isRunning.store(true);
        return true;
    }

    /**
     * @brief Executes cell code in the persistent kernel namespace.
     * @param code Wide string containing the Python cell code.
     * @return KernelExecutionResult containing clean stdout/stderr and exit code.
     */
    KernelExecutionResult Execute(const std::wstring& code, const std::wstring& pythonExe) {
        KernelExecutionResult res;
        auto startTime = std::chrono::high_resolution_clock::now();

        if (!EnsureStarted(pythonExe)) {
            res.exitCode = 1;
            res.hasError = true;
            res.output = L"Error: Failed to launch Python kernel process.\n";
            return res;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_isBusy.store(true);

        std::string u8Code = WideToUtf8(code);
        std::string b64 = Base64Encode(u8Code);
        std::string line = "EXEC_B64 " + b64 + "\n";

        DWORD written = 0;
        if (!WriteFile(m_hStdinWrite, line.data(), static_cast<DWORD>(line.size()), &written, NULL)) {
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

        while (true) {
            BOOL readOk = ReadFile(m_hStdoutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
            if (!readOk || bytesRead == 0) {
                // Process terminated unexpectedly
                CleanupProcess();
                break;
            }
            buffer[bytesRead] = '\0';
            rawOutput.append(buffer, bytesRead);

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

                    // Trim everything from the sentinel onwards
                    rawOutput.erase(sPos);
                    break;
                }
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        res.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        res.hasError = (res.exitCode != 0);

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

        m_isBusy.store(false);
        return res;
    }

    /**
     * @brief Interrupts the currently executing cell by restarting the kernel.
     */
    void Interrupt() {
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupProcess();
    }

    /**
     * @brief Restarts the kernel with a clean empty namespace.
     */
    void Restart() {
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupProcess();
    }

    /**
     * @brief Cleanly shuts down the kernel process.
     */
    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_hStdinWrite && IsProcessAlive()) {
            DWORD written = 0;
            WriteFile(m_hStdinWrite, "QUIT\n", 5, &written, NULL);
            WaitForSingleObject(m_hProcess, 300);
        }
        CleanupProcess();
    }

    [[nodiscard]] bool IsBusy() const noexcept { return m_isBusy.load(); }
    [[nodiscard]] bool IsRunning() const noexcept { return m_isRunning.load(); }

private:
    HANDLE m_hProcess = NULL;
    HANDLE m_hStdinWrite = NULL;
    HANDLE m_hStdoutRead = NULL;
    std::mutex m_mutex;
    std::atomic<bool> m_isRunning{ false };
    std::atomic<bool> m_isBusy{ false };
    std::wstring m_pythonPath;

    bool IsProcessAlive() const {
        if (!m_hProcess) return false;
        DWORD code = 0;
        if (GetExitCodeProcess(m_hProcess, &code)) {
            return (code == STILL_ACTIVE);
        }
        return false;
    }

    void CleanupProcess() {
        if (m_hProcess) {
            if (IsProcessAlive()) {
                TerminateProcess(m_hProcess, 0);
            }
            CloseHandle(m_hProcess);
            m_hProcess = NULL;
        }
        if (m_hStdinWrite) {
            CloseHandle(m_hStdinWrite);
            m_hStdinWrite = NULL;
        }
        if (m_hStdoutRead) {
            CloseHandle(m_hStdoutRead);
            m_hStdoutRead = NULL;
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
            "import sys, base64, traceback\n\n"
            "_ns = {'__name__': '__main__'}\n\n"
            "while True:\n"
            "    try:\n"
            "        line = sys.stdin.readline()\n"
            "        if not line:\n"
            "            break\n"
            "        line = line.strip()\n"
            "        if line in ('EXIT', 'QUIT'):\n"
            "            break\n"
            "        if not line.startswith('EXEC_B64 '):\n"
            "            continue\n"
            "        b64_str = line[9:]\n"
            "        code_str = base64.b64decode(b64_str.encode('ascii')).decode('utf-8', errors='replace')\n\n"
            "        exit_code = 0\n"
            "        try:\n"
            "            compiled = compile(code_str, '<cell>', 'exec')\n"
            "            exec(compiled, _ns)\n"
            "        except Exception:\n"
            "            traceback.print_exc()\n"
            "            exit_code = 1\n"
            "        except SystemExit as se:\n"
            "            exit_code = se.code if isinstance(se.code, int) else 1\n\n"
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
