/**
 * @file DapClient.hpp
 * @brief Native Debug Adapter Protocol (DAP) & Multi-Backend Step Debugger (#26 & #27):
 *        - .phide/launch.json (with .vscode/launch.json fallback) configuration parser & macro expansion
 *        - RFC DAP Content-Length wire frame encoder & incremental decoder
 *        - Real backends: Python bdb JSON adapter, GDB/MI (C/C++), Node.js CDP (--inspect), PHP Xdebug DBGp
 *        - Conditional breakpoints, hit counts, logpoints, exception breakpoints, expandable Variables, Watch, Call Stack
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace phide::dev {

enum class DapAdapterType {
    None = 0,
    Python,
    GdbMi,
    NodeInspect,
    PhpXdebug,
    ExternalDap
};

struct LaunchConfig {
    std::wstring name;
    std::wstring type;           ///< "python", "node", "cppdbg", "gdb", "lldb", "php", "dap"
    std::wstring request = L"launch"; ///< "launch" | "attach"
    std::wstring program;
    std::vector<std::wstring> args;
    std::wstring cwd;
    bool stopOnEntry = false;
    int port = 0;                ///< Attach/listen port (e.g., 9229 Node, 9003 Xdebug)
    std::wstring preLaunchTask;
    std::wstring miDebuggerPath;
    std::wstring adapterCommand; ///< External DAP executable (e.g., "debugpy-adapter", "lldb-dap")
    std::vector<std::wstring> adapterArgs;
};

struct DapBreakpoint {
    std::wstring filePath;
    size_t line1Based = 1;
    bool enabled = true;
    std::wstring condition;
    int hitCondition = 0;        ///< Break only when hitCount >= hitCondition (0 = always)
    int hitCount = 0;
    std::wstring logMessage;     ///< Logpoint template, e.g. "x={x}" (does not pause when non-empty)
    bool verified = false;
};

struct DapVariable {
    std::wstring name;
    std::wstring value;
    std::wstring type;
    int variablesReference = 0;  ///< > 0 if expandable via RequestVariableChildren
    bool expanded = false;
    std::vector<DapVariable> children;
};

struct DapScope {
    std::wstring name;           ///< "Locals", "Globals", "Closure"
    int variablesReference = 0;
    bool expanded = true;
    std::vector<DapVariable> variables;
};

struct DapStackFrame {
    int id = 0;
    std::wstring name;
    std::wstring sourcePath;
    int line1Based = 1;
    int col1Based = 1;
};

struct DapWatchItem {
    std::wstring expression;
    std::wstring value;
    std::wstring type;
    bool hasError = false;
};

struct DapExceptionOptions {
    bool breakOnRaised = false;
    bool breakOnUncaught = true;
};

struct DapSessionSnapshot {
    DapAdapterType adapter = DapAdapterType::None;
    bool isRunning = false;
    bool isPaused = false;
    std::wstring stoppedReason;
    std::wstring stoppedText;
    std::wstring pausedFile;
    int pausedLine1Based = 0;
    int selectedFrameId = 0;
    std::vector<DapStackFrame> callStack;
    std::vector<DapScope> scopes;
    std::vector<DapWatchItem> watches;
    std::vector<DapBreakpoint> breakpoints;
    std::vector<std::wstring> loadedScripts;
    DapExceptionOptions exceptions;
};

// =========================================================================
// RFC DAP Wire Framing (Content-Length: N\r\n\r\n<JSON>)
// =========================================================================

class DapFrameDecoder {
public:
    void Feed(const char* data, size_t len);
    bool PopMessage(std::string& outJson);
    void Clear();

private:
    std::string m_buffer;
};

[[nodiscard]] std::string EncodeDapFrame(const std::string& jsonPayload);

// =========================================================================
// Launch Config & Expression Helpers
// =========================================================================

[[nodiscard]] std::vector<LaunchConfig> ParseLaunchJson(
    const std::wstring& jsonContent,
    const std::wstring& workspaceRoot,
    const std::wstring& activeFilePath = L"");

[[nodiscard]] std::vector<LaunchConfig> LoadWorkspaceLaunchConfigs(
    const std::wstring& workspaceRoot,
    const std::wstring& activeFilePath = L"");

// =========================================================================
// Multi-Backend Interactive Step Debugger Client
// =========================================================================

class DapClient {
public:
    DapClient();
    ~DapClient();

    DapClient(const DapClient&) = delete;
    DapClient& operator=(const DapClient&) = delete;

    // Callbacks invoked from background thread (keep UI handlers thread-safe / InvalidateRect)
    std::function<void(const std::wstring& text)> onOutput;
    std::function<void(const std::wstring& filePath, int line1Based)> onPaused;
    std::function<void()> onContinued;
    std::function<void()> onStateChanged;
    std::function<void(int exitCode)> onTerminated;

    static DapAdapterType DetectAdapterForFile(const std::wstring& filePath) noexcept;
    static DapAdapterType AdapterFromTypeString(const std::wstring& typeStr) noexcept;

    bool StartSession(
        const LaunchConfig& cfg,
        const std::vector<DapBreakpoint>& initialBreakpoints,
        const DapExceptionOptions& exceptions = {},
        const std::wstring& pythonExe = L"python");

    void StopSession();

    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(); }
    [[nodiscard]] bool IsPaused() const noexcept { return m_paused.load(); }

    void Continue();
    void Pause();
    void StepOver();
    void StepInto();
    void StepOut();
    void SelectStackFrame(int frameId);

    void UpdateBreakpoints(const std::vector<DapBreakpoint>& bps);
    void SetExceptionOptions(const DapExceptionOptions& opts);
    void ExpandVariableChildren(int variablesReference);

    void AddWatch(const std::wstring& expression);
    void RemoveWatch(size_t index);
    void RefreshWatches();
    std::wstring EvaluateBlocking(const std::wstring& expression, int timeoutMs = 2500);

    [[nodiscard]] DapSessionSnapshot GetSnapshot() const;

    // Exposed helper for testing conditional / hit-count / logpoint logic deterministically
    static bool ShouldPauseAtBreakpoint(
        DapBreakpoint& bp,
        const std::function<std::wstring(const std::wstring&)>& evalFn,
        std::wstring& outLogpointMessage);

    // Exposed parsers for unit testing multi-backend parity deterministically
    static void ParseGdbMiFrames(const std::string& miRecord, std::vector<DapStackFrame>& outFrames);
    static void ParseGdbMiVariables(const std::string& miRecord, std::vector<DapVariable>& outVars);
    static void ParsePhpDbgpStack(const std::string& stackXml,
                                  const std::wstring& fallbackProgram,
                                  std::vector<DapStackFrame>& outFrames);
    static void ParsePhpDbgpContextVars(const std::string& ctxXml,
                                        std::vector<DapVariable>& outVars,
                                        const std::function<int(const std::string&)>& allocVarRef = nullptr);
    static std::wstring DecodePhpDbgpEvalResult(const std::string& evalXml);
    static void ParseNodeCdpFrames(const std::string& pausedMsgJson,
                                   const std::wstring& fallbackProgram,
                                   const std::vector<std::pair<std::string, std::wstring>>& scriptIdToPath,
                                   std::vector<DapStackFrame>& outFrames,
                                   std::vector<std::string>& outCallFrameIds,
                                   std::vector<std::string>& outScopeObjIds);
    static void ParseNodeCdpProperties(const std::string& propsJson,
                                       std::vector<DapVariable>& outVars,
                                       const std::function<int(const std::string&)>& allocVarRef = nullptr);

private:
    void WorkerLoop(LaunchConfig cfg, std::wstring pythonExe);
    void RunPythonBdbSession(const LaunchConfig& cfg, const std::wstring& pythonExe);
    void RunGdbMiSession(const LaunchConfig& cfg);
    void RunNodeCdpSession(const LaunchConfig& cfg);
    void RunPhpDbGpSession(const LaunchConfig& cfg);
    void RunExternalDapSession(const LaunchConfig& cfg);

    bool HandleBreakpointEvaluationLocked(
        const std::wstring& filePath,
        int line1Based,
        const std::function<std::wstring(const std::wstring&)>& evalFn);

    static bool AssignVariableChildrenRecursive(
        std::vector<DapVariable>& vars,
        int targetVarRef,
        std::vector<DapVariable>&& children,
        int depth = 0);

    void WritePipeLine(const std::string& line);
    void QueueControlCmd(const std::string& cmd, const std::wstring& arg = L"", int intArg = 0);

    mutable std::mutex m_mutex;
    DapSessionSnapshot m_state;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_stopRequested{false};

    void* m_hChildProc = nullptr;
    void* m_hStdinWrite = nullptr;
    void* m_hStdoutRead = nullptr;
    unsigned long m_childPid = 0;
    std::wstring m_tempBridgeFile;
    std::wstring m_tempBridgeCfgFile;
    uintptr_t m_sockHandle = static_cast<uintptr_t>(-1);

    std::mutex m_cmdMutex;
    std::condition_variable m_cmdCv;
    struct ControlMsg {
        std::string cmd;
        std::wstring arg;
        int intArg = 0;
    };
    std::queue<ControlMsg> m_cmdQueue;

    std::mutex m_evalCallMutex;
    std::mutex m_evalMutex;
    std::condition_variable m_evalCv;
    bool m_evalReady = false;
    std::wstring m_evalResult;

    std::thread m_workerThread;
};

} // namespace phide::dev
