/**
 * @file ApiTesterView.cpp
 * @brief Implementation of production-ready full-fledged API Tester in Phide native desktop design.
 */

#include "phide/ui/ApiTesterView.hpp"
#include <winhttp.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>

#pragma comment(lib, "winhttp.lib")

namespace phide::ui {

// =============================================================================
// DirectWrite Caret Measurement & Selection Helpers
// =============================================================================
static float MeasureCaretX(IDWriteFactory* pFactory, IDWriteTextFormat* pFormat,
                           const std::wstring& text, size_t caretPos,
                           float maxWidth = 10000.0f, float maxHeight = 100.0f) {
    if (!pFactory || !pFormat || text.empty() || caretPos == 0) return 0.0f;
    size_t c = (std::min)(caretPos, text.length());
    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
    if (SUCCEEDED(pFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.length()),
                                            pFormat, maxWidth, maxHeight, &pLayout))) {
        FLOAT hx = 0.0f, hy = 0.0f;
        DWRITE_HIT_TEST_METRICS htm{};
        if (SUCCEEDED(pLayout->HitTestTextPosition(static_cast<UINT32>(c), FALSE, &hx, &hy, &htm))) {
            return hx;
        }
    }
    return 0.0f;
}

static size_t HitTestCaretIndex(IDWriteFactory* pFactory, IDWriteTextFormat* pFormat,
                                const std::wstring& text, float clickX, float clickY = 0.0f,
                                float maxWidth = 10000.0f, float maxHeight = 100.0f) {
    if (!pFactory || !pFormat || text.empty()) return 0;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
    if (SUCCEEDED(pFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.length()),
                                            pFormat, maxWidth, maxHeight, &pLayout))) {
        BOOL isTrailing = FALSE, isInside = FALSE;
        DWRITE_HIT_TEST_METRICS htm{};
        if (SUCCEEDED(pLayout->HitTestPoint(clickX, clickY, &isTrailing, &isInside, &htm))) {
            return isTrailing ? (htm.textPosition + htm.length) : htm.textPosition;
        }
    }
    return text.length();
}

static void DrawTextSelection(ID2D1RenderTarget* pRT, ID2D1Brush* pBrush, IDWriteFactory* pFactory,
                              IDWriteTextFormat* pFormat, const std::wstring& text,
                              size_t selStart, size_t selEnd, float left, float top,
                              float maxWidth = 10000.0f, float maxHeight = 100.0f) {
    if (!pRT || !pBrush || !pFactory || !pFormat || text.empty() || selStart == selEnd) return;
    size_t minS = (std::min)(selStart, selEnd);
    size_t maxS = (std::min)((std::max)(selStart, selEnd), text.length());
    if (minS >= maxS) return;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
    if (SUCCEEDED(pFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.length()),
                                            pFormat, maxWidth, maxHeight, &pLayout))) {
        DWRITE_HIT_TEST_METRICS htm[32];
        UINT32 actualCount = 0;
        if (SUCCEEDED(pLayout->HitTestTextRange(static_cast<UINT32>(minS), static_cast<UINT32>(maxS - minS),
                                                0.0f, 0.0f, htm, 32, &actualCount))) {
            for (UINT32 i = 0; i < actualCount; ++i) {
                D2D1_RECT_F rc = D2D1::RectF(left + htm[i].left, top + htm[i].top,
                                             left + htm[i].left + htm[i].width, top + htm[i].top + htm[i].height);
                pRT->FillRectangle(rc, pBrush);
            }
        }
    }
}

// =============================================================================
// JSON Formatter Helper
// =============================================================================
static std::wstring FormatJsonPretty(const std::wstring& json) {
    std::wstring result;
    result.reserve(json.length() * 2);
    int indentLevel = 0;
    bool inString = false;
    bool isEscaped = false;

    auto appendIndent = [&](int level) {
        result.push_back(L'\n');
        for (int i = 0; i < level; ++i) {
            result.append(L"  ");
        }
    };

    for (size_t i = 0; i < json.length(); ++i) {
        wchar_t c = json[i];

        if (inString) {
            result.push_back(c);
            if (isEscaped) {
                isEscaped = false;
            } else if (c == L'\\') {
                isEscaped = true;
            } else if (c == L'"') {
                inString = false;
            }
            continue;
        }

        if (c == L'"') {
            inString = true;
            result.push_back(c);
            continue;
        }

        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            continue;
        }

        if (c == L'{' || c == L'[') {
            result.push_back(c);
            size_t nextIdx = i + 1;
            while (nextIdx < json.length() && (json[nextIdx] == L' ' || json[nextIdx] == L'\t' || json[nextIdx] == L'\r' || json[nextIdx] == L'\n')) {
                nextIdx++;
            }
            if (nextIdx < json.length() && ((c == L'{' && json[nextIdx] == L'}') || (c == L'[' && json[nextIdx] == L']'))) {
                result.push_back(json[nextIdx]);
                i = nextIdx;
                continue;
            }
            indentLevel++;
            appendIndent(indentLevel);
        } else if (c == L'}' || c == L']') {
            if (indentLevel > 0) indentLevel--;
            appendIndent(indentLevel);
            result.push_back(c);
        } else if (c == L',') {
            result.push_back(c);
            appendIndent(indentLevel);
        } else if (c == L':') {
            result.push_back(c);
            result.push_back(L' ');
        } else {
            result.push_back(c);
        }
    }

    return result.empty() ? json : result;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &strTo[0], sizeNeeded, nullptr, nullptr);
    return strTo;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring wstrTo(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &wstrTo[0], sizeNeeded);
    return wstrTo;
}

static std::wstring Base64Encode(const std::string& in) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return Utf8ToWide(out);
}

static std::string UrlEncodeUtf8(const std::wstring& wstr) {
    std::string utf8 = WideToUtf8(wstr);
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else if (c == ' ') {
            escaped << '+';
        } else {
            escaped << std::uppercase << '%' << std::setw(2) << int(c) << std::nouppercase;
        }
    }
    return escaped.str();
}

static bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) noexcept {
    return pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom;
}

static bool CaseInsensitiveFind(const std::wstring& str, const std::wstring& query) {
    if (query.empty()) return true;
    if (query.length() > str.length()) return false;
    auto it = std::search(
        str.begin(), str.end(),
        query.begin(), query.end(),
        [](wchar_t ch1, wchar_t ch2) { return towlower(ch1) == towlower(ch2); }
    );
    return it != str.end();
}

// Standard HTTP Header Catalog
static const wchar_t* const kStandardHeaderKeys[] = {
    L"Accept",
    L"Accept-Charset",
    L"Accept-Encoding",
    L"Accept-Language",
    L"Access-Control-Allow-Headers",
    L"Access-Control-Allow-Methods",
    L"Access-Control-Allow-Origin",
    L"Access-Control-Request-Headers",
    L"Access-Control-Request-Method",
    L"Authorization",
    L"Cache-Control",
    L"Connection",
    L"Content-Disposition",
    L"Content-Encoding",
    L"Content-Language",
    L"Content-Length",
    L"Content-Type",
    L"Cookie",
    L"DNT",
    L"Host",
    L"If-Match",
    L"If-Modified-Since",
    L"If-None-Match",
    L"Origin",
    L"Pragma",
    L"Range",
    L"Referer",
    L"Sec-Fetch-Dest",
    L"Sec-Fetch-Mode",
    L"Sec-Fetch-Site",
    L"User-Agent",
    L"X-API-Key",
    L"X-CSRF-Token",
    L"X-Forwarded-For",
    L"X-Forwarded-Proto",
    L"X-Requested-With"
};

// =============================================================================
// ApiTesterView Constructor & Setup
// =============================================================================
ApiTesterView::ApiTesterView() {
    AddNewSession();
}

void ApiTesterView::DiscardDeviceResources() {
}

void ApiTesterView::AddNewSession() {
    ApiRequestSession session;
    session.method = HttpMethod::Get;
    session.url.clear();
    session.activeReqTab = ApiRequestTab::Params;
    session.bodyType = ApiBodyType::None;
    session.bodyText.clear();
    EnsureDefaultData(session);
    m_sessions.push_back(std::move(session));
    m_activeSessionIdx = m_sessions.size() - 1;
}

void ApiTesterView::EnsureDefaultData(ApiRequestSession& session) {
    if (session.params.empty()) {
        session.params.push_back({ true, L"", L"", L"" });
    }
    if (session.headers.empty()) {
        session.headers.push_back({ true, L"", L"", L"" });
    }
    if (session.formData.empty()) {
        session.formData.push_back({ true, L"", L"", L"" });
    }
    if (session.urlEncoded.empty()) {
        session.urlEncoded.push_back({ true, L"", L"", L"" });
    }
}

std::wstring ApiTesterView::GetActiveRequestDisplay() const {
    if (m_activeSessionIdx < m_sessions.size()) {
        const auto& s = m_sessions[m_activeSessionIdx];
        if (!s.url.empty()) {
            return std::wstring(HttpMethodToString(s.method)) + L" " + s.url;
        }
        return std::wstring(HttpMethodToString(s.method));
    }
    return L"API Tester";
}

// =============================================================================
// Query Params <-> URL Synchronizer
// =============================================================================
void ApiTesterView::SyncParamsToUrl(ApiRequestSession& session) {
    std::wstring baseUrl = session.url;
    auto qPos = baseUrl.find(L'?');
    if (qPos != std::wstring::npos) {
        baseUrl = baseUrl.substr(0, qPos);
    }

    std::wstring qStr;
    bool first = true;
    for (const auto& p : session.params) {
        if (p.enabled && (!p.key.empty() || !p.value.empty())) {
            if (!first) qStr += L"&";
            first = false;
            qStr += p.key;
            if (!p.value.empty()) {
                qStr += L"=" + p.value;
            }
        }
    }

    if (!qStr.empty()) {
        session.url = baseUrl + L"?" + qStr;
    } else {
        session.url = baseUrl;
    }
    session.urlCaret = session.url.length();
}

void ApiTesterView::SyncUrlToParams(ApiRequestSession& session) {
    auto qPos = session.url.find(L'?');
    if (qPos == std::wstring::npos) {
        std::vector<ApiKeyValueItem> kept;
        for (const auto& p : session.params) {
            if (!p.enabled && (!p.key.empty() || !p.value.empty())) {
                kept.push_back(p);
            }
        }
        if (kept.empty()) {
            kept.push_back({ true, L"", L"", L"" });
        }
        session.params = std::move(kept);
        return;
    }

    std::wstring qStr = session.url.substr(qPos + 1);
    std::vector<ApiKeyValueItem> newParams;

    size_t start = 0;
    while (start < qStr.length()) {
        size_t nextAmp = qStr.find(L'&', start);
        std::wstring pair = (nextAmp == std::wstring::npos) ? qStr.substr(start) : qStr.substr(start, nextAmp - start);
        if (!pair.empty()) {
            auto eqPos = pair.find(L'=');
            if (eqPos != std::wstring::npos) {
                newParams.push_back({ true, pair.substr(0, eqPos), pair.substr(eqPos + 1), L"" });
            } else {
                newParams.push_back({ true, pair, L"", L"" });
            }
        }
        if (nextAmp == std::wstring::npos) break;
        start = nextAmp + 1;
    }

    for (const auto& p : session.params) {
        if (!p.enabled && (!p.key.empty() || !p.value.empty())) {
            newParams.push_back(p);
        }
    }

    if (newParams.empty()) {
        newParams.push_back({ true, L"", L"", L"" });
    }
    session.params = std::move(newParams);
}

// =============================================================================
// Header Autocomplete Dropdown Logic
// =============================================================================
void ApiTesterView::UpdateHeaderSuggestions(const std::wstring& currentText) {
    m_headerSuggestions.clear();
    m_headerSuggestHoverIdx = -1;

    if (m_isHeaderKeySuggest) {
        if (currentText.empty()) {
            m_headerSuggestions = {
                L"Accept", L"Authorization", L"Cache-Control", L"Content-Type",
                L"Cookie", L"Origin", L"User-Agent", L"X-API-Key"
            };
        } else {
            for (const wchar_t* k : kStandardHeaderKeys) {
                if (CaseInsensitiveFind(k, currentText)) {
                    m_headerSuggestions.push_back(k);
                    if (m_headerSuggestions.size() >= 12) break;
                }
            }
        }
    } else {
        auto& s = m_sessions[m_activeSessionIdx];
        std::wstring currentKey;
        if (m_headerSuggestRow < s.headers.size()) {
            currentKey = s.headers[m_headerSuggestRow].key;
        }

        std::vector<std::wstring> pool;
        if (CaseInsensitiveFind(currentKey, L"content-type") || CaseInsensitiveFind(currentKey, L"accept")) {
            pool = {
                L"application/json",
                L"application/x-www-form-urlencoded",
                L"multipart/form-data",
                L"text/plain",
                L"text/html",
                L"application/xml",
                L"application/javascript",
                L"application/octet-stream",
                L"*/*"
            };
        } else if (CaseInsensitiveFind(currentKey, L"authorization")) {
            pool = { L"Bearer ", L"Basic ", L"Token " };
        } else if (CaseInsensitiveFind(currentKey, L"encoding")) {
            pool = { L"gzip, deflate, br", L"gzip", L"deflate", L"br" };
        } else if (CaseInsensitiveFind(currentKey, L"cache")) {
            pool = { L"no-cache", L"no-store", L"max-age=0", L"must-revalidate" };
        } else if (CaseInsensitiveFind(currentKey, L"connection")) {
            pool = { L"keep-alive", L"close" };
        } else if (CaseInsensitiveFind(currentKey, L"agent")) {
            pool = { L"Phide-ApiClient/1.0", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)" };
        } else {
            pool = { L"application/json", L"*/*", L"no-cache", L"keep-alive" };
        }

        for (const auto& item : pool) {
            if (currentText.empty() || CaseInsensitiveFind(item, currentText)) {
                m_headerSuggestions.push_back(item);
            }
        }
    }

    m_isHeaderSuggestOpen = !m_headerSuggestions.empty();
}

void ApiTesterView::SelectHeaderSuggestion(size_t index) {
    if (index >= m_headerSuggestions.size()) return;
    auto& s = m_sessions[m_activeSessionIdx];
    if (m_headerSuggestRow >= s.headers.size()) return;

    std::wstring selected = m_headerSuggestions[index];
    if (m_isHeaderKeySuggest) {
        s.headers[m_headerSuggestRow].key = selected;
        // Advance to Value cell and open Value suggestions immediately!
        m_focusedField = ApiFocusField::HeaderVal;
        s.cellCaret = s.headers[m_headerSuggestRow].value.length();
        m_isHeaderKeySuggest = false;
        UpdateHeaderSuggestions(s.headers[m_headerSuggestRow].value);
    } else {
        s.headers[m_headerSuggestRow].value = selected;
        s.cellCaret = selected.length();
        m_isHeaderSuggestOpen = false;
    }
    ClearSelection();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

// =============================================================================
// Production-Ready Asynchronous WinHTTP Engine
// =============================================================================
ApiResponseData ApiTesterView::ExecuteNetworkRequest(const ApiRequestSession& session) {
    ApiResponseData resp;

    std::wstring rawUrl = session.url;
    while (!rawUrl.empty() && (rawUrl.front() == L' ' || rawUrl.front() == L'\t')) rawUrl.erase(rawUrl.begin());
    while (!rawUrl.empty() && (rawUrl.back() == L' ' || rawUrl.back() == L'\t')) rawUrl.pop_back();

    if (rawUrl.empty()) {
        resp.isError = true;
        resp.errorMessage = L"Please enter a request URL before sending.";
        return resp;
    }

    std::wstring targetUrl = rawUrl;
    if (targetUrl.find(L"://") == std::wstring::npos) {
        if (targetUrl.starts_with(L"localhost") || targetUrl.starts_with(L"127.0.0.1") || targetUrl.starts_with(L"0.0.0.0")) {
            targetUrl = L"http://" + targetUrl;
        } else {
            targetUrl = L"https://" + targetUrl;
        }
    }

    if (session.authType == ApiAuthType::ApiKey && !session.apiKeyInHeader && !session.apiKeyName.empty()) {
        wchar_t sep = (targetUrl.find(L'?') == std::wstring::npos) ? L'?' : L'&';
        targetUrl += sep + session.apiKeyName + L"=" + session.apiKeyValue;
    }

    URL_COMPONENTSW urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t szHost[512] = { 0 };
    wchar_t szPath[4096] = { 0 };
    urlComp.lpszHostName = szHost;
    urlComp.dwHostNameLength = 512;
    urlComp.lpszUrlPath = szPath;
    urlComp.dwUrlPathLength = 4096;

    if (!WinHttpCrackUrl(targetUrl.c_str(), static_cast<DWORD>(targetUrl.length()), 0, &urlComp)) {
        resp.isError = true;
        resp.errorMessage = L"Invalid URL format: " + targetUrl;
        return resp;
    }

    HINTERNET hSession = WinHttpOpen(L"Phide-ApiClient/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.isError = true;
        resp.errorMessage = L"Failed to initialize WinHTTP session (Error " + std::to_wstring(GetLastError()) + L")";
        return resp;
    }

    int timeoutMs = 15000;
    try {
        if (!session.timeoutMsStr.empty()) {
            timeoutMs = (std::max)(1000, std::stoi(session.timeoutMsStr));
        }
    } catch (...) {
        timeoutMs = 15000;
    }
    WinHttpSetTimeouts(hSession, 5000, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, szHost, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        resp.isError = true;
        resp.errorMessage = L"Failed to connect to host: " + std::wstring(szHost);
        return resp;
    }

    DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    std::wstring verb = HttpMethodToString(session.method);

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, verb.c_str(),
                                            szPath[0] ? szPath : L"/",
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            reqFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.isError = true;
        resp.errorMessage = L"Failed to open HTTP request.";
        return resp;
    }

    if ((reqFlags & WINHTTP_FLAG_SECURE) && !session.verifySsl) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    DWORD redirectPolicy = session.followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    // 1. Build Headers String
    std::wstring allHeaders;
    for (const auto& h : session.headers) {
        if (h.enabled && !h.key.empty()) {
            allHeaders += h.key + L": " + h.value + L"\r\n";
        }
    }

    // 2. Add Authorization Header
    if (session.authType == ApiAuthType::BearerToken && !session.bearerToken.empty()) {
        allHeaders += L"Authorization: Bearer " + session.bearerToken + L"\r\n";
    } else if (session.authType == ApiAuthType::BasicAuth && (!session.basicUser.empty() || !session.basicPass.empty())) {
        std::string raw = WideToUtf8(session.basicUser) + ":" + WideToUtf8(session.basicPass);
        allHeaders += L"Authorization: Basic " + Base64Encode(raw) + L"\r\n";
    } else if (session.authType == ApiAuthType::ApiKey && session.apiKeyInHeader && !session.apiKeyName.empty()) {
        allHeaders += session.apiKeyName + L": " + session.apiKeyValue + L"\r\n";
    }

    // 3. Prepare Body Payload & Content-Type
    std::string payloadBytes;
    if (session.method != HttpMethod::Get && session.method != HttpMethod::Head) {
        if (session.bodyType == ApiBodyType::RawJson) {
            payloadBytes = WideToUtf8(session.bodyText);
            if (allHeaders.find(L"Content-Type:") == std::wstring::npos && allHeaders.find(L"content-type:") == std::wstring::npos) {
                allHeaders += L"Content-Type: application/json; charset=utf-8\r\n";
            }
        } else if (session.bodyType == ApiBodyType::UrlEncoded) {
            bool first = true;
            for (const auto& item : session.urlEncoded) {
                if (item.enabled && !item.key.empty()) {
                    if (!first) payloadBytes += "&";
                    first = false;
                    payloadBytes += UrlEncodeUtf8(item.key) + "=" + UrlEncodeUtf8(item.value);
                }
            }
            if (allHeaders.find(L"Content-Type:") == std::wstring::npos && allHeaders.find(L"content-type:") == std::wstring::npos) {
                allHeaders += L"Content-Type: application/x-www-form-urlencoded\r\n";
            }
        } else if (session.bodyType == ApiBodyType::FormData) {
            std::string boundary = "----PhideFormBoundary7MA4YWxkTrZu0gW";
            std::ostringstream bodyStream;
            for (const auto& item : session.formData) {
                if (item.enabled && !item.key.empty()) {
                    bodyStream << "--" << boundary << "\r\n";
                    bodyStream << "Content-Disposition: form-data; name=\"" << WideToUtf8(item.key) << "\"\r\n\r\n";
                    bodyStream << WideToUtf8(item.value) << "\r\n";
                }
            }
            bodyStream << "--" << boundary << "--\r\n";
            payloadBytes = bodyStream.str();
            if (allHeaders.find(L"Content-Type:") == std::wstring::npos && allHeaders.find(L"content-type:") == std::wstring::npos) {
                allHeaders += L"Content-Type: multipart/form-data; boundary=" + Utf8ToWide(boundary) + L"\r\n";
            }
        }
    }

    auto tStart = std::chrono::high_resolution_clock::now();

    BOOL bSend = WinHttpSendRequest(hRequest,
                                    allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str(),
                                    static_cast<DWORD>(allHeaders.length()),
                                    payloadBytes.empty() ? WINHTTP_NO_REQUEST_DATA : (void*)payloadBytes.data(),
                                    static_cast<DWORD>(payloadBytes.size()),
                                    static_cast<DWORD>(payloadBytes.size()),
                                    0);

    if (!bSend || !WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.isError = true;
        resp.errorMessage = L"Request failed (WinHTTP Error " + std::to_wstring(err) + L"). Verify URL and server status.";
        return resp;
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    resp.timeMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    // Query Status Code
    DWORD dwStatus = 0;
    DWORD dwStatusSize = sizeof(dwStatus);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatus, &dwStatusSize, WINHTTP_NO_HEADER_INDEX)) {
        resp.statusCode = static_cast<int>(dwStatus);
    }

    // Query Status Text
    wchar_t szStatusText[256] = { 0 };
    DWORD dwStatusTextSize = sizeof(szStatusText);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_TEXT,
                            WINHTTP_HEADER_NAME_BY_INDEX, szStatusText, &dwStatusTextSize, WINHTTP_NO_HEADER_INDEX)) {
        resp.statusText = szStatusText;
    }
    if (resp.statusText.empty()) {
        if (resp.statusCode == 200) resp.statusText = L"OK";
        else if (resp.statusCode == 201) resp.statusText = L"Created";
        else if (resp.statusCode == 204) resp.statusText = L"No Content";
        else if (resp.statusCode == 400) resp.statusText = L"Bad Request";
        else if (resp.statusCode == 401) resp.statusText = L"Unauthorized";
        else if (resp.statusCode == 403) resp.statusText = L"Forbidden";
        else if (resp.statusCode == 404) resp.statusText = L"Not Found";
        else if (resp.statusCode == 500) resp.statusText = L"Internal Server Error";
        else resp.statusText = std::to_wstring(resp.statusCode);
    }

    // Query All Response Headers
    DWORD dwHeaderSize = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &dwHeaderSize, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && dwHeaderSize > 0) {
        std::vector<wchar_t> hdrBuf(dwHeaderSize / sizeof(wchar_t) + 1, 0);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, hdrBuf.data(), &dwHeaderSize, WINHTTP_NO_HEADER_INDEX)) {
            std::wstring rawHdrs(hdrBuf.data());
            std::wstringstream ss(rawHdrs);
            std::wstring line;
            bool isFirstLine = true;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (line.empty()) continue;
                if (isFirstLine) { isFirstLine = false; continue; }
                auto colon = line.find(L':');
                if (colon != std::wstring::npos) {
                    std::wstring k = line.substr(0, colon);
                    std::wstring v = line.substr(colon + 1);
                    while (!v.empty() && v.front() == L' ') v.erase(v.begin());
                    resp.headers.push_back({ k, v });
                    if (_wcsicmp(k.c_str(), L"set-cookie") == 0) {
                        resp.cookies.push_back(v);
                    }
                }
            }
        }
    }

    // Read Response Body
    std::string bodyRaw;
    DWORD dwSize = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;

        std::vector<char> chunk(dwSize + 1, 0);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, chunk.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
            bodyRaw.append(chunk.data(), dwDownloaded);
        }
    } while (dwSize > 0);

    resp.sizeBytes = bodyRaw.size();
    resp.body = Utf8ToWide(bodyRaw);

    bool isJsonResponse = false;
    for (const auto& h : resp.headers) {
        if (_wcsicmp(h.first.c_str(), L"content-type") == 0 &&
            (h.second.find(L"json") != std::wstring::npos || h.second.find(L"JSON") != std::wstring::npos)) {
            isJsonResponse = true;
            break;
        }
    }
    if (!resp.body.empty() && (isJsonResponse || resp.body.front() == L'{' || resp.body.front() == L'[')) {
        resp.body = FormatJsonPretty(resp.body);
    }

    resp.hasResponse = true;
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

void ApiTesterView::SendCurrentRequest() {
    if (m_activeSessionIdx >= m_sessions.size()) return;
    auto& session = m_sessions[m_activeSessionIdx];
    if (session.isSending) return;

    session.isSending = true;
    session.response = ApiResponseData{};
    session.respScrollY = 0.0f;

    ApiRequestSession sessionCopy = session;
    size_t sessionIdx = m_activeSessionIdx;
    HWND hwnd = m_hwnd;

    std::thread([this, sessionIdx, sessionCopy, hwnd]() {
        ApiResponseData result = ExecuteNetworkRequest(sessionCopy);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (sessionIdx < m_sessions.size()) {
                m_sessions[sessionIdx].response = std::move(result);
                m_sessions[sessionIdx].isSending = false;
            }
        }
        if (hwnd) {
            InvalidateRect(hwnd, nullptr, FALSE);
            PostMessageW(hwnd, WM_NULL, 0, 0);
        }
    }).detach();

    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

// =============================================================================
// Selection & Clipboard Helpers
// =============================================================================
void ApiTesterView::ClearSelection() noexcept {
    m_hasSelection = false;
    m_selStart = 0;
    m_selEnd = 0;
}

std::wstring* ApiTesterView::GetActiveCellString(ApiRequestSession& s) {
    switch (m_focusedField) {
        case ApiFocusField::ParamKey:   return (m_focusedRowIndex < s.params.size()) ? &s.params[m_focusedRowIndex].key : nullptr;
        case ApiFocusField::ParamVal:   return (m_focusedRowIndex < s.params.size()) ? &s.params[m_focusedRowIndex].value : nullptr;
        case ApiFocusField::ParamDesc:  return (m_focusedRowIndex < s.params.size()) ? &s.params[m_focusedRowIndex].description : nullptr;
        case ApiFocusField::HeaderKey:  return (m_focusedRowIndex < s.headers.size()) ? &s.headers[m_focusedRowIndex].key : nullptr;
        case ApiFocusField::HeaderVal:  return (m_focusedRowIndex < s.headers.size()) ? &s.headers[m_focusedRowIndex].value : nullptr;
        case ApiFocusField::HeaderDesc: return (m_focusedRowIndex < s.headers.size()) ? &s.headers[m_focusedRowIndex].description : nullptr;
        case ApiFocusField::FormKey:    return (m_focusedRowIndex < s.formData.size()) ? &s.formData[m_focusedRowIndex].key : nullptr;
        case ApiFocusField::FormVal:    return (m_focusedRowIndex < s.formData.size()) ? &s.formData[m_focusedRowIndex].value : nullptr;
        case ApiFocusField::FormDesc:   return (m_focusedRowIndex < s.formData.size()) ? &s.formData[m_focusedRowIndex].description : nullptr;
        case ApiFocusField::UrlEncKey:  return (m_focusedRowIndex < s.urlEncoded.size()) ? &s.urlEncoded[m_focusedRowIndex].key : nullptr;
        case ApiFocusField::UrlEncVal:  return (m_focusedRowIndex < s.urlEncoded.size()) ? &s.urlEncoded[m_focusedRowIndex].value : nullptr;
        case ApiFocusField::UrlEncDesc: return (m_focusedRowIndex < s.urlEncoded.size()) ? &s.urlEncoded[m_focusedRowIndex].description : nullptr;
        case ApiFocusField::AuthToken:  return &s.bearerToken;
        case ApiFocusField::ApiKeyName: return &s.apiKeyName;
        case ApiFocusField::ApiKeyValue:return &s.apiKeyValue;
        case ApiFocusField::AuthUser:   return &s.basicUser;
        case ApiFocusField::AuthPass:   return &s.basicPass;
        case ApiFocusField::SettingsTimeout: return &s.timeoutMsStr;
        default: return nullptr;
    }
}

void ApiTesterView::DeleteSelectedRange(ApiRequestSession& s) {
    if (!m_hasSelection) return;
    size_t minS = (std::min)(m_selStart, m_selEnd);
    size_t maxS = (std::max)(m_selStart, m_selEnd);
    size_t len = maxS - minS;
    if (len == 0) {
        ClearSelection();
        return;
    }

    if (m_focusedField == ApiFocusField::Url) {
        if (minS < s.url.length()) {
            s.url.erase(minS, len);
            s.urlCaret = minS;
            SyncUrlToParams(s);
        }
    } else if (m_focusedField == ApiFocusField::Body) {
        if (minS < s.bodyText.length()) {
            s.bodyText.erase(minS, len);
            s.bodyCaret = minS;
        }
    } else if (std::wstring* pCell = GetActiveCellString(s)) {
        if (minS < pCell->length()) {
            pCell->erase(minS, len);
            s.cellCaret = minS;
            if (m_focusedField == ApiFocusField::ParamKey || m_focusedField == ApiFocusField::ParamVal) {
                SyncParamsToUrl(s);
            }
        }
    }
    ClearSelection();
}

std::wstring ApiTesterView::GetSelectedText(const ApiRequestSession& s) const {
    if (!m_hasSelection) return L"";
    size_t minS = (std::min)(m_selStart, m_selEnd);
    size_t maxS = (std::max)(m_selStart, m_selEnd);
    if (minS >= maxS) return L"";

    if (m_focusedField == ApiFocusField::Url) {
        if (minS < s.url.length()) return s.url.substr(minS, maxS - minS);
    } else if (m_focusedField == ApiFocusField::Body) {
        if (minS < s.bodyText.length()) return s.bodyText.substr(minS, maxS - minS);
    } else {
        // Table cell or auth
        auto* mutableThis = const_cast<ApiTesterView*>(this);
        auto* mutableSession = const_cast<ApiRequestSession*>(&s);
        if (std::wstring* pCell = mutableThis->GetActiveCellString(*mutableSession)) {
            if (minS < pCell->length()) return pCell->substr(minS, maxS - minS);
        }
    }
    return L"";
}

void ApiTesterView::PasteFromClipboard() {
    if (!m_hwnd || !OpenClipboard(m_hwnd)) return;
    std::wstring clipText;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* pStr = static_cast<const wchar_t*>(GlobalLock(hData));
        if (pStr) {
            clipText = pStr;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    if (clipText.empty()) return;

    clipText.erase(std::remove(clipText.begin(), clipText.end(), L'\r'), clipText.end());

    auto& s = m_sessions[m_activeSessionIdx];
    if (m_hasSelection) {
        DeleteSelectedRange(s);
    }

    if (m_focusedField == ApiFocusField::Url) {
        std::replace(clipText.begin(), clipText.end(), L'\n', L' ');
        size_t pos = (std::min)(s.urlCaret, s.url.length());
        s.url.insert(pos, clipText);
        s.urlCaret = pos + clipText.length();
        SyncUrlToParams(s);
    } else if (m_focusedField == ApiFocusField::Body) {
        size_t pos = (std::min)(s.bodyCaret, s.bodyText.length());
        s.bodyText.insert(pos, clipText);
        s.bodyCaret = pos + clipText.length();
    } else if (std::wstring* pCell = GetActiveCellString(s)) {
        std::replace(clipText.begin(), clipText.end(), L'\n', L' ');
        size_t pos = (std::min)(s.cellCaret, pCell->length());
        pCell->insert(pos, clipText);
        s.cellCaret = pos + clipText.length();
        if (m_focusedField == ApiFocusField::ParamKey || m_focusedField == ApiFocusField::ParamVal) {
            SyncParamsToUrl(s);
        } else if (m_focusedField == ApiFocusField::HeaderKey || m_focusedField == ApiFocusField::HeaderVal) {
            UpdateHeaderSuggestions(*pCell);
        }
    }
}

void ApiTesterView::CopyResponseToClipboard() {
    auto& s = m_sessions[m_activeSessionIdx];
    std::wstring textToCopy;
    if (s.response.hasResponse) {
        if (s.activeRespTab == ApiResponseTab::Headers) {
            for (const auto& h : s.response.headers) {
                textToCopy += h.first + L": " + h.second + L"\r\n";
            }
        } else if (s.activeRespTab == ApiResponseTab::Cookies) {
            for (const auto& c : s.response.cookies) {
                textToCopy += c + L"\r\n";
            }
        } else {
            textToCopy = s.response.body;
        }
    } else if (s.response.isError) {
        textToCopy = s.response.errorMessage;
    }

    if (textToCopy.empty()) {
        m_toastMessage = L"Nothing to copy yet";
        m_toastExpiry = GetTickCount() + 1500;
        return;
    }

    if (m_hwnd && OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t bytes = (textToCopy.length() + 1) * sizeof(wchar_t);
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hGlobal) {
            void* pMem = GlobalLock(hGlobal);
            if (pMem) {
                memcpy(pMem, textToCopy.c_str(), bytes);
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            }
        }
        CloseClipboard();
        m_toastMessage = L"✓ Copied to clipboard!";
        m_toastExpiry = GetTickCount() + 2000;
    }
}

// =============================================================================
// Rendering Pipeline
// =============================================================================
void ApiTesterView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           float x, float w, float y, float h) {
    if (!pRT) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    m_viewX = x;
    m_viewY = y;
    m_viewW = w;
    m_viewH = h;

    D2D1_RECT_F rcFull = D2D1::RectF(x, y, x + w, y + h);
    pRT->FillRectangle(rcFull, theme.brushCanvasBg.Get());

    if (m_sessions.empty()) {
        AddNewSession();
    }
    if (m_activeSessionIdx >= m_sessions.size()) {
        m_activeSessionIdx = 0;
    }
    EnsureDefaultData(m_sessions[m_activeSessionIdx]);

    DWORD now = GetTickCount();
    if (now - m_lastCaretBlink > 500) {
        m_showCaret = !m_showCaret;
        m_lastCaretBlink = now;
    }

    float padX = config.ScaleF(16.0f);
    float curY = y + config.ScaleF(12.0f);

    float availH = (y + h) - curY - config.ScaleF(12.0f);
    float minPaneH = config.ScaleF(170.0f);
    float reqPaneH = (std::max)(minPaneH, (std::min)(availH - minPaneH, availH * m_splitRatio));
    float respPaneH = (std::max)(minPaneH, availH - reqPaneH - m_splitterH);

    // 1. Request Upper Container (URL Bar + Sub Navigation Tabs + Content)
    D2D1_RECT_F rcReqPane = D2D1::RectF(x + padX, curY, x + w - padX, curY + reqPaneH);
    D2D1_ROUNDED_RECT rrcReqPane = D2D1::RoundedRect(rcReqPane, 8.0f, 8.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.94f) : D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f), &cardBg);
    if (cardBg) pRT->FillRoundedRectangle(rrcReqPane, cardBg.Get());
    pRT->DrawRoundedRectangle(rrcReqPane, theme.brushBorderSubtle.Get(), 1.0f);

    float reqContentY = curY + config.ScaleF(12.0f);
    float reqContentW = (rcReqPane.right - rcReqPane.left) - config.ScaleF(24.0f);
    float reqContentX = rcReqPane.left + config.ScaleF(12.0f);

    // 1a. URL Bar
    RenderUrlBar(pRT, theme, config, reqContentX, reqContentY, reqContentW);
    reqContentY += config.ScaleF(42.0f);

    // 1b. Request Sub-Nav Tabs (Params, Authorization, Headers, Body, Settings)
    RenderSubNavTabs(pRT, theme, config, reqContentX, reqContentY, reqContentW);
    reqContentY += config.ScaleF(32.0f);

    // 1c. Request Sub-Nav Content
    m_subContentH = rcReqPane.bottom - reqContentY - config.ScaleF(10.0f);
    RenderSubNavContent(pRT, theme, config, reqContentX, reqContentY, reqContentW, m_subContentH);

    // 2. Resizable Horizontal Splitter
    float splitterY = curY + reqPaneH;
    m_rcSplitter = D2D1::RectF(x + padX, splitterY, x + w - padX, splitterY + m_splitterH);
    D2D1_ROUNDED_RECT rrcSplit = D2D1::RoundedRect(m_rcSplitter, 3.0f, 3.0f);
    bool splitHover = m_isDraggingSplitter || PtInRectF(m_mousePt, m_rcSplitter);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> splitBrush;
    pRT->CreateSolidColorBrush(splitHover
        ? D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.45f)
        : (theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.18f, 0.22f, 0.70f) : D2D1::ColorF(0.88f, 0.88f, 0.92f, 1.0f)), &splitBrush);
    if (splitBrush) pRT->FillRoundedRectangle(rrcSplit, splitBrush.Get());

    float cx = (m_rcSplitter.left + m_rcSplitter.right) * 0.5f;
    float cy = (m_rcSplitter.top + m_rcSplitter.bottom) * 0.5f;
    float gripW = config.ScaleF(32.0f);
    pRT->DrawLine(D2D1::Point2F(cx - gripW * 0.5f, cy), D2D1::Point2F(cx + gripW * 0.5f, cy),
                  splitHover ? theme.brushBrandAccent.Get() : theme.brushTextMuted.Get(), 2.0f);

    // 3. Response Lower Container
    float respY = splitterY + m_splitterH;
    D2D1_RECT_F rcRespPane = D2D1::RectF(x + padX, respY, x + w - padX, respY + respPaneH);
    D2D1_ROUNDED_RECT rrcRespPane = D2D1::RoundedRect(rcRespPane, 8.0f, 8.0f);
    if (cardBg) pRT->FillRoundedRectangle(rrcRespPane, cardBg.Get());
    pRT->DrawRoundedRectangle(rrcRespPane, theme.brushBorderSubtle.Get(), 1.0f);

    m_respContentH = respPaneH - config.ScaleF(20.0f);
    RenderResponseSection(pRT, theme, config, rcRespPane.left + config.ScaleF(12.0f),
                          rcRespPane.top + config.ScaleF(10.0f),
                          (rcRespPane.right - rcRespPane.left) - config.ScaleF(24.0f),
                          m_respContentH);

    // 4. Floating Autocomplete & Dropdowns (Rendered on top of all containers)
    if (m_isHeaderSuggestOpen) {
        RenderHeaderSuggestions(pRT, theme, config);
    }
    if (m_isMethodDropdownOpen) {
        RenderMethodDropdown(pRT, theme, config);
    }
    if (m_isAuthDropdownOpen) {
        RenderAuthDropdown(pRT, theme, config);
    }

    // 5. Toast Notification
    if (!m_toastMessage.empty() && now < m_toastExpiry) {
        float toastW = config.ScaleF(210.0f);
        float toastH = config.ScaleF(32.0f);
        float tx = x + (w - toastW) * 0.5f;
        float ty = y + h - toastH - config.ScaleF(24.0f);
        D2D1_RECT_F rcToast = D2D1::RectF(tx, ty, tx + toastW, ty + toastH);
        D2D1_ROUNDED_RECT rrcToast = D2D1::RoundedRect(rcToast, 6.0f, 6.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> toastBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.13f, 0.96f), &toastBg);
        if (toastBg) pRT->FillRoundedRectangle(rrcToast, toastBg.Get());
        pRT->DrawRoundedRectangle(rrcToast, theme.brushBrandAccent.Get(), 1.2f);
        pRT->DrawText(m_toastMessage.c_str(), static_cast<UINT32>(m_toastMessage.length()),
                      theme.fontButtonCenter.Get(), rcToast, theme.brushTextWhite.Get());
    }
}

// -----------------------------------------------------------------------------
// URL Bar & Method Selector
// -----------------------------------------------------------------------------
void ApiTesterView::RenderUrlBar(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 float x, float y, float w) {
    auto& s = m_sessions[m_activeSessionIdx];

    float btnW = config.ScaleF(100.0f);
    float sendBtnW = config.ScaleF(92.0f);
    float barH = config.ScaleF(34.0f);

    // 1. Method Dropdown Button
    m_rcMethodBtn = D2D1::RectF(x, y, x + btnW, y + barH);
    D2D1_ROUNDED_RECT rrcMethod = D2D1::RoundedRect(m_rcMethodBtn, 6.0f, 6.0f);

    bool methodHover = PtInRectF(m_mousePt, m_rcMethodBtn) || m_isMethodDropdownOpen;
    D2D1_COLOR_F mCol = GetMethodColor(s.method);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mBgBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(mCol.r, mCol.g, mCol.b, methodHover ? 0.24f : 0.15f), &mBgBrush);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mTxtBrush;
    pRT->CreateSolidColorBrush(mCol, &mTxtBrush);

    if (mBgBrush) pRT->FillRoundedRectangle(rrcMethod, mBgBrush.Get());
    if (mTxtBrush) pRT->DrawRoundedRectangle(rrcMethod, mTxtBrush.Get(), methodHover ? 1.5f : 1.2f);

    std::wstring mLabel = std::wstring(HttpMethodToString(s.method)) + L"  ▼";
    if (mTxtBrush) {
        pRT->DrawText(mLabel.c_str(), static_cast<UINT32>(mLabel.length()),
                      theme.fontButtonCenter.Get(), m_rcMethodBtn, mTxtBrush.Get());
    }

    // 2. URL Input Box
    float urlX = x + btnW + config.ScaleF(8.0f);
    float urlW = w - btnW - sendBtnW - config.ScaleF(16.0f);
    m_rcUrlBox = D2D1::RectF(urlX, y, urlX + urlW, y + barH);
    D2D1_ROUNDED_RECT rrcUrl = D2D1::RoundedRect(m_rcUrlBox, 6.0f, 6.0f);

    bool isUrlFocused = (m_focusedField == ApiFocusField::Url);
    pRT->FillRoundedRectangle(rrcUrl, theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrcUrl, isUrlFocused ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(),
                              isUrlFocused ? 1.5f : 1.0f);

    D2D1_RECT_F rcUrlText = D2D1::RectF(urlX + config.ScaleF(12.0f), y + config.ScaleF(7.0f),
                                       urlX + urlW - config.ScaleF(12.0f), y + barH - config.ScaleF(5.0f));
    pRT->PushAxisAlignedClip(rcUrlText, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (isUrlFocused && m_hasSelection) {
        DrawTextSelection(pRT, theme.brushTreeSelected.Get(), theme.dwriteFactory.Get(),
                          theme.fontTab.Get(), s.url, m_selStart, m_selEnd,
                          rcUrlText.left, rcUrlText.top, rcUrlText.right - rcUrlText.left, rcUrlText.bottom - rcUrlText.top);
    }

    if (s.url.empty()) {
        const wchar_t* hint = L"Enter request URL (e.g. https://example.com/api/v1/items)";
        pRT->DrawText(hint, static_cast<UINT32>(wcslen(hint)),
                      theme.fontHint.Get(), rcUrlText, theme.brushTextMuted.Get());
    } else {
        pRT->DrawText(s.url.c_str(), static_cast<UINT32>(s.url.length()),
                      theme.fontTab.Get(), rcUrlText, theme.brushTextPrimary.Get());
    }

    // Exact DirectWrite Caret Calculation (Zero guessing, crisp white cursor)
    if (isUrlFocused && m_showCaret) {
        float exactCaretX = rcUrlText.left + MeasureCaretX(theme.dwriteFactory.Get(), theme.fontTab.Get(),
                                                           s.url, s.urlCaret,
                                                           rcUrlText.right - rcUrlText.left, rcUrlText.bottom - rcUrlText.top);
        exactCaretX = (std::min)(exactCaretX, rcUrlText.right - 2.0f);
        pRT->DrawLine(D2D1::Point2F(exactCaretX, rcUrlText.top + 2.0f),
                      D2D1::Point2F(exactCaretX, rcUrlText.bottom - 2.0f),
                      theme.brushTextWhite.Get(), 1.5f);
    }
    pRT->PopAxisAlignedClip();

    // 3. Send Button
    float sendX = urlX + urlW + config.ScaleF(8.0f);
    m_rcSendBtn = D2D1::RectF(sendX, y, sendX + sendBtnW, y + barH);
    D2D1_ROUNDED_RECT rrcSend = D2D1::RoundedRect(m_rcSendBtn, 6.0f, 6.0f);

    bool sendHover = PtInRectF(m_mousePt, m_rcSendBtn);
    if (s.isSending) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sendDim;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.40f, 0.70f, 0.75f), &sendDim);
        if (sendDim) pRT->FillRoundedRectangle(rrcSend, sendDim.Get());
        pRT->DrawText(L"Sending...", 10, theme.fontButtonCenter.Get(), m_rcSendBtn, theme.brushTextWhite.Get());
    } else {
        if (sendHover) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sendBrt;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.56f, 0.94f, 1.0f), &sendBrt);
            if (sendBrt) pRT->FillRoundedRectangle(rrcSend, sendBrt.Get());
        } else {
            pRT->FillRoundedRectangle(rrcSend, theme.brushBrandAccent.Get());
        }
        pRT->DrawText(L"Send", 4, theme.fontButtonCenter.Get(), m_rcSendBtn, theme.brushTextWhite.Get());
    }
}

// -----------------------------------------------------------------------------
// Floating Method Dropdown
// -----------------------------------------------------------------------------
void ApiTesterView::RenderMethodDropdown(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    static const HttpMethod methods[] = {
        HttpMethod::Get, HttpMethod::Post, HttpMethod::Put,
        HttpMethod::Patch, HttpMethod::Delete, HttpMethod::Head, HttpMethod::Options
    };

    auto& s = m_sessions[m_activeSessionIdx];
    float dropW = config.ScaleF(126.0f);
    float itemH = config.ScaleF(28.0f);
    float dropH = itemH * 7.0f + config.ScaleF(8.0f);
    float dropX = m_rcMethodBtn.left;
    float dropY = m_rcMethodBtn.bottom + config.ScaleF(4.0f);

    m_rcMethodDropdown = D2D1::RectF(dropX, dropY, dropX + dropW, dropY + dropH);
    D2D1_ROUNDED_RECT rrcDrop = D2D1::RoundedRect(m_rcMethodDropdown, 6.0f, 6.0f);

    pRT->FillRoundedRectangle(rrcDrop, theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrcDrop, theme.brushMenuBorder.Get(), 1.0f);

    float curY = dropY + config.ScaleF(4.0f);
    for (HttpMethod m : methods) {
        D2D1_RECT_F rcItem = D2D1::RectF(dropX + config.ScaleF(4.0f), curY, dropX + dropW - config.ScaleF(4.0f), curY + itemH);
        bool isHovered = PtInRectF(m_mousePt, rcItem);
        bool isSelected = (s.method == m);

        if (isHovered || isSelected) {
            D2D1_ROUNDED_RECT rrcItem = D2D1::RoundedRect(rcItem, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrcItem, isHovered ? theme.brushItemHover.Get() : theme.brushItemActive.Get());
        }

        D2D1_COLOR_F mCol = GetMethodColor(m);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mTxt;
        pRT->CreateSolidColorBrush(mCol, &mTxt);

        float dotR = config.ScaleF(3.5f);
        D2D1_POINT_2F dotPt = D2D1::Point2F(rcItem.left + config.ScaleF(10.0f), (rcItem.top + rcItem.bottom) * 0.5f);
        if (mTxt) pRT->FillEllipse(D2D1::Ellipse(dotPt, dotR, dotR), mTxt.Get());

        D2D1_RECT_F rcLabel = D2D1::RectF(rcItem.left + config.ScaleF(22.0f), rcItem.top, rcItem.right - config.ScaleF(6.0f), rcItem.bottom);
        const wchar_t* mStr = HttpMethodToString(m);
        if (mTxt) {
            pRT->DrawText(mStr, static_cast<UINT32>(wcslen(mStr)),
                          theme.fontKeycap.Get(), rcLabel, mTxt.Get());
        }
        curY += itemH;
    }
}

// -----------------------------------------------------------------------------
// Floating Header Autocomplete Dropdown
// -----------------------------------------------------------------------------
void ApiTesterView::RenderHeaderSuggestions(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    if (m_headerSuggestions.empty()) return;

    float itemH = config.ScaleF(26.0f);
    size_t displayCount = (std::min)(m_headerSuggestions.size(), static_cast<size_t>(8));
    float dropH = static_cast<float>(displayCount) * itemH + config.ScaleF(8.0f);
    m_rcHeaderSuggest.bottom = m_rcHeaderSuggest.top + dropH;

    D2D1_ROUNDED_RECT rrcDrop = D2D1::RoundedRect(m_rcHeaderSuggest, 6.0f, 6.0f);
    pRT->FillRoundedRectangle(rrcDrop, theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrcDrop, theme.brushMenuBorder.Get(), 1.0f);

    float curY = m_rcHeaderSuggest.top + config.ScaleF(4.0f);
    for (size_t i = 0; i < displayCount; ++i) {
        D2D1_RECT_F rcRow = D2D1::RectF(m_rcHeaderSuggest.left + config.ScaleF(4.0f), curY,
                                       m_rcHeaderSuggest.right - config.ScaleF(4.0f), curY + itemH);
        bool isHovered = PtInRectF(m_mousePt, rcRow) || (static_cast<int>(i) == m_headerSuggestHoverIdx);
        if (isHovered) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRow, 4.0f, 4.0f), theme.brushItemHover.Get());
        }

        D2D1_RECT_F rcTxt = D2D1::RectF(rcRow.left + config.ScaleF(8.0f), rcRow.top + 2.0f,
                                       rcRow.right - config.ScaleF(8.0f), rcRow.bottom - 2.0f);
        pRT->DrawText(m_headerSuggestions[i].c_str(), static_cast<UINT32>(m_headerSuggestions[i].length()),
                      theme.fontMenuItem.Get(), rcTxt,
                      isHovered ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
        curY += itemH;
    }
}

// -----------------------------------------------------------------------------
// Request Sub Navigation Tabs (Params, Authorization, Headers, Body, Settings)
// -----------------------------------------------------------------------------
void ApiTesterView::RenderSubNavTabs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     float x, float y, float w) {
    auto& s = m_sessions[m_activeSessionIdx];
    m_subNavTabs.clear();

    struct NavInfo {
        ApiRequestTab tab;
        std::wstring label;
        bool hasBadge = false;
        std::wstring badgeText;
        bool hasDot = false;
    };

    size_t activeParamsCount = 0;
    for (const auto& p : s.params) {
        if (p.enabled && (!p.key.empty() || !p.value.empty())) activeParamsCount++;
    }

    size_t activeHeadersCount = 0;
    for (const auto& h : s.headers) {
        if (h.enabled && (!h.key.empty() || !h.value.empty())) activeHeadersCount++;
    }

    bool bodyActive = (s.bodyType != ApiBodyType::None);

    NavInfo navs[] = {
        { ApiRequestTab::Params,   L"Params",        activeParamsCount > 0,  std::to_wstring(activeParamsCount),  false },
        { ApiRequestTab::Auth,     L"Authorization", s.authType != ApiAuthType::NoAuth, L"", s.authType != ApiAuthType::NoAuth },
        { ApiRequestTab::Headers,  L"Headers",       activeHeadersCount > 0, std::to_wstring(activeHeadersCount), false },
        { ApiRequestTab::Body,     L"Body",          false, L"", bodyActive },
        { ApiRequestTab::Settings, L"Settings",      false, L"", false }
    };

    float curX = x;
    float tabH = config.ScaleF(26.0f);

    for (const auto& n : navs) {
        bool isActive = (s.activeReqTab == n.tab);
        float textW = static_cast<float>(n.label.length()) * config.ScaleF(7.8f);
        float tabW = textW + config.ScaleF(22.0f) + (n.hasBadge && !n.badgeText.empty() ? config.ScaleF(22.0f) : 0.0f) + (n.hasDot ? config.ScaleF(12.0f) : 0.0f);

        D2D1_RECT_F rcTab = D2D1::RectF(curX, y, curX + tabW, y + tabH);
        m_subNavTabs.push_back({ n.tab, rcTab });

        bool isHov = PtInRectF(m_mousePt, rcTab);

        D2D1_RECT_F rcText = D2D1::RectF(curX, y, curX + textW + config.ScaleF(16.0f), y + tabH);
        pRT->DrawText(n.label.c_str(), static_cast<UINT32>(n.label.length()),
                      theme.fontTab.Get(), rcText,
                      (isActive || isHov) ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get());

        if (n.hasBadge && !n.badgeText.empty()) {
            float badgeX = curX + textW + config.ScaleF(16.0f);
            D2D1_RECT_F rcBadge = D2D1::RectF(badgeX, y + config.ScaleF(4.0f), badgeX + config.ScaleF(18.0f), y + tabH - config.ScaleF(4.0f));
            D2D1_ROUNDED_RECT rrcB = D2D1::RoundedRect(rcBadge, 9.0f, 9.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBg;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.25f, 0.30f, 1.0f), &bBg);
            if (bBg) pRT->FillRoundedRectangle(rrcB, bBg.Get());
            pRT->DrawText(n.badgeText.c_str(), static_cast<UINT32>(n.badgeText.length()),
                          theme.fontKeycap.Get(), rcBadge, theme.brushTextWhite.Get());
        } else if (n.hasDot) {
            float dotX = curX + textW + config.ScaleF(16.0f);
            float dotY = y + tabH * 0.5f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> greenDot;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.85f, 0.40f, 1.0f), &greenDot);
            if (greenDot) pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), 3.2f, 3.2f), greenDot.Get());
        }

        if (isActive) {
            pRT->DrawLine(D2D1::Point2F(curX, y + tabH - 1.0f), D2D1::Point2F(curX + tabW - config.ScaleF(8.0f), y + tabH - 1.0f),
                          theme.brushBrandAccent.Get(), 2.0f);
        }

        curX += tabW + config.ScaleF(8.0f);
    }
}

// -----------------------------------------------------------------------------
// Sub-Navigation Content Router
// -----------------------------------------------------------------------------
void ApiTesterView::RenderSubNavContent(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                        float x, float y, float w, float h) {
    if (h <= config.ScaleF(20.0f)) return;
    auto& s = m_sessions[m_activeSessionIdx];

    switch (s.activeReqTab) {
        case ApiRequestTab::Params:
            RenderKeyValueTable(pRT, theme, config, x, y, w, h, s.params,
                                ApiFocusField::ParamKey, ApiFocusField::ParamVal, ApiFocusField::ParamDesc,
                                L"+ Add Param");
            break;
        case ApiRequestTab::Auth:
            RenderAuthTab(pRT, theme, config, x, y, w, h);
            break;
        case ApiRequestTab::Headers:
            RenderKeyValueTable(pRT, theme, config, x, y, w, h, s.headers,
                                ApiFocusField::HeaderKey, ApiFocusField::HeaderVal, ApiFocusField::HeaderDesc,
                                L"+ Add Header");
            break;
        case ApiRequestTab::Body:
            RenderBodyTab(pRT, theme, config, x, y, w, h);
            break;
        case ApiRequestTab::Settings:
            RenderSettingsTab(pRT, theme, config, x, y, w, h);
            break;
    }
}

// -----------------------------------------------------------------------------
// Reusable Interactive Key-Value-Description Table
// -----------------------------------------------------------------------------
void ApiTesterView::RenderKeyValueTable(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                        float x, float y, float w, float h,
                                        std::vector<ApiKeyValueItem>& rows,
                                        ApiFocusField keyField, ApiFocusField valField, ApiFocusField descField,
                                        const wchar_t* addBtnLabel) {
    auto& s = m_sessions[m_activeSessionIdx];

    float rowH = config.ScaleF(30.0f);
    float checkColW = config.ScaleF(32.0f);
    float delColW = config.ScaleF(28.0f);
    float usableW = (std::max)(config.ScaleF(200.0f), w - checkColW - delColW);
    float colKeyW = usableW * 0.30f;
    float colValW = usableW * 0.38f;
    float colDescW = usableW - colKeyW - colValW;

    // Table Header
    D2D1_RECT_F rcHeader = D2D1::RectF(x, y, x + w, y + config.ScaleF(24.0f));
    pRT->FillRectangle(rcHeader, theme.brushTabBarBg.Get());
    pRT->DrawRectangle(rcHeader, theme.brushBorderSubtle.Get(), 0.8f);

    float hdrY = y;
    float hdrH = config.ScaleF(24.0f);
    pRT->DrawText(L"Key", 3, theme.fontKeycap.Get(),
                  D2D1::RectF(x + checkColW + config.ScaleF(6.0f), hdrY, x + checkColW + colKeyW, hdrY + hdrH),
                  theme.brushTextMuted.Get());
    pRT->DrawText(L"Value", 5, theme.fontKeycap.Get(),
                  D2D1::RectF(x + checkColW + colKeyW + config.ScaleF(6.0f), hdrY, x + checkColW + colKeyW + colValW, hdrY + hdrH),
                  theme.brushTextMuted.Get());
    pRT->DrawText(L"Description", 11, theme.fontKeycap.Get(),
                  D2D1::RectF(x + checkColW + colKeyW + colValW + config.ScaleF(6.0f), hdrY, x + w - delColW, hdrY + hdrH),
                  theme.brushTextMuted.Get());

    D2D1_RECT_F rcTableArea = D2D1::RectF(x, y + hdrH, x + w, y + h);
    pRT->PushAxisAlignedClip(rcTableArea, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float curY = y + hdrH + config.ScaleF(2.0f) - s.reqScrollY;

    auto drawCellInput = [&](const D2D1_RECT_F& bounds, const std::wstring& text,
                             const wchar_t* placeholder, bool isFocused, bool isEnabled) {
        D2D1_ROUNDED_RECT rrcCell = D2D1::RoundedRect(bounds, 4.0f, 4.0f);
        pRT->FillRoundedRectangle(rrcCell, theme.brushMenuBg.Get());
        pRT->DrawRoundedRectangle(rrcCell, isFocused ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(),
                                  isFocused ? 1.4f : 0.8f);

        D2D1_RECT_F rcTxt = D2D1::RectF(bounds.left + config.ScaleF(8.0f), bounds.top + config.ScaleF(3.0f),
                                        bounds.right - config.ScaleF(6.0f), bounds.bottom - config.ScaleF(3.0f));
        pRT->PushAxisAlignedClip(rcTxt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (isFocused && m_hasSelection) {
            DrawTextSelection(pRT, theme.brushTreeSelected.Get(), theme.dwriteFactory.Get(),
                              theme.fontTab.Get(), text, m_selStart, m_selEnd,
                              rcTxt.left, rcTxt.top, rcTxt.right - rcTxt.left, rcTxt.bottom - rcTxt.top);
        }

        if (text.empty()) {
            pRT->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)),
                          theme.fontHint.Get(), rcTxt, theme.brushTextMuted.Get());
        } else {
            pRT->DrawText(text.c_str(), static_cast<UINT32>(text.length()),
                          theme.fontTab.Get(), rcTxt,
                          isEnabled ? theme.brushTextPrimary.Get() : theme.brushTextMuted.Get());
        }

        // Exact DirectWrite Caret Calculation (White cursor, zero gap)
        if (isFocused && m_showCaret) {
            float exactCaretX = rcTxt.left + MeasureCaretX(theme.dwriteFactory.Get(), theme.fontTab.Get(),
                                                           text, s.cellCaret,
                                                           rcTxt.right - rcTxt.left, rcTxt.bottom - rcTxt.top);
            exactCaretX = (std::min)(exactCaretX, rcTxt.right - 2.0f);
            pRT->DrawLine(D2D1::Point2F(exactCaretX, rcTxt.top + 2.0f),
                          D2D1::Point2F(exactCaretX, rcTxt.bottom - 2.0f),
                          theme.brushTextWhite.Get(), 1.5f);
        }
        pRT->PopAxisAlignedClip();
    };

    for (size_t i = 0; i < rows.size(); ++i) {
        auto& r = rows[i];

        // Checkbox
        r.checkBounds = D2D1::RectF(x + config.ScaleF(8.0f), curY + config.ScaleF(7.0f),
                                    x + config.ScaleF(24.0f), curY + config.ScaleF(23.0f));
        D2D1_ROUNDED_RECT rrcCk = D2D1::RoundedRect(r.checkBounds, 3.5f, 3.5f);
        if (r.enabled) {
            pRT->FillRoundedRectangle(rrcCk, theme.brushBrandAccent.Get());
            pRT->DrawText(L"✓", 1, theme.fontKeycap.Get(), r.checkBounds, theme.brushTextWhite.Get());
        } else {
            pRT->FillRoundedRectangle(rrcCk, theme.brushMenuBg.Get());
            pRT->DrawRoundedRectangle(rrcCk, theme.brushBorderSubtle.Get(), 1.0f);
        }

        // Key Cell
        float kx = x + checkColW;
        r.keyBounds = D2D1::RectF(kx, curY + config.ScaleF(2.0f), kx + colKeyW - config.ScaleF(4.0f), curY + rowH - config.ScaleF(2.0f));
        bool isKeyFoc = (m_focusedField == keyField && m_focusedRowIndex == i);
        drawCellInput(r.keyBounds, r.key, L"Key", isKeyFoc, r.enabled);

        // Value Cell
        float vx = kx + colKeyW;
        r.valBounds = D2D1::RectF(vx, curY + config.ScaleF(2.0f), vx + colValW - config.ScaleF(4.0f), curY + rowH - config.ScaleF(2.0f));
        bool isValFoc = (m_focusedField == valField && m_focusedRowIndex == i);
        drawCellInput(r.valBounds, r.value, L"Value", isValFoc, r.enabled);

        // Description Cell
        float dx = vx + colValW;
        r.descBounds = D2D1::RectF(dx, curY + config.ScaleF(2.0f), dx + colDescW - config.ScaleF(4.0f), curY + rowH - config.ScaleF(2.0f));
        bool isDescFoc = (m_focusedField == descField && m_focusedRowIndex == i);
        drawCellInput(r.descBounds, r.description, L"Description", isDescFoc, r.enabled);

        // Delete button '✕'
        r.deleteBounds = D2D1::RectF(x + w - delColW, curY + config.ScaleF(2.0f), x + w, curY + rowH - config.ScaleF(2.0f));
        bool delHov = PtInRectF(m_mousePt, r.deleteBounds);
        if (delHov) {
            D2D1_ROUNDED_RECT rrcDel = D2D1::RoundedRect(r.deleteBounds, 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrcDel, theme.brushItemHover.Get());
        }
        pRT->DrawText(L"✕", 1, theme.fontKeycap.Get(), r.deleteBounds,
                      delHov && theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushTextMuted.Get());

        curY += rowH;
    }

    // Add Row button
    m_rcAddRowBtn = D2D1::RectF(x, curY + config.ScaleF(6.0f), x + config.ScaleF(116.0f), curY + config.ScaleF(32.0f));
    D2D1_ROUNDED_RECT rrcAdd = D2D1::RoundedRect(m_rcAddRowBtn, 5.0f, 5.0f);
    bool addHov = PtInRectF(m_mousePt, m_rcAddRowBtn);
    pRT->FillRoundedRectangle(rrcAdd, addHov ? theme.brushItemHover.Get() : theme.brushTabBarBg.Get());
    pRT->DrawRoundedRectangle(rrcAdd, addHov ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(), 1.0f);
    pRT->DrawText(addBtnLabel, static_cast<UINT32>(wcslen(addBtnLabel)),
                  theme.fontButtonCenter.Get(), m_rcAddRowBtn,
                  addHov ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get());

    pRT->PopAxisAlignedClip();
}

// -----------------------------------------------------------------------------
// Authorization Tab
// -----------------------------------------------------------------------------
void ApiTesterView::RenderAuthTab(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  float x, float y, float w, float h) {
    auto& s = m_sessions[m_activeSessionIdx];
    m_rcAuthField1 = {};
    m_rcAuthField2 = {};
    m_rcApiKeyHeaderBtn = {};
    m_rcApiKeyQueryBtn = {};

    float curY = y + config.ScaleF(6.0f);
    pRT->DrawText(L"Auth Type:", 10, theme.fontKeycap.Get(),
                  D2D1::RectF(x, curY, x + config.ScaleF(95.0f), curY + config.ScaleF(28.0f)),
                  theme.brushTextSecondary.Get());

    m_rcAuthTypeBtn = D2D1::RectF(x + config.ScaleF(100.0f), curY, x + config.ScaleF(250.0f), curY + config.ScaleF(28.0f));
    D2D1_ROUNDED_RECT rrcAuth = D2D1::RoundedRect(m_rcAuthTypeBtn, 5.0f, 5.0f);
    bool authBtnHov = PtInRectF(m_mousePt, m_rcAuthTypeBtn) || m_isAuthDropdownOpen;
    pRT->FillRoundedRectangle(rrcAuth, authBtnHov ? theme.brushItemHover.Get() : theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrcAuth, authBtnHov ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(), 1.0f);

    std::wstring typeStr = L"No Auth  ▼";
    if (s.authType == ApiAuthType::BearerToken) typeStr = L"Bearer Token  ▼";
    else if (s.authType == ApiAuthType::ApiKey) typeStr = L"API Key  ▼";
    else if (s.authType == ApiAuthType::BasicAuth) typeStr = L"Basic Auth  ▼";

    pRT->DrawText(typeStr.c_str(), static_cast<UINT32>(typeStr.length()),
                  theme.fontButtonCenter.Get(), m_rcAuthTypeBtn, theme.brushTextPrimary.Get());

    curY += config.ScaleF(40.0f);

    auto drawInputBox = [&](const D2D1_RECT_F& rcBox, const std::wstring& val,
                             const wchar_t* hint, bool isFocused, bool maskPassword = false) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcBox, 5.0f, 5.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushMenuBg.Get());
        pRT->DrawRoundedRectangle(rrc, isFocused ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(),
                                  isFocused ? 1.5f : 1.0f);

        D2D1_RECT_F rcTxt = D2D1::RectF(rcBox.left + config.ScaleF(10.0f), rcBox.top + config.ScaleF(5.0f),
                                        rcBox.right - config.ScaleF(10.0f), rcBox.bottom - config.ScaleF(5.0f));
        pRT->PushAxisAlignedClip(rcTxt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        std::wstring disp = maskPassword ? std::wstring(val.length(), L'•') : val;

        if (isFocused && m_hasSelection) {
            DrawTextSelection(pRT, theme.brushTreeSelected.Get(), theme.dwriteFactory.Get(),
                              theme.fontTab.Get(), disp, m_selStart, m_selEnd,
                              rcTxt.left, rcTxt.top, rcTxt.right - rcTxt.left, rcTxt.bottom - rcTxt.top);
        }

        if (disp.empty()) {
            pRT->DrawText(hint, static_cast<UINT32>(wcslen(hint)), theme.fontHint.Get(), rcTxt, theme.brushTextMuted.Get());
        } else {
            pRT->DrawText(disp.c_str(), static_cast<UINT32>(disp.length()), theme.fontTab.Get(), rcTxt, theme.brushTextPrimary.Get());
        }

        if (isFocused && m_showCaret) {
            float exactCaretX = rcTxt.left + MeasureCaretX(theme.dwriteFactory.Get(), theme.fontTab.Get(),
                                                           disp, s.cellCaret,
                                                           rcTxt.right - rcTxt.left, rcTxt.bottom - rcTxt.top);
            exactCaretX = (std::min)(exactCaretX, rcTxt.right - 2.0f);
            pRT->DrawLine(D2D1::Point2F(exactCaretX, rcTxt.top + 2.0f),
                          D2D1::Point2F(exactCaretX, rcTxt.bottom - 2.0f),
                          theme.brushTextWhite.Get(), 1.5f);
        }
        pRT->PopAxisAlignedClip();
    };

    if (s.authType == ApiAuthType::BearerToken) {
        pRT->DrawText(L"Token:", 6, theme.fontKeycap.Get(),
                      D2D1::RectF(x, curY, x + config.ScaleF(95.0f), curY + config.ScaleF(30.0f)),
                      theme.brushTextSecondary.Get());
        m_rcAuthField1 = D2D1::RectF(x + config.ScaleF(100.0f), curY, x + w - config.ScaleF(20.0f), curY + config.ScaleF(30.0f));
        drawInputBox(m_rcAuthField1, s.bearerToken, L"Enter Bearer Token", m_focusedField == ApiFocusField::AuthToken);
    } else if (s.authType == ApiAuthType::ApiKey) {
        pRT->DrawText(L"Key:", 4, theme.fontKeycap.Get(),
                      D2D1::RectF(x, curY, x + config.ScaleF(95.0f), curY + config.ScaleF(30.0f)),
                      theme.brushTextSecondary.Get());
        m_rcAuthField1 = D2D1::RectF(x + config.ScaleF(100.0f), curY, x + config.ScaleF(360.0f), curY + config.ScaleF(30.0f));
        drawInputBox(m_rcAuthField1, s.apiKeyName, L"Header / Param Name (e.g. X-API-Key)", m_focusedField == ApiFocusField::ApiKeyName);

        pRT->DrawText(L"Add to:", 7, theme.fontKeycap.Get(),
                      D2D1::RectF(x + config.ScaleF(380.0f), curY, x + config.ScaleF(440.0f), curY + config.ScaleF(30.0f)),
                      theme.brushTextSecondary.Get());

        m_rcApiKeyHeaderBtn = D2D1::RectF(x + config.ScaleF(445.0f), curY + 2.0f, x + config.ScaleF(525.0f), curY + config.ScaleF(28.0f));
        m_rcApiKeyQueryBtn  = D2D1::RectF(x + config.ScaleF(532.0f), curY + 2.0f, x + config.ScaleF(635.0f), curY + config.ScaleF(28.0f));

        D2D1_ROUNDED_RECT rrcH = D2D1::RoundedRect(m_rcApiKeyHeaderBtn, 4.0f, 4.0f);
        pRT->FillRoundedRectangle(rrcH, s.apiKeyInHeader ? theme.brushTabActiveBg.Get() : theme.brushTabBarBg.Get());
        pRT->DrawRoundedRectangle(rrcH, s.apiKeyInHeader ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(), 1.0f);
        pRT->DrawText(L"Header", 6, theme.fontButtonCenter.Get(), m_rcApiKeyHeaderBtn,
                      s.apiKeyInHeader ? theme.brushTextWhite.Get() : theme.brushTextMuted.Get());

        D2D1_ROUNDED_RECT rrcQ = D2D1::RoundedRect(m_rcApiKeyQueryBtn, 4.0f, 4.0f);
        pRT->FillRoundedRectangle(rrcQ, !s.apiKeyInHeader ? theme.brushTabActiveBg.Get() : theme.brushTabBarBg.Get());
        pRT->DrawRoundedRectangle(rrcQ, !s.apiKeyInHeader ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(), 1.0f);
        pRT->DrawText(L"Query Params", 12, theme.fontButtonCenter.Get(), m_rcApiKeyQueryBtn,
                      !s.apiKeyInHeader ? theme.brushTextWhite.Get() : theme.brushTextMuted.Get());

        curY += config.ScaleF(38.0f);
        pRT->DrawText(L"Value:", 6, theme.fontKeycap.Get(),
                      D2D1::RectF(x, curY, x + config.ScaleF(95.0f), curY + config.ScaleF(30.0f)),
                      theme.brushTextSecondary.Get());
        m_rcAuthField2 = D2D1::RectF(x + config.ScaleF(100.0f), curY, x + w - config.ScaleF(20.0f), curY + config.ScaleF(30.0f));
        drawInputBox(m_rcAuthField2, s.apiKeyValue, L"Enter API Key Value", m_focusedField == ApiFocusField::ApiKeyValue);
    } else if (s.authType == ApiAuthType::BasicAuth) {
        pRT->DrawText(L"Username:", 9, theme.fontKeycap.Get(),
                      D2D1::RectF(x, curY, x + config.ScaleF(95.0f), curY + config.ScaleF(30.0f)),
                      theme.brushTextSecondary.Get());
        m_rcAuthField1 = D2D1::RectF(x + config.ScaleF(100.0f), curY, x + config.ScaleF(380.0f), curY + config.ScaleF(30.0f));
        drawInputBox(m_rcAuthField1, s.basicUser, L"Username", m_focusedField == ApiFocusField::AuthUser);

        curY += config.ScaleF(38.0f);
        pRT->DrawText(L"Password:", 9, theme.fontKeycap.Get(),
                      D2D1::RectF(x, curY, x + config.ScaleF(95.0f), curY + config.ScaleF(30.0f)),
                      theme.brushTextSecondary.Get());
        m_rcAuthField2 = D2D1::RectF(x + config.ScaleF(100.0f), curY, x + config.ScaleF(380.0f), curY + config.ScaleF(30.0f));
        drawInputBox(m_rcAuthField2, s.basicPass, L"Password", m_focusedField == ApiFocusField::AuthPass, true);
    } else {
        pRT->DrawText(L"This request does not use any authorization.", 44,
                      theme.fontHint.Get(), D2D1::RectF(x, curY, x + w, curY + config.ScaleF(30.0f)),
                      theme.brushTextMuted.Get());
    }
}

void ApiTesterView::RenderAuthDropdown(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    float dropW = config.ScaleF(160.0f);
    float itemH = config.ScaleF(28.0f);
    float dropH = itemH * 4.0f + config.ScaleF(8.0f);
    float dropX = m_rcAuthTypeBtn.left;
    float dropY = m_rcAuthTypeBtn.bottom + config.ScaleF(4.0f);

    m_rcAuthDropdown = D2D1::RectF(dropX, dropY, dropX + dropW, dropY + dropH);
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_rcAuthDropdown, 6.0f, 6.0f);
    pRT->FillRoundedRectangle(rrc, theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrc, theme.brushMenuBorder.Get(), 1.0f);

    const wchar_t* types[] = { L"No Auth", L"Bearer Token", L"API Key", L"Basic Auth" };
    float curY = dropY + config.ScaleF(4.0f);
    for (int i = 0; i < 4; ++i) {
        D2D1_RECT_F rcRow = D2D1::RectF(dropX + config.ScaleF(4.0f), curY, dropX + dropW - config.ScaleF(4.0f), curY + itemH);
        if (PtInRectF(m_mousePt, rcRow)) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRow, 4.0f, 4.0f), theme.brushItemHover.Get());
        }
        D2D1_RECT_F rcItem = D2D1::RectF(dropX + config.ScaleF(12.0f), curY, dropX + dropW - config.ScaleF(8.0f), curY + itemH);
        pRT->DrawText(types[i], static_cast<UINT32>(wcslen(types[i])),
                      theme.fontMenuItem.Get(), rcItem, theme.brushTextPrimary.Get());
        curY += itemH;
    }
}

// -----------------------------------------------------------------------------
// Body Tab (none, raw (JSON), form-data, x-www-form-urlencoded)
// -----------------------------------------------------------------------------
void ApiTesterView::RenderBodyTab(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  float x, float y, float w, float h) {
    auto& s = m_sessions[m_activeSessionIdx];
    m_bodyFormatTabs.clear();
    m_rcBodyEditor = {};

    float topBarH = config.ScaleF(26.0f);
    float curX = x;

    struct BodyFmt { ApiBodyType type; const wchar_t* name; };
    BodyFmt fmts[] = {
        { ApiBodyType::None,        L"none" },
        { ApiBodyType::RawJson,     L"raw (JSON)" },
        { ApiBodyType::FormData,    L"form-data" },
        { ApiBodyType::UrlEncoded,  L"x-www-form-urlencoded" }
    };

    for (const auto& f : fmts) {
        bool isSel = (s.bodyType == f.type);
        float itemW = static_cast<float>(wcslen(f.name)) * config.ScaleF(7.5f) + config.ScaleF(20.0f);
        D2D1_RECT_F rcPill = D2D1::RectF(curX, y, curX + itemW, y + topBarH);
        m_bodyFormatTabs.push_back({ f.type, rcPill });

        bool isHov = PtInRectF(m_mousePt, rcPill);
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcPill, 4.0f, 4.0f);

        if (isSel) {
            pRT->FillRoundedRectangle(rrc, theme.brushTabBarBg.Get());
            pRT->DrawRoundedRectangle(rrc, theme.brushBrandAccent.Get(), 1.2f);
        } else if (isHov) {
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
            pRT->DrawRoundedRectangle(rrc, theme.brushBorderSubtle.Get(), 1.0f);
        }
        pRT->DrawText(f.name, static_cast<UINT32>(wcslen(f.name)),
                      theme.fontKeycap.Get(), rcPill,
                      (isSel || isHov) ? theme.brushTextWhite.Get() : theme.brushTextMuted.Get());

        curX += itemW + config.ScaleF(6.0f);
    }

    float editorY = y + topBarH + config.ScaleF(6.0f);
    float editorH = (std::max)(config.ScaleF(50.0f), h - topBarH - config.ScaleF(8.0f));

    if (s.bodyType == ApiBodyType::None) {
        D2D1_RECT_F rcNone = D2D1::RectF(x, editorY + config.ScaleF(10.0f), x + w, editorY + editorH);
        pRT->DrawText(L"This request does not have a body.", 33, theme.fontHint.Get(), rcNone, theme.brushTextMuted.Get());
        return;
    }

    if (s.bodyType == ApiBodyType::FormData) {
        RenderKeyValueTable(pRT, theme, config, x, editorY, w, editorH, s.formData,
                            ApiFocusField::FormKey, ApiFocusField::FormVal, ApiFocusField::FormDesc,
                            L"+ Add Field");
        return;
    }

    if (s.bodyType == ApiBodyType::UrlEncoded) {
        RenderKeyValueTable(pRT, theme, config, x, editorY, w, editorH, s.urlEncoded,
                            ApiFocusField::UrlEncKey, ApiFocusField::UrlEncVal, ApiFocusField::UrlEncDesc,
                            L"+ Add Field");
        return;
    }

    // Raw JSON Code Editor Container
    m_rcBodyEditor = D2D1::RectF(x, editorY, x + w, editorY + editorH);
    D2D1_ROUNDED_RECT rrcEd = D2D1::RoundedRect(m_rcBodyEditor, 6.0f, 6.0f);
    bool isBodyFocused = (m_focusedField == ApiFocusField::Body);
    pRT->FillRoundedRectangle(rrcEd, theme.brushCanvasBg.Get());
    pRT->DrawRoundedRectangle(rrcEd, isBodyFocused ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(),
                              isBodyFocused ? 1.5f : 1.0f);

    float gutterW = config.ScaleF(38.0f);
    D2D1_RECT_F rcGutter = D2D1::RectF(x, editorY, x + gutterW, editorY + editorH);
    pRT->FillRectangle(rcGutter, theme.brushTabBarBg.Get());
    pRT->DrawLine(D2D1::Point2F(x + gutterW, editorY), D2D1::Point2F(x + gutterW, editorY + editorH),
                  theme.brushBorderSubtle.Get(), 1.0f);

    float lineH = config.ScaleF(19.0f);
    float textX = x + gutterW + config.ScaleF(10.0f);
    float lineY = editorY + config.ScaleF(6.0f) - s.reqScrollY;

    pRT->PushAxisAlignedClip(m_rcBodyEditor, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (s.bodyText.empty() && !isBodyFocused) {
        D2D1_RECT_F rcNum = D2D1::RectF(x, lineY, x + gutterW - config.ScaleF(6.0f), lineY + lineH);
        pRT->DrawText(L"1", 1, theme.fontLineNumber.Get(), rcNum, theme.brushLineNumber.Get());
        D2D1_RECT_F rcHint = D2D1::RectF(textX, lineY, x + w - config.ScaleF(10.0f), lineY + lineH);
        pRT->DrawText(L"Enter raw JSON payload (e.g. {\"key\": \"value\"})", 46,
                      theme.fontHint.Get(), rcHint, theme.brushTextMuted.Get());
    } else {
        std::vector<std::wstring> lines;
        size_t start = 0;
        while (start <= s.bodyText.length()) {
            size_t nl = s.bodyText.find(L'\n', start);
            if (nl == std::wstring::npos) {
                lines.push_back(s.bodyText.substr(start));
                break;
            }
            lines.push_back(s.bodyText.substr(start, nl - start));
            start = nl + 1;
        }
        if (lines.empty()) lines.push_back(L"");

        size_t caretPos = (std::min)(s.bodyCaret, s.bodyText.length());
        size_t caretLine = 0;
        size_t caretCol = 0;
        size_t accum = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (caretPos <= accum + lines[i].length()) {
                caretLine = i;
                caretCol = caretPos - accum;
                break;
            }
            accum += lines[i].length() + 1;
        }

        size_t selMin = (std::min)(m_selStart, m_selEnd);
        size_t selMax = (std::max)(m_selStart, m_selEnd);
        size_t lineStartPos = 0;

        for (size_t i = 0; i < lines.size(); ++i) {
            size_t lineEndPos = lineStartPos + lines[i].length();

            if (lineY + lineH > editorY && lineY < editorY + editorH) {
                std::wstring numStr = std::to_wstring(i + 1);
                D2D1_RECT_F rcNum = D2D1::RectF(x, lineY, x + gutterW - config.ScaleF(6.0f), lineY + lineH);
                pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()),
                              theme.fontLineNumber.Get(), rcNum, theme.brushLineNumber.Get());

                D2D1_RECT_F rcCode = D2D1::RectF(textX, lineY, x + w - config.ScaleF(10.0f), lineY + lineH);

                // Selection highlight across this line
                if (isBodyFocused && m_hasSelection && selMax > lineStartPos && selMin < lineEndPos) {
                    size_t sInLine = (selMin > lineStartPos) ? (selMin - lineStartPos) : 0;
                    size_t eInLine = (selMax < lineEndPos) ? (selMax - lineStartPos) : lines[i].length();
                    DrawTextSelection(pRT, theme.brushTreeSelected.Get(), theme.dwriteFactory.Get(),
                                      theme.fontCode.Get(), lines[i], sInLine, eInLine,
                                      rcCode.left, rcCode.top, rcCode.right - rcCode.left, lineH);
                }

                if (!lines[i].empty()) {
                    pRT->DrawText(lines[i].c_str(), static_cast<UINT32>(lines[i].length()),
                                  theme.fontCode.Get(), rcCode, theme.brushTextPrimary.Get());
                }

                // Exact DirectWrite Caret Calculation (White cursor, zero gap)
                if (isBodyFocused && m_showCaret && i == caretLine) {
                    float exactCaretX = textX + MeasureCaretX(theme.dwriteFactory.Get(), theme.fontCode.Get(),
                                                             lines[i], caretCol,
                                                             rcCode.right - rcCode.left, lineH);
                    pRT->DrawLine(D2D1::Point2F(exactCaretX, lineY + 2.0f),
                                  D2D1::Point2F(exactCaretX, lineY + lineH - 2.0f),
                                  theme.brushTextWhite.Get(), 1.5f);
                }
            }
            lineStartPos = lineEndPos + 1;
            lineY += lineH;
        }
    }

    pRT->PopAxisAlignedClip();
}

// -----------------------------------------------------------------------------
// Settings Tab
// -----------------------------------------------------------------------------
void ApiTesterView::RenderSettingsTab(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                      float x, float y, float w, float h) {
    auto& s = m_sessions[m_activeSessionIdx];

    float curY = y + config.ScaleF(6.0f);
    float rowH = config.ScaleF(34.0f);
    float labelW = config.ScaleF(250.0f);
    float btnW = config.ScaleF(96.0f);
    float btnH = config.ScaleF(26.0f);

    // 1. SSL Certificate Verification
    pRT->DrawText(L"SSL Certificate Verification:", 29, theme.fontTab.Get(),
                  D2D1::RectF(x, curY, x + labelW, curY + btnH), theme.brushTextPrimary.Get());
    m_rcSslToggleBtn = D2D1::RectF(x + labelW, curY, x + labelW + btnW, curY + btnH);
    D2D1_ROUNDED_RECT rrcSsl = D2D1::RoundedRect(m_rcSslToggleBtn, 5.0f, 5.0f);
    pRT->FillRoundedRectangle(rrcSsl, s.verifySsl ? theme.brushBrandAccent.Get() : theme.brushTabBarBg.Get());
    pRT->DrawRoundedRectangle(rrcSsl, s.verifySsl ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(), 1.0f);
    pRT->DrawText(s.verifySsl ? L"Enabled" : L"Disabled", s.verifySsl ? 7 : 8,
                  theme.fontButtonCenter.Get(), m_rcSslToggleBtn, theme.brushTextWhite.Get());

    curY += rowH;

    // 2. Follow Redirects
    pRT->DrawText(L"Automatically Follow Redirects:", 31, theme.fontTab.Get(),
                  D2D1::RectF(x, curY, x + labelW, curY + btnH), theme.brushTextPrimary.Get());
    m_rcRedirectToggleBtn = D2D1::RectF(x + labelW, curY, x + labelW + btnW, curY + btnH);
    D2D1_ROUNDED_RECT rrcRed = D2D1::RoundedRect(m_rcRedirectToggleBtn, 5.0f, 5.0f);
    pRT->FillRoundedRectangle(rrcRed, s.followRedirects ? theme.brushBrandAccent.Get() : theme.brushTabBarBg.Get());
    pRT->DrawRoundedRectangle(rrcRed, s.followRedirects ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(), 1.0f);
    pRT->DrawText(s.followRedirects ? L"Enabled" : L"Disabled", s.followRedirects ? 7 : 8,
                  theme.fontButtonCenter.Get(), m_rcRedirectToggleBtn, theme.brushTextWhite.Get());

    curY += rowH;

    // 3. Request Timeout (ms)
    pRT->DrawText(L"Request Timeout (ms):", 21, theme.fontTab.Get(),
                  D2D1::RectF(x, curY, x + labelW, curY + btnH), theme.brushTextPrimary.Get());
    m_rcTimeoutInput = D2D1::RectF(x + labelW, curY, x + labelW + config.ScaleF(120.0f), curY + btnH);
    bool isTimeFoc = (m_focusedField == ApiFocusField::SettingsTimeout);
    D2D1_ROUNDED_RECT rrcTime = D2D1::RoundedRect(m_rcTimeoutInput, 5.0f, 5.0f);
    pRT->FillRoundedRectangle(rrcTime, theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrcTime, isTimeFoc ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(),
                              isTimeFoc ? 1.5f : 1.0f);
    D2D1_RECT_F rcTimeTxt = D2D1::RectF(m_rcTimeoutInput.left + config.ScaleF(8.0f), m_rcTimeoutInput.top + 3.0f,
                                        m_rcTimeoutInput.right - config.ScaleF(8.0f), m_rcTimeoutInput.bottom - 3.0f);
    pRT->DrawText(s.timeoutMsStr.c_str(), static_cast<UINT32>(s.timeoutMsStr.length()),
                  theme.fontTab.Get(), rcTimeTxt, theme.brushTextPrimary.Get());
    if (isTimeFoc && m_showCaret) {
        float exactCaretX = rcTimeTxt.left + MeasureCaretX(theme.dwriteFactory.Get(), theme.fontTab.Get(),
                                                           s.timeoutMsStr, s.cellCaret,
                                                           rcTimeTxt.right - rcTimeTxt.left, rcTimeTxt.bottom - rcTimeTxt.top);
        pRT->DrawLine(D2D1::Point2F(exactCaretX, rcTimeTxt.top + 2.0f),
                      D2D1::Point2F(exactCaretX, rcTimeTxt.bottom - 2.0f),
                      theme.brushTextWhite.Get(), 1.5f);
    }
}

// -----------------------------------------------------------------------------
// Response Section (Lower Container)
// -----------------------------------------------------------------------------
void ApiTesterView::RenderResponseSection(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                          float x, float y, float w, float h) {
    auto& s = m_sessions[m_activeSessionIdx];

    float topBarH = config.ScaleF(30.0f);
    float clearW = config.ScaleF(72.0f);
    float copyW  = config.ScaleF(132.0f);
    float btnGap = config.ScaleF(8.0f);

    // 1. Response Metrics / Status Bar on left
    if (s.response.hasResponse) {
        int code = s.response.statusCode;
        D2D1_COLOR_F statusColor = (code >= 200 && code < 300) ? D2D1::ColorF(0.20f, 0.85f, 0.40f, 1.0f) :
                                   (code >= 300 && code < 400) ? D2D1::ColorF(0.95f, 0.70f, 0.15f, 1.0f) :
                                   (code >= 400 && code < 500) ? D2D1::ColorF(0.95f, 0.40f, 0.15f, 1.0f) :
                                                                 D2D1::ColorF(0.95f, 0.25f, 0.25f, 1.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> statBrush;
        pRT->CreateSolidColorBrush(statusColor, &statBrush);

        std::wstring statStr = std::to_wstring(code) + L" " + s.response.statusText;
        float statW = static_cast<float>(statStr.length()) * config.ScaleF(7.5f) + config.ScaleF(24.0f);
        D2D1_RECT_F rcStat = D2D1::RectF(x, y, x + statW, y + topBarH - config.ScaleF(2.0f));
        D2D1_ROUNDED_RECT rrcStat = D2D1::RoundedRect(rcStat, 12.0f, 12.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> statBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(statusColor.r, statusColor.g, statusColor.b, 0.16f), &statBg);
        if (statBg) pRT->FillRoundedRectangle(rrcStat, statBg.Get());
        if (statBrush) pRT->DrawRoundedRectangle(rrcStat, statBrush.Get(), 1.0f);
        if (statBrush) pRT->DrawText(statStr.c_str(), static_cast<UINT32>(statStr.length()),
                                     theme.fontButtonCenter.Get(), rcStat, statBrush.Get());

        wchar_t szTime[32];
        if (s.response.timeMs >= 1000.0) {
            swprintf_s(szTime, L"%.2f s", s.response.timeMs / 1000.0);
        } else {
            swprintf_s(szTime, L"%.0f ms", s.response.timeMs);
        }
        D2D1_RECT_F rcTime = D2D1::RectF(x + statW + config.ScaleF(14.0f), y, x + statW + config.ScaleF(100.0f), y + topBarH);
        pRT->DrawText(szTime, static_cast<UINT32>(wcslen(szTime)), theme.fontTab.Get(), rcTime, theme.brushTextSecondary.Get());

        wchar_t szSize[32];
        if (s.response.sizeBytes >= 1024 * 1024) {
            swprintf_s(szSize, L"%.2f MB", static_cast<double>(s.response.sizeBytes) / (1024.0 * 1024.0));
        } else {
            swprintf_s(szSize, L"%.2f KB", static_cast<double>(s.response.sizeBytes) / 1024.0);
        }
        D2D1_RECT_F rcSize = D2D1::RectF(rcTime.right + config.ScaleF(6.0f), y, rcTime.right + config.ScaleF(110.0f), y + topBarH);
        pRT->DrawText(szSize, static_cast<UINT32>(wcslen(szSize)), theme.fontTab.Get(), rcSize, theme.brushTextSecondary.Get());
    } else if (s.response.isError) {
        D2D1_RECT_F rcErr = D2D1::RectF(x, y, x + w - (clearW + copyW + btnGap * 2.0f), y + topBarH);
        pRT->PushAxisAlignedClip(rcErr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(s.response.errorMessage.c_str(), static_cast<UINT32>(s.response.errorMessage.length()),
                      theme.fontTab.Get(), rcErr, theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushTextWhite.Get());
        pRT->PopAxisAlignedClip();
    } else {
        D2D1_RECT_F rcWait = D2D1::RectF(x, y, x + config.ScaleF(200.0f), y + topBarH);
        pRT->DrawText(L"Response", 8, theme.fontTab.Get(), rcWait, theme.brushTextSecondary.Get());
    }

    // 2. Copy Response & Clear Buttons on top right
    float btnH = topBarH - config.ScaleF(2.0f);
    m_rcClearBtn = D2D1::RectF(x + w - clearW, y, x + w, y + btnH);
    m_rcCopyBtn  = D2D1::RectF(m_rcClearBtn.left - btnGap - copyW, y, m_rcClearBtn.left - btnGap, y + btnH);

    bool clearHov = PtInRectF(m_mousePt, m_rcClearBtn);
    bool copyHov  = PtInRectF(m_mousePt, m_rcCopyBtn);

    {
        D2D1_ROUNDED_RECT rrcClear = D2D1::RoundedRect(m_rcClearBtn, 5.0f, 5.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> clearBg;
        pRT->CreateSolidColorBrush(clearHov
            ? D2D1::ColorF(0.85f, 0.22f, 0.25f, 0.30f)
            : (theme.IsDarkMode() ? D2D1::ColorF(0.22f, 0.16f, 0.20f, 1.0f) : D2D1::ColorF(0.92f, 0.88f, 0.90f, 1.0f)), &clearBg);
        if (clearBg) pRT->FillRoundedRectangle(rrcClear, clearBg.Get());
        pRT->DrawRoundedRectangle(rrcClear, (clearHov && theme.brushIconRed) ? theme.brushIconRed.Get() : theme.brushBorderSubtle.Get(), 1.0f);
        pRT->PushAxisAlignedClip(m_rcClearBtn, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(L"Clear", 5, theme.fontButtonCenter.Get(), m_rcClearBtn,
                      clearHov ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
        pRT->PopAxisAlignedClip();
    }

    {
        D2D1_ROUNDED_RECT rrcCopy = D2D1::RoundedRect(m_rcCopyBtn, 5.0f, 5.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> copyBg;
        pRT->CreateSolidColorBrush(copyHov
            ? D2D1::ColorF(0.24f, 0.25f, 0.30f, 1.0f)
            : (theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.19f, 0.23f, 1.0f) : D2D1::ColorF(0.90f, 0.91f, 0.94f, 1.0f)), &copyBg);
        if (copyBg) pRT->FillRoundedRectangle(rrcCopy, copyBg.Get());
        pRT->DrawRoundedRectangle(rrcCopy, copyHov ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(), 1.0f);

        pRT->PushAxisAlignedClip(m_rcCopyBtn, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float icX = m_rcCopyBtn.left + config.ScaleF(10.0f);
        float icY = (m_rcCopyBtn.top + m_rcCopyBtn.bottom) * 0.5f - config.ScaleF(5.5f);
        float icS = config.ScaleF(8.0f);
        ID2D1Brush* icBrush = copyHov ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get();
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(icX, icY, icX + icS, icY + icS), 1.5f, 1.5f), icBrush, 1.1f);
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(icX + 3.0f, icY + 3.0f, icX + icS + 3.0f, icY + icS + 3.0f), 1.5f, 1.5f), icBrush, 1.1f);

        D2D1_RECT_F rcCopyTxt = D2D1::RectF(icX + icS + config.ScaleF(4.0f), m_rcCopyBtn.top, m_rcCopyBtn.right - config.ScaleF(4.0f), m_rcCopyBtn.bottom);
        pRT->DrawText(L"Copy Response", 13, theme.fontButtonCenter.Get(), rcCopyTxt,
                      copyHov ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
        pRT->PopAxisAlignedClip();
    }

    // 3. Response Sub-tabs: Body, Headers, Cookies
    float navY = y + topBarH + config.ScaleF(4.0f);
    m_respNavTabs.clear();

    std::wstring hdrTabTitle = s.response.headers.empty()
        ? L"Headers"
        : (L"Headers (" + std::to_wstring(s.response.headers.size()) + L")");
    std::wstring ckTabTitle = s.response.cookies.empty()
        ? L"Cookies"
        : (L"Cookies (" + std::to_wstring(s.response.cookies.size()) + L")");

    struct RespTabInfo { ApiResponseTab tab; std::wstring name; };
    RespTabInfo rTabs[] = {
        { ApiResponseTab::Body,    L"Body" },
        { ApiResponseTab::Headers, hdrTabTitle },
        { ApiResponseTab::Cookies, ckTabTitle }
    };

    float rx = x;
    for (const auto& rt : rTabs) {
        bool isAct = (s.activeRespTab == rt.tab);
        float tw = static_cast<float>(rt.name.length()) * config.ScaleF(7.8f) + config.ScaleF(20.0f);
        D2D1_RECT_F rc = D2D1::RectF(rx, navY, rx + tw, navY + config.ScaleF(24.0f));
        m_respNavTabs.push_back({ rt.tab, rc });

        bool isHov = PtInRectF(m_mousePt, rc);
        pRT->DrawText(rt.name.c_str(), static_cast<UINT32>(rt.name.length()),
                      theme.fontTab.Get(), rc,
                      (isAct || isHov) ? theme.brushTextWhite.Get() : theme.brushTextMuted.Get());

        if (isAct) {
            pRT->DrawLine(D2D1::Point2F(rx, navY + config.ScaleF(24.0f) - 1.0f),
                          D2D1::Point2F(rx + tw - config.ScaleF(6.0f), navY + config.ScaleF(24.0f) - 1.0f),
                          theme.brushBrandAccent.Get(), 2.0f);
        }
        rx += tw + config.ScaleF(6.0f);
    }

    // 4. Response Content Viewer
    float contentY = navY + config.ScaleF(28.0f);
    float contentH = (std::max)(config.ScaleF(40.0f), (y + h) - contentY);
    D2D1_RECT_F rcContent = D2D1::RectF(x, contentY, x + w, contentY + contentH);
    D2D1_ROUNDED_RECT rrcContent = D2D1::RoundedRect(rcContent, 6.0f, 6.0f);
    pRT->FillRoundedRectangle(rrcContent, theme.brushCanvasBg.Get());
    pRT->DrawRoundedRectangle(rrcContent, theme.brushBorderSubtle.Get(), 1.0f);

    if (!s.response.hasResponse) {
        float iconBoxSize = config.ScaleF(48.0f);
        float midX = (rcContent.left + rcContent.right) * 0.5f;
        float midY = (rcContent.top + rcContent.bottom) * 0.5f - config.ScaleF(16.0f);
        m_icons.DrawApi(pRT, D2D1::RectF(midX - iconBoxSize * 0.5f, midY - iconBoxSize * 0.5f,
                                         midX + iconBoxSize * 0.5f, midY + iconBoxSize * 0.5f),
                        theme.brushBrandAccent.Get(), 1.8f);

        std::wstring emptyMsg = s.isSending
            ? L"Sending HTTP request..."
            : (s.response.isError ? s.response.errorMessage : L"Enter a URL and click Send to test your API endpoint");
        float msgW = (std::min)(w - 40.0f, static_cast<float>(emptyMsg.length()) * config.ScaleF(7.6f) + 20.0f);
        D2D1_RECT_F rcHint = D2D1::RectF(midX - msgW * 0.5f, midY + iconBoxSize * 0.5f + 10.0f,
                                         midX + msgW * 0.5f, midY + iconBoxSize * 0.5f + 40.0f);
        pRT->DrawText(emptyMsg.c_str(), static_cast<UINT32>(emptyMsg.length()),
                      theme.fontHint.Get(), rcHint,
                      (s.response.isError && theme.brushIconRed) ? theme.brushIconRed.Get() : theme.brushTextMuted.Get());
        return;
    }

    pRT->PushAxisAlignedClip(rcContent, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (s.activeRespTab == ApiResponseTab::Body) {
        float gutterW = config.ScaleF(40.0f);
        D2D1_RECT_F rcGutter = D2D1::RectF(x, contentY, x + gutterW, contentY + contentH);
        pRT->FillRectangle(rcGutter, theme.brushTabBarBg.Get());
        pRT->DrawLine(D2D1::Point2F(x + gutterW, contentY), D2D1::Point2F(x + gutterW, contentY + contentH),
                      theme.brushBorderSubtle.Get(), 1.0f);

        float lineH = config.ScaleF(18.0f);
        float textX = x + gutterW + config.ScaleF(10.0f);
        float lineY = contentY + config.ScaleF(6.0f) - s.respScrollY;

        std::wstringstream ss(s.response.body);
        std::wstring line;
        int lineNum = 1;

        while (std::getline(ss, line)) {
            if (lineY + lineH > contentY && lineY < contentY + contentH) {
                std::wstring numStr = std::to_wstring(lineNum);
                D2D1_RECT_F rcNum = D2D1::RectF(x, lineY, x + gutterW - config.ScaleF(6.0f), lineY + lineH);
                pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()),
                              theme.fontLineNumber.Get(), rcNum, theme.brushLineNumber.Get());

                D2D1_RECT_F rcCode = D2D1::RectF(textX, lineY, x + w - config.ScaleF(10.0f), lineY + lineH);
                auto colon = line.find(L':');
                if (colon != std::wstring::npos && line.find(L'"') < colon) {
                    pRT->DrawText(line.c_str(), static_cast<UINT32>(line.length()),
                                  theme.fontCode.Get(), rcCode, theme.brushIconBlue ? theme.brushIconBlue.Get() : theme.brushTextPrimary.Get());
                } else {
                    pRT->DrawText(line.c_str(), static_cast<UINT32>(line.length()),
                                  theme.fontCode.Get(), rcCode, theme.brushTextPrimary.Get());
                }
            }
            lineY += lineH;
            lineNum++;
        }
    } else if (s.activeRespTab == ApiResponseTab::Headers) {
        float rowH = config.ScaleF(24.0f);
        float curY = contentY + config.ScaleF(8.0f) - s.respScrollY;
        for (const auto& hdr : s.response.headers) {
            if (curY + rowH > contentY && curY < contentY + contentH) {
                D2D1_RECT_F rcK = D2D1::RectF(x + config.ScaleF(16.0f), curY, x + config.ScaleF(240.0f), curY + rowH);
                pRT->DrawText(hdr.first.c_str(), static_cast<UINT32>(hdr.first.length()),
                              theme.fontKeycap.Get(), rcK, theme.brushBrandAccent.Get());

                D2D1_RECT_F rcV = D2D1::RectF(x + config.ScaleF(250.0f), curY, x + w - config.ScaleF(16.0f), curY + rowH);
                pRT->DrawText(hdr.second.c_str(), static_cast<UINT32>(hdr.second.length()),
                              theme.fontTab.Get(), rcV, theme.brushTextPrimary.Get());
            }
            curY += rowH;
        }
    } else if (s.activeRespTab == ApiResponseTab::Cookies) {
        float rowH = config.ScaleF(24.0f);
        float curY = contentY + config.ScaleF(8.0f) - s.respScrollY;
        if (s.response.cookies.empty()) {
            pRT->DrawText(L"No cookies received from the server.", 36,
                          theme.fontHint.Get(), D2D1::RectF(x + 20.0f, contentY + 20.0f, x + w, contentY + 50.0f),
                          theme.brushTextMuted.Get());
        } else {
            for (const auto& ck : s.response.cookies) {
                if (curY + rowH > contentY && curY < contentY + contentH) {
                    D2D1_RECT_F rcCk = D2D1::RectF(x + config.ScaleF(16.0f), curY, x + w - config.ScaleF(16.0f), curY + rowH);
                    pRT->DrawText(ck.c_str(), static_cast<UINT32>(ck.length()),
                                  theme.fontTab.Get(), rcCk, theme.brushTextPrimary.Get());
                }
                curY += rowH;
            }
        }
    }

    pRT->PopAxisAlignedClip();
}

// =============================================================================
// Mouse and Interaction Routing
// =============================================================================
bool ApiTesterView::HandleMouseMove(POINT pt) {
    bool changed = (m_mousePt.x != pt.x || m_mousePt.y != pt.y);
    m_mousePt = pt;

    if (m_isDraggingSplitter) {
        float availH = m_viewH - 24.0f;
        if (availH > 100.0f) {
            float relY = static_cast<float>(pt.y - m_viewY);
            m_splitRatio = (std::max)(0.22f, (std::min)(0.78f, relY / availH));
        }
        return true;
    }

    if (m_isHeaderSuggestOpen && PtInRectF(pt, m_rcHeaderSuggest)) {
        float itemH = 26.0f;
        int idx = static_cast<int>((pt.y - (m_rcHeaderSuggest.top + 4.0f)) / itemH);
        if (idx >= 0 && idx < static_cast<int>(m_headerSuggestions.size())) {
            m_headerSuggestHoverIdx = idx;
            return true;
        }
    }

    return changed;
}

bool ApiTesterView::HandleLButtonDown(POINT pt, const app::AppConfig& config, Theme& theme) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sessions.empty()) AddNewSession();
    auto& s = m_sessions[m_activeSessionIdx];

    // 0. If Header Autocomplete Dropdown Open
    if (m_isHeaderSuggestOpen) {
        if (PtInRectF(pt, m_rcHeaderSuggest)) {
            float itemH = config.ScaleF(26.0f);
            int idx = static_cast<int>((pt.y - (m_rcHeaderSuggest.top + config.ScaleF(4.0f))) / itemH);
            if (idx >= 0 && idx < static_cast<int>(m_headerSuggestions.size())) {
                SelectHeaderSuggestion(static_cast<size_t>(idx));
                return true;
            }
        }
        m_isHeaderSuggestOpen = false;
        // Don't return here if clicking another control
    }

    // 1. If Method Dropdown Open
    if (m_isMethodDropdownOpen) {
        if (PtInRectF(pt, m_rcMethodDropdown)) {
            float itemH = config.ScaleF(28.0f);
            int idx = static_cast<int>((pt.y - m_rcMethodDropdown.top - config.ScaleF(4.0f)) / itemH);
            static const HttpMethod methods[] = {
                HttpMethod::Get, HttpMethod::Post, HttpMethod::Put,
                HttpMethod::Patch, HttpMethod::Delete, HttpMethod::Head, HttpMethod::Options
            };
            if (idx >= 0 && idx < 7) {
                s.method = methods[idx];
            }
        }
        m_isMethodDropdownOpen = false;
        return true;
    }

    // 2. If Auth Dropdown Open
    if (m_isAuthDropdownOpen) {
        if (PtInRectF(pt, m_rcAuthDropdown)) {
            float itemH = config.ScaleF(28.0f);
            int idx = static_cast<int>((pt.y - m_rcAuthDropdown.top - config.ScaleF(4.0f)) / itemH);
            static const ApiAuthType types[] = {
                ApiAuthType::NoAuth, ApiAuthType::BearerToken, ApiAuthType::ApiKey, ApiAuthType::BasicAuth
            };
            if (idx >= 0 && idx < 4) {
                s.authType = types[idx];
                m_focusedField = ApiFocusField::None;
            }
        }
        m_isAuthDropdownOpen = false;
        return true;
    }

    // 3. Splitter Drag
    if (pt.x >= m_rcSplitter.left && pt.x <= m_rcSplitter.right &&
        pt.y >= m_rcSplitter.top - 3.0f && pt.y <= m_rcSplitter.bottom + 3.0f) {
        m_isDraggingSplitter = true;
        if (m_hwnd) SetCapture(m_hwnd);
        return true;
    }

    // 4. Method Dropdown Button
    if (PtInRectF(pt, m_rcMethodBtn)) {
        m_isMethodDropdownOpen = !m_isMethodDropdownOpen;
        m_isAuthDropdownOpen = false;
        m_isHeaderSuggestOpen = false;
        return true;
    }

    // 5. Send Button
    if (PtInRectF(pt, m_rcSendBtn)) {
        SendCurrentRequest();
        return true;
    }

    // 6. URL Box Click (Hit testing with DirectWrite)
    if (PtInRectF(pt, m_rcUrlBox)) {
        m_focusedField = ApiFocusField::Url;
        ClearSelection();
        float relX = (std::max)(0.0f, pt.x - (m_rcUrlBox.left + config.ScaleF(12.0f)));
        float relY = (std::max)(0.0f, pt.y - (m_rcUrlBox.top + config.ScaleF(7.0f)));
        s.urlCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(),
                                      s.url, relX, relY,
                                      (m_rcUrlBox.right - m_rcUrlBox.left) - config.ScaleF(24.0f),
                                      (m_rcUrlBox.bottom - m_rcUrlBox.top));
        m_showCaret = true;
        m_lastCaretBlink = GetTickCount();
        return true;
    }

    // 7. Request Sub-Nav Tabs (Params, Authorization, Headers, Body, Settings)
    for (const auto& [tab, rc] : m_subNavTabs) {
        if (PtInRectF(pt, rc)) {
            s.activeReqTab = tab;
            s.reqScrollY = 0.0f;
            m_focusedField = ApiFocusField::None;
            m_isHeaderSuggestOpen = false;
            ClearSelection();
            return true;
        }
    }

    // 8. Response Sub-Tabs (Body, Headers, Cookies)
    for (const auto& [tab, rc] : m_respNavTabs) {
        if (PtInRectF(pt, rc)) {
            s.activeRespTab = tab;
            s.respScrollY = 0.0f;
            return true;
        }
    }

    // 9. Copy Response Button
    if (PtInRectF(pt, m_rcCopyBtn)) {
        CopyResponseToClipboard();
        return true;
    }

    // 10. Clear Response Button
    if (PtInRectF(pt, m_rcClearBtn)) {
        s.response = ApiResponseData{};
        s.respScrollY = 0.0f;
        m_toastMessage = L"Response cleared";
        m_toastExpiry = GetTickCount() + 1500;
        return true;
    }

    // Helper for Key-Value-Description Table clicks
    auto handleTableClicks = [&](std::vector<ApiKeyValueItem>& rows,
                                 ApiFocusField keyF, ApiFocusField valF, ApiFocusField descF,
                                 bool isHeaderTable, bool syncUrl) -> bool {
        if (PtInRectF(pt, m_rcAddRowBtn)) {
            rows.push_back({ true, L"", L"", L"" });
            m_focusedField = keyF;
            m_focusedRowIndex = rows.size() - 1;
            s.cellCaret = 0;
            ClearSelection();
            m_showCaret = true;
            m_lastCaretBlink = GetTickCount();
            if (isHeaderTable) {
                m_isHeaderKeySuggest = true;
                m_headerSuggestRow = m_focusedRowIndex;
                m_rcHeaderSuggest = D2D1::RectF(rows.back().keyBounds.left, rows.back().keyBounds.bottom + 2.0f,
                                               rows.back().keyBounds.left + 220.0f, rows.back().keyBounds.bottom + 180.0f);
                UpdateHeaderSuggestions(L"");
            }
            return true;
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            auto& r = rows[i];
            if (PtInRectF(pt, r.checkBounds)) {
                r.enabled = !r.enabled;
                if (syncUrl) SyncParamsToUrl(s);
                return true;
            }
            if (PtInRectF(pt, r.deleteBounds)) {
                if (rows.size() <= 1) {
                    rows[0] = { true, L"", L"", L"" };
                } else {
                    rows.erase(rows.begin() + i);
                }
                m_focusedField = ApiFocusField::None;
                m_isHeaderSuggestOpen = false;
                ClearSelection();
                if (syncUrl) SyncParamsToUrl(s);
                return true;
            }
            if (PtInRectF(pt, r.keyBounds)) {
                m_focusedField = keyF;
                m_focusedRowIndex = i;
                ClearSelection();
                float relX = (std::max)(0.0f, pt.x - (r.keyBounds.left + config.ScaleF(8.0f)));
                s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(),
                                                r.key, relX, 0.0f,
                                                r.keyBounds.right - r.keyBounds.left, r.keyBounds.bottom - r.keyBounds.top);
                m_showCaret = true;
                m_lastCaretBlink = GetTickCount();

                if (isHeaderTable) {
                    m_isHeaderKeySuggest = true;
                    m_headerSuggestRow = i;
                    m_rcHeaderSuggest = D2D1::RectF(r.keyBounds.left, r.keyBounds.bottom + 2.0f,
                                                   r.keyBounds.left + (std::max)(220.0f, r.keyBounds.right - r.keyBounds.left),
                                                   r.keyBounds.bottom + 180.0f);
                    UpdateHeaderSuggestions(r.key);
                }
                return true;
            }
            if (PtInRectF(pt, r.valBounds)) {
                m_focusedField = valF;
                m_focusedRowIndex = i;
                ClearSelection();
                float relX = (std::max)(0.0f, pt.x - (r.valBounds.left + config.ScaleF(8.0f)));
                s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(),
                                                r.value, relX, 0.0f,
                                                r.valBounds.right - r.valBounds.left, r.valBounds.bottom - r.valBounds.top);
                m_showCaret = true;
                m_lastCaretBlink = GetTickCount();

                if (isHeaderTable) {
                    m_isHeaderKeySuggest = false;
                    m_headerSuggestRow = i;
                    m_rcHeaderSuggest = D2D1::RectF(r.valBounds.left, r.valBounds.bottom + 2.0f,
                                                   r.valBounds.left + (std::max)(240.0f, r.valBounds.right - r.valBounds.left),
                                                   r.valBounds.bottom + 180.0f);
                    UpdateHeaderSuggestions(r.value);
                }
                return true;
            }
            if (PtInRectF(pt, r.descBounds)) {
                m_focusedField = descF;
                m_focusedRowIndex = i;
                m_isHeaderSuggestOpen = false;
                ClearSelection();
                float relX = (std::max)(0.0f, pt.x - (r.descBounds.left + config.ScaleF(8.0f)));
                s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(),
                                                r.description, relX, 0.0f,
                                                r.descBounds.right - r.descBounds.left, r.descBounds.bottom - r.descBounds.top);
                m_showCaret = true;
                m_lastCaretBlink = GetTickCount();
                return true;
            }
        }
        return false;
    };

    // 11. Active Sub-Tab Specific Interactions
    if (s.activeReqTab == ApiRequestTab::Params) {
        if (handleTableClicks(s.params, ApiFocusField::ParamKey, ApiFocusField::ParamVal, ApiFocusField::ParamDesc, false, true)) {
            return true;
        }
    } else if (s.activeReqTab == ApiRequestTab::Headers) {
        if (handleTableClicks(s.headers, ApiFocusField::HeaderKey, ApiFocusField::HeaderVal, ApiFocusField::HeaderDesc, true, false)) {
            return true;
        }
    } else if (s.activeReqTab == ApiRequestTab::Auth) {
        if (PtInRectF(pt, m_rcAuthTypeBtn)) {
            m_isAuthDropdownOpen = !m_isAuthDropdownOpen;
            m_isMethodDropdownOpen = false;
            return true;
        }
        if (s.authType == ApiAuthType::BearerToken && PtInRectF(pt, m_rcAuthField1)) {
            m_focusedField = ApiFocusField::AuthToken;
            ClearSelection();
            float relX = (std::max)(0.0f, pt.x - (m_rcAuthField1.left + config.ScaleF(10.0f)));
            s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(), s.bearerToken, relX);
            m_showCaret = true;
            return true;
        }
        if (s.authType == ApiAuthType::ApiKey) {
            if (PtInRectF(pt, m_rcAuthField1)) {
                m_focusedField = ApiFocusField::ApiKeyName;
                ClearSelection();
                float relX = (std::max)(0.0f, pt.x - (m_rcAuthField1.left + config.ScaleF(10.0f)));
                s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(), s.apiKeyName, relX);
                m_showCaret = true;
                return true;
            }
            if (PtInRectF(pt, m_rcAuthField2)) {
                m_focusedField = ApiFocusField::ApiKeyValue;
                ClearSelection();
                float relX = (std::max)(0.0f, pt.x - (m_rcAuthField2.left + config.ScaleF(10.0f)));
                s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(), s.apiKeyValue, relX);
                m_showCaret = true;
                return true;
            }
            if (PtInRectF(pt, m_rcApiKeyHeaderBtn)) {
                s.apiKeyInHeader = true;
                return true;
            }
            if (PtInRectF(pt, m_rcApiKeyQueryBtn)) {
                s.apiKeyInHeader = false;
                return true;
            }
        }
        if (s.authType == ApiAuthType::BasicAuth) {
            if (PtInRectF(pt, m_rcAuthField1)) {
                m_focusedField = ApiFocusField::AuthUser;
                ClearSelection();
                float relX = (std::max)(0.0f, pt.x - (m_rcAuthField1.left + config.ScaleF(10.0f)));
                s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(), s.basicUser, relX);
                m_showCaret = true;
                return true;
            }
            if (PtInRectF(pt, m_rcAuthField2)) {
                m_focusedField = ApiFocusField::AuthPass;
                ClearSelection();
                float relX = (std::max)(0.0f, pt.x - (m_rcAuthField2.left + config.ScaleF(10.0f)));
                s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(), s.basicPass, relX);
                m_showCaret = true;
                return true;
            }
        }
    } else if (s.activeReqTab == ApiRequestTab::Body) {
        for (const auto& [bType, rcPill] : m_bodyFormatTabs) {
            if (PtInRectF(pt, rcPill)) {
                s.bodyType = bType;
                s.reqScrollY = 0.0f;
                m_focusedField = (bType == ApiBodyType::RawJson) ? ApiFocusField::Body : ApiFocusField::None;
                ClearSelection();
                return true;
            }
        }
        if (s.bodyType == ApiBodyType::FormData) {
            if (handleTableClicks(s.formData, ApiFocusField::FormKey, ApiFocusField::FormVal, ApiFocusField::FormDesc, false, false)) {
                return true;
            }
        } else if (s.bodyType == ApiBodyType::UrlEncoded) {
            if (handleTableClicks(s.urlEncoded, ApiFocusField::UrlEncKey, ApiFocusField::UrlEncVal, ApiFocusField::UrlEncDesc, false, false)) {
                return true;
            }
        } else if (s.bodyType == ApiBodyType::RawJson && PtInRectF(pt, m_rcBodyEditor)) {
            m_focusedField = ApiFocusField::Body;
            ClearSelection();

            float gutterW = config.ScaleF(38.0f);
            float lineH = config.ScaleF(19.0f);
            float textX = m_rcBodyEditor.left + gutterW + config.ScaleF(10.0f);
            float relY = (std::max)(0.0f, (pt.y - (m_rcBodyEditor.top + config.ScaleF(6.0f))) + s.reqScrollY);
            float relX = (std::max)(0.0f, pt.x - textX);

            size_t targetLine = static_cast<size_t>(relY / lineH);

            size_t curLine = 0;
            size_t lineStart = 0;
            while (curLine < targetLine && lineStart < s.bodyText.length()) {
                size_t nl = s.bodyText.find(L'\n', lineStart);
                if (nl == std::wstring::npos) {
                    lineStart = s.bodyText.length();
                    break;
                }
                lineStart = nl + 1;
                curLine++;
            }
            size_t lineEnd = s.bodyText.find(L'\n', lineStart);
            if (lineEnd == std::wstring::npos) lineEnd = s.bodyText.length();

            std::wstring clickedLine = s.bodyText.substr(lineStart, lineEnd - lineStart);
            size_t colHit = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontCode.Get(),
                                              clickedLine, relX, 0.0f,
                                              (m_rcBodyEditor.right - textX), lineH);
            s.bodyCaret = lineStart + colHit;
            m_showCaret = true;
            m_lastCaretBlink = GetTickCount();
            return true;
        }
    } else if (s.activeReqTab == ApiRequestTab::Settings) {
        if (PtInRectF(pt, m_rcSslToggleBtn)) {
            s.verifySsl = !s.verifySsl;
            return true;
        }
        if (PtInRectF(pt, m_rcRedirectToggleBtn)) {
            s.followRedirects = !s.followRedirects;
            return true;
        }
        if (PtInRectF(pt, m_rcTimeoutInput)) {
            m_focusedField = ApiFocusField::SettingsTimeout;
            ClearSelection();
            float relX = (std::max)(0.0f, pt.x - (m_rcTimeoutInput.left + config.ScaleF(8.0f)));
            s.cellCaret = HitTestCaretIndex(theme.dwriteFactory.Get(), theme.fontTab.Get(), s.timeoutMsStr, relX);
            m_showCaret = true;
            return true;
        }
    }

    m_focusedField = ApiFocusField::None;
    ClearSelection();
    return true;
}

bool ApiTesterView::HandleLButtonUp(POINT pt) {
    if (m_isDraggingSplitter) {
        m_isDraggingSplitter = false;
        ReleaseCapture();
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Mouse Wheel - Bounded Scrolling (Eliminates Infinite Scrolling)
// -----------------------------------------------------------------------------
bool ApiTesterView::HandleMouseWheel(short delta, float mouseY) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sessions.empty()) return false;
    auto& s = m_sessions[m_activeSessionIdx];
    float scrollStep = 36.0f;
    float deltaVal = (delta > 0) ? -scrollStep : scrollStep;

    if (mouseY > m_rcSplitter.bottom) {
        // Calculate bounded height for active response tab
        float totalH = 0.0f;
        if (s.activeRespTab == ApiResponseTab::Body) {
            size_t lineCount = 1;
            for (wchar_t ch : s.response.body) if (ch == L'\n') lineCount++;
            totalH = static_cast<float>(lineCount) * 18.0f + 30.0f;
        } else if (s.activeRespTab == ApiResponseTab::Headers) {
            totalH = static_cast<float>(s.response.headers.size()) * 24.0f + 30.0f;
        } else if (s.activeRespTab == ApiResponseTab::Cookies) {
            totalH = static_cast<float>(s.response.cookies.size()) * 24.0f + 30.0f;
        }

        float maxScroll = (std::max)(0.0f, totalH - m_respContentH);
        s.respScrollY = (std::max)(0.0f, (std::min)(maxScroll, s.respScrollY + deltaVal));
    } else {
        // Calculate bounded height for active request tab
        float totalH = 0.0f;
        if (s.activeReqTab == ApiRequestTab::Body && s.bodyType == ApiBodyType::RawJson) {
            size_t lineCount = 1;
            for (wchar_t ch : s.bodyText) if (ch == L'\n') lineCount++;
            totalH = static_cast<float>(lineCount) * 19.0f + 30.0f;
        } else if (s.activeReqTab == ApiRequestTab::Params) {
            totalH = static_cast<float>(s.params.size()) * 30.0f + 70.0f;
        } else if (s.activeReqTab == ApiRequestTab::Headers) {
            totalH = static_cast<float>(s.headers.size()) * 30.0f + 70.0f;
        } else if (s.activeReqTab == ApiRequestTab::Body && s.bodyType == ApiBodyType::FormData) {
            totalH = static_cast<float>(s.formData.size()) * 30.0f + 70.0f;
        } else if (s.activeReqTab == ApiRequestTab::Body && s.bodyType == ApiBodyType::UrlEncoded) {
            totalH = static_cast<float>(s.urlEncoded.size()) * 30.0f + 70.0f;
        }

        float maxScroll = (std::max)(0.0f, totalH - m_subContentH);
        s.reqScrollY = (std::max)(0.0f, (std::min)(maxScroll, s.reqScrollY + deltaVal));
    }
    return true;
}

// =============================================================================
// Keyboard Input Handling (Ctrl+A, Ctrl+C, Ctrl+X, Arrow Navigation, Caret)
// =============================================================================
bool ApiTesterView::HandleChar(wchar_t ch) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sessions.empty()) return false;
    auto& s = m_sessions[m_activeSessionIdx];

    if (ch < 32 && ch != L'\t') return false;

    m_showCaret = true;
    m_lastCaretBlink = GetTickCount();

    if (m_hasSelection) {
        DeleteSelectedRange(s);
    }

    if (m_focusedField == ApiFocusField::Url) {
        if (ch >= 32) {
            size_t pos = (std::min)(s.urlCaret, s.url.length());
            s.url.insert(s.url.begin() + pos, ch);
            s.urlCaret = pos + 1;
            SyncUrlToParams(s);
            return true;
        }
    } else if (m_focusedField == ApiFocusField::Body) {
        size_t pos = (std::min)(s.bodyCaret, s.bodyText.length());
        if (ch == L'\t') {
            s.bodyText.insert(pos, L"  ");
            s.bodyCaret = pos + 2;
        } else if (ch >= 32) {
            s.bodyText.insert(s.bodyText.begin() + pos, ch);
            s.bodyCaret = pos + 1;
        }
        return true;
    } else if (std::wstring* pCell = GetActiveCellString(s)) {
        if (ch >= 32) {
            if (m_focusedField == ApiFocusField::SettingsTimeout && (ch < L'0' || ch > L'9')) {
                return true;
            }
            size_t pos = (std::min)(s.cellCaret, pCell->length());
            pCell->insert(pCell->begin() + pos, ch);
            s.cellCaret = pos + 1;
            if (m_focusedField == ApiFocusField::ParamKey || m_focusedField == ApiFocusField::ParamVal) {
                SyncParamsToUrl(s);
            } else if (m_focusedField == ApiFocusField::HeaderKey || m_focusedField == ApiFocusField::HeaderVal) {
                UpdateHeaderSuggestions(*pCell);
            }
            return true;
        }
    }
    return false;
}

bool ApiTesterView::HandleKeyDown(WPARAM key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sessions.empty()) return false;
    auto& s = m_sessions[m_activeSessionIdx];

    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    m_showCaret = true;
    m_lastCaretBlink = GetTickCount();

    // Escape closes header suggestions or method dropdown
    if (key == VK_ESCAPE) {
        if (m_isHeaderSuggestOpen) {
            m_isHeaderSuggestOpen = false;
            return true;
        }
        if (m_isMethodDropdownOpen) {
            m_isMethodDropdownOpen = false;
            return true;
        }
        if (m_isAuthDropdownOpen) {
            m_isAuthDropdownOpen = false;
            return true;
        }
        if (m_hasSelection) {
            ClearSelection();
            return true;
        }
    }

    // Header Autocomplete Up/Down/Enter navigation
    if (m_isHeaderSuggestOpen && !m_headerSuggestions.empty()) {
        if (key == VK_DOWN) {
            m_headerSuggestHoverIdx = (std::min)(static_cast<int>(m_headerSuggestions.size() - 1), m_headerSuggestHoverIdx + 1);
            return true;
        }
        if (key == VK_UP) {
            m_headerSuggestHoverIdx = (std::max)(0, m_headerSuggestHoverIdx - 1);
            return true;
        }
        if (key == VK_RETURN && m_headerSuggestHoverIdx >= 0) {
            SelectHeaderSuggestion(static_cast<size_t>(m_headerSuggestHoverIdx));
            return true;
        }
    }

    // Ctrl+Enter: Send request from anywhere
    if (key == VK_RETURN && ctrl) {
        SendCurrentRequest();
        return true;
    }

    // Ctrl+A: Select All in active field
    if (ctrl && key == 'A') {
        m_hasSelection = true;
        m_selStart = 0;
        if (m_focusedField == ApiFocusField::Url) {
            m_selEnd = s.url.length();
            s.urlCaret = s.url.length();
        } else if (m_focusedField == ApiFocusField::Body) {
            m_selEnd = s.bodyText.length();
            s.bodyCaret = s.bodyText.length();
        } else if (std::wstring* pStr = GetActiveCellString(s)) {
            m_selEnd = pStr->length();
            s.cellCaret = pStr->length();
        }
        return true;
    }

    // Ctrl+C: Copy selected text
    if (ctrl && key == 'C') {
        std::wstring text = GetSelectedText(s);
        if (!text.empty() && m_hwnd && OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            size_t bytes = (text.length() + 1) * sizeof(wchar_t);
            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hGlobal) {
                void* pMem = GlobalLock(hGlobal);
                if (pMem) {
                    memcpy(pMem, text.c_str(), bytes);
                    GlobalUnlock(hGlobal);
                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                }
            }
            CloseClipboard();
        }
        return true;
    }

    // Ctrl+X: Cut selected text
    if (ctrl && key == 'X') {
        std::wstring text = GetSelectedText(s);
        if (!text.empty() && m_hwnd && OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            size_t bytes = (text.length() + 1) * sizeof(wchar_t);
            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hGlobal) {
                void* pMem = GlobalLock(hGlobal);
                if (pMem) {
                    memcpy(pMem, text.c_str(), bytes);
                    GlobalUnlock(hGlobal);
                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                }
            }
            CloseClipboard();
            DeleteSelectedRange(s);
        }
        return true;
    }

    // Ctrl+V: Paste from clipboard
    if (ctrl && key == 'V') {
        PasteFromClipboard();
        return true;
    }

    // Delete selected text on Backspace or Delete
    if ((key == VK_BACK || key == VK_DELETE) && m_hasSelection) {
        DeleteSelectedRange(s);
        return true;
    }

    if (m_focusedField == ApiFocusField::Url) {
        if (key == VK_RETURN) {
            SendCurrentRequest();
            return true;
        }
        if (key == VK_BACK) {
            if (s.urlCaret > 0 && !s.url.empty()) {
                s.url.erase(s.url.begin() + s.urlCaret - 1);
                s.urlCaret--;
                SyncUrlToParams(s);
            }
            ClearSelection();
            return true;
        }
        if (key == VK_DELETE) {
            if (s.urlCaret < s.url.length()) {
                s.url.erase(s.url.begin() + s.urlCaret);
                SyncUrlToParams(s);
            }
            ClearSelection();
            return true;
        }
        if (key == VK_LEFT)  { ClearSelection(); if (s.urlCaret > 0) s.urlCaret--; return true; }
        if (key == VK_RIGHT) { ClearSelection(); if (s.urlCaret < s.url.length()) s.urlCaret++; return true; }
        if (key == VK_HOME)  { ClearSelection(); s.urlCaret = 0; return true; }
        if (key == VK_END)   { ClearSelection(); s.urlCaret = s.url.length(); return true; }
    } else if (m_focusedField == ApiFocusField::Body) {
        if (key == VK_RETURN) {
            size_t pos = (std::min)(s.bodyCaret, s.bodyText.length());
            s.bodyText.insert(s.bodyText.begin() + pos, L'\n');
            s.bodyCaret = pos + 1;
            ClearSelection();
            return true;
        }
        if (key == VK_BACK) {
            if (s.bodyCaret > 0 && !s.bodyText.empty()) {
                s.bodyText.erase(s.bodyText.begin() + s.bodyCaret - 1);
                s.bodyCaret--;
            }
            ClearSelection();
            return true;
        }
        if (key == VK_DELETE) {
            if (s.bodyCaret < s.bodyText.length()) {
                s.bodyText.erase(s.bodyText.begin() + s.bodyCaret);
            }
            ClearSelection();
            return true;
        }
        if (key == VK_LEFT)  { ClearSelection(); if (s.bodyCaret > 0) s.bodyCaret--; return true; }
        if (key == VK_RIGHT) { ClearSelection(); if (s.bodyCaret < s.bodyText.length()) s.bodyCaret++; return true; }
        if (key == VK_HOME)  { ClearSelection(); s.bodyCaret = 0; return true; }
        if (key == VK_END)   { ClearSelection(); s.bodyCaret = s.bodyText.length(); return true; }
    } else if (std::wstring* pCell = GetActiveCellString(s)) {
        if (key == VK_BACK) {
            if (s.cellCaret > 0 && !pCell->empty()) {
                pCell->erase(pCell->begin() + s.cellCaret - 1);
                s.cellCaret--;
                if (m_focusedField == ApiFocusField::ParamKey || m_focusedField == ApiFocusField::ParamVal) {
                    SyncParamsToUrl(s);
                } else if (m_focusedField == ApiFocusField::HeaderKey || m_focusedField == ApiFocusField::HeaderVal) {
                    UpdateHeaderSuggestions(*pCell);
                }
            }
            ClearSelection();
            return true;
        }
        if (key == VK_DELETE) {
            if (s.cellCaret < pCell->length()) {
                pCell->erase(pCell->begin() + s.cellCaret);
                if (m_focusedField == ApiFocusField::ParamKey || m_focusedField == ApiFocusField::ParamVal) {
                    SyncParamsToUrl(s);
                } else if (m_focusedField == ApiFocusField::HeaderKey || m_focusedField == ApiFocusField::HeaderVal) {
                    UpdateHeaderSuggestions(*pCell);
                }
            }
            ClearSelection();
            return true;
        }
        if (key == VK_LEFT)  { ClearSelection(); if (s.cellCaret > 0) s.cellCaret--; return true; }
        if (key == VK_RIGHT) { ClearSelection(); if (s.cellCaret < pCell->length()) s.cellCaret++; return true; }
        if (key == VK_HOME)  { ClearSelection(); s.cellCaret = 0; return true; }
        if (key == VK_END)   { ClearSelection(); s.cellCaret = pCell->length(); return true; }
        if (key == VK_TAB) {
            ClearSelection();
            m_isHeaderSuggestOpen = false;
            if (m_focusedField == ApiFocusField::ParamKey) m_focusedField = ApiFocusField::ParamVal;
            else if (m_focusedField == ApiFocusField::ParamVal) m_focusedField = ApiFocusField::ParamDesc;
            else if (m_focusedField == ApiFocusField::HeaderKey) m_focusedField = ApiFocusField::HeaderVal;
            else if (m_focusedField == ApiFocusField::HeaderVal) m_focusedField = ApiFocusField::HeaderDesc;
            else if (m_focusedField == ApiFocusField::FormKey) m_focusedField = ApiFocusField::FormVal;
            else if (m_focusedField == ApiFocusField::FormVal) m_focusedField = ApiFocusField::FormDesc;
            else if (m_focusedField == ApiFocusField::UrlEncKey) m_focusedField = ApiFocusField::UrlEncVal;
            else if (m_focusedField == ApiFocusField::UrlEncVal) m_focusedField = ApiFocusField::UrlEncDesc;
            if (std::wstring* pNext = GetActiveCellString(s)) {
                s.cellCaret = pNext->length();
            }
            return true;
        }
    }

    return false;
}

// =============================================================================
// Comprehensive Cursor Hit-Testing (IDC_HAND, IDC_IBEAM, IDC_SIZENS)
// =============================================================================
LPCWSTR ApiTesterView::GetCursorForPoint(POINT pt) const noexcept {
    if (m_isHeaderSuggestOpen && PtInRectF(pt, m_rcHeaderSuggest)) {
        return IDC_HAND;
    }
    if (m_isMethodDropdownOpen && PtInRectF(pt, m_rcMethodDropdown)) {
        return IDC_HAND;
    }
    if (m_isAuthDropdownOpen && PtInRectF(pt, m_rcAuthDropdown)) {
        return IDC_HAND;
    }

    if (m_isDraggingSplitter || (pt.x >= m_rcSplitter.left && pt.x <= m_rcSplitter.right &&
                                 pt.y >= m_rcSplitter.top - 3.0f && pt.y <= m_rcSplitter.bottom + 3.0f)) {
        return IDC_SIZENS;
    }

    if (PtInRectF(pt, m_rcMethodBtn) || PtInRectF(pt, m_rcSendBtn)) {
        return IDC_HAND;
    }
    if (PtInRectF(pt, m_rcUrlBox)) {
        return IDC_IBEAM;
    }

    for (const auto& [tab, rc] : m_subNavTabs) {
        if (PtInRectF(pt, rc)) return IDC_HAND;
    }

    if (PtInRectF(pt, m_rcCopyBtn) || PtInRectF(pt, m_rcClearBtn)) {
        return IDC_HAND;
    }
    for (const auto& [tab, rc] : m_respNavTabs) {
        if (PtInRectF(pt, rc)) return IDC_HAND;
    }

    if (m_activeSessionIdx >= m_sessions.size()) return nullptr;
    const auto& s = m_sessions[m_activeSessionIdx];

    auto checkTableCursor = [&](const std::vector<ApiKeyValueItem>& rows) -> LPCWSTR {
        if (PtInRectF(pt, m_rcAddRowBtn)) return IDC_HAND;
        for (const auto& r : rows) {
            if (PtInRectF(pt, r.checkBounds) || PtInRectF(pt, r.deleteBounds)) return IDC_HAND;
            if (PtInRectF(pt, r.keyBounds) || PtInRectF(pt, r.valBounds) || PtInRectF(pt, r.descBounds)) return IDC_IBEAM;
        }
        return nullptr;
    };

    if (s.activeReqTab == ApiRequestTab::Params) {
        if (LPCWSTR c = checkTableCursor(s.params)) return c;
    } else if (s.activeReqTab == ApiRequestTab::Headers) {
        if (LPCWSTR c = checkTableCursor(s.headers)) return c;
    } else if (s.activeReqTab == ApiRequestTab::Auth) {
        if (PtInRectF(pt, m_rcAuthTypeBtn)) return IDC_HAND;
        if (s.authType == ApiAuthType::BearerToken && PtInRectF(pt, m_rcAuthField1)) return IDC_IBEAM;
        if (s.authType == ApiAuthType::ApiKey) {
            if (PtInRectF(pt, m_rcApiKeyHeaderBtn) || PtInRectF(pt, m_rcApiKeyQueryBtn)) return IDC_HAND;
            if (PtInRectF(pt, m_rcAuthField1) || PtInRectF(pt, m_rcAuthField2)) return IDC_IBEAM;
        }
        if (s.authType == ApiAuthType::BasicAuth) {
            if (PtInRectF(pt, m_rcAuthField1) || PtInRectF(pt, m_rcAuthField2)) return IDC_IBEAM;
        }
    } else if (s.activeReqTab == ApiRequestTab::Body) {
        for (const auto& [bType, rcPill] : m_bodyFormatTabs) {
            if (PtInRectF(pt, rcPill)) return IDC_HAND;
        }
        if (s.bodyType == ApiBodyType::FormData) {
            if (LPCWSTR c = checkTableCursor(s.formData)) return c;
        } else if (s.bodyType == ApiBodyType::UrlEncoded) {
            if (LPCWSTR c = checkTableCursor(s.urlEncoded)) return c;
        } else if (s.bodyType == ApiBodyType::RawJson && PtInRectF(pt, m_rcBodyEditor)) {
            return IDC_IBEAM;
        }
    } else if (s.activeReqTab == ApiRequestTab::Settings) {
        if (PtInRectF(pt, m_rcSslToggleBtn) || PtInRectF(pt, m_rcRedirectToggleBtn)) return IDC_HAND;
        if (PtInRectF(pt, m_rcTimeoutInput)) return IDC_IBEAM;
    }

    return nullptr;
}

} // namespace phide::ui
