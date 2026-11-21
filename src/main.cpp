/**
 * @file main.cpp
 * @brief Application entry point for phide (Modern C++ IDE).
 * 
 * Configures Per-Monitor V2 High-DPI awareness, initializes COM apartments for Direct2D,
 * instantiates the main application window, and starts the Win32 message loop.
 */

#include <windows.h>
#include <objbase.h>
#include <exception>
#include <string>
#include "phide/app/Logger.hpp"
#include "phide/app/Window.hpp"
#include "phide/ui/SplashScreen.hpp"

/**
 * @brief Win32 Application Entry Point.
 * @param hInstance Current instance handle.
 * @param hPrevInstance Unused in modern Windows.
 * @param lpCmdLine Unicode command line string.
 * @param nCmdShow Initial window presentation mode.
 * @return Process exit code.
 */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;

    // 0. Initialize async categorized logger (%APPDATA%\phide\logs, 7-day retention) & crash handlers
    auto& logger = phide::app::Logger::Instance();
    logger.Initialize({}, 7);
    logger.InstallCrashHandlers();

    const bool isNewWindow = (lpCmdLine && (wcsstr(lpCmdLine, L"--new-window") != nullptr || wcsstr(lpCmdLine, L"--detach") != nullptr));
    PHIDE_LOG_INFO(System,
        L"phide starting (v2026.e-b, PID=" + std::to_wstring(GetCurrentProcessId()) +
        L", mode=" + (isNewWindow ? L"secondary-window" : L"primary") +
        L", logs=" + logger.GetLogsRootDir() + L")");

    // 1. Enable Per-Monitor V2 High-DPI Awareness for razor-sharp typography and scaling
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 2. Initialize Component Object Model (COM) for Direct2D and DirectWrite
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        PHIDE_LOG_FATAL(System, L"CoInitializeEx failed with HRESULT " + phide::app::Logger::FormatHexCode(static_cast<uint32_t>(hr)));
        logger.Flush(300);
        std::wstring errMsg = L"Failed to initialize Windows COM subsystem (" +
            phide::app::Logger::FormatHexCode(static_cast<uint32_t>(hr)) +
            L").\n\nDetails were logged to:\n" + logger.GetTodayLogPath(phide::app::LogCategory::Error);
        MessageBoxW(nullptr, errMsg.c_str(), L"phide Error", MB_ICONERROR);
        logger.Shutdown();
        return 1;
    }

    int exitCode = 0;
    try {
        // 3. Show Splash Screen on startup (bypassed if launched as secondary window via --new-window or --detach)
        if (!isNewWindow) {
            phide::ui::SplashScreen::Show(hInstance, 1200);
        }

        // 4. Create Main Application Window
        phide::app::Window window;
        if (!window.Create(hInstance, nCmdShow)) {
            PHIDE_LOG_FATAL(System, L"Window::Create failed to initialize Direct2D / Win32 pipeline.");
            logger.Flush(300);
            std::wstring errMsg = L"Failed to initialize Direct2D / Window graphics pipeline.\n\n"
                L"Error log saved to:\n" + logger.GetTodayLogPath(phide::app::LogCategory::Error) +
                L"\n\nSystem log saved to:\n" + logger.GetTodayLogPath(phide::app::LogCategory::System);
            MessageBoxW(nullptr, errMsg.c_str(), L"phide Error", MB_ICONERROR);
            CoUninitialize();
            logger.Shutdown();
            return 1;
        }

        PHIDE_LOG_INFO(System, L"Main window created successfully; entering message loop.");

        // 5. Run Message Loop
        exitCode = window.Run();
        PHIDE_LOG_INFO(System, L"Message loop exited cleanly (exitCode=" + std::to_wstring(exitCode) + L").");
    } catch (const std::exception& ex) {
        logger.ReportUnhandledCppException(ex.what());
        CoUninitialize();
        logger.Shutdown();
        return 1;
    } catch (...) {
        logger.ReportUnhandledCppException("Unknown C++ exception escaped wWinMain");
        CoUninitialize();
        logger.Shutdown();
        return 1;
    }

    // 6. Cleanup COM & flush logs
    CoUninitialize();
    logger.Shutdown();

    return exitCode;
}
