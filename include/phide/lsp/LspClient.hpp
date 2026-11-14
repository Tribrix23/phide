/**
 * @file LspClient.hpp
 * @brief Language Server Protocol (LSP) client interface for PHP, Python, JS/TS, and C++ intelligence.
 *
 * Manages JSON-RPC 2.0 communication over standard input/output Win32 anonymous pipes
 * with external language servers (e.g. phpactor, intelephense, pylsp, pyright, clangd, typescript-language-server).
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace phide::lsp {

/**
 * @struct Diagnostic
 * @brief Represents a compiler warning or syntax error reported by the language server.
 */
struct Diagnostic {
    size_t line = 0;
    size_t col = 0;
    size_t length = 0;
    std::wstring message;
    int severity = 1; // 1 = Error, 2 = Warning, 3 = Info, 4 = Hint
};

/**
 * @struct CompletionItem
 * @brief Code completion suggestion.
 */
struct CompletionItem {
    std::wstring label;
    std::wstring detail;
    std::wstring insertText;
    int kind = 1;
};

/**
 * @struct Location
 * @brief Source code location returned by textDocument/definition.
 */
struct Location {
    std::wstring filePath;
    size_t line = 0;
    size_t col = 0;
};

/**
 * @class LspClient
 * @brief Orchestrates language server process lifecycle and JSON-RPC 2.0 request/response dispatch.
 */
class LspClient {
public:
    using DiagnosticsCallback = std::function<void(const std::wstring& filePath, std::vector<Diagnostic>)>;

    LspClient();
    ~LspClient();

    LspClient(const LspClient&) = delete;
    LspClient& operator=(const LspClient&) = delete;

    /**
     * @brief Launches the language server process for the requested executable and workspace URI.
     * @param serverExecutable Path or command name (e.g. "clangd.exe", "intelephense.cmd").
     * @param workspaceUri URI or filesystem path of the root project folder.
     * @return True if process successfully spawned and LSP initialize handshake succeeded.
     */
    bool StartServer(const std::wstring& serverExecutable, const std::wstring& workspaceUri);

    /**
     * @brief Auto-discovers and ensures a real LSP server is running for the given language ID.
     */
    bool EnsureServerForLanguage(const std::wstring& languageId, const std::wstring& workspaceRoot);

    /**
     * @brief Stops the language server cleanly (shutdown + exit, closes pipes/job handles, joins reader thread).
     */
    void Shutdown();

    /**
     * @brief Notifies server of an opened document.
     * @param fileUri Document path or file:// URI.
     * @param languageId "php", "python", "cpp", "javascript", "typescript", etc.
     * @param text File contents.
     */
    void NotifyDidOpen(const std::wstring& fileUri, const std::wstring& languageId, const std::wstring& text);

    /**
     * @brief Notifies server of updated document text (full sync).
     */
    void NotifyDidChange(const std::wstring& fileUri, const std::wstring& text);

    /**
     * @brief Notifies server that a document was saved.
     */
    void NotifyDidSave(const std::wstring& fileUri, const std::wstring& text);

    /**
     * @brief Notifies server that a document was closed.
     */
    void NotifyDidClose(const std::wstring& fileUri);

    /**
     * @brief Requests code completion proposals at the given 0-based position.
     */
    [[nodiscard]] std::vector<CompletionItem> RequestCompletions(const std::wstring& fileUri, size_t line, size_t col);

    /**
     * @brief Requests go-to-definition target location for the symbol at the given 0-based position.
     */
    bool RequestDefinition(const std::wstring& fileUri, size_t line, size_t col, Location& outLocation);

    /**
     * @brief Registers a callback invoked on background reader thread when textDocument/publishDiagnostics arrives.
     */
    void SetDiagnosticsCallback(DiagnosticsCallback cb);

    /**
     * @brief Clears cached negative discovery results (e.g. when switching workspaces).
     */
    void ResetDiscoveryCache();

    [[nodiscard]] static std::wstring PathToFileUri(const std::wstring& path);
    [[nodiscard]] static std::wstring FileUriToPath(const std::wstring& uri);

    /**
     * @brief Checks if the language server is currently running and initialized.
     */
    [[nodiscard]] bool IsRunning() const noexcept { return m_isRunning.load(); }

    [[nodiscard]] std::wstring GetCurrentLanguageId() const {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_currentLanguageId;
    }

private:
    struct PendingRequest {
        std::mutex mtx;
        std::condition_variable cv;
        bool done = false;
        std::string responsePayload;
    };

    bool LaunchProcessAndInitialize(const std::wstring& commandLine, const std::wstring& workingDir, const std::wstring& rootUri);
    bool SendJsonRpcFrame(const std::string& jsonBody);
    bool SendRequestAndWait(const std::string& method, const std::string& paramsJson, std::string& outResponseJson, int timeoutMs);
    void SendNotification(const std::string& method, const std::string& paramsJson);
    void ReaderThreadLoop(void* hReadPipe);
    void HandleIncomingJsonMessage(const std::string& jsonText);

    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<int> m_nextRequestId{1};

    mutable std::mutex m_stateMutex;
    std::recursive_mutex m_lifecycleMutex;
    std::wstring m_currentServer;
    std::wstring m_currentLanguageId;
    std::wstring m_workspaceRoot;
    std::unordered_map<std::wstring, int> m_docVersions;
    std::unordered_set<std::wstring> m_failedDiscoveryKeys;
    DiagnosticsCallback m_diagnosticsCallback;

    std::mutex m_writeMutex;
    std::mutex m_pendingMutex;
    std::unordered_map<int, std::shared_ptr<PendingRequest>> m_pendingRequests;

    // Opaque Win32 HANDLEs stored as void* to keep header lightweight
    void* m_hChildStdinWrite = nullptr;
    void* m_hChildStdoutRead = nullptr;
    void* m_hChildProcess = nullptr;
    void* m_hJobObject = nullptr;
    std::thread m_readerThread;
};

} // namespace phide::lsp
