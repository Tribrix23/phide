#include "phide/server/LiveServer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

namespace phide::server {

LiveServer::LiveServer() {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
        m_wsaInitialized = true;
    }
    DiscoverNetworkIp();
}

LiveServer::~LiveServer() {
    Stop();
    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
    }
}

void LiveServer::DiscoverNetworkIp() {
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(hostname, nullptr, &hints, &res) == 0 && res != nullptr) {
            for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next) {
                sockaddr_in* sockaddr_ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
                char ipStr[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &(sockaddr_ipv4->sin_addr), ipStr, INET_ADDRSTRLEN);
                std::string ip(ipStr);
                if (ip != "127.0.0.1" && ip.find("169.254.") != 0 && !ip.empty()) {
                    wchar_t wbuf[INET_ADDRSTRLEN] = {0};
                    MultiByteToWideChar(CP_UTF8, 0, ipStr, -1, wbuf, INET_ADDRSTRLEN);
                    m_localIp = wbuf;
                    break;
                }
            }
            freeaddrinfo(res);
        }
    }
    if (m_localIp.empty()) {
        m_localIp = L"127.0.0.1";
    }
}

std::wstring LiveServer::GetLocalUrl(const std::wstring& relPath) const {
    std::wstring path = relPath.empty() ? m_defaultFile : relPath;
    std::wstring url = L"http://localhost:" + std::to_wstring(m_port);
    if (!path.empty()) {
        if (path.front() != L'/') url += L"/";
        url += path;
    }
    return url;
}

std::wstring LiveServer::GetNetworkUrl(const std::wstring& relPath) const {
    std::wstring path = relPath.empty() ? m_defaultFile : relPath;
    std::wstring url = L"http://" + m_localIp + L":" + std::to_wstring(m_port);
    if (!path.empty()) {
        if (path.front() != L'/') url += L"/";
        url += path;
    }
    return url;
}

bool LiveServer::Start(const std::wstring& rootDir, const std::wstring& defaultHtmlFile, int preferredPort) {
    Stop();

    std::error_code ec;
    std::filesystem::path root(rootDir);
    if (!std::filesystem::exists(root, ec)) {
        root = std::filesystem::current_path();
    }
    m_rootDir = root.wstring();
    m_defaultFile = defaultHtmlFile.empty() ? L"index.html" : defaultHtmlFile;

    m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSock == INVALID_SOCKET) {
        return false;
    }

    // Reuse address
    int opt = 1;
    setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    int port = preferredPort;
    bool bound = false;
    for (int attempt = 0; attempt < 30; ++attempt) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<u_short>(port));

        if (bind(m_listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            bound = true;
            m_port = port;
            break;
        }
        port++;
    }

    if (!bound || listen(m_listenSock, SOMAXCONN) != 0) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
        return false;
    }

    m_isRunning = true;
    m_workerThread = std::thread(&LiveServer::ServerWorker, this);
    if (!m_watchStopEvent) {
        m_watchStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    } else {
        ResetEvent(m_watchStopEvent);
    }
    m_watcherThread = std::thread(&LiveServer::WatcherWorker, this);
    return true;
}

void LiveServer::Stop() {
    if (!m_isRunning.exchange(false)) {
        if (m_watchStopEvent) {
            SetEvent(m_watchStopEvent);
        }
        if (m_watcherThread.joinable()) {
            m_watcherThread.join();
        }
        if (m_watchStopEvent) {
            CloseHandle(m_watchStopEvent);
            m_watchStopEvent = nullptr;
        }
        return;
    }

    if (m_watchStopEvent) {
        SetEvent(m_watchStopEvent);
    }

    {
        std::lock_guard<std::mutex> cl(m_clientsMutex);
        for (SOCKET s : m_sseClients) {
            if (s != INVALID_SOCKET) {
                shutdown(s, SD_BOTH);
                closesocket(s);
            }
        }
        m_sseClients.clear();
        for (SOCKET s : m_wsClients) {
            if (s != INVALID_SOCKET) {
                shutdown(s, SD_BOTH);
                closesocket(s);
            }
        }
        m_wsClients.clear();
    }

    SOCKET sock = m_listenSock;
    m_listenSock = INVALID_SOCKET;
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    if (m_watcherThread.joinable()) {
        m_watcherThread.join();
    }
    if (m_watchStopEvent) {
        CloseHandle(m_watchStopEvent);
        m_watchStopEvent = nullptr;
    }
}

void LiveServer::ServerWorker() {
    while (m_isRunning.load()) {
        SOCKET s = m_listenSock;
        if (s == INVALID_SOCKET) break;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s, &readSet);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms polling timeout to keep thread responsive to Stop()

        int sel = select(0, &readSet, nullptr, nullptr, &tv);
        if (sel <= 0) {
            if (!m_isRunning.load() || m_listenSock == INVALID_SOCKET) break;
            continue;
        }

        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET clientSock = accept(s, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

        if (clientSock == INVALID_SOCKET) {
            if (!m_isRunning.load()) break;
            continue;
        }

        std::thread([this, clientSock]() {
            HandleClient(clientSock);
        }).detach();
    }
}

static std::string UrlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        if (in[i] == '%' && i + 2 < in.length()) {
            int h = 0;
            std::stringstream ss;
            ss << std::hex << in.substr(i + 1, 2);
            ss >> h;
            out += static_cast<char>(h);
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

std::string LiveServer::GetMimeType(const std::filesystem::path& path) const {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js" || ext == ".mjs" || ext == ".cjs") return "application/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

void LiveServer::HandleClient(SOCKET clientSock) {
    char reqBuf[4096] = {0};
    int bytesRecv = recv(clientSock, reqBuf, sizeof(reqBuf) - 1, 0);
    if (bytesRecv <= 0) {
        closesocket(clientSock);
        return;
    }

    std::string req(reqBuf, bytesRecv);
    std::istringstream reqStream(req);
    std::string method, rawUri, proto;
    reqStream >> method >> rawUri >> proto;

    if (method != "GET" && method != "HEAD") {
        std::string res = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(clientSock, res.data(), static_cast<int>(res.size()), 0);
        closesocket(clientSock);
        return;
    }

    // Strip query parameters
    size_t qPos = rawUri.find('?');
    if (qPos != std::string::npos) {
        rawUri = rawUri.substr(0, qPos);
    }

    // Live-Reload WebSocket endpoint (RFC 6455)
    if (rawUri == "/__phide_ws") {
        std::string wsKey;
        std::istringstream hdrs(req);
        std::string hline;
        std::getline(hdrs, hline); // request line
        while (std::getline(hdrs, hline)) {
            size_t colon = hline.find(':');
            if (colon == std::string::npos) continue;
            std::string k = hline.substr(0, colon);
            for (char& ch : k) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (k == "sec-websocket-key") {
                wsKey = hline.substr(colon + 1);
                while (!wsKey.empty() && (wsKey.front() == ' ' || wsKey.front() == '\t')) wsKey.erase(wsKey.begin());
                while (!wsKey.empty() && (wsKey.back() == '\r' || wsKey.back() == '\n' || wsKey.back() == ' ')) wsKey.pop_back();
                break;
            }
        }
        if (!wsKey.empty()) {
            std::string acceptKey = BuildWebSocketAcceptKey(wsKey);
            std::ostringstream hs;
            hs << "HTTP/1.1 101 Switching Protocols\r\n"
               << "Upgrade: websocket\r\n"
               << "Connection: Upgrade\r\n"
               << "Sec-WebSocket-Accept: " << acceptKey << "\r\n\r\n";
            std::string hsStr = hs.str();
            send(clientSock, hsStr.data(), static_cast<int>(hsStr.size()), 0);
            std::lock_guard<std::mutex> cl(m_clientsMutex);
            m_wsClients.push_back(clientSock);
            return;
        }
    }

    // Live-Reload Server-Sent Events (SSE) endpoint
    if (rawUri == "/__phide_sse") {
        std::ostringstream sse;
        sse << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: text/event-stream; charset=utf-8\r\n"
            << "Cache-Control: no-cache\r\n"
            << "Connection: keep-alive\r\n"
            << "Access-Control-Allow-Origin: *\r\n\r\n"
            << "retry: 500\nevent: hello\ndata: {\"revision\":" << m_reloadRevision.load() << "}\n\n";
        std::string sseStr = sse.str();
        send(clientSock, sseStr.data(), static_cast<int>(sseStr.size()), 0);
        std::lock_guard<std::mutex> cl(m_clientsMutex);
        m_sseClients.push_back(clientSock);
        return;
    }

    // Lightweight JSON revision endpoint
    if (rawUri == "/__phide_revision") {
        std::string body = "{\"revision\":" + std::to_string(m_reloadRevision.load()) + "}";
        std::string res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-cache\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        send(clientSock, res.data(), static_cast<int>(res.size()), 0);
        closesocket(clientSock);
        return;
    }

    std::string decodedPath = UrlDecode(rawUri);
    auto utf8DefaultFile = [&]() -> std::string {
        int len = WideCharToMultiByte(CP_UTF8, 0, m_defaultFile.c_str(), -1, NULL, 0, NULL, NULL);
        std::string defFile = "index.html";
        if (len > 1) {
            defFile.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, m_defaultFile.c_str(), -1, &defFile[0], len, NULL, NULL);
        }
        return defFile;
    };
    if (decodedPath.empty() || decodedPath == "/") {
        decodedPath = "/" + utf8DefaultFile();
    }

    while (!decodedPath.empty() && decodedPath.front() == '/') {
        decodedPath.erase(decodedPath.begin());
    }

    std::filesystem::path root(m_rootDir);
    std::filesystem::path filePath = root / decodedPath;

    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(filePath, ec);
    std::filesystem::path rootCanonical = std::filesystem::weakly_canonical(root, ec);

    // Optional SPA route fallback when file doesn't exist
    if (m_spaFallback.load() && (!std::filesystem::exists(canonical, ec) || std::filesystem::is_directory(canonical, ec))) {
        std::filesystem::path fallback = std::filesystem::weakly_canonical(root / utf8DefaultFile(), ec);
        if (std::filesystem::exists(fallback, ec) && !std::filesystem::is_directory(fallback, ec)) {
            canonical = fallback;
        }
    }

    // Prevent directory traversal attacks
    std::string canonStr = canonical.string();
    std::string rootStr = rootCanonical.string();
    if (canonStr.find(rootStr) != 0 || !std::filesystem::exists(canonical, ec) || std::filesystem::is_directory(canonical, ec)) {
        std::string body = "<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>Phide Live Server could not find the requested resource.</p></body></html>";
        std::string res = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(body.length()) + "\r\nConnection: close\r\n\r\n" + body;
        send(clientSock, res.data(), static_cast<int>(res.size()), 0);
        closesocket(clientSock);
        return;
    }

    std::ifstream file(canonical, std::ios::binary);
    if (!file.is_open()) {
        std::string res = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(clientSock, res.data(), static_cast<int>(res.size()), 0);
        closesocket(clientSock);
        return;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::string fileBytes;
    fileBytes.resize(fileSize);
    if (fileSize > 0) {
        file.read(fileBytes.data(), fileSize);
    }

    std::string mime = GetMimeType(canonical);
    if (m_liveReloadEnabled.load() && method == "GET" && mime.find("text/html") != std::string::npos) {
        fileBytes = InjectLiveReloadScript(std::move(fileBytes));
    }

    std::ostringstream header;
    header << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: " << mime << "\r\n"
           << "Content-Length: " << fileBytes.size() << "\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Cache-Control: no-cache, no-store, must-revalidate\r\n"
           << "Connection: close\r\n\r\n";

    std::string headerStr = header.str();
    send(clientSock, headerStr.data(), static_cast<int>(headerStr.size()), 0);

    if (method == "GET" && !fileBytes.empty()) {
        send(clientSock, fileBytes.data(), static_cast<int>(fileBytes.size()), 0);
    }

    closesocket(clientSock);
}

std::string LiveServer::InjectLiveReloadScript(std::string html) {
    static const char* kLiveScript =
        "\n<script>(function(){"
        "if(window.__phideLiveReload)return;window.__phideLiveReload=1;"
        "function applyReload(msg){try{var d=JSON.parse(msg);"
        "if(d&&d.path&&/\\.css$/i.test(d.path)){var ls=document.querySelectorAll('link[rel=\"stylesheet\"]');"
        "if(ls.length){ls.forEach(function(l){var u=l.href.split('?')[0];l.href=u+'?_phide='+Date.now();});return;}}"
        "}catch(e){}location.reload();}"
        "function startSse(){if(!window.EventSource)return;"
        "var es=new EventSource('/__phide_sse');es.addEventListener('reload',function(e){applyReload(e.data);});}"
        "try{var ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/__phide_ws');"
        "ws.onmessage=function(e){applyReload(e.data);};ws.onerror=function(){try{ws.close();}catch(_){}startSse();};}"
        "catch(e){startSse();}"
        "})();</script>\n";

    std::string lower = html;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t bodyEnd = lower.rfind("</body>");
    if (bodyEnd != std::string::npos) {
        html.insert(bodyEnd, kLiveScript);
    } else {
        html += kLiveScript;
    }
    return html;
}

std::string LiveServer::BuildWebSocketAcceptKey(const std::string& clientKey) {
    // RFC 6455 Section 1.3: SHA-1(clientKey + GUID) Base64-encoded
    const std::string concat = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::vector<uint8_t> msg(concat.begin(), concat.end());
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8ULL;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF));
    }

    auto rotl = [](uint32_t v, int n) -> uint32_t { return (v << n) | (v >> (32 - n)); };
    uint32_t h0 = 0x67452301U;
    uint32_t h1 = 0xEFCDAB89U;
    uint32_t h2 = 0x98BADCFEU;
    uint32_t h3 = 0x10325476U;
    uint32_t h4 = 0xC3D2E1F0U;

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80] = {};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999U; }
            else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1U; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCU; }
            else             { f = b ^ c ^ d;            k = 0xCA62C1D6U; }
            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    uint8_t digest[20] = {};
    uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        digest[i * 4 + 0] = static_cast<uint8_t>((hs[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFF);
    }

    static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    for (size_t i = 0; i < 20; i += 3) {
        uint32_t val = static_cast<uint32_t>(digest[i]) << 16;
        if (i + 1 < 20) val |= static_cast<uint32_t>(digest[i + 1]) << 8;
        if (i + 2 < 20) val |= static_cast<uint32_t>(digest[i + 2]);
        b64.push_back(kB64[(val >> 18) & 0x3F]);
        b64.push_back(kB64[(val >> 12) & 0x3F]);
        b64.push_back((i + 1 < 20) ? kB64[(val >> 6) & 0x3F] : '=');
        b64.push_back((i + 2 < 20) ? kB64[val & 0x3F] : '=');
    }
    return b64;
}

std::vector<uint8_t> LiveServer::EncodeWebSocketTextFrame(const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + Text opcode
    const size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
        }
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

bool LiveServer::TryDecodeWebSocketTextFrame(const std::vector<uint8_t>& frameBytes, std::string& outPayload) {
    outPayload.clear();
    if (frameBytes.size() < 2) return false;
    if ((frameBytes[0] & 0x0F) != 0x01) return false;
    const bool masked = (frameBytes[1] & 0x80) != 0;
    uint64_t len = frameBytes[1] & 0x7F;
    size_t pos = 2;
    if (len == 126) {
        if (frameBytes.size() < 4) return false;
        len = (static_cast<uint64_t>(frameBytes[2]) << 8) | frameBytes[3];
        pos = 4;
    } else if (len == 127) {
        if (frameBytes.size() < 10) return false;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | frameBytes[2 + i];
        pos = 10;
    }
    uint8_t mask[4] = {};
    if (masked) {
        if (frameBytes.size() < pos + 4) return false;
        for (int i = 0; i < 4; ++i) mask[i] = frameBytes[pos + i];
        pos += 4;
    }
    if (frameBytes.size() < pos + len) return false;
    outPayload.resize(static_cast<size_t>(len));
    for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
        outPayload[i] = static_cast<char>(masked ? (frameBytes[pos + i] ^ mask[i % 4]) : frameBytes[pos + i]);
    }
    return true;
}

size_t LiveServer::GetConnectedClientCount() const {
    std::lock_guard<std::mutex> cl(m_clientsMutex);
    return m_sseClients.size() + m_wsClients.size();
}

void LiveServer::BroadcastReload(const std::wstring& changedRelPath) {
    uint64_t rev = ++m_reloadRevision;
    std::string relUtf8;
    if (!changedRelPath.empty()) {
        int n = WideCharToMultiByte(CP_UTF8, 0, changedRelPath.data(), static_cast<int>(changedRelPath.size()), nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            relUtf8.resize(n);
            WideCharToMultiByte(CP_UTF8, 0, changedRelPath.data(), static_cast<int>(changedRelPath.size()), relUtf8.data(), n, nullptr, nullptr);
        }
    }
    std::string escapedPath;
    for (char c : relUtf8) {
        if (c == '\\') escapedPath += '/';
        else if (c == '"') escapedPath += "\\\"";
        else if (static_cast<unsigned char>(c) >= 0x20) escapedPath.push_back(c);
    }

    std::string json = "{\"type\":\"reload\",\"path\":\"" + escapedPath + "\",\"revision\":" + std::to_string(rev) + "}";
    std::string sseMsg = "event: reload\ndata: " + json + "\n\n";
    std::vector<uint8_t> wsFrame = EncodeWebSocketTextFrame(json);

    std::lock_guard<std::mutex> cl(m_clientsMutex);
    for (auto it = m_sseClients.begin(); it != m_sseClients.end();) {
        int sent = send(*it, sseMsg.data(), static_cast<int>(sseMsg.size()), 0);
        if (sent <= 0) {
            closesocket(*it);
            it = m_sseClients.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_wsClients.begin(); it != m_wsClients.end();) {
        int sent = send(*it, reinterpret_cast<const char*>(wsFrame.data()), static_cast<int>(wsFrame.size()), 0);
        if (sent <= 0) {
            closesocket(*it);
            it = m_wsClients.erase(it);
        } else {
            ++it;
        }
    }
}

void LiveServer::WatcherWorker() {
    if (m_rootDir.empty()) return;
    HANDLE hChange = FindFirstChangeNotificationW(
        m_rootDir.c_str(), TRUE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
    if (hChange == INVALID_HANDLE_VALUE || !hChange) return;

    ULONGLONG lastBroadcastMs = 0;
    while (m_isRunning.load()) {
        HANDLE handles[2] = { hChange, m_watchStopEvent ? m_watchStopEvent : hChange };
        DWORD nHandles = m_watchStopEvent ? 2 : 1;
        DWORD res = WaitForMultipleObjects(nHandles, handles, FALSE, 500);
        if (!m_isRunning.load() || (m_watchStopEvent && res == WAIT_OBJECT_0 + 1)) {
            break;
        }
        if (res == WAIT_OBJECT_0) {
            ULONGLONG now = GetTickCount64();
            if (now - lastBroadcastMs >= 100) {
                lastBroadcastMs = now;
                BroadcastReload(L"workspace");
            }
            if (!FindNextChangeNotification(hChange)) break;
        }
    }
    FindCloseChangeNotification(hChange);
}

std::vector<LiveServer::PhpRuntimeEntry> LiveServer::DiscoverInstalledPhpVersions(const std::wstring& phideRoot) {
    std::vector<PhpRuntimeEntry> out;
    auto probePhp = [&](const std::filesystem::path& phpExe) {
        std::error_code ec;
        if (!std::filesystem::exists(phpExe, ec)) return;
        std::wstring canon = std::filesystem::weakly_canonical(phpExe, ec).wstring();
        for (const auto& existing : out) {
            if (_wcsicmp(existing.exePath.c_str(), canon.c_str()) == 0) return;
        }
        PhpRuntimeEntry e;
        e.exePath = canon;
        std::filesystem::path ini = phpExe.parent_path() / L"php.ini";
        if (std::filesystem::exists(ini, ec)) e.iniPath = ini.wstring();

        // Read version quickly via php -r "echo PHP_VERSION;"
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE hR = nullptr, hW = nullptr;
        if (CreatePipe(&hR, &hW, &sa, 0)) {
            SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = hW;
            si.hStdError = hW;
            std::wstring cmd = L"\"" + canon + L"\" -r \"echo PHP_VERSION;\"";
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(hW);
                hW = nullptr;
                WaitForSingleObject(pi.hProcess, 1500);
                char buf[128] = {};
                DWORD rd = 0;
                DWORD avail = 0;
                if (PeekNamedPipe(hR, nullptr, 0, nullptr, &avail, nullptr) && avail > 0 &&
                    ReadFile(hR, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
                    std::string v(buf, rd);
                    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
                    e.version = std::wstring(v.begin(), v.end());
                }
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            if (hW) CloseHandle(hW);
            CloseHandle(hR);
        }
        if (e.version.empty()) e.version = phpExe.parent_path().filename().wstring();
        out.push_back(std::move(e));
    };

    std::error_code ec;
    if (!phideRoot.empty()) {
        std::filesystem::path rtDir = std::filesystem::path(phideRoot) / L"runtimes";
        if (std::filesystem::exists(rtDir, ec)) {
            for (const auto& d : std::filesystem::directory_iterator(rtDir, ec)) {
                if (d.is_directory(ec)) probePhp(d.path() / L"php.exe");
            }
        }
    }
    wchar_t localApp[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0) {
        std::filesystem::path phpDirs = std::filesystem::path(localApp) / L"Programs" / L"PHP";
        if (std::filesystem::exists(phpDirs, ec)) {
            for (const auto& d : std::filesystem::directory_iterator(phpDirs, ec)) {
                if (d.is_directory(ec)) probePhp(d.path() / L"php.exe");
            }
        }
    }
    wchar_t pathBuf[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"php.exe", nullptr, MAX_PATH, pathBuf, nullptr) > 0) {
        probePhp(pathBuf);
    }
    return out;
}

std::wstring LiveServer::GenerateNginxVhostPreset(
    const std::wstring& presetName,
    const std::wstring& serverName,
    int port,
    const std::wstring& docRoot,
    int phpFpmPort) {
    std::wstring rootFwd = docRoot;
    for (auto& c : rootFwd) if (c == L'\\') c = L'/';
    std::wstring host = serverName.empty() ? L"localhost" : serverName;
    if (port <= 0 || port > 65535) port = 80;

    std::wostringstream out;
    out << L"# Phide Preset: " << presetName << L"\n"
        << L"server {\n"
        << L"    listen " << port << L";\n"
        << L"    server_name " << host << L";\n";

    if (presetName == L"Laravel / public") {
        out << L"    root \"" << rootFwd << L"/public\";\n"
            << L"    index index.php index.html;\n\n"
            << L"    location / {\n"
            << L"        try_files $uri $uri/ /index.php?$query_string;\n"
            << L"    }\n";
    } else if (presetName == L"SPA History Mode") {
        out << L"    root \"" << rootFwd << L"\";\n"
            << L"    index index.html;\n\n"
            << L"    location / {\n"
            << L"        try_files $uri $uri/ /index.html;\n"
            << L"    }\n";
    } else {
        out << L"    root \"" << rootFwd << L"\";\n"
            << L"    index index.php index.html index.htm;\n\n"
            << L"    location / {\n"
            << L"        try_files $uri $uri/ =404;\n"
            << L"    }\n";
    }

    if (presetName != L"SPA History Mode") {
        out << L"\n    location ~ \\.php$ {\n"
            << L"        fastcgi_pass 127.0.0.1:" << phpFpmPort << L";\n"
            << L"        fastcgi_index index.php;\n"
            << L"        include fastcgi_params;\n"
            << L"        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;\n"
            << L"    }\n";
    }
    out << L"}\n";
    return out.str();
}

} // namespace phide::server
