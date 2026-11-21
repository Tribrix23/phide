/**
 * @file Logger.cpp
 * @brief Asynchronous, categorized, date-partitioned file logger and crash reporter for phide.
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "phide/app/Logger.hpp"

#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace phide::app {

namespace {

constexpr size_t kCategoryCount       = static_cast<size_t>(LogCategory::Count);
constexpr size_t kMaxQueueSize        = 4096;
constexpr size_t kMaxMessageChars     = 8192;
constexpr uint64_t kMaxSingleFileBytes = 20ULL * 1024ULL * 1024ULL; // 20 MB per category per day
constexpr size_t kBreadcrumbCapacity  = 64;
constexpr size_t kBreadcrumbTextChars = 192;
constexpr const char* kPhideVersion   = "2026.f";

struct LogEntry {
    SYSTEMTIME st{};
    DWORD tid = 0;
    LogCategory category = LogCategory::System;
    LogLevel level = LogLevel::Info;
    std::wstring message;
};

struct CrashBreadcrumb {
    SYSTEMTIME st{};
    DWORD tid = 0;
    uint8_t level = 0;
    uint8_t category = 0;
    uint8_t valid = 0;
    wchar_t text[kBreadcrumbTextChars]{};
};

// Static crash-safe storage (never destroyed early, requires zero heap allocation during crash)
struct CrashStaticState {
    wchar_t logsRoot[MAX_PATH]{};
    wchar_t crashDir[MAX_PATH]{};
    wchar_t errorDir[MAX_PATH]{};
    wchar_t systemDir[MAX_PATH]{};
    wchar_t exeDir[MAX_PATH]{};
    std::atomic<bool> ready{false};
    std::atomic<bool> suppressDialog{false};
    std::atomic_flag handlingCrash = ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> breadcrumbSeq{0};
    CrashBreadcrumb breadcrumbs[kBreadcrumbCapacity]{};
};

CrashStaticState g_crash{};

static int MakeDateKey(const SYSTEMTIME& st) noexcept {
    return static_cast<int>(st.wYear) * 10000 + static_cast<int>(st.wMonth) * 100 + static_cast<int>(st.wDay);
}

static int64_t DaysFromCivil(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2) ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2u ? static_cast<unsigned>(-3) : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

static bool ParseDateFromFilename(const std::wstring& filename, int& outYear, unsigned& outMonth, unsigned& outDay) noexcept {
    // Searches for YYYY-MM-DD anywhere inside the filename (e.g., system-2026-11-21.log, crash-2026-11-21_08-35-12.log)
    if (filename.size() < 10) return false;
    auto isDigit = [](wchar_t c) noexcept { return c >= L'0' && c <= L'9'; };
    for (size_t i = 0; i + 10 <= filename.size(); ++i) {
        if (isDigit(filename[i]) && isDigit(filename[i + 1]) && isDigit(filename[i + 2]) && isDigit(filename[i + 3]) &&
            filename[i + 4] == L'-' &&
            isDigit(filename[i + 5]) && isDigit(filename[i + 6]) &&
            filename[i + 7] == L'-' &&
            isDigit(filename[i + 8]) && isDigit(filename[i + 9])) {
            int y = (filename[i] - L'0') * 1000 + (filename[i + 1] - L'0') * 100 +
                    (filename[i + 2] - L'0') * 10 + (filename[i + 3] - L'0');
            unsigned m = static_cast<unsigned>((filename[i + 5] - L'0') * 10 + (filename[i + 6] - L'0'));
            unsigned d = static_cast<unsigned>((filename[i + 8] - L'0') * 10 + (filename[i + 9] - L'0'));
            if (y >= 2000 && y <= 2999 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
                outYear = y;
                outMonth = m;
                outDay = d;
                return true;
            }
        }
    }
    return false;
}

static void EnsureDirTreeWin32(const wchar_t* path) noexcept {
    if (!path || !path[0]) return;
    wchar_t buf[MAX_PATH]{};
    wcsncpy_s(buf, path, _TRUNCATE);
    size_t len = wcslen(buf);
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == L'/') buf[i] = L'\\';
    }
    // Skip drive prefix like "C:\" or UNC root
    size_t start = 0;
    if (len >= 3 && buf[1] == L':' && buf[2] == L'\\') {
        start = 3;
    } else if (len >= 2 && buf[0] == L'\\' && buf[1] == L'\\') {
        start = 2;
    }
    for (size_t i = start; i < len; ++i) {
        if (buf[i] == L'\\') {
            buf[i] = L'\0';
            if (buf[0]) CreateDirectoryW(buf, nullptr);
            buf[i] = L'\\';
        }
    }
    CreateDirectoryW(buf, nullptr);
}

static std::string WideToUtf8Fast(const wchar_t* src, size_t srcLen) {
    if (!src || srcLen == 0) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, src, static_cast<int>(srcLen), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, src, static_cast<int>(srcLen), out.data(), needed, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWideFast(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

static void AppendNormalizedUtf8Message(std::string& dst, const std::wstring& msg) {
    if (msg.empty()) return;
    std::string utf8 = WideToUtf8Fast(msg.data(), msg.size());
    dst.reserve(dst.size() + utf8.size() + 8);
    for (size_t i = 0; i < utf8.size(); ++i) {
        char c = utf8[i];
        if (c == '\r') {
            if (i + 1 < utf8.size() && utf8[i + 1] == '\n') ++i;
            dst.append(" | ");
        } else if (c == '\n') {
            dst.append(" | ");
        } else if (c == '\0') {
            dst.push_back(' ');
        } else {
            dst.push_back(c);
        }
    }
}

static void FormatLogLine(std::string& out, const LogEntry& e) {
    char header[128]{};
    int hlen = snprintf(
        header, sizeof(header),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] [%s] [TID:%05lu] ",
        static_cast<unsigned>(e.st.wYear),
        static_cast<unsigned>(e.st.wMonth),
        static_cast<unsigned>(e.st.wDay),
        static_cast<unsigned>(e.st.wHour),
        static_cast<unsigned>(e.st.wMinute),
        static_cast<unsigned>(e.st.wSecond),
        static_cast<unsigned>(e.st.wMilliseconds),
        Logger::LevelTag(e.level),
        Logger::CategoryTag(e.category),
        static_cast<unsigned long>(e.tid)
    );
    if (hlen > 0) {
        out.append(header, static_cast<size_t>(hlen));
    }
    AppendNormalizedUtf8Message(out, e.message);
    out.append("\r\n");
}

static void WriteRawAll(HANDLE hFile, const void* data, size_t len) noexcept {
    if (hFile == INVALID_HANDLE_VALUE || !data || len == 0) return;
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        DWORD chunk = static_cast<DWORD>((std::min<size_t>)(remaining, 65536u));
        DWORD written = 0;
        if (!WriteFile(hFile, ptr, chunk, &written, nullptr) || written == 0) {
            break;
        }
        ptr += written;
        remaining -= written;
    }
}

static void WriteRawStr(HANDLE hFile, const char* s) noexcept {
    if (!s) return;
    WriteRawAll(hFile, s, strlen(s));
}

static void WriteRawFmt(HANDLE hFile, const char* fmt, ...) noexcept {
    char buf[1024]{};
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        size_t len = (static_cast<size_t>(n) < sizeof(buf)) ? static_cast<size_t>(n) : (sizeof(buf) - 1);
        WriteRawAll(hFile, buf, len);
    }
}

static const char* ExceptionCodeToName(DWORD code) noexcept {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
        case 0xE06D7363:                         return "MSVC_CPP_UNHANDLED_EXCEPTION";
        case 0xC0000374:                         return "STATUS_HEAP_CORRUPTION";
        case 0xC0000409:                         return "STATUS_STACK_BUFFER_OVERRUN";
        default:                                 return "UNKNOWN_EXCEPTION";
    }
}

static void ResolveModuleForAddress(uintptr_t addr, char* outModName, size_t modCap, uintptr_t& outRva) noexcept {
    outModName[0] = '\0';
    outRva = 0;
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(addr), &hMod) && hMod) {
        outRva = addr - reinterpret_cast<uintptr_t>(hMod);
        wchar_t wPath[MAX_PATH]{};
        if (GetModuleFileNameW(hMod, wPath, MAX_PATH) > 0) {
            const wchar_t* base = wcsrchr(wPath, L'\\');
            const wchar_t* name = base ? (base + 1) : wPath;
            WideCharToMultiByte(CP_UTF8, 0, name, -1, outModName, static_cast<int>(modCap), nullptr, nullptr);
            return;
        }
    }
    strncpy_s(outModName, modCap, "<unknown>", _TRUNCATE);
}

// Dynamic dbghelp.dll function pointers (no link-time dependency required)
using FnSymSetOptions         = DWORD(WINAPI*)(DWORD);
using FnSymInitializeW        = BOOL(WINAPI*)(HANDLE, PCWSTR, BOOL);
using FnSymCleanup            = BOOL(WINAPI*)(HANDLE);
using FnStackWalk64           = BOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID,
                                              PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                              PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
using FnSymFunctionTableAcc64 = PVOID(WINAPI*)(HANDLE, DWORD64);
using FnSymGetModuleBase64    = DWORD64(WINAPI*)(HANDLE, DWORD64);
using FnSymFromAddrW          = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFOW);
using FnSymGetLineFromAddrW64 = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINEW64);
using FnMiniDumpWriteDump     = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                              PMINIDUMP_EXCEPTION_INFORMATION,
                                              PMINIDUMP_USER_STREAM_INFORMATION,
                                              PMINIDUMP_CALLBACK_INFORMATION);

static void WriteStackTrace(HANDLE hFile, PEXCEPTION_POINTERS pEx) noexcept {
    WriteRawStr(hFile, "\r\n--- Stack Trace ---\r\n");

    HMODULE hDbg = LoadLibraryW(L"dbghelp.dll");
    FnSymSetOptions pSymSetOptions = nullptr;
    FnSymInitializeW pSymInit = nullptr;
    FnSymCleanup pSymCleanup = nullptr;
    FnStackWalk64 pStackWalk = nullptr;
    FnSymFunctionTableAcc64 pSymFuncTable = nullptr;
    FnSymGetModuleBase64 pSymModBase = nullptr;
    FnSymFromAddrW pSymFromAddr = nullptr;
    FnSymGetLineFromAddrW64 pSymGetLine = nullptr;

    bool symReady = false;
    HANDLE hProc = GetCurrentProcess();
    if (hDbg) {
        pSymSetOptions = reinterpret_cast<FnSymSetOptions>(GetProcAddress(hDbg, "SymSetOptions"));
        pSymInit       = reinterpret_cast<FnSymInitializeW>(GetProcAddress(hDbg, "SymInitializeW"));
        pSymCleanup    = reinterpret_cast<FnSymCleanup>(GetProcAddress(hDbg, "SymCleanup"));
        pStackWalk     = reinterpret_cast<FnStackWalk64>(GetProcAddress(hDbg, "StackWalk64"));
        pSymFuncTable  = reinterpret_cast<FnSymFunctionTableAcc64>(GetProcAddress(hDbg, "SymFunctionTableAccess64"));
        pSymModBase    = reinterpret_cast<FnSymGetModuleBase64>(GetProcAddress(hDbg, "SymGetModuleBase64"));
        pSymFromAddr   = reinterpret_cast<FnSymFromAddrW>(GetProcAddress(hDbg, "SymFromAddrW"));
        pSymGetLine    = reinterpret_cast<FnSymGetLineFromAddrW64>(GetProcAddress(hDbg, "SymGetLineFromAddrW64"));

        if (pSymSetOptions) {
            pSymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        }
        if (pSymInit) {
            const wchar_t* searchPath = g_crash.exeDir[0] ? g_crash.exeDir : nullptr;
            symReady = (pSymInit(hProc, searchPath, TRUE) != FALSE);
        }
    }

    auto formatFrame = [&](int idx, DWORD64 addr) {
        char modName[128]{};
        uintptr_t rva = 0;
        ResolveModuleForAddress(static_cast<uintptr_t>(addr), modName, sizeof(modName), rva);

        char symUtf8[256]{};
        char fileUtf8[260]{};
        DWORD lineNum = 0;
        DWORD64 disp = 0;

        if (symReady && pSymFromAddr) {
            alignas(SYMBOL_INFOW) unsigned char symBuf[sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t)]{};
            auto* sym = reinterpret_cast<SYMBOL_INFOW*>(symBuf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFOW);
            sym->MaxNameLen = 255;
            if (pSymFromAddr(hProc, addr, &disp, sym)) {
                WideCharToMultiByte(CP_UTF8, 0, sym->Name, -1, symUtf8, sizeof(symUtf8), nullptr, nullptr);
            }
        }
        if (symReady && pSymGetLine) {
            IMAGEHLP_LINEW64 lineInfo{};
            lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINEW64);
            DWORD lineDisp = 0;
            if (pSymGetLine(hProc, addr, &lineDisp, &lineInfo) && lineInfo.FileName) {
                WideCharToMultiByte(CP_UTF8, 0, lineInfo.FileName, -1, fileUtf8, sizeof(fileUtf8), nullptr, nullptr);
                lineNum = lineInfo.LineNumber;
            }
        }

        if (symUtf8[0] && fileUtf8[0]) {
            WriteRawFmt(hFile, "  #%02d  0x%016llX  %s+0x%llX  %s+0x%llX  (%s:%lu)\r\n",
                        idx, static_cast<unsigned long long>(addr), modName,
                        static_cast<unsigned long long>(rva), symUtf8,
                        static_cast<unsigned long long>(disp), fileUtf8,
                        static_cast<unsigned long>(lineNum));
        } else if (symUtf8[0]) {
            WriteRawFmt(hFile, "  #%02d  0x%016llX  %s+0x%llX  %s+0x%llX\r\n",
                        idx, static_cast<unsigned long long>(addr), modName,
                        static_cast<unsigned long long>(rva), symUtf8,
                        static_cast<unsigned long long>(disp));
        } else {
            WriteRawFmt(hFile, "  #%02d  0x%016llX  %s+0x%llX\r\n",
                        idx, static_cast<unsigned long long>(addr), modName,
                        static_cast<unsigned long long>(rva));
        }
    };

    bool walked = false;
#if defined(_M_X64) || defined(_M_AMD64)
    if (pEx && pEx->ContextRecord && pStackWalk && pSymFuncTable && pSymModBase) {
        CONTEXT ctxCopy = *pEx->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset    = ctxCopy.Rip;
        frame.AddrPC.Mode      = AddrModeFlat;
        frame.AddrFrame.Offset = ctxCopy.Rbp;
        frame.AddrFrame.Mode   = AddrModeFlat;
        frame.AddrStack.Offset = ctxCopy.Rsp;
        frame.AddrStack.Mode   = AddrModeFlat;

        HANDLE hThread = GetCurrentThread();
        for (int i = 0; i < 48; ++i) {
            if (!pStackWalk(IMAGE_FILE_MACHINE_AMD64, hProc, hThread, &frame, &ctxCopy,
                            nullptr, pSymFuncTable, pSymModBase, nullptr)) {
                break;
            }
            if (frame.AddrPC.Offset == 0) break;
            formatFrame(i, frame.AddrPC.Offset);
            walked = true;
        }
    }
#endif

    if (!walked) {
        void* frames[48]{};
        USHORT captured = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
        for (USHORT i = 0; i < captured; ++i) {
            formatFrame(static_cast<int>(i), reinterpret_cast<DWORD64>(frames[i]));
        }
        if (captured == 0) {
            WriteRawStr(hFile, "  (stack frames unavailable)\r\n");
        }
    }

    if (symReady && pSymCleanup) {
        pSymCleanup(hProc);
    }
    if (hDbg) {
        FreeLibrary(hDbg);
    }
}

static bool WriteMinidumpFile(const wchar_t* dmpPath, PEXCEPTION_POINTERS pEx) noexcept {
    if (!dmpPath || !dmpPath[0] || !pEx) return false;
    HMODULE hDbg = LoadLibraryW(L"dbghelp.dll");
    if (!hDbg) return false;
    auto pMiniDump = reinterpret_cast<FnMiniDumpWriteDump>(GetProcAddress(hDbg, "MiniDumpWriteDump"));
    bool ok = false;
    if (pMiniDump) {
        HANDLE hDmp = CreateFileW(dmpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDmp != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = pEx;
            mei.ClientPointers = FALSE;
            ok = (pMiniDump(GetCurrentProcess(), GetCurrentProcessId(), hDmp,
                            MiniDumpNormal, &mei, nullptr, nullptr) != FALSE);
            CloseHandle(hDmp);
            if (!ok) {
                DeleteFileW(dmpPath);
            }
        }
    }
    FreeLibrary(hDbg);
    return ok;
}

static void AppendFatalMirrorLine(const wchar_t* dirPath, const wchar_t* prefix,
                                  const SYSTEMTIME& st, DWORD tid,
                                  const char* reasonUtf8, const wchar_t* crashLogPath) noexcept {
    if (!dirPath || !dirPath[0]) return;
    EnsureDirTreeWin32(dirPath);
    wchar_t filePath[MAX_PATH]{};
    swprintf_s(filePath, L"%s\\%s-%04u-%02u-%02u.log",
               dirPath, prefix,
               static_cast<unsigned>(st.wYear),
               static_cast<unsigned>(st.wMonth),
               static_cast<unsigned>(st.wDay));

    HANDLE hFile = CreateFileW(filePath, FILE_APPEND_DATA | SYNCHRONIZE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    char crashPathUtf8[MAX_PATH * 3]{};
    if (crashLogPath && crashLogPath[0]) {
        WideCharToMultiByte(CP_UTF8, 0, crashLogPath, -1, crashPathUtf8, sizeof(crashPathUtf8), nullptr, nullptr);
    }
    WriteRawFmt(hFile,
                "%04u-%02u-%02u %02u:%02u:%02u.%03u [FATAL] [CRASH  ] [TID:%05lu] %s | Crash report: %s\r\n",
                static_cast<unsigned>(st.wYear),
                static_cast<unsigned>(st.wMonth),
                static_cast<unsigned>(st.wDay),
                static_cast<unsigned>(st.wHour),
                static_cast<unsigned>(st.wMinute),
                static_cast<unsigned>(st.wSecond),
                static_cast<unsigned>(st.wMilliseconds),
                static_cast<unsigned long>(tid),
                reasonUtf8 ? reasonUtf8 : "Unhandled crash",
                crashPathUtf8[0] ? crashPathUtf8 : "(unavailable)");
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
}

static void WriteWindowsVersionLine(HANDLE hFile) noexcept {
    using FnRtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        auto pRtlGetVersion = reinterpret_cast<FnRtlGetVersion>(GetProcAddress(hNt, "RtlGetVersion"));
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW rovi{};
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (pRtlGetVersion(&rovi) == 0) {
                WriteRawFmt(hFile, "OS Version   : Windows %lu.%lu (Build %lu)\r\n",
                            static_cast<unsigned long>(rovi.dwMajorVersion),
                            static_cast<unsigned long>(rovi.dwMinorVersion),
                            static_cast<unsigned long>(rovi.dwBuildNumber));
                return;
            }
        }
    }
    WriteRawStr(hFile, "OS Version   : Windows (version query unavailable)\r\n");
}

static std::wstring WriteCrashReportSync(const wchar_t* reasonWide, PEXCEPTION_POINTERS pEx, bool showDialog) noexcept {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    const DWORD pid = GetCurrentProcessId();
    const DWORD tid = GetCurrentThreadId();

    wchar_t crashDir[MAX_PATH]{};
    wchar_t logsRoot[MAX_PATH]{};
    wchar_t errorDir[MAX_PATH]{};
    wchar_t systemDir[MAX_PATH]{};

    if (g_crash.ready.load(std::memory_order_acquire) && g_crash.crashDir[0]) {
        wcsncpy_s(logsRoot, g_crash.logsRoot, _TRUNCATE);
        wcsncpy_s(crashDir, g_crash.crashDir, _TRUNCATE);
        wcsncpy_s(errorDir, g_crash.errorDir, _TRUNCATE);
        wcsncpy_s(systemDir, g_crash.systemDir, _TRUNCATE);
    } else {
        wchar_t appData[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0) {
            GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
        }
        if (appData[0]) {
            swprintf_s(logsRoot, L"%s\\phide\\logs", appData);
        } else {
            wcsncpy_s(logsRoot, L"logs", _TRUNCATE);
        }
        swprintf_s(crashDir,  L"%s\\crash",  logsRoot);
        swprintf_s(errorDir,  L"%s\\error",  logsRoot);
        swprintf_s(systemDir, L"%s\\system", logsRoot);
    }

    EnsureDirTreeWin32(logsRoot);
    EnsureDirTreeWin32(crashDir);

    wchar_t crashLogPath[MAX_PATH]{};
    wchar_t crashDmpPath[MAX_PATH]{};
    swprintf_s(crashLogPath, L"%s\\crash-%04u-%02u-%02u_%02u-%02u-%02u.log",
               crashDir,
               static_cast<unsigned>(st.wYear),
               static_cast<unsigned>(st.wMonth),
               static_cast<unsigned>(st.wDay),
               static_cast<unsigned>(st.wHour),
               static_cast<unsigned>(st.wMinute),
               static_cast<unsigned>(st.wSecond));
    swprintf_s(crashDmpPath, L"%s\\crash-%04u-%02u-%02u_%02u-%02u-%02u.dmp",
               crashDir,
               static_cast<unsigned>(st.wYear),
               static_cast<unsigned>(st.wMonth),
               static_cast<unsigned>(st.wDay),
               static_cast<unsigned>(st.wHour),
               static_cast<unsigned>(st.wMinute),
               static_cast<unsigned>(st.wSecond));

    char reasonUtf8[512]{};
    if (reasonWide && reasonWide[0]) {
        WideCharToMultiByte(CP_UTF8, 0, reasonWide, -1, reasonUtf8, sizeof(reasonUtf8), nullptr, nullptr);
    } else if (pEx && pEx->ExceptionRecord) {
        snprintf(reasonUtf8, sizeof(reasonUtf8), "%s (0x%08lX)",
                 ExceptionCodeToName(pEx->ExceptionRecord->ExceptionCode),
                 static_cast<unsigned long>(pEx->ExceptionRecord->ExceptionCode));
    } else {
        strncpy_s(reasonUtf8, "Unhandled fatal error", _TRUNCATE);
    }

    HANDLE hFile = CreateFileW(crashLogPath, GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        WriteRawStr(hFile, "========================================================================\r\n");
        WriteRawStr(hFile, "  PHIDE CRASH REPORT [FATAL]\r\n");
        WriteRawStr(hFile, "========================================================================\r\n");
        WriteRawFmt(hFile, "Timestamp    : %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n",
                    static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay),
                    static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond),
                    static_cast<unsigned>(st.wMilliseconds));
        WriteRawFmt(hFile, "PHIDE Version: %s\r\n", kPhideVersion);
        WriteWindowsVersionLine(hFile);
        WriteRawFmt(hFile, "Process / TID: PID=%lu  TID=%lu\r\n",
                    static_cast<unsigned long>(pid), static_cast<unsigned long>(tid));
        WriteRawFmt(hFile, "Reason       : %s\r\n", reasonUtf8);

        if (pEx && pEx->ExceptionRecord) {
            const EXCEPTION_RECORD* rec = pEx->ExceptionRecord;
            char modName[128]{};
            uintptr_t rva = 0;
            ResolveModuleForAddress(reinterpret_cast<uintptr_t>(rec->ExceptionAddress), modName, sizeof(modName), rva);
            WriteRawFmt(hFile, "Exception    : %s (0x%08lX)\r\n",
                        ExceptionCodeToName(rec->ExceptionCode),
                        static_cast<unsigned long>(rec->ExceptionCode));
            WriteRawFmt(hFile, "Fault Address: 0x%016llX (%s+0x%llX)\r\n",
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(rec->ExceptionAddress)),
                        modName, static_cast<unsigned long long>(rva));
            if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
                const char* op = (rec->ExceptionInformation[0] == 0) ? "READ"
                               : (rec->ExceptionInformation[0] == 1) ? "WRITE"
                               : (rec->ExceptionInformation[0] == 8) ? "DEP_EXEC" : "ACCESS";
                WriteRawFmt(hFile, "Access Detail: %s at address 0x%016llX\r\n",
                            op, static_cast<unsigned long long>(rec->ExceptionInformation[1]));
            }
        }

#if defined(_M_X64) || defined(_M_AMD64)
        if (pEx && pEx->ContextRecord) {
            const CONTEXT* c = pEx->ContextRecord;
            WriteRawStr(hFile, "\r\n--- CPU Registers (x64) ---\r\n");
            WriteRawFmt(hFile, "  RIP=0x%016llX  RSP=0x%016llX  RBP=0x%016llX  EFLAGS=0x%08lX\r\n",
                        static_cast<unsigned long long>(c->Rip), static_cast<unsigned long long>(c->Rsp),
                        static_cast<unsigned long long>(c->Rbp), static_cast<unsigned long>(c->EFlags));
            WriteRawFmt(hFile, "  RAX=0x%016llX  RBX=0x%016llX  RCX=0x%016llX  RDX=0x%016llX\r\n",
                        static_cast<unsigned long long>(c->Rax), static_cast<unsigned long long>(c->Rbx),
                        static_cast<unsigned long long>(c->Rcx), static_cast<unsigned long long>(c->Rdx));
            WriteRawFmt(hFile, "  RSI=0x%016llX  RDI=0x%016llX  R8 =0x%016llX  R9 =0x%016llX\r\n",
                        static_cast<unsigned long long>(c->Rsi), static_cast<unsigned long long>(c->Rdi),
                        static_cast<unsigned long long>(c->R8),  static_cast<unsigned long long>(c->R9));
            WriteRawFmt(hFile, "  R10=0x%016llX  R11=0x%016llX  R12=0x%016llX  R13=0x%016llX\r\n",
                        static_cast<unsigned long long>(c->R10), static_cast<unsigned long long>(c->R11),
                        static_cast<unsigned long long>(c->R12), static_cast<unsigned long long>(c->R13));
            WriteRawFmt(hFile, "  R14=0x%016llX  R15=0x%016llX\r\n",
                        static_cast<unsigned long long>(c->R14), static_cast<unsigned long long>(c->R15));
        }
#endif

        WriteStackTrace(hFile, pEx);

        // Dump recent activity breadcrumbs without locking any mutex (deadlock-free during crash!)
        WriteRawStr(hFile, "\r\n--- Recent PHIDE Activity (Latest Breadcrumbs) ---\r\n");
        const uint32_t totalSeq = g_crash.breadcrumbSeq.load(std::memory_order_acquire);
        const uint32_t count = (totalSeq < kBreadcrumbCapacity) ? totalSeq : static_cast<uint32_t>(kBreadcrumbCapacity);
        const uint32_t startSeq = totalSeq - count;
        if (count == 0) {
            WriteRawStr(hFile, "  (no recent log entries)\r\n");
        } else {
            for (uint32_t i = 0; i < count; ++i) {
                const CrashBreadcrumb& b = g_crash.breadcrumbs[(startSeq + i) % kBreadcrumbCapacity];
                if (!b.valid) continue;
                char msgUtf8[kBreadcrumbTextChars * 3]{};
                WideCharToMultiByte(CP_UTF8, 0, b.text, -1, msgUtf8, sizeof(msgUtf8), nullptr, nullptr);
                WriteRawFmt(hFile, "  %04u-%02u-%02u %02u:%02u:%02u.%03u [%s] [%s] [TID:%05lu] %s\r\n",
                            static_cast<unsigned>(b.st.wYear), static_cast<unsigned>(b.st.wMonth), static_cast<unsigned>(b.st.wDay),
                            static_cast<unsigned>(b.st.wHour), static_cast<unsigned>(b.st.wMinute), static_cast<unsigned>(b.st.wSecond),
                            static_cast<unsigned>(b.st.wMilliseconds),
                            Logger::LevelTag(static_cast<LogLevel>(b.level)),
                            Logger::CategoryTag(static_cast<LogCategory>(b.category)),
                            static_cast<unsigned long>(b.tid),
                            msgUtf8);
            }
        }

        bool dmpOk = WriteMinidumpFile(crashDmpPath, pEx);
        if (dmpOk) {
            char dmpUtf8[MAX_PATH * 3]{};
            WideCharToMultiByte(CP_UTF8, 0, crashDmpPath, -1, dmpUtf8, sizeof(dmpUtf8), nullptr, nullptr);
            WriteRawFmt(hFile, "\r\nMinidump     : %s\r\n", dmpUtf8);
        }

        char rootUtf8[MAX_PATH * 3]{};
        WideCharToMultiByte(CP_UTF8, 0, logsRoot, -1, rootUtf8, sizeof(rootUtf8), nullptr, nullptr);
        WriteRawFmt(hFile, "Logs Folder  : %s\r\n", rootUtf8);

        FlushFileBuffers(hFile);
        CloseHandle(hFile);
    }

    // Mirror a concise [FATAL] line directly into today's error log and system log
    AppendFatalMirrorLine(errorDir, L"error", st, tid, reasonUtf8, crashLogPath);
    AppendFatalMirrorLine(systemDir, L"system", st, tid, reasonUtf8, crashLogPath);

    if (showDialog && !g_crash.suppressDialog.load(std::memory_order_acquire)) {
        wchar_t reasonDisplay[256]{};
        MultiByteToWideChar(CP_UTF8, 0, reasonUtf8, -1, reasonDisplay, 256);

        wchar_t msg[1536]{};
        swprintf_s(
            msg,
            L"phide encountered an unexpected error and needs to close.\n\n"
            L"Reason: %s\n\n"
            L"Crash log saved to:\n%s\n\n"
            L"All phide logs (system, error, crash, editor, lsp, server, plugins) are stored in:\n%s\n\n"
            L"Do you want to open the crash log folder in File Explorer?",
            reasonDisplay,
            crashLogPath,
            logsRoot
        );

        int choice = MessageBoxW(nullptr, msg, L"phide Crash Report",
                                 MB_YESNO | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        if (choice == IDYES) {
            wchar_t param[MAX_PATH + 32]{};
            swprintf_s(param, L"/select,\"%s\"", crashLogPath);
            ShellExecuteW(nullptr, L"open", L"explorer.exe", param, nullptr, SW_SHOWNORMAL);
        }
    }

    return std::wstring(crashLogPath);
}

static LONG WINAPI UnhandledExceptionFilterProc(PEXCEPTION_POINTERS pEx) {
    if (g_crash.handlingCrash.test_and_set(std::memory_order_acq_rel)) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    WriteCrashReportSync(nullptr, pEx, true);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void TerminateHandlerProc() {
    if (g_crash.handlingCrash.test_and_set(std::memory_order_acq_rel)) {
        _exit(3);
    }
    wchar_t reasonBuf[512] = L"std::terminate called (unhandled C++ exception)";
    try {
        auto eptr = std::current_exception();
        if (eptr) {
            std::rethrow_exception(eptr);
        }
    } catch (const std::exception& ex) {
        std::wstring w = Utf8ToWideFast(ex.what());
        swprintf_s(reasonBuf, L"Unhandled C++ exception (std::terminate): %s", w.c_str());
    } catch (...) {
        wcsncpy_s(reasonBuf, L"Unknown unhandled C++ exception (std::terminate)", _TRUNCATE);
    }
    WriteCrashReportSync(reasonBuf, nullptr, true);
    _exit(3);
}

static void PureCallHandlerProc() {
    if (g_crash.handlingCrash.test_and_set(std::memory_order_acq_rel)) {
        _exit(3);
    }
    WriteCrashReportSync(L"Pure virtual function call (_purecall)", nullptr, true);
    _exit(3);
}

static void InvalidParamHandlerProc(const wchar_t* expression, const wchar_t* function,
                                    const wchar_t* file, unsigned int line, uintptr_t) {
    if (g_crash.handlingCrash.test_and_set(std::memory_order_acq_rel)) {
        _exit(3);
    }
    wchar_t reasonBuf[512]{};
    swprintf_s(reasonBuf, L"CRT invalid parameter in %s (%s:%u): %s",
               function ? function : L"<unknown>",
               file ? file : L"<unknown>",
               line,
               expression ? expression : L"<none>");
    WriteCrashReportSync(reasonBuf, nullptr, true);
    _exit(3);
}

static void AbortSignalHandlerProc(int) {
    if (g_crash.handlingCrash.test_and_set(std::memory_order_acq_rel)) {
        _exit(3);
    }
    WriteCrashReportSync(L"Abnormal termination signal (SIGABRT)", nullptr, true);
    _exit(3);
}

static std::wstring ResolveDefaultLogsRoot() {
    wchar_t buf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH) > 0 && buf[0] != L'\0') {
        return (std::filesystem::path(buf) / L"phide" / L"logs").wstring();
    }
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0 && buf[0] != L'\0') {
        return (std::filesystem::path(buf) / L"phide" / L"logs").wstring();
    }
    return (std::filesystem::current_path() / L"logs").wstring();
}

} // anonymous namespace

struct Logger::Impl {
    std::atomic<bool> initialized{false};
    std::atomic<int> retentionDays{7};
    std::atomic<uint64_t> droppedCount{0};

    std::wstring logsRoot;
    std::array<std::wstring, kCategoryCount> categoryDirs;

    // Async MPSC queue & worker thread
    mutable std::mutex queueMutex;
    std::condition_variable queueCv;
    std::condition_variable flushCv;
    std::deque<LogEntry> queue;
    bool stopRequested = false;
    bool flushRequested = false;
    uint64_t flushGeneration = 0;

    std::thread writerThread;

    // Background-thread-only state (never touched on UI thread!)
    int openDateKey = 0;
    std::array<HANDLE, kCategoryCount> handles{};
    std::array<std::string, kCategoryCount> batchBuffers{};

    Impl() {
        handles.fill(INVALID_HANDLE_VALUE);
    }

    ~Impl() {
        CloseAllHandles();
    }

    void CloseAllHandles() noexcept {
        for (auto& h : handles) {
            if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
                h = INVALID_HANDLE_VALUE;
            }
        }
    }

    std::wstring BuildDatedPath(LogCategory cat, const SYSTEMTIME& st) const {
        const size_t idx = static_cast<size_t>(cat);
        if (idx >= kCategoryCount) return {};
        wchar_t fname[96]{};
        swprintf_s(fname, L"%s-%04u-%02u-%02u.log",
                   Logger::CategoryFolderName(cat),
                   static_cast<unsigned>(st.wYear),
                   static_cast<unsigned>(st.wMonth),
                   static_cast<unsigned>(st.wDay));
        return (std::filesystem::path(categoryDirs[idx]) / fname).wstring();
    }

    HANDLE EnsureCategoryHandle(LogCategory cat, const SYSTEMTIME& st) {
        const size_t idx = static_cast<size_t>(cat);
        if (idx >= kCategoryCount) return INVALID_HANDLE_VALUE;

        if (handles[idx] != INVALID_HANDLE_VALUE) {
            // Protect disk space: rotate if single day's category log exceeds 20 MB
            LARGE_INTEGER li{};
            if (GetFileSizeEx(handles[idx], &li) && static_cast<uint64_t>(li.QuadPart) >= kMaxSingleFileBytes) {
                CloseHandle(handles[idx]);
                handles[idx] = INVALID_HANDLE_VALUE;
                std::wstring activePath = BuildDatedPath(cat, st);
                std::wstring prevPath = activePath;
                if (prevPath.size() > 4) {
                    prevPath.replace(prevPath.size() - 4, 4, L".prev.log");
                } else {
                    prevPath += L".prev";
                }
                MoveFileExW(activePath.c_str(), prevPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            } else {
                return handles[idx];
            }
        }

        EnsureDirTreeWin32(categoryDirs[idx].c_str());
        std::wstring path = BuildDatedPath(cat, st);
        HANDLE h = CreateFileW(path.c_str(),
                               FILE_APPEND_DATA | SYNCHRONIZE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr,
                               OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        handles[idx] = h;
        return h;
    }

    size_t PruneOldLogsInternal() {
        const int keepDays = (std::max)(0, retentionDays.load(std::memory_order_acquire));
        SYSTEMTIME nowLocal{};
        GetLocalTime(&nowLocal);
        const int64_t todayCivil = DaysFromCivil(nowLocal.wYear, nowLocal.wMonth, nowLocal.wDay);

        FILETIME nowFt{};
        GetSystemTimeAsFileTime(&nowFt);
        ULARGE_INTEGER nowUi{};
        nowUi.LowPart = nowFt.dwLowDateTime;
        nowUi.HighPart = nowFt.dwHighDateTime;
        constexpr uint64_t kTicksPerDay = 86400ULL * 10000000ULL;
        const uint64_t maxAgeTicks = static_cast<uint64_t>(keepDays) * kTicksPerDay;

        size_t deletedCount = 0;
        std::error_code ec;
        for (size_t i = 0; i < kCategoryCount; ++i) {
            const auto& dir = categoryDirs[i];
            if (dir.empty() || !std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
                continue;
            }
            for (const auto& entry : std::filesystem::directory_iterator(
                     dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                std::wstring ext = entry.path().extension().wstring();
                for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
                if (ext != L".log" && ext != L".dmp") continue;

                std::wstring fname = entry.path().filename().wstring();
                int fy = 0;
                unsigned fm = 0, fd = 0;
                const bool hasFileDate = ParseDateFromFilename(fname, fy, fm, fd);
                int64_t dateAgeDays = 0;
                if (hasFileDate) {
                    dateAgeDays = todayCivil - DaysFromCivil(fy, fm, fd);
                    if (dateAgeDays <= 0) {
                        continue; // Never delete today's (or future-clock) active log
                    }
                }

                bool expired = false;
                if (hasFileDate && dateAgeDays > keepDays) {
                    expired = true;
                } else {
                    WIN32_FILE_ATTRIBUTE_DATA fad{};
                    if (GetFileAttributesExW(entry.path().c_str(), GetFileExInfoStandard, &fad)) {
                        ULARGE_INTEGER writeUi{};
                        writeUi.LowPart = fad.ftLastWriteTime.dwLowDateTime;
                        writeUi.HighPart = fad.ftLastWriteTime.dwHighDateTime;
                        if (nowUi.QuadPart > writeUi.QuadPart && (nowUi.QuadPart - writeUi.QuadPart) > maxAgeTicks) {
                            expired = true;
                        }
                    }
                }

                if (expired) {
                    std::error_code rmEc;
                    if (std::filesystem::remove(entry.path(), rmEc) && !rmEc) {
                        ++deletedCount;
                    }
                }
            }
        }
        return deletedCount;
    }

    void WriterLoop() {
        // Keep logger strictly below UI/rendering thread priority so writing logs can never stutter PHIDE
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        // Create all log category directories & prune expired logs (> 7 days) off the UI thread
        EnsureDirTreeWin32(logsRoot.c_str());
        for (size_t i = 0; i < kCategoryCount; ++i) {
            EnsureDirTreeWin32(categoryDirs[i].c_str());
        }

        SYSTEMTIME startSt{};
        GetLocalTime(&startSt);
        openDateKey = MakeDateKey(startSt);

        const size_t startupPruned = PruneOldLogsInternal();
        if (startupPruned > 0) {
            LogEntry notice{};
            GetLocalTime(&notice.st);
            notice.tid = GetCurrentThreadId();
            notice.category = LogCategory::System;
            notice.level = LogLevel::Info;
            notice.message = L"Log retention cleanup removed " + std::to_wstring(startupPruned) +
                             L" log file(s) older than " + std::to_wstring(retentionDays.load()) + L" day(s).";
            std::string line;
            FormatLogLine(line, notice);
            HANDLE hSys = EnsureCategoryHandle(LogCategory::System, notice.st);
            WriteRawAll(hSys, line.data(), line.size());
        }

        std::deque<LogEntry> localBatch;

        for (;;) {
            bool doFlush = false;
            bool stopping = false;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCv.wait(lock, [&]() {
                    return !queue.empty() || flushRequested || stopRequested;
                });
                localBatch.swap(queue);
                doFlush = flushRequested;
                stopping = stopRequested;
            }

            const uint64_t droppedNow = droppedCount.exchange(0, std::memory_order_relaxed);
            if (droppedNow > 0) {
                LogEntry dropNote{};
                GetLocalTime(&dropNote.st);
                dropNote.tid = GetCurrentThreadId();
                dropNote.category = LogCategory::System;
                dropNote.level = LogLevel::Warn;
                dropNote.message = L"Logger queue saturated under burst load; dropped " +
                                   std::to_wstring(droppedNow) + L" low-priority log message(s) to protect UI responsiveness.";
                localBatch.push_back(std::move(dropNote));
            }

            if (!localBatch.empty()) {
                for (auto& buf : batchBuffers) buf.clear();

                bool hasHighSeverity = false;
                SYSTEMTIME batchSt = localBatch.front().st;
                int batchDateKey = MakeDateKey(batchSt);

                auto flushBatchBuffers = [&](const SYSTEMTIME& refSt, bool syncDisk) {
                    for (size_t i = 0; i < kCategoryCount; ++i) {
                        if (batchBuffers[i].empty()) continue;
                        HANDLE h = EnsureCategoryHandle(static_cast<LogCategory>(i), refSt);
                        if (h != INVALID_HANDLE_VALUE) {
                            WriteRawAll(h, batchBuffers[i].data(), batchBuffers[i].size());
                            if (syncDisk) {
                                FlushFileBuffers(h);
                            }
                        }
                        batchBuffers[i].clear();
                    }
                };

                for (const auto& entry : localBatch) {
                    const int entryDateKey = MakeDateKey(entry.st);
                    if (entryDateKey != batchDateKey) {
                        // Date boundary crossed mid-batch: flush previous date first
                        flushBatchBuffers(batchSt, hasHighSeverity);
                        batchDateKey = entryDateKey;
                        batchSt = entry.st;
                    }
                    if (entryDateKey != openDateKey) {
                        CloseAllHandles();
                        openDateKey = entryDateKey;
                        PruneOldLogsInternal();
                    }

                    const size_t catIdx = static_cast<size_t>(entry.category);
                    if (catIdx >= kCategoryCount) continue;

                    std::string formatted;
                    FormatLogLine(formatted, entry);

                    batchBuffers[catIdx].append(formatted);

                    // Mirror all WARN / ERROR / FATAL across all subsystems into the dedicated error\ log
                    if (entry.level >= LogLevel::Warn && entry.category != LogCategory::Error) {
                        batchBuffers[static_cast<size_t>(LogCategory::Error)].append(formatted);
                    }
                    // Mirror FATAL from other categories into system\ as well
                    if (entry.level == LogLevel::Fatal && entry.category != LogCategory::System) {
                        batchBuffers[static_cast<size_t>(LogCategory::System)].append(formatted);
                    }
                    if (entry.level >= LogLevel::Error) {
                        hasHighSeverity = true;
                    }
                }

                flushBatchBuffers(batchSt, hasHighSeverity || doFlush || stopping);
                localBatch.clear();
            } else if (doFlush || stopping) {
                for (auto h : handles) {
                    if (h != INVALID_HANDLE_VALUE) {
                        FlushFileBuffers(h);
                    }
                }
            }

            if (doFlush) {
                std::lock_guard<std::mutex> lock(queueMutex);
                if (queue.empty()) {
                    flushRequested = false;
                    ++flushGeneration;
                    flushCv.notify_all();
                }
            }

            if (stopping) {
                std::lock_guard<std::mutex> lock(queueMutex);
                if (queue.empty()) {
                    break;
                }
            }
        }

        CloseAllHandles();
    }
};

Logger& Logger::Instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::Logger()
    : m_impl(new Impl()) {
}

Logger::~Logger() {
    Shutdown();
    delete m_impl;
    m_impl = nullptr;
}

void Logger::Initialize(const std::wstring& customLogsRoot, int retentionDays) {
    if (!m_impl) return;
    if (m_impl->initialized.load(std::memory_order_acquire)) {
        Shutdown();
    }

    m_impl->retentionDays.store((std::max)(0, retentionDays), std::memory_order_release);
    m_impl->droppedCount.store(0, std::memory_order_relaxed);
    m_impl->logsRoot = customLogsRoot.empty() ? ResolveDefaultLogsRoot() : customLogsRoot;

    for (size_t i = 0; i < kCategoryCount; ++i) {
        m_impl->categoryDirs[i] = (std::filesystem::path(m_impl->logsRoot) /
                                   CategoryFolderName(static_cast<LogCategory>(i))).wstring();
    }

    // Populate crash-safe static fixed buffers (zero heap allocation needed during crash)
    wcsncpy_s(g_crash.logsRoot,  m_impl->logsRoot.c_str(), _TRUNCATE);
    wcsncpy_s(g_crash.crashDir,  m_impl->categoryDirs[static_cast<size_t>(LogCategory::Crash)].c_str(), _TRUNCATE);
    wcsncpy_s(g_crash.errorDir,  m_impl->categoryDirs[static_cast<size_t>(LogCategory::Error)].c_str(), _TRUNCATE);
    wcsncpy_s(g_crash.systemDir, m_impl->categoryDirs[static_cast<size_t>(LogCategory::System)].c_str(), _TRUNCATE);

    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        wchar_t* slash = wcsrchr(exePath, L'\\');
        if (slash) *slash = L'\0';
        wcsncpy_s(g_crash.exeDir, exePath, _TRUNCATE);
    }

    wchar_t noDlgBuf[16]{};
    if (GetEnvironmentVariableW(L"PHIDE_NO_CRASH_DIALOG", noDlgBuf, 16) > 0 && noDlgBuf[0] == L'1') {
        g_crash.suppressDialog.store(true, std::memory_order_release);
    }

    g_crash.handlingCrash.clear(std::memory_order_release);
    g_crash.breadcrumbSeq.store(0, std::memory_order_release);
    memset(g_crash.breadcrumbs, 0, sizeof(g_crash.breadcrumbs));
    g_crash.ready.store(true, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(m_impl->queueMutex);
        m_impl->queue.clear();
        m_impl->stopRequested = false;
        m_impl->flushRequested = false;
    }

    m_impl->initialized.store(true, std::memory_order_release);
    m_impl->writerThread = std::thread([this]() {
        m_impl->WriterLoop();
    });
}

void Logger::InstallCrashHandlers() {
    SetUnhandledExceptionFilter(&UnhandledExceptionFilterProc);
    std::set_terminate(&TerminateHandlerProc);
    _set_purecall_handler(&PureCallHandlerProc);
    _set_invalid_parameter_handler(&InvalidParamHandlerProc);
    std::signal(SIGABRT, &AbortSignalHandlerProc);
}

void Logger::Shutdown() {
    if (!m_impl) return;
    if (!m_impl->initialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->queueMutex);
        m_impl->stopRequested = true;
    }
    m_impl->queueCv.notify_all();

    if (m_impl->writerThread.joinable()) {
        m_impl->writerThread.join();
    }
}

void Logger::Log(LogCategory category, LogLevel level, const std::wstring& message) {
    if (!m_impl || !m_impl->initialized.load(std::memory_order_acquire)) {
        return;
    }

    LogEntry entry{};
    GetLocalTime(&entry.st);
    entry.tid = GetCurrentThreadId();
    entry.category = category;
    entry.level = level;
    if (message.size() > kMaxMessageChars) {
        entry.message.assign(message.data(), kMaxMessageChars);
        entry.message.append(L" ...[truncated]");
    } else {
        entry.message = message;
    }

    bool shouldNotify = false;
    {
        std::lock_guard<std::mutex> lock(m_impl->queueMutex);

        // Record compact crash breadcrumb in fixed-size static ring buffer (no heap alloc)
        const uint32_t seq = g_crash.breadcrumbSeq.load(std::memory_order_relaxed);
        CrashBreadcrumb& bc = g_crash.breadcrumbs[seq % kBreadcrumbCapacity];
        bc.st = entry.st;
        bc.tid = entry.tid;
        bc.level = static_cast<uint8_t>(level);
        bc.category = static_cast<uint8_t>(category);
        const size_t copyLen = (std::min)(entry.message.size(), kBreadcrumbTextChars - 1);
        for (size_t i = 0; i < copyLen; ++i) {
            wchar_t ch = entry.message[i];
            bc.text[i] = (ch == L'\r' || ch == L'\n') ? L' ' : ch;
        }
        bc.text[copyLen] = L'\0';
        bc.valid = 1;
        g_crash.breadcrumbSeq.store(seq + 1, std::memory_order_release);

        // Bounded queue: NEVER block the UI thread under burst load
        if (m_impl->queue.size() >= kMaxQueueSize) {
            if (level <= LogLevel::Info) {
                m_impl->droppedCount.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            // Evict one older low-priority entry if available so WARN/ERROR/FATAL is preserved
            bool evicted = false;
            const size_t scanLimit = (std::min<size_t>)(m_impl->queue.size(), 64u);
            for (size_t i = 0; i < scanLimit; ++i) {
                if (m_impl->queue[i].level <= LogLevel::Info) {
                    m_impl->queue.erase(m_impl->queue.begin() + static_cast<ptrdiff_t>(i));
                    evicted = true;
                    break;
                }
            }
            if (!evicted && !m_impl->queue.empty()) {
                m_impl->queue.pop_front();
            }
            m_impl->droppedCount.fetch_add(1, std::memory_order_relaxed);
        }

        shouldNotify = m_impl->queue.empty();
        m_impl->queue.push_back(std::move(entry));
    }

    if (shouldNotify) {
        m_impl->queueCv.notify_one();
    }
}

void Logger::LogUtf8(LogCategory category, LogLevel level, const std::string& message) {
    if (!m_impl || !m_impl->initialized.load(std::memory_order_acquire)) {
        return;
    }
    Log(category, level, Utf8ToWideFast(message));
}

void Logger::Flush(uint32_t timeoutMs) {
    if (!m_impl || !m_impl->initialized.load(std::memory_order_acquire)) {
        return;
    }
    std::unique_lock<std::mutex> lock(m_impl->queueMutex);
    const uint64_t targetGen = m_impl->flushGeneration + 1;
    m_impl->flushRequested = true;
    m_impl->queueCv.notify_one();
    m_impl->flushCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
        return m_impl->flushGeneration >= targetGen || m_impl->stopRequested;
    });
}

std::wstring Logger::ReportUnhandledCppException(const char* whatMessage) {
    if (g_crash.handlingCrash.test_and_set(std::memory_order_acq_rel)) {
        return GetLatestCrashLogPath();
    }
    std::wstring reason = L"Unhandled C++ exception";
    if (whatMessage && whatMessage[0]) {
        reason += L": ";
        reason += Utf8ToWideFast(whatMessage);
    }
    return WriteCrashReportSync(reason.c_str(), nullptr, true);
}

size_t Logger::PruneOldLogsNow() {
    if (!m_impl) return 0;
    return m_impl->PruneOldLogsInternal();
}

void Logger::EnsureLogDirectoriesExist() const {
    if (!m_impl) return;
    const std::wstring root = GetLogsRootDir();
    EnsureDirTreeWin32(root.c_str());
    for (size_t i = 0; i < kCategoryCount; ++i) {
        std::wstring sub = (std::filesystem::path(root) / CategoryFolderName(static_cast<LogCategory>(i))).wstring();
        EnsureDirTreeWin32(sub.c_str());
    }
}

std::wstring Logger::GetLogsRootDir() const {
    if (m_impl && !m_impl->logsRoot.empty()) {
        return m_impl->logsRoot;
    }
    return ResolveDefaultLogsRoot();
}

std::wstring Logger::GetCategoryDir(LogCategory category) const {
    const size_t idx = static_cast<size_t>(category);
    if (m_impl && idx < kCategoryCount && !m_impl->categoryDirs[idx].empty()) {
        return m_impl->categoryDirs[idx];
    }
    return (std::filesystem::path(GetLogsRootDir()) / CategoryFolderName(category)).wstring();
}

std::wstring Logger::GetTodayLogPath(LogCategory category) const {
    if (category == LogCategory::Crash) {
        return GetLatestCrashLogPath();
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t fname[96]{};
    swprintf_s(fname, L"%s-%04u-%02u-%02u.log",
               CategoryFolderName(category),
               static_cast<unsigned>(st.wYear),
               static_cast<unsigned>(st.wMonth),
               static_cast<unsigned>(st.wDay));
    return (std::filesystem::path(GetCategoryDir(category)) / fname).wstring();
}

std::wstring Logger::EnsureTodayLogFile(LogCategory category) {
    if (category == LogCategory::Crash) {
        EnsureDirTreeWin32(GetCategoryDir(LogCategory::Crash).c_str());
        return GetLatestCrashLogPath();
    }

    Flush(150);

    const std::wstring dir = GetCategoryDir(category);
    EnsureDirTreeWin32(dir.c_str());

    const std::wstring path = GetTodayLogPath(category);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        HANDLE h = CreateFileW(path.c_str(),
                               FILE_APPEND_DATA | SYNCHRONIZE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr,
                               OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz{};
            if (GetFileSizeEx(h, &sz) && sz.QuadPart == 0) {
                SYSTEMTIME st{};
                GetLocalTime(&st);
                char dispUtf8[64]{};
                WideCharToMultiByte(CP_UTF8, 0, CategoryDisplayName(category), -1, dispUtf8, sizeof(dispUtf8), nullptr, nullptr);
                // Intentionally omit error/warn keywords in this empty-state banner so LogTail highlights it cleanly as INFO
                WriteRawFmt(h,
                            "%04u-%02u-%02u %02u:%02u:%02u.%03u [INFO ] [TID:%05lu] %s log opened — no entries recorded yet today.\r\n",
                            static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay),
                            static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond),
                            static_cast<unsigned>(st.wMilliseconds),
                            static_cast<unsigned long>(GetCurrentThreadId()),
                            dispUtf8);
            }
            CloseHandle(h);
        }
    }
    return path;
}

std::vector<std::wstring> Logger::ListCrashLogs(size_t maxCount) const {
    std::vector<std::pair<std::filesystem::file_time_type, std::wstring>> found;
    const std::wstring crashDir = GetCategoryDir(LogCategory::Crash);
    std::error_code ec;
    if (!std::filesystem::exists(crashDir, ec) || !std::filesystem::is_directory(crashDir, ec)) {
        return {};
    }
    for (const auto& entry : std::filesystem::directory_iterator(
             crashDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::wstring ext = entry.path().extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        if (ext != L".log") continue;
        auto wt = entry.last_write_time(ec);
        if (!ec) {
            found.emplace_back(wt, entry.path().wstring());
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second > b.second;
    });
    std::vector<std::wstring> result;
    const size_t n = (std::min)(found.size(), maxCount);
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        result.push_back(std::move(found[i].second));
    }
    return result;
}

std::wstring Logger::GetLatestCrashLogPath() const {
    auto list = ListCrashLogs(1);
    return list.empty() ? std::wstring{} : list.front();
}

bool Logger::IsInitialized() const noexcept {
    return m_impl && m_impl->initialized.load(std::memory_order_acquire);
}

int Logger::GetRetentionDays() const noexcept {
    return m_impl ? m_impl->retentionDays.load(std::memory_order_acquire) : 7;
}

uint64_t Logger::GetDroppedCount() const noexcept {
    return m_impl ? m_impl->droppedCount.load(std::memory_order_relaxed) : 0;
}

const wchar_t* Logger::CategoryFolderName(LogCategory category) noexcept {
    switch (category) {
        case LogCategory::System:  return L"system";
        case LogCategory::Error:   return L"error";
        case LogCategory::Crash:   return L"crash";
        case LogCategory::Editor:  return L"editor";
        case LogCategory::Lsp:     return L"lsp";
        case LogCategory::Server:  return L"server";
        case LogCategory::Plugins: return L"plugins";
        default:                   return L"system";
    }
}

const wchar_t* Logger::CategoryDisplayName(LogCategory category) noexcept {
    switch (category) {
        case LogCategory::System:  return L"System";
        case LogCategory::Error:   return L"Diagnostics";
        case LogCategory::Crash:   return L"Crash";
        case LogCategory::Editor:  return L"Editor";
        case LogCategory::Lsp:     return L"LSP";
        case LogCategory::Server:  return L"Server & DB";
        case LogCategory::Plugins: return L"Plugins";
        default:                   return L"System";
    }
}

const char* Logger::CategoryTag(LogCategory category) noexcept {
    switch (category) {
        case LogCategory::System:  return "SYSTEM ";
        case LogCategory::Error:   return "ERROR  ";
        case LogCategory::Crash:   return "CRASH  ";
        case LogCategory::Editor:  return "EDITOR ";
        case LogCategory::Lsp:     return "LSP    ";
        case LogCategory::Server:  return "SERVER ";
        case LogCategory::Plugins: return "PLUGINS";
        default:                   return "SYSTEM ";
    }
}

const char* Logger::LevelTag(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default:              return "INFO ";
    }
}

std::wstring Logger::FormatHexCode(uint32_t code) {
    wchar_t buf[20]{};
    swprintf_s(buf, L"0x%08X", static_cast<unsigned>(code));
    return buf;
}

void Logger::SetSuppressCrashDialog(bool suppress) noexcept {
    g_crash.suppressDialog.store(suppress, std::memory_order_release);
}

std::wstring Logger::WriteSyntheticCrashReportForTest(const wchar_t* reason) {
    return WriteCrashReportSync(reason ? reason : L"Synthetic crash report test", nullptr, false);
}

} // namespace phide::app
