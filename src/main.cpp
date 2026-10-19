/**
 * @file main.cpp
 * @brief Application entry point for phide (Modern C++ IDE).
 * 
 * Configures Per-Monitor V2 High-DPI awareness, initializes COM apartments for Direct2D,
 * instantiates the main application window, and starts the Win32 message loop.
 */

#include <windows.h>
#include <objbase.h>
#include "phide/app/Window.hpp"

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
    (void)lpCmdLine;

    // 1. Enable Per-Monitor V2 High-DPI Awareness for razor-sharp typography and scaling
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 2. Initialize Component Object Model (COM) for Direct2D and DirectWrite
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        return 1;
    }

    // 3. Create Main Application Window
    phide::app::Window window;
    if (!window.Create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"Failed to initialize Direct2D / Window graphics pipeline.", L"phide Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // 4. Run Message Loop
    int exitCode = window.Run();

    // 5. Cleanup COM
    CoUninitialize();

    return exitCode;
}
