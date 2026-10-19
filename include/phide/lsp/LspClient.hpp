/**
 * @file LspClient.hpp
 * @brief Language Server Protocol (LSP) client interface for PHP and C++ language intelligence.
 * 
 * Manages JSON-RPC 2.0 communication over standard input/output pipes with language
 * servers (e.g. clangd for C++, intelephense or phpactor for PHP).
 */

#pragma once

#include <string>
#include <vector>
#include <functional>

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
 * @class LspClient
 * @brief Orchestrates language server process lifecycle and request/response dispatch.
 */
class LspClient {
public:
    LspClient();
    ~LspClient();

    /**
     * @brief Launches the language server process for the requested language.
     * @param serverExecutable Path or command name (e.g. "clangd.exe", "intelephense.cmd").
     * @param workspaceUri URI of the root project folder.
     * @return True if process successfully spawned.
     */
    bool StartServer(const std::wstring& serverExecutable, const std::wstring& workspaceUri);

    /**
     * @brief Stops the language server.
     */
    void Shutdown();

    /**
     * @brief Notifies server of an opened document.
     * @param fileUri Document URI.
     * @param languageId "php" or "cpp".
     * @param text File contents.
     */
    void NotifyDidOpen(const std::wstring& fileUri, const std::wstring& languageId, const std::wstring& text);

    /**
     * @brief Requests code completion proposals at the given position.
     * @param fileUri Document URI.
     * @param line 0-based line number.
     * @param col 0-based character column.
     * @return List of CompletionItem proposals.
     */
    [[nodiscard]] std::vector<CompletionItem> RequestCompletions(const std::wstring& fileUri, size_t line, size_t col);

    /**
     * @brief Checks if the language server is currently running.
     */
    [[nodiscard]] bool IsRunning() const noexcept { return m_isRunning; }

private:
    bool m_isRunning = false;
    std::wstring m_currentServer;
};

} // namespace phide::lsp
