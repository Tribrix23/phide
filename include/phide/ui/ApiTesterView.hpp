/**
 * @file ApiTesterView.hpp
 * @brief Production-ready full-fledged API Tester view (Postman / Thunder Client style in phide design).
 *        Supports GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS, query params, auth, headers,
 *        body (none, raw JSON, form-data, x-www-form-urlencoded), request settings, and async WinHTTP execution.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <mutex>
#include <chrono>
#include <memory>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

/**
 * @enum HttpMethod
 * @brief Supported HTTP Request Methods
 */
enum class HttpMethod {
    Get,
    Post,
    Put,
    Patch,
    Delete,
    Head,
    Options
};

inline const wchar_t* HttpMethodToString(HttpMethod m) {
    switch (m) {
        case HttpMethod::Get:     return L"GET";
        case HttpMethod::Post:    return L"POST";
        case HttpMethod::Put:     return L"PUT";
        case HttpMethod::Patch:   return L"PATCH";
        case HttpMethod::Delete:  return L"DELETE";
        case HttpMethod::Head:    return L"HEAD";
        case HttpMethod::Options: return L"OPTIONS";
    }
    return L"GET";
}

inline D2D1_COLOR_F GetMethodColor(HttpMethod m) {
    switch (m) {
        case HttpMethod::Get:     return D2D1::ColorF(0.13f, 0.77f, 0.37f, 1.0f); // #22C55E Green
        case HttpMethod::Post:    return D2D1::ColorF(0.98f, 0.45f, 0.09f, 1.0f); // #F97316 Orange
        case HttpMethod::Put:     return D2D1::ColorF(0.23f, 0.51f, 0.96f, 1.0f); // #3B82F6 Blue
        case HttpMethod::Patch:   return D2D1::ColorF(0.66f, 0.33f, 0.97f, 1.0f); // #A855F7 Purple
        case HttpMethod::Delete:  return D2D1::ColorF(0.94f, 0.27f, 0.27f, 1.0f); // #EF4444 Red
        case HttpMethod::Head:    return D2D1::ColorF(0.58f, 0.64f, 0.72f, 1.0f); // #94A3B8 Slate
        case HttpMethod::Options: return D2D1::ColorF(0.02f, 0.71f, 0.83f, 1.0f); // #06B6D4 Cyan
    }
    return D2D1::ColorF(0.8f, 0.8f, 0.8f, 1.0f);
}

enum class ApiRequestTab {
    Params,
    Auth,
    Headers,
    Body,
    Settings
};

enum class ApiAuthType {
    NoAuth,
    BearerToken,
    ApiKey,
    BasicAuth
};

enum class ApiBodyType {
    None,
    RawJson,
    FormData,
    UrlEncoded
};

enum class ApiResponseTab {
    Body,
    Headers,
    Cookies
};

struct ApiKeyValueItem {
    bool enabled = true;
    std::wstring key;
    std::wstring value;
    std::wstring description;

    // Runtime hit bounds
    D2D1_RECT_F checkBounds{};
    D2D1_RECT_F keyBounds{};
    D2D1_RECT_F valBounds{};
    D2D1_RECT_F descBounds{};
    D2D1_RECT_F deleteBounds{};
};

struct ApiResponseData {
    bool hasResponse = false;
    bool isError = false;
    int statusCode = 0;
    std::wstring statusText;
    double timeMs = 0.0;
    size_t sizeBytes = 0;
    std::wstring body;
    std::vector<std::pair<std::wstring, std::wstring>> headers;
    std::vector<std::wstring> cookies;
    std::wstring errorMessage;
};

struct ApiRequestSession {
    HttpMethod method = HttpMethod::Get;
    std::wstring url;
    ApiRequestTab activeReqTab = ApiRequestTab::Params;

    // Params
    std::vector<ApiKeyValueItem> params;

    // Auth
    ApiAuthType authType = ApiAuthType::NoAuth;
    std::wstring bearerToken;
    std::wstring apiKeyName;
    std::wstring apiKeyValue;
    bool apiKeyInHeader = true;
    std::wstring basicUser;
    std::wstring basicPass;

    // Headers
    std::vector<ApiKeyValueItem> headers;

    // Body
    ApiBodyType bodyType = ApiBodyType::None;
    std::wstring bodyText;
    std::vector<ApiKeyValueItem> formData;
    std::vector<ApiKeyValueItem> urlEncoded;

    // Settings
    bool verifySsl = false;
    bool followRedirects = true;
    std::wstring timeoutMsStr = L"15000";

    // Response
    ApiResponseData response;
    bool isSending = false;
    float reqScrollY = 0.0f;
    float respScrollY = 0.0f;
    ApiResponseTab activeRespTab = ApiResponseTab::Body;

    // Carets
    size_t urlCaret = 0;
    size_t bodyCaret = 0;
    size_t cellCaret = 0;
};

enum class ApiFocusField {
    None,
    Url,
    Body,
    ParamKey,
    ParamVal,
    ParamDesc,
    HeaderKey,
    HeaderVal,
    HeaderDesc,
    FormKey,
    FormVal,
    FormDesc,
    UrlEncKey,
    UrlEncVal,
    UrlEncDesc,
    AuthToken,
    ApiKeyName,
    ApiKeyValue,
    AuthUser,
    AuthPass,
    SettingsTimeout
};

/**
 * @class ApiTesterView
 * @brief Production-ready API Tester inside Phide Editor Canvas
 */
class ApiTesterView {
public:
    ApiTesterView();
    ~ApiTesterView() = default;

    void SetHwnd(HWND hwnd) noexcept { m_hwnd = hwnd; }
    void DiscardDeviceResources();

    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float x, float w, float y, float h);

    bool HandleMouseMove(POINT clientPt);
    bool HandleLButtonDown(POINT clientPt, const app::AppConfig& config);
    bool HandleLButtonUp(POINT clientPt);
    bool HandleMouseWheel(short delta, float mouseY);
    bool HandleChar(wchar_t ch);
    bool HandleKeyDown(WPARAM key);
    LPCWSTR GetCursorForPoint(POINT clientPt) const noexcept;

    std::wstring GetActiveRequestDisplay() const;

    void AddNewSession();
    void SendCurrentRequest();

private:
    void EnsureDefaultData(ApiRequestSession& session);
    void SyncParamsToUrl(ApiRequestSession& session);
    void SyncUrlToParams(ApiRequestSession& session);
    ApiResponseData ExecuteNetworkRequest(const ApiRequestSession& session);

    std::wstring* GetActiveCellString(ApiRequestSession& s);
    void PasteFromClipboard();
    void CopyResponseToClipboard();

    // Helpers for rendering sub-components
    void RenderUrlBar(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w);
    void RenderMethodDropdown(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);
    void RenderAuthDropdown(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);
    void RenderSubNavTabs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w);
    void RenderSubNavContent(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h);
    void RenderKeyValueTable(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                             float x, float y, float w, float h,
                             std::vector<ApiKeyValueItem>& rows,
                             ApiFocusField keyField, ApiFocusField valField, ApiFocusField descField,
                             const wchar_t* addBtnLabel);
    void RenderAuthTab(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h);
    void RenderBodyTab(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h);
    void RenderSettingsTab(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h);
    void RenderResponseSection(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float x, float y, float w, float h);

private:
    HWND m_hwnd = nullptr;
    std::mutex m_mutex;
    LucideIcons m_icons;

    std::vector<ApiRequestSession> m_sessions;
    size_t m_activeSessionIdx = 0;

    // Layout metrics & hit targets
    float m_viewX = 0.0f;
    float m_viewY = 0.0f;
    float m_viewW = 0.0f;
    float m_viewH = 0.0f;
    float m_splitRatio = 0.52f; // 52% request, 48% response
    bool m_isDraggingSplitter = false;
    float m_splitterH = 8.0f;
    D2D1_RECT_F m_rcSplitter{};

    POINT m_mousePt{ -1, -1 };

    // Request Bar targets
    D2D1_RECT_F m_rcMethodBtn{};
    D2D1_RECT_F m_rcUrlBox{};
    D2D1_RECT_F m_rcSendBtn{};
    bool m_isMethodDropdownOpen = false;
    D2D1_RECT_F m_rcMethodDropdown{};

    // Auth dropdown & inputs
    bool m_isAuthDropdownOpen = false;
    D2D1_RECT_F m_rcAuthTypeBtn{};
    D2D1_RECT_F m_rcAuthDropdown{};
    D2D1_RECT_F m_rcAuthField1{};
    D2D1_RECT_F m_rcAuthField2{};
    D2D1_RECT_F m_rcApiKeyHeaderBtn{};
    D2D1_RECT_F m_rcApiKeyQueryBtn{};

    // Settings tab targets
    D2D1_RECT_F m_rcSslToggleBtn{};
    D2D1_RECT_F m_rcRedirectToggleBtn{};
    D2D1_RECT_F m_rcTimeoutInput{};

    // Sub nav tabs bounds
    std::vector<std::pair<ApiRequestTab, D2D1_RECT_F>> m_subNavTabs;

    // Body format selector bounds
    std::vector<std::pair<ApiBodyType, D2D1_RECT_F>> m_bodyFormatTabs;
    D2D1_RECT_F m_rcBodyEditor{};

    // Response tabs bounds
    std::vector<std::pair<ApiResponseTab, D2D1_RECT_F>> m_respNavTabs;
    D2D1_RECT_F m_rcCopyBtn{};
    D2D1_RECT_F m_rcClearBtn{};
    D2D1_RECT_F m_rcAddRowBtn{};

    // Active Focus & Caret state
    ApiFocusField m_focusedField = ApiFocusField::Url;
    size_t m_focusedRowIndex = 0;
    DWORD m_lastCaretBlink = 0;
    bool m_showCaret = true;

    // Toast notification
    std::wstring m_toastMessage;
    DWORD m_toastExpiry = 0;
};

} // namespace phide::ui
