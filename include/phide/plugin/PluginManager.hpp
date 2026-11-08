/**
 * @file PluginManager.hpp
 * @brief Unified Extension, Plugin, and Modding Architecture for Phide (VS Code & JetBrains style).
 *
 * Supports:
 * 1. Declarative JSON Extensions & Mods (plugin.json / extension.json / mod.json / package.json)
 * 2. Native Dynamic Library Plugins (.dll via PhidePluginABI.h)
 * 3. External Script / CLI Providers (formatters, linters, commands, lifecycle hooks)
 *
 * Extends every IDE subsystem without adding UI clutter:
 * - Theme & UI Color Modding (ThemeColors overrides globally or per-theme)
 * - AppConfig & Layout Metrics Modding (geometry, editor settings, feature flags)
 * - Syntax Lexer & Custom Language Highlighting (keywords, types, extensions)
 * - IntelliSense & Autocomplete Providers
 * - Custom Snippets
 * - Code Formatters (native callback + CLI stdin/stdout or file templates)
 * - Syntax Validators & Diagnostic Linters (native callback + regex rules + CLI linters)
 * - Commands, Keybindings & Command Palette integration
 * - Custom Project Framework Detectors & Dev Server Runners
 * - Status Bar Items (left/right pills with tooltips and click commands)
 * - Full IDE Lifecycle Event Hooks (startup, shutdown, workspace, document, git, API tester)
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <filesystem>

#include "phide/plugin/PhidePluginABI.h"
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/editor/SnippetManager.hpp"
#include "phide/editor/SyntaxValidator.hpp"
#include "phide/editor/Buffer.hpp"

namespace phide::plugin {

struct PluginMetadata {
    std::wstring id;
    std::wstring name;
    std::wstring version = L"1.0.0";
    std::wstring author;
    std::wstring description;
    std::wstring rootDir;
    std::wstring manifestPath;
    std::wstring mainDllPath;
    bool enabled = true;
    bool isNativeDll = false;
    HMODULE hModule = nullptr;
    PhidePluginShutdownFn shutdownFn = nullptr;
};

struct PluginCommand {
    std::wstring id;
    std::wstring title;
    std::wstring shortcut;               ///< e.g. "Ctrl+Alt+M" or "Ctrl+Shift+L"
    std::vector<std::wstring> keyTokens; ///< Parsed tokens e.g. {"Ctrl", "Alt", "M"}
    std::wstring when = L"global";
    std::wstring sourcePluginId;

    // Execution targets (any combination):
    PhideCommandCallback nativeCallback = nullptr;
    void* nativeUserData = nullptr;
    std::function<void()> cppCallback;
    std::wstring shellCommand;           ///< External CLI/script command (supports {file}, {workspace}, {line})
    std::wstring insertText;             ///< Text to insert into active editor
    bool runInTerminal = false;          ///< True to run shellCommand in integrated terminal
    bool replaceDocumentWithStdout = false; ///< True to replace active editor text with stdout of shellCommand
};

struct PluginFormatter {
    std::wstring languageExt;            ///< Lowercase extension e.g. ".php", ".py", ".rs", or "*"
    std::wstring sourcePluginId;
    PhideFormatCallback nativeCallback = nullptr;
    void* nativeUserData = nullptr;
    std::function<std::wstring(const std::wstring& src, int tabSize)> cppCallback;
    std::wstring cliCommand;             ///< External CLI command (stdin->stdout or {file})
    bool useStdin = true;
};

struct PluginRegexLintRule {
    std::wstring pattern;
    std::wstring message;
    int severity = 2; ///< 1 = Error, 2 = Warning, 3 = Info, 4 = Hint
};

struct PluginLinter {
    std::wstring languageExt;            ///< Lowercase extension e.g. ".php", ".py", or "*"
    std::wstring sourcePluginId;
    PhideLintCallback nativeCallback = nullptr;
    void* nativeUserData = nullptr;
    std::function<std::vector<lsp::Diagnostic>(const std::wstring& filePath, const editor::Buffer& buf)> cppCallback;
    std::vector<PluginRegexLintRule> regexRules;
    std::wstring cliCommand;             ///< External CLI linter outputting "line:col: severity: message"
};

struct PluginStatusBarItem {
    std::wstring id;
    std::wstring text;
    std::wstring tooltip;
    std::wstring commandId;
    bool alignRight = true;
    int priority = 0;
    D2D1_RECT_F bounds{};
    bool isHovered = false;
};

struct PluginFrameworkDetector {
    std::wstring id;
    std::wstring title;
    std::wstring markerFile;             ///< e.g. "Cargo.toml", "go.mod", "artisan", "deno.json"
    std::wstring markerContains;         ///< Optional substring required inside markerFile
    std::wstring fileExtension;          ///< Optional single-file fallback extension e.g. ".rs", ".go"
    std::wstring command;                ///< e.g. "cargo run", "go run .", "php artisan serve"
    int port = 0;
    std::wstring customUrl;
};

struct PluginEventHook {
    std::wstring eventName;              ///< e.g. "onStartup", "onDocumentSave", "onGitPreCommit", etc.
    PhideEventCallback nativeCallback = nullptr;
    void* nativeUserData = nullptr;
    std::function<void(const std::wstring& payload)> cppCallback;
    std::wstring shellCommand;           ///< Optional background CLI/script hook
};

struct HostCallbacks {
    std::function<std::wstring()> getActiveFilePath;
    std::function<std::wstring()> getActiveDocumentText;
    std::function<bool(const std::wstring&)> setActiveDocumentText;
    std::function<bool(const std::wstring&)> insertTextAtCursor;
    std::function<bool(size_t&, size_t&)> getCursorPosition;
    std::function<bool(size_t, size_t)> setCursorPosition;
    std::function<bool(const std::wstring&)> openFile;
    std::function<bool(const std::wstring&)> runTerminalCommand;
    std::function<void(const std::wstring&)> showNotification;
    std::function<std::wstring()> getWorkspaceRoot;
    std::function<void(const std::wstring&)> executeBuiltinCommand;
};

class PluginManager {
public:
    static PluginManager& Instance();

    /**
     * @brief Initializes plugin discovery from global directories (<exe>/plugins, <exe>/extensions,
     *        <exe>/mods, %LOCALAPPDATA%/phide/{plugins,extensions,mods}, %APPDATA%/phide/plugins).
     */
    void Initialize(const std::wstring& exeDir = L"");

    /**
     * @brief Loads workspace-scoped plugins, extensions, and mods from <workspaceRoot>/.phide/{plugins,extensions,mods}.
     */
    void LoadWorkspacePlugins(const std::wstring& workspaceRoot);

    /**
     * @brief Reloads all plugins, manifests, and mods.
     */
    void ReloadAll();

    /**
     * @brief Unloads all plugins and releases native DLL modules.
     */
    void Shutdown();

    /**
     * @brief Loads a single plugin/extension/mod directory or manifest file directly.
     */
    bool LoadPluginFromManifest(const std::filesystem::path& manifestPath);

    /**
     * @brief Loads a native .dll plugin directly.
     */
    bool LoadNativeDllPlugin(const std::filesystem::path& dllPath);

    /**
     * @brief Parses a JSON manifest string directly (used by tests and runtime dynamic registration).
     */
    bool LoadPluginFromJsonString(const std::wstring& jsonContent, const std::wstring& baseDir = L"");

    /**
     * @brief Binds host window callbacks so plugins can interact with the active editor, terminal, and workspace.
     */
    void SetHostCallbacks(const HostCallbacks& callbacks);

    // --- 1. Theme & UI Modding ---
    void RegisterThemeColorOverride(int themeMode, const std::wstring& colorKey, D2D1_COLOR_F color);
    void ApplyThemeOverrides(ui::ThemeColors& colors, int currentThemeMode) const;
    void ApplyAppConfigMods(app::AppConfig& config) const;

    // --- 2. Syntax Lexer & Custom Language Modding ---
    void RegisterLanguageKeywords(const std::wstring& ext, const std::vector<std::wstring>& words, bool isType);
    bool IsCustomKeyword(const std::wstring& ext, const std::wstring& word) const;
    bool IsCustomType(const std::wstring& ext, const std::wstring& word) const;
    std::wstring ResolveLanguageAlias(const std::wstring& ext) const;

    // --- 3. IntelliSense & Autocomplete ---
    void RegisterCompletionItem(const std::wstring& ext, const editor::IntelliSenseItem& item);
    std::vector<editor::IntelliSenseItem> QueryCompletions(
        const std::wstring& ext,
        const std::wstring& fileName,
        const std::wstring& currentLine,
        size_t cursorCol,
        const std::wstring& prefix
    ) const;

    // --- 4. Snippets ---
    void RegisterSnippet(const editor::Snippet& snippet);
    std::vector<editor::Snippet> GetContributedSnippets() const;

    // --- 5. Formatters ---
    void RegisterFormatter(const PluginFormatter& formatter);
    bool CanFormat(const std::wstring& ext) const;
    bool TryFormat(const std::wstring& src, const std::wstring& ext, int tabSize, std::wstring& outFormatted) const;

    // --- 6. Diagnostics & Linters ---
    void RegisterLinter(const PluginLinter& linter);
    std::vector<lsp::Diagnostic> RunDiagnostics(
        const std::wstring& filePath,
        const std::wstring& ext,
        const editor::Buffer& buffer
    ) const;

    // --- 7. Commands, Keybindings & Command Palette ---
    void RegisterCommand(const PluginCommand& cmd);
    std::vector<PluginCommand> GetCommands() const;
    bool ExecuteCommand(const std::wstring& commandId);
    bool TryHandleKeyBinding(UINT vk, bool ctrl, bool shift, bool alt);

    // --- 8. Project Framework Detectors ---
    void RegisterFrameworkDetector(const PluginFrameworkDetector& detector);
    bool TryDetectFramework(
        const std::wstring& rootDir,
        const std::wstring& activeFilePath,
        PluginFrameworkDetector& outMatched,
        std::wstring& outMarkerPath
    ) const;

    // --- 9. Status Bar Items ---
    void RegisterStatusBarItem(const PluginStatusBarItem& item);
    void UpdateStatusBarItem(const std::wstring& id, const std::wstring& text, const std::wstring& tooltip);
    std::vector<PluginStatusBarItem>& GetStatusBarItemsMutable();
    std::vector<PluginStatusBarItem> GetStatusBarItems() const;

    // --- 10. Lifecycle Event Hooks ---
    void RegisterEventHook(const PluginEventHook& hook);
    void DispatchEvent(const std::wstring& eventName, const std::wstring& payloadJson = L"{}");

    // --- Metadata Inspection ---
    std::vector<PluginMetadata> GetLoadedPlugins() const;
    const PhideHostApi* GetHostApiTable() const { return &m_hostApi; }
    const HostCallbacks& GetHostCallbacks() const { return m_hostCallbacks; }

private:
    PluginManager();
    ~PluginManager();

    void InitHostApiTable();
    void ScanDirectoryForPlugins(const std::filesystem::path& dirPath);
    std::wstring ExpandVariables(const std::wstring& templateStr) const;
    static std::wstring NormalizeExt(const std::wstring& ext);
    static bool ParseHexColor(const std::wstring& hexStr, D2D1_COLOR_F& outColor);
    static std::vector<std::wstring> ParseShortcutTokens(const std::wstring& shortcut);

    mutable std::recursive_mutex m_mutex;
    bool m_initialized = false;
    std::wstring m_exeDir;
    std::wstring m_workspaceRoot;

    PhideHostApi m_hostApi{};
    HostCallbacks m_hostCallbacks{};

    std::vector<PluginMetadata> m_plugins;
    std::unordered_set<std::wstring> m_loadedManifestPaths;

    // Theme overrides: themeMode (-1 for all) -> (colorKey -> D2D1_COLOR_F)
    std::unordered_map<int, std::unordered_map<std::wstring, D2D1_COLOR_F>> m_themeOverrides;

    // AppConfig overrides: key -> value string
    std::unordered_map<std::wstring, std::wstring> m_configOverrides;

    // Custom language keywords & types: ext (e.g. ".rs" or "*") -> set of words
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_customKeywords;
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_customTypes;
    std::unordered_map<std::wstring, std::wstring> m_languageAliases; // e.g. ". blade.php" -> ".php"

    // Custom completions: ext -> vector<IntelliSenseItem>
    std::unordered_map<std::wstring, std::vector<editor::IntelliSenseItem>> m_completions;

    // Custom snippets
    std::vector<editor::Snippet> m_snippets;

    // Custom formatters: ext -> vector<PluginFormatter>
    std::unordered_map<std::wstring, std::vector<PluginFormatter>> m_formatters;

    // Custom linters: ext -> vector<PluginLinter>
    std::unordered_map<std::wstring, std::vector<PluginLinter>> m_linters;

    // Custom commands
    std::vector<PluginCommand> m_commands;

    // Custom framework detectors
    std::vector<PluginFrameworkDetector> m_frameworkDetectors;

    // Custom status bar items
    std::vector<PluginStatusBarItem> m_statusBarItems;

    // Event hooks: eventName -> vector<PluginEventHook>
    std::unordered_map<std::wstring, std::vector<PluginEventHook>> m_eventHooks;
};

} // namespace phide::plugin
