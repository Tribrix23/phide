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
#include <vector>
#include <cstdint>

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

    /**
     * @brief Broadcasts a live-reload notification over active WebSocket & SSE connections.
     */
    void BroadcastReload(const std::wstring& changedRelPath = L"");
    void SetLiveReloadEnabled(bool enabled) noexcept { m_liveReloadEnabled.store(enabled); }
    [[nodiscard]] bool IsLiveReloadEnabled() const noexcept { return m_liveReloadEnabled.load(); }
    void SetSpaFallback(bool enabled) noexcept { m_spaFallback.store(enabled); }
    [[nodiscard]] bool IsSpaFallback() const noexcept { return m_spaFallback.load(); }
    [[nodiscard]] uint64_t GetReloadRevision() const noexcept { return m_reloadRevision.load(); }
    [[nodiscard]] size_t GetConnectedClientCount() const;

    struct PhpRuntimeEntry {
        std::wstring version;
        std::wstring exePath;
        std::wstring iniPath;
    };
    static std::vector<PhpRuntimeEntry> DiscoverInstalledPhpVersions(const std::wstring& phideRoot);
    static std::wstring GenerateNginxVhostPreset(const std::wstring& presetName,
                                                 const std::wstring& serverName,
                                                 int port,
                                                 const std::wstring& docRoot,
                                                 int phpFpmPort = 9000);
    static std::string BuildWebSocketAcceptKey(const std::string& clientKey);
    static std::vector<uint8_t> EncodeWebSocketTextFrame(const std::string& payload);
    static bool TryDecodeWebSocketTextFrame(const std::vector<uint8_t>& frameBytes, std::string& outPayload);

private:
    void ServerWorker();
    void WatcherWorker();
    void HandleClient(SOCKET clientSock);
    std::string GetMimeType(const std::filesystem::path& path) const;
    static std::string InjectLiveReloadScript(std::string html);
    void DiscoverNetworkIp();

    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_liveReloadEnabled{true};
    std::atomic<bool> m_spaFallback{false};
    std::atomic<uint64_t> m_reloadRevision{1};
    SOCKET m_listenSock = INVALID_SOCKET;
    int m_port = 5500;
    std::wstring m_rootDir;
    std::wstring m_defaultFile = L"index.html";
    std::wstring m_localIp = L"127.0.0.1";

    std::thread m_workerThread;
    std::thread m_watcherThread;
    HANDLE m_watchStopEvent = nullptr;
    mutable std::mutex m_mutex;
    mutable std::mutex m_clientsMutex;
    std::vector<SOCKET> m_sseClients;
    std::vector<SOCKET> m_wsClients;
    bool m_wsaInitialized = false;
};

} // namespace phide::server
