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
    return true;
}

void LiveServer::Stop() {
    if (!m_isRunning.exchange(false)) {
        return;
    }

    if (m_listenSock != INVALID_SOCKET) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void LiveServer::ServerWorker() {
    while (m_isRunning.load()) {
        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET clientSock = accept(m_listenSock, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

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

    std::string decodedPath = UrlDecode(rawUri);
    if (decodedPath.empty() || decodedPath == "/") {
        int len = WideCharToMultiByte(CP_UTF8, 0, m_defaultFile.c_str(), -1, NULL, 0, NULL, NULL);
        std::string defFile = "index.html";
        if (len > 1) {
            defFile.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, m_defaultFile.c_str(), -1, &defFile[0], len, NULL, NULL);
        }
        decodedPath = "/" + defFile;
    }

    while (!decodedPath.empty() && decodedPath.front() == '/') {
        decodedPath.erase(decodedPath.begin());
    }

    std::filesystem::path root(m_rootDir);
    std::filesystem::path filePath = root / decodedPath;

    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(filePath, ec);
    std::filesystem::path rootCanonical = std::filesystem::weakly_canonical(root, ec);

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

    std::vector<char> fileData(fileSize);
    if (fileSize > 0) {
        file.read(fileData.data(), fileSize);
    }

    std::string mime = GetMimeType(canonical);
    std::ostringstream header;
    header << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: " << mime << "\r\n"
           << "Content-Length: " << fileSize << "\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Cache-Control: no-cache, no-store, must-revalidate\r\n"
           << "Connection: close\r\n\r\n";

    std::string headerStr = header.str();
    send(clientSock, headerStr.data(), static_cast<int>(headerStr.size()), 0);

    if (method == "GET" && fileSize > 0) {
        send(clientSock, fileData.data(), static_cast<int>(fileSize), 0);
    }

    closesocket(clientSock);
}

} // namespace phide::server
