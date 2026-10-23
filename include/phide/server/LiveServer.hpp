#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")

namespace phide::server {

/**
 * @brief Lightweight, ultra-fast native HTTP Live Server for web development.
 *
 * Serves HTML, CSS, JavaScript, and static assets from the active project workspace
 * with automatic port selection, local and network IP discovery, and zero external dependencies.
 */
class LiveServer {
public:
    LiveServer();
    ~LiveServer();

    /**
     * @brief Starts the HTTP live server serving the specified root directory.
     * @param rootDir Base filesystem directory to serve files from.
     * @param defaultHtmlFile Default HTML document served on '/' (e.g. "index.html").
     * @param preferredPort Port to attempt binding to (defaults to 5500).
     * @return True if server successfully bound and listening.
     */
    bool Start(const std::wstring& rootDir, const std::wstring& defaultHtmlFile = L"index.html", int preferredPort = 5500);

    /**
     * @brief Stops the running HTTP live server and closes listener sockets.
     */
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept { return m_isRunning.load(); }
    [[nodiscard]] int GetPort() const noexcept { return m_port; }
    [[nodiscard]] const std::wstring& GetRootDir() const noexcept { return m_rootDir; }
    [[nodiscard]] const std::wstring& GetDefaultFile() const noexcept { return m_defaultFile; }
    [[nodiscard]] const std::wstring& GetLocalIp() const noexcept { return m_localIp; }

    [[nodiscard]] std::wstring GetLocalUrl(const std::wstring& relPath = L"") const;
    [[nodiscard]] std::wstring GetNetworkUrl(const std::wstring& relPath = L"") const;

private:
    void ServerWorker();
    void HandleClient(SOCKET clientSock);
    std::string GetMimeType(const std::filesystem::path& path) const;
    void DiscoverNetworkIp();

    std::atomic<bool> m_isRunning{false};
    SOCKET m_listenSock = INVALID_SOCKET;
    int m_port = 5500;
    std::wstring m_rootDir;
    std::wstring m_defaultFile = L"index.html";
    std::wstring m_localIp = L"127.0.0.1";

    std::thread m_workerThread;
    mutable std::mutex m_mutex;
    bool m_wsaInitialized = false;
};

} // namespace phide::server
