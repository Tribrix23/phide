#pragma once

/**
 * @file Logger.hpp
 * @brief Asynchronous, categorized, date-partitioned application logger and crash reporter for phide.
 *
 * Design guarantees:
 * - Zero UI thread blocking: Log() only enqueues into a bounded in-memory queue and wakes a
 *   below-normal priority background writer thread. Disk I/O never executes on the UI thread.
 * - Categorized folders under %APPDATA%\phide\logs\<category>\<category>-YYYY-MM-DD.log:
 *     system/  - App lifecycle, DPI/D2D/window init, session, workspace, shutdown
 *     error/   - Dedicated aggregated WARN / ERROR / FATAL log across all subsystems
 *     crash/   - Synchronous crash reports (crash-YYYY-MM-DD_HH-MM-SS.log + optional .dmp)
 *     editor/  - File I/O, encoding, recovery/hot-exit, formatting, syntax issues
 *     lsp/     - Language server startup, resolution, crashes, protocol errors
 *     server/  - LiveServer, Nginx/PHP/phpMyAdmin, SQL/DB engines, DAP debugger
 *     plugins/ - Plugin discovery, loading, manifest/DLL failures
 * - Automatic retention: Deletes log and dump files strictly older than 7 days (1 week)
 *   asynchronously on startup and on daily calendar rollover.
 * - Crash safety: Installs SEH unhandled exception filter, std::terminate, purecall,
 *   invalid-parameter, and SIGABRT handlers that write a crash report with symbolized
 *   stack trace + recent in-memory activity breadcrumbs and inform the user of the log path.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace phide::app {

enum class LogCategory : uint8_t {
    System = 0,
    Error,
    Crash,
    Editor,
    Lsp,
    Server,
    Plugins,
    Count
};

enum class LogLevel : uint8_t {
    Debug = 0,
    Info,
    Warn,
    Error,
    Fatal
};

class Logger {
public:
    static Logger& Instance();

    /**
     * @brief Starts the asynchronous background writer thread and schedules 7-day retention cleanup.
     * @param customLogsRoot Optional override directory (empty = %APPDATA%\phide\logs).
     * @param retentionDays  Logs strictly older than this many days are automatically deleted (default: 7).
     */
    void Initialize(const std::wstring& customLogsRoot = {}, int retentionDays = 7);

    /**
     * @brief Registers Windows SEH, C++ terminate, purecall, invalid-parameter, and SIGABRT crash handlers.
     */
    void InstallCrashHandlers();

    /**
     * @brief Flushes pending entries and cleanly joins the background writer thread.
     */
    void Shutdown();

    /**
     * @brief Enqueues a log message without blocking or doing disk I/O on the caller thread.
     */
    void Log(LogCategory category, LogLevel level, const std::wstring& message);

    /**
     * @brief UTF-8 overload that converts and enqueues without blocking on disk I/O.
     */
    void LogUtf8(LogCategory category, LogLevel level, const std::string& message);

    void Debug(LogCategory category, const std::wstring& message) { Log(category, LogLevel::Debug, message); }
    void Info(LogCategory category, const std::wstring& message)  { Log(category, LogLevel::Info,  message); }
    void Warn(LogCategory category, const std::wstring& message)  { Log(category, LogLevel::Warn,  message); }
    void Error(LogCategory category, const std::wstring& message) { Log(category, LogLevel::Error, message); }
    void Fatal(LogCategory category, const std::wstring& message) { Log(category, LogLevel::Fatal, message); }

    /**
     * @brief Waits up to timeoutMs for queued log entries to be written by the background thread.
     */
    void Flush(uint32_t timeoutMs = 200);

    /**
     * @brief Writes a crash report synchronously from a top-level C++ catch handler and notifies the user.
     * @return Full path to the generated crash log file.
     */
    std::wstring ReportUnhandledCppException(const char* whatMessage);

    /**
     * @brief Deletes log/dump files older than the configured retention period immediately.
     * @return Number of files deleted.
     */
    size_t PruneOldLogsNow();

    /** @brief Ensures the logs root and all category subfolders exist. */
    void EnsureLogDirectoriesExist() const;

    /** @brief Returns the base logs directory (e.g. %APPDATA%\phide\logs). */
    [[nodiscard]] std::wstring GetLogsRootDir() const;

    /** @brief Returns the folder path for a given log category. */
    [[nodiscard]] std::wstring GetCategoryDir(LogCategory category) const;

    /** @brief Returns today's dated log path for a given category (for Crash, returns latest crash log if any). */
    [[nodiscard]] std::wstring GetTodayLogPath(LogCategory category) const;

    /**
     * @brief Flushes pending writes and guarantees today's log file exists on disk so the UI can open it.
     * @return Full path to today's log file for the category.
     */
    [[nodiscard]] std::wstring EnsureTodayLogFile(LogCategory category);

    /** @brief Returns the newest crash log file path in crash\, or empty if none exist. */
    [[nodiscard]] std::wstring GetLatestCrashLogPath() const;

    /** @brief Lists crash log files sorted newest-first. */
    [[nodiscard]] std::vector<std::wstring> ListCrashLogs(size_t maxCount = 20) const;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] int GetRetentionDays() const noexcept;
    [[nodiscard]] uint64_t GetDroppedCount() const noexcept;

    [[nodiscard]] static const wchar_t* CategoryFolderName(LogCategory category) noexcept;
    [[nodiscard]] static const wchar_t* CategoryDisplayName(LogCategory category) noexcept;
    [[nodiscard]] static const char* CategoryTag(LogCategory category) noexcept;
    [[nodiscard]] static const char* LevelTag(LogLevel level) noexcept;

    /** @brief Formats a Win32 error / HRESULT code as 0xXXXXXXXX. */
    [[nodiscard]] static std::wstring FormatHexCode(uint32_t code);

    /** @brief Suppresses the interactive crash MessageBox (used for headless / automated testing). */
    void SetSuppressCrashDialog(bool suppress) noexcept;

    /** @brief Writes a crash report synchronously without terminating (for automated tests). */
    std::wstring WriteSyntheticCrashReportForTest(const wchar_t* reason);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct Impl;
    Impl* m_impl = nullptr;
};

#define PHIDE_LOG_INFO(cat, msg)  ::phide::app::Logger::Instance().Info(::phide::app::LogCategory::cat, (msg))
#define PHIDE_LOG_WARN(cat, msg)  ::phide::app::Logger::Instance().Warn(::phide::app::LogCategory::cat, (msg))
#define PHIDE_LOG_ERROR(cat, msg) ::phide::app::Logger::Instance().Error(::phide::app::LogCategory::cat, (msg))
#define PHIDE_LOG_FATAL(cat, msg) ::phide::app::Logger::Instance().Fatal(::phide::app::LogCategory::cat, (msg))

} // namespace phide::app
