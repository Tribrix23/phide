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
    std::wstring source;
    std::wstring code;
};

enum class TextDocumentSyncKind {
    None = 0,
    Full = 1,
    Incremental = 2
};

struct HoverResult {
    std::wstring markdown;
    bool isValid = false;
    size_t rangeLine = 0;
    size_t rangeStartCol = 0;
    size_t rangeEndCol = 0;
};

struct SignatureParamInfo {
    std::wstring label;
    std::wstring documentation;
};

struct SignatureInfo {
    std::wstring label;
    std::wstring documentation;
    std::vector<SignatureParamInfo> parameters;
    int activeParameter = -1;
};

struct SignatureHelpResult {
    std::vector<SignatureInfo> signatures;
    int activeSignature = 0;
    int activeParameter = 0;
    bool isValid = false;
};

/**
 * @struct LspTextEdit
 * @brief Text edit range returned by textDocument/formatting and rangeFormatting.
 */
struct LspTextEdit {
    size_t startLine = 0;
    size_t startCol = 0;
    size_t endLine = 0;
    size_t endCol = 0;
    std::wstring newText;
};

struct WorkspaceEditResult {
    std::vector<std::pair<std::wstring, std::vector<LspTextEdit>>> fileChanges;
};

struct CodeActionItem {
    std::wstring title;
    std::wstring kind;
    std::wstring diagnosticMessage;
    WorkspaceEditResult edit;
    bool isPreferred = false;
    std::string rawJson;
};

struct InlayHintItem {
    size_t line = 0;
    size_t col = 0;
    std::wstring label;
    int kind = 1; // 1 = Type, 2 = Parameter
    std::wstring tooltip;
    bool paddingLeft = false;
    bool paddingRight = false;
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

struct DocumentSymbolItem {
    std::wstring name;
    std::wstring detail;
    int kind = 12;
    size_t line = 0;
    size_t col = 0;
    size_t endLine = 0;
    int depth = 0;
};

struct CallHierarchyItem {
    std::wstring name;
    std::wstring detail;
    int kind = 12;
    std::wstring filePath;
    size_t line = 0;
    size_t col = 0;
    std::string rawItemJson;
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
     * @brief Requests whole-document formatting edits via textDocument/formatting.
     */
    bool RequestFormatting(const std::wstring& fileUri, int tabSize, bool insertSpaces, std::vector<LspTextEdit>& outEdits);

    /**
     * @brief Requests range formatting edits via textDocument/rangeFormatting.
     */
    bool RequestRangeFormatting(const std::wstring& fileUri, size_t startLine, size_t startCol,
                                size_t endLine, size_t endCol, int tabSize, bool insertSpaces,
                                std::vector<LspTextEdit>& outEdits);

    bool RequestDefinitionLocations(const std::wstring& fileUri, size_t line, size_t col,
                                    std::vector<Location>& outLocations);
    bool RequestTypeDefinition(const std::wstring& fileUri, size_t line, size_t col,
                               std::vector<Location>& outLocations);
    bool RequestImplementation(const std::wstring& fileUri, size_t line, size_t col,
                               std::vector<Location>& outLocations);
    bool RequestDocumentSymbols(const std::wstring& fileUri, std::vector<DocumentSymbolItem>& outSymbols);
    bool RequestReferences(const std::wstring& fileUri, size_t line, size_t col,
                           bool includeDeclaration, std::vector<Location>& outLocations);
    bool PrepareRename(const std::wstring& fileUri, size_t line, size_t col,
                       size_t& outStartCol, size_t& outEndCol, std::wstring& outPlaceholder);
    bool RequestRename(const std::wstring& fileUri, size_t line, size_t col,
                       const std::wstring& newName, WorkspaceEditResult& outEdit);
    bool PrepareCallHierarchy(const std::wstring& fileUri, size_t line, size_t col,
                              std::vector<CallHierarchyItem>& outItems);
    bool RequestIncomingCalls(const CallHierarchyItem& item, std::vector<CallHierarchyItem>& outCallers);
    bool RequestOutgoingCalls(const CallHierarchyItem& item, std::vector<CallHierarchyItem>& outCallees);

    bool RequestHover(const std::wstring& fileUri, size_t line, size_t col, HoverResult& outHover);
    bool RequestSignatureHelp(const std::wstring& fileUri, size_t line, size_t col, SignatureHelpResult& outSigHelp);
    bool RequestCodeActions(const std::wstring& fileUri, size_t startLine, size_t startCol,
                            size_t endLine, size_t endCol,
                            const std::vector<Diagnostic>& diagnostics,
                            std::vector<CodeActionItem>& outActions);
    bool ResolveCodeAction(CodeActionItem& action);
    bool RequestInlayHints(const std::wstring& fileUri, size_t startLine, size_t endLine,
                           std::vector<InlayHintItem>& outHints);

    /**
     * @brief Configures optional custom language server executable paths (keyed by language family e.g. "typescript", "python", "php", "cpp").
     */
    void SetCustomServerPaths(const std::unordered_map<std::wstring, std::wstring>& customPaths);
    void SetCustomServerPath(const std::wstring& languageIdOrFamily, const std::wstring& executablePath);

    [[nodiscard]] TextDocumentSyncKind GetNegotiatedSyncKind() const noexcept {
        return m_syncKind.load();
    }

    /**
     * @brief Computes minimal UTF-16 LSP incremental change delta between oldText and newText.
     */
    static bool ComputeIncrementalDelta(const std::wstring& oldText, const std::wstring& newText,
                                        size_t& outStartLine, size_t& outStartChar,
                                        size_t& outEndLine, size_t& outEndChar,
                                        std::wstring& outReplacement);

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
    std::atomic<TextDocumentSyncKind> m_syncKind{TextDocumentSyncKind::Full};

    mutable std::mutex m_stateMutex;
    std::recursive_mutex m_lifecycleMutex;
    std::wstring m_currentServer;
    std::wstring m_currentLanguageId;
    std::wstring m_workspaceRoot;
    std::unordered_map<std::wstring, int> m_docVersions;
    std::unordered_map<std::wstring, std::wstring> m_docSyncedTexts;
    std::unordered_map<std::wstring, std::wstring> m_customServerPaths;
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

/**
 * @class LspManager
 * @brief Multi-server LSP lifecycle coordinator running concurrent language servers per language family and workspace.
 */
class LspManager {
public:
    using DiagnosticsCallback = LspClient::DiagnosticsCallback;

    LspManager() = default;
    ~LspManager();

    LspManager(const LspManager&) = delete;
    LspManager& operator=(const LspManager&) = delete;

    [[nodiscard]] static std::wstring NormalizeLanguageFamily(const std::wstring& langOrExtOrPath);

    void SetCustomServerPaths(const std::unordered_map<std::wstring, std::wstring>& customPaths);
    void SetCustomServerPath(const std::wstring& languageIdOrFamily, const std::wstring& executablePath);
    void SetDiagnosticsCallback(DiagnosticsCallback cb);
    void ResetDiscoveryCache();

    bool EnsureServerForLanguage(const std::wstring& languageId, const std::wstring& workspaceRoot);
    void Shutdown();

    void NotifyDidOpen(const std::wstring& fileUri, const std::wstring& languageId, const std::wstring& text);
    void NotifyDidChange(const std::wstring& fileUri, const std::wstring& text);
    void NotifyDidSave(const std::wstring& fileUri, const std::wstring& text);
    void NotifyDidClose(const std::wstring& fileUri);

    [[nodiscard]] std::vector<CompletionItem> RequestCompletions(const std::wstring& fileUri, size_t line, size_t col);
    bool RequestDefinition(const std::wstring& fileUri, size_t line, size_t col, Location& outLocation);
    bool RequestDefinitionLocations(const std::wstring& fileUri, size_t line, size_t col, std::vector<Location>& outLocations);
    bool RequestTypeDefinition(const std::wstring& fileUri, size_t line, size_t col, std::vector<Location>& outLocations);
    bool RequestImplementation(const std::wstring& fileUri, size_t line, size_t col, std::vector<Location>& outLocations);
    bool RequestDocumentSymbols(const std::wstring& fileUri, std::vector<DocumentSymbolItem>& outSymbols);
    bool RequestReferences(const std::wstring& fileUri, size_t line, size_t col, bool includeDeclaration, std::vector<Location>& outLocations);
    bool PrepareRename(const std::wstring& fileUri, size_t line, size_t col, size_t& outStartCol, size_t& outEndCol, std::wstring& outPlaceholder);
    bool RequestRename(const std::wstring& fileUri, size_t line, size_t col, const std::wstring& newName, WorkspaceEditResult& outEdit);
    bool PrepareCallHierarchy(const std::wstring& fileUri, size_t line, size_t col, std::vector<CallHierarchyItem>& outItems);
    bool RequestIncomingCalls(const CallHierarchyItem& item, std::vector<CallHierarchyItem>& outCallers);
    bool RequestOutgoingCalls(const CallHierarchyItem& item, std::vector<CallHierarchyItem>& outCallees);
    bool RequestFormatting(const std::wstring& fileUri, int tabSize, bool insertSpaces, std::vector<LspTextEdit>& outEdits);
    bool RequestRangeFormatting(const std::wstring& fileUri, size_t startLine, size_t startCol,
                                size_t endLine, size_t endCol, int tabSize, bool insertSpaces,
                                std::vector<LspTextEdit>& outEdits);
    bool RequestHover(const std::wstring& fileUri, size_t line, size_t col, HoverResult& outHover);
    bool RequestSignatureHelp(const std::wstring& fileUri, size_t line, size_t col, SignatureHelpResult& outSigHelp);
    bool RequestCodeActions(const std::wstring& fileUri, size_t startLine, size_t startCol,
                            size_t endLine, size_t endCol,
                            const std::vector<Diagnostic>& diagnostics,
                            std::vector<CodeActionItem>& outActions);
    bool ResolveCodeAction(const std::wstring& fileUri, CodeActionItem& action);
    bool RequestInlayHints(const std::wstring& fileUri, size_t startLine, size_t endLine,
                           std::vector<InlayHintItem>& outHints);

    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] bool IsRunningForFile(const std::wstring& fileUriOrPath) const;
    [[nodiscard]] bool IsRunningForLanguage(const std::wstring& languageId) const;
    [[nodiscard]] std::wstring GetCurrentLanguageId() const;
    [[nodiscard]] size_t GetActiveServerCount() const;
    [[nodiscard]] std::vector<std::wstring> GetRunningLanguages() const;

    // Exposed for concurrency routing & unit tests: registers/retrieves or associates client for a file URI
    std::shared_ptr<LspClient> GetClientForFile(const std::wstring& fileUri) const;
    std::shared_ptr<LspClient> GetOrCreateSlotForTesting(const std::wstring& languageId, const std::wstring& workspaceRoot);

private:
    static std::wstring MakeKey(const std::wstring& family, const std::wstring& workspaceRoot);
    std::shared_ptr<LspClient> ResolveClientForDocument(const std::wstring& fileUri) const;

    mutable std::mutex m_mutex;
    std::unordered_map<std::wstring, std::shared_ptr<LspClient>> m_clients;
    std::unordered_map<std::wstring, std::wstring> m_uriToClientKey;
    std::unordered_map<std::wstring, std::wstring> m_customServerPaths;
    std::wstring m_lastLanguageId;
    std::wstring m_lastWorkspaceRoot;
    DiagnosticsCallback m_diagnosticsCallback;
};

} // namespace phide::lsp
