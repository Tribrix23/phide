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

        // 3. Workspace root check with caching (valid for 2 seconds)
        if (!workspaceRoot.empty()) {
            auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(s_cacheMutex);
                if (s_cachedRoot == workspaceRoot && (now - s_lastCheck) < std::chrono::seconds(2)) {
                    return s_cachedResult;
                }
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
            L"tailwind.config.json",
            L"postcss.config.js",
            L"postcss.config.cjs",
            L"postcss.config.mjs"
        };
        for (const auto* cfg : s_configFiles) {
            if (std::filesystem::exists(root / cfg, ec)) {
                return true;
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

        // C) Check root CSS/HTML/PHP files for @tailwind or cdn.tailwindcss.com
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (entry.is_regular_file(ec)) {
                auto ext = entry.path().extension().wstring();
                for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
                if (ext == L".css" || ext == L".html" || ext == L".php") {
                    std::ifstream f(entry.path(), std::ios::binary);
                    if (f.is_open()) {
                        std::string s;
                        s.resize(4096);
                        f.read(&s[0], s.size());
                        s.resize(static_cast<size_t>(f.gcount()));
                        if (s.find("cdn.tailwindcss.com") != std::string::npos ||
                            s.find("@tailwind") != std::string::npos ||
                            s.find("tailwindcss") != std::string::npos) {
                            return true;
                        }
                    }
                }
            }
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

        // 3. Layout, Flexbox, Grid, Sizing, Spacing, Typography
        static const struct GeneralUtil { const wchar_t* name; const wchar_t* detail; } genUtils[] = {
            { L"flex", L"display: flex;" },
            { L"inline-flex", L"display: inline-flex;" },
            { L"grid", L"display: grid;" },
            { L"inline-grid", L"display: inline-grid;" },
            { L"hidden", L"display: none;" },
            { L"block", L"display: block;" },
            { L"inline-block", L"display: inline-block;" },
            { L"flex-col", L"flex-direction: column;" },
            { L"flex-row", L"flex-direction: row;" },
            { L"flex-wrap", L"flex-wrap: wrap;" },
            { L"flex-nowrap", L"flex-wrap: nowrap;" },
            { L"items-center", L"align-items: center;" },
            { L"items-start", L"align-items: flex-start;" },
            { L"items-end", L"align-items: flex-end;" },
            { L"items-baseline", L"align-items: baseline;" },
            { L"items-stretch", L"align-items: stretch;" },
            { L"justify-center", L"justify-content: center;" },
            { L"justify-between", L"justify-content: space-between;" },
            { L"justify-around", L"justify-content: space-around;" },
            { L"justify-evenly", L"justify-content: space-evenly;" },
            { L"justify-start", L"justify-content: flex-start;" },
            { L"justify-end", L"justify-content: flex-end;" },
            { L"gap-1", L"gap: 0.25rem;" },
            { L"gap-2", L"gap: 0.5rem;" },
            { L"gap-3", L"gap: 0.75rem;" },
            { L"gap-4", L"gap: 1rem;" },
            { L"gap-6", L"gap: 1.5rem;" },
            { L"gap-8", L"gap: 2rem;" },
            { L"p-1", L"padding: 0.25rem;" },
            { L"p-2", L"padding: 0.5rem;" },
            { L"p-3", L"padding: 0.75rem;" },
            { L"p-4", L"padding: 1rem;" },
            { L"p-6", L"padding: 1.5rem;" },
            { L"p-8", L"padding: 2rem;" },
            { L"px-2", L"padding-left: 0.5rem; padding-right: 0.5rem;" },
            { L"px-4", L"padding-left: 1rem; padding-right: 1rem;" },
            { L"px-6", L"padding-left: 1.5rem; padding-right: 1.5rem;" },
            { L"py-1", L"padding-top: 0.25rem; padding-bottom: 0.25rem;" },
            { L"py-2", L"padding-top: 0.5rem; padding-bottom: 0.5rem;" },
            { L"py-4", L"padding-top: 1rem; padding-bottom: 1rem;" },
            { L"m-1", L"margin: 0.25rem;" },
            { L"m-2", L"margin: 0.5rem;" },
            { L"m-4", L"margin: 1rem;" },
            { L"mx-auto", L"margin-left: auto; margin-right: auto;" },
            { L"my-2", L"margin-top: 0.5rem; margin-bottom: 0.5rem;" },
            { L"my-4", L"margin-top: 1rem; margin-bottom: 1rem;" },
            { L"w-full", L"width: 100%;" },
            { L"w-screen", L"width: 100vw;" },
            { L"w-auto", L"width: auto;" },
            { L"w-fit", L"width: fit-content;" },
            { L"h-full", L"height: 100%;" },
            { L"h-screen", L"height: 100vh;" },
            { L"h-auto", L"height: auto;" },
            { L"h-fit", L"height: fit-content;" },
            { L"text-xs", L"font-size: 0.75rem; line-height: 1rem;" },
            { L"text-sm", L"font-size: 0.875rem; line-height: 1.25rem;" },
            { L"text-base", L"font-size: 1rem; line-height: 1.5rem;" },
            { L"text-lg", L"font-size: 1.125rem; line-height: 1.75rem;" },
            { L"text-xl", L"font-size: 1.25rem; line-height: 1.75rem;" },
            { L"text-2xl", L"font-size: 1.5rem; line-height: 2rem;" },
            { L"text-left", L"text-align: left;" },
            { L"text-center", L"text-align: center;" },
            { L"text-right", L"text-align: right;" },
            { L"font-bold", L"font-weight: 700;" },
            { L"font-semibold", L"font-weight: 600;" },
            { L"font-medium", L"font-weight: 500;" },
            { L"font-normal", L"font-weight: 400;" },
            { L"font-extrabold", L"font-weight: 800;" },
            { L"rounded", L"border-radius: 0.25rem;" },
            { L"rounded-md", L"border-radius: 0.375rem;" },
            { L"rounded-lg", L"border-radius: 0.5rem;" },
            { L"rounded-xl", L"border-radius: 0.75rem;" },
            { L"rounded-full", L"border-radius: 9999px;" },
            { L"border", L"border-width: 1px;" },
            { L"border-2", L"border-width: 2px;" },
            { L"shadow", L"box-shadow: 0 1px 3px 0 rgb(0 0 0 / 0.1);" },
            { L"shadow-md", L"box-shadow: 0 4px 6px -1px rgb(0 0 0 / 0.1);" },
            { L"shadow-lg", L"box-shadow: 0 10px 15px -3px rgb(0 0 0 / 0.1);" },
            { L"cursor-pointer", L"cursor: pointer;" },
            { L"overflow-hidden", L"overflow: hidden;" },
            { L"overflow-auto", L"overflow: auto;" },
            { L"relative", L"position: relative;" },
            { L"absolute", L"position: absolute;" },
            { L"inset-0", L"inset: 0px;" },
            { L"transition", L"transition-property: color, background-color, border-color, text-decoration-color, fill, stroke, opacity, box-shadow, transform, filter, backdrop-filter;" },
            { L"duration-150", L"transition-duration: 150ms;" },
            { L"duration-200", L"transition-duration: 200ms;" },
            { L"duration-300", L"transition-duration: 300ms;" }
        };
        for (const auto& gu : genUtils) {
            TailwindCandidate c;
            c.name = gu.name;
            c.detail = gu.detail;
            c.doc = L"utility";
            c.isColor = false;
            c.hex = 0;
            list.push_back(std::move(c));
        }

        return list;
    }
};

} // namespace phide::editor
