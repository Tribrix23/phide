/**
 * @file PackageManagerView.cpp
 * @brief Implementation of PyCharm-styled Package Manager with language grid first,
 *        debounced fuzzy search, lazy-loaded package list with split Install|▾ buttons,
 *        right-side GitHub preview, and Supabase integration.
 */

#include "phide/ui/PackageManagerView.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cwctype>
#include <filesystem>
#include <unordered_set>
#include <winhttp.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/event.h>
#include <WebView2.h>

#pragma comment(lib, "winhttp.lib")

namespace phide::ui {

PackageManagerView::PackageManagerView() {
    DetectInstalledRuntimes();
    InitializeCatalog();
}

PackageManagerView::~PackageManagerView() = default;

void PackageManagerView::DiscardDeviceResources() {
    m_bmpPython.Reset();
    m_bmpPhp.Reset();
    m_bmpNode.Reset();
    m_bitmapsLoaded = false;
}

void PackageManagerView::SetHwnd(HWND hwnd) {
    m_hwnd = hwnd;
    if (m_hwnd && !m_webViewController && !m_webViewInitializing) {
        InitWebView2(m_hwnd);
    }
}

void PackageManagerView::InitWebView2(HWND hwnd) {
    if (!hwnd || m_webViewController || m_webViewInitializing) return;
    m_hwnd = hwnd;
    m_webViewInitializing = true;

    wchar_t localAppData[MAX_PATH];
    GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    std::wstring userDataFolder = std::wstring(localAppData) + L"\\phide\\WebView2Data";
    std::error_code ec;
    std::filesystem::create_directories(userDataFolder, ec);

    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.c_str(), nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    m_webViewInitializing = false;
                    return result;
                }
                m_webViewEnvironment = env;
                env->CreateCoreWebView2Controller(m_hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                            m_webViewInitializing = false;
                            if (FAILED(res) || !controller) return res;

                            m_webViewController = controller;
                            m_webViewController->get_CoreWebView2(&m_webView);
                            m_webViewReady = true;

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            if (m_webView && SUCCEEDED(m_webView->get_Settings(&settings)) && settings) {
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(TRUE);
                            }

                            UpdateWebViewBounds();

                            if (!m_pendingUrl.empty() && m_webView) {
                                m_currentLoadedUrl = m_pendingUrl;
                                m_webView->Navigate(m_pendingUrl.c_str());
                                m_pendingUrl.clear();
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void PackageManagerView::UpdateWebViewBounds() {
    if (!m_webViewController) return;

    bool shouldBeVisible = (m_viewState == PkgViewState::PackageSearch) &&
                           (m_selectedIndex >= 0) &&
                           (m_rcRightPreview.right > m_rcRightPreview.left + 50.0f);

    m_webViewController->put_IsVisible(shouldBeVisible ? TRUE : FALSE);

    if (shouldBeVisible) {
        RECT bounds;
        bounds.left = static_cast<LONG>(m_rcRightPreview.left);
        bounds.top = static_cast<LONG>(m_rcRightPreview.top);
        bounds.right = static_cast<LONG>(m_rcRightPreview.right);
        bounds.bottom = static_cast<LONG>(m_rcRightPreview.bottom);
        m_webViewController->put_Bounds(bounds);
    }
}

void PackageManagerView::HideWebView() {
    if (m_webViewController) {
        m_webViewController->put_IsVisible(FALSE);
    }
}

void PackageManagerView::NavigateToPackage(const std::wstring& url) {
    if (url.empty()) return;
    if (url == m_currentLoadedUrl && m_webView) return;

    if (m_webView && m_webViewReady) {
        m_currentLoadedUrl = url;
        m_webView->Navigate(url.c_str());
    } else {
        m_pendingUrl = url;
    }
    UpdateWebViewBounds();
}

void PackageManagerView::DetectInstalledRuntimes() {
    m_languageCards.clear();

    // 1. Python Detection - dynamically search system PATH, Windows launcher, and appdata
    LanguageCard pyCard;
    pyCard.lang = RuntimeLanguage::Python;
    pyCard.name = L"Python";
    pyCard.managerName = L"pip";
    pyCard.isInstalled = false;
    pyCard.version.clear();

    wchar_t pyFound[MAX_PATH] = { 0 };
    std::error_code ecPyFound;
    bool foundValidPy = false;
    if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, pyFound, nullptr) > 0 &&
        std::wstring(pyFound).find(L"WindowsApps") == std::wstring::npos &&
        std::filesystem::exists(pyFound, ecPyFound) && !ecPyFound) {
        foundValidPy = true;
    } else if (SearchPathW(nullptr, L"py.exe", nullptr, MAX_PATH, pyFound, nullptr) > 0 &&
               std::filesystem::exists(pyFound, ecPyFound) && !ecPyFound) {
        foundValidPy = true;
    }
    if (foundValidPy) {
        pyCard.isInstalled = true;
        pyCard.executable = pyFound;
    } else {
        wchar_t szAppData[MAX_PATH] = { 0 };
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", szAppData, MAX_PATH) > 0) {
            std::filesystem::path pyDir = std::filesystem::path(szAppData) / L"Programs" / L"Python";
            std::error_code ec;
            if (std::filesystem::exists(pyDir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(pyDir, ec)) {
                    if (entry.is_directory() && std::filesystem::exists(entry.path() / L"python.exe", ec)) {
                        pyCard.isInstalled = true;
                        pyCard.executable = (entry.path() / L"python.exe").wstring();
                        break;
                    }
                }
            }
        }
    }
    m_languageCards.push_back(std::move(pyCard));

    // 2. PHP Detection - dynamically search system PATH and local appdata programs
    LanguageCard phpCard;
    phpCard.lang = RuntimeLanguage::Php;
    phpCard.name = L"PHP";
    phpCard.managerName = L"Composer";
    phpCard.isInstalled = false;
    phpCard.version.clear();

    wchar_t phpFound[MAX_PATH] = { 0 };
    if (SearchPathW(nullptr, L"php.exe", nullptr, MAX_PATH, phpFound, nullptr) > 0) {
        phpCard.isInstalled = true;
        phpCard.executable = phpFound;
    } else {
        wchar_t szAppData[MAX_PATH] = { 0 };
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", szAppData, MAX_PATH) > 0) {
            std::filesystem::path phpDir = std::filesystem::path(szAppData) / L"Programs" / L"PHP";
            std::error_code ec;
            if (std::filesystem::exists(phpDir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(phpDir, ec)) {
                    if (entry.is_directory() && std::filesystem::exists(entry.path() / L"php.exe", ec)) {
                        phpCard.isInstalled = true;
                        phpCard.executable = (entry.path() / L"php.exe").wstring();
                        break;
                    }
                }
            }
        }
    }
    m_languageCards.push_back(std::move(phpCard));

    // 3. Node.js Detection - dynamically search system PATH and Program Files
    LanguageCard nodeCard;
    nodeCard.lang = RuntimeLanguage::Node;
    nodeCard.name = L"Node.js";
    nodeCard.managerName = L"npm";
    nodeCard.isInstalled = false;
    nodeCard.version.clear();

    wchar_t nodeFound[MAX_PATH] = { 0 };
    if (SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, nodeFound, nullptr) > 0) {
        nodeCard.isInstalled = true;
        nodeCard.executable = nodeFound;
    } else if (std::filesystem::exists(L"C:\\Program Files\\nodejs\\node.exe")) {
        nodeCard.isInstalled = true;
        nodeCard.executable = L"C:\\Program Files\\nodejs\\node.exe";
    }
    m_languageCards.push_back(std::move(nodeCard));
}

void PackageManagerView::LoadAssetBitmaps(ID2D1RenderTarget* pRT) {
    if (!pRT || m_bitmapsLoaded) return;
    m_bitmapsLoaded = true;

    auto loadOne = [&](const std::wstring& filename, Microsoft::WRL::ComPtr<ID2D1Bitmap>& outBmp) {
        std::vector<std::wstring> candidates = {
            filename,
            L"assets\\" + filename,
            L"bin\\assets\\" + filename,
            L"..\\assets\\" + filename
        };
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) {
                Microsoft::WRL::ComPtr<IWICImagingFactory> pWicFactory;
                if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWicFactory)))) {
                    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
                    if (SUCCEEDED(pWicFactory->CreateDecoderFromFilename(p.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder))) {
                        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
                        if (SUCCEEDED(pDecoder->GetFrame(0, &pFrame))) {
                            Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
                            if (SUCCEEDED(pWicFactory->CreateFormatConverter(&pConverter))) {
                                if (SUCCEEDED(pConverter->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                                    pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &outBmp);
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
    };

    loadOne(L"python.png", m_bmpPython);
    loadOne(L"php.png", m_bmpPhp);
    loadOne(L"node.png", m_bmpNode);
}

void PackageManagerView::RenderBrandedIcon(ID2D1RenderTarget* pRT, Theme& theme, RuntimeLanguage lang, const D2D1_RECT_F& bounds) {
    ID2D1Bitmap* pBmp = nullptr;
    if (lang == RuntimeLanguage::Python && m_bmpPython) pBmp = m_bmpPython.Get();
    else if (lang == RuntimeLanguage::Php && m_bmpPhp) pBmp = m_bmpPhp.Get();
    else if (lang == RuntimeLanguage::Node && m_bmpNode) pBmp = m_bmpNode.Get();

    if (pBmp) {
        D2D1_SIZE_F sz = pBmp->GetSize();
        if (sz.width > 0.0f && sz.height > 0.0f) {
            float bw = bounds.right - bounds.left;
            float bh = bounds.bottom - bounds.top;
            float scale = (std::min)(bw / sz.width, bh / sz.height);
            float drawW = sz.width * scale;
            float drawH = sz.height * scale;
            float drawX = bounds.left + (bw - drawW) / 2.0f;
            float drawY = bounds.top + (bh - drawH) / 2.0f;
            pRT->DrawBitmap(pBmp, D2D1::RectF(drawX, drawY, drawX + drawW, drawY + drawH), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
    }

    // High quality vector branded graphics fallback matching ExtensionsPanel
    float cx = (bounds.left + bounds.right) / 2.0f;
    float cy = (bounds.top + bounds.bottom) / 2.0f;
    float s = (std::min)(bounds.right - bounds.left, bounds.bottom - bounds.top);

    if (lang == RuntimeLanguage::Python) {
        // Python Two-Tone Blue & Yellow icon
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBlue, bYellow;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.46f, 0.67f), &bBlue);
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.83f, 0.22f), &bYellow);

        float r = s * 0.38f;
        D2D1_ROUNDED_RECT rrcTop = D2D1::RoundedRect(D2D1::RectF(cx - r, cy - r, cx + r * 0.3f, cy + r * 0.3f), 4.0f, 4.0f);
        D2D1_ROUNDED_RECT rrcBot = D2D1::RoundedRect(D2D1::RectF(cx - r * 0.3f, cy - r * 0.3f, cx + r, cy + r), 4.0f, 4.0f);
        if (bBlue) pRT->FillRoundedRectangle(rrcTop, bBlue.Get());
        if (bYellow) pRT->FillRoundedRectangle(rrcBot, bYellow.Get());

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bWhite;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &bWhite);
        if (bWhite) {
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.45f, cy - r * 0.45f), 2.0f, 2.0f), bWhite.Get());
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.45f, cy + r * 0.45f), 2.0f, 2.0f), bWhite.Get());
        }
    } else if (lang == RuntimeLanguage::Php) {
        // PHP Indigo Pill
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bPhp;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.47f, 0.48f, 0.71f), &bPhp);
        D2D1_ROUNDED_RECT rrcPhp = D2D1::RoundedRect(bounds, 8.0f, 8.0f);
        if (bPhp) pRT->FillRoundedRectangle(rrcPhp, bPhp.Get());

        IDWriteTextFormat* pFmt = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontTitle.Get();
        if (pFmt && theme.brushTextWhite) {
            pRT->DrawText(L"PHP", 3, pFmt, bounds, theme.brushTextWhite.Get());
        }
    } else {
        // Node.js Hex Green
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bNode;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.60f, 0.20f), &bNode);
        D2D1_ROUNDED_RECT rrcNode = D2D1::RoundedRect(bounds, 8.0f, 8.0f);
        if (bNode) pRT->FillRoundedRectangle(rrcNode, bNode.Get());

        IDWriteTextFormat* pFmt = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontTitle.Get();
        if (pFmt && theme.brushTextWhite) {
            pRT->DrawText(L"node", 4, pFmt, bounds, theme.brushTextWhite.Get());
        }
    }
}

std::vector<PackageEntry>& PackageManagerView::GetActivePackageList() {
    if (m_activeLanguage == RuntimeLanguage::Php) return m_phpPackages;
    if (m_activeLanguage == RuntimeLanguage::Node) return m_nodePackages;
    return m_packages;
}

const std::vector<PackageEntry>& PackageManagerView::GetActivePackageList() const {
    if (m_activeLanguage == RuntimeLanguage::Php) return m_phpPackages;
    if (m_activeLanguage == RuntimeLanguage::Node) return m_nodePackages;
    return m_packages;
}

void PackageManagerView::InitializeCatalog() {
    {
        std::lock_guard<std::mutex> lock(m_packageMutex);
        m_packages.clear();
        m_phpPackages.clear();
        m_nodePackages.clear();
    }

    // Connect to Supabase to fetch real database packages asynchronously!
    FetchInitialPackagesFromSupabase(RuntimeLanguage::Python);
    FetchInitialPackagesFromSupabase(RuntimeLanguage::Php);
    FetchInitialPackagesFromSupabase(RuntimeLanguage::Node);
}

void PackageManagerView::ParseAndMergeSupabasePackages(const std::string& responseBody, RuntimeLanguage lang) {
    if (responseBody.empty() || responseBody.front() != '[') return;

    bool isPhp = (lang == RuntimeLanguage::Php);
    bool isNode = (lang == RuntimeLanguage::Node);
    std::vector<PackageEntry> parsedItems;

    size_t pos = 0;
    while ((pos = responseBody.find('{', pos)) != std::string::npos) {
        size_t endObj = responseBody.find('}', pos);
        if (endObj == std::string::npos) break;
        std::string objStr = responseBody.substr(pos, endObj - pos + 1);

        auto extractField = [&](const std::string& fieldName) -> std::wstring {
            std::string needle = "\"" + fieldName + "\"";
            size_t fp = objStr.find(needle);
            if (fp == std::string::npos) return L"";
            size_t colon = objStr.find(':', fp + needle.length());
            if (colon == std::string::npos) return L"";
            size_t valStart = objStr.find_first_not_of(" \t\r\n", colon + 1);
            if (valStart == std::string::npos) return L"";
            if (objStr[valStart] == '"') {
                size_t valEnd = objStr.find('"', valStart + 1);
                if (valEnd != std::string::npos) {
                    std::string v = objStr.substr(valStart + 1, valEnd - valStart - 1);
                    std::wstring wv;
                    wv.reserve(v.size());
                    for (char c : v) wv.push_back(static_cast<wchar_t>(c));
                    return wv;
                }
            }
            return L"";
        };

        std::wstring name = extractField("name");
        std::wstring dl = extractField("download");
        std::wstring gh = extractField("github");

        if (!name.empty()) {
            PackageEntry pe;
            pe.name = name;
            if (isPhp) {
                pe.download = dl.empty() ? (L"composer require " + name) : dl;
                if (!gh.empty()) {
                    pe.github = gh;
                } else if (name.find(L'/') != std::wstring::npos) {
                    pe.github = L"https://github.com/" + name;
                } else {
                    pe.github = L"https://github.com/" + name + L"/" + name;
                }
                pe.topics = L"#php #composer #packagist";
            } else if (isNode) {
                pe.download = dl.empty() ? (L"npm install " + name) : dl;
                if (!gh.empty()) {
                    pe.github = gh;
                } else if (!name.empty() && name[0] == L'@') {
                    // E.g. @11ty/eleventy-utils -> https://github.com/11ty/eleventy-utils
                    pe.github = L"https://github.com/" + name.substr(1);
                } else {
                    pe.github = L"https://github.com/npm/" + name;
                }
                pe.topics = L"#node #npm #javascript";
            } else {
                pe.download = dl.empty() ? (L"pip install " + name) : dl;
                pe.github = gh.empty() ? (L"https://github.com/pypa/" + name) : gh;
                pe.topics = L"#python #package";
            }
            pe.version = L"";
            pe.summary = pe.download; // Authentic command directly from database
            pe.license = L"";
            pe.stars = L"";
            pe.isRemote = true;
            parsedItems.push_back(std::move(pe));
        }
        pos = endObj + 1;
    }

    if (!parsedItems.empty()) {
        std::lock_guard<std::mutex> lock(m_packageMutex);
        auto& targetList = isPhp ? m_phpPackages : (isNode ? m_nodePackages : m_packages);
        for (auto& item : parsedItems) {
            bool exists = false;
            for (auto& existing : targetList) {
                if (_wcsicmp(existing.name.c_str(), item.name.c_str()) == 0) {
                    exists = true;
                    if (!item.download.empty()) existing.download = item.download;
                    if (!item.github.empty()) existing.github = item.github;
                    break;
                }
            }
            if (!exists) {
                targetList.push_back(std::move(item));
            }
        }
    }

    ApplyFilter();
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void PackageManagerView::FetchInitialPackagesFromSupabase(RuntimeLanguage lang) {
    std::thread([this, lang]() {
        HINTERNET hSession = WinHttpOpen(L"phide-IDE/1.0",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return;
        WinHttpSetTimeouts(hSession, 4000, 4000, 4000, 4000);

        HINTERNET hConnect = WinHttpConnect(hSession, L"lzwtgcbvuoogpzcgujdb.supabase.co",
                                            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return;
        }

        std::wstring table = (lang == RuntimeLanguage::Php) ? L"php_package" : (lang == RuntimeLanguage::Node ? L"node_package" : L"python_package");
        std::wstring headers = L"apikey: sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry\r\nAuthorization: Bearer sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry\r\nAccept: application/json\r\n";

        // Paginate in chunks of 1000 to stream all packages from database
        int maxOffset = (lang == RuntimeLanguage::Node) ? 25000 : 5000;
        for (int offset = 0; offset < maxOffset; offset += 1000) {
            std::wstring path = L"/rest/v1/" + table + L"?select=name,download,github&limit=1000&offset=" + std::to_wstring(offset);

            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
                                                    path.c_str(),
                                                    nullptr, WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    WINHTTP_FLAG_SECURE);
            if (!hRequest) break;

            BOOL bSend = WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(headers.length()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            std::string responseBody;
            if (bSend && WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD bytesAvail = 0;
                while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail > 0) {
                    std::vector<char> buf(bytesAvail);
                    DWORD bytesRead = 0;
                    if (WinHttpReadData(hRequest, buf.data(), bytesAvail, &bytesRead) && bytesRead > 0) {
                        responseBody.append(buf.data(), bytesRead);
                    } else break;
                }
            }
            WinHttpCloseHandle(hRequest);

            if (responseBody.empty() || responseBody == "[]") {
                break;
            }

            ParseAndMergeSupabasePackages(responseBody, lang);
        }

        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
    }).detach();
}

void PackageManagerView::FetchSupabasePackagesAsync(const std::wstring& query) {
    uint64_t ticket = ++m_searchTicket;
    std::wstring q = query;
    RuntimeLanguage lang = m_activeLanguage;

    std::thread([this, ticket, q, lang]() {
        if (!q.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (ticket != m_searchTicket.load()) {
                return;
            }
        }

        m_isFetchingRemote.store(true);

        HINTERNET hSession = WinHttpOpen(L"phide-IDE/1.0",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            m_isFetchingRemote.store(false);
            return;
        }
        WinHttpSetTimeouts(hSession, 4000, 4000, 4000, 4000);

        HINTERNET hConnect = WinHttpConnect(hSession, L"lzwtgcbvuoogpzcgujdb.supabase.co",
                                            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            m_isFetchingRemote.store(false);
            return;
        }

        std::wstring table = (lang == RuntimeLanguage::Php) ? L"php_package" : (lang == RuntimeLanguage::Node ? L"node_package" : L"python_package");
        std::wstring path = L"/rest/v1/" + table + L"?select=name,download,github&limit=100";
        if (!q.empty()) {
            path += L"&name=ilike.*" + q + L"*";
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
                                                path.c_str(),
                                                nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            m_isFetchingRemote.store(false);
            return;
        }

        std::wstring headers = L"apikey: sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry\r\nAuthorization: Bearer sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry\r\nAccept: application/json\r\n";
        BOOL bSend = WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(headers.length()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (bSend && WinHttpReceiveResponse(hRequest, nullptr)) {
            std::string responseBody;
            DWORD bytesAvail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail > 0) {
                std::vector<char> buf(bytesAvail);
                DWORD bytesRead = 0;
                if (WinHttpReadData(hRequest, buf.data(), bytesAvail, &bytesRead) && bytesRead > 0) {
                    responseBody.append(buf.data(), bytesRead);
                } else break;
            }

            m_remoteFetchSuccess.store(true);
            ParseAndMergeSupabasePackages(responseBody, lang);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        m_isFetchingRemote.store(false);
    }).detach();
}

int PackageManagerView::ComputeRelevance(const std::wstring& text, const std::wstring& query) const {
    if (query.empty()) return 0; // Initial is NONE!
    if (text.empty()) return 0;

    std::wstring lowerText = text;
    for (auto& c : lowerText) c = static_cast<wchar_t>(towlower(c));
    std::wstring lowerQuery = query;
    for (auto& c : lowerQuery) c = static_cast<wchar_t>(towlower(c));

    // 1. Exact match
    if (lowerText == lowerQuery) return 1000;

    // 2. Starts with query (prefix match)
    if (lowerText.rfind(lowerQuery, 0) == 0) {
        return 500 + static_cast<int>(100.0f * (static_cast<float>(query.length()) / static_cast<float>(text.length())));
    }

    // 3. Word boundary match
    size_t hyphenPos = lowerText.find(L'-');
    if (hyphenPos != std::wstring::npos && lowerText.substr(hyphenPos + 1).rfind(lowerQuery, 0) == 0) {
        return 350;
    }
    size_t slashPos = lowerText.find(L'/');
    if (slashPos != std::wstring::npos && lowerText.substr(slashPos + 1).rfind(lowerQuery, 0) == 0) {
        return 350;
    }

    // 4. Substring match
    size_t subPos = lowerText.find(lowerQuery);
    if (subPos != std::wstring::npos) {
        return 200 - static_cast<int>(subPos * 4);
    }

    // 5. Sequential character match (subsequence)
    size_t qIdx = 0;
    int gaps = 0;
    size_t firstMatch = std::wstring::npos;
    for (size_t tIdx = 0; tIdx < lowerText.length() && qIdx < lowerQuery.length(); ++tIdx) {
        if (lowerText[tIdx] == lowerQuery[qIdx]) {
            if (firstMatch == std::wstring::npos) firstMatch = tIdx;
            ++qIdx;
        } else if (qIdx > 0) {
            ++gaps;
        }
    }
    if (qIdx == lowerQuery.length()) {
        int score = 100 - (gaps * 5) - static_cast<int>(firstMatch * 2);
        return (std::max)(10, score);
    }

    return 0;
}

void PackageManagerView::ApplyFilter() {
    std::lock_guard<std::mutex> lock(m_packageMutex);
    m_filteredIndices.clear();

    // User requirement: "the output is initial none and when use type it would load first what seeable in screen"
    if (m_searchQuery.empty()) {
        m_selectedIndex = -1;
        return;
    }

    auto& activeList = GetActivePackageList();

    struct ScoredItem {
        size_t index;
        int score;
    };
    std::vector<ScoredItem> scored;

    for (size_t i = 0; i < activeList.size(); ++i) {
        int score = ComputeRelevance(activeList[i].name, m_searchQuery);
        if (score > 0) {
            activeList[i].searchScore = score;
            scored.push_back({ i, score });
        }
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredItem& a, const ScoredItem& b) {
        return a.score > b.score;
    });

    for (const auto& item : scored) {
        m_filteredIndices.push_back(item.index);
    }

    if (m_selectedIndex >= static_cast<int>(m_filteredIndices.size()) || m_selectedIndex < 0) {
        m_selectedIndex = m_filteredIndices.empty() ? -1 : 0;
    }
}

void PackageManagerView::SetSearchQuery(const std::wstring& q) {
    m_searchQuery = q;
    m_searchCaretPos = q.length();
    m_scrollIndex = 0;
    m_visibleLimit = 12; // Initial chunk visible on screen
    ApplyFilter();
    FetchSupabasePackagesAsync(q);
}

void PackageManagerView::SetSelectedIndex(int idx) {
    if (idx >= 0 && idx < static_cast<int>(m_filteredIndices.size())) {
        m_selectedIndex = idx;
        const auto& activeList = GetActivePackageList();
        size_t pkgIdx = m_filteredIndices[idx];
        if (pkgIdx < activeList.size()) {
            NavigateToPackage(activeList[pkgIdx].github);
        }
    }
}

void PackageManagerView::SetActiveLanguage(RuntimeLanguage lang) {
    m_activeLanguage = lang;
    m_scrollIndex = 0;
    m_selectedIndex = -1;
    m_searchQuery.clear();
    m_searchCaretPos = 0;
    m_activeDropdownPkgIdx = -1;
    ApplyFilter();
}

void PackageManagerView::SelectLanguageAndOpenSearch(RuntimeLanguage lang) {
    SetActiveLanguage(lang);
    m_viewState = PkgViewState::PackageSearch;
    m_isSearchFocused = true;
    m_searchCaretPos = 0;
    FetchInitialPackagesFromSupabase(lang);
}

void PackageManagerView::NavigateBackToLanguages() {
    m_viewState = PkgViewState::LanguageGrid;
    m_activeDropdownPkgIdx = -1;
    m_isSearchFocused = false;
    HideWebView();
}

bool PackageManagerView::HandleMouseMove(POINT clientPt) {
    bool needRepaint = false;
    float mx = static_cast<float>(clientPt.x);
    float my = static_cast<float>(clientPt.y);

    if (m_viewState == PkgViewState::LanguageGrid) {
        for (auto& card : m_languageCards) {
            bool h = (mx >= card.bounds.left && mx <= card.bounds.right &&
                      my >= card.bounds.top && my <= card.bounds.bottom);
            if (h != card.isHovered) {
                card.isHovered = h;
                needRepaint = true;
            }
        }
        return needRepaint;
    }

    // Hover on Back button
    bool hoverBack = (mx >= m_rcBackBtn.left && mx <= m_rcBackBtn.right &&
                      my >= m_rcBackBtn.top && my <= m_rcBackBtn.bottom);
    if (hoverBack != m_isHoveringBack) {
        m_isHoveringBack = hoverBack;
        needRepaint = true;
    }

    // Search box hover sets I-beam cursor
    if (mx >= m_rcSearchBox.left && mx <= m_rcSearchBox.right &&
        my >= m_rcSearchBox.top && my <= m_rcSearchBox.bottom) {
        SetCursor(LoadCursor(NULL, IDC_IBEAM));
    }

    int prevHovered = m_hoveredIndex;
    m_hoveredIndex = -1;

    auto& activeList = GetActivePackageList();

    // Check package item hover
    for (size_t i = m_scrollIndex; i < (std::min)(m_filteredIndices.size(), m_scrollIndex + m_visibleLimit); ++i) {
        size_t pkgIdx = m_filteredIndices[i];
        const auto& b = activeList[pkgIdx].bounds;
        if (mx >= b.left && mx <= b.right && my >= b.top && my <= b.bottom) {
            m_hoveredIndex = static_cast<int>(i);
            break;
        }
    }
    if (m_hoveredIndex != prevHovered) needRepaint = true;

    // Hover on GitHub link in preview
    bool hoverGh = (mx >= m_rcGithubHeaderLink.left && mx <= m_rcGithubHeaderLink.right &&
                    my >= m_rcGithubHeaderLink.top && my <= m_rcGithubHeaderLink.bottom);
    if (hoverGh != m_isHoveringGithubLink) {
        m_isHoveringGithubLink = hoverGh;
        needRepaint = true;
    }

    // Hover on dropdown items
    if (m_activeDropdownPkgIdx >= 0) {
        bool hProj = (mx >= m_rcDropdownProject.left && mx <= m_rcDropdownProject.right &&
                      my >= m_rcDropdownProject.top && my <= m_rcDropdownProject.bottom);
        bool hGlob = (mx >= m_rcDropdownGlobal.left && mx <= m_rcDropdownGlobal.right &&
                      my >= m_rcDropdownGlobal.top && my <= m_rcDropdownGlobal.bottom);
        if (hProj != m_isHoveringDropdownProject || hGlob != m_isHoveringDropdownGlobal) {
            m_isHoveringDropdownProject = hProj;
            m_isHoveringDropdownGlobal = hGlob;
            needRepaint = true;
        }
    }

    return needRepaint;
}

LPCWSTR PackageManagerView::GetCursorForPoint(POINT clientPt) const noexcept {
    float mx = static_cast<float>(clientPt.x);
    float my = static_cast<float>(clientPt.y);

    if (m_viewState == PkgViewState::LanguageGrid) {
        for (const auto& card : m_languageCards) {
            if (mx >= card.bounds.left && mx <= card.bounds.right &&
                my >= card.bounds.top && my <= card.bounds.bottom) {
                return IDC_HAND;
            }
        }
        return IDC_ARROW;
    }

    // 1. Dropdown items if menu is currently open
    if (m_activeDropdownPkgIdx >= 0) {
        if ((mx >= m_rcDropdownProject.left && mx <= m_rcDropdownProject.right &&
             my >= m_rcDropdownProject.top && my <= m_rcDropdownProject.bottom) ||
            (mx >= m_rcDropdownGlobal.left && mx <= m_rcDropdownGlobal.right &&
             my >= m_rcDropdownGlobal.top && my <= m_rcDropdownGlobal.bottom)) {
            return IDC_HAND;
        }
    }

    // 2. Back Button
    if (mx >= m_rcBackBtn.left && mx <= m_rcBackBtn.right &&
        my >= m_rcBackBtn.top && my <= m_rcBackBtn.bottom) {
        return IDC_HAND;
    }

    // 3. Search Box & Clear Button
    if (mx >= m_rcClearSearch.left && mx <= m_rcClearSearch.right &&
        my >= m_rcClearSearch.top && my <= m_rcClearSearch.bottom && !m_searchQuery.empty()) {
        return IDC_HAND;
    }
    if (mx >= m_rcSearchBox.left && mx <= m_rcSearchBox.right &&
        my >= m_rcSearchBox.top && my <= m_rcSearchBox.bottom) {
        return IDC_IBEAM;
    }

    // 4. External link or buttons in GitHub preview
    if ((mx >= m_rcGithubHeaderLink.left && mx <= m_rcGithubHeaderLink.right &&
         my >= m_rcGithubHeaderLink.top && my <= m_rcGithubHeaderLink.bottom) ||
        (mx >= m_rcGithubOpenBtn.left && mx <= m_rcGithubOpenBtn.right &&
         my >= m_rcGithubOpenBtn.top && my <= m_rcGithubOpenBtn.bottom)) {
        return IDC_HAND;
    }

    // 5. Output package card items and buttons
    const auto& activeList = GetActivePackageList();
    for (size_t i = m_scrollIndex; i < (std::min)(m_filteredIndices.size(), m_scrollIndex + m_visibleLimit); ++i) {
        size_t pkgIdx = m_filteredIndices[i];
        if (pkgIdx >= activeList.size()) continue;
        const auto& pkg = activeList[pkgIdx];

        // Install button or split dropdown arrow -> POINTER (IDC_HAND)
        if (mx >= pkg.rcInstallBtn.left && mx <= pkg.rcInstallBtn.right &&
            my >= pkg.rcInstallBtn.top && my <= pkg.rcInstallBtn.bottom) {
            return IDC_HAND;
        }
        if (mx >= pkg.rcSplitArrowBtn.left && mx <= pkg.rcSplitArrowBtn.right &&
            my >= pkg.rcSplitArrowBtn.top && my <= pkg.rcSplitArrowBtn.bottom) {
            return IDC_HAND;
        }
        if (mx >= pkg.bounds.left && mx <= pkg.bounds.right &&
            my >= pkg.bounds.top && my <= pkg.bounds.bottom) {
            return IDC_HAND;
        }
    }

    return IDC_ARROW;
}

void PackageManagerView::MarkPackageInstalled(const std::wstring& pkgName, PkgInstallScope scope) {
    std::lock_guard<std::mutex> lock(m_packageMutex);
    for (auto& pkg : m_packages) {
        if (_wcsicmp(pkg.name.c_str(), pkgName.c_str()) == 0) {
            pkg.isInstalling = false;
            if (scope == PkgInstallScope::Project) pkg.isInstalledProject = true;
            else pkg.isInstalledGlobal = true;
            pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
        }
    }
    for (auto& pkg : m_phpPackages) {
        if (_wcsicmp(pkg.name.c_str(), pkgName.c_str()) == 0) {
            pkg.isInstalling = false;
            if (scope == PkgInstallScope::Project) pkg.isInstalledProject = true;
            else pkg.isInstalledGlobal = true;
            pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
        }
    }
    for (auto& pkg : m_nodePackages) {
        if (_wcsicmp(pkg.name.c_str(), pkgName.c_str()) == 0) {
            pkg.isInstalling = false;
            if (scope == PkgInstallScope::Project) pkg.isInstalledProject = true;
            else pkg.isInstalledGlobal = true;
            pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
        }
    }
}

void PackageManagerView::MarkPackageUninstalled(const std::wstring& pkgName, PkgInstallScope scope) {
    std::lock_guard<std::mutex> lock(m_packageMutex);
    for (auto& pkg : m_packages) {
        if (_wcsicmp(pkg.name.c_str(), pkgName.c_str()) == 0) {
            pkg.isInstalling = false;
            if (scope == PkgInstallScope::Project) pkg.isInstalledProject = false;
            else pkg.isInstalledGlobal = false;
            pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
        }
    }
    for (auto& pkg : m_phpPackages) {
        if (_wcsicmp(pkg.name.c_str(), pkgName.c_str()) == 0) {
            pkg.isInstalling = false;
            if (scope == PkgInstallScope::Project) pkg.isInstalledProject = false;
            else pkg.isInstalledGlobal = false;
            pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
        }
    }
    for (auto& pkg : m_nodePackages) {
        if (_wcsicmp(pkg.name.c_str(), pkgName.c_str()) == 0) {
            pkg.isInstalling = false;
            if (scope == PkgInstallScope::Project) pkg.isInstalledProject = false;
            else pkg.isInstalledGlobal = false;
            pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
        }
    }
}

void PackageManagerView::ScanInstalledPackages(const std::wstring& wsFolder) {
    std::unordered_set<std::wstring> projectInstalled;
    std::unordered_set<std::wstring> globalInstalled;
    std::error_code ec;

    auto addFromSitePackages = [&](const std::filesystem::path& spDir, std::unordered_set<std::wstring>& outSet) {
        if (!std::filesystem::exists(spDir, ec) || !std::filesystem::is_directory(spDir, ec)) return;
        for (const auto& entry : std::filesystem::directory_iterator(spDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (entry.is_directory(ec)) {
                std::wstring name = entry.path().filename().wstring();
                auto distPos = name.find(L"-");
                if (distPos != std::wstring::npos && (name.find(L".dist-info") != std::wstring::npos || name.find(L".egg-info") != std::wstring::npos)) {
                    name = name.substr(0, distPos);
                }
                std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                std::replace(name.begin(), name.end(), L'_', L'-');
                outSet.insert(name);
            }
        }
    };

    // 1. Check workspace virtualenv site-packages if workspace is provided
    if (!wsFolder.empty()) {
        std::filesystem::path venvSp = std::filesystem::path(wsFolder) / L".venv" / L"Lib" / L"site-packages";
        addFromSitePackages(venvSp, projectInstalled);
    }

    // 2. Check global python site-packages (from system python or LOCALAPPDATA)
    wchar_t szLocalApp[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", szLocalApp, MAX_PATH) > 0) {
        std::filesystem::path pyBase = std::filesystem::path(szLocalApp) / L"Programs" / L"Python";
        if (std::filesystem::exists(pyBase, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(pyBase, ec)) {
                if (entry.is_directory(ec)) {
                    addFromSitePackages(entry.path() / L"Lib" / L"site-packages", globalInstalled);
                }
            }
        }
    }
    wchar_t pyFound[MAX_PATH] = { 0 };
    if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, pyFound, nullptr) > 0) {
        std::filesystem::path spPath = std::filesystem::path(pyFound).parent_path() / L"Lib" / L"site-packages";
        addFromSitePackages(spPath, globalInstalled);
    }

    std::unordered_set<std::wstring> projectPhpInstalled;
    std::unordered_set<std::wstring> globalPhpInstalled;

    auto addFromComposerVendor = [&](const std::filesystem::path& vDir, std::unordered_set<std::wstring>& outSet) {
        if (!std::filesystem::exists(vDir, ec) || !std::filesystem::is_directory(vDir, ec)) return;
        for (const auto& vendorEntry : std::filesystem::directory_iterator(vDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (vendorEntry.is_directory(ec)) {
                std::wstring vendorName = vendorEntry.path().filename().wstring();
                if (vendorName == L"composer" || vendorName == L"bin") continue;
                for (const auto& pkgEntry : std::filesystem::directory_iterator(vendorEntry.path(), std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (pkgEntry.is_directory(ec)) {
                        std::wstring pkgName = pkgEntry.path().filename().wstring();
                        std::wstring fullName = vendorName + L"/" + pkgName;
                        std::transform(fullName.begin(), fullName.end(), fullName.begin(), ::towlower);
                        outSet.insert(fullName);
                    }
                }
            }
        }
    };

    if (!wsFolder.empty()) {
        addFromComposerVendor(std::filesystem::path(wsFolder) / L"vendor", projectPhpInstalled);
        std::filesystem::path wsCompJson = std::filesystem::path(wsFolder) / L"composer.json";
        if (std::filesystem::exists(wsCompJson, ec)) {
            std::ifstream ifs(wsCompJson);
            if (ifs.is_open()) {
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                std::string content = buffer.str();
                for (const auto& p : m_phpPackages) {
                    std::string narrowName;
                    narrowName.reserve(p.name.length());
                    for (wchar_t wc : p.name) narrowName.push_back(static_cast<char>(wc & 0x7F));
                    if (content.find("\"" + narrowName + "\"") != std::string::npos) {
                        std::wstring norm = p.name;
                        std::transform(norm.begin(), norm.end(), norm.begin(), ::towlower);
                        projectPhpInstalled.insert(norm);
                    }
                }
            }
        }
    }

    wchar_t szAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"APPDATA", szAppData, MAX_PATH) > 0) {
        std::filesystem::path gCompVendor = std::filesystem::path(szAppData) / L"Composer" / L"vendor";
        addFromComposerVendor(gCompVendor, globalPhpInstalled);
    }

    std::lock_guard<std::mutex> lock(m_packageMutex);
    for (auto& pkg : m_packages) {
        std::wstring norm = pkg.name;
        std::transform(norm.begin(), norm.end(), norm.begin(), ::towlower);
        std::replace(norm.begin(), norm.end(), L'_', L'-');
        pkg.isInstalledProject = (projectInstalled.count(norm) > 0);
        pkg.isInstalledGlobal = (globalInstalled.count(norm) > 0);
        pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
    }
    for (auto& pkg : m_phpPackages) {
        std::wstring norm = pkg.name;
        std::transform(norm.begin(), norm.end(), norm.begin(), ::towlower);
        pkg.isInstalledProject = (projectPhpInstalled.count(norm) > 0);
        pkg.isInstalledGlobal = (globalPhpInstalled.count(norm) > 0);
        pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
    }

    std::unordered_set<std::wstring> projectNodeInstalled;
    std::unordered_set<std::wstring> globalNodeInstalled;

    auto addFromNodeModules = [&](const std::filesystem::path& nmPath, std::unordered_set<std::wstring>& outSet) {
        if (!std::filesystem::exists(nmPath, ec) || !std::filesystem::is_directory(nmPath, ec)) return;
        for (const auto& entry : std::filesystem::directory_iterator(nmPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (entry.is_directory(ec)) {
                std::wstring dirName = entry.path().filename().wstring();
                if (!dirName.empty() && dirName[0] == L'@') {
                    for (const auto& subEntry : std::filesystem::directory_iterator(entry.path(), std::filesystem::directory_options::skip_permission_denied, ec)) {
                        if (subEntry.is_directory(ec)) {
                            std::wstring scopedName = dirName + L"/" + subEntry.path().filename().wstring();
                            std::transform(scopedName.begin(), scopedName.end(), scopedName.begin(), ::towlower);
                            outSet.insert(scopedName);
                        }
                    }
                } else {
                    std::transform(dirName.begin(), dirName.end(), dirName.begin(), ::towlower);
                    outSet.insert(dirName);
                }
            }
        }
    };

    if (!wsFolder.empty()) {
        addFromNodeModules(std::filesystem::path(wsFolder) / L"node_modules", projectNodeInstalled);
        std::filesystem::path pkgJson = std::filesystem::path(wsFolder) / L"package.json";
        if (std::filesystem::exists(pkgJson, ec)) {
            std::ifstream ifs(pkgJson);
            if (ifs.is_open()) {
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                std::string content = buffer.str();
                for (const auto& p : m_nodePackages) {
                    std::string narrowName;
                    narrowName.reserve(p.name.length());
                    for (wchar_t wc : p.name) narrowName.push_back(static_cast<char>(wc & 0x7F));
                    if (content.find("\"" + narrowName + "\"") != std::string::npos) {
                        std::wstring norm = p.name;
                        std::transform(norm.begin(), norm.end(), norm.begin(), ::towlower);
                        projectNodeInstalled.insert(norm);
                    }
                }
            }
        }
    }

    if (szAppData[0] != 0) {
        std::filesystem::path gNodeModules = std::filesystem::path(szAppData) / L"npm" / L"node_modules";
        addFromNodeModules(gNodeModules, globalNodeInstalled);
    }
    std::filesystem::path progNode = L"C:\\Program Files\\nodejs\\node_modules";
    addFromNodeModules(progNode, globalNodeInstalled);

    for (auto& pkg : m_nodePackages) {
        std::wstring norm = pkg.name;
        std::transform(norm.begin(), norm.end(), norm.begin(), ::towlower);
        pkg.isInstalledProject = (projectNodeInstalled.count(norm) > 0);
        pkg.isInstalledGlobal = (globalNodeInstalled.count(norm) > 0);
        pkg.isInstalled = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
    }
}

bool PackageManagerView::HandleLButtonDown(POINT clientPt) {
    float mx = static_cast<float>(clientPt.x);
    float my = static_cast<float>(clientPt.y);

    // =========================================================================
    // 1. LanguageGrid Screen Interaction
    // =========================================================================
    if (m_viewState == PkgViewState::LanguageGrid) {
        for (const auto& card : m_languageCards) {
            if (mx >= card.bounds.left && mx <= card.bounds.right &&
                my >= card.bounds.top && my <= card.bounds.bottom) {
                SelectLanguageAndOpenSearch(card.lang);
                return true;
            }
        }
        return false;
    }

    // =========================================================================
    // 2. PackageSearch Screen Interaction
    // =========================================================================
    // A. Back Button
    if (mx >= m_rcBackBtn.left && mx <= m_rcBackBtn.right &&
        my >= m_rcBackBtn.top && my <= m_rcBackBtn.bottom) {
        NavigateBackToLanguages();
        return true;
    }

    // B. Active Dropdown Menu Clicks (Project vs Global)
    if (m_activeDropdownPkgIdx >= 0) {
        if (mx >= m_rcDropdownProject.left && mx <= m_rcDropdownProject.right &&
            my >= m_rcDropdownProject.top && my <= m_rcDropdownProject.bottom) {
            auto& activeList = GetActivePackageList();
            if (m_activeDropdownPkgIdx < static_cast<int>(activeList.size())) {
                activeList[m_activeDropdownPkgIdx].scope = PkgInstallScope::Project;
                activeList[m_activeDropdownPkgIdx].isInstalled = activeList[m_activeDropdownPkgIdx].isInstalledProject;
            }
            m_activeDropdownPkgIdx = -1;
            return true;
        }
        if (mx >= m_rcDropdownGlobal.left && mx <= m_rcDropdownGlobal.right &&
            my >= m_rcDropdownGlobal.top && my <= m_rcDropdownGlobal.bottom) {
            auto& activeList = GetActivePackageList();
            if (m_activeDropdownPkgIdx < static_cast<int>(activeList.size())) {
                activeList[m_activeDropdownPkgIdx].scope = PkgInstallScope::Global;
                activeList[m_activeDropdownPkgIdx].isInstalled = activeList[m_activeDropdownPkgIdx].isInstalledGlobal;
            }
            m_activeDropdownPkgIdx = -1;
            return true;
        }
        // Clicked outside dropdown -> dismiss
        m_activeDropdownPkgIdx = -1;
    }

    // C. Search Box Focus & Clear
    if (mx >= m_rcClearSearch.left && mx <= m_rcClearSearch.right &&
        my >= m_rcClearSearch.top && my <= m_rcClearSearch.bottom && !m_searchQuery.empty()) {
        SetSearchQuery(L"");
        m_isSearchFocused = true;
        m_searchCaretPos = 0;
        return true;
    }
    if (mx >= m_rcSearchBox.left && mx <= m_rcSearchBox.right &&
        my >= m_rcSearchBox.top && my <= m_rcSearchBox.bottom) {
        m_isSearchFocused = true;
        // Hit-test caret position based on horizontal mouse offset
        float relX = mx - (m_rcSearchBox.left + 30.0f);
        if (relX <= 0.0f || m_searchQuery.empty()) {
            m_searchCaretPos = 0;
        } else {
            size_t pos = static_cast<size_t>(relX / 7.5f + 0.5f);
            m_searchCaretPos = (std::min)(pos, m_searchQuery.length());
        }
        return true;
    }

    auto& activeList = GetActivePackageList();

    // D. Package Card Clicks (Install Button, Split Dropdown Arrow, Select Card)
    for (size_t i = m_scrollIndex; i < (std::min)(m_filteredIndices.size(), m_scrollIndex + m_visibleLimit); ++i) {
        size_t pkgIdx = m_filteredIndices[i];
        auto& pkg = activeList[pkgIdx];

        // 1. Split Dropdown Arrow Click ("▾")
        if (mx >= pkg.rcSplitArrowBtn.left && mx <= pkg.rcSplitArrowBtn.right &&
            my >= pkg.rcSplitArrowBtn.top && my <= pkg.rcSplitArrowBtn.bottom) {
            m_activeDropdownPkgIdx = (m_activeDropdownPkgIdx == static_cast<int>(pkgIdx)) ? -1 : static_cast<int>(pkgIdx);
            return true;
        }

        // 2. Main Install / Uninstall Button Click
        if (mx >= pkg.rcInstallBtn.left && mx <= pkg.rcInstallBtn.right &&
            my >= pkg.rcInstallBtn.top && my <= pkg.rcInstallBtn.bottom) {
            bool isInstalledForCurrentScope = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
            pkg.isInstalled = isInstalledForCurrentScope;
            std::wstring cmdToRun;
            if (pkg.isInstalled) {
                // User requirement: Clicking button when installed uninstalls package
                if (m_activeLanguage == RuntimeLanguage::Php) {
                    cmdToRun = (pkg.scope == PkgInstallScope::Global)
                        ? (L"composer global remove " + pkg.name)
                        : (L"composer remove " + pkg.name);
                } else if (m_activeLanguage == RuntimeLanguage::Node) {
                    cmdToRun = (pkg.scope == PkgInstallScope::Global)
                        ? (L"npm uninstall -g " + pkg.name)
                        : (L"npm uninstall " + pkg.name);
                } else {
                    cmdToRun = L"pip uninstall -y " + pkg.name;
                }
            } else {
                if (m_activeLanguage == RuntimeLanguage::Php) {
                    if (pkg.scope == PkgInstallScope::Global) {
                        cmdToRun = L"composer global require " + pkg.name;
                    } else {
                        if (!pkg.download.empty() && pkg.download.find(L"composer require") != std::wstring::npos) {
                            cmdToRun = pkg.download;
                        } else {
                            cmdToRun = L"composer require " + pkg.name;
                        }
                    }
                } else if (m_activeLanguage == RuntimeLanguage::Node) {
                    if (pkg.scope == PkgInstallScope::Global) {
                        cmdToRun = L"npm install -g " + pkg.name;
                    } else {
                        if (!pkg.download.empty() && pkg.download.find(L"npm install") != std::wstring::npos) {
                            cmdToRun = pkg.download;
                        } else {
                            cmdToRun = L"npm install " + pkg.name;
                        }
                    }
                } else {
                    cmdToRun = (pkg.scope == PkgInstallScope::Global)
                        ? (L"pip install --user " + pkg.name)
                        : (L"pip install " + pkg.name);
                }
            }
            pkg.isInstalling = true;
            if (m_onInstallCallback) {
                m_onInstallCallback(pkg.name, cmdToRun, pkg.scope);
            }
            return true;
        }

        // 3. Card Selection
        if (mx >= pkg.bounds.left && mx <= pkg.bounds.right &&
            my >= pkg.bounds.top && my <= pkg.bounds.bottom) {
            SetSelectedIndex(static_cast<int>(i));
            m_isSearchFocused = false;
            return true;
        }
    }

    // E. Right Preview GitHub Header External Link Click
    if (mx >= m_rcGithubHeaderLink.left && mx <= m_rcGithubHeaderLink.right &&
        my >= m_rcGithubHeaderLink.top && my <= m_rcGithubHeaderLink.bottom) {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredIndices.size())) {
            size_t selIdx = m_filteredIndices[m_selectedIndex];
            ShellExecuteW(nullptr, L"open", activeList[selIdx].github.c_str(), nullptr, nullptr, SW_SHOW);
        }
        return true;
    }

    return false;
}

bool PackageManagerView::HandleMouseWheel(short delta, float mouseY) {
    // USER REQUIREMENT: "fix the drop down when i scroll and the drop down is still open it nmoves also which shouldnt be when i scroll the drop down should closes ."
    m_activeDropdownPkgIdx = -1;

    if (m_viewState == PkgViewState::LanguageGrid) return false;

    // Check if scrolling on right preview
    if (mouseY >= m_rcRightPreview.top && mouseY <= m_rcRightPreview.bottom) {
        float oldPreviewScroll = m_previewScrollY;
        if (delta > 0) {
            m_previewScrollY = (std::max)(0.0f, m_previewScrollY - 45.0f);
        } else {
            m_previewScrollY += 45.0f;
        }
        if (m_previewScrollY != oldPreviewScroll) return true;
    }

    if (m_filteredIndices.empty()) return false;

    size_t oldScroll = m_scrollIndex;
    if (delta > 0) {
        if (m_scrollIndex >= 2) m_scrollIndex -= 2;
        else m_scrollIndex = 0;
    } else {
        if (m_scrollIndex + 2 < m_filteredIndices.size()) {
            m_scrollIndex += 2;
            // Lazy load / virtualize next chunk as user scrolls towards bottom
            if (m_scrollIndex + 8 >= m_visibleLimit && m_visibleLimit < m_filteredIndices.size()) {
                m_visibleLimit += 10;
            }
        }
    }
    return m_scrollIndex != oldScroll;
}

bool PackageManagerView::HandleChar(wchar_t ch) {
    if (m_viewState != PkgViewState::PackageSearch || !m_isSearchFocused) return false;
    if (ch >= 32 && ch != 127) {
        if (m_searchCaretPos > m_searchQuery.length()) m_searchCaretPos = m_searchQuery.length();
        m_searchQuery.insert(m_searchCaretPos, 1, ch);
        m_searchCaretPos++;
        m_lastSearchKeystrokeTime = GetTickCount64();
        m_searchDebouncePending = true;
        m_visibleLimit = 12; // Reset visible chunk on new query
        ApplyFilter();
        FetchSupabasePackagesAsync(m_searchQuery);
        return true;
    }
    return false;
}

bool PackageManagerView::HandleKeyDown(WPARAM key) {
    if (m_viewState != PkgViewState::PackageSearch) return false;

    if (key == VK_BACK) {
        if (m_searchCaretPos > 0 && !m_searchQuery.empty()) {
            if (m_searchCaretPos > m_searchQuery.length()) m_searchCaretPos = m_searchQuery.length();
            m_searchQuery.erase(m_searchCaretPos - 1, 1);
            m_searchCaretPos--;
            m_lastSearchKeystrokeTime = GetTickCount64();
            m_searchDebouncePending = true;
            m_visibleLimit = 12;
            ApplyFilter();
            FetchSupabasePackagesAsync(m_searchQuery);
            return true;
        }
    } else if (key == VK_DELETE) {
        if (m_searchCaretPos < m_searchQuery.length()) {
            m_searchQuery.erase(m_searchCaretPos, 1);
            m_lastSearchKeystrokeTime = GetTickCount64();
            m_searchDebouncePending = true;
            m_visibleLimit = 12;
            ApplyFilter();
            FetchSupabasePackagesAsync(m_searchQuery);
            return true;
        }
    } else if (key == VK_LEFT) {
        if (m_searchCaretPos > 0) {
            m_searchCaretPos--;
            return true;
        }
    } else if (key == VK_RIGHT) {
        if (m_searchCaretPos < m_searchQuery.length()) {
            m_searchCaretPos++;
            return true;
        }
    } else if (key == VK_HOME) {
        if (m_searchCaretPos != 0) {
            m_searchCaretPos = 0;
            return true;
        }
    } else if (key == VK_END) {
        if (m_searchCaretPos != m_searchQuery.length()) {
            m_searchCaretPos = m_searchQuery.length();
            return true;
        }
    } else if (key == VK_ESCAPE) {
        if (m_activeDropdownPkgIdx >= 0) {
            m_activeDropdownPkgIdx = -1;
            return true;
        }
        if (!m_searchQuery.empty()) {
            SetSearchQuery(L"");
            return true;
        }
        NavigateBackToLanguages();
        return true;
    } else if (key == VK_UP || key == VK_DOWN) {
        if (key == VK_UP && m_selectedIndex > 0) {
            SetSelectedIndex(m_selectedIndex - 1);
            return true;
        }
        if (key == VK_DOWN && m_selectedIndex + 1 < static_cast<int>(m_filteredIndices.size())) {
            SetSelectedIndex(m_selectedIndex + 1);
            return true;
        }
    }
    return false;
}

void PackageManagerView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                float left, float width, float top, float height) {
    if (!pRT || width <= 10.0f || height <= 10.0f) return;

    // Load branded assets on demand
    LoadAssetBitmaps(pRT);

    // Fill background
    D2D1_RECT_F rcFull = D2D1::RectF(left, top, left + width, top + height);
    pRT->FillRectangle(rcFull, theme.brushCanvasBg ? theme.brushCanvasBg.Get() : theme.brushDrawerBg.Get());

    const float padX = config.ScaleF(24.0f);
    const float padY = config.ScaleF(18.0f);
    float curY = top + padY;

    // =========================================================================
    // STATE 1: LANGUAGE SELECTION GRID (Shown First As Requested, Image 2)
    // =========================================================================
    if (m_viewState == PkgViewState::LanguageGrid) {
        // 1. Header Banner
        D2D1_RECT_F rcIcon = D2D1::RectF(left + padX, curY, left + padX + config.ScaleF(28.0f), curY + config.ScaleF(28.0f));
        m_icons.DrawPackage(pRT, rcIcon, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.8f);

        D2D1_RECT_F rcTitle = D2D1::RectF(rcIcon.right + config.ScaleF(12.0f), curY - config.ScaleF(2.0f), left + width - padX, curY + config.ScaleF(26.0f));
        pRT->DrawText(L"Package Manager", 15, theme.fontTitle.Get(), rcTitle, theme.brushTextPrimary.Get());

        D2D1_RECT_F rcSub = D2D1::RectF(rcIcon.right + config.ScaleF(12.0f), curY + config.ScaleF(24.0f), left + width - padX, curY + config.ScaleF(44.0f));
        const wchar_t* subtext = L"Select an installed programming language / runtime environment to manage packages:";
        pRT->DrawText(subtext, static_cast<UINT32>(wcslen(subtext)), theme.fontHint.Get(), rcSub, theme.brushTextSecondary.Get());

        curY += config.ScaleF(56.0f);

        // 2. Square Cards arranged in a grid with rounded corners and modern flat design (Matching Image 2)
        const float cardW = config.ScaleF(145.0f);
        const float cardH = config.ScaleF(140.0f);
        const float cardGap = config.ScaleF(18.0f);

        for (size_t i = 0; i < m_languageCards.size(); ++i) {
            auto& card = m_languageCards[i];
            float cx = left + padX + (i % 3) * (cardW + cardGap);
            float cy = curY + (i / 3) * (cardH + cardGap);
            card.bounds = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);

            D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(card.bounds, 10.0f, 10.0f);

            // Card background & subtle border (highlight on hover)
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
            if (card.isHovered) {
                pRT->CreateSolidColorBrush(
                    theme.IsDarkMode() ? D2D1::ColorF(0.16f, 0.17f, 0.20f, 1.0f) : D2D1::ColorF(0.90f, 0.92f, 0.95f, 1.0f),
                    &cardBg
                );
            } else {
                pRT->CreateSolidColorBrush(
                    theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.13f, 0.15f, 1.0f) : D2D1::ColorF(0.95f, 0.95f, 0.97f, 1.0f),
                    &cardBg
                );
            }
            if (cardBg) pRT->FillRoundedRectangle(rrcCard, cardBg.Get());

            if (card.isHovered) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverBorder;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.8f), &hoverBorder);
                if (hoverBorder) pRT->DrawRoundedRectangle(rrcCard, hoverBorder.Get(), 1.4f);
            } else if (theme.brushBorderSubtle) {
                pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 0.9f);
            }

            // Centered Icon Box pedestal (Image 2)
            const float iconBoxSize = config.ScaleF(46.0f);
            const float iconX = cx + (cardW - iconBoxSize) / 2.0f;
            const float iconY = cy + (cardH - iconBoxSize - config.ScaleF(28.0f)) / 2.0f;
            D2D1_RECT_F rcIconBox = D2D1::RectF(iconX, iconY, iconX + iconBoxSize, iconY + iconBoxSize);
            D2D1_ROUNDED_RECT rrcIconBox = D2D1::RoundedRect(rcIconBox, 8.0f, 8.0f);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBoxBg;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.08f, 0.08f, 0.10f, 0.95f) : D2D1::ColorF(0.98f, 0.98f, 1.0f, 0.95f),
                &iconBoxBg
            );
            if (iconBoxBg) pRT->FillRoundedRectangle(rrcIconBox, iconBoxBg.Get());

            // Center branded icon inside icon box
            const float innerIconSize = config.ScaleF(34.0f);
            float innerX = iconX + (iconBoxSize - innerIconSize) / 2.0f;
            float innerY = iconY + (iconBoxSize - innerIconSize) / 2.0f;
            D2D1_RECT_F rcInnerIcon = D2D1::RectF(innerX, innerY, innerX + innerIconSize, innerY + innerIconSize);
            RenderBrandedIcon(pRT, theme, card.lang, rcInnerIcon);

            // Language Name centered below icon (Version number removed per user requirement)
            float nameY = iconY + iconBoxSize + config.ScaleF(10.0f);
            D2D1_RECT_F rcName = D2D1::RectF(cx + 4.0f, nameY, cx + cardW - 4.0f, nameY + config.ScaleF(22.0f));
            pRT->DrawText(card.name.c_str(), static_cast<UINT32>(card.name.length()),
                          theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontTitle.Get(),
                          rcName, theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
        }
        return;
    }

    // =========================================================================
    // STATE 2: PACKAGE SEARCH & OUTPUT LIST WITH REPOSITORY PREVIEW
    // =========================================================================
    // 1. Navigation Header Bar (Back button + active environment pill)
    m_rcBackBtn = D2D1::RectF(left + padX, curY, left + padX + config.ScaleF(130.0f), curY + config.ScaleF(26.0f));
    D2D1_ROUNDED_RECT rrcBack = D2D1::RoundedRect(m_rcBackBtn, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcBack, m_isHoveringBack ? theme.brushItemHover.Get() : theme.brushDrawerBg.Get());
    pRT->DrawRoundedRectangle(rrcBack, theme.brushBorderSubtle.Get(), 1.0f);
    const wchar_t* backText = L"◂ Environments";
    pRT->DrawText(backText, static_cast<UINT32>(wcslen(backText)),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontHint.Get(),
                  m_rcBackBtn, theme.brushTextPrimary.Get());

    // Active Environment Pill
    std::wstring envPillText = (m_activeLanguage == RuntimeLanguage::Php) ? L"PHP (Composer)" :
                               (m_activeLanguage == RuntimeLanguage::Node ? L"Node.js (npm)" : L"Python (pip)");
    D2D1_RECT_F rcEnvPill = D2D1::RectF(m_rcBackBtn.right + config.ScaleF(10.0f), curY, m_rcBackBtn.right + config.ScaleF(145.0f), curY + config.ScaleF(26.0f));
    D2D1_ROUNDED_RECT rrcEnv = D2D1::RoundedRect(rcEnvPill, 4.0f, 4.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushEnvBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 0.18f), &brushEnvBg);
    if (brushEnvBg) pRT->FillRoundedRectangle(rrcEnv, brushEnvBg.Get());
    pRT->DrawText(envPillText.c_str(), static_cast<UINT32>(envPillText.length()),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBadge.Get(),
                  rcEnvPill, theme.brushBrandAccent.Get());

    curY += config.ScaleF(32.0f);

    // USER REQUIREMENT: "reduce the witdht compeltely" -> compact sidebar width for output list
    const float leftColW = config.ScaleF(280.0f);
    const float contentH = top + height - curY - padY;

    // 2. Search Bar
    const float searchH = config.ScaleF(32.0f);
    m_rcSearchBox = D2D1::RectF(left + padX, curY, left + padX + leftColW, curY + searchH);
    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(m_rcSearchBox, 5.0f, 5.0f);

    pRT->FillRoundedRectangle(rrcSearch, theme.brushCanvasBg.Get());
    if (m_isSearchFocused) {
        pRT->DrawRoundedRectangle(rrcSearch, theme.brushBrandAccent.Get(), 1.5f);
    } else {
        pRT->DrawRoundedRectangle(rrcSearch, theme.brushBorderSubtle.Get(), 1.0f);
    }

    D2D1_RECT_F rcSearchIcon = D2D1::RectF(m_rcSearchBox.left + config.ScaleF(8.0f), m_rcSearchBox.top + config.ScaleF(7.0f),
                                          m_rcSearchBox.left + config.ScaleF(24.0f), m_rcSearchBox.bottom - config.ScaleF(7.0f));
    m_icons.DrawSearch(pRT, rcSearchIcon, theme.brushTextSecondary.Get(), 1.4f);

    D2D1_RECT_F rcSearchText = D2D1::RectF(rcSearchIcon.right + config.ScaleF(6.0f), m_rcSearchBox.top + config.ScaleF(6.0f),
                                          m_rcSearchBox.right - config.ScaleF(28.0f), m_rcSearchBox.bottom - config.ScaleF(4.0f));

    if (m_searchQuery.empty()) {
        const wchar_t* placeholder = (m_activeLanguage == RuntimeLanguage::Php)
            ? L"Search PHP packages..."
            : (m_activeLanguage == RuntimeLanguage::Node ? L"Search npm packages..." : L"Search Python packages...");
        pRT->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)), theme.fontBody.Get(), rcSearchText, theme.brushTextSecondary.Get());

        // USER REQUIREMENT: White blinking caret matching mouse click position
        if (m_isSearchFocused) {
            bool blinkOn = ((GetTickCount64() / 500) % 2 == 0);
            if (blinkOn) {
                float caretX = rcSearchText.left;
                ID2D1Brush* caretBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
                pRT->DrawLine(D2D1::Point2F(caretX, m_rcSearchBox.top + config.ScaleF(7.0f)),
                              D2D1::Point2F(caretX, m_rcSearchBox.bottom - config.ScaleF(7.0f)),
                              caretBrush, 1.2f);
            }
        }
    } else {
        pRT->DrawText(m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()), theme.fontBody.Get(), rcSearchText, theme.brushTextPrimary.Get());

        // White blinking caret at m_searchCaretPos
        if (m_isSearchFocused) {
            bool blinkOn = ((GetTickCount64() / 500) % 2 == 0);
            if (blinkOn) {
                float caretOffset = 0.0f;
                if (m_searchCaretPos > 0) {
                    std::wstring sub = m_searchQuery.substr(0, (std::min)(m_searchCaretPos, m_searchQuery.length()));
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pSubLayout;
                    if (theme.dwriteFactory && SUCCEEDED(theme.dwriteFactory->CreateTextLayout(sub.c_str(), static_cast<UINT32>(sub.length()), theme.fontBody.Get(), 1000.0f, searchH, &pSubLayout)) && pSubLayout) {
                        DWRITE_TEXT_METRICS stm{};
                        if (SUCCEEDED(pSubLayout->GetMetrics(&stm))) {
                            caretOffset = stm.widthIncludingTrailingWhitespace;
                        }
                    } else {
                        caretOffset = static_cast<float>(sub.length()) * config.ScaleF(7.5f);
                    }
                }
                float caretX = (std::min)(rcSearchText.left + caretOffset, rcSearchText.right - config.ScaleF(4.0f));
                ID2D1Brush* caretBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
                pRT->DrawLine(D2D1::Point2F(caretX, m_rcSearchBox.top + config.ScaleF(7.0f)),
                              D2D1::Point2F(caretX, m_rcSearchBox.bottom - config.ScaleF(7.0f)),
                              caretBrush, 1.2f);
            }
        }

        m_rcClearSearch = D2D1::RectF(m_rcSearchBox.right - config.ScaleF(24.0f), m_rcSearchBox.top + config.ScaleF(6.0f),
                                     m_rcSearchBox.right - config.ScaleF(6.0f), m_rcSearchBox.bottom - config.ScaleF(6.0f));
        pRT->DrawText(L"×", 1, theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontTitle.Get(), m_rcClearSearch, theme.brushTextSecondary.Get());
    }

    // 3. Package Output List
    float listTopY = curY + searchH + config.ScaleF(10.0f);
    float listH = contentH - searchH - config.ScaleF(10.0f);
    m_rcPackageList = D2D1::RectF(left + padX, listTopY, left + padX + leftColW, listTopY + listH);

    D2D1_ROUNDED_RECT rrcList = D2D1::RoundedRect(m_rcPackageList, 6.0f, 6.0f);
    pRT->FillRoundedRectangle(rrcList, theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
    pRT->DrawRoundedRectangle(rrcList, theme.brushBorderSubtle.Get(), 1.0f);

    auto& activeList = GetActivePackageList();

    // Initial Output is NONE (User requirement)
    if (m_searchQuery.empty()) {
        D2D1_RECT_F rcEmptyText = D2D1::RectF(m_rcPackageList.left + config.ScaleF(16.0f), m_rcPackageList.top + config.ScaleF(60.0f),
                                             m_rcPackageList.right - config.ScaleF(16.0f), m_rcPackageList.top + config.ScaleF(160.0f));
        const wchar_t* emptyHint = L"Type to search over 5,000+ packages from database.";
        pRT->DrawText(emptyHint, static_cast<UINT32>(wcslen(emptyHint)), theme.fontHint.Get(), rcEmptyText, theme.brushTextSecondary.Get());
    } else {
        // Render Output List with virtualized chunk size
        pRT->PushAxisAlignedClip(m_rcPackageList, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // USER REQUIREMENT: "each output should be like a card with borders", "remove the pip install udner the name", "center the isntall word"
        const float itemH = config.ScaleF(42.0f);
        float itemY = listTopY + config.ScaleF(6.0f);

        size_t renderLimit = (std::min)(m_filteredIndices.size(), m_scrollIndex + m_visibleLimit);
        for (size_t i = m_scrollIndex; i < renderLimit; ++i) {
            if (itemY + itemH > m_rcPackageList.bottom + itemH) break;

            size_t pkgIdx = m_filteredIndices[i];
            auto& pkg = activeList[pkgIdx];
            pkg.bounds = D2D1::RectF(m_rcPackageList.left + config.ScaleF(6.0f), itemY,
                                    m_rcPackageList.right - config.ScaleF(6.0f), itemY + itemH);

            bool isSelected = (static_cast<int>(i) == m_selectedIndex);
            bool isHovered = (static_cast<int>(i) == m_hoveredIndex);

            D2D1_ROUNDED_RECT rrcItem = D2D1::RoundedRect(pkg.bounds, 6.0f, 6.0f);

            // Card background & distinct borders (User requirement)
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
            if (isSelected) {
                pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.0f, 0.471f, 0.831f, 0.22f) : D2D1::ColorF(0.85f, 0.92f, 1.0f, 0.9f), &cardBg);
            } else if (isHovered) {
                pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.19f, 0.23f, 1.0f) : D2D1::ColorF(0.92f, 0.93f, 0.96f, 1.0f), &cardBg);
            } else {
                pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.13f, 0.14f, 0.17f, 1.0f) : D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f), &cardBg);
            }
            if (cardBg) pRT->FillRoundedRectangle(rrcItem, cardBg.Get());

            // Distinct card border
            if (isSelected) {
                pRT->DrawRoundedRectangle(rrcItem, theme.brushBrandAccent.Get(), 1.5f);
            } else if (isHovered) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverBorder;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.6f), &hoverBorder);
                if (hoverBorder) pRT->DrawRoundedRectangle(rrcItem, hoverBorder.Get(), 1.2f);
            } else {
                pRT->DrawRoundedRectangle(rrcItem, theme.brushBorderSubtle.Get(), 1.0f);
            }

            // Install Button bounds on right of card
            const float btnW = config.ScaleF(88.0f);
            const float btnH = config.ScaleF(24.0f);
            float btnX = pkg.bounds.right - btnW - config.ScaleF(6.0f);
            float btnY = pkg.bounds.top + (itemH - btnH) / 2.0f;

            // Package Name vertically centered on left, NO pip install subtitle (User requirement)
            D2D1_RECT_F rcItemName = D2D1::RectF(pkg.bounds.left + config.ScaleF(10.0f), pkg.bounds.top + config.ScaleF(10.0f),
                                                btnX - config.ScaleF(6.0f), pkg.bounds.bottom - config.ScaleF(10.0f));
            pRT->DrawText(pkg.name.c_str(), static_cast<UINT32>(pkg.name.length()), theme.fontTitle.Get(), rcItemName, theme.brushTextPrimary.Get());

            // Split Button: [ Install | ▾ ]
            pkg.rcInstallBtn = D2D1::RectF(btnX, btnY, btnX + btnW - config.ScaleF(22.0f), btnY + btnH);
            D2D1_ROUNDED_RECT rrcInstallBtn = D2D1::RoundedRect(pkg.rcInstallBtn, 3.5f, 3.5f);

            bool isInstalledForScope = (pkg.scope == PkgInstallScope::Project) ? pkg.isInstalledProject : pkg.isInstalledGlobal;
            pkg.isInstalled = isInstalledForScope;
            std::wstring installLabel = (pkg.scope == PkgInstallScope::Global) ? L"Install (G)" : L"Install";
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnBrush;
            if (pkg.isInstalled) {
                // User requirement: "if its installed the button here would change to uninstall and read [red] color"
                installLabel = pkg.isInstalling ? L"Removing..." : L"Uninstall";
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.20f, 0.20f, 1.0f), &btnBrush);
            } else if (pkg.isInstalling) {
                installLabel = L"Installing...";
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.38f, 0.72f, 0.85f), &btnBrush);
            } else {
                btnBrush = theme.brushBrandAccent;
            }
            pRT->FillRoundedRectangle(rrcInstallBtn, btnBrush ? btnBrush.Get() : theme.brushBrandAccent.Get());

            // Centered Install text (User requirement)
            pRT->DrawText(installLabel.c_str(), static_cast<UINT32>(installLabel.length()),
                          theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontHint.Get(),
                          pkg.rcInstallBtn, theme.brushTextWhite.Get());

            // Split Dropdown Arrow portion ("▾")
            pkg.rcSplitArrowBtn = D2D1::RectF(btnX + btnW - config.ScaleF(22.0f), btnY, btnX + btnW, btnY + btnH);
            D2D1_ROUNDED_RECT rrcArrowBtn = D2D1::RoundedRect(pkg.rcSplitArrowBtn, 3.5f, 3.5f);
            pRT->FillRoundedRectangle(rrcArrowBtn, btnBrush ? btnBrush.Get() : theme.brushBrandAccent.Get());
            // Divider line between Install and ▾
            pRT->DrawLine(D2D1::Point2F(pkg.rcSplitArrowBtn.left, btnY + 3.0f),
                          D2D1::Point2F(pkg.rcSplitArrowBtn.left, btnY + btnH - 3.0f),
                          theme.brushBorderSubtle.Get(), 1.0f);
            pRT->DrawText(L"▾", 1, theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBadge.Get(),
                          pkg.rcSplitArrowBtn, theme.brushTextWhite.Get());

            itemY += itemH + config.ScaleF(5.0f);
        }
        pRT->PopAxisAlignedClip();

        // Render Active Dropdown Popup if open (PyCharm / IDE style)
        if (m_activeDropdownPkgIdx >= 0) {
            auto& activePkg = activeList[m_activeDropdownPkgIdx];
            const float dropW = config.ScaleF(185.0f);
            const float rowH = config.ScaleF(32.0f);
            const float dropH = rowH * 2.0f + config.ScaleF(6.0f);
            float dropX = activePkg.rcSplitArrowBtn.right - dropW;
            if (dropX < m_rcPackageList.left + config.ScaleF(4.0f)) {
                dropX = m_rcPackageList.left + config.ScaleF(4.0f);
            }
            float dropY = activePkg.rcSplitArrowBtn.bottom + config.ScaleF(4.0f);

            D2D1_RECT_F rcDropBox = D2D1::RectF(dropX, dropY, dropX + dropW, dropY + dropH);
            D2D1_ROUNDED_RECT rrcDrop = D2D1::RoundedRect(rcDropBox, 6.0f, 6.0f);

            // Subtle drop shadow
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bShadow;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &bShadow);
            if (bShadow) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(dropX + 2.0f, dropY + 2.0f, dropX + dropW + 2.0f, dropY + dropH + 2.0f), 6.0f, 6.0f), bShadow.Get());
            }

            // Dark card background & border
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bMenuBg, bMenuBorder;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0x181a20) : D2D1::ColorF(0xf5f6f8),
                &bMenuBg
            );
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0x3a3f4d) : D2D1::ColorF(0xc4c7d0),
                &bMenuBorder
            );

            if (bMenuBg) pRT->FillRoundedRectangle(rrcDrop, bMenuBg.Get());
            if (bMenuBorder) pRT->DrawRoundedRectangle(rrcDrop, bMenuBorder.Get(), 1.0f);

            m_rcDropdownProject = D2D1::RectF(dropX + config.ScaleF(4.0f), dropY + config.ScaleF(3.0f),
                                             dropX + dropW - config.ScaleF(4.0f), dropY + config.ScaleF(3.0f) + rowH);
            m_rcDropdownGlobal  = D2D1::RectF(dropX + config.ScaleF(4.0f), dropY + config.ScaleF(3.0f) + rowH,
                                             dropX + dropW - config.ScaleF(4.0f), dropY + dropH - config.ScaleF(3.0f));

            if (m_isHoveringDropdownProject) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcDropdownProject, 4.0f, 4.0f), theme.brushItemHover.Get());
            }
            if (m_isHoveringDropdownGlobal) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcDropdownGlobal, 4.0f, 4.0f), theme.brushItemHover.Get());
            }

            // Divider between Project and Global
            float divY = dropY + config.ScaleF(3.0f) + rowH;
            if (bMenuBorder) {
                pRT->DrawLine(D2D1::Point2F(dropX + config.ScaleF(8.0f), divY),
                              D2D1::Point2F(dropX + dropW - config.ScaleF(8.0f), divY),
                              bMenuBorder.Get(), 0.8f);
            }

            // Left gutter checkmarks
            const float gutterW = config.ScaleF(24.0f);
            if (activePkg.scope == PkgInstallScope::Project) {
                D2D1_RECT_F rcCheck = D2D1::RectF(m_rcDropdownProject.left, m_rcDropdownProject.top,
                                                 m_rcDropdownProject.left + gutterW, m_rcDropdownProject.bottom);
                pRT->DrawText(L"✓", 1, theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(), rcCheck, theme.brushBrandAccent.Get());
            }
            if (activePkg.scope == PkgInstallScope::Global) {
                D2D1_RECT_F rcCheck = D2D1::RectF(m_rcDropdownGlobal.left, m_rcDropdownGlobal.top,
                                                 m_rcDropdownGlobal.left + gutterW, m_rcDropdownGlobal.bottom);
                pRT->DrawText(L"✓", 1, theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(), rcCheck, theme.brushBrandAccent.Get());
            }

            // Left-aligned text in rows
            IDWriteTextFormat* pMenuFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();

            D2D1_RECT_F rcProjText = D2D1::RectF(m_rcDropdownProject.left + gutterW, m_rcDropdownProject.top,
                                                m_rcDropdownProject.right - config.ScaleF(6.0f), m_rcDropdownProject.bottom);
            D2D1_RECT_F rcGlobText = D2D1::RectF(m_rcDropdownGlobal.left + gutterW, m_rcDropdownGlobal.top,
                                                m_rcDropdownGlobal.right - config.ScaleF(6.0f), m_rcDropdownGlobal.bottom);

            const wchar_t* pText = L"Project (local)";
            const wchar_t* gText = (m_activeLanguage == RuntimeLanguage::Php) ? L"Global (composer global)" :
                                   (m_activeLanguage == RuntimeLanguage::Node ? L"Global (npm -g)" : L"Global (--user)");

            pRT->DrawText(pText, static_cast<UINT32>(wcslen(pText)), pMenuFmt, rcProjText, theme.brushTextPrimary.Get());
            pRT->DrawText(gText, static_cast<UINT32>(wcslen(gText)), pMenuFmt, rcGlobText, theme.brushTextPrimary.Get());
        }
    }

    // =========================================================================
    // 4. RIGHT COLUMN: Complete Authentic GitHub Web Render (User requirement)
    // =========================================================================
    float rightLeft = left + padX + leftColW + config.ScaleF(16.0f);
    float rightW = width - padX * 2.0f - leftColW - config.ScaleF(16.0f);
    m_rcRightPreview = D2D1::RectF(rightLeft, curY, rightLeft + rightW, curY + contentH);

    D2D1_ROUNDED_RECT rrcPreview = D2D1::RoundedRect(m_rcRightPreview, 8.0f, 8.0f);
    pRT->FillRoundedRectangle(rrcPreview, theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
    pRT->DrawRoundedRectangle(rrcPreview, theme.brushBorderSubtle.Get(), 1.0f);

    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredIndices.size())) {
        size_t selIdx = m_filteredIndices[m_selectedIndex];
        const auto& curPkg = activeList[selIdx];
        RenderGitHubWebPage(pRT, theme, config, curPkg, m_rcRightPreview);
    } else {
        HideWebView();
        D2D1_RECT_F rcNoPkg = D2D1::RectF(m_rcRightPreview.left + config.ScaleF(30.0f), m_rcRightPreview.top + config.ScaleF(100.0f),
                                         m_rcRightPreview.right - config.ScaleF(30.0f), m_rcRightPreview.top + config.ScaleF(180.0f));
        const wchar_t* noPkgHint = L"Select or search a package to view its authentic GitHub web repository.";
        pRT->DrawText(noPkgHint, static_cast<UINT32>(wcslen(noPkgHint)), theme.fontHint.Get(), rcNoPkg, theme.brushTextSecondary.Get());
    }

    UpdateWebViewBounds();
}

void PackageManagerView::RenderGitHubWebPage(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                            const PackageEntry& pkg, const D2D1_RECT_F& bounds) {
    pRT->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // 1. GitHub Dark Theme Palette (#0d1117 authentic web styling)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bGhBg, bGhHeader, bGhCard, bGhBorder, bGhBorderSubtle;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bGhText, bGhMuted, bGhLink, bGhGreen, bGhCoral, bGhCodeBg;

    pRT->CreateSolidColorBrush(D2D1::ColorF(0x0d1117), &bGhBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x010409), &bGhHeader);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x161b22), &bGhCard);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x30363d), &bGhBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x21262d), &bGhBorderSubtle);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0xe6edf3), &bGhText);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x848d97), &bGhMuted);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x4493f8), &bGhLink);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x238636), &bGhGreen);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0xf78166), &bGhCoral);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x161b22), &bGhCodeBg);

    // Text formats with explicit LEFT (LEADING) alignment to avoid centering overlap
    Microsoft::WRL::ComPtr<IDWriteTextFormat> ghTitleFmt, ghBodyFmt, ghAboutFmt;
    if (theme.dwriteFactory) {
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-US", &ghTitleFmt);
        if (ghTitleFmt) {
            ghTitleFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            ghTitleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ghTitleFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &ghBodyFmt);
        if (ghBodyFmt) {
            ghBodyFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            ghBodyFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ghBodyFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &ghAboutFmt);
        if (ghAboutFmt) {
            ghAboutFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            ghAboutFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            ghAboutFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
    }

    auto MeasureTextWidth = [&](const std::wstring& str, IDWriteTextFormat* pFmt) -> float {
        if (!theme.dwriteFactory || !pFmt || str.empty()) return static_cast<float>(str.length()) * config.ScaleF(8.0f);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(str.c_str(), static_cast<UINT32>(str.length()), pFmt, 2000.0f, 100.0f, &pLayout)) && pLayout) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                return tm.widthIncludingTrailingWhitespace;
            }
        }
        return static_cast<float>(str.length()) * config.ScaleF(8.0f);
    };

    // Canvas background
    pRT->FillRectangle(bounds, bGhBg.Get());

    float startX = bounds.left;
    float startY = bounds.top - m_previewScrollY;
    float totalW = bounds.right - bounds.left;

    // A. GitHub Top Header Bar
    D2D1_RECT_F rcGhTop = D2D1::RectF(startX, startY, bounds.right, startY + config.ScaleF(48.0f));
    pRT->FillRectangle(rcGhTop, bGhHeader.Get());
    pRT->DrawLine(D2D1::Point2F(startX, rcGhTop.bottom), D2D1::Point2F(bounds.right, rcGhTop.bottom), bGhBorderSubtle.Get(), 1.0f);

    // Octocat icon
    D2D1_RECT_F rcOcto = D2D1::RectF(startX + config.ScaleF(16.0f), startY + config.ScaleF(12.0f),
                                     startX + config.ScaleF(38.0f), startY + config.ScaleF(34.0f));
    m_icons.DrawPackage(pRT, rcOcto, bGhText.Get(), 1.6f);

    // Repo breadcrumb: owner / repo Public
    bool isPhp = (m_activeLanguage == RuntimeLanguage::Php);
    bool isNode = (m_activeLanguage == RuntimeLanguage::Node);
    std::wstring owner = isPhp ? L"packagist" : (isNode ? L"npm" : L"pypa");
    std::wstring repo = pkg.name;
    auto slashPos = pkg.github.find(L"github.com/");
    if (slashPos != std::wstring::npos) {
        std::wstring sub = pkg.github.substr(slashPos + 11);
        auto s2 = sub.find(L'/');
        if (s2 != std::wstring::npos) {
            owner = sub.substr(0, s2);
            repo = sub.substr(s2 + 1);
        }
    }

    float textX = rcOcto.right + config.ScaleF(10.0f);
    IDWriteTextFormat* pFmtTitle = ghTitleFmt ? ghTitleFmt.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pFmtBody = ghBodyFmt ? ghBodyFmt.Get() : theme.fontBody.Get();

    float ownerW = MeasureTextWidth(owner, pFmtTitle);
    float slashW = MeasureTextWidth(L" / ", pFmtTitle);
    float repoW = MeasureTextWidth(repo, pFmtTitle);

    D2D1_RECT_F rcOwner = D2D1::RectF(textX, startY + config.ScaleF(12.0f), textX + ownerW, startY + config.ScaleF(36.0f));
    pRT->DrawText(owner.c_str(), static_cast<UINT32>(owner.length()), pFmtTitle, rcOwner, bGhLink.Get());
    textX += ownerW;

    D2D1_RECT_F rcSlash = D2D1::RectF(textX, startY + config.ScaleF(12.0f), textX + slashW, startY + config.ScaleF(36.0f));
    pRT->DrawText(L" / ", 3, pFmtTitle, rcSlash, bGhMuted.Get());
    textX += slashW;

    D2D1_RECT_F rcRepo = D2D1::RectF(textX, startY + config.ScaleF(12.0f), textX + repoW, startY + config.ScaleF(36.0f));
    pRT->DrawText(repo.c_str(), static_cast<UINT32>(repo.length()), pFmtTitle, rcRepo, bGhText.Get());
    textX += repoW + config.ScaleF(10.0f);

    // Public badge
    D2D1_RECT_F rcPub = D2D1::RectF(textX, startY + config.ScaleF(15.0f), textX + config.ScaleF(46.0f), startY + config.ScaleF(31.0f));
    D2D1_ROUNDED_RECT rrcPub = D2D1::RoundedRect(rcPub, 8.0f, 8.0f);
    pRT->FillRoundedRectangle(rrcPub, bGhBorderSubtle.Get());
    pRT->DrawRoundedRectangle(rrcPub, bGhBorder.Get(), 1.0f);
    const wchar_t* pubText = L"Public";
    pRT->DrawText(pubText, 6, theme.fontBadge.Get(), rcPub, bGhMuted.Get());

    // Top controls: Address / URL Pill, Star, Fork (No external open button)
    float btnRightX = bounds.right - config.ScaleF(14.0f);

    // Browser Address Pill (🔒 https://github.com/...)
    float addrW = config.ScaleF(210.0f);
    D2D1_RECT_F rcAddrPill = D2D1::RectF(btnRightX - addrW, startY + config.ScaleF(11.0f), btnRightX, startY + config.ScaleF(35.0f));
    D2D1_ROUNDED_RECT rrcAddr = D2D1::RoundedRect(rcAddrPill, 12.0f, 12.0f);
    pRT->FillRoundedRectangle(rrcAddr, bGhBorderSubtle.Get());
    pRT->DrawRoundedRectangle(rrcAddr, bGhBorder.Get(), 1.0f);

    std::wstring addrText = L"🔒 " + pkg.github;
    D2D1_RECT_F rcAddrText = D2D1::RectF(rcAddrPill.left + config.ScaleF(10.0f), rcAddrPill.top,
                                         rcAddrPill.right - config.ScaleF(10.0f), rcAddrPill.bottom);
    pRT->PushAxisAlignedClip(rcAddrText, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    pRT->DrawText(addrText.c_str(), static_cast<UINT32>(addrText.length()), pFmtBody, rcAddrText, bGhMuted.Get());
    pRT->PopAxisAlignedClip();
    btnRightX -= (addrW + config.ScaleF(10.0f));

    // Star button
    float starW = config.ScaleF(105.0f);
    D2D1_RECT_F rcStarBtn = D2D1::RectF(btnRightX - starW, startY + config.ScaleF(11.0f), btnRightX, startY + config.ScaleF(35.0f));
    D2D1_ROUNDED_RECT rrcStar = D2D1::RoundedRect(rcStarBtn, 5.0f, 5.0f);
    pRT->FillRoundedRectangle(rrcStar, bGhCard.Get());
    pRT->DrawRoundedRectangle(rrcStar, bGhBorder.Get(), 1.0f);
    std::wstring starLabel = L"★ Star " + pkg.stars + L" ▾";
    pRT->DrawText(starLabel.c_str(), static_cast<UINT32>(starLabel.length()),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontHint.Get(),
                  rcStarBtn, bGhText.Get());
    btnRightX -= (starW + config.ScaleF(8.0f));

    // Fork button
    float forkW = config.ScaleF(78.0f);
    D2D1_RECT_F rcForkBtn = D2D1::RectF(btnRightX - forkW, startY + config.ScaleF(11.0f), btnRightX, startY + config.ScaleF(35.0f));
    D2D1_ROUNDED_RECT rrcFork = D2D1::RoundedRect(rcForkBtn, 5.0f, 5.0f);
    pRT->FillRoundedRectangle(rrcFork, bGhCard.Get());
    pRT->DrawRoundedRectangle(rrcFork, bGhBorder.Get(), 1.0f);
    const wchar_t* forkLabel = L"⑂ Fork 7.8k";
    pRT->DrawText(forkLabel, static_cast<UINT32>(wcslen(forkLabel)),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontHint.Get(),
                  rcForkBtn, bGhText.Get());

    // B. Subnav Tabs Bar
    float tabY = startY + config.ScaleF(48.0f);
    D2D1_RECT_F rcTabsBar = D2D1::RectF(startX, tabY, bounds.right, tabY + config.ScaleF(36.0f));
    pRT->FillRectangle(rcTabsBar, bGhHeader.Get());
    pRT->DrawLine(D2D1::Point2F(startX, rcTabsBar.bottom), D2D1::Point2F(bounds.right, rcTabsBar.bottom), bGhBorderSubtle.Get(), 1.0f);

    struct TabItem {
        const wchar_t* label;
        bool active;
    };
    static const TabItem ghTabs[] = {
        { L"<> Code", true },
        { L"Issues  842", false },
        { L"Pull requests  47", false },
        { L"Actions", false },
        { L"Projects", false },
        { L"Security", false },
        { L"Insights", false }
    };

    float tabX = startX + config.ScaleF(16.0f);
    for (const auto& tb : ghTabs) {
        float tw = config.ScaleF(static_cast<float>(wcslen(tb.label) * 7.5f + 16.0f));
        D2D1_RECT_F rcTab = D2D1::RectF(tabX, tabY, tabX + tw, rcTabsBar.bottom);
        pRT->DrawText(tb.label, static_cast<UINT32>(wcslen(tb.label)),
                      theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontHint.Get(),
                      rcTab, tb.active ? bGhText.Get() : bGhMuted.Get());

        if (tb.active) {
            pRT->FillRectangle(D2D1::RectF(tabX, rcTabsBar.bottom - 2.0f, tabX + tw, rcTabsBar.bottom), bGhCoral.Get());
        }
        tabX += tw + config.ScaleF(6.0f);
    }

    // C. Branch Bar & Action Buttons
    float barY = tabY + config.ScaleF(44.0f);
    const float padX = config.ScaleF(18.0f);

    // ⑂ main ▾
    D2D1_RECT_F rcBranch = D2D1::RectF(startX + padX, barY, startX + padX + config.ScaleF(85.0f), barY + config.ScaleF(26.0f));
    D2D1_ROUNDED_RECT rrcBranch = D2D1::RoundedRect(rcBranch, 5.0f, 5.0f);
    pRT->FillRoundedRectangle(rrcBranch, bGhCard.Get());
    pRT->DrawRoundedRectangle(rrcBranch, bGhBorder.Get(), 1.0f);
    const wchar_t* branchLabel = L"⑂ main ▾";
    pRT->DrawText(branchLabel, static_cast<UINT32>(wcslen(branchLabel)),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontHint.Get(),
                  rcBranch, bGhText.Get());

    // 18 branches · 94 tags
    D2D1_RECT_F rcBranchCounts = D2D1::RectF(rcBranch.right + config.ScaleF(10.0f), barY + config.ScaleF(3.0f),
                                            rcBranch.right + config.ScaleF(200.0f), barY + config.ScaleF(25.0f));
    const wchar_t* bCounts = L"18 branches   •   94 tags";
    pRT->DrawText(bCounts, static_cast<UINT32>(wcslen(bCounts)), pFmtBody, rcBranchCounts, bGhMuted.Get());

    // Right Action: <> Code ▾
    float codeBtnW = config.ScaleF(88.0f);
    D2D1_RECT_F rcCodeBtn = D2D1::RectF(bounds.right - padX - codeBtnW, barY, bounds.right - padX, barY + config.ScaleF(26.0f));
    D2D1_ROUNDED_RECT rrcCode = D2D1::RoundedRect(rcCodeBtn, 5.0f, 5.0f);
    pRT->FillRoundedRectangle(rrcCode, bGhGreen.Get());
    const wchar_t* codeLabel = L"<> Code ▾";
    pRT->DrawText(codeLabel, static_cast<UINT32>(wcslen(codeLabel)),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontHint.Get(),
                  rcCodeBtn, theme.brushTextWhite.Get());

    // D. Latest Commit Bar & Layout Setup
    float commitY = barY + config.ScaleF(34.0f);
    float sidebarW = config.ScaleF(230.0f);
    float mainW = totalW - padX * 2.0f - sidebarW - config.ScaleF(20.0f);

    D2D1_RECT_F rcCommit = D2D1::RectF(startX + padX, commitY, startX + padX + mainW, commitY + config.ScaleF(32.0f));
    D2D1_ROUNDED_RECT rrcCommit = D2D1::RoundedRect(rcCommit, 6.0f, 6.0f);
    pRT->FillRoundedRectangle(rrcCommit, bGhCard.Get());
    pRT->DrawRoundedRectangle(rrcCommit, bGhBorder.Get(), 1.0f);

    // Committer avatar circle
    D2D1_ELLIPSE elAvatar = D2D1::Ellipse(D2D1::Point2F(rcCommit.left + config.ScaleF(16.0f), rcCommit.top + config.ScaleF(16.0f)),
                                         config.ScaleF(9.0f), config.ScaleF(9.0f));
    pRT->FillEllipse(elAvatar, bGhBorderSubtle.Get());
    pRT->DrawEllipse(elAvatar, bGhBorder.Get(), 1.0f);

    // Commit author + message
    std::wstring commitMsg = L"maintainer   Merge pull request #2841 - Release v" + pkg.version;
    D2D1_RECT_F rcCommitText = D2D1::RectF(rcCommit.left + config.ScaleF(32.0f), rcCommit.top + config.ScaleF(4.0f),
                                          rcCommit.right - config.ScaleF(140.0f), rcCommit.bottom - config.ScaleF(4.0f));
    pRT->PushAxisAlignedClip(rcCommitText, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    pRT->DrawText(commitMsg.c_str(), static_cast<UINT32>(commitMsg.length()), pFmtBody, rcCommitText, bGhText.Get());
    pRT->PopAxisAlignedClip();

    const wchar_t* commitMeta = L"3a8f19c  yesterday";
    D2D1_RECT_F rcCommitMeta = D2D1::RectF(rcCommit.right - config.ScaleF(135.0f), rcCommit.top + config.ScaleF(4.0f),
                                          rcCommit.right - config.ScaleF(10.0f), rcCommit.bottom - config.ScaleF(4.0f));
    pRT->DrawText(commitMeta, static_cast<UINT32>(wcslen(commitMeta)), pFmtBody, rcCommitMeta, bGhMuted.Get());

    // E. Files Table Container
    float tableY = commitY + config.ScaleF(40.0f);
    D2D1_RECT_F rcTable = D2D1::RectF(startX + padX, tableY, startX + padX + mainW, tableY + config.ScaleF(150.0f));
    D2D1_ROUNDED_RECT rrcTable = D2D1::RoundedRect(rcTable, 6.0f, 6.0f);
    pRT->FillRoundedRectangle(rrcTable, bGhBg.Get());
    pRT->DrawRoundedRectangle(rrcTable, bGhBorder.Get(), 1.0f);

    struct FileRow {
        const wchar_t* name;
        const wchar_t* msg;
        const wchar_t* time;
        bool isDir;
    };
    static const FileRow ghFiles[] = {
        { L".github", L"Update CI workflows and test matrix", L"3 days ago", true },
        { L"lib / core", L"Refactor graphics rasterization backend", L"yesterday", true },
        { L"tests", L"Add regression tests for DPI scaling", L"5 days ago", true },
        { L".gitignore", L"Ignore local build cache directories", L"3 months ago", false },
        { L"LICENSE", L"PSF / BSD open-source license declaration", L"1 year ago", false },
        { L"pyproject.toml", L"Update build dependencies and tool settings", L"2 weeks ago", false },
        { L"README.md", L"Update installation badges and quickstart guide", L"yesterday", false }
    };

    float rowY = tableY;
    float rowH = config.ScaleF(21.4f);
    for (size_t ri = 0; ri < 7; ++ri) {
        const auto& f = ghFiles[ri];
        D2D1_RECT_F rcRow = D2D1::RectF(rcTable.left, rowY, rcTable.right, rowY + rowH);
        if (ri > 0) {
            pRT->DrawLine(D2D1::Point2F(rcTable.left, rowY), D2D1::Point2F(rcTable.right, rowY), bGhBorderSubtle.Get(), 1.0f);
        }
        pRT->PushAxisAlignedClip(rcRow, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        std::wstring fIcon = f.isDir ? L"📁  " : L"📄  ";
        std::wstring fName = fIcon + f.name;
        D2D1_RECT_F rcFName = D2D1::RectF(rcTable.left + config.ScaleF(12.0f), rowY + config.ScaleF(2.0f),
                                         rcTable.left + config.ScaleF(140.0f), rowY + rowH - config.ScaleF(2.0f));
        pRT->DrawText(fName.c_str(), static_cast<UINT32>(fName.length()), pFmtBody, rcFName, bGhLink.Get());

        D2D1_RECT_F rcFMsg = D2D1::RectF(rcTable.left + config.ScaleF(150.0f), rowY + config.ScaleF(2.0f),
                                        rcTable.right - config.ScaleF(100.0f), rowY + rowH - config.ScaleF(2.0f));
        pRT->DrawText(f.msg, static_cast<UINT32>(wcslen(f.msg)), pFmtBody, rcFMsg, bGhMuted.Get());

        D2D1_RECT_F rcFTime = D2D1::RectF(rcTable.right - config.ScaleF(95.0f), rowY + config.ScaleF(2.0f),
                                         rcTable.right - config.ScaleF(8.0f), rowY + rowH - config.ScaleF(2.0f));
        pRT->DrawText(f.time, static_cast<UINT32>(wcslen(f.time)), pFmtBody, rcFTime, bGhMuted.Get());

        pRT->PopAxisAlignedClip();
        rowY += rowH;
    }

    // F. Authentic README.md Web Render Container
    float readmeY = tableY + config.ScaleF(160.0f);
    D2D1_RECT_F rcReadmeCard = D2D1::RectF(startX + padX, readmeY, startX + padX + mainW, readmeY + config.ScaleF(270.0f));
    D2D1_ROUNDED_RECT rrcReadme = D2D1::RoundedRect(rcReadmeCard, 6.0f, 6.0f);
    pRT->FillRoundedRectangle(rrcReadme, bGhBg.Get());
    pRT->DrawRoundedRectangle(rrcReadme, bGhBorder.Get(), 1.0f);

    // README Header Bar
    D2D1_RECT_F rcRmHead = D2D1::RectF(rcReadmeCard.left, readmeY, rcReadmeCard.right, readmeY + config.ScaleF(32.0f));
    pRT->FillRectangle(rcRmHead, bGhCard.Get());
    pRT->DrawLine(D2D1::Point2F(rcReadmeCard.left, rcRmHead.bottom), D2D1::Point2F(rcReadmeCard.right, rcRmHead.bottom), bGhBorder.Get(), 1.0f);

    const wchar_t* rmTitle = L"📖  README.md   •   482 lines (364 sloc) · 16.8 KB";
    D2D1_RECT_F rcRmTitle = D2D1::RectF(rcRmHead.left + config.ScaleF(12.0f), rcRmHead.top + config.ScaleF(5.0f),
                                       rcRmHead.right - config.ScaleF(16.0f), rcRmHead.bottom - config.ScaleF(4.0f));
    pRT->DrawText(rmTitle, static_cast<UINT32>(wcslen(rmTitle)), pFmtBody, rcRmTitle, bGhText.Get());

    // Inside README: H1 Title
    float docY = rcRmHead.bottom + config.ScaleF(12.0f);
    std::wstring h1Title = L"# " + pkg.name;
    D2D1_RECT_F rcH1 = D2D1::RectF(rcReadmeCard.left + config.ScaleF(20.0f), docY,
                                  rcReadmeCard.right - config.ScaleF(20.0f), docY + config.ScaleF(26.0f));
    pRT->DrawText(h1Title.c_str(), static_cast<UINT32>(h1Title.length()), pFmtTitle, rcH1, bGhText.Get());
    docY += config.ScaleF(26.0f);
    pRT->DrawLine(D2D1::Point2F(rcReadmeCard.left + config.ScaleF(20.0f), docY),
                  D2D1::Point2F(rcReadmeCard.right - config.ScaleF(20.0f), docY), bGhBorderSubtle.Get(), 1.0f);
    docY += config.ScaleF(8.0f);

    // Badges Row
    struct Badge {
        const wchar_t* l;
        std::wstring r;
    };
    Badge badges[] = {
        { isNode ? L"npm" : (isPhp ? L"packagist" : L"pypi"), L"v" + pkg.version },
        { L"build", L"passing" },
        { L"downloads", isNode ? L"8.5M / month" : (isPhp ? L"1.2M / month" : L"15M / month") },
        { L"license", pkg.license },
        { isNode ? L"node" : (isPhp ? L"php" : L"python"), isNode ? L">=18.0.0" : (isPhp ? L"^8.0" : L"3.9+") }
    };
    float badgeX = rcReadmeCard.left + config.ScaleF(20.0f);
    for (const auto& bg : badges) {
        float bw = config.ScaleF(static_cast<float>((wcslen(bg.l) + bg.r.length()) * 6.5f + 16.0f));
        D2D1_RECT_F rcBgBox = D2D1::RectF(badgeX, docY, badgeX + bw, docY + config.ScaleF(18.0f));
        D2D1_ROUNDED_RECT rrcBgBox = D2D1::RoundedRect(rcBgBox, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrcBgBox, bGhCard.Get());
        pRT->DrawRoundedRectangle(rrcBgBox, bGhBorder.Get(), 0.8f);

        std::wstring bText = std::wstring(bg.l) + L" | " + bg.r;
        pRT->DrawText(bText.c_str(), static_cast<UINT32>(bText.length()), theme.fontBadge.Get(), rcBgBox, bGhText.Get());
        badgeX += bw + config.ScaleF(6.0f);
    }
    docY += config.ScaleF(24.0f);

    // Summary description
    D2D1_RECT_F rcSummary = D2D1::RectF(rcReadmeCard.left + config.ScaleF(20.0f), docY,
                                       rcReadmeCard.right - config.ScaleF(20.0f), docY + config.ScaleF(32.0f));
    pRT->DrawText(pkg.summary.c_str(), static_cast<UINT32>(pkg.summary.length()), pFmtBody, rcSummary, bGhText.Get());
    docY += config.ScaleF(36.0f);

    // Section ## Installation
    D2D1_RECT_F rcInstHead = D2D1::RectF(rcReadmeCard.left + config.ScaleF(20.0f), docY,
                                        rcReadmeCard.right - config.ScaleF(20.0f), docY + config.ScaleF(20.0f));
    const wchar_t* instHead = L"## Installation";
    pRT->DrawText(instHead, static_cast<UINT32>(wcslen(instHead)), pFmtTitle, rcInstHead, bGhText.Get());
    docY += config.ScaleF(22.0f);

    // Code block: $ pip install ... / $ composer require ...
    D2D1_RECT_F rcCodeBlock = D2D1::RectF(rcReadmeCard.left + config.ScaleF(20.0f), docY,
                                         rcReadmeCard.right - config.ScaleF(20.0f), docY + config.ScaleF(28.0f));
    D2D1_ROUNDED_RECT rrcCodeBlock = D2D1::RoundedRect(rcCodeBlock, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcCodeBlock, bGhCodeBg.Get());
    pRT->DrawRoundedRectangle(rrcCodeBlock, bGhBorder.Get(), 1.0f);

    std::wstring codeLine = L"$ " + pkg.download;
    D2D1_RECT_F rcCodeText = D2D1::RectF(rcCodeBlock.left + config.ScaleF(12.0f), rcCodeBlock.top + config.ScaleF(5.0f),
                                        rcCodeBlock.right - config.ScaleF(12.0f), rcCodeBlock.bottom - config.ScaleF(4.0f));
    pRT->DrawText(codeLine.c_str(), static_cast<UINT32>(codeLine.length()), theme.fontCode.Get(), rcCodeText, bGhGreen.Get());

    // G. Right Column: Authentic GitHub "About" Sidebar
    float sideX = rcCommit.right + config.ScaleF(20.0f);
    float sideY = commitY;

    // About Header
    D2D1_RECT_F rcAboutHead = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(20.0f));
    pRT->DrawText(L"About", 5, pFmtTitle, rcAboutHead, bGhText.Get());
    sideY += config.ScaleF(24.0f);

    // About text with word wrapping (clean, left-aligned)
    D2D1_RECT_F rcAboutDesc = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(52.0f));
    pRT->PushAxisAlignedClip(rcAboutDesc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    pRT->DrawText(pkg.summary.c_str(), static_cast<UINT32>(pkg.summary.length()),
                  ghAboutFmt ? ghAboutFmt.Get() : pFmtBody, rcAboutDesc, bGhMuted.Get());
    pRT->PopAxisAlignedClip();
    sideY += config.ScaleF(56.0f);

    // Website Link
    std::wstring siteUrl = pkg.github.empty() ? L"🌐  https://github.com" : (L"🌐  " + pkg.github);
    D2D1_RECT_F rcSite = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(siteUrl.c_str(), static_cast<UINT32>(siteUrl.length()), pFmtBody, rcSite, bGhLink.Get());
    sideY += config.ScaleF(22.0f);

    // Topics Pills
    std::wstringstream tss(pkg.topics);
    std::wstring topic;
    float tagX = sideX;
    float tagY = sideY;
    while (tss >> topic) {
        float tw = config.ScaleF(static_cast<float>(topic.length() * 6.8f + 14.0f));
        if (tagX + tw > sideX + sidebarW) {
            tagX = sideX;
            tagY += config.ScaleF(22.0f);
        }
        D2D1_RECT_F rcTag = D2D1::RectF(tagX, tagY, tagX + tw, tagY + config.ScaleF(18.0f));
        D2D1_ROUNDED_RECT rrcTag = D2D1::RoundedRect(rcTag, 9.0f, 9.0f);
        pRT->FillRoundedRectangle(rrcTag, bGhBorderSubtle.Get());
        pRT->DrawRoundedRectangle(rrcTag, bGhBorder.Get(), 0.8f);
        pRT->DrawText(topic.c_str(), static_cast<UINT32>(topic.length()), theme.fontBadge.Get(), rcTag, bGhLink.Get());
        tagX += tw + config.ScaleF(5.0f);
    }
    sideY = tagY + config.ScaleF(26.0f);

    // Readme & License links
    const wchar_t* resReadme = L"📖  Readme";
    D2D1_RECT_F rcResRm = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(resReadme, static_cast<UINT32>(wcslen(resReadme)), pFmtBody, rcResRm, bGhMuted.Get());
    sideY += config.ScaleF(20.0f);

    std::wstring resLic = L"⚖️  " + pkg.license + L" license";
    D2D1_RECT_F rcResLic = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(resLic.c_str(), static_cast<UINT32>(resLic.length()), pFmtBody, rcResLic, bGhMuted.Get());
    sideY += config.ScaleF(26.0f);

    // Releases section
    D2D1_RECT_F rcRelHead = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(L"Releases", 8, pFmtTitle, rcRelHead, bGhText.Get());
    sideY += config.ScaleF(20.0f);

    std::wstring relText = L"🏷️  v" + pkg.version + L" Latest";
    D2D1_RECT_F rcRel = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(relText.c_str(), static_cast<UINT32>(relText.length()), pFmtBody, rcRel, bGhGreen.Get());
    sideY += config.ScaleF(26.0f);

    // Languages section
    D2D1_RECT_F rcLangHead = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(L"Languages", 9, pFmtTitle, rcLangHead, bGhText.Get());
    sideY += config.ScaleF(20.0f);

    // Language progress bar
    D2D1_RECT_F rcLangBar = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(7.0f));
    D2D1_ROUNDED_RECT rrcLangBar = D2D1::RoundedRect(rcLangBar, 3.5f, 3.5f);
    pRT->FillRoundedRectangle(rrcLangBar, bGhBorderSubtle.Get());

    // Segments: Python 74%, C++ 22%, Other 4%
    float wPy = sidebarW * 0.74f;
    float wCpp = sidebarW * 0.22f;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bPyLang, bCppLang;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0x3572A5), &bPyLang);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0xf34b7d), &bCppLang);
    pRT->FillRectangle(D2D1::RectF(sideX, sideY, sideX + wPy, sideY + config.ScaleF(7.0f)), bPyLang.Get());
    pRT->FillRectangle(D2D1::RectF(sideX + wPy, sideY, sideX + wPy + wCpp, sideY + config.ScaleF(7.0f)), bCppLang.Get());
    sideY += config.ScaleF(12.0f);

    const wchar_t* langLegend = (m_activeLanguage == RuntimeLanguage::Php)
        ? L"● PHP 88.4%   ● HTML 9.2%   ● Other 2.4%"
        : (m_activeLanguage == RuntimeLanguage::Node
            ? L"● JavaScript 72.8%   ● TypeScript 23.1%   ● Other 4.1%"
            : L"● Python 74.2%   ● C++ 21.6%   ● Other 4.2%");
    D2D1_RECT_F rcLegend = D2D1::RectF(sideX, sideY, sideX + sidebarW, sideY + config.ScaleF(16.0f));
    pRT->DrawText(langLegend, static_cast<UINT32>(wcslen(langLegend)), theme.fontBadge.Get(), rcLegend, bGhMuted.Get());

    pRT->PopAxisAlignedClip();
}

} // namespace phide::ui
