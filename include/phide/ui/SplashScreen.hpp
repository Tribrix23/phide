#pragma once

#include <windows.h>
#include <string>

namespace phide::ui {

/**
 * @class SplashScreen
 * @brief Displays the Phide architectural branding splash screen for 5 seconds on startup.
 */
class SplashScreen {
public:
    /**
     * @brief Shows the centered borderless splash screen for the specified duration in milliseconds.
     * @param hInstance Win32 application instance handle.
     * @param durationMs Duration in milliseconds to display (defaults to 5000 ms = 5 sec).
     */
    static void Show(HINSTANCE hInstance, int durationMs = 5000);

    /**
     * @brief Dismisses the splash screen window once the main IDE window is created and ready to paint.
     */
    static void Dismiss();

    /**
     * @brief Finds the resolved absolute path to assets/splash.jpg.
     */
    static std::wstring FindSplashImagePath();

private:
    static inline HWND s_hSplash = nullptr;
    static inline HINSTANCE s_hInstance = nullptr;
};

} // namespace phide::ui
