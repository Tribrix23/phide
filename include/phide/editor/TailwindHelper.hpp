#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

namespace phide::editor {

struct TailwindColorInfo {
    uint32_t hex = 0;
    std::wstring cssProp;     // e.g. L"background-color"
    std::wstring cssValue;    // e.g. L"rgb(239 68 68);"
    std::wstring hexStr;      // e.g. L"#ef4444"
    float alpha = 1.0f;
    bool hasAlpha = false;
};

struct TailwindCandidate {
    std::wstring name;
    std::wstring detail;
    std::wstring doc;
    bool isColor = false;
    uint32_t hex = 0;
};

class TailwindHelper {
public:
    static bool TryExtractColorPrefix(const std::wstring_view& text, std::wstring_view& outPrefix, std::wstring_view& outRem, std::wstring& outProp) {
        struct Pfx {
            const wchar_t* pfx;
            const wchar_t* prop;
        };
        static const Pfx s_pfxList[] = {
            { L"ring-offset-", L"--tw-ring-offset-color" },
            { L"ring-",        L"ring-color" },
            { L"border-t-",    L"border-top-color" },
            { L"border-r-",    L"border-right-color" },
            { L"border-b-",    L"border-bottom-color" },
            { L"border-l-",    L"border-left-color" },
            { L"border-x-",    L"border-inline-color" },
            { L"border-y-",    L"border-block-color" },
            { L"border-",      L"border-color" },
            { L"divide-x-",    L"border-inline-color" },
            { L"divide-y-",    L"border-block-color" },
            { L"divide-",      L"border-color" },
            { L"placeholder-", L"placeholder-color" },
            { L"decoration-",  L"text-decoration-color" },
            { L"outline-",     L"outline-color" },
            { L"accent-",      L"accent-color" },
            { L"caret-",       L"caret-color" },
            { L"shadow-",      L"--tw-shadow-color" },
            { L"stroke-",      L"stroke" },
            { L"fill-",        L"fill" },
            { L"from-",        L"gradient-from" },
            { L"via-",         L"gradient-via" },
            { L"to-",          L"gradient-to" },
            { L"text-",        L"color" },
            { L"bg-",          L"background-color" }
        };

        for (const auto& item : s_pfxList) {
            size_t len = wcslen(item.pfx);
            if (text.starts_with(item.pfx)) {
                outPrefix = text.substr(0, len);
                outRem = text.substr(len);
                outProp = item.prop;
                return true;
            }
        }
        return false;
    }

    static bool TryExtractColorPrefix(const std::wstring_view& text, std::wstring_view& outPrefix, std::wstring_view& outRem) {
        std::wstring dummy;
        return TryExtractColorPrefix(text, outPrefix, outRem, dummy);
    }

    static bool TryParseArbitraryColor(const std::wstring_view& inner, uint32_t& outHex, float& outAlpha, bool& outHasAlpha) {
        if (inner.empty()) return false;

        if (inner.front() == L'#') {
            std::wstring_view h = inner.substr(1);
            if (h.length() == 3) {
                unsigned int r, g, b;
                if (swscanf_s(std::wstring(h).c_str(), L"%1x%1x%1x", &r, &g, &b) == 3) {
                    outHex = ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
                    return true;
                }
            } else if (h.length() == 4) {
                unsigned int r, g, b, a;
                if (swscanf_s(std::wstring(h).c_str(), L"%1x%1x%1x%1x", &r, &g, &b, &a) == 4) {
                    outHex = ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
                    outAlpha = (a * 17) / 255.0f;
                    outHasAlpha = true;
                    return true;
                }
            } else if (h.length() == 6) {
                unsigned int rgb;
                if (swscanf_s(std::wstring(h).c_str(), L"%x", &rgb) == 1) {
                    outHex = rgb & 0xFFFFFF;
                    return true;
                }
            } else if (h.length() == 8) {
                unsigned int rgba;
                if (swscanf_s(std::wstring(h).c_str(), L"%x", &rgba) == 1) {
                    outHex = (rgba >> 8) & 0xFFFFFF;
                    outAlpha = (rgba & 0xFF) / 255.0f;
                    outHasAlpha = true;
                    return true;
                }
            }
            return false;
        }

        if (inner.starts_with(L"rgb(") || inner.starts_with(L"rgba(")) {
            float r = 0, g = 0, b = 0, a = 1.0f;
            std::wstring s(inner);
            int parsed = swscanf_s(s.c_str(), L"rgba(%f,%f,%f,%f)", &r, &g, &b, &a);
            if (parsed < 4) {
                parsed = swscanf_s(s.c_str(), L"rgb(%f,%f,%f)", &r, &g, &b);
                a = 1.0f;
            }
            if (parsed >= 3) {
                int ir = (std::clamp)(static_cast<int>(r), 0, 255);
                int ig = (std::clamp)(static_cast<int>(g), 0, 255);
                int ib = (std::clamp)(static_cast<int>(b), 0, 255);
                outHex = (ir << 16) | (ig << 8) | ib;
                if (parsed == 4) {
                    outAlpha = a;
                    outHasAlpha = (a < 0.999f);
                }
                return true;
            }
        }

        return false;
    }

    static bool TryParseClass(const std::wstring_view& text, TailwindColorInfo& outInfo) {
        if (text.empty()) return false;

        std::wstring_view prefix, rem;
        std::wstring prop;
        if (!TryExtractColorPrefix(text, prefix, rem, prop)) {
            return false;
        }
        if (rem.empty()) return false;

        float alpha = 1.0f;
        bool hasAlpha = false;

        // 1. Arbitrary bracketed value, e.g. "[#1D2E7E]" or "[#1D2E7E]/50" or "[rgb(1,2,3)]"
        if (rem.front() == L'[') {
            size_t closeB = rem.find(L']');
            if (closeB != std::wstring_view::npos) {
                std::wstring_view inner = rem.substr(1, closeB - 1);
                std::wstring_view afterBracket = rem.substr(closeB + 1);
                if (afterBracket.starts_with(L"/")) {
                    std::wstring_view alphaStr = afterBracket.substr(1);
                    int opVal = 0;
                    bool opOk = true;
                    for (wchar_t ch : alphaStr) {
                        if (ch < L'0' || ch > L'9') { opOk = false; break; }
                        opVal = opVal * 10 + (ch - L'0');
                    }
                    if (opOk && opVal >= 0 && opVal <= 100) {
                        alpha = static_cast<float>(opVal) / 100.0f;
                        hasAlpha = true;
                    }
                }

                uint32_t hex = 0;
                if (TryParseArbitraryColor(inner, hex, alpha, hasAlpha)) {
                    outInfo.hex = hex;
                    outInfo.alpha = alpha;
                    outInfo.hasAlpha = hasAlpha;
                    outInfo.cssProp = prop;
                    wchar_t hexBuf[16];
                    swprintf_s(hexBuf, L"#%02x%02x%02x", (hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
                    outInfo.hexStr = hexBuf;
                    outInfo.cssValue = prop + L": " + hexBuf + L";";
                    return true;
                }
                return false;
            }
        }

        // 2. Opacity modifier in named class, e.g. "red-600/50"
        size_t slashPos = rem.find(L'/');
        if (slashPos != std::wstring_view::npos) {
            std::wstring_view alphaStr = rem.substr(slashPos + 1);
            int opVal = 0;
            bool opOk = true;
            for (wchar_t ch : alphaStr) {
                if (ch < L'0' || ch > L'9') { opOk = false; break; }
                opVal = opVal * 10 + (ch - L'0');
            }
            if (opOk && opVal >= 0 && opVal <= 100) {
                alpha = static_cast<float>(opVal) / 100.0f;
                hasAlpha = true;
            }
            rem = rem.substr(0, slashPos);
        }

        if (rem == L"black") {
            outInfo.hex = 0x000000;
            outInfo.alpha = alpha;
            outInfo.hasAlpha = hasAlpha;
            outInfo.cssProp = prop;
            outInfo.cssValue = prop + L": #000;";
            outInfo.hexStr = L"#000000";
            return true;
        }
        if (rem == L"white") {
            outInfo.hex = 0xffffff;
            outInfo.alpha = alpha;
            outInfo.hasAlpha = hasAlpha;
            outInfo.cssProp = prop;
            outInfo.cssValue = prop + L": #fff;";
            outInfo.hexStr = L"#ffffff";
            return true;
        }
        if (rem == L"transparent") {
            outInfo.hex = 0x000000;
            outInfo.alpha = 0.0f;
            outInfo.hasAlpha = true;
            outInfo.cssProp = prop;
            outInfo.cssValue = prop + L": transparent;";
            outInfo.hexStr = L"transparent";
            return true;
        }

        size_t dash = rem.rfind(L'-');
        if (dash == std::wstring_view::npos || dash == 0 || dash + 1 >= rem.length()) {
            return false;
        }

        std::wstring_view colorName = rem.substr(0, dash);
        std::wstring_view shadeStr = rem.substr(dash + 1);

        int shade = 0;
        for (wchar_t ch : shadeStr) {
            if (ch < L'0' || ch > L'9') return false;
            shade = shade * 10 + (ch - L'0');
        }

        static const int s_shades[11] = { 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 950 };
        int sIdx = -1;
        for (int i = 0; i < 11; ++i) {
            if (s_shades[i] == shade) { sIdx = i; break; }
        }
        if (sIdx < 0) return false;

        uint32_t hex = GetColorFamilyHex(colorName, sIdx);
        if (hex == 0xFFFFFFFF) return false;

        uint32_t r = (hex >> 16) & 0xFF;
        uint32_t g = (hex >> 8) & 0xFF;
        uint32_t b = hex & 0xFF;

        wchar_t hexBuf[16];
        swprintf_s(hexBuf, L"#%02x%02x%02x", r, g, b);

        outInfo.hex = hex;
        outInfo.alpha = alpha;
        outInfo.hasAlpha = hasAlpha;
        outInfo.cssProp = prop;
        outInfo.cssValue = prop + L": rgb(" + std::to_wstring(r) + L" " + std::to_wstring(g) + L" " + std::to_wstring(b) + L");";
        outInfo.hexStr = hexBuf;
        return true;
    }

    static const std::vector<TailwindCandidate>& GetAllCandidates() {
        static std::vector<TailwindCandidate> s_candidates = BuildCandidates();
        return s_candidates;
    }

    static void InvalidateCache() {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        s_cachedRoot.clear();
        s_lastCheck = {};
        s_cachedResult = false;
    }

    /**
     * @brief Checks if Tailwind CSS is connected to the workspace, file, or buffer snippet.
     * Tailwind is considered connected if:
     * 1. The buffer snippet contains cdn.tailwindcss.com, @tailwind, or @import "tailwindcss"
     * 2. The file path contains "tailwind"
     * 3. The workspace contains tailwind.config.*, postcss.config.*, package.json with "tailwindcss", or root tailwind files.
     */
    static bool IsTailwindConnected(
        const std::wstring& workspaceRoot,
        const std::wstring& filePath = L"",
        const std::wstring_view& bufferSnippet = {}
    ) {
        // 1. Direct buffer snippet check (fast in-memory)
        if (!bufferSnippet.empty()) {
            if (bufferSnippet.find(L"cdn.tailwindcss.com") != std::wstring_view::npos ||
                bufferSnippet.find(L"tailwindcss.com") != std::wstring_view::npos ||
                bufferSnippet.find(L"@tailwind") != std::wstring_view::npos ||
                bufferSnippet.find(L"@import \"tailwindcss\"") != std::wstring_view::npos ||
                bufferSnippet.find(L"@import 'tailwindcss'") != std::wstring_view::npos) {
                return true;
            }
        }

        // 2. Direct file path check
        if (!filePath.empty()) {
            std::wstring fpLower = filePath;
            for (auto& ch : fpLower) ch = static_cast<wchar_t>(towlower(ch));
            if (fpLower.find(L"tailwind") != std::wstring::npos) {
                return true;
            }
        }

        // 3. Workspace root check with caching (initial sync check per root, async refresh after 30s)
        if (!workspaceRoot.empty()) {
            auto now = std::chrono::steady_clock::now();
            bool haveCached = false;
            bool cachedVal = false;
            bool needAsyncRefresh = false;
            {
                std::lock_guard<std::mutex> lock(s_cacheMutex);
                if (s_cachedRoot == workspaceRoot) {
                    haveCached = true;
                    cachedVal = s_cachedResult;
                    if ((now - s_lastCheck) >= std::chrono::seconds(30) && !s_refreshing.exchange(true)) {
                        needAsyncRefresh = true;
                    }
                }
            }
            if (haveCached) {
                if (needAsyncRefresh) {
                    std::wstring rootCopy = workspaceRoot;
                    std::thread([rootCopy]() {
                        bool detected = CheckWorkspaceForTailwind(rootCopy);
                        auto doneTime = std::chrono::steady_clock::now();
                        std::lock_guard<std::mutex> lock(s_cacheMutex);
                        if (s_cachedRoot == rootCopy) {
                            s_cachedResult = detected;
                            s_lastCheck = doneTime;
                        }
                        s_refreshing.store(false);
                    }).detach();
                }
                return cachedVal;
            }

            bool detected = CheckWorkspaceForTailwind(workspaceRoot);
            {
                std::lock_guard<std::mutex> lock(s_cacheMutex);
                s_cachedRoot = workspaceRoot;
                s_lastCheck = now;
                s_cachedResult = detected;
            }
            return detected;
        }

        return false;
    }

private:
    inline static std::mutex s_cacheMutex;
    inline static std::wstring s_cachedRoot;
    inline static std::chrono::steady_clock::time_point s_lastCheck{};
    inline static bool s_cachedResult = false;
    inline static std::atomic<bool> s_refreshing{ false };

    static bool CheckWorkspaceForTailwind(const std::wstring& workspaceRoot) {
        std::error_code ec;
        std::filesystem::path root(workspaceRoot);
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
            return false;
        }

        // A) Standard Tailwind configuration files in workspace root
        static const wchar_t* s_configFiles[] = {
            L"tailwind.config.js",
            L"tailwind.config.cjs",
            L"tailwind.config.mjs",
            L"tailwind.config.ts",
            L"tailwind.config.json"
        };
        for (const auto* cfg : s_configFiles) {
            if (std::filesystem::exists(root / cfg, ec)) {
                return true;
            }
        }
        static const wchar_t* s_postcssFiles[] = {
            L"postcss.config.js",
            L"postcss.config.cjs",
            L"postcss.config.mjs"
        };
        for (const auto* pcfg : s_postcssFiles) {
            std::filesystem::path pp = root / pcfg;
            if (std::filesystem::exists(pp, ec)) {
                std::ifstream f(pp, std::ios::binary);
                if (f.is_open()) {
                    std::string s(4096, '\0');
                    f.read(&s[0], s.size());
                    s.resize(static_cast<size_t>(f.gcount()));
                    if (s.find("tailwindcss") != std::string::npos) {
                        return true;
                    }
                }
            }
        }

        // B) package.json with "tailwindcss" dependency
        std::filesystem::path pkgPath = root / L"package.json";
        if (std::filesystem::exists(pkgPath, ec)) {
            std::ifstream f(pkgPath, std::ios::binary);
            if (f.is_open()) {
                std::string s;
                s.resize(8192);
                f.read(&s[0], s.size());
                s.resize(static_cast<size_t>(f.gcount()));
                if (s.find("tailwindcss") != std::string::npos) {
                    return true;
                }
            }
        }

        // C) Check root and common web asset/source subdirectories for @tailwind, @import "tailwindcss", or cdn.tailwindcss.com
        auto checkDirForTailwind = [&](const std::filesystem::path& dirPath) -> bool {
            if (!std::filesystem::exists(dirPath, ec) || !std::filesystem::is_directory(dirPath, ec)) return false;
            for (const auto* cfg : s_configFiles) {
                if (std::filesystem::exists(dirPath / cfg, ec)) return true;
            }
            for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                auto fname = entry.path().filename().wstring();
                for (auto& c : fname) c = static_cast<wchar_t>(towlower(c));
                auto ext = entry.path().extension().wstring();
                for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
                if (fname.find(L"tailwind") != std::wstring::npos &&
                    (ext == L".css" || ext == L".js" || ext == L".ts" || ext == L".cjs" || ext == L".mjs")) {
                    return true;
                }
                if (ext == L".css" || ext == L".scss") {
                    std::ifstream f(entry.path(), std::ios::binary);
                    if (f.is_open()) {
                        std::string s(4096, '\0');
                        f.read(&s[0], s.size());
                        s.resize(static_cast<size_t>(f.gcount()));
                        if (s.find("@tailwind") != std::string::npos ||
                            s.find("@import \"tailwindcss\"") != std::string::npos ||
                            s.find("@import 'tailwindcss'") != std::string::npos ||
                            s.find("@import \"tailwindcss/") != std::string::npos) {
                            return true;
                        }
                    }
                } else if (ext == L".html" || ext == L".php") {
                    std::ifstream f(entry.path(), std::ios::binary);
                    if (f.is_open()) {
                        std::string s(4096, '\0');
                        f.read(&s[0], s.size());
                        s.resize(static_cast<size_t>(f.gcount()));
                        if (s.find("cdn.tailwindcss.com") != std::string::npos) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };

        if (checkDirForTailwind(root)) return true;
        static const wchar_t* s_subDirs[] = {
            L"assets", L"assets\\css", L"css", L"styles",
            L"src", L"src\\styles", L"src\\css", L"src\\app",
            L"app", L"public", L"public\\css", L"resources\\css"
        };
        for (const auto* sub : s_subDirs) {
            if (checkDirForTailwind(root / sub)) return true;
        }

        return false;
    }
    static uint32_t GetColorFamilyHex(const std::wstring_view& name, int shadeIdx) {
        struct Fam { const wchar_t* name; uint32_t hex[11]; };
        static const Fam fams[] = {
            { L"slate",   { 0xf8fafc, 0xf1f5f9, 0xe2e8f0, 0xcbd5e1, 0x94a3b8, 0x64748b, 0x475569, 0x334155, 0x1e293b, 0x0f172a, 0x020617 } },
            { L"gray",    { 0xf9fafb, 0xf3f4f6, 0xe5e7eb, 0xd1d5db, 0x9ca3af, 0x6b7280, 0x4b5563, 0x374151, 0x1f2937, 0x111827, 0x030712 } },
            { L"zinc",    { 0xfafafa, 0xf4f4f5, 0xe4e4e7, 0xd4d4d8, 0xa1a1aa, 0x71717a, 0x52525b, 0x3f3f46, 0x27272a, 0x18181b, 0x09090b } },
            { L"neutral", { 0xfafafa, 0xf5f5f5, 0xe5e5e5, 0xd4d4d4, 0xa3a3a3, 0x737373, 0x525252, 0x404040, 0x262626, 0x171717, 0x0a0a0a } },
            { L"stone",   { 0xfafaf9, 0xf5f5f4, 0xe7e5e4, 0xd6d3d1, 0xa8a29e, 0x78716c, 0x57534e, 0x44403c, 0x292524, 0x1c1917, 0x0c0a09 } },
            { L"red",     { 0xfef2f2, 0xfee2e2, 0xfecaca, 0xfca5a5, 0xf87171, 0xef4444, 0xdc2626, 0xb91c1c, 0x991b1b, 0x7f1d1d, 0x450a0a } },
            { L"orange",  { 0xfff7ed, 0xffedd5, 0xfed7aa, 0xfdba74, 0xfb923c, 0xf97316, 0xea580c, 0xc2410c, 0x9a3412, 0x7c2d12, 0x431407 } },
            { L"amber",   { 0xfffbeb, 0xfef3c7, 0xfde68a, 0xfcd34d, 0xfbbf24, 0xf59e0b, 0xd97706, 0xb45309, 0x92400e, 0x78350f, 0x451a03 } },
            { L"yellow",  { 0xfefce8, 0xfef9c3, 0xfef08a, 0xfde047, 0xfacc15, 0xeab308, 0xca8a04, 0xa16207, 0x854d0e, 0x713f12, 0x422006 } },
            { L"lime",    { 0xf7fee7, 0xecfccb, 0xd9f99d, 0xbef264, 0xa3e635, 0x84cc16, 0x65a30d, 0x4d7c0f, 0x3f6212, 0x365314, 0x1a2e05 } },
            { L"green",   { 0xf0fdf4, 0xdcfce7, 0xbbf7d0, 0x86efac, 0x4ade80, 0x22c55e, 0x16a34a, 0x15803d, 0x166534, 0x14532d, 0x052e16 } },
            { L"emerald", { 0xecfdf5, 0xd1fae5, 0xa7f3d0, 0x6ee7b7, 0x34d399, 0x10b981, 0x059669, 0x047857, 0x065f46, 0x064e3b, 0x022c22 } },
            { L"teal",    { 0xf0fdfa, 0xccfbf1, 0x99f6e4, 0x5eead4, 0x2dd4bf, 0x14b8a6, 0x0d9488, 0x0f766e, 0x115e59, 0x134e4a, 0x042f2e } },
            { L"cyan",    { 0xecfeff, 0xcffafe, 0xa5f3fc, 0x67e8f9, 0x22d3ee, 0x06b6d4, 0x0891b2, 0x0e7490, 0x155e75, 0x164e63, 0x083344 } },
            { L"sky",     { 0xf0f9ff, 0xe0f2fe, 0xbae6fd, 0x7dd3fc, 0x38bdf8, 0x0ea5e9, 0x0284c7, 0x0369a1, 0x075985, 0x0c4a6e, 0x082f49 } },
            { L"blue",    { 0xeff6ff, 0xdbeafe, 0xbfdbfe, 0x93c5fd, 0x60a5fa, 0x3b82f6, 0x2563eb, 0x1d4ed8, 0x1e40af, 0x1e3a8a, 0x172554 } },
            { L"indigo",  { 0xeef2ff, 0xe0e7ff, 0xc7d2fe, 0xa5b4fc, 0x818cf8, 0x6366f1, 0x4f46e5, 0x4338ca, 0x3730a3, 0x312e81, 0x1e1b4b } },
            { L"violet",  { 0xf5f3ff, 0xede9fe, 0xddd6fe, 0xc4b5fd, 0xa78bfa, 0x8b5cf6, 0x7c3aed, 0x6d28d9, 0x5b21b6, 0x4c1d95, 0x2e1065 } },
            { L"purple",  { 0xfaf5ff, 0xf3e8ff, 0xe9d5ff, 0xd8b4fe, 0xc084fc, 0xa855f7, 0x9333ea, 0x7e22ce, 0x6b21a8, 0x581c87, 0x3b0764 } },
            { L"fuchsia", { 0xfdf4ff, 0xfae8ff, 0xf5d0fe, 0xf0abfc, 0xe879f9, 0xd946ef, 0xc026d3, 0xa21caf, 0x86198f, 0x701a75, 0x4a044e } },
            { L"pink",    { 0xfdf2f8, 0xfce7f3, 0xfbcfe8, 0xf9a8d4, 0xf472b6, 0xec4899, 0xdb2777, 0xbe185d, 0x9d174d, 0x831843, 0x500724 } },
            { L"rose",    { 0xfff1f2, 0xffe4e6, 0xfecdd3, 0xfda4af, 0xfb7185, 0xf43f5e, 0xe11d48, 0xbe123c, 0x9f1239, 0x881337, 0x4c0519 } }
        };
        for (const auto& fam : fams) {
            if (name == fam.name) {
                return fam.hex[shadeIdx];
            }
        }
        return 0xFFFFFFFF;
    }

    static std::vector<TailwindCandidate> BuildCandidates() {
        std::vector<TailwindCandidate> list;

        static const wchar_t* prefixes[] = {
            L"bg-", L"text-", L"border-", L"outline-", L"ring-", L"ring-offset-",
            L"decoration-", L"placeholder-", L"caret-", L"accent-", L"fill-",
            L"stroke-", L"shadow-", L"divide-", L"from-", L"to-", L"via-"
        };
        static const wchar_t* props[] = {
            L"background-color", L"color", L"border-color", L"outline-color", L"ring-color", L"--tw-ring-offset-color",
            L"text-decoration-color", L"placeholder-color", L"caret-color", L"accent-color", L"fill",
            L"stroke", L"--tw-shadow-color", L"border-color", L"gradient-from", L"gradient-to", L"gradient-via"
        };
        static const wchar_t* famNames[] = {
            L"slate", L"gray", L"zinc", L"neutral", L"stone", L"red", L"orange",
            L"amber", L"yellow", L"lime", L"green", L"emerald", L"teal", L"cyan",
            L"sky", L"blue", L"indigo", L"violet", L"purple", L"fuchsia", L"pink", L"rose"
        };
        static const int s_shades[11] = { 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 950 };

        // 1. Color candidates across all prefixes and all shades
        for (size_t p = 0; p < (sizeof(prefixes) / sizeof(prefixes[0])); ++p) {
            std::wstring pfx = prefixes[p];
            std::wstring prop = props[p];

            // black, white, transparent
            {
                TailwindCandidate c;
                c.name = pfx + L"black";
                c.detail = prop + L": #000;";
                c.doc = L"#000000";
                c.isColor = true;
                c.hex = 0x000000;
                list.push_back(std::move(c));
            }
            {
                TailwindCandidate c;
                c.name = pfx + L"white";
                c.detail = prop + L": #fff;";
                c.doc = L"#ffffff";
                c.isColor = true;
                c.hex = 0xffffff;
                list.push_back(std::move(c));
            }
            {
                TailwindCandidate c;
                c.name = pfx + L"transparent";
                c.detail = prop + L": transparent;";
                c.doc = L"transparent";
                c.isColor = false;
                c.hex = 0;
                list.push_back(std::move(c));
            }
            {
                TailwindCandidate c;
                c.name = pfx + L"current";
                c.detail = prop + L": currentColor;";
                c.doc = L"currentColor";
                c.isColor = false;
                c.hex = 0;
                list.push_back(std::move(c));
            }
            {
                TailwindCandidate c;
                c.name = pfx + L"inherit";
                c.detail = prop + L": inherit;";
                c.doc = L"inherit";
                c.isColor = false;
                c.hex = 0;
                list.push_back(std::move(c));
            }

            for (const auto* fn : famNames) {
                for (int s = 0; s < 11; ++s) {
                    uint32_t hex = GetColorFamilyHex(fn, s);
                    uint32_t r = (hex >> 16) & 0xFF;
                    uint32_t g = (hex >> 8) & 0xFF;
                    uint32_t b = hex & 0xFF;

                    wchar_t hexBuf[16];
                    swprintf_s(hexBuf, L"#%02x%02x%02x", r, g, b);

                    TailwindCandidate tc;
                    tc.name = pfx + fn + L"-" + std::to_wstring(s_shades[s]);
                    tc.detail = prop + L": rgb(" + std::to_wstring(r) + L" " + std::to_wstring(g) + L" " + std::to_wstring(b) + L");";
                    tc.doc = hexBuf;
                    tc.isColor = true;
                    tc.hex = hex;
                    list.push_back(std::move(tc));
                }
            }
        }

        // 2. Non-color background utilities (Image 2 benchmark)
        static const struct BgUtil { const wchar_t* name; const wchar_t* detail; const wchar_t* doc; } bgUtils[] = {
            { L"bg-blend-color", L"background-blend-mode: color;", L"blend-mode" },
            { L"bg-blend-color-burn", L"background-blend-mode: color-burn;", L"blend-mode" },
            { L"bg-blend-color-dodge", L"background-blend-mode: color-dodge;", L"blend-mode" },
            { L"bg-blend-darken", L"background-blend-mode: darken;", L"blend-mode" },
            { L"bg-blend-difference", L"background-blend-mode: difference;", L"blend-mode" },
            { L"bg-blend-exclusion", L"background-blend-mode: exclusion;", L"blend-mode" },
            { L"bg-blend-hard-light", L"background-blend-mode: hard-light;", L"blend-mode" },
            { L"bg-blend-hue", L"background-blend-mode: hue;", L"blend-mode" },
            { L"bg-blend-lighten", L"background-blend-mode: lighten;", L"blend-mode" },
            { L"bg-blend-luminosity", L"background-blend-mode: luminosity;", L"blend-mode" },
            { L"bg-blend-multiply", L"background-blend-mode: multiply;", L"blend-mode" },
            { L"bg-blend-normal", L"background-blend-mode: normal;", L"blend-mode" },
            { L"bg-blend-overlay", L"background-blend-mode: overlay;", L"blend-mode" },
            { L"bg-blend-saturation", L"background-blend-mode: saturation;", L"blend-mode" },
            { L"bg-blend-screen", L"background-blend-mode: screen;", L"blend-mode" },
            { L"bg-blend-soft-light", L"background-blend-mode: soft-light;", L"blend-mode" },
            { L"bg-bottom", L"background-position: bottom;", L"position" },
            { L"bg-center", L"background-position: center;", L"position" },
            { L"bg-contain", L"background-size: contain;", L"size" },
            { L"bg-cover", L"background-size: cover;", L"size" },
            { L"bg-auto", L"background-size: auto;", L"size" },
            { L"bg-fixed", L"background-attachment: fixed;", L"attachment" },
            { L"bg-local", L"background-attachment: local;", L"attachment" },
            { L"bg-scroll", L"background-attachment: scroll;", L"attachment" },
            { L"bg-left", L"background-position: left;", L"position" },
            { L"bg-left-bottom", L"background-position: left bottom;", L"position" },
            { L"bg-left-top", L"background-position: left top;", L"position" },
            { L"bg-right", L"background-position: right;", L"position" },
            { L"bg-right-bottom", L"background-position: right bottom;", L"position" },
            { L"bg-right-top", L"background-position: right top;", L"position" },
            { L"bg-top", L"background-position: top;", L"position" },
            { L"bg-no-repeat", L"background-repeat: no-repeat;", L"repeat" },
            { L"bg-repeat", L"background-repeat: repeat;", L"repeat" },
            { L"bg-repeat-round", L"background-repeat: round;", L"repeat" },
            { L"bg-repeat-space", L"background-repeat: space;", L"repeat" },
            { L"bg-repeat-x", L"background-repeat: repeat-x;", L"repeat" },
            { L"bg-repeat-y", L"background-repeat: repeat-y;", L"repeat" },
            { L"bg-origin-border", L"background-origin: border-box;", L"origin" },
            { L"bg-origin-content", L"background-origin: content-box;", L"origin" },
            { L"bg-origin-padding", L"background-origin: padding-box;", L"origin" }
        };
        for (const auto& bu : bgUtils) {
            TailwindCandidate c;
            c.name = bu.name;
            c.detail = bu.detail;
            c.doc = bu.doc;
            c.isColor = false;
            c.hex = 0;
            list.push_back(std::move(c));
        }

        // 3. Complete Tailwind CSS v3/v4 Utility Catalog (Layout, Flex, Grid, Spacing, Sizing, Typography, Borders, Effects, Filters, Transforms, Transitions, Interactivity, SVG, Accessibility)
        auto addUtil = [&](const std::wstring& name, const std::wstring& detail, const wchar_t* doc = L"utility") {
            TailwindCandidate c;
            c.name = name;
            c.detail = detail;
            c.doc = doc;
            c.isColor = false;
            c.hex = 0;
            list.push_back(std::move(c));
        };

        // 3a. Full Spacing Scale (0..96) for padding, margin, gap, space, inset, width, height, size, translate
        struct ScaleEntry { const wchar_t* key; const wchar_t* css; };
        static const ScaleEntry spacingScale[] = {
            { L"0", L"0px" }, { L"px", L"1px" }, { L"0.5", L"0.125rem" }, { L"1", L"0.25rem" },
            { L"1.5", L"0.375rem" }, { L"2", L"0.5rem" }, { L"2.5", L"0.625rem" }, { L"3", L"0.75rem" },
            { L"3.5", L"0.875rem" }, { L"4", L"1rem" }, { L"5", L"1.25rem" }, { L"6", L"1.5rem" },
            { L"7", L"1.75rem" }, { L"8", L"2rem" }, { L"9", L"2.25rem" }, { L"10", L"2.5rem" },
            { L"11", L"2.75rem" }, { L"12", L"3rem" }, { L"14", L"3.5rem" }, { L"16", L"4rem" },
            { L"20", L"5rem" }, { L"24", L"6rem" }, { L"28", L"7rem" }, { L"32", L"8rem" },
            { L"36", L"9rem" }, { L"40", L"10rem" }, { L"44", L"11rem" }, { L"48", L"12rem" },
            { L"52", L"13rem" }, { L"56", L"14rem" }, { L"60", L"15rem" }, { L"64", L"16rem" },
            { L"72", L"18rem" }, { L"80", L"20rem" }, { L"96", L"24rem" }
        };
        struct SpacingProp { const wchar_t* pfx; const wchar_t* cssProp; bool allowAuto; bool allowNeg; };
        static const SpacingProp spacingProps[] = {
            { L"p-", L"padding", false, false },
            { L"px-", L"padding-inline", false, false },
            { L"py-", L"padding-block", false, false },
            { L"pt-", L"padding-top", false, false },
            { L"pr-", L"padding-right", false, false },
            { L"pb-", L"padding-bottom", false, false },
            { L"pl-", L"padding-left", false, false },
            { L"ps-", L"padding-inline-start", false, false },
            { L"pe-", L"padding-inline-end", false, false },
            { L"m-", L"margin", true, true },
            { L"mx-", L"margin-inline", true, true },
            { L"my-", L"margin-block", true, true },
            { L"mt-", L"margin-top", true, true },
            { L"mr-", L"margin-right", true, true },
            { L"mb-", L"margin-bottom", true, true },
            { L"ml-", L"margin-left", true, true },
            { L"ms-", L"margin-inline-start", true, true },
            { L"me-", L"margin-inline-end", true, true },
            { L"gap-", L"gap", false, false },
            { L"gap-x-", L"column-gap", false, false },
            { L"gap-y-", L"row-gap", false, false },
            { L"space-x-", L"margin-left", false, true },
            { L"space-y-", L"margin-top", false, true },
            { L"w-", L"width", true, false },
            { L"h-", L"height", true, false },
            { L"size-", L"width & height", true, false },
            { L"min-w-", L"min-width", false, false },
            { L"max-w-", L"max-width", false, false },
            { L"min-h-", L"min-height", false, false },
            { L"max-h-", L"max-height", false, false },
            { L"inset-", L"inset", true, true },
            { L"inset-x-", L"inset-inline", true, true },
            { L"inset-y-", L"inset-block", true, true },
            { L"top-", L"top", true, true },
            { L"right-", L"right", true, true },
            { L"bottom-", L"bottom", true, true },
            { L"left-", L"left", true, true },
            { L"start-", L"inset-inline-start", true, true },
            { L"end-", L"inset-inline-end", true, true },
            { L"basis-", L"flex-basis", true, false },
            { L"translate-x-", L"transform: translateX", false, true },
            { L"translate-y-", L"transform: translateY", false, true },
            { L"indent-", L"text-indent", false, true },
            { L"scroll-m-", L"scroll-margin", false, false },
            { L"scroll-p-", L"scroll-padding", false, false }
        };
        for (const auto& sp : spacingProps) {
            if (sp.allowAuto) {
                addUtil(std::wstring(sp.pfx) + L"auto", std::wstring(sp.cssProp) + L": auto;", L"spacing");
            }
            for (const auto& sc : spacingScale) {
                std::wstring nm = std::wstring(sp.pfx) + sc.key;
                std::wstring det = std::wstring(sp.cssProp) + L": " + sc.css + L";";
                addUtil(nm, det, L"spacing");
                if (sp.allowNeg && sc.key[0] != L'0') {
                    addUtil(L"-" + nm, std::wstring(sp.cssProp) + L": -" + sc.css + L";", L"spacing");
                }
            }
        }
        // Fractions & viewport sizing for w-, h-, size-, basis-, inset-, top/right/bottom/left
        static const ScaleEntry fractionScale[] = {
            { L"1/2", L"50%" }, { L"1/3", L"33.333333%" }, { L"2/3", L"66.666667%" },
            { L"1/4", L"25%" }, { L"2/4", L"50%" }, { L"3/4", L"75%" },
            { L"1/5", L"20%" }, { L"2/5", L"40%" }, { L"3/5", L"60%" }, { L"4/5", L"80%" },
            { L"1/6", L"16.666667%" }, { L"5/6", L"83.333333%" },
            { L"1/12", L"8.333333%" }, { L"5/12", L"41.666667%" }, { L"7/12", L"58.333333%" }, { L"11/12", L"91.666667%" },
            { L"full", L"100%" }, { L"min", L"min-content" }, { L"max", L"max-content" }, { L"fit", L"fit-content" }
        };
        for (const auto& fr : fractionScale) {
            addUtil(std::wstring(L"w-") + fr.key, std::wstring(L"width: ") + fr.css + L";", L"sizing");
            addUtil(std::wstring(L"h-") + fr.key, std::wstring(L"height: ") + fr.css + L";", L"sizing");
            addUtil(std::wstring(L"size-") + fr.key, std::wstring(L"width: ") + fr.css + L"; height: " + fr.css + L";", L"sizing");
            addUtil(std::wstring(L"basis-") + fr.key, std::wstring(L"flex-basis: ") + fr.css + L";", L"flexbox");
        }
        static const ScaleEntry maxWScale[] = {
            { L"none", L"none" }, { L"xs", L"20rem" }, { L"sm", L"24rem" }, { L"md", L"28rem" },
            { L"lg", L"32rem" }, { L"xl", L"36rem" }, { L"2xl", L"42rem" }, { L"3xl", L"48rem" },
            { L"4xl", L"56rem" }, { L"5xl", L"64rem" }, { L"6xl", L"72rem" }, { L"7xl", L"80rem" },
            { L"full", L"100%" }, { L"min", L"min-content" }, { L"max", L"max-content" }, { L"fit", L"fit-content" },
            { L"prose", L"65ch" }, { L"screen-sm", L"640px" }, { L"screen-md", L"768px" },
            { L"screen-lg", L"1024px" }, { L"screen-xl", L"1280px" }, { L"screen-2xl", L"1536px" }
        };
        for (const auto& mw : maxWScale) {
            addUtil(std::wstring(L"max-w-") + mw.key, std::wstring(L"max-width: ") + mw.css + L";", L"sizing");
        }

        // 3b. Grid columns/rows/span/start/end & Order
        for (int n = 1; n <= 12; ++n) {
            std::wstring ns = std::to_wstring(n);
            addUtil(L"grid-cols-" + ns, L"grid-template-columns: repeat(" + ns + L", minmax(0, 1fr));", L"grid");
            addUtil(L"grid-rows-" + ns, L"grid-template-rows: repeat(" + ns + L", minmax(0, 1fr));", L"grid");
            addUtil(L"col-span-" + ns, L"grid-column: span " + ns + L" / span " + ns + L";", L"grid");
            addUtil(L"row-span-" + ns, L"grid-row: span " + ns + L" / span " + ns + L";", L"grid");
            addUtil(L"col-start-" + ns, L"grid-column-start: " + ns + L";", L"grid");
            addUtil(L"col-end-" + ns, L"grid-column-end: " + ns + L";", L"grid");
            addUtil(L"row-start-" + ns, L"grid-row-start: " + ns + L";", L"grid");
            addUtil(L"row-end-" + ns, L"grid-row-end: " + ns + L";", L"grid");
            addUtil(L"order-" + ns, L"order: " + ns + L";", L"flexbox");
            addUtil(L"columns-" + ns, L"columns: " + ns + L";", L"layout");
        }
        addUtil(L"col-start-13", L"grid-column-start: 13;", L"grid");
        addUtil(L"col-end-13", L"grid-column-end: 13;", L"grid");
        addUtil(L"row-start-13", L"grid-row-start: 13;", L"grid");
        addUtil(L"row-end-13", L"grid-row-end: 13;", L"grid");

        // 3c. Opacity (0..100 step 5), Scale, Rotate, Duration, Delay, Z-index
        for (int op = 0; op <= 100; op += 5) {
            std::wstring os = std::to_wstring(op);
            addUtil(L"opacity-" + os, L"opacity: " + std::to_wstring(op / 100.0).substr(0, 4) + L";", L"effects");
        }
        static const int scales[] = { 0, 50, 75, 90, 95, 100, 105, 110, 125, 150 };
        for (int sc : scales) {
            std::wstring ss = std::to_wstring(sc);
            addUtil(L"scale-" + ss, L"transform: scale(" + ss + L"%);", L"transform");
            addUtil(L"scale-x-" + ss, L"transform: scaleX(" + ss + L"%);", L"transform");
            addUtil(L"scale-y-" + ss, L"transform: scaleY(" + ss + L"%);", L"transform");
        }
        static const int rots[] = { 0, 1, 2, 3, 6, 12, 45, 90, 180 };
        for (int rt : rots) {
            std::wstring rs = std::to_wstring(rt);
            addUtil(L"rotate-" + rs, L"transform: rotate(" + rs + L"deg);", L"transform");
            if (rt > 0) addUtil(L"-rotate-" + rs, L"transform: rotate(-" + rs + L"deg);", L"transform");
        }
        static const int times[] = { 0, 75, 100, 150, 200, 300, 500, 700, 1000 };
        for (int ms : times) {
            std::wstring mss = std::to_wstring(ms);
            addUtil(L"duration-" + mss, L"transition-duration: " + mss + L"ms;", L"transition");
            addUtil(L"delay-" + mss, L"transition-delay: " + mss + L"ms;", L"transition");
        }
        static const int zVals[] = { 0, 10, 20, 30, 40, 50 };
        for (int zv : zVals) {
            std::wstring zs = std::to_wstring(zv);
            addUtil(L"z-" + zs, L"z-index: " + zs + L";", L"layout");
        }

        // 3d. Rounded corners across all sides & sizes
        static const wchar_t* roundDirs[] = { L"", L"-t", L"-r", L"-b", L"-l", L"-tl", L"-tr", L"-br", L"-bl", L"-s", L"-e" };
        static const ScaleEntry roundSizes[] = {
            { L"-none", L"0px" }, { L"-sm", L"0.125rem" }, { L"", L"0.25rem" }, { L"-md", L"0.375rem" },
            { L"-lg", L"0.5rem" }, { L"-xl", L"0.75rem" }, { L"-2xl", L"1rem" }, { L"-3xl", L"1.5rem" }, { L"-full", L"9999px" }
        };
        for (const wchar_t* rd : roundDirs) {
            for (const auto& rs : roundSizes) {
                addUtil(std::wstring(L"rounded") + rd + rs.key, std::wstring(L"border-radius: ") + rs.css + L";", L"border");
            }
        }

        // 3e. Border widths across all sides
        static const wchar_t* borderSides[] = { L"", L"-t", L"-r", L"-b", L"-l", L"-x", L"-y", L"-s", L"-e" };
        static const ScaleEntry borderWidths[] = {
            { L"", L"1px" }, { L"-0", L"0px" }, { L"-2", L"2px" }, { L"-4", L"4px" }, { L"-8", L"8px" }
        };
        for (const wchar_t* bs : borderSides) {
            for (const auto& bw : borderWidths) {
                addUtil(std::wstring(L"border") + bs + bw.key, std::wstring(L"border-width: ") + bw.css + L";", L"border");
            }
        }

        // 3f. Complete General Utilities (Typography, Text Decoration, All Cursors, Layout, Flex, Grid, Borders, Shadows, Filters, Interactivity, Accessibility)
        static const struct GeneralUtil { const wchar_t* name; const wchar_t* detail; } genUtils[] = {
            // Display & Layout
            { L"container", L"width: 100%; (responsive max-width)" },
            { L"block", L"display: block;" },
            { L"inline-block", L"display: inline-block;" },
            { L"inline", L"display: inline;" },
            { L"flex", L"display: flex;" },
            { L"inline-flex", L"display: inline-flex;" },
            { L"table", L"display: table;" },
            { L"inline-table", L"display: inline-table;" },
            { L"table-caption", L"display: table-caption;" },
            { L"table-cell", L"display: table-cell;" },
            { L"table-column", L"display: table-column;" },
            { L"table-row", L"display: table-row;" },
            { L"flow-root", L"display: flow-root;" },
            { L"grid", L"display: grid;" },
            { L"inline-grid", L"display: inline-grid;" },
            { L"contents", L"display: contents;" },
            { L"list-item", L"display: list-item;" },
            { L"hidden", L"display: none;" },
            { L"static", L"position: static;" },
            { L"fixed", L"position: fixed;" },
            { L"absolute", L"position: absolute;" },
            { L"relative", L"position: relative;" },
            { L"sticky", L"position: sticky;" },
            { L"visible", L"visibility: visible;" },
            { L"invisible", L"visibility: hidden;" },
            { L"collapse", L"visibility: collapse;" },
            { L"isolate", L"isolation: isolate;" },
            { L"isolation-auto", L"isolation: auto;" },
            { L"z-auto", L"z-index: auto;" },
            { L"box-border", L"box-sizing: border-box;" },
            { L"box-content", L"box-sizing: content-box;" },
            { L"float-start", L"float: inline-start;" },
            { L"float-end", L"float: inline-end;" },
            { L"float-right", L"float: right;" },
            { L"float-left", L"float: left;" },
            { L"float-none", L"float: none;" },
            { L"clear-start", L"clear: inline-start;" },
            { L"clear-end", L"clear: inline-end;" },
            { L"clear-left", L"clear: left;" },
            { L"clear-right", L"clear: right;" },
            { L"clear-both", L"clear: both;" },
            { L"clear-none", L"clear: none;" },
            { L"aspect-auto", L"aspect-ratio: auto;" },
            { L"aspect-square", L"aspect-ratio: 1 / 1;" },
            { L"aspect-video", L"aspect-ratio: 16 / 9;" },
            { L"object-contain", L"object-fit: contain;" },
            { L"object-cover", L"object-fit: cover;" },
            { L"object-fill", L"object-fit: fill;" },
            { L"object-none", L"object-fit: none;" },
            { L"object-scale-down", L"object-fit: scale-down;" },
            { L"object-center", L"object-position: center;" },
            { L"object-top", L"object-position: top;" },
            { L"object-bottom", L"object-position: bottom;" },
            { L"object-left", L"object-position: left;" },
            { L"object-right", L"object-position: right;" },
            { L"overflow-auto", L"overflow: auto;" },
            { L"overflow-hidden", L"overflow: hidden;" },
            { L"overflow-clip", L"overflow: clip;" },
            { L"overflow-visible", L"overflow: visible;" },
            { L"overflow-scroll", L"overflow: scroll;" },
            { L"overflow-x-auto", L"overflow-x: auto;" },
            { L"overflow-y-auto", L"overflow-y: auto;" },
            { L"overflow-x-hidden", L"overflow-x: hidden;" },
            { L"overflow-y-hidden", L"overflow-y: hidden;" },
            { L"overflow-x-scroll", L"overflow-x: scroll;" },
            { L"overflow-y-scroll", L"overflow-y: scroll;" },
            { L"overscroll-auto", L"overscroll-behavior: auto;" },
            { L"overscroll-contain", L"overscroll-behavior: contain;" },
            { L"overscroll-none", L"overscroll-behavior: none;" },

            // Flexbox & Grid Alignment
            { L"flex-row", L"flex-direction: row;" },
            { L"flex-row-reverse", L"flex-direction: row-reverse;" },
            { L"flex-col", L"flex-direction: column;" },
            { L"flex-col-reverse", L"flex-direction: column-reverse;" },
            { L"flex-wrap", L"flex-wrap: wrap;" },
            { L"flex-wrap-reverse", L"flex-wrap: wrap-reverse;" },
            { L"flex-nowrap", L"flex-wrap: nowrap;" },
            { L"flex-1", L"flex: 1 1 0%;" },
            { L"flex-auto", L"flex: 1 1 auto;" },
            { L"flex-initial", L"flex: 0 1 auto;" },
            { L"flex-none", L"flex: none;" },
            { L"grow", L"flex-grow: 1;" },
            { L"grow-0", L"flex-grow: 0;" },
            { L"shrink", L"flex-shrink: 1;" },
            { L"shrink-0", L"flex-shrink: 0;" },
            { L"order-first", L"order: -9999;" },
            { L"order-last", L"order: 9999;" },
            { L"order-none", L"order: 0;" },
            { L"grid-cols-none", L"grid-template-columns: none;" },
            { L"grid-cols-subgrid", L"grid-template-columns: subgrid;" },
            { L"grid-rows-none", L"grid-template-rows: none;" },
            { L"grid-rows-subgrid", L"grid-template-rows: subgrid;" },
            { L"col-auto", L"grid-column: auto;" },
            { L"col-span-full", L"grid-column: 1 / -1;" },
            { L"row-auto", L"grid-row: auto;" },
            { L"row-span-full", L"grid-row: 1 / -1;" },
            { L"grid-flow-row", L"grid-auto-flow: row;" },
            { L"grid-flow-col", L"grid-auto-flow: column;" },
            { L"grid-flow-dense", L"grid-auto-flow: dense;" },
            { L"grid-flow-row-dense", L"grid-auto-flow: row dense;" },
            { L"grid-flow-col-dense", L"grid-auto-flow: column dense;" },
            { L"auto-cols-auto", L"grid-auto-columns: auto;" },
            { L"auto-cols-min", L"grid-auto-columns: min-content;" },
            { L"auto-cols-max", L"grid-auto-columns: max-content;" },
            { L"auto-cols-fr", L"grid-auto-columns: minmax(0, 1fr);" },
            { L"auto-rows-auto", L"grid-auto-rows: auto;" },
            { L"auto-rows-min", L"grid-auto-rows: min-content;" },
            { L"auto-rows-max", L"grid-auto-rows: max-content;" },
            { L"auto-rows-fr", L"grid-auto-rows: minmax(0, 1fr);" },
            { L"justify-normal", L"justify-content: normal;" },
            { L"justify-start", L"justify-content: flex-start;" },
            { L"justify-end", L"justify-content: flex-end;" },
            { L"justify-center", L"justify-content: center;" },
            { L"justify-between", L"justify-content: space-between;" },
            { L"justify-around", L"justify-content: space-around;" },
            { L"justify-evenly", L"justify-content: space-evenly;" },
            { L"justify-stretch", L"justify-content: stretch;" },
            { L"justify-items-start", L"justify-items: start;" },
            { L"justify-items-end", L"justify-items: end;" },
            { L"justify-items-center", L"justify-items: center;" },
            { L"justify-items-stretch", L"justify-items: stretch;" },
            { L"justify-self-auto", L"justify-self: auto;" },
            { L"justify-self-start", L"justify-self: start;" },
            { L"justify-self-end", L"justify-self: end;" },
            { L"justify-self-center", L"justify-self: center;" },
            { L"justify-self-stretch", L"justify-self: stretch;" },
            { L"content-normal", L"align-content: normal;" },
            { L"content-center", L"align-content: center;" },
            { L"content-start", L"align-content: flex-start;" },
            { L"content-end", L"align-content: flex-end;" },
            { L"content-between", L"align-content: space-between;" },
            { L"content-around", L"align-content: space-around;" },
            { L"content-evenly", L"align-content: space-evenly;" },
            { L"content-baseline", L"align-content: baseline;" },
            { L"content-stretch", L"align-content: stretch;" },
            { L"items-start", L"align-items: flex-start;" },
            { L"items-end", L"align-items: flex-end;" },
            { L"items-center", L"align-items: center;" },
            { L"items-baseline", L"align-items: baseline;" },
            { L"items-stretch", L"align-items: stretch;" },
            { L"self-auto", L"align-self: auto;" },
            { L"self-start", L"align-self: flex-start;" },
            { L"self-end", L"align-self: flex-end;" },
            { L"self-center", L"align-self: center;" },
            { L"self-stretch", L"align-self: stretch;" },
            { L"self-baseline", L"align-self: baseline;" },
            { L"place-content-center", L"place-content: center;" },
            { L"place-content-start", L"place-content: start;" },
            { L"place-content-end", L"place-content: end;" },
            { L"place-content-between", L"place-content: space-between;" },
            { L"place-content-around", L"place-content: space-around;" },
            { L"place-content-evenly", L"place-content: space-evenly;" },
            { L"place-content-stretch", L"place-content: stretch;" },
            { L"place-items-start", L"place-items: start;" },
            { L"place-items-end", L"place-items: end;" },
            { L"place-items-center", L"place-items: center;" },
            { L"place-items-stretch", L"place-items: stretch;" },
            { L"place-self-auto", L"place-self: auto;" },
            { L"place-self-start", L"place-self: start;" },
            { L"place-self-end", L"place-self: end;" },
            { L"place-self-center", L"place-self: center;" },
            { L"place-self-stretch", L"place-self: stretch;" },
            { L"space-x-reverse", L"--tw-space-x-reverse: 1;" },
            { L"space-y-reverse", L"--tw-space-y-reverse: 1;" },

            // Viewport Sizing
            { L"w-screen", L"width: 100vw;" },
            { L"w-svw", L"width: 100svw;" },
            { L"w-lvw", L"width: 100lvw;" },
            { L"w-dvw", L"width: 100dvw;" },
            { L"h-screen", L"height: 100vh;" },
            { L"h-svh", L"height: 100svh;" },
            { L"h-lvh", L"height: 100lvh;" },
            { L"h-dvh", L"height: 100dvh;" },
            { L"min-h-screen", L"min-height: 100vh;" },
            { L"min-h-svh", L"min-height: 100svh;" },
            { L"min-h-lvh", L"min-height: 100lvh;" },
            { L"min-h-dvh", L"min-height: 100dvh;" },
            { L"min-h-full", L"min-height: 100%;" },
            { L"min-h-fit", L"min-height: fit-content;" },
            { L"max-h-screen", L"max-height: 100vh;" },
            { L"max-h-full", L"max-height: 100%;" },
            { L"max-h-fit", L"max-height: fit-content;" },
            { L"min-w-full", L"min-width: 100%;" },
            { L"min-w-min", L"min-width: min-content;" },
            { L"min-w-max", L"min-width: max-content;" },
            { L"min-w-fit", L"min-width: fit-content;" },

            // Typography & Text Decoration
            { L"font-sans", L"font-family: ui-sans-serif, system-ui, sans-serif;" },
            { L"font-serif", L"font-family: ui-serif, Georgia, serif;" },
            { L"font-mono", L"font-family: ui-monospace, SFMono-Regular, monospace;" },
            { L"text-xs", L"font-size: 0.75rem; line-height: 1rem;" },
            { L"text-sm", L"font-size: 0.875rem; line-height: 1.25rem;" },
            { L"text-base", L"font-size: 1rem; line-height: 1.5rem;" },
            { L"text-lg", L"font-size: 1.125rem; line-height: 1.75rem;" },
            { L"text-xl", L"font-size: 1.25rem; line-height: 1.75rem;" },
            { L"text-2xl", L"font-size: 1.5rem; line-height: 2rem;" },
            { L"text-3xl", L"font-size: 1.875rem; line-height: 2.25rem;" },
            { L"text-4xl", L"font-size: 2.25rem; line-height: 2.5rem;" },
            { L"text-5xl", L"font-size: 3rem; line-height: 1;" },
            { L"text-6xl", L"font-size: 3.75rem; line-height: 1;" },
            { L"text-7xl", L"font-size: 4.5rem; line-height: 1;" },
            { L"text-8xl", L"font-size: 6rem; line-height: 1;" },
            { L"text-9xl", L"font-size: 8rem; line-height: 1;" },
            { L"antialiased", L"-webkit-font-smoothing: antialiased; -moz-osx-font-smoothing: grayscale;" },
            { L"subpixel-antialiased", L"-webkit-font-smoothing: auto; -moz-osx-font-smoothing: auto;" },
            { L"italic", L"font-style: italic;" },
            { L"not-italic", L"font-style: normal;" },
            { L"font-thin", L"font-weight: 100;" },
            { L"font-extralight", L"font-weight: 200;" },
            { L"font-light", L"font-weight: 300;" },
            { L"font-normal", L"font-weight: 400;" },
            { L"font-medium", L"font-weight: 500;" },
            { L"font-semibold", L"font-weight: 600;" },
            { L"font-bold", L"font-weight: 700;" },
            { L"font-extrabold", L"font-weight: 800;" },
            { L"font-black", L"font-weight: 900;" },
            { L"tabular-nums", L"font-variant-numeric: tabular-nums;" },
            { L"lining-nums", L"font-variant-numeric: lining-nums;" },
            { L"oldstyle-nums", L"font-variant-numeric: oldstyle-nums;" },
            { L"proportional-nums", L"font-variant-numeric: proportional-nums;" },
            { L"ordinal", L"font-variant-numeric: ordinal;" },
            { L"slashed-zero", L"font-variant-numeric: slashed-zero;" },
            { L"tracking-tighter", L"letter-spacing: -0.05em;" },
            { L"tracking-tight", L"letter-spacing: -0.025em;" },
            { L"tracking-normal", L"letter-spacing: 0em;" },
            { L"tracking-wide", L"letter-spacing: 0.025em;" },
            { L"tracking-wider", L"letter-spacing: 0.05em;" },
            { L"tracking-widest", L"letter-spacing: 0.1em;" },
            { L"leading-none", L"line-height: 1;" },
            { L"leading-tight", L"line-height: 1.25;" },
            { L"leading-snug", L"line-height: 1.375;" },
            { L"leading-normal", L"line-height: 1.5;" },
            { L"leading-relaxed", L"line-height: 1.625;" },
            { L"leading-loose", L"line-height: 2;" },
            { L"leading-3", L"line-height: 0.75rem;" },
            { L"leading-4", L"line-height: 1rem;" },
            { L"leading-5", L"line-height: 1.25rem;" },
            { L"leading-6", L"line-height: 1.5rem;" },
            { L"leading-7", L"line-height: 1.75rem;" },
            { L"leading-8", L"line-height: 2rem;" },
            { L"leading-9", L"line-height: 2.25rem;" },
            { L"leading-10", L"line-height: 2.5rem;" },
            { L"line-clamp-1", L"overflow: hidden; -webkit-line-clamp: 1;" },
            { L"line-clamp-2", L"overflow: hidden; -webkit-line-clamp: 2;" },
            { L"line-clamp-3", L"overflow: hidden; -webkit-line-clamp: 3;" },
            { L"line-clamp-4", L"overflow: hidden; -webkit-line-clamp: 4;" },
            { L"line-clamp-5", L"overflow: hidden; -webkit-line-clamp: 5;" },
            { L"line-clamp-6", L"overflow: hidden; -webkit-line-clamp: 6;" },
            { L"line-clamp-none", L"overflow: visible; -webkit-line-clamp: none;" },
            { L"list-none", L"list-style-type: none;" },
            { L"list-disc", L"list-style-type: disc;" },
            { L"list-decimal", L"list-style-type: decimal;" },
            { L"list-inside", L"list-style-position: inside;" },
            { L"list-outside", L"list-style-position: outside;" },
            { L"text-left", L"text-align: left;" },
            { L"text-center", L"text-align: center;" },
            { L"text-right", L"text-align: right;" },
            { L"text-justify", L"text-align: justify;" },
            { L"text-start", L"text-align: start;" },
            { L"text-end", L"text-align: end;" },
            { L"underline", L"text-decoration-line: underline;" },
            { L"overline", L"text-decoration-line: overline;" },
            { L"line-through", L"text-decoration-line: line-through;" },
            { L"no-underline", L"text-decoration-line: none;" },
            { L"decoration-solid", L"text-decoration-style: solid;" },
            { L"decoration-double", L"text-decoration-style: double;" },
            { L"decoration-dotted", L"text-decoration-style: dotted;" },
            { L"decoration-dashed", L"text-decoration-style: dashed;" },
            { L"decoration-wavy", L"text-decoration-style: wavy;" },
            { L"decoration-auto", L"text-decoration-thickness: auto;" },
            { L"decoration-from-font", L"text-decoration-thickness: from-font;" },
            { L"decoration-0", L"text-decoration-thickness: 0px;" },
            { L"decoration-1", L"text-decoration-thickness: 1px;" },
            { L"decoration-2", L"text-decoration-thickness: 2px;" },
            { L"decoration-4", L"text-decoration-thickness: 4px;" },
            { L"decoration-8", L"text-decoration-thickness: 8px;" },
            { L"underline-offset-auto", L"text-underline-offset: auto;" },
            { L"underline-offset-0", L"text-underline-offset: 0px;" },
            { L"underline-offset-1", L"text-underline-offset: 1px;" },
            { L"underline-offset-2", L"text-underline-offset: 2px;" },
            { L"underline-offset-4", L"text-underline-offset: 4px;" },
            { L"underline-offset-8", L"text-underline-offset: 8px;" },
            { L"uppercase", L"text-transform: uppercase;" },
            { L"lowercase", L"text-transform: lowercase;" },
            { L"capitalize", L"text-transform: capitalize;" },
            { L"normal-case", L"text-transform: none;" },
            { L"truncate", L"overflow: hidden; text-overflow: ellipsis; white-space: nowrap;" },
            { L"text-ellipsis", L"text-overflow: ellipsis;" },
            { L"text-clip", L"text-overflow: clip;" },
            { L"text-wrap", L"text-wrap: wrap;" },
            { L"text-nowrap", L"text-wrap: nowrap;" },
            { L"text-balance", L"text-wrap: balance;" },
            { L"text-pretty", L"text-wrap: pretty;" },
            { L"align-baseline", L"vertical-align: baseline;" },
            { L"align-top", L"vertical-align: top;" },
            { L"align-middle", L"vertical-align: middle;" },
            { L"align-bottom", L"vertical-align: bottom;" },
            { L"align-text-top", L"vertical-align: text-top;" },
            { L"align-text-bottom", L"vertical-align: text-bottom;" },
            { L"align-sub", L"vertical-align: sub;" },
            { L"align-super", L"vertical-align: super;" },
            { L"whitespace-normal", L"white-space: normal;" },
            { L"whitespace-nowrap", L"white-space: nowrap;" },
            { L"whitespace-pre", L"white-space: pre;" },
            { L"whitespace-pre-line", L"white-space: pre-line;" },
            { L"whitespace-pre-wrap", L"white-space: pre-wrap;" },
            { L"whitespace-break-spaces", L"white-space: break-spaces;" },
            { L"break-normal", L"overflow-wrap: normal; word-break: normal;" },
            { L"break-words", L"overflow-wrap: break-word;" },
            { L"break-all", L"word-break: break-all;" },
            { L"break-keep", L"word-break: keep-all;" },
            { L"hyphens-none", L"hyphens: none;" },
            { L"hyphens-manual", L"hyphens: manual;" },
            { L"hyphens-auto", L"hyphens: auto;" },

            // Gradients
            { L"bg-none", L"background-image: none;" },
            { L"bg-gradient-to-t", L"background-image: linear-gradient(to top, var(--tw-gradient-stops));" },
            { L"bg-gradient-to-tr", L"background-image: linear-gradient(to top right, var(--tw-gradient-stops));" },
            { L"bg-gradient-to-r", L"background-image: linear-gradient(to right, var(--tw-gradient-stops));" },
            { L"bg-gradient-to-br", L"background-image: linear-gradient(to bottom right, var(--tw-gradient-stops));" },
            { L"bg-gradient-to-b", L"background-image: linear-gradient(to bottom, var(--tw-gradient-stops));" },
            { L"bg-gradient-to-bl", L"background-image: linear-gradient(to bottom left, var(--tw-gradient-stops));" },
            { L"bg-gradient-to-l", L"background-image: linear-gradient(to left, var(--tw-gradient-stops));" },
            { L"bg-gradient-to-tl", L"background-image: linear-gradient(to top left, var(--tw-gradient-stops));" },

            // Borders, Dividers, Outlines, Rings
            { L"border-solid", L"border-style: solid;" },
            { L"border-dashed", L"border-style: dashed;" },
            { L"border-dotted", L"border-style: dotted;" },
            { L"border-double", L"border-style: double;" },
            { L"border-hidden", L"border-style: hidden;" },
            { L"border-none", L"border-style: none;" },
            { L"divide-x", L"border-right-width: 0px; border-left-width: 1px;" },
            { L"divide-x-0", L"border-right-width: 0px; border-left-width: 0px;" },
            { L"divide-x-2", L"border-right-width: 0px; border-left-width: 2px;" },
            { L"divide-x-4", L"border-right-width: 0px; border-left-width: 4px;" },
            { L"divide-y", L"border-top-width: 1px; border-bottom-width: 0px;" },
            { L"divide-y-0", L"border-top-width: 0px; border-bottom-width: 0px;" },
            { L"divide-y-2", L"border-top-width: 2px; border-bottom-width: 0px;" },
            { L"divide-y-4", L"border-top-width: 4px; border-bottom-width: 0px;" },
            { L"divide-solid", L"border-style: solid;" },
            { L"divide-dashed", L"border-style: dashed;" },
            { L"divide-dotted", L"border-style: dotted;" },
            { L"divide-none", L"border-style: none;" },
            { L"outline-none", L"outline: 2px solid transparent; outline-offset: 2px;" },
            { L"outline", L"outline-style: solid;" },
            { L"outline-dashed", L"outline-style: dashed;" },
            { L"outline-dotted", L"outline-style: dotted;" },
            { L"outline-double", L"outline-style: double;" },
            { L"outline-0", L"outline-width: 0px;" },
            { L"outline-1", L"outline-width: 1px;" },
            { L"outline-2", L"outline-width: 2px;" },
            { L"outline-4", L"outline-width: 4px;" },
            { L"outline-8", L"outline-width: 8px;" },
            { L"outline-offset-0", L"outline-offset: 0px;" },
            { L"outline-offset-1", L"outline-offset: 1px;" },
            { L"outline-offset-2", L"outline-offset: 2px;" },
            { L"outline-offset-4", L"outline-offset: 4px;" },
            { L"outline-offset-8", L"outline-offset: 8px;" },
            { L"ring", L"box-shadow: 0 0 0 3px var(--tw-ring-color);" },
            { L"ring-0", L"box-shadow: 0 0 0 0px var(--tw-ring-color);" },
            { L"ring-1", L"box-shadow: 0 0 0 1px var(--tw-ring-color);" },
            { L"ring-2", L"box-shadow: 0 0 0 2px var(--tw-ring-color);" },
            { L"ring-4", L"box-shadow: 0 0 0 4px var(--tw-ring-color);" },
            { L"ring-8", L"box-shadow: 0 0 0 8px var(--tw-ring-color);" },
            { L"ring-inset", L"--tw-ring-inset: inset;" },
            { L"ring-offset-0", L"--tw-ring-offset-width: 0px;" },
            { L"ring-offset-1", L"--tw-ring-offset-width: 1px;" },
            { L"ring-offset-2", L"--tw-ring-offset-width: 2px;" },
            { L"ring-offset-4", L"--tw-ring-offset-width: 4px;" },
            { L"ring-offset-8", L"--tw-ring-offset-width: 8px;" },

            // Shadows, Filters & Backdrop Filters
            { L"shadow-sm", L"box-shadow: 0 1px 2px 0 rgb(0 0 0 / 0.05);" },
            { L"shadow", L"box-shadow: 0 1px 3px 0 rgb(0 0 0 / 0.1);" },
            { L"shadow-md", L"box-shadow: 0 4px 6px -1px rgb(0 0 0 / 0.1);" },
            { L"shadow-lg", L"box-shadow: 0 10px 15px -3px rgb(0 0 0 / 0.1);" },
            { L"shadow-xl", L"box-shadow: 0 20px 25px -5px rgb(0 0 0 / 0.1);" },
            { L"shadow-2xl", L"box-shadow: 0 25px 50px -12px rgb(0 0 0 / 0.25);" },
            { L"shadow-inner", L"box-shadow: inset 0 2px 4px 0 rgb(0 0 0 / 0.05);" },
            { L"shadow-none", L"box-shadow: 0 0 #0000;" },
            { L"blur-none", L"filter: blur(0);" },
            { L"blur-sm", L"filter: blur(4px);" },
            { L"blur", L"filter: blur(8px);" },
            { L"blur-md", L"filter: blur(12px);" },
            { L"blur-lg", L"filter: blur(16px);" },
            { L"blur-xl", L"filter: blur(24px);" },
            { L"blur-2xl", L"filter: blur(40px);" },
            { L"blur-3xl", L"filter: blur(64px);" },
            { L"backdrop-blur-none", L"backdrop-filter: blur(0);" },
            { L"backdrop-blur-sm", L"backdrop-filter: blur(4px);" },
            { L"backdrop-blur", L"backdrop-filter: blur(8px);" },
            { L"backdrop-blur-md", L"backdrop-filter: blur(12px);" },
            { L"backdrop-blur-lg", L"backdrop-filter: blur(16px);" },
            { L"backdrop-blur-xl", L"backdrop-filter: blur(24px);" },
            { L"backdrop-blur-2xl", L"backdrop-filter: blur(40px);" },
            { L"backdrop-blur-3xl", L"backdrop-filter: blur(64px);" },
            { L"grayscale", L"filter: grayscale(100%);" },
            { L"grayscale-0", L"filter: grayscale(0);" },
            { L"invert", L"filter: invert(100%);" },
            { L"invert-0", L"filter: invert(0);" },
            { L"sepia", L"filter: sepia(100%);" },
            { L"sepia-0", L"filter: sepia(0);" },
            { L"drop-shadow-sm", L"filter: drop-shadow(0 1px 1px rgb(0 0 0 / 0.05));" },
            { L"drop-shadow", L"filter: drop-shadow(0 1px 2px rgb(0 0 0 / 0.1));" },
            { L"drop-shadow-md", L"filter: drop-shadow(0 4px 3px rgb(0 0 0 / 0.07));" },
            { L"drop-shadow-lg", L"filter: drop-shadow(0 10px 8px rgb(0 0 0 / 0.04));" },
            { L"drop-shadow-xl", L"filter: drop-shadow(0 20px 13px rgb(0 0 0 / 0.03));" },
            { L"drop-shadow-2xl", L"filter: drop-shadow(0 25px 25px rgb(0 0 0 / 0.15));" },
            { L"drop-shadow-none", L"filter: drop-shadow(0 0 #0000);" },
            { L"mix-blend-normal", L"mix-blend-mode: normal;" },
            { L"mix-blend-multiply", L"mix-blend-mode: multiply;" },
            { L"mix-blend-screen", L"mix-blend-mode: screen;" },
            { L"mix-blend-overlay", L"mix-blend-mode: overlay;" },
            { L"mix-blend-darken", L"mix-blend-mode: darken;" },
            { L"mix-blend-lighten", L"mix-blend-mode: lighten;" },
            { L"mix-blend-difference", L"mix-blend-mode: difference;" },

            // Transitions, Transforms & Animations
            { L"transition-none", L"transition-property: none;" },
            { L"transition-all", L"transition-property: all;" },
            { L"transition", L"transition-property: color, background-color, border-color, text-decoration-color, fill, stroke, opacity, box-shadow, transform, filter, backdrop-filter;" },
            { L"transition-colors", L"transition-property: color, background-color, border-color, text-decoration-color, fill, stroke;" },
            { L"transition-opacity", L"transition-property: opacity;" },
            { L"transition-shadow", L"transition-property: box-shadow;" },
            { L"transition-transform", L"transition-property: transform;" },
            { L"ease-linear", L"transition-timing-function: linear;" },
            { L"ease-in", L"transition-timing-function: cubic-bezier(0.4, 0, 1, 1);" },
            { L"ease-out", L"transition-timing-function: cubic-bezier(0, 0, 0.2, 1);" },
            { L"ease-in-out", L"transition-timing-function: cubic-bezier(0.4, 0, 0.2, 1);" },
            { L"animate-none", L"animation: none;" },
            { L"animate-spin", L"animation: spin 1s linear infinite;" },
            { L"animate-ping", L"animation: ping 1s cubic-bezier(0, 0, 0.2, 1) infinite;" },
            { L"animate-pulse", L"animation: pulse 2s cubic-bezier(0.4, 0, 0.6, 1) infinite;" },
            { L"animate-bounce", L"animation: bounce 1s infinite;" },
            { L"origin-center", L"transform-origin: center;" },
            { L"origin-top", L"transform-origin: top;" },
            { L"origin-top-right", L"transform-origin: top right;" },
            { L"origin-right", L"transform-origin: right;" },
            { L"origin-bottom-right", L"transform-origin: bottom right;" },
            { L"origin-bottom", L"transform-origin: bottom;" },
            { L"origin-bottom-left", L"transform-origin: bottom left;" },
            { L"origin-left", L"transform-origin: left;" },
            { L"origin-top-left", L"transform-origin: top left;" },

            // Complete Cursor Utilities (all 36 standard Tailwind cursor-* classes)
            { L"cursor-auto", L"cursor: auto;" },
            { L"cursor-default", L"cursor: default;" },
            { L"cursor-pointer", L"cursor: pointer;" },
            { L"cursor-wait", L"cursor: wait;" },
            { L"cursor-text", L"cursor: text;" },
            { L"cursor-move", L"cursor: move;" },
            { L"cursor-help", L"cursor: help;" },
            { L"cursor-not-allowed", L"cursor: not-allowed;" },
            { L"cursor-none", L"cursor: none;" },
            { L"cursor-context-menu", L"cursor: context-menu;" },
            { L"cursor-progress", L"cursor: progress;" },
            { L"cursor-cell", L"cursor: cell;" },
            { L"cursor-crosshair", L"cursor: crosshair;" },
            { L"cursor-vertical-text", L"cursor: vertical-text;" },
            { L"cursor-alias", L"cursor: alias;" },
            { L"cursor-copy", L"cursor: copy;" },
            { L"cursor-no-drop", L"cursor: no-drop;" },
            { L"cursor-grab", L"cursor: grab;" },
            { L"cursor-grabbing", L"cursor: grabbing;" },
            { L"cursor-all-scroll", L"cursor: all-scroll;" },
            { L"cursor-col-resize", L"cursor: col-resize;" },
            { L"cursor-row-resize", L"cursor: row-resize;" },
            { L"cursor-n-resize", L"cursor: n-resize;" },
            { L"cursor-e-resize", L"cursor: e-resize;" },
            { L"cursor-s-resize", L"cursor: s-resize;" },
            { L"cursor-w-resize", L"cursor: w-resize;" },
            { L"cursor-ne-resize", L"cursor: ne-resize;" },
            { L"cursor-nw-resize", L"cursor: nw-resize;" },
            { L"cursor-se-resize", L"cursor: se-resize;" },
            { L"cursor-sw-resize", L"cursor: sw-resize;" },
            { L"cursor-ew-resize", L"cursor: ew-resize;" },
            { L"cursor-ns-resize", L"cursor: ns-resize;" },
            { L"cursor-nesw-resize", L"cursor: nesw-resize;" },
            { L"cursor-nwse-resize", L"cursor: nwse-resize;" },
            { L"cursor-zoom-in", L"cursor: zoom-in;" },
            { L"cursor-zoom-out", L"cursor: zoom-out;" },

            // Interactivity, Scroll, Snap, Tables, SVG & Accessibility
            { L"select-none", L"user-select: none;" },
            { L"select-text", L"user-select: text;" },
            { L"select-all", L"user-select: all;" },
            { L"select-auto", L"user-select: auto;" },
            { L"pointer-events-none", L"pointer-events: none;" },
            { L"pointer-events-auto", L"pointer-events: auto;" },
            { L"resize-none", L"resize: none;" },
            { L"resize-y", L"resize: vertical;" },
            { L"resize-x", L"resize: horizontal;" },
            { L"resize", L"resize: both;" },
            { L"appearance-none", L"appearance: none;" },
            { L"appearance-auto", L"appearance: auto;" },
            { L"scroll-auto", L"scroll-behavior: auto;" },
            { L"scroll-smooth", L"scroll-behavior: smooth;" },
            { L"snap-none", L"scroll-snap-type: none;" },
            { L"snap-x", L"scroll-snap-type: x var(--tw-scroll-snap-strictness);" },
            { L"snap-y", L"scroll-snap-type: y var(--tw-scroll-snap-strictness);" },
            { L"snap-both", L"scroll-snap-type: both var(--tw-scroll-snap-strictness);" },
            { L"snap-mandatory", L"--tw-scroll-snap-strictness: mandatory;" },
            { L"snap-proximity", L"--tw-scroll-snap-strictness: proximity;" },
            { L"snap-start", L"scroll-snap-align: start;" },
            { L"snap-end", L"scroll-snap-align: end;" },
            { L"snap-center", L"scroll-snap-align: center;" },
            { L"touch-auto", L"touch-action: auto;" },
            { L"touch-none", L"touch-action: none;" },
            { L"touch-pan-x", L"touch-action: pan-x;" },
            { L"touch-pan-y", L"touch-action: pan-y;" },
            { L"touch-manipulation", L"touch-action: manipulation;" },
            { L"will-change-auto", L"will-change: auto;" },
            { L"will-change-scroll", L"will-change: scroll-position;" },
            { L"will-change-contents", L"will-change: contents;" },
            { L"will-change-transform", L"will-change: transform;" },
            { L"table-auto", L"table-layout: auto;" },
            { L"table-fixed", L"table-layout: fixed;" },
            { L"border-collapse", L"border-collapse: collapse;" },
            { L"border-separate", L"border-collapse: separate;" },
            { L"stroke-0", L"stroke-width: 0;" },
            { L"stroke-1", L"stroke-width: 1;" },
            { L"stroke-2", L"stroke-width: 2;" },
            { L"sr-only", L"position: absolute; width: 1px; height: 1px; padding: 0; margin: -1px; overflow: hidden; clip: rect(0,0,0,0); white-space: nowrap; border-width: 0;" },
            { L"not-sr-only", L"position: static; width: auto; height: auto; padding: 0; margin: 0; overflow: visible; clip: auto; white-space: normal;" },
            { L"group", L"Marker class for styling children based on parent state (group-hover:*, group-focus:*)" },
            { L"peer", L"Marker class for styling siblings based on sibling state (peer-checked:*, peer-focus:*)" }
        };
        for (const auto& gu : genUtils) {
            addUtil(gu.name, gu.detail, L"utility");
        }

        return list;
    }
};

} // namespace phide::editor
