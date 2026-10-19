/**
 * @file LspClient.cpp
 * @brief Implementation of Language Server Protocol client communication.
 */

#include "phide/lsp/LspClient.hpp"

namespace phide::lsp {

LspClient::LspClient() = default;

LspClient::~LspClient() {
    Shutdown();
}

bool LspClient::StartServer(const std::wstring& serverExecutable, const std::wstring& workspaceUri) {
    (void)workspaceUri;
    m_currentServer = serverExecutable;
    // Note: In full implementation, Win32 CreateProcess with anonymous pipes for stdin/stdout is invoked.
    m_isRunning = true;
    return true;
}

void LspClient::Shutdown() {
    m_isRunning = false;
}

void LspClient::NotifyDidOpen(const std::wstring& fileUri, const std::wstring& languageId, const std::wstring& text) {
    (void)fileUri;
    (void)languageId;
    (void)text;
    // Transmit JSON-RPC "textDocument/didOpen" payload over stdin pipe
}

std::vector<CompletionItem> LspClient::RequestCompletions(const std::wstring& fileUri, size_t line, size_t col) {
    (void)fileUri;
    (void)line;
    (void)col;

    // Default mock proposals for demonstration
    return {
        { L"echo", L"void echo(string $arg)", L"echo \"$0\";", 14 },
        { L"var_dump", L"void var_dump(mixed $value)", L"var_dump($0);", 3 },
        { L"print_r", L"mixed print_r(mixed $value)", L"print_r($0);", 3 }
    };
}

} // namespace phide::lsp
