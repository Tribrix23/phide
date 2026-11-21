#include "phide/server/LiveServer.hpp"
#include "phide/app/Logger.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "secur32.lib")

namespace phide::server {

struct LiveServer::TlsState {
    HCERTSTORE hStore = nullptr;
    PCCERT_CONTEXT pCert = nullptr;
    CredHandle cred{};
    bool hasCred = false;
};

struct LiveServer::TlsSocketSession {
    CtxtHandle ctxt{};
    bool hasCtxt = false;
    SecPkgContext_StreamSizes sizes{};
    std::mutex ioMutex;
    ~TlsSocketSession() {
        if (hasCtxt) {
            DeleteSecurityContext(&ctxt);
            hasCtxt = false;
        }
    }
};

namespace {

static std::string Base64EncodeDer(const uint8_t* data, size_t len) {
    static const char* kTbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string raw;
    raw.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) v |= static_cast<uint32_t>(data[i + 2]);
        raw.push_back(kTbl[(v >> 18) & 0x3F]);
        raw.push_back(kTbl[(v >> 12) & 0x3F]);
        raw.push_back((i + 1 < len) ? kTbl[(v >> 6) & 0x3F] : '=');
        raw.push_back((i + 2 < len) ? kTbl[v & 0x3F] : '=');
    }
    std::string wrapped;
    wrapped.reserve(raw.size() + raw.size() / 64 + 8);
    for (size_t i = 0; i < raw.size(); i += 64) {
        wrapped.append(raw, i, (std::min<size_t>)(64, raw.size() - i));
        wrapped.push_back('\n');
    }
    return wrapped;
}

static bool ReadWholeFileBytes(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return false;
    ifs.seekg(0, std::ios::end);
    std::streamoff sz = ifs.tellg();
    if (sz <= 0 || sz > 1024 * 1024) return false;
    ifs.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    return static_cast<bool>(ifs.read(reinterpret_cast<char*>(out.data()), sz));
}

static bool SendAllSocketBytes(SOCKET s, const char* data, int len) {
    int off = 0;
    while (off < len) {
        int n = send(s, data + off, len - off, 0);
        if (n <= 0) return false;
        off += n;
    }
    return true;
}

} // namespace

LiveServer::LiveServer() : m_tlsState(std::make_unique<TlsState>()) {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
        m_wsaInitialized = true;
    }
    DiscoverNetworkIp();
}

LiveServer::~LiveServer() {
    Stop();
    CleanupTlsCredentials();
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
    const wchar_t* scheme = m_tlsActive.load() ? L"https://localhost:" : L"http://localhost:";
    std::wstring url = scheme + std::to_wstring(m_port);
    if (!path.empty()) {
        if (path.front() != L'/') url += L"/";
        url += path;
    }
    return url;
}

std::wstring LiveServer::GetNetworkUrl(const std::wstring& relPath) const {
    std::wstring path = relPath.empty() ? m_defaultFile : relPath;
    const wchar_t* scheme = m_tlsActive.load() ? L"https://" : L"http://";
    std::wstring url = scheme + m_localIp + L":" + std::to_wstring(m_port);
    if (!path.empty()) {
        if (path.front() != L'/') url += L"/";
        url += path;
    }
    return url;
}

bool LiveServer::EnsureLocalDevCertificate(
    const std::wstring& certDir,
    std::wstring& outPfxPath,
    std::wstring& outCertPemPath,
    std::wstring& outKeyPemPath,
    std::wstring& outError) {
    outPfxPath.clear();
    outCertPemPath.clear();
    outKeyPemPath.clear();
    outError.clear();
    if (certDir.empty()) {
        outError = L"Empty certificate directory.";
        return false;
    }
    std::error_code ec;
    std::filesystem::path dir(certDir);
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path pfxPath = dir / L"phide-dev-localhost.pfx";
    std::filesystem::path crtPath = dir / L"phide-dev-localhost.crt";
    std::filesystem::path keyPath = dir / L"phide-dev-localhost.key";

    if (std::filesystem::exists(pfxPath, ec) && std::filesystem::file_size(pfxPath, ec) > 0 &&
        std::filesystem::exists(crtPath, ec) && std::filesystem::exists(keyPath, ec)) {
        outPfxPath = pfxPath.wstring();
        outCertPemPath = crtPath.wstring();
        outKeyPemPath = keyPath.wstring();
        return true;
    }

    wchar_t containerName[96]{};
    swprintf_s(containerName, L"PhideLocalDevTls_%lu_%llu",
               GetCurrentProcessId(),
               static_cast<unsigned long long>(GetTickCount64()));
    {
        HCRYPTPROV hDel = 0;
        CryptAcquireContextW(&hDel, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET | CRYPT_SILENT);
    }

    DWORD keysetFlag = 0;
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET | CRYPT_SILENT)) {
        keysetFlag = CRYPT_MACHINE_KEYSET;
        HCRYPTPROV hDelM = 0;
        CryptAcquireContextW(&hDelM, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET | CRYPT_MACHINE_KEYSET | CRYPT_SILENT);
        if (!CryptAcquireContextW(&hProv, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET | CRYPT_MACHINE_KEYSET | CRYPT_SILENT)) {
            outError = L"CryptAcquireContextW failed (" + std::to_wstring(GetLastError()) + L").";
            return false;
        }
    }

    HCRYPTKEY hKey = 0;
    if (!CryptGenKey(hProv, AT_KEYEXCHANGE, (2048U << 16) | CRYPT_EXPORTABLE, &hKey)) {
        CryptReleaseContext(hProv, 0);
        HCRYPTPROV hDel = 0;
        CryptAcquireContextW(&hDel, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET | keysetFlag | CRYPT_SILENT);
        outError = L"CryptGenKey failed (" + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    BYTE subjBuf[256] = {};
    DWORD subjLen = sizeof(subjBuf);
    CERT_NAME_BLOB subjBlob{};
    if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=localhost", CERT_X500_NAME_STR, nullptr, subjBuf, &subjLen, nullptr)) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        outError = L"CertStrToNameW failed.";
        return false;
    }
    subjBlob.cbData = subjLen;
    subjBlob.pbData = subjBuf;

    // Subject Alternative Name extension: DNS:localhost + IP:127.0.0.1
    uint8_t ip4Bytes[4] = { 127, 0, 0, 1 };
    CERT_ALT_NAME_ENTRY sanEntries[2]{};
    sanEntries[0].dwAltNameChoice = CERT_ALT_NAME_DNS_NAME;
    sanEntries[0].pwszDNSName = const_cast<LPWSTR>(L"localhost");
    sanEntries[1].dwAltNameChoice = CERT_ALT_NAME_IP_ADDRESS;
    sanEntries[1].IPAddress.cbData = 4;
    sanEntries[1].IPAddress.pbData = ip4Bytes;
    CERT_ALT_NAME_INFO sanInfo{ 2, sanEntries };

    BYTE sanEncoded[512] = {};
    DWORD sanEncodedLen = sizeof(sanEncoded);
    CERT_EXTENSION sanExt{};
    CERT_EXTENSIONS exts{};
    if (CryptEncodeObjectEx(X509_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2, &sanInfo, 0, nullptr, sanEncoded, &sanEncodedLen)) {
        sanExt.pszObjId = const_cast<LPSTR>(szOID_SUBJECT_ALT_NAME2);
        sanExt.fCritical = FALSE;
        sanExt.Value.cbData = sanEncodedLen;
        sanExt.Value.pbData = sanEncoded;
        exts.cExtension = 1;
        exts.rgExtension = &sanExt;
    }

    CRYPT_KEY_PROV_INFO kpi{};
    kpi.pwszContainerName = containerName;
    kpi.pwszProvName = const_cast<LPWSTR>(MS_ENH_RSA_AES_PROV_W);
    kpi.dwProvType = PROV_RSA_AES;
    kpi.dwFlags = keysetFlag;
    kpi.dwKeySpec = AT_KEYEXCHANGE;

    SYSTEMTIME stStart{}, stEnd{};
    GetSystemTime(&stStart);
    stEnd = stStart;
    stEnd.wYear += 2;

    PCCERT_CONTEXT pCert = CertCreateSelfSignCertificate(
        hProv, &subjBlob, 0, &kpi, nullptr, &stStart, &stEnd, exts.cExtension ? &exts : nullptr);
    if (!pCert) {
        DWORD errCode = GetLastError();
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        HCRYPTPROV hDel = 0;
        CryptAcquireContextW(&hDel, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET | keysetFlag | CRYPT_SILENT);
        outError = L"CertCreateSelfSignCertificate failed (" + std::to_wstring(errCode) + L").";
        return false;
    }

    // Export PEM certificate
    {
        std::string pem = "-----BEGIN CERTIFICATE-----\n" +
                          Base64EncodeDer(pCert->pbCertEncoded, pCert->cbCertEncoded) +
                          "-----END CERTIFICATE-----\n";
        std::ofstream ofs(crtPath, std::ios::binary | std::ios::trunc);
        ofs.write(pem.data(), static_cast<std::streamsize>(pem.size()));
    }

    // Export RSA PKCS#1 private key PEM for Nginx
    {
        DWORD blobLen = 0;
        if (CryptExportKey(hKey, 0, PRIVATEKEYBLOB, 0, nullptr, &blobLen) && blobLen > 0) {
            std::vector<uint8_t> privBlob(blobLen);
            if (CryptExportKey(hKey, 0, PRIVATEKEYBLOB, 0, privBlob.data(), &blobLen)) {
                DWORD derLen = 0;
                if (CryptEncodeObjectEx(X509_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, privBlob.data(), 0, nullptr, nullptr, &derLen) && derLen > 0) {
                    std::vector<uint8_t> der(derLen);
                    if (CryptEncodeObjectEx(X509_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, privBlob.data(), 0, nullptr, der.data(), &derLen)) {
                        std::string keyPem = "-----BEGIN RSA PRIVATE KEY-----\n" +
                                             Base64EncodeDer(der.data(), derLen) +
                                             "-----END RSA PRIVATE KEY-----\n";
                        std::ofstream ofs(keyPath, std::ios::binary | std::ios::trunc);
                        ofs.write(keyPem.data(), static_cast<std::streamsize>(keyPem.size()));
                    }
                }
            }
        }
    }

    // Export PFX into cached store file
    HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    bool pfxOk = false;
    if (hMemStore) {
        if (CertAddCertificateContextToStore(hMemStore, pCert, CERT_STORE_ADD_REPLACE_EXISTING, nullptr)) {
            CRYPT_DATA_BLOB pfxBlob{};
            if (PFXExportCertStoreEx(hMemStore, &pfxBlob, L"phide", nullptr, EXPORT_PRIVATE_KEYS) && pfxBlob.cbData > 0) {
                std::vector<uint8_t> buf(pfxBlob.cbData);
                pfxBlob.pbData = buf.data();
                if (PFXExportCertStoreEx(hMemStore, &pfxBlob, L"phide", nullptr, EXPORT_PRIVATE_KEYS)) {
                    std::ofstream ofs(pfxPath, std::ios::binary | std::ios::trunc);
                    ofs.write(reinterpret_cast<const char*>(pfxBlob.pbData), pfxBlob.cbData);
                    pfxOk = ofs.good();
                }
            }
        }
        CertCloseStore(hMemStore, 0);
    }

    CertFreeCertificateContext(pCert);
    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);
    {
        HCRYPTPROV hDel = 0;
        CryptAcquireContextW(&hDel, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET | keysetFlag | CRYPT_SILENT);
    }

    if (!pfxOk) {
        outError = L"PFXExportCertStoreEx failed.";
        return false;
    }

    outPfxPath = pfxPath.wstring();
    outCertPemPath = crtPath.wstring();
    outKeyPemPath = keyPath.wstring();
    return true;
}

void LiveServer::CleanupTlsCredentials() {
    m_tlsActive.store(false);
    if (!m_tlsState) return;
    if (m_tlsState->hasCred) {
        FreeCredentialsHandle(&m_tlsState->cred);
        m_tlsState->hasCred = false;
    }
    if (m_tlsState->pCert) {
        CertFreeCertificateContext(m_tlsState->pCert);
        m_tlsState->pCert = nullptr;
    }
    if (m_tlsState->hStore) {
        CertCloseStore(m_tlsState->hStore, 0);
        m_tlsState->hStore = nullptr;
    }
}

void LiveServer::InitTlsCredentials() {
    CleanupTlsCredentials();
    if (!m_tlsEnabled.load() || !m_tlsState) return;

    wchar_t* pLocal = nullptr;
    size_t nLocal = 0;
    std::wstring certDir;
    if (_wdupenv_s(&pLocal, &nLocal, L"LOCALAPPDATA") == 0 && pLocal) {
        certDir = (std::filesystem::path(pLocal) / L"phide" / L"certs").wstring();
        free(pLocal);
    } else {
        certDir = (std::filesystem::temp_directory_path() / L"phide_certs").wstring();
    }

    std::wstring pfxPath, crtPath, keyPath, err;
    if (!EnsureLocalDevCertificate(certDir, pfxPath, crtPath, keyPath, err)) {
        return;
    }

    std::vector<uint8_t> pfxBytes;
    if (!ReadWholeFileBytes(pfxPath, pfxBytes)) return;

    CRYPT_DATA_BLOB blob{ static_cast<DWORD>(pfxBytes.size()), pfxBytes.data() };
    HCERTSTORE hStore = PFXImportCertStore(&blob, L"phide", CRYPT_EXPORTABLE);
    if (!hStore) return;

    PCCERT_CONTEXT pCert = CertEnumCertificatesInStore(hStore, nullptr);
    if (!pCert) {
        CertCloseStore(hStore, 0);
        return;
    }

    SCHANNEL_CRED scCred{};
    scCred.dwVersion = SCHANNEL_CRED_VERSION;
    scCred.cCreds = 1;
    scCred.paCred = &pCert;
    scCred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_NO_SYSTEM_MAPPER;

    TimeStamp expiry{};
    SECURITY_STATUS ss = AcquireCredentialsHandleW(
        nullptr, const_cast<LPWSTR>(UNISP_NAME_W), SECPKG_CRED_INBOUND,
        nullptr, &scCred, nullptr, nullptr, &m_tlsState->cred, &expiry);
    if (ss != SEC_E_OK) {
        CertFreeCertificateContext(pCert);
        CertCloseStore(hStore, 0);
        return;
    }

    m_tlsState->hStore = hStore;
    m_tlsState->pCert = pCert;
    m_tlsState->hasCred = true;
    m_tlsActive.store(true);
}

bool LiveServer::SendClientRawOrTls(SOCKET clientSock, TlsSocketSession* tlsSession, const char* data, int len) const {
    if (clientSock == INVALID_SOCKET || !data || len <= 0) return false;
    if (!tlsSession || !tlsSession->hasCtxt) {
        return SendAllSocketBytes(clientSock, data, len);
    }

    std::lock_guard<std::mutex> ioLk(tlsSession->ioMutex);
    const size_t maxChunk = tlsSession->sizes.cbMaximumMessage > 0 ? tlsSession->sizes.cbMaximumMessage : 16384;
    size_t offset = 0;
    while (offset < static_cast<size_t>(len)) {
        size_t chunk = (std::min)(maxChunk, static_cast<size_t>(len) - offset);
        std::vector<uint8_t> packet(tlsSession->sizes.cbHeader + chunk + tlsSession->sizes.cbTrailer);
        memcpy(packet.data() + tlsSession->sizes.cbHeader, data + offset, chunk);

        SecBuffer bufs[4]{};
        bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
        bufs[0].cbBuffer = tlsSession->sizes.cbHeader;
        bufs[0].pvBuffer = packet.data();
        bufs[1].BufferType = SECBUFFER_DATA;
        bufs[1].cbBuffer = static_cast<DWORD>(chunk);
        bufs[1].pvBuffer = packet.data() + tlsSession->sizes.cbHeader;
        bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
        bufs[2].cbBuffer = tlsSession->sizes.cbTrailer;
        bufs[2].pvBuffer = packet.data() + tlsSession->sizes.cbHeader + chunk;
        bufs[3].BufferType = SECBUFFER_EMPTY;

        SecBufferDesc desc{ SECBUFFER_VERSION, 4, bufs };
        if (EncryptMessage(&tlsSession->ctxt, 0, &desc, 0) != SEC_E_OK) {
            return false;
        }
        int totalSend = static_cast<int>(bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer);
        if (!SendAllSocketBytes(clientSock, reinterpret_cast<const char*>(packet.data()), totalSend)) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

bool LiveServer::Start(const std::wstring& rootDir, const std::wstring& defaultHtmlFile, int preferredPort) {
    Stop();
    InitTlsCredentials();

    std::error_code ec;
    std::filesystem::path root(rootDir);
    if (!std::filesystem::exists(root, ec)) {
        root = std::filesystem::current_path();
    }
    m_rootDir = root.wstring();
    m_defaultFile = defaultHtmlFile.empty() ? L"index.html" : defaultHtmlFile;

    m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSock == INVALID_SOCKET) {
        PHIDE_LOG_ERROR(Server, L"LiveServer failed to create TCP listen socket (WSAGetLastError=" +
                                std::to_wstring(WSAGetLastError()) + L").");
        CleanupTlsCredentials();
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
        PHIDE_LOG_ERROR(Server, L"LiveServer failed to bind/listen starting at port " +
                                std::to_wstring(preferredPort) + L" (WSAGetLastError=" +
                                std::to_wstring(WSAGetLastError()) + L").");
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
        CleanupTlsCredentials();
        return false;
    }

    PHIDE_LOG_INFO(Server, L"LiveServer listening on port " + std::to_wstring(m_port) +
                           L" (root=" + m_rootDir + L")");

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
        CleanupTlsCredentials();
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
        m_clientTlsMap.clear();
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
    for (int waitIdx = 0; waitIdx < 100 && m_activeClients.load() > 0; ++waitIdx) {
        Sleep(10);
    }
    if (m_watchStopEvent) {
        CloseHandle(m_watchStopEvent);
        m_watchStopEvent = nullptr;
    }
    CleanupTlsCredentials();
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

        m_activeClients.fetch_add(1);
        std::thread([this, clientSock]() {
            struct ActiveGuard {
                std::atomic<int>& c;
                ~ActiveGuard() { c.fetch_sub(1); }
            } g{ m_activeClients };
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
    std::shared_ptr<TlsSocketSession> tlsSession;
    std::string req;

    if (m_tlsActive.load() && m_tlsState && m_tlsState->hasCred) {
        tlsSession = std::make_shared<TlsSocketSession>();
        std::vector<uint8_t> inBuf;
        inBuf.reserve(8192);
        bool handshakeOk = false;
        bool needMore = true;
        CtxtHandle* pCtx = nullptr;
        TimeStamp tsExpiry{};

        for (int iter = 0; iter < 64 && m_isRunning.load(); ++iter) {
            if (needMore) {
                if (inBuf.size() >= 65536) break;
                char tmp[4096];
                int n = recv(clientSock, tmp, sizeof(tmp), 0);
                if (n <= 0) break;
                inBuf.insert(inBuf.end(), tmp, tmp + n);
            }

            SecBuffer inBufs[2]{};
            inBufs[0].BufferType = SECBUFFER_TOKEN;
            inBufs[0].cbBuffer = static_cast<DWORD>(inBuf.size());
            inBufs[0].pvBuffer = inBuf.data();
            inBufs[1].BufferType = SECBUFFER_EMPTY;
            SecBufferDesc inDesc{ SECBUFFER_VERSION, 2, inBufs };

            SecBuffer outBufs[1]{};
            outBufs[0].BufferType = SECBUFFER_TOKEN;
            SecBufferDesc outDesc{ SECBUFFER_VERSION, 1, outBufs };

            DWORD ctxAttr = 0;
            SECURITY_STATUS ss = AcceptSecurityContext(
                &m_tlsState->cred,
                pCtx,
                &inDesc,
                ASC_REQ_STREAM | ASC_REQ_ALLOCATE_MEMORY,
                0,
                &tlsSession->ctxt,
                &outDesc,
                &ctxAttr,
                &tsExpiry);
            pCtx = &tlsSession->ctxt;
            tlsSession->hasCtxt = true;

            if (outBufs[0].pvBuffer && outBufs[0].cbBuffer > 0) {
                SendAllSocketBytes(clientSock, reinterpret_cast<const char*>(outBufs[0].pvBuffer), static_cast<int>(outBufs[0].cbBuffer));
                FreeContextBuffer(outBufs[0].pvBuffer);
            }

            if (ss == SEC_E_INCOMPLETE_MESSAGE) {
                needMore = true;
                continue;
            }
            if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_OK) {
                if (inBufs[1].BufferType == SECBUFFER_EXTRA && inBufs[1].cbBuffer > 0) {
                    size_t extra = inBufs[1].cbBuffer;
                    memmove(inBuf.data(), inBuf.data() + (inBuf.size() - extra), extra);
                    inBuf.resize(extra);
                    needMore = false;
                } else {
                    inBuf.clear();
                    needMore = true;
                }
                if (ss == SEC_E_OK) {
                    if (QueryContextAttributesW(&tlsSession->ctxt, SECPKG_ATTR_STREAM_SIZES, &tlsSession->sizes) == SEC_E_OK) {
                        handshakeOk = true;
                    }
                    break;
                }
                continue;
            }
            break;
        }

        if (!handshakeOk) {
            closesocket(clientSock);
            return;
        }

        // Read & decrypt initial HTTP request
        for (int iter = 0; iter < 32 && req.find("\r\n\r\n") == std::string::npos; ++iter) {
            if (!inBuf.empty()) {
                SecBuffer dBufs[4]{};
                dBufs[0].BufferType = SECBUFFER_DATA;
                dBufs[0].cbBuffer = static_cast<DWORD>(inBuf.size());
                dBufs[0].pvBuffer = inBuf.data();
                dBufs[1].BufferType = SECBUFFER_EMPTY;
                dBufs[2].BufferType = SECBUFFER_EMPTY;
                dBufs[3].BufferType = SECBUFFER_EMPTY;
                SecBufferDesc dDesc{ SECBUFFER_VERSION, 4, dBufs };
                SECURITY_STATUS dss = DecryptMessage(&tlsSession->ctxt, &dDesc, 0, nullptr);
                if (dss == SEC_E_OK) {
                    for (int k = 0; k < 4; ++k) {
                        if (dBufs[k].BufferType == SECBUFFER_DATA && dBufs[k].pvBuffer && dBufs[k].cbBuffer > 0) {
                            req.append(reinterpret_cast<const char*>(dBufs[k].pvBuffer), dBufs[k].cbBuffer);
                        }
                    }
                    inBuf.clear();
                    if (req.find("\r\n\r\n") != std::string::npos || req.size() >= 16384) break;
                } else if (dss != SEC_E_INCOMPLETE_MESSAGE) {
                    break;
                }
            }
            if (inBuf.size() >= 65536) break;
            char tmp[4096];
            int n = recv(clientSock, tmp, sizeof(tmp), 0);
            if (n <= 0) break;
            inBuf.insert(inBuf.end(), tmp, tmp + n);
        }
    } else {
        char reqBuf[4096] = {0};
        int bytesRecv = recv(clientSock, reqBuf, sizeof(reqBuf) - 1, 0);
        if (bytesRecv <= 0) {
            closesocket(clientSock);
            return;
        }
        req.assign(reqBuf, bytesRecv);
    }

    if (req.empty()) {
        closesocket(clientSock);
        return;
    }

    TlsSocketSession* pTls = tlsSession.get();
    auto sendPayload = [&](const char* d, int l) {
        SendClientRawOrTls(clientSock, pTls, d, l);
    };
    auto sendStr = [&](const std::string& s) {
        sendPayload(s.data(), static_cast<int>(s.size()));
    };

    std::istringstream reqStream(req);
    std::string method, rawUri, proto;
    reqStream >> method >> rawUri >> proto;

    if (method != "GET" && method != "HEAD") {
        sendStr("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
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
            sendStr(hs.str());
            std::lock_guard<std::mutex> cl(m_clientsMutex);
            m_wsClients.push_back(clientSock);
            if (tlsSession) m_clientTlsMap[clientSock] = std::move(tlsSession);
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
        sendStr(sse.str());
        std::lock_guard<std::mutex> cl(m_clientsMutex);
        m_sseClients.push_back(clientSock);
        if (tlsSession) m_clientTlsMap[clientSock] = std::move(tlsSession);
        return;
    }

    // Lightweight JSON revision endpoint
    if (rawUri == "/__phide_revision") {
        std::string body = "{\"revision\":" + std::to_string(m_reloadRevision.load()) + "}";
        std::string res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-cache\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        sendStr(res);
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
        sendStr(res);
        closesocket(clientSock);
        return;
    }

    std::ifstream file(canonical, std::ios::binary);
    if (!file.is_open()) {
        sendStr("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
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

    sendStr(header.str());

    if (method == "GET" && !fileBytes.empty()) {
        sendPayload(fileBytes.data(), static_cast<int>(fileBytes.size()));
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
        TlsSocketSession* tls = nullptr;
        if (auto tf = m_clientTlsMap.find(*it); tf != m_clientTlsMap.end()) tls = tf->second.get();
        bool sentOk = SendClientRawOrTls(*it, tls, sseMsg.data(), static_cast<int>(sseMsg.size()));
        if (!sentOk) {
            m_clientTlsMap.erase(*it);
            closesocket(*it);
            it = m_sseClients.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_wsClients.begin(); it != m_wsClients.end();) {
        TlsSocketSession* tls = nullptr;
        if (auto tf = m_clientTlsMap.find(*it); tf != m_clientTlsMap.end()) tls = tf->second.get();
        bool sentOk = SendClientRawOrTls(*it, tls, reinterpret_cast<const char*>(wsFrame.data()), static_cast<int>(wsFrame.size()));
        if (!sentOk) {
            m_clientTlsMap.erase(*it);
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
    int phpFpmPort,
    int upstreamPort,
    const std::wstring& sslCertPath,
    const std::wstring& sslKeyPath) {
    std::wstring rootFwd = docRoot;
    for (auto& c : rootFwd) if (c == L'\\') c = L'/';
    std::wstring certFwd = sslCertPath;
    for (auto& c : certFwd) if (c == L'\\') c = L'/';
    std::wstring keyFwd = sslKeyPath;
    for (auto& c : keyFwd) if (c == L'\\') c = L'/';
    std::wstring host = serverName.empty() ? L"localhost" : serverName;
    if (port <= 0 || port > 65535) port = 80;
    if (upstreamPort <= 0 || upstreamPort > 65535) upstreamPort = 3000;

    std::wostringstream out;
    out << L"# Phide Preset: " << presetName << L"\n"
        << L"server {\n";

    if (presetName == L"HTTPS Reverse Proxy (SSL + proxy_pass)") {
        const int httpsPort = (port == 80) ? 443 : port;
        out << L"    listen " << httpsPort << L" ssl;\n"
            << L"    server_name " << host << L";\n"
            << L"    ssl_certificate     \"" << (certFwd.empty() ? L"conf/localhost.crt" : certFwd) << L"\";\n"
            << L"    ssl_certificate_key \"" << (keyFwd.empty() ? L"conf/localhost.key" : keyFwd) << L"\";\n"
            << L"    ssl_protocols       TLSv1.2 TLSv1.3;\n\n"
            << L"    location / {\n"
            << L"        proxy_pass http://127.0.0.1:" << upstreamPort << L";\n"
            << L"        proxy_http_version 1.1;\n"
            << L"        proxy_set_header Upgrade $http_upgrade;\n"
            << L"        proxy_set_header Connection \"upgrade\";\n"
            << L"        proxy_set_header Host $host;\n"
            << L"        proxy_set_header X-Forwarded-Proto https;\n"
            << L"        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
            << L"    }\n"
            << L"}\n";
        return out.str();
    }

    out << L"    listen " << port << L";\n"
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
    } else if (presetName == L"Reverse Proxy (Next.js / Vite)") {
        out << L"\n    location / {\n"
            << L"        proxy_pass http://127.0.0.1:" << upstreamPort << L";\n"
            << L"        proxy_http_version 1.1;\n"
            << L"        proxy_set_header Upgrade $http_upgrade;\n"
            << L"        proxy_set_header Connection \"upgrade\";\n"
            << L"        proxy_set_header Host $host;\n"
            << L"        proxy_set_header X-Real-IP $remote_addr;\n"
            << L"        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
            << L"    }\n";
    } else if (presetName == L"API + Frontend Split Proxy") {
        out << L"    root \"" << rootFwd << L"\";\n"
            << L"    index index.php index.html;\n\n"
            << L"    location /api/ {\n"
            << L"        try_files $uri $uri/ /index.php?$query_string;\n"
            << L"    }\n\n"
            << L"    location / {\n"
            << L"        proxy_pass http://127.0.0.1:" << upstreamPort << L";\n"
            << L"        proxy_http_version 1.1;\n"
            << L"        proxy_set_header Upgrade $http_upgrade;\n"
            << L"        proxy_set_header Connection \"upgrade\";\n"
            << L"        proxy_set_header Host $host;\n"
            << L"    }\n";
    } else {
        out << L"    root \"" << rootFwd << L"\";\n"
            << L"    index index.php index.html index.htm;\n\n"
            << L"    location / {\n"
            << L"        try_files $uri $uri/ =404;\n"
            << L"    }\n";
    }

    if (presetName != L"SPA History Mode" && presetName != L"Reverse Proxy (Next.js / Vite)") {
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
