/**
 * @file GitLogManager.hpp
 * @brief Centralized real-time logging for all Git commands and outputs (matching Images 3 & 4).
 */

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace phide::ui {

struct GitLogLine {
    std::wstring text;
    bool isCommand = false;
    bool isError = false;
    bool isInfo = false;
};

class GitLogManager {
public:
    static GitLogManager& Instance() {
        static GitLogManager s_instance;
        return s_instance;
    }

    /**
     * @brief Formats current local time as YYYY-MM-DD HH:MM:SS.mmm
     */
    static std::wstring GetCurrentTimestamp() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
                   st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return std::wstring(buf);
    }

    /**
     * @brief Logs a completed git command with its output and execution duration.
     */
    void LogCommand(const std::wstring& cmd, int exitCode, const std::wstring& output, DWORD durationMs) {
        // Do not spam Git Logs with routine background status polling queries unless they produce an error
        if (exitCode == 0) {
            if (cmd.find(L"update-index") != std::wstring::npos ||
                cmd.find(L"status --porcelain") != std::wstring::npos ||
                cmd.find(L"diff --name-only") != std::wstring::npos ||
                cmd.find(L"diff --cached --name-only") != std::wstring::npos ||
                cmd.find(L"diff -U0 HEAD") != std::wstring::npos ||
                cmd.find(L"rev-list --count") != std::wstring::npos ||
                cmd.find(L"rev-parse --is-inside-work-tree") != std::wstring::npos ||
                cmd.find(L"branch --show-current") != std::wstring::npos ||
                cmd.find(L"status -sb") != std::wstring::npos ||
                cmd.find(L"log -n 40") != std::wstring::npos ||
                cmd.find(L"show HEAD:\"") != std::wstring::npos ||
                cmd.find(L"show --shortstat") != std::wstring::npos ||
                cmd.find(L"show --name-status") != std::wstring::npos ||
                cmd.find(L"ls-files --error-unmatch") != std::wstring::npos) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        std::wstring ts = GetCurrentTimestamp();

        // 1. Header line: YYYY-MM-DD HH:MM:SS.mmm [info] > git <cmd> [<duration>ms]
        std::wstring header = ts + L" [info] > " + cmd + L" [" + std::to_wstring(durationMs) + L"ms]";
        m_lines.push_back(header);
        m_structuredLines.push_back({ header, true, exitCode != 0, false });

        std::vector<std::wstring> newBatch;
        newBatch.push_back(header);

        // 2. Output lines
        if (!output.empty()) {
            std::wstringstream ss(output);
            std::wstring line;
            while (std::getline(ss, line)) {
                while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
                    line.pop_back();
                }
                if (line.empty()) continue;

                bool isErrLine = (exitCode != 0) || 
                                 (line.find(L"fatal:") != std::wstring::npos) || 
                                 (line.find(L"error:") != std::wstring::npos);

                std::wstring formattedLine = ts + L" [info] " + line;
                m_lines.push_back(formattedLine);
                m_structuredLines.push_back({ formattedLine, false, isErrLine, false });
                newBatch.push_back(formattedLine);
            }
        }

        // Keep buffer bounded to 5,000 lines
        if (m_lines.size() > 5000) {
            size_t drop = m_lines.size() - 5000;
            m_lines.erase(m_lines.begin(), m_lines.begin() + drop);
            m_structuredLines.erase(m_structuredLines.begin(), m_structuredLines.begin() + drop);
        }

        if (exitCode != 0) {
            m_lastFailedCommand = cmd;
            m_lastFailedOutput = output;
        }

        // Notify active real-time listeners (e.g. TerminalPanel's Git tab)
        for (const auto& listener : m_listeners) {
            if (listener) {
                listener(newBatch);
            }
        }
    }

    [[nodiscard]] std::vector<std::wstring> GetLogLines() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lines;
    }

    [[nodiscard]] std::vector<GitLogLine> GetStructuredLines() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_structuredLines;
    }

    [[nodiscard]] std::wstring GetLastFailedCommand() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastFailedCommand;
    }

    [[nodiscard]] std::wstring GetLastFailedOutput() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastFailedOutput;
    }

    void SetListener(std::function<void(const std::vector<std::wstring>&)> cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listeners.clear();
        if (cb) m_listeners.push_back(std::move(cb));
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lines.clear();
        m_structuredLines.clear();
        m_lastFailedCommand.clear();
        m_lastFailedOutput.clear();
    }

private:
    GitLogManager() = default;
    ~GitLogManager() = default;
    GitLogManager(const GitLogManager&) = delete;
    GitLogManager& operator=(const GitLogManager&) = delete;

    mutable std::mutex m_mutex;
    std::vector<std::wstring> m_lines;
    std::vector<GitLogLine> m_structuredLines;
    std::wstring m_lastFailedCommand;
    std::wstring m_lastFailedOutput;
    std::vector<std::function<void(const std::vector<std::wstring>&)>> m_listeners;
};

} // namespace phide::ui
