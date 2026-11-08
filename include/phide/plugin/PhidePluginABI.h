/**
 * @file PhidePluginABI.h
 * @brief Public C-ABI header for native Phide plugins, extensions, and mods (.dll).
 *
 * Native plugins export:
 *   extern "C" __declspec(dllexport) bool PhidePluginInitialize(const PhideHostApi* host, PhidePluginInfo* outInfo);
 *   extern "C" __declspec(dllexport) void PhidePluginShutdown();
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHIDE_PLUGIN_API_VERSION 1

typedef struct PhidePluginInfo {
    uint32_t apiVersion;
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* version;
    const wchar_t* author;
    const wchar_t* description;
} PhidePluginInfo;

typedef void (*PhideCommandCallback)(const wchar_t* commandId, void* userData);
typedef void (*PhideEventCallback)(const wchar_t* eventName, const wchar_t* payloadJson, void* userData);

/**
 * @brief Native code formatter callback.
 * @param languageExt Lowercase extension (e.g., L".php", L".py").
 * @param sourceText Input source text.
 * @param tabSize Indentation width.
 * @param outFormatted Buffer allocated by caller or via host; return true if formatted text written to outBuf.
 * @param outBufCapacity Capacity of outBuf in wchar_t elements.
 * @param userData User pointer passed at registration.
 */
typedef bool (*PhideFormatCallback)(
    const wchar_t* languageExt,
    const wchar_t* sourceText,
    int tabSize,
    wchar_t* outBuf,
    size_t outBufCapacity,
    void* userData
);

typedef struct PhideDiagnosticEntry {
    size_t line;
    size_t col;
    size_t length;
    int severity; /* 0 = Error, 1 = Warning, 2 = Info */
    wchar_t message[256];
} PhideDiagnosticEntry;

/**
 * @brief Native diagnostic linter callback.
 * @return Number of diagnostics written to outDiagnostics (up to maxDiagnostics).
 */
typedef size_t (*PhideLintCallback)(
    const wchar_t* filePath,
    const wchar_t* languageExt,
    const wchar_t* sourceText,
    PhideDiagnosticEntry* outDiagnostics,
    size_t maxDiagnostics,
    void* userData
);

typedef struct PhideHostApi {
    uint32_t apiVersion;

    /* Command & Keybinding Registration */
    void (*RegisterCommand)(
        const wchar_t* id,
        const wchar_t* title,
        const wchar_t* shortcut,
        PhideCommandCallback callback,
        void* userData
    );

    /* IntelliSense / Autocomplete Registration */
    void (*RegisterCompletion)(
        const wchar_t* languageExt,
        const wchar_t* label,
        const wchar_t* insertText,
        const wchar_t* detail,
        const wchar_t* documentation,
        int kind,
        int priority
    );

    /* Snippet Registration */
    void (*RegisterSnippet)(
        const wchar_t* prefix,
        const wchar_t* body,
        const wchar_t* description,
        const wchar_t* scope
    );

    /* Syntax Lexer Keyword / Type Modding */
    void (*RegisterKeywords)(
        const wchar_t* languageExt,
        const wchar_t* commaSeparatedWords,
        bool isType
    );

    /* Code Formatter Registration */
    void (*RegisterFormatter)(
        const wchar_t* languageExt,
        PhideFormatCallback callback,
        void* userData
    );

    /* Linter / Diagnostic Validator Registration */
    void (*RegisterLinter)(
        const wchar_t* languageExt,
        PhideLintCallback callback,
        void* userData
    );

    /* Status Bar Item Registration & Update */
    void (*RegisterStatusBarItem)(
        const wchar_t* id,
        const wchar_t* text,
        const wchar_t* tooltip,
        const wchar_t* commandId,
        bool alignRight,
        int priority
    );
    void (*UpdateStatusBarItem)(
        const wchar_t* id,
        const wchar_t* text,
        const wchar_t* tooltip
    );

    /* Theme Color Modding (themeMode: -1 for all themes, or 0..19 for specific ThemeMode) */
    void (*RegisterThemeColorOverride)(
        int themeMode,
        const wchar_t* colorKey,
        uint32_t rgbaHex
    );

    /* Project Framework Runner Registration */
    void (*RegisterFrameworkDetector)(
        const wchar_t* id,
        const wchar_t* title,
        const wchar_t* markerFile,
        const wchar_t* command,
        int port,
        const wchar_t* customUrl
    );

    /* Lifecycle Event Hook Registration */
    void (*RegisterEventHook)(
        const wchar_t* eventName,
        PhideEventCallback callback,
        void* userData
    );

    /* Editor & Host Interaction Callbacks */
    bool (*GetActiveFilePath)(wchar_t* outBuf, size_t maxLen);
    bool (*GetActiveDocumentText)(wchar_t* outBuf, size_t maxLen);
    bool (*SetActiveDocumentText)(const wchar_t* newText);
    bool (*InsertTextAtCursor)(const wchar_t* text);
    bool (*GetCursorPosition)(size_t* outLine, size_t* outCol);
    bool (*SetCursorPosition)(size_t line, size_t col);
    bool (*OpenFile)(const wchar_t* filePath);
    bool (*RunTerminalCommand)(const wchar_t* command);
    void (*ShowNotification)(const wchar_t* message);
    bool (*GetWorkspaceRoot)(wchar_t* outBuf, size_t maxLen);
} PhideHostApi;

typedef bool (*PhidePluginInitFn)(const PhideHostApi* host, PhidePluginInfo* outInfo);
typedef void (*PhidePluginShutdownFn)(void);

#ifdef __cplusplus
}
#endif
