/**
 * @file SettingsView.cpp
 * @brief Implementation of the modern, categorized, card-based Settings view.
 */

#include "phide/ui/SettingsView.hpp"
#include <algorithm>
#include <cwctype>

namespace phide::ui {

static std::wstring ThemeModeToDisplayName(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Dark:              return L"Dark Mode";
        case ThemeMode::Light:             return L"Light Mode";
        case ThemeMode::Auto:              return L"Auto (System)";
        case ThemeMode::Purple:            return L"Deep Purple";
        case ThemeMode::Cyan:              return L"Cyan Cyber";
        case ThemeMode::Turquoise:         return L"Turquoise Teal";
        case ThemeMode::Monokai:           return L"Monokai Pro";
        case ThemeMode::Dracula:           return L"Dracula";
        case ThemeMode::Nord:              return L"Nord Arctic";
        case ThemeMode::Solarized:         return L"Solarized Dark";
        case ThemeMode::GitHubDark:        return L"GitHub Dark";
        case ThemeMode::OneDark:           return L"One Dark Pro";
        case ThemeMode::SynthWave:         return L"SynthWave '84";
        case ThemeMode::RetroGrid80s:      return L"Retro Grid '80s";
        case ThemeMode::CosmicStarfield:   return L"Cosmic Starfield";
        case ThemeMode::MatrixDigitalRain: return L"Matrix Rain";
        case ThemeMode::AuroraBorealis:    return L"Aurora Borealis";
        case ThemeMode::SunsetGlow:        return L"Sunset Glow";
        case ThemeMode::EmeraldForest:     return L"Emerald Forest";
        case ThemeMode::RosePine:          return L"Rosé Pine";
        default:                           return L"Dark Mode";
    }
}

static ThemeMode DisplayNameToThemeMode(const std::wstring& name) {
    if (name == L"Light Mode")             return ThemeMode::Light;
    if (name == L"Auto (System)")          return ThemeMode::Auto;
    if (name == L"Deep Purple")            return ThemeMode::Purple;
    if (name == L"Cyan Cyber")             return ThemeMode::Cyan;
    if (name == L"Turquoise Teal")         return ThemeMode::Turquoise;
    if (name == L"Monokai Pro")            return ThemeMode::Monokai;
    if (name == L"Dracula")                return ThemeMode::Dracula;
    if (name == L"Nord Arctic")            return ThemeMode::Nord;
    if (name == L"Solarized Dark")         return ThemeMode::Solarized;
    if (name == L"GitHub Dark")            return ThemeMode::GitHubDark;
    if (name == L"One Dark Pro")           return ThemeMode::OneDark;
    if (name == L"SynthWave '84")          return ThemeMode::SynthWave;
    if (name == L"Retro Grid '80s")        return ThemeMode::RetroGrid80s;
    if (name == L"Cosmic Starfield")       return ThemeMode::CosmicStarfield;
    if (name == L"Matrix Rain")            return ThemeMode::MatrixDigitalRain;
    if (name == L"Aurora Borealis")        return ThemeMode::AuroraBorealis;
    if (name == L"Sunset Glow")            return ThemeMode::SunsetGlow;
    if (name == L"Emerald Forest")         return ThemeMode::EmeraldForest;
    if (name == L"Rosé Pine")              return ThemeMode::RosePine;
    return ThemeMode::Dark;
}

static const wchar_t* DockPositionToName(int pos) {
    switch (pos) {
        case 1:  return L"Editor Area";
        case 2:  return L"Right Side";
        default: return L"Bottom Panel";
    }
}

static int NameToDockPosition(const std::wstring& name) {
    if (name == L"Editor Area") return 1;
    if (name == L"Right Side")  return 2;
    return 0;
}

static float MeasureTextWidthDWrite(IDWriteFactory* pFactory, IDWriteTextFormat* pFmt, const std::wstring& text, float fallbackCharW) {
    if (text.empty()) return 0.0f;
    if (pFactory && pFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(pFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), pFmt, 2000.0f, 100.0f, &pLayout)) && pLayout) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                return tm.widthIncludingTrailingWhitespace;
            }
        }
    }
    return static_cast<float>(text.size()) * fallbackCharW;
}

static bool* ResolveBoolSettingPtr(const std::wstring& id, app::AppConfig& cfg) {
    if (id == L"editor.wordWrap")                         return &cfg.wordWrap;
    if (id == L"editor.minimap")                          return &cfg.showMinimap;
    if (id == L"editor.codeLens")                         return &cfg.enableCodeLens;
    if (id == L"editor.lineNumbers")                      return &cfg.showLineNumbers;
    if (id == L"editor.insertSpaces")                     return &cfg.insertSpaces;
    if (id == L"editor.formatOnSave")                     return &cfg.formatOnSave;
    if (id == L"editor.stickyScroll.enabled")             return &cfg.enableStickyScroll;
    if (id == L"editor.renderControlCharacters")          return &cfg.renderControlChars;
    if (id == L"editor.linkedEditing")                    return &cfg.enableLinkedEditing;
    if (id == L"editor.bracketPairColorization.enabled")  return &cfg.enableBracketPairColorization;
    if (id == L"editor.folding")                          return &cfg.enableCodeFolding;
    if (id == L"explorer.updateImportsOnRename")          return &cfg.updateImportsOnRename;
    if (id == L"files.trimTrailingWhitespace")             return &cfg.trimTrailingWhitespace;
    if (id == L"files.insertFinalNewline")                return &cfg.insertFinalNewline;
    if (id == L"files.enableEditorConfig")                return &cfg.enableEditorConfig;
    if (id == L"files.autoIgnorePhide")                   return &cfg.autoIgnorePhideDir;
    if (id == L"search.useIgnoreFiles")                   return &cfg.searchUseIgnoreFiles;
    if (id == L"workbench.breadcrumbs")                  return &cfg.showBreadcrumbs;
    if (id == L"workbench.statusBar")                    return &cfg.showStatusBar;
    if (id == L"workbench.menuBar")                      return &cfg.showMenuBar;
    if (id == L"workbench.centeredLayout")               return &cfg.centeredLayout;
    if (id == L"workbench.graphColored")                 return &cfg.isGraphColored;
    if (id == L"workbench.markdownPreview")              return &cfg.enableMarkdownPreview;
    if (id == L"workbench.zenMode")                      return &cfg.zenMode;
    if (id == L"editor.gitLens")                          return &cfg.enableGitLens;
    if (id == L"security.privacyMask")                    return &cfg.enablePrivacyMask;
    if (id == L"editor.inlayHints")                       return &cfg.enableInlayHints;
    if (id == L"editor.lspHover")                         return &cfg.enableLspHover;
    if (id == L"editor.signatureHelp")                    return &cfg.enableSignatureHelp;
    if (id == L"linter.externalEnabled")                  return &cfg.enableExternalLinters;
    if (id == L"emmet.enabled")                           return &cfg.enableEmmet;
    if (id == L"intellisense.tailwindClasses")            return &cfg.enableTailwindIntelliSense;
    if (id == L"intellisense.databaseSchema")             return &cfg.enableDbSchemaIntelliSense;
    if (id == L"code-runner.runInTerminal")               return &cfg.codeRunnerRunInTerminal;
    if (id == L"code-runner.saveFileBeforeRun")           return &cfg.codeRunnerSaveFileBeforeRun;
    if (id == L"code-runner.clearPreviousOutput")         return &cfg.codeRunnerClearPreviousOutput;
    if (id == L"python.linting.enabled")                  return &cfg.pythonLintingEnabled;
    if (id == L"python.linting.undefinedVariableCheck")    return &cfg.pythonCheckUndefinedVariables;
    if (id == L"python.terminal.highlightErrorsInEditor") return &cfg.pythonHighlightTerminalErrors;
    if (id == L"git.diffSideBySide")                     return &cfg.gitDiffSideBySide;
    if (id == L"git.stashExcludePhide")                  return &cfg.gitSmartStashExcludePhide;
    if (id == L"git.autoRefreshGraph")                    return &cfg.gitAutoRefreshGraph;
    if (id == L"debug.inlineValues")                      return &cfg.debugInlineValues;
    if (id == L"debug.openDebugOnBreak")                  return &cfg.debugOpenOnBreak;
    if (id == L"liveServer.hotReload")                    return &cfg.liveServerHotReload;
    if (id == L"editor.smoothScrolling")                  return &cfg.smoothScrolling;
    if (id == L"editor.scrollBeyondLastLine")             return &cfg.scrollBeyondLastLine;
    if (id == L"editor.highlightActiveIndentGuide")       return &cfg.highlightActiveIndentGuide;
    if (id == L"editor.guides.indentation")               return &cfg.showIndentGuides;
    if (id == L"editor.matchBrackets")                    return &cfg.highlightMatchingBrackets;
    if (id == L"editor.formatOnPaste")                    return &cfg.formatOnPaste;
    if (id == L"editor.trimAutoWhitespace")               return &cfg.trimAutoWhitespace;
    if (id == L"workbench.activityBar.visible")           return &cfg.showActivityBar;
    if (id == L"workbench.editor.showTabs")               return &cfg.showEditorTabs;
    if (id == L"workbench.commandCenter")                 return &cfg.showCommandCenter;
    if (id == L"files.excludeDotfiles")                   return &cfg.explorerHideDotfiles;
    if (id == L"search.caseSensitive")                    return &cfg.searchDefaultCaseSensitive;
    if (id == L"search.wholeWord")                        return &cfg.searchDefaultWholeWord;
    if (id == L"editor.quickSuggestions")                 return &cfg.enableQuickSuggestions;
    if (id == L"editor.suggest.snippets")                 return &cfg.enableSnippetSuggestions;
    if (id == L"editor.parameterHints.cycle")             return &cfg.cycleParameterHints;
    if (id == L"editor.diagnostics.onType")                return &cfg.validateDiagnosticsOnType;
    if (id == L"terminal.integrated.copyOnSelection")      return &cfg.terminalCopyOnSelection;
    if (id == L"code-runner.preserveFocus")               return &cfg.codeRunnerPreserveFocus;
    if (id == L"extensions.autoCheckUpdates")             return &cfg.extensionsAutoCheckUpdates;
    if (id == L"extensions.autoUpdate")                   return &cfg.extensionsAutoUpdate;
    if (id == L"php.validate.enable")                     return &cfg.phpLintingEnabled;
    if (id == L"javascript.validate.enable")              return &cfg.jsTsLintingEnabled;
    if (id == L"css.validate.enable")                     return &cfg.cssLintingEnabled;
    if (id == L"markdown.lint.enable")                   return &cfg.markdownLintEnabled;
    if (id == L"git.enabled")                             return &cfg.gitEnabled;
    if (id == L"git.autofetch")                           return &cfg.gitAutoFetch;
    if (id == L"git.confirmSync")                         return &cfg.gitConfirmSync;
    if (id == L"git.enableSmartCommit")                   return &cfg.gitSmartCommit;
    if (id == L"git.showInlineBlame")                     return &cfg.gitShowInlineBlame;
    if (id == L"git.decorations.enabled")                 return &cfg.gitExplorerDecorations;
    if (id == L"debug.allowBreakpointsEverywhere")        return &cfg.debugBreakpointsEverywhere;
    if (id == L"debug.showSubSessionsInToolBar")          return &cfg.debugFloatingToolbar;
    if (id == L"database.autoDiscoverServers")            return &cfg.dbAutoDiscoverServers;
    if (id == L"liveServer.autoOpenBrowser")              return &cfg.liveServerAutoOpenBrowser;
    if (id == L"liveServer.https")                        return &cfg.liveServerHttps;
    if (id == L"coverage.showGutter")                     return &cfg.coverageShowGutter;
    if (id == L"markdown.syncScroll")                     return &cfg.markdownSyncScroll;
    return nullptr;
}

static bool ReadBoolSetting(const std::wstring& id, const app::AppConfig& cfg) {
    app::AppConfig& mutableCfg = const_cast<app::AppConfig&>(cfg);
    if (const bool* ptr = ResolveBoolSettingPtr(id, mutableCfg)) {
        return *ptr;
    }
    return false;
}

static std::wstring ReadDropdownSetting(const std::wstring& id, const app::AppConfig& cfg, const Theme& theme) {
    if (id == L"files.autoSave")                   return cfg.autoSave;
    if (id == L"files.eol")                        return cfg.defaultEol;
    if (id == L"files.encoding")                   return cfg.defaultEncoding;
    if (id == L"editor.fontFamily")                return cfg.fontFamily;
    if (id == L"editor.renderWhitespace")          return cfg.renderWhitespaceMode;
    if (id == L"editor.cursorStyle")               return cfg.cursorStyle;
    if (id == L"editor.autoClosingBrackets")       return cfg.autoClosingBrackets;
    if (id == L"editor.autoClosingQuotes")         return cfg.autoClosingQuotes;
    if (id == L"editor.multiCursorModifier")       return cfg.multiCursorModifier;
    if (id == L"workbench.colorTheme")             return ThemeModeToDisplayName(theme.GetThemeMode());
    if (id == L"workbench.sideBar.location")       return cfg.sideBarOnRight ? L"right" : L"left";
    if (id == L"terminal.integrated.dockPosition") return DockPositionToName(cfg.terminalDockPosition);
    if (id == L"code-runner.executorMap.python")   return cfg.codeRunnerExecutorPython;
    if (id == L"python.linting.linter")            return cfg.pythonLinter;
    if (id == L"lsp.typescript.executable")        return cfg.lspPathTypeScript.empty() ? L"auto-detect" : cfg.lspPathTypeScript;
    if (id == L"lsp.python.executable")            return cfg.lspPathPython.empty() ? L"auto-detect" : cfg.lspPathPython;
    if (id == L"lsp.php.executable")               return cfg.lspPathPhp.empty() ? L"auto-detect" : cfg.lspPathPhp;
    if (id == L"lsp.cpp.executable")               return cfg.lspPathCpp.empty() ? L"auto-detect" : cfg.lspPathCpp;
    if (id == L"editor.cursorBlinking")            return cfg.cursorBlinking;
    if (id == L"files.hotExit")                    return cfg.hotExit;
    if (id == L"terminal.integrated.shell.windows") return cfg.terminalDefaultShell;
    if (id == L"terminal.integrated.cursorStyle")  return cfg.terminalCursorStyle;
    if (id == L"liveServer.root")                  return cfg.liveServerRoot;
    if (id == L"editor.rulers")                    return cfg.editorRulers;
    return L"";
}

static void WriteDropdownSetting(const std::wstring& id, const std::wstring& val, app::AppConfig& cfg, Theme& theme) {
    if (id == L"files.autoSave") {
        cfg.autoSave = val;
    } else if (id == L"files.eol") {
        cfg.defaultEol = val;
    } else if (id == L"files.encoding") {
        cfg.defaultEncoding = val;
    } else if (id == L"editor.fontFamily") {
        cfg.fontFamily = val;
    } else if (id == L"editor.renderWhitespace") {
        cfg.renderWhitespaceMode = val;
        cfg.renderWhitespace = (val != L"none");
    } else if (id == L"editor.cursorStyle") {
        cfg.cursorStyle = val;
    } else if (id == L"editor.autoClosingBrackets") {
        cfg.autoClosingBrackets = val;
    } else if (id == L"editor.autoClosingQuotes") {
        cfg.autoClosingQuotes = val;
    } else if (id == L"editor.multiCursorModifier") {
        cfg.multiCursorModifier = val;
    } else if (id == L"editor.rulers") {
        cfg.editorRulers = val;
    } else if (id == L"workbench.colorTheme") {
        theme.SetThemeMode(DisplayNameToThemeMode(val));
    } else if (id == L"workbench.sideBar.location") {
        cfg.sideBarOnRight = (val == L"right");
    } else if (id == L"terminal.integrated.dockPosition") {
        cfg.terminalDockPosition = NameToDockPosition(val);
    } else if (id == L"code-runner.executorMap.python") {
        cfg.codeRunnerExecutorPython = val;
    } else if (id == L"python.linting.linter") {
        cfg.pythonLinter = val;
    } else if (id == L"lsp.typescript.executable") {
        cfg.lspPathTypeScript = (val == L"auto-detect") ? L"" : val;
    } else if (id == L"lsp.python.executable") {
        cfg.lspPathPython = (val == L"auto-detect") ? L"" : val;
    } else if (id == L"lsp.php.executable") {
        cfg.lspPathPhp = (val == L"auto-detect") ? L"" : val;
    } else if (id == L"lsp.cpp.executable") {
        cfg.lspPathCpp = (val == L"auto-detect") ? L"" : val;
    } else if (id == L"editor.cursorBlinking") {
        cfg.cursorBlinking = val;
    } else if (id == L"files.hotExit") {
        cfg.hotExit = val;
    } else if (id == L"terminal.integrated.shell.windows") {
        cfg.terminalDefaultShell = val;
    } else if (id == L"terminal.integrated.cursorStyle") {
        cfg.terminalCursorStyle = val;
    } else if (id == L"liveServer.root") {
        cfg.liveServerRoot = val;
    }
}

static std::wstring FormatStepperSettingValue(const SettingItem& it, const app::AppConfig& cfg) {
    if (it.id == L"editor.fontSize")                    return std::to_wstring(static_cast<int>(cfg.fontSize)) + it.unit;
    if (it.id == L"editor.tabSize")                     return std::to_wstring(cfg.tabSize) + it.unit;
    if (it.id == L"files.autoSaveDelay")                return std::to_wstring(cfg.autoSaveDelay) + it.unit;
    if (it.id == L"files.largeFileMmapThresholdMB")     return std::to_wstring(cfg.largeFileMmapThresholdMB) + it.unit;
    if (it.id == L"workbench.drawerWidth")              return std::to_wstring(cfg.drawerWidth) + it.unit;
    if (it.id == L"terminal.integrated.defaultHeight")  return std::to_wstring(static_cast<int>(cfg.terminalHeight)) + it.unit;
    if (it.id == L"terminal.integrated.rightWidth")     return std::to_wstring(static_cast<int>(cfg.terminalRightWidth)) + it.unit;
    if (it.id == L"database.sqlPageSize")               return std::to_wstring(cfg.sqlPageSize) + it.unit;
    if (it.id == L"liveServer.port")                    return std::to_wstring(cfg.liveServerPort) + it.unit;
    if (it.id == L"workbench.tree.indent")              return std::to_wstring(cfg.explorerTreeIndent) + it.unit;
    if (it.id == L"search.maxResults")                  return std::to_wstring(cfg.searchMaxResults) + it.unit;
    if (it.id == L"terminal.integrated.fontSize")       return std::to_wstring(cfg.terminalFontSize) + it.unit;
    if (it.id == L"terminal.integrated.scrollback")     return std::to_wstring(cfg.terminalScrollbackLines) + it.unit;
    if (it.id == L"database.queryTimeoutSec")           return std::to_wstring(cfg.dbQueryTimeoutSec) + it.unit;
    if (it.id == L"editor.lineHeight") {
        wchar_t buf[32] = { 0 };
        swprintf_s(buf, L"%.2f", cfg.lineHeightMultiplier);
        return std::wstring(buf) + it.unit;
    }
    return L"";
}

static void StepNumericSettingValue(const SettingItem& it, int dir, app::AppConfig& cfg) {
    auto stepInt = [&](int& target) {
        int delta = static_cast<int>(it.stepVal) * dir;
        target = (std::clamp)(target + delta, static_cast<int>(it.minVal), static_cast<int>(it.maxVal));
    };
    auto stepFloat = [&](float& target, float precisionStep) {
        float next = target + it.stepVal * static_cast<float>(dir);
        if (precisionStep > 0.0f) {
            next = std::round(next / precisionStep) * precisionStep;
        }
        target = (std::clamp)(next, it.minVal, it.maxVal);
    };

    if (it.id == L"editor.fontSize")                    stepFloat(cfg.fontSize, 1.0f);
    else if (it.id == L"editor.lineHeight")             stepFloat(cfg.lineHeightMultiplier, 0.05f);
    else if (it.id == L"editor.tabSize")                stepInt(cfg.tabSize);
    else if (it.id == L"files.autoSaveDelay")           stepInt(cfg.autoSaveDelay);
    else if (it.id == L"files.largeFileMmapThresholdMB")stepInt(cfg.largeFileMmapThresholdMB);
    else if (it.id == L"workbench.drawerWidth")         stepInt(cfg.drawerWidth);
    else if (it.id == L"terminal.integrated.defaultHeight") stepFloat(cfg.terminalHeight, 1.0f);
    else if (it.id == L"terminal.integrated.rightWidth")    stepFloat(cfg.terminalRightWidth, 1.0f);
    else if (it.id == L"database.sqlPageSize")          stepInt(cfg.sqlPageSize);
    else if (it.id == L"liveServer.port")               stepInt(cfg.liveServerPort);
    else if (it.id == L"workbench.tree.indent")         stepInt(cfg.explorerTreeIndent);
    else if (it.id == L"search.maxResults")             stepInt(cfg.searchMaxResults);
    else if (it.id == L"terminal.integrated.fontSize")  stepInt(cfg.terminalFontSize);
    else if (it.id == L"terminal.integrated.scrollback")stepInt(cfg.terminalScrollbackLines);
    else if (it.id == L"database.queryTimeoutSec")      stepInt(cfg.dbQueryTimeoutSec);
}

SettingsView::SettingsView() {
    InitializeSettings();
}

void SettingsView::InitializeSettings() {
    m_items.clear();

    auto addToggle = [&](const std::wstring& id, const std::wstring& scope, const std::wstring& title,
                         const std::wstring& desc, SettingCategory cat, bool common, bool appearance = false) {
        SettingItem it;
        it.id = id;
        it.scope = scope;
        it.title = title;
        it.description = desc;
        it.category = cat;
        it.isCommonlyUsed = common;
        it.isAppearance = appearance;
        it.control = SettingControl::Toggle;
        m_items.push_back(std::move(it));
    };

    auto addStepper = [&](const std::wstring& id, const std::wstring& scope, const std::wstring& title,
                          const std::wstring& desc, SettingCategory cat, bool common, bool appearance,
                          float minVal, float maxVal, float stepVal, const std::wstring& unit) {
        SettingItem it;
        it.id = id;
        it.scope = scope;
        it.title = title;
        it.description = desc;
        it.category = cat;
        it.isCommonlyUsed = common;
        it.isAppearance = appearance;
        it.control = SettingControl::NumberStepper;
        it.minVal = minVal;
        it.maxVal = maxVal;
        it.stepVal = stepVal;
        it.unit = unit;
        m_items.push_back(std::move(it));
    };

    auto addDropdown = [&](const std::wstring& id, const std::wstring& scope, const std::wstring& title,
                           const std::wstring& desc, SettingCategory cat, bool common, bool appearance,
                           std::vector<std::wstring> options) {
        SettingItem it;
        it.id = id;
        it.scope = scope;
        it.title = title;
        it.description = desc;
        it.category = cat;
        it.isCommonlyUsed = common;
        it.isAppearance = appearance;
        it.control = SettingControl::Dropdown;
        it.options = std::move(options);
        m_items.push_back(std::move(it));
    };

    // === 1. Files & Workspace Search === (Keep files.autoSave first for layout/test consistency)
    addDropdown(L"files.autoSave", L"FILES", L"Auto Save",
                L"Controls auto save of editors that have unsaved changes.",
                SettingCategory::Files, true, false,
                { L"afterDelay", L"onFocusChange", L"onWindowChange", L"off" });
    addStepper(L"files.autoSaveDelay", L"FILES", L"Auto Save Delay",
               L"Controls the delay in milliseconds after which an editor is saved automatically.",
               SettingCategory::Files, true, false, 200.0f, 5000.0f, 200.0f, L"ms");
    addDropdown(L"files.eol", L"FILES", L"Default End of Line",
                L"Default line ending sequence for new or saved files (auto, LF, or CRLF).",
                SettingCategory::Files, false, false,
                { L"auto", L"LF", L"CRLF" });
    addDropdown(L"files.encoding", L"FILES", L"Default File Encoding",
                L"Default character encoding used when reading and saving workspace files.",
                SettingCategory::Files, false, false,
                { L"UTF-8", L"UTF-8 with BOM", L"UTF-16 LE", L"ISO-8859-1" });
    addToggle(L"files.trimTrailingWhitespace", L"FILES", L"Trim Trailing Whitespace",
              L"Remove trailing whitespace characters automatically when saving a file.",
              SettingCategory::Files, true, false);
    addToggle(L"files.insertFinalNewline", L"FILES", L"Insert Final Newline",
              L"Ensure saved files terminate with a trailing newline character.",
              SettingCategory::Files, false, false);
    addToggle(L"files.enableEditorConfig", L"FILES", L"Respect .editorconfig",
              L"Apply per-directory .editorconfig indentation, charset, and EOL rules automatically.",
              SettingCategory::Files, true, false);
    addToggle(L"files.autoIgnorePhide", L"FILES", L"Auto-Ignore .phide Directory",
              L"Ensure .phide/ is present in .gitignore and workspace ignore files when created.",
              SettingCategory::Files, false, false);
    addStepper(L"files.largeFileMmapThresholdMB", L"FILES", L"Memory-Mapped Large File Threshold",
               L"Open files larger than this threshold using the read-only memory-mapped engine.",
               SettingCategory::Files, false, false, 2.0f, 64.0f, 2.0f, L" MB");
    addToggle(L"search.useIgnoreFiles", L"SEARCH", L"Use Ignore Files in Search",
              L"Exclude paths matching .gitignore and .ignore rules during Quick Open and Workspace Search.",
              SettingCategory::Files, false, false);
    addDropdown(L"files.hotExit", L"FILES", L"Hot Exit Recovery",
                L"Persist unsaved dirty editor buffers across IDE restarts so work is never lost.",
                SettingCategory::Files, true, false,
                { L"onExit", L"onExitAndWindowClose", L"off" });
    addToggle(L"files.excludeDotfiles", L"EXPLORER", L"Hide Dotfiles in Explorer",
              L"Hide dot-prefixed configuration files (e.g. .env, .gitignore) in the Explorer tree.",
              SettingCategory::Files, false, false);
    addToggle(L"search.caseSensitive", L"SEARCH", L"Search Case Sensitive by Default",
              L"Enable case-sensitive matching by default when opening Workspace Search.",
              SettingCategory::Files, false, false);
    addToggle(L"search.wholeWord", L"SEARCH", L"Search Whole Word by Default",
              L"Enable whole-word matching by default in Workspace Search and Find.",
              SettingCategory::Files, false, false);
    addStepper(L"search.maxResults", L"SEARCH", L"Maximum Search Results",
               L"Maximum number of matches collected per workspace search query.",
               SettingCategory::Files, false, false, 500.0f, 10000.0f, 500.0f, L" hits");

    // === 2. Text Editor ===
    addStepper(L"editor.fontSize", L"EDITOR", L"Font Size",
               L"Controls the font size in pixels for code editors.",
               SettingCategory::TextEditor, true, true, 8.0f, 36.0f, 1.0f, L"px");
    addDropdown(L"editor.fontFamily", L"EDITOR", L"Font Family",
                L"Controls the typeface used in the editor buffer.",
                SettingCategory::TextEditor, true, true,
                { L"Koda Mono", L"Koda Mono Semibold Italic", L"Consolas", L"Cascadia Code", L"Courier New", L"Lucida Console" });
    addStepper(L"editor.lineHeight", L"EDITOR", L"Line Height Multiplier",
               L"Controls line vertical spacing relative to editor font size.",
               SettingCategory::TextEditor, true, true, 1.10f, 2.40f, 0.05f, L"x");
    addStepper(L"editor.tabSize", L"EDITOR", L"Tab Size",
               L"The number of spaces a tab is equal to.",
               SettingCategory::TextEditor, true, false, 1.0f, 8.0f, 1.0f, L" spaces");
    addToggle(L"editor.insertSpaces", L"EDITOR", L"Insert Spaces",
              L"Insert spaces when pressing Tab instead of a hard tab character.",
              SettingCategory::TextEditor, true, false);
    addToggle(L"editor.wordWrap", L"EDITOR", L"Word Wrap",
              L"Controls whether lines wrap to fit within the editor viewport width.",
              SettingCategory::TextEditor, true, false);
    addToggle(L"editor.lineNumbers", L"EDITOR", L"Line Numbers",
              L"Controls the display of line numbers in the editor gutter.",
              SettingCategory::TextEditor, true, true);
    addToggle(L"editor.minimap", L"EDITOR", L"Minimap",
              L"Controls whether the code overview minimap is shown on the right.",
              SettingCategory::TextEditor, true, true);
    addToggle(L"editor.stickyScroll.enabled", L"EDITOR", L"Sticky Scroll",
              L"Pin enclosing class, namespace, and function headers at the top while scrolling.",
              SettingCategory::TextEditor, true, true);
    addToggle(L"editor.formatOnSave", L"EDITOR", L"Format On Save",
              L"Automatically format document (HTML, CSS, JS, JSON, PHP) on save (Shift+Alt+F).",
              SettingCategory::TextEditor, true, false);
    addDropdown(L"editor.renderWhitespace", L"EDITOR", L"Render Whitespace",
                L"Controls how the editor should render whitespace characters.",
                SettingCategory::TextEditor, true, true,
                { L"selection", L"boundary", L"trailing", L"all", L"none" });
    addToggle(L"editor.renderControlCharacters", L"EDITOR", L"Render Control Characters",
              L"Display non-printable ASCII control characters explicitly inside editor lines.",
              SettingCategory::TextEditor, false, true);
    addDropdown(L"editor.cursorStyle", L"EDITOR", L"Cursor Style",
                L"Controls the cursor style in insert input mode.",
                SettingCategory::TextEditor, true, true,
                { L"line", L"block", L"underline" });
    addDropdown(L"editor.autoClosingBrackets", L"EDITOR", L"Auto Closing Brackets",
                L"Automatically insert matching (), [], and {} brackets as you type.",
                SettingCategory::TextEditor, false, false,
                { L"always", L"languageDefined", L"never" });
    addDropdown(L"editor.autoClosingQuotes", L"EDITOR", L"Auto Closing Quotes",
                L"Automatically close string quotes and backticks inside editable code blocks.",
                SettingCategory::TextEditor, false, false,
                { L"always", L"languageDefined", L"never" });
    addToggle(L"editor.linkedEditing", L"EDITOR", L"Linked Tag Editing",
              L"Synchronize edits between opening and closing HTML/JSX tag names while keeping attributes isolated.",
              SettingCategory::TextEditor, true, false);
    addToggle(L"editor.bracketPairColorization.enabled", L"EDITOR", L"Bracket Pair Colorization",
              L"Highlight nested bracket pairs with cycling accent colors.",
              SettingCategory::TextEditor, true, true);
    addToggle(L"editor.folding", L"EDITOR", L"Code Folding",
              L"Render interactive fold chevrons in the gutter to collapse functions and blocks.",
              SettingCategory::TextEditor, true, false);
    addDropdown(L"editor.multiCursorModifier", L"EDITOR", L"Multi-Cursor Click Modifier",
                L"Modifier key used with mouse click to place additional arbitrary carets.",
                SettingCategory::TextEditor, false, false,
                { L"alt", L"ctrlCmd" });
    addToggle(L"editor.codeLens", L"EDITOR", L"CodeLens",
              L"Controls whether the editor shows CodeLens Run, Debug, and Reference actions above symbols.",
              SettingCategory::TextEditor, true, true);
    addToggle(L"explorer.updateImportsOnRename", L"EXPLORER", L"Update Imports On Rename / Move",
              L"Prompt to update relative imports across all referencing files when renaming or moving a file.",
              SettingCategory::TextEditor, true, false);
    addToggle(L"editor.smoothScrolling", L"EDITOR", L"Smooth Scrolling",
              L"Enable smooth interpolated vertical scrolling when navigating with the mouse wheel.",
              SettingCategory::TextEditor, false, false);
    addToggle(L"editor.scrollBeyondLastLine", L"EDITOR", L"Scroll Beyond Last Line",
              L"Allow scrolling past the final line of a document for comfortable bottom-of-file editing.",
              SettingCategory::TextEditor, false, false);
    addDropdown(L"editor.cursorBlinking", L"EDITOR", L"Cursor Blinking",
                L"Control the animation style of the editor text caret.",
                SettingCategory::TextEditor, false, true,
                { L"blink", L"smooth", L"phase", L"solid" });
    addToggle(L"editor.guides.indentation", L"EDITOR", L"Indentation Guides",
              L"Render subtle vertical guide lines at each indentation level.",
              SettingCategory::TextEditor, false, true);
    addToggle(L"editor.highlightActiveIndentGuide", L"EDITOR", L"Highlight Active Indent Guide",
              L"Emphasize the indentation guide enclosing the active cursor scope.",
              SettingCategory::TextEditor, false, true);
    addToggle(L"editor.matchBrackets", L"EDITOR", L"Highlight Matching Brackets",
              L"Highlight the matching opening or closing bracket when the cursor touches a delimiter.",
              SettingCategory::TextEditor, false, true);
    addDropdown(L"editor.rulers", L"EDITOR", L"Column Rulers",
                L"Render vertical column wrap guides after the specified number of monospace characters.",
                SettingCategory::TextEditor, true, true,
                { L"off", L"80", L"100", L"120", L"80, 120" });
    addToggle(L"editor.formatOnPaste", L"EDITOR", L"Format On Paste",
              L"Automatically format pasted multi-line code blocks using the active language formatter.",
              SettingCategory::TextEditor, false, false);
    addToggle(L"editor.trimAutoWhitespace", L"EDITOR", L"Trim Auto Whitespace",
              L"Remove trailing auto-inserted indentation when leaving an otherwise empty line.",
              SettingCategory::TextEditor, false, false);

    // === 3. Appearance & Layout ===
    addDropdown(L"workbench.colorTheme", L"APPEARANCE", L"Color Theme",
                L"Specifies the visual color theme used across the editor, sidebar, and workbench UI.",
                SettingCategory::Appearance, true, true,
                {
                    L"Dark Mode", L"Light Mode", L"Auto (System)", L"Deep Purple",
                    L"Cyan Cyber", L"Turquoise Teal", L"Monokai Pro", L"Dracula",
                    L"Nord Arctic", L"Solarized Dark", L"GitHub Dark", L"One Dark Pro",
                    L"SynthWave '84", L"Retro Grid '80s", L"Cosmic Starfield", L"Matrix Rain",
                    L"Aurora Borealis", L"Sunset Glow", L"Emerald Forest", L"Rosé Pine"
                });
    addToggle(L"workbench.breadcrumbs", L"APPEARANCE", L"Breadcrumbs",
              L"Turn file path navigation breadcrumbs on or off above the editor.",
              SettingCategory::Appearance, true, true);
    addToggle(L"workbench.statusBar", L"APPEARANCE", L"Status Bar",
              L"Controls the visibility of the bottom status bar.",
              SettingCategory::Appearance, true, true);
    addToggle(L"workbench.menuBar", L"APPEARANCE", L"Menu Bar",
              L"Controls visibility of the classic top menu bar items.",
              SettingCategory::Appearance, false, true);
    addDropdown(L"workbench.sideBar.location", L"APPEARANCE", L"Primary Sidebar Location",
                L"Dock the Explorer, Search, and Git sidebar on the left or right of the workbench.",
                SettingCategory::Appearance, false, true,
                { L"left", L"right" });
    addStepper(L"workbench.drawerWidth", L"APPEARANCE", L"Sidebar Drawer Width",
               L"Default logical width of the Explorer/Git sidebar panel.",
               SettingCategory::Appearance, false, true, 240.0f, 520.0f, 10.0f, L"px");
    addToggle(L"workbench.centeredLayout", L"APPEARANCE", L"Centered Editor Layout",
              L"Center the editor column horizontally for comfortable widescreen reading.",
              SettingCategory::Appearance, false, true);
    addToggle(L"workbench.zenMode", L"APPEARANCE", L"Zen Mode",
              L"Distraction-free fullscreen presentation mode hiding sidebars, activity rails, and status bars.",
              SettingCategory::Appearance, false, true);
    addToggle(L"workbench.markdownPreview", L"APPEARANCE", L"Markdown Preview",
              L"Automatically open a side-by-side Markdown preview when viewing a Markdown file.",
              SettingCategory::Appearance, true, true);
    addToggle(L"workbench.graphColored", L"APPEARANCE", L"Graph Node Colors",
              L"Colors nodes in the File Network graph view by file extension type.",
              SettingCategory::Appearance, true, true);
    addToggle(L"editor.gitLens", L"APPEARANCE", L"GitLens Inline Blame",
              L"Controls whether inline git authorship and commit blame annotations are shown on the active line.",
              SettingCategory::Appearance, true, true);
    addToggle(L"security.privacyMask", L"SECURITY", L"Privacy Mask (.env & Secrets)",
              L"Mask .env values, API tokens, and credential literals on screen (persisted across restarts).",
              SettingCategory::Appearance, true, true);
    addToggle(L"workbench.activityBar.visible", L"APPEARANCE", L"Activity Bar Visibility",
              L"Show or hide the left vertical Activity Rail icons.",
              SettingCategory::Appearance, false, true);
    addToggle(L"workbench.editor.showTabs", L"APPEARANCE", L"Show Editor Tabs",
              L"Display open document tabs across the top of each editor group.",
              SettingCategory::Appearance, false, true);
    addToggle(L"workbench.commandCenter", L"APPEARANCE", L"Title Bar Command Center",
              L"Show the quick search & command pill in the integrated title bar.",
              SettingCategory::Appearance, false, true);
    addStepper(L"workbench.tree.indent", L"APPEARANCE", L"Explorer Tree Indent",
               L"Controls tree indentation in pixels for nested folders in the Explorer.",
               SettingCategory::Appearance, false, true, 8.0f, 28.0f, 2.0f, L"px");

    // === 4. Language & LSP Intelligence ===
    addToggle(L"editor.inlayHints", L"LSP", L"Inlay Hints",
              L"Controls whether inferred parameter names and type annotations are displayed inline.",
              SettingCategory::LanguageLsp, true, false);
    addToggle(L"editor.lspHover", L"LSP", L"Rich Hover Documentation",
              L"Shows formatted type signatures and Markdown documentation tooltips when hovering symbols.",
              SettingCategory::LanguageLsp, true, false);
    addToggle(L"editor.signatureHelp", L"LSP", L"Signature Parameter Help",
              L"Automatically displays active parameter hints when typing function call arguments.",
              SettingCategory::LanguageLsp, true, false);
    addToggle(L"linter.externalEnabled", L"LINTER", L"External CLI Linters (TSC / ESLint / PHPStan / Ruff)",
              L"Aggregates diagnostics from locally installed CLI linters into the Problems panel and editor squiggles.",
              SettingCategory::LanguageLsp, true, false);
    addToggle(L"emmet.enabled", L"INTELLISENSE", L"Emmet Abbreviations",
              L"Expand Emmet HTML/CSS abbreviations (e.g. div.card>ul>li*3) with Tab.",
              SettingCategory::LanguageLsp, false, false);
    addToggle(L"intellisense.tailwindClasses", L"INTELLISENSE", L"Tailwind CSS Suggestions",
              L"Offer Tailwind utility class completions inside class and className attributes.",
              SettingCategory::LanguageLsp, false, false);
    addToggle(L"intellisense.databaseSchema", L"INTELLISENSE", L"Live Database Schema Completion",
              L"Autocomplete SQL table and column names discovered from connected MySQL/PostgreSQL instances.",
              SettingCategory::LanguageLsp, false, false);
    addDropdown(L"lsp.typescript.executable", L"LSP", L"TypeScript / JS Server",
                L"Executable strategy for TypeScript and TSX/JSX language intelligence.",
                SettingCategory::LanguageLsp, false, false,
                { L"auto-detect", L"typescript-language-server", L"npx typescript-language-server" });
    addDropdown(L"lsp.python.executable", L"LSP", L"Python Language Server",
                L"Executable strategy for Python language intelligence.",
                SettingCategory::LanguageLsp, false, false,
                { L"auto-detect", L"pyright-langserver", L"pylsp" });
    addDropdown(L"lsp.php.executable", L"LSP", L"PHP Language Server",
                L"Executable strategy for PHP language intelligence.",
                SettingCategory::LanguageLsp, false, false,
                { L"auto-detect", L"intelephense", L"phpactor" });
    addDropdown(L"lsp.cpp.executable", L"LSP", L"C / C++ Language Server",
                L"Executable strategy for C/C++ language intelligence.",
                SettingCategory::LanguageLsp, false, false,
                { L"auto-detect", L"clangd" });
    addToggle(L"editor.quickSuggestions", L"INTELLISENSE", L"Quick Suggestions While Typing",
              L"Automatically open IntelliSense completion popups as you type identifiers.",
              SettingCategory::LanguageLsp, true, false);
    addToggle(L"editor.suggest.snippets", L"INTELLISENSE", L"Show Snippet Suggestions",
              L"Include language snippets alongside symbol completions in IntelliSense.",
              SettingCategory::LanguageLsp, false, false);
    addToggle(L"editor.parameterHints.cycle", L"LSP", L"Cycle Overload Signatures",
              L"Allow cycling through overloaded function signatures inside Parameter Help.",
              SettingCategory::LanguageLsp, false, false);
    addToggle(L"editor.diagnostics.onType", L"LINTER", L"Live Diagnostics On Type",
              L"Continuously run syntax and semantic validation while editing buffers.",
              SettingCategory::LanguageLsp, false, false);

    // === 5. Terminal & Task Runner ===
    addDropdown(L"terminal.integrated.dockPosition", L"TERMINAL", L"Terminal Dock Location",
                L"Choose whether the integrated ConPTY terminal docks at Bottom, Editor Area, or Right Side.",
                SettingCategory::TerminalTasks, true, true,
                { L"Bottom Panel", L"Editor Area", L"Right Side" });
    addStepper(L"terminal.integrated.defaultHeight", L"TERMINAL", L"Terminal Panel Height",
               L"Default height in logical pixels when the terminal is docked at the bottom.",
               SettingCategory::TerminalTasks, false, false, 180.0f, 600.0f, 20.0f, L"px");
    addStepper(L"terminal.integrated.rightWidth", L"TERMINAL", L"Terminal Right-Dock Width",
               L"Default width in logical pixels when the terminal is docked to the right.",
               SettingCategory::TerminalTasks, false, false, 320.0f, 760.0f, 20.0f, L"px");
    addDropdown(L"code-runner.executorMap.python", L"RUNNER", L"Python Executor",
                L"Command used to execute Python files (e.g. 'python -u' for unbuffered real-time output).",
                SettingCategory::TerminalTasks, true, false,
                { L"python -u", L"python", L"py -u", L"python3 -u" });
    addToggle(L"code-runner.runInTerminal", L"RUNNER", L"Run Code In Terminal",
              L"Execute scripts and framework tasks inside the interactive ConPTY terminal.",
              SettingCategory::TerminalTasks, true, false);
    addToggle(L"code-runner.saveFileBeforeRun", L"RUNNER", L"Save File Before Run",
              L"Automatically save the active document prior to running or debugging.",
              SettingCategory::TerminalTasks, true, false);
    addToggle(L"code-runner.clearPreviousOutput", L"RUNNER", L"Clear Previous Terminal Output",
              L"Clear prior terminal output before launching a new run task.",
              SettingCategory::TerminalTasks, false, false);
    addDropdown(L"terminal.integrated.shell.windows", L"TERMINAL", L"Default Terminal Shell",
                L"Default shell profile launched when opening a new integrated ConPTY tab.",
                SettingCategory::TerminalTasks, true, false,
                { L"PowerShell", L"Command Prompt", L"Git Bash", L"WSL" });
    addStepper(L"terminal.integrated.fontSize", L"TERMINAL", L"Terminal Font Size",
               L"Controls the font size in pixels inside the integrated terminal.",
               SettingCategory::TerminalTasks, false, false, 10.0f, 24.0f, 1.0f, L"px");
    addStepper(L"terminal.integrated.scrollback", L"TERMINAL", L"Terminal Scrollback Buffer",
               L"Maximum number of historical lines retained in each terminal session.",
               SettingCategory::TerminalTasks, false, false, 1000.0f, 20000.0f, 1000.0f, L" lines");
    addDropdown(L"terminal.integrated.cursorStyle", L"TERMINAL", L"Terminal Cursor Style",
                L"Caret shape rendered inside active ConPTY terminal sessions.",
                SettingCategory::TerminalTasks, false, false,
                { L"block", L"line", L"underline" });
    addToggle(L"terminal.integrated.copyOnSelection", L"TERMINAL", L"Copy On Selection",
              L"Automatically copy highlighted terminal text to the system clipboard.",
              SettingCategory::TerminalTasks, false, false);
    addToggle(L"code-runner.preserveFocus", L"RUNNER", L"Preserve Editor Focus On Run",
              L"Keep keyboard focus in the active code editor when launching a run command.",
              SettingCategory::TerminalTasks, false, false);

    // === 6. Extensions & Linters === (Python Linter settings stay under Extensions for [TEST 116])
    addToggle(L"python.linting.enabled", L"PYTHON LINTER", L"Enable Python Linting",
              L"Whether to enable real-time Python syntax and variable linting in the editor.",
              SettingCategory::Extensions, true, false);
    addToggle(L"python.linting.undefinedVariableCheck", L"PYTHON LINTER", L"Check Undefined Variables",
              L"Flag missing variables, undefined names, and typo suggestions (NameError / Flake8 F821) immediately.",
              SettingCategory::Extensions, true, false);
    addToggle(L"python.terminal.highlightErrorsInEditor", L"PYTHON LINTER", L"Highlight Terminal Execution Errors",
              L"Automatically underline execution errors and tracebacks from the terminal directly on the editor line.",
              SettingCategory::Extensions, true, false);
    addDropdown(L"python.linting.linter", L"PYTHON LINTER", L"Python Linter Engine",
                L"Linter engine used for real-time diagnostics (built-in AST/Flake8, Flake8 CLI, or Ruff).",
                SettingCategory::Extensions, true, false,
                { L"builtin (ast/flake8)", L"flake8", L"ruff" });
    addToggle(L"extensions.autoCheckUpdates", L"EXTENSIONS", L"Auto-Check Extension Updates",
              L"Periodically check installed plugins and language packs for newer releases.",
              SettingCategory::Extensions, false, false);
    addToggle(L"extensions.autoUpdate", L"EXTENSIONS", L"Auto-Update Extensions",
              L"Automatically install compatible updates for enabled extensions in the background.",
              SettingCategory::Extensions, false, false);
    addToggle(L"php.validate.enable", L"PHP LINTER", L"Enable PHP Validation",
              L"Run built-in PHP syntax, undefined class/function, and variable checks on open buffers.",
              SettingCategory::Extensions, false, false);
    addToggle(L"coverage.showGutter", L"TESTING & COVERAGE", L"Show Code Coverage In Gutter",
              L"Highlight covered (green) and uncovered (red) lines in the editor gutter from lcov.info or coverage.xml.",
              SettingCategory::TerminalTasks, false, false);
    addToggle(L"markdown.syncScroll", L"MARKDOWN", L"Synchronized Scroll In Markdown Preview",
              L"Synchronize vertical scrolling proportionally between the Markdown editor and live preview pane.",
              SettingCategory::TextEditor, false, false);
    addToggle(L"javascript.validate.enable", L"JS / TS LINTER", L"Enable JS/TS Validation",
              L"Run semantic and syntax diagnostics on JavaScript, TypeScript, JSX, and TSX files.",
              SettingCategory::Extensions, false, false);
    addToggle(L"css.validate.enable", L"CSS LINTER", L"Enable CSS & Tailwind Validation",
              L"Validate CSS properties, custom properties, and Tailwind directives.",
              SettingCategory::Extensions, false, false);
    addToggle(L"markdown.lint.enable", L"MARKDOWN", L"Enable Markdown Linting",
              L"Check Markdown files for malformed headings and broken local links.",
              SettingCategory::Extensions, false, false);

    // === 7. Git & Source Control ===
    addToggle(L"git.diffSideBySide", L"GIT", L"Side-by-Side Diff View",
              L"Compare HEAD and working-tree file revisions in a two-column diff view.",
              SettingCategory::GitSourceControl, false, false);
    addToggle(L"git.stashExcludePhide", L"GIT", L"Safe Git Stash (.phide Isolation)",
              L"Exclude .phide/ workspace state on stash Push/Apply/Pop and preserve restored untracked files.",
              SettingCategory::GitSourceControl, true, false);
    addToggle(L"git.autoRefreshGraph", L"GIT", L"Auto-Refresh Git & File Graph",
              L"Automatically refresh commit history, branches, and File Network graph on workspace edits.",
              SettingCategory::GitSourceControl, false, false);
    addToggle(L"git.enabled", L"GIT", L"Enable Git Integration",
              L"Enable Git repository detection, Source Control staging, and commit history graph.",
              SettingCategory::GitSourceControl, true, false);
    addToggle(L"git.autofetch", L"GIT", L"Auto-Fetch Remote Branches",
              L"Periodically fetch updates from your configured Git remote without modifying local branches.",
              SettingCategory::GitSourceControl, false, false);
    addToggle(L"git.confirmSync", L"GIT", L"Confirm Before Push / Pull Sync",
              L"Prompt before pushing or pulling commits to/from remote repositories.",
              SettingCategory::GitSourceControl, false, false);
    addToggle(L"git.enableSmartCommit", L"GIT", L"Smart Commit All Changes",
              L"Automatically stage all modified files when committing with an empty staged list.",
              SettingCategory::GitSourceControl, false, false);
    addToggle(L"git.showInlineBlame", L"GIT", L"Inline Blame Author & Age",
              L"Show latest commit author, relative timestamp, and message inline on the active line.",
              SettingCategory::GitSourceControl, false, false);
    addToggle(L"git.decorations.enabled", L"GIT", L"Explorer Git Status Badges",
              L"Colorize modified, untracked, and staged files with status badges in the Explorer tree.",
              SettingCategory::GitSourceControl, false, false);

    // === 8. Debug, Database & Live Server ===
    addToggle(L"debug.inlineValues", L"DEBUG", L"Inline Debugger Variable Values",
              L"Display local variable values inline next to source lines while paused at a breakpoint.",
              SettingCategory::DebugDatabase, false, false);
    addToggle(L"debug.openDebugOnBreak", L"DEBUG", L"Focus Paused Line On Breakpoint",
              L"Open and scroll the active editor automatically to the paused frame on breakpoint hit or step.",
              SettingCategory::DebugDatabase, false, false);
    addToggle(L"debug.allowBreakpointsEverywhere", L"DEBUG", L"Allow Breakpoints In Any File",
              L"Allow toggling gutter breakpoints in all source file types regardless of active adapter.",
              SettingCategory::DebugDatabase, false, false);
    addToggle(L"debug.showSubSessionsInToolBar", L"DEBUG", L"Floating Debug Control Toolbar",
              L"Display floating Continue, Step Over, Step Into, and Stop controls during debug sessions.",
              SettingCategory::DebugDatabase, false, false);
    addStepper(L"database.sqlPageSize", L"DATABASE", L"SQL Console Result Page Size",
               L"Number of rows shown per page in the integrated MySQL / PostgreSQL / SQLite Query Console.",
               SettingCategory::DebugDatabase, true, false, 25.0f, 500.0f, 25.0f, L" rows");
    addToggle(L"database.autoDiscoverServers", L"DATABASE", L"Auto-Discover Local DB Servers",
              L"Automatically probe local MySQL, PostgreSQL, and SQLite services when opening SQL Console.",
              SettingCategory::DebugDatabase, false, false);
    addStepper(L"database.queryTimeoutSec", L"DATABASE", L"SQL Query Timeout",
               L"Maximum execution time in seconds before a long-running SQL query is cancelled.",
               SettingCategory::DebugDatabase, false, false, 5.0f, 120.0f, 5.0f, L"s");
    addStepper(L"liveServer.port", L"LIVE SERVER", L"Live Server Default Port",
               L"Preferred HTTP listen port when launching the integrated static/PHP Live Server.",
               SettingCategory::DebugDatabase, false, false, 3000.0f, 9000.0f, 50.0f, L"");
    addToggle(L"liveServer.hotReload", L"LIVE SERVER", L"Live Server Hot Reload (WS / SSE)",
              L"Push DOM/CSS hot-reload signals to connected browsers when HTML or stylesheet buffers change.",
              SettingCategory::DebugDatabase, false, false);
    addToggle(L"liveServer.autoOpenBrowser", L"LIVE SERVER", L"Auto-Open Browser On Start",
              L"Automatically launch the default web browser when starting the integrated Live Server.",
              SettingCategory::DebugDatabase, false, false);
    addToggle(L"liveServer.https", L"LIVE SERVER", L"Serve Local Live Server Over HTTPS (TLS)",
              L"Use a cached local self-signed TLS certificate (%LOCALAPPDATA%\\phide\\certs) with Schannel TLS.",
              SettingCategory::DebugDatabase, false, false);
    addDropdown(L"liveServer.root", L"LIVE SERVER", L"Live Server Document Root",
                L"Default relative folder served as the HTTP root directory.",
                SettingCategory::DebugDatabase, false, false,
                { L"/", L"/public", L"/dist", L"/build" });
}

std::vector<size_t> SettingsView::GetFilteredItemIndices() const {
    std::vector<size_t> result;

    std::wstring qLower = m_searchQuery;
    for (auto& c : qLower) c = towlower(c);
    while (!qLower.empty() && qLower.front() == L' ') qLower.erase(0, 1);
    while (!qLower.empty() && qLower.back() == L' ') qLower.pop_back();

    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto& it = m_items[i];

        // If search query is non-empty, search across title, description, id, and scope
        if (!qLower.empty()) {
            std::wstring tLower = it.title;
            for (auto& c : tLower) c = towlower(c);
            std::wstring dLower = it.description;
            for (auto& c : dLower) c = towlower(c);
            std::wstring idLower = it.id;
            for (auto& c : idLower) c = towlower(c);
            std::wstring sLower = it.scope;
            for (auto& c : sLower) c = towlower(c);

            if (tLower.find(qLower) != std::wstring::npos ||
                dLower.find(qLower) != std::wstring::npos ||
                idLower.find(qLower) != std::wstring::npos ||
                sLower.find(qLower) != std::wstring::npos) {
                result.push_back(i);
            }
        } else {
            // Category filter
            if (m_activeCategory == SettingCategory::CommonlyUsed) {
                if (it.isCommonlyUsed) result.push_back(i);
            } else if (it.category == m_activeCategory || (m_activeCategory == SettingCategory::Appearance && it.isAppearance)) {
                result.push_back(i);
            }
        }
    }
    return result;
}

void SettingsView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          float left, float width, float top, float height) {
    if (!pRT) return;

    m_viewBounds = D2D1::RectF(left, top, left + width, top + height);

    // Initialize Lucide icons if needed
    ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        m_icons.Initialize(pFactory.Get());
    }

    bool allKodaItalic = theme.IsAllKodaSemiBoldItalic();
    static bool s_lastAllKodaItalic = false;
    static float s_lastDpiScale = 0.0f;
    if (s_lastAllKodaItalic != allKodaItalic || s_lastDpiScale != config.dpiScale) {
        m_pFontDesc.Reset();
        m_pFontTitle.Reset();
        m_pFontTabCenter.Reset();
        s_lastAllKodaItalic = allKodaItalic;
        s_lastDpiScale = config.dpiScale;
    }

    // Ensure crisp leading-aligned and centered typography formats
    if (!m_pFontDesc && theme.dwriteFactory) {
        const wchar_t* family = allKodaItalic ? L"Koda Mono" : L"Segoe UI";
        IDWriteFontCollection* pCollection = allKodaItalic ? theme.customFontCollection.Get() : nullptr;
        DWRITE_FONT_WEIGHT weight = allKodaItalic ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR;
        DWRITE_FONT_STYLE style = allKodaItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
        theme.dwriteFactory->CreateTextFormat(
            family, pCollection, weight, style,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.0f), L"en-US", &m_pFontDesc
        );
        if (m_pFontDesc) {
            m_pFontDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_pFontDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_pFontDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    if (!m_pFontTitle && theme.dwriteFactory) {
        const wchar_t* family = allKodaItalic ? L"Koda Mono" : L"Segoe UI";
        IDWriteFontCollection* pCollection = allKodaItalic ? theme.customFontCollection.Get() : nullptr;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        DWRITE_FONT_STYLE style = allKodaItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
        theme.dwriteFactory->CreateTextFormat(
            family, pCollection, weight, style,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(14.5f), L"en-US", &m_pFontTitle
        );
        if (m_pFontTitle) {
            m_pFontTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_pFontTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_pFontTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    if (!m_pFontTabCenter && theme.dwriteFactory) {
        const wchar_t* family = allKodaItalic ? L"Koda Mono" : L"Segoe UI";
        IDWriteFontCollection* pCollection = allKodaItalic ? theme.customFontCollection.Get() : nullptr;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        DWRITE_FONT_STYLE style = allKodaItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
        theme.dwriteFactory->CreateTextFormat(
            family, pCollection, weight, style,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.0f), L"en-US", &m_pFontTabCenter
        );
        if (m_pFontTabCenter) {
            m_pFontTabCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_pFontTabCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_pFontTabCenter->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    // 1. Fill View Background with refined glass IDE backdrop
    ID2D1Brush* pBgBrush = theme.brushCanvasBg ? theme.brushCanvasBg.Get() : theme.brushRailBg.Get();
    pRT->FillRectangle(m_viewBounds, pBgBrush);

    const float headerH = config.ScaleF(68.0f);
    const float sidebarW = config.ScaleF(224.0f);

    // 2. Render Header Section (Title + Subtitle + Search Bar)
    pRT->DrawLine(
        D2D1::Point2F(left, top + headerH),
        D2D1::Point2F(left + width, top + headerH),
        theme.brushBorderSubtle.Get(),
        0.8f
    );

    // Settings Gear Icon Badge in Header
    const float iconBoxSize = config.ScaleF(36.0f);
    const float iconBoxX = left + config.ScaleF(24.0f);
    const float iconBoxY = top + (headerH - iconBoxSize) / 2.0f;
    D2D1_RECT_F rcIconBox = D2D1::RectF(iconBoxX, iconBoxY, iconBoxX + iconBoxSize, iconBoxY + iconBoxSize);
    D2D1_ROUNDED_RECT rrcIconBox = D2D1::RoundedRect(rcIconBox, 8.0f, 8.0f);

    ID2D1Brush* pBadgeBg = theme.brushHomeBadgeBg ? theme.brushHomeBadgeBg.Get() : theme.brushItemHover.Get();
    pRT->FillRoundedRectangle(rrcIconBox, pBadgeBg);
    pRT->DrawRoundedRectangle(rrcIconBox, theme.brushBorderSubtle.Get(), 1.0f);

    D2D1_RECT_F rcGear = D2D1::RectF(iconBoxX + 4.0f, iconBoxY + 4.0f, iconBoxX + iconBoxSize - 4.0f, iconBoxY + iconBoxSize - 4.0f);
    ID2D1Brush* pAccentBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get();
    m_icons.DrawSettings(pRT, rcGear, pAccentBrush, 1.4f);

    // Search Bar on Right of Header (compute searchX first so title/subtitle never collide with it)
    const float searchW = config.ScaleF((std::min)(420.0f, width * 0.42f));
    const float searchH = config.ScaleF(34.0f);
    const float searchX = left + width - searchW - config.ScaleF(28.0f);
    const float searchY = top + (headerH - searchH) / 2.0f;
    m_searchBoxBounds = D2D1::RectF(searchX, searchY, searchX + searchW, searchY + searchH);

    // Header Title and Subtitle (leading-aligned to sit cleanly beside icon)
    float textX = iconBoxX + iconBoxSize + config.ScaleF(14.0f);
    float headerTextRight = (std::max)(textX + config.ScaleF(120.0f), searchX - config.ScaleF(18.0f));
    D2D1_RECT_F rcTitle = D2D1::RectF(textX, top + config.ScaleF(12.0f), headerTextRight, top + config.ScaleF(36.0f));
    const wchar_t* titleStr = L"Settings";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  m_pFontTitle.Get() ? m_pFontTitle.Get() : theme.fontBody.Get(),
                  rcTitle, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    D2D1_RECT_F rcSub = D2D1::RectF(textX, top + config.ScaleF(38.0f), headerTextRight, top + config.ScaleF(58.0f));
    const wchar_t* subStr = L"Manage preferences, editor behavior, language tools, and appearance";
    pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)),
                  m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get(),
                  rcSub, theme.brushTextSecondary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(m_searchBoxBounds, 6.0f, 6.0f);

    // Search background
    ID2D1Brush* pSearchBg = theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushItemHover.Get();
    pRT->FillRoundedRectangle(rrcSearch, pSearchBg);
    ID2D1Brush* pSearchBorder = m_isSearchFocused
        ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get())
        : theme.brushBorderSubtle.Get();
    pRT->DrawRoundedRectangle(rrcSearch, pSearchBorder, m_isSearchFocused ? 1.4f : 1.0f);

    // Search Icon inside Search Bar
    D2D1_RECT_F rcSearchIcon = D2D1::RectF(searchX + config.ScaleF(8.0f), searchY + config.ScaleF(7.0f), searchX + config.ScaleF(24.0f), searchY + searchH - config.ScaleF(7.0f));
    m_icons.DrawSearch(pRT, rcSearchIcon, theme.brushTextSecondary.Get(), 1.4f);

    // Search Text or Placeholder (leading-aligned)
    D2D1_RECT_F rcSearchText = D2D1::RectF(searchX + config.ScaleF(30.0f), searchY + config.ScaleF(4.0f), searchX + searchW - config.ScaleF(30.0f), searchY + searchH - config.ScaleF(4.0f));
    if (m_searchQuery.empty()) {
        const wchar_t* phStr = L"Search settings (e.g. font, wrap, git, sql, mask)...";
        pRT->DrawText(phStr, static_cast<UINT32>(wcslen(phStr)),
                      m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get(),
                      rcSearchText, theme.brushTextSecondary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    } else {
        pRT->DrawText(m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                      m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get(),
                      rcSearchText, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        // Clear 'x' button
        float clearSize = config.ScaleF(16.0f);
        m_searchClearBounds = D2D1::RectF(searchX + searchW - clearSize - config.ScaleF(8.0f), searchY + (searchH - clearSize) / 2.0f,
                                          searchX + searchW - config.ScaleF(8.0f), searchY + (searchH + clearSize) / 2.0f);
        if (m_hoveredSearchClear) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_searchClearBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        float cx = (m_searchClearBounds.left + m_searchClearBounds.right) / 2.0f;
        float cy = (m_searchClearBounds.top + m_searchClearBounds.bottom) / 2.0f;
        float cr = config.ScaleF(3.5f);
        pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr), D2D1::Point2F(cx + cr, cy + cr), theme.brushTextSecondary.Get(), 1.4f);
        pRT->DrawLine(D2D1::Point2F(cx - cr, cy + cr), D2D1::Point2F(cx + cr, cy - cr), theme.brushTextSecondary.Get(), 1.4f);
    }

    // Dynamic Blinking Caret
    if (m_isSearchFocused) {
        bool showCaret = ((GetTickCount64() / 500) % 2 == 0);
        if (showCaret) {
            float caretX = rcSearchText.left;
            if (!m_searchQuery.empty() && theme.dwriteFactory) {
                IDWriteTextFormat* pQueryFmt = m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get();
                caretX += MeasureTextWidthDWrite(theme.dwriteFactory.Get(), pQueryFmt, m_searchQuery, config.ScaleF(7.5f)) + 1.0f;
            } else if (!m_searchQuery.empty()) {
                caretX += static_cast<float>(m_searchQuery.length()) * config.ScaleF(7.5f);
            }
            if (caretX > rcSearchText.right - config.ScaleF(4.0f)) {
                caretX = rcSearchText.right - config.ScaleF(4.0f);
            }
            float caretY1 = rcSearchText.top + config.ScaleF(5.0f);
            float caretY2 = rcSearchText.bottom - config.ScaleF(5.0f);
            ID2D1Brush* pCaretBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : pAccentBrush;
            pRT->DrawLine(D2D1::Point2F(caretX, caretY1), D2D1::Point2F(caretX, caretY2), pCaretBrush, 1.5f);
        }
    }

    // 3. Render Left Category Sidebar (9 Categories covering all major IDE subsystems)
    float bodyTop = top + headerH;
    float bodyH = height - headerH;

    // Vertical Divider
    pRT->DrawLine(
        D2D1::Point2F(left + sidebarW, bodyTop),
        D2D1::Point2F(left + sidebarW, top + height),
        theme.brushBorderSubtle.Get(),
        0.8f
    );

    struct CatDef {
        SettingCategory cat;
        const wchar_t* name;
        const wchar_t* glyph;
    };
    static const CatDef kCategories[] = {
        { SettingCategory::CommonlyUsed,     L"Commonly Used",      L"✦" },
        { SettingCategory::TextEditor,       L"Text Editor",        L"✎" },
        { SettingCategory::Appearance,       L"Appearance",         L"🎨" },
        { SettingCategory::Files,            L"Files & Search",     L"📁" },
        { SettingCategory::Extensions,       L"Extensions",         L"🧩" },
        { SettingCategory::LanguageLsp,      L"Language & LSP",     L"⚡" },
        { SettingCategory::TerminalTasks,    L"Terminal & Runner",  L">_" },
        { SettingCategory::GitSourceControl, L"Git & SCM",          L"⎇" },
        { SettingCategory::DebugDatabase,    L"Debug, DB & Server", L"🗄" }
    };
    const size_t kCatCount = sizeof(kCategories) / sizeof(kCategories[0]);

    m_categoryBounds.clear();
    const float catItemH = config.ScaleF(34.0f);
    const float catPadX = config.ScaleF(12.0f);
    float curCatY = bodyTop + config.ScaleF(14.0f);

    for (size_t i = 0; i < kCatCount; ++i) {
        D2D1_RECT_F rcCat = D2D1::RectF(left + catPadX, curCatY, left + sidebarW - catPadX, curCatY + catItemH);
        m_categoryBounds.push_back(rcCat);

        bool isActive = (m_searchQuery.empty() && m_activeCategory == kCategories[i].cat);
        bool isHovered = (m_hoveredCategoryIdx == static_cast<int>(i));

        D2D1_ROUNDED_RECT rrcCat = D2D1::RoundedRect(rcCat, 6.0f, 6.0f);
        if (isActive) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> activeBg;
            if (theme.IsDarkMode()) {
                activeBg = theme.brushHomeBadgeBg ? theme.brushHomeBadgeBg : theme.brushItemHover;
            } else {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.90f, 0.92f, 0.97f, 1.0f), &activeBg);
            }
            if (activeBg) pRT->FillRoundedRectangle(rrcCat, activeBg.Get());
            D2D1_RECT_F rcBar = D2D1::RectF(rcCat.left, rcCat.top + config.ScaleF(7.0f), rcCat.left + config.ScaleF(3.5f), rcCat.bottom - config.ScaleF(7.0f));
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcBar, 2.0f, 2.0f), pAccentBrush);
        } else if (isHovered) {
            pRT->FillRoundedRectangle(rrcCat, theme.brushItemHover.Get());
        }

        // Category Icon / Glyph + Text
        D2D1_RECT_F rcCatText = D2D1::RectF(rcCat.left + config.ScaleF(12.0f), rcCat.top, rcCat.right - config.ScaleF(6.0f), rcCat.bottom);
        std::wstring fullLabel = std::wstring(kCategories[i].glyph) + L"  " + kCategories[i].name;
        ID2D1Brush* pTextCol = (isActive || isHovered)
            ? (theme.IsDarkMode() ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextPrimary.Get())
            : theme.brushTextSecondary.Get();
        pRT->DrawText(fullLabel.c_str(), static_cast<UINT32>(fullLabel.length()),
                      m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get(),
                      rcCatText, pTextCol, D2D1_DRAW_TEXT_OPTIONS_CLIP);

        curCatY += catItemH + config.ScaleF(4.0f);
    }

    // 4. Render Scope Switcher Bar (All Settings | User | Workspace) + Right Content Area
    float contentLeft = left + sidebarW + config.ScaleF(28.0f);
    float contentW = (std::max)(config.ScaleF(560.0f), width - sidebarW - config.ScaleF(56.0f));
    float scopeBarH = config.ScaleF(30.0f);

    // Draw scope switcher strip with DWrite-measured tab widths so labels never clip
    {
        static const wchar_t* kScopeNames[3] = { L"All Settings", L"User", L"Workspace" };
        m_scopeBounds.clear();
        float sx = contentLeft;
        float sy = bodyTop + config.ScaleF(2.0f);
        float sh = config.ScaleF(26.0f);
        IDWriteTextFormat* pScopeFmt = m_pFontTabCenter.Get() ? m_pFontTabCenter.Get() : (m_pFontTitle.Get() ? m_pFontTitle.Get() : theme.fontBody.Get());
        for (int s = 0; s < 3; ++s) {
            const std::wstring scopeLabel = kScopeNames[s];
            const float measuredW = MeasureTextWidthDWrite(theme.dwriteFactory.Get(), pScopeFmt, scopeLabel, config.ScaleF(9.0f));
            const float minTabW = config.ScaleF(s == 0 ? 136.0f : (s == 1 ? 86.0f : 120.0f));
            const float sw = (std::max)(minTabW, measuredW + config.ScaleF(28.0f));
            D2D1_RECT_F rcS = D2D1::RectF(sx, sy, sx + sw, sy + sh);
            m_scopeBounds.push_back(rcS);
            bool activeS = (m_activeScope == s);
            bool hovS = (m_hoveredScopeIdx == s);
            if (activeS) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> activeBg;
                if (theme.IsDarkMode()) {
                    activeBg = theme.brushHomeBadgeBg ? theme.brushHomeBadgeBg : theme.brushItemHover;
                } else {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.88f, 0.91f, 0.97f, 1.0f), &activeBg);
                }
                if (activeBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcS, 5.0f, 5.0f), activeBg.Get());
                pRT->DrawLine(D2D1::Point2F(rcS.left + 8.0f, rcS.bottom - 1.5f),
                              D2D1::Point2F(rcS.right - 8.0f, rcS.bottom - 1.5f),
                              pAccentBrush, 2.0f);
            } else if (hovS) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcS, 5.0f, 5.0f), theme.brushItemHover.Get());
            }
            pRT->PushAxisAlignedClip(rcS, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            ID2D1Brush* pSCol = (activeS || hovS)
                ? (theme.IsDarkMode() ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextPrimary.Get())
                : theme.brushTextSecondary.Get();
            if (pScopeFmt) {
                pRT->DrawText(scopeLabel.c_str(), static_cast<UINT32>(scopeLabel.length()),
                              pScopeFmt,
                              D2D1::RectF(rcS.left + config.ScaleF(4.0f), rcS.top, rcS.right - config.ScaleF(4.0f), rcS.bottom),
                              pSCol, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            pRT->PopAxisAlignedClip();
            sx += sw + config.ScaleF(8.0f);
        }
        if (m_activeScope == 2) {
            const wchar_t* wsNote = L"Saves to .phide/settings.json (auto-ignored in .gitignore)";
            D2D1_RECT_F rcNote = D2D1::RectF(sx + config.ScaleF(10.0f), sy, left + width - config.ScaleF(16.0f), sy + sh);
            if (rcNote.right > rcNote.left + config.ScaleF(40.0f)) {
                pRT->DrawText(wsNote, static_cast<UINT32>(wcslen(wsNote)),
                              m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get(),
                              rcNote, theme.brushTextSecondary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        pRT->DrawLine(D2D1::Point2F(left + sidebarW, bodyTop + scopeBarH),
                      D2D1::Point2F(left + width, bodyTop + scopeBarH),
                      theme.brushBorderSubtle.Get(), 0.8f);
    }

    float contentTop = bodyTop + scopeBarH + config.ScaleF(6.0f);
    float contentH = (bodyH - scopeBarH) - config.ScaleF(14.0f);

    D2D1_RECT_F rcContentClip = D2D1::RectF(contentLeft, bodyTop + scopeBarH, left + width - config.ScaleF(8.0f), top + height);
    pRT->PushAxisAlignedClip(rcContentClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Filter items
    auto filteredIndices = GetFilteredItemIndices();

    // Section Header Title
    std::wstring sectionTitle;
    if (!m_searchQuery.empty()) {
        sectionTitle = L"Search Results (" + std::to_wstring(filteredIndices.size()) + L" settings found)";
    } else {
        for (const auto& c : kCategories) {
            if (c.cat == m_activeCategory) {
                sectionTitle = std::wstring(c.glyph) + L" " + c.name + L" (" + std::to_wstring(filteredIndices.size()) + L" settings)";
                break;
            }
        }
    }

    float cardY = contentTop - m_scrollOffsetY;
    D2D1_RECT_F rcSecHeader = D2D1::RectF(contentLeft, cardY, contentLeft + contentW, cardY + config.ScaleF(24.0f));
    pRT->DrawText(sectionTitle.c_str(), static_cast<UINT32>(sectionTitle.length()),
                  theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get(),
                  rcSecHeader, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    cardY += config.ScaleF(26.0f);

    // If no matching settings
    if (filteredIndices.empty()) {
        D2D1_RECT_F rcEmpty = D2D1::RectF(contentLeft, cardY + config.ScaleF(30.0f), contentLeft + contentW, cardY + config.ScaleF(80.0f));
        const wchar_t* emptyMsg = L"No settings found matching your search query.";
        pRT->DrawText(emptyMsg, static_cast<UINT32>(wcslen(emptyMsg)),
                      m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get(),
                      rcEmpty, theme.brushTextSecondary.Get());
    }

    // Card height and spacing (preserves exact top-card dropdown coordinates)
    const float cardH = config.ScaleF(82.0f);
    const float cardSpacing = config.ScaleF(12.0f);

    for (size_t idx : filteredIndices) {
        auto& it = m_items[idx];

        it.cardBounds = D2D1::RectF(contentLeft, cardY, contentLeft + contentW, cardY + cardH);

        bool isCardHovered = (m_hoveredCardIdx == static_cast<int>(idx));

        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(it.cardBounds, 8.0f, 8.0f);

        // Card Background: tactile modern card
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
        if (theme.IsDarkMode()) {
            cardBg = theme.brushDrawerBg ? theme.brushDrawerBg : theme.brushItemHover;
        } else {
            pRT->CreateSolidColorBrush(isCardHovered ? D2D1::ColorF(0.97f, 0.98f, 1.0f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &cardBg);
        }
        if (cardBg) pRT->FillRoundedRectangle(rrcCard, cardBg.Get());
        ID2D1Brush* pCardBorder = isCardHovered
            ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushBorderHover.Get())
            : theme.brushBorderSubtle.Get();
        pRT->DrawRoundedRectangle(rrcCard, pCardBorder, isCardHovered ? 1.2f : 1.0f);

        // Right Control Placement
        const float ctrlRight = contentLeft + contentW - config.ScaleF(24.0f);
        const float ctrlCenterY = cardY + cardH / 2.0f;

        // Scope Tag Badge with measured text width (never clipped)
        IDWriteTextFormat* pBadgeFmt = theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontHint.Get();
        float measuredBadgeW = MeasureTextWidthDWrite(theme.dwriteFactory.Get(), pBadgeFmt, it.scope, config.ScaleF(7.8f));
        float scopeBadgeW = (std::max)(config.ScaleF(static_cast<float>(it.scope.length()) * 7.8f + 16.0f),
                                       measuredBadgeW + config.ScaleF(18.0f));
        float scopeBadgeH = config.ScaleF(18.0f);
        D2D1_RECT_F rcScopeBadge = D2D1::RectF(contentLeft + config.ScaleF(20.0f), cardY + config.ScaleF(14.0f),
                                              contentLeft + config.ScaleF(20.0f) + scopeBadgeW, cardY + config.ScaleF(14.0f) + scopeBadgeH);
        D2D1_ROUNDED_RECT rrcScope = D2D1::RoundedRect(rcScopeBadge, 4.0f, 4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> scopeBg;
        if (theme.IsDarkMode()) {
            scopeBg = theme.brushHomeBadgeBg ? theme.brushHomeBadgeBg : theme.brushItemHover;
        } else {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.94f, 0.97f, 1.0f), &scopeBg);
        }
        if (scopeBg) pRT->FillRoundedRectangle(rrcScope, scopeBg.Get());
        pRT->DrawText(it.scope.c_str(), static_cast<UINT32>(it.scope.length()),
                      pBadgeFmt, rcScopeBadge, pAccentBrush);

        // Compute control width upfront so title/description never collide with the control
        std::wstring dropValStr;
        float dropW = config.ScaleF(170.0f);
        float ctrlReserveW = config.ScaleF(170.0f);
        if (it.control == SettingControl::Dropdown) {
            dropValStr = ReadDropdownSetting(it.id, config, theme);
            IDWriteTextFormat* pDropFmt = m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get();
            float maxOptW = MeasureTextWidthDWrite(theme.dwriteFactory.Get(), pDropFmt, dropValStr, config.ScaleF(7.8f));
            dropW = (std::max)(dropW, (std::min)(config.ScaleF(300.0f), maxOptW + config.ScaleF(46.0f)));
            if (it.id == L"python.linting.linter" || it.id == L"workbench.colorTheme" || it.id == L"editor.fontFamily") {
                dropW = (std::max)(dropW, config.ScaleF(260.0f));
            }
            ctrlReserveW = dropW + config.ScaleF(20.0f);
        }

        // Setting Title
        float titleX = rcScopeBadge.right + config.ScaleF(12.0f);
        float textRightLimit = (std::max)(titleX + config.ScaleF(80.0f), ctrlRight - ctrlReserveW);
        D2D1_RECT_F rcCardTitle = D2D1::RectF(titleX, cardY + config.ScaleF(12.0f), textRightLimit, cardY + config.ScaleF(34.0f));
        pRT->DrawText(it.title.c_str(), static_cast<UINT32>(it.title.length()),
                      m_pFontTitle.Get() ? m_pFontTitle.Get() : theme.fontBody.Get(),
                      rcCardTitle, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

        // Setting Description
        D2D1_RECT_F rcDesc = D2D1::RectF(contentLeft + config.ScaleF(20.0f), cardY + config.ScaleF(42.0f),
                                         ctrlRight - ctrlReserveW, cardY + cardH - config.ScaleF(10.0f));
        pRT->DrawText(it.description.c_str(), static_cast<UINT32>(it.description.length()),
                      m_pFontDesc.Get() ? m_pFontDesc.Get() : theme.fontBody.Get(),
                      rcDesc, theme.brushTextSecondary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

        if (it.control == SettingControl::Toggle) {
            const float toggleW = config.ScaleF(46.0f);
            const float toggleH = config.ScaleF(26.0f);
            it.controlBounds = D2D1::RectF(ctrlRight - toggleW, ctrlCenterY - toggleH / 2.0f, ctrlRight, ctrlCenterY + toggleH / 2.0f);
            bool isChecked = ReadBoolSetting(it.id, config);
            DrawToggleSwitch(pRT, theme, config, it.controlBounds, isChecked, isCardHovered);
        } else if (it.control == SettingControl::NumberStepper) {
            const float stepperW = config.ScaleF(136.0f);
            const float stepperH = config.ScaleF(30.0f);
            it.controlBounds = D2D1::RectF(ctrlRight - stepperW, ctrlCenterY - stepperH / 2.0f, ctrlRight, ctrlCenterY + stepperH / 2.0f);

            std::wstring valStr = FormatStepperSettingValue(it, config);
            bool hMinus = (isCardHovered && m_hoveredControlSubId == "minus");
            bool hPlus  = (isCardHovered && m_hoveredControlSubId == "plus");
            DrawStepper(pRT, theme, config, it, valStr, hMinus, hPlus);
        } else if (it.control == SettingControl::Dropdown) {
            const float dropH = config.ScaleF(32.0f);
            it.controlBounds = D2D1::RectF(ctrlRight - dropW, ctrlCenterY - dropH / 2.0f, ctrlRight, ctrlCenterY + dropH / 2.0f);
            bool isOpen = (m_openDropdownItemIdx == static_cast<int>(idx));
            DrawDropdown(pRT, theme, config, it.controlBounds, dropValStr, isCardHovered, isOpen);
        }

        cardY += cardH + cardSpacing;
    }

    pRT->PopAxisAlignedClip();

    // 5. Update Max Scroll Offset and draw scrollbar if needed
    float totalContentH = (cardY + m_scrollOffsetY) - contentTop;
    m_maxScrollOffsetY = (std::max)(0.0f, totalContentH - contentH);

    if (m_maxScrollOffsetY > 0.0f) {
        float scrollTrackW = config.ScaleF(6.0f);
        float scrollTrackX = left + width - scrollTrackW - config.ScaleF(4.0f);
        m_rcScrollTrack = D2D1::RectF(scrollTrackX, bodyTop + scopeBarH + config.ScaleF(8.0f), scrollTrackX + scrollTrackW, top + height - config.ScaleF(8.0f));
        float trackH = m_rcScrollTrack.bottom - m_rcScrollTrack.top;
        float thumbH = (std::max)(config.ScaleF(30.0f), trackH * (contentH / totalContentH));
        float thumbY = m_rcScrollTrack.top + (m_scrollOffsetY / m_maxScrollOffsetY) * (trackH - thumbH);
        m_rcScrollThumb = D2D1::RectF(scrollTrackX, thumbY, scrollTrackX + scrollTrackW, thumbY + thumbH);

        // Show scrollbar when hovering settings, dragging, or hovering scrollbar
        if (m_isMouseOver || m_isDraggingScrollbar || m_isScrollbarHovered) {
            float opacity = (m_isDraggingScrollbar) ? 0.85f : (m_isScrollbarHovered ? 0.70f : 0.38f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushThumb;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, opacity) : D2D1::ColorF(0.0f, 0.0f, 0.0f, opacity),
                &brushThumb
            );
            if (brushThumb) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcScrollThumb, 3.0f, 3.0f), brushThumb.Get());
            }
        }
    } else {
        m_rcScrollTrack = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        m_rcScrollThumb = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // 6. Draw Open Floating Dropdown Menu (Rendered on top of cards outside clipping)
    if (m_openDropdownItemIdx >= 0 && m_openDropdownItemIdx < static_cast<int>(m_items.size())) {
        DrawDropdownPopup(pRT, theme, config);
    }
}

void SettingsView::DrawToggleSwitch(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    const D2D1_RECT_F& bounds, bool isChecked, bool isHovered) {
    const float w = bounds.right - bounds.left;
    const float h = bounds.bottom - bounds.top;
    const float r = h / 2.0f;

    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(bounds, r, r);

    // Pill background: Green accent when checked, clean light gray (or dark charcoal) when unchecked
    if (isChecked) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushActive;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.72f, 0.45f, 1.0f), &brushActive); // Modern active green
        pRT->FillRoundedRectangle(rrcPill, brushActive ? brushActive.Get() : theme.brushBrandAccent.Get());
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushInactive;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.24f, 0.28f, 1.0f) : D2D1::ColorF(0.85f, 0.86f, 0.89f, 1.0f),
            &brushInactive
        );
        pRT->FillRoundedRectangle(rrcPill, brushInactive ? brushInactive.Get() : theme.brushItemHover.Get());
    }

    ID2D1Brush* pBorder = isHovered
        ? (theme.IsDarkMode() ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushBorderHover.Get()) : (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushBorderHover.Get()))
        : theme.brushBorderSubtle.Get();
    pRT->DrawRoundedRectangle(rrcPill, pBorder, 1.0f);

    // Sliding Circular Knob
    const float knobPad = config.ScaleF(2.5f);
    const float knobRadius = r - knobPad;
    float knobCenterX = isChecked ? (bounds.right - r) : (bounds.left + r);
    float knobCenterY = (bounds.top + bounds.bottom) / 2.0f;

    pRT->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(knobCenterX, knobCenterY), knobRadius, knobRadius),
        theme.brushTextWhite.Get()
    );
    if (!theme.IsDarkMode() && !isChecked) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> knobBorder;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.72f, 0.75f, 0.80f, 1.0f), &knobBorder);
        if (knobBorder) pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(knobCenterX, knobCenterY), knobRadius, knobRadius), knobBorder.Get(), 0.9f);
    }
}

void SettingsView::DrawStepper(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               const SettingItem& item, const std::wstring& valStr, bool isHoveredMinus, bool isHoveredPlus) {
    const float btnW = config.ScaleF(30.0f);
    const float h = item.controlBounds.bottom - item.controlBounds.top;

    auto& itMutable = const_cast<SettingItem&>(item);
    itMutable.minusBounds = D2D1::RectF(item.controlBounds.left, item.controlBounds.top, item.controlBounds.left + btnW, item.controlBounds.bottom);
    itMutable.plusBounds  = D2D1::RectF(item.controlBounds.right - btnW, item.controlBounds.top, item.controlBounds.right, item.controlBounds.bottom);

    // Entire Stepper container frame
    D2D1_ROUNDED_RECT rrcFrame = D2D1::RoundedRect(item.controlBounds, 6.0f, 6.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> frameBg;
    if (theme.IsDarkMode()) {
        frameBg = theme.brushDrawerBg ? theme.brushDrawerBg : theme.brushItemHover;
    } else {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.97f, 0.99f, 1.0f), &frameBg);
    }
    if (frameBg) pRT->FillRoundedRectangle(rrcFrame, frameBg.Get());
    pRT->DrawRoundedRectangle(rrcFrame, theme.brushBorderSubtle.Get(), 1.0f);

    // Minus Button
    D2D1_ROUNDED_RECT rrcMinus = D2D1::RoundedRect(itMutable.minusBounds, 5.0f, 5.0f);
    if (isHoveredMinus) pRT->FillRoundedRectangle(rrcMinus, theme.brushItemHover.Get());
    float mcx = (itMutable.minusBounds.left + itMutable.minusBounds.right) / 2.0f;
    float mcy = (itMutable.minusBounds.top + itMutable.minusBounds.bottom) / 2.0f;
    pRT->DrawLine(D2D1::Point2F(mcx - 4.5f, mcy), D2D1::Point2F(mcx + 4.5f, mcy), theme.brushTextPrimary.Get(), 1.5f);

    // Value in center (Centered text alignment so number does not crowd buttons)
    D2D1_RECT_F rcVal = D2D1::RectF(itMutable.minusBounds.right, item.controlBounds.top, itMutable.plusBounds.left, item.controlBounds.bottom);
    IDWriteTextFormat* pStepperFmt = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : (theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get());
    pRT->DrawText(valStr.c_str(), static_cast<UINT32>(valStr.length()),
                  pStepperFmt, rcVal, theme.brushTextPrimary.Get());

    // Plus Button
    D2D1_ROUNDED_RECT rrcPlus = D2D1::RoundedRect(itMutable.plusBounds, 5.0f, 5.0f);
    if (isHoveredPlus) pRT->FillRoundedRectangle(rrcPlus, theme.brushItemHover.Get());
    float pcx = (itMutable.plusBounds.left + itMutable.plusBounds.right) / 2.0f;
    float pcy = (itMutable.plusBounds.top + itMutable.plusBounds.bottom) / 2.0f;
    pRT->DrawLine(D2D1::Point2F(pcx - 4.5f, pcy), D2D1::Point2F(pcx + 4.5f, pcy), theme.brushTextPrimary.Get(), 1.5f);
    pRT->DrawLine(D2D1::Point2F(pcx, pcy - 4.5f), D2D1::Point2F(pcx, pcy + 4.5f), theme.brushTextPrimary.Get(), 1.5f);
}

void SettingsView::DrawDropdown(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                const D2D1_RECT_F& bounds, const std::wstring& valStr, bool isHovered, bool isOpen) {
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(bounds, 6.0f, 6.0f);
    ID2D1Brush* pBg = isOpen
        ? (theme.brushItemHover ? theme.brushItemHover.Get() : theme.brushDrawerBg.Get())
        : (theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushItemHover.Get());
    pRT->FillRoundedRectangle(rrc, pBg);

    ID2D1Brush* pBorder = (isOpen || isHovered)
        ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushBorderHover.Get())
        : theme.brushBorderSubtle.Get();
    pRT->DrawRoundedRectangle(rrc, pBorder, (isOpen || isHovered) ? 1.4f : 1.0f);

    // Selected value text with clipping so it never leaks
    D2D1_RECT_F rcText = D2D1::RectF(bounds.left + config.ScaleF(12.0f), bounds.top, bounds.right - config.ScaleF(28.0f), bounds.bottom);
    pRT->PushAxisAlignedClip(rcText, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    pRT->DrawText(valStr.c_str(), static_cast<UINT32>(valStr.length()),
                  theme.fontBody.Get(), rcText, theme.brushTextPrimary.Get());
    pRT->PopAxisAlignedClip();

    // Chevron glyph
    float cx = bounds.right - config.ScaleF(16.0f);
    float cy = (bounds.top + bounds.bottom) / 2.0f;
    if (isOpen) {
        // Chevron pointing up ▲
        D2D1_POINT_2F p1 = D2D1::Point2F(cx - 3.5f, cy + 2.0f);
        D2D1_POINT_2F p2 = D2D1::Point2F(cx, cy - 2.5f);
        D2D1_POINT_2F p3 = D2D1::Point2F(cx + 3.5f, cy + 2.0f);
        pRT->DrawLine(p1, p2, theme.brushTextPrimary.Get(), 1.5f);
        pRT->DrawLine(p2, p3, theme.brushTextPrimary.Get(), 1.5f);
    } else {
        // Chevron pointing down ▼
        D2D1_POINT_2F p1 = D2D1::Point2F(cx - 3.5f, cy - 2.5f);
        D2D1_POINT_2F p2 = D2D1::Point2F(cx, cy + 2.0f);
        D2D1_POINT_2F p3 = D2D1::Point2F(cx + 3.5f, cy - 2.5f);
        pRT->DrawLine(p1, p2, theme.brushTextSecondary.Get(), 1.5f);
        pRT->DrawLine(p2, p3, theme.brushTextSecondary.Get(), 1.5f);
    }
}

void SettingsView::DrawDropdownPopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    if (m_openDropdownItemIdx < 0 || m_openDropdownItemIdx >= static_cast<int>(m_items.size())) return;
    const auto& item = m_items[m_openDropdownItemIdx];
    if (item.options.empty()) return;

    const std::wstring curVal = ReadDropdownSetting(item.id, config, theme);

    const float rowH = config.ScaleF(32.0f);
    const float padY = config.ScaleF(6.0f);
    const float menuH = item.options.size() * rowH + padY * 2.0f;
    float measuredMaxW = config.ScaleF(180.0f);
    for (const auto& opt : item.options) {
        measuredMaxW = (std::max)(measuredMaxW, MeasureTextWidthDWrite(theme.dwriteFactory.Get(), theme.fontBody.Get(), opt, config.ScaleF(8.0f)) + config.ScaleF(44.0f));
    }
    const float minW = (item.id == L"python.linting.linter" || item.id == L"workbench.colorTheme" || item.id == L"editor.fontFamily")
                       ? config.ScaleF(260.0f) : config.ScaleF(180.0f);
    const float menuW = (std::max)(item.controlBounds.right - item.controlBounds.left, (std::max)(minW, measuredMaxW));

    // Place menu directly below control button
    float menuLeft = item.controlBounds.right - menuW;
    float menuTop  = item.controlBounds.bottom + config.ScaleF(4.0f);
    // If it would exceed bottom of view, flip above and clamp cleanly inside view
    if (menuTop + menuH > m_viewBounds.bottom - config.ScaleF(10.0f)) {
        menuTop = item.controlBounds.top - menuH - config.ScaleF(4.0f);
    }
    if (menuTop < m_viewBounds.top + config.ScaleF(8.0f)) {
        menuTop = m_viewBounds.top + config.ScaleF(8.0f);
    }
    m_openDropdownBounds = D2D1::RectF(menuLeft, menuTop, menuLeft + menuW, menuTop + menuH);

    // Drop shadow
    D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(
        D2D1::RectF(menuLeft + 2.0f, menuTop + 3.0f, menuLeft + menuW + 2.0f, menuTop + menuH + 3.0f),
        8.0f, 8.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &shadowBrush);
    if (shadowBrush) pRT->FillRoundedRectangle(rrcShadow, shadowBrush.Get());

    // Menu background
    D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_openDropdownBounds, 8.0f, 8.0f);
    ID2D1Brush* pMenuBg = theme.brushMenuBg ? theme.brushMenuBg.Get() :
                          (theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushRailBg.Get());
    pRT->FillRoundedRectangle(rrcMenu, pMenuBg);
    pRT->DrawRoundedRectangle(rrcMenu, theme.brushMenuBorder ? theme.brushMenuBorder.Get() : theme.brushBorderSubtle.Get(), 1.0f);

    // Options rows
    float curRowY = menuTop + padY;
    for (size_t i = 0; i < item.options.size(); ++i) {
        D2D1_RECT_F rcRow = D2D1::RectF(menuLeft + config.ScaleF(6.0f), curRowY, menuLeft + menuW - config.ScaleF(6.0f), curRowY + rowH);
        bool isHovered = (m_hoveredDropdownOption == static_cast<int>(i));
        bool isSelected = (item.options[i] == curVal || _wcsicmp(item.options[i].c_str(), curVal.c_str()) == 0);

        if (isHovered) {
            D2D1_ROUNDED_RECT rrcRow = D2D1::RoundedRect(rcRow, 5.0f, 5.0f);
            pRT->FillRoundedRectangle(rrcRow, theme.brushItemHover.Get());
        }

        // Checkmark for active selection
        if (isSelected) {
            float checkX = rcRow.left + config.ScaleF(12.0f);
            float checkY = (rcRow.top + rcRow.bottom) / 2.0f;
            D2D1_POINT_2F p1 = D2D1::Point2F(checkX - 3.5f, checkY);
            D2D1_POINT_2F p2 = D2D1::Point2F(checkX - 0.5f, checkY + 3.5f);
            D2D1_POINT_2F p3 = D2D1::Point2F(checkX + 5.0f, checkY - 3.5f);
            ID2D1Brush* pAccent = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
            pRT->DrawLine(p1, p2, pAccent, 1.8f);
            pRT->DrawLine(p2, p3, pAccent, 1.8f);
        }

        // Option text with clipping
        D2D1_RECT_F rcOptText = D2D1::RectF(rcRow.left + config.ScaleF(26.0f), rcRow.top, rcRow.right - config.ScaleF(8.0f), rcRow.bottom);
        pRT->PushAxisAlignedClip(rcOptText, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ID2D1Brush* pTextBrush = isSelected
            ? (theme.IsDarkMode() ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushBrandAccent.Get()) : theme.brushBrandAccent.Get())
            : (isHovered ? (theme.IsDarkMode() ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextPrimary.Get()) : theme.brushTextPrimary.Get());
        pRT->DrawText(item.options[i].c_str(), static_cast<UINT32>(item.options[i].length()),
                      theme.fontBody.Get(), rcOptText, pTextBrush);
        pRT->PopAxisAlignedClip();

        curRowY += rowH;
    }
}

bool SettingsView::HandleMouseMove(POINT clientPt) {
    int oldCard = m_hoveredCardIdx;
    int oldCat = m_hoveredCategoryIdx;
    bool oldClear = m_hoveredSearchClear;
    std::string oldCtrlSub = m_hoveredControlSubId;
    int oldDropOpt = m_hoveredDropdownOption;

    m_hoveredCardIdx = -1;
    m_hoveredCategoryIdx = -1;
    m_hoveredSearchClear = false;
    m_hoveredControlSubId.clear();
    m_hoveredDropdownOption = -1;

    // Track whether mouse is over settings view
    m_isMouseOver = (clientPt.x >= m_viewBounds.left && clientPt.x <= m_viewBounds.right &&
                     clientPt.y >= m_viewBounds.top && clientPt.y <= m_viewBounds.bottom);

    // Check scrollbar drag first
    if (m_isDraggingScrollbar) {
        float trackH = m_rcScrollTrack.bottom - m_rcScrollTrack.top;
        float thumbH = m_rcScrollThumb.bottom - m_rcScrollThumb.top;
        float scrollRange = trackH - thumbH;
        if (scrollRange > 0.0f && m_maxScrollOffsetY > 0.0f) {
            float deltaY = static_cast<float>(clientPt.y) - m_dragScrollStartY;
            float newOffset = m_dragScrollStartOffset + (deltaY / scrollRange) * m_maxScrollOffsetY;
            m_scrollOffsetY = (std::clamp)(newOffset, 0.0f, m_maxScrollOffsetY);
            return true;
        }
        return false;
    }

    // Check scrollbar hover (with generous +/- 6px horizontal tolerance)
    bool wasScrollHovered = m_isScrollbarHovered;
    m_isScrollbarHovered = (m_maxScrollOffsetY > 0.0f &&
                            clientPt.x >= m_rcScrollTrack.left - 6.0f && clientPt.x <= m_rcScrollTrack.right + 6.0f &&
                            clientPt.y >= m_rcScrollTrack.top && clientPt.y <= m_rcScrollTrack.bottom);

    // Check open dropdown popup options hover first
    if (m_openDropdownItemIdx >= 0 && m_openDropdownItemIdx < static_cast<int>(m_items.size())) {
        if (clientPt.x >= m_openDropdownBounds.left && clientPt.x <= m_openDropdownBounds.right &&
            clientPt.y >= m_openDropdownBounds.top && clientPt.y <= m_openDropdownBounds.bottom) {
            float relY = clientPt.y - (m_openDropdownBounds.top + 6.0f);
            const float totalH = m_openDropdownBounds.bottom - m_openDropdownBounds.top - 12.0f;
            const size_t optCount = m_items[m_openDropdownItemIdx].options.size();
            if (relY >= 0.0f && totalH > 0.0f && optCount > 0) {
                int opt = static_cast<int>(relY / (totalH / static_cast<float>(optCount)));
                if (opt >= 0 && opt < static_cast<int>(optCount)) {
                    m_hoveredDropdownOption = opt;
                }
            }
            return (oldDropOpt != m_hoveredDropdownOption);
        }
    }

    // Check search clear
    if (m_searchClearBounds.right > m_searchClearBounds.left &&
        clientPt.x >= m_searchClearBounds.left && clientPt.x <= m_searchClearBounds.right &&
        clientPt.y >= m_searchClearBounds.top && clientPt.y <= m_searchClearBounds.bottom) {
        m_hoveredSearchClear = true;
    }

    // Check Scope tabs hover
    int oldScopeHover = m_hoveredScopeIdx;
    m_hoveredScopeIdx = -1;
    for (size_t i = 0; i < m_scopeBounds.size(); ++i) {
        const auto& rc = m_scopeBounds[i];
        if (clientPt.x >= rc.left && clientPt.x <= rc.right &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            m_hoveredScopeIdx = static_cast<int>(i);
            break;
        }
    }

    // Check Category sidebar (full row width clickable/hoverable)
    for (size_t i = 0; i < m_categoryBounds.size(); ++i) {
        const auto& rc = m_categoryBounds[i];
        if (clientPt.x >= m_viewBounds.left && clientPt.x <= rc.right + 20.0f &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            m_hoveredCategoryIdx = static_cast<int>(i);
            break;
        }
    }

    // Check visible cards
    auto filtered = GetFilteredItemIndices();
    for (size_t idx : filtered) {
        const auto& it = m_items[idx];
        if (clientPt.x >= it.cardBounds.left && clientPt.x <= it.cardBounds.right &&
            clientPt.y >= it.cardBounds.top && clientPt.y <= it.cardBounds.bottom) {
            m_hoveredCardIdx = static_cast<int>(idx);

            // Check stepper sub controls
            if (it.control == SettingControl::NumberStepper) {
                if (clientPt.x >= it.minusBounds.left && clientPt.x <= it.minusBounds.right &&
                    clientPt.y >= it.minusBounds.top && clientPt.y <= it.minusBounds.bottom) {
                    m_hoveredControlSubId = "minus";
                } else if (clientPt.x >= it.plusBounds.left && clientPt.x <= it.plusBounds.right &&
                           clientPt.y >= it.plusBounds.top && clientPt.y <= it.plusBounds.bottom) {
                    m_hoveredControlSubId = "plus";
                }
            }
            break;
        }
    }

    return (oldCard != m_hoveredCardIdx || oldCat != m_hoveredCategoryIdx ||
            oldClear != m_hoveredSearchClear || oldCtrlSub != m_hoveredControlSubId ||
            oldDropOpt != m_hoveredDropdownOption || oldScopeHover != m_hoveredScopeIdx);
}

bool SettingsView::HandleLButtonDown(POINT clientPt, app::AppConfig& config, Theme& theme, std::wstring& outModifiedSettingId) {
    // 0. Handle Click / Drag on Scrollbar
    if (m_maxScrollOffsetY > 0.0f &&
        clientPt.x >= m_rcScrollTrack.left - 6.0f && clientPt.x <= m_rcScrollTrack.right + 6.0f &&
        clientPt.y >= m_rcScrollTrack.top && clientPt.y <= m_rcScrollTrack.bottom) {
        
        float thumbTop = m_rcScrollThumb.top - 4.0f;
        float thumbBottom = m_rcScrollThumb.bottom + 4.0f;
        if (clientPt.y >= thumbTop && clientPt.y <= thumbBottom) {
            m_isDraggingScrollbar = true;
            m_dragScrollStartY = static_cast<float>(clientPt.y);
            m_dragScrollStartOffset = m_scrollOffsetY;
        } else if (clientPt.y < m_rcScrollThumb.top) {
            m_scrollOffsetY = (std::max)(0.0f, m_scrollOffsetY - 250.0f);
        } else {
            m_scrollOffsetY = (std::min)(m_maxScrollOffsetY, m_scrollOffsetY + 250.0f);
        }
        return true;
    }

    // 0b. Handle Click inside Open Dropdown Popup
    if (m_openDropdownItemIdx >= 0 && m_openDropdownItemIdx < static_cast<int>(m_items.size())) {
        auto& dropItem = m_items[m_openDropdownItemIdx];
        if (clientPt.x >= m_openDropdownBounds.left && clientPt.x <= m_openDropdownBounds.right &&
            clientPt.y >= m_openDropdownBounds.top && clientPt.y <= m_openDropdownBounds.bottom) {
            float relY = clientPt.y - (m_openDropdownBounds.top + 6.0f);
            const float totalH = m_openDropdownBounds.bottom - m_openDropdownBounds.top - 12.0f;
            const size_t optCount = dropItem.options.size();
            if (relY >= 0.0f && totalH > 0.0f && optCount > 0) {
                int optIdx = static_cast<int>(relY / (totalH / static_cast<float>(optCount)));
                if (optIdx >= 0 && optIdx < static_cast<int>(optCount)) {
                    WriteDropdownSetting(dropItem.id, dropItem.options[optIdx], config, theme);
                    outModifiedSettingId = dropItem.id;
                }
            }
            m_openDropdownItemIdx = -1;
            m_hoveredDropdownOption = -1;
            return true;
        } else {
            // Click outside closes dropdown
            m_openDropdownItemIdx = -1;
            m_hoveredDropdownOption = -1;
        }
    }

    // 1. Search Box Click
    if (clientPt.x >= m_searchBoxBounds.left && clientPt.x <= m_searchBoxBounds.right &&
        clientPt.y >= m_searchBoxBounds.top && clientPt.y <= m_searchBoxBounds.bottom) {
        // If clicked clear
        if (m_hoveredSearchClear && !m_searchQuery.empty()) {
            m_searchQuery.clear();
            m_scrollOffsetY = 0.0f;
            return true;
        }
        m_isSearchFocused = true;
        return true;
    }
    m_isSearchFocused = false;

    // 1b. Scope Tabs Click (All Settings | User | Workspace)
    for (size_t i = 0; i < m_scopeBounds.size(); ++i) {
        const auto& rc = m_scopeBounds[i];
        if (clientPt.x >= rc.left && clientPt.x <= rc.right &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            m_activeScope = static_cast<int>(i);
            m_scrollOffsetY = 0.0f;
            m_openDropdownItemIdx = -1;
            m_hoveredDropdownOption = -1;
            return true;
        }
    }

    // 2. Category Sidebar Click
    for (size_t i = 0; i < m_categoryBounds.size(); ++i) {
        const auto& rc = m_categoryBounds[i];
        if (clientPt.x >= m_viewBounds.left && clientPt.x <= rc.right + 20.0f &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            m_activeCategory = static_cast<SettingCategory>(i);
            m_searchQuery.clear();
            m_scrollOffsetY = 0.0f;
            return true;
        }
    }

    // 3. Card Controls Click
    auto filtered = GetFilteredItemIndices();
    for (size_t idx : filtered) {
        auto& it = m_items[idx];
        if (clientPt.x >= it.cardBounds.left && clientPt.x <= it.cardBounds.right &&
            clientPt.y >= it.cardBounds.top && clientPt.y <= it.cardBounds.bottom) {

            // A. Toggle control
            if (it.control == SettingControl::Toggle) {
                if (clientPt.x >= it.controlBounds.left && clientPt.x <= it.controlBounds.right &&
                    clientPt.y >= it.controlBounds.top && clientPt.y <= it.controlBounds.bottom) {
                    if (bool* pBool = ResolveBoolSettingPtr(it.id, config)) {
                        *pBool = !(*pBool);
                        outModifiedSettingId = it.id;
                        return true;
                    }
                }
            }

            // B. Number Stepper control
            if (it.control == SettingControl::NumberStepper) {
                bool isMinus = (clientPt.x >= it.minusBounds.left && clientPt.x <= it.minusBounds.right &&
                                clientPt.y >= it.minusBounds.top && clientPt.y <= it.minusBounds.bottom);
                bool isPlus  = (clientPt.x >= it.plusBounds.left && clientPt.x <= it.plusBounds.right &&
                                clientPt.y >= it.plusBounds.top && clientPt.y <= it.plusBounds.bottom);

                if (isMinus || isPlus) {
                    StepNumericSettingValue(it, isMinus ? -1 : 1, config);
                    outModifiedSettingId = it.id;
                    return true;
                }
            }

            // C. Dropdown Click: Toggle floating popup
            if (it.control == SettingControl::Dropdown) {
                if (clientPt.x >= it.controlBounds.left && clientPt.x <= it.controlBounds.right &&
                    clientPt.y >= it.controlBounds.top && clientPt.y <= it.controlBounds.bottom) {
                    if (m_openDropdownItemIdx == static_cast<int>(idx)) {
                        m_openDropdownItemIdx = -1;
                    } else {
                        m_openDropdownItemIdx = static_cast<int>(idx);
                    }
                    m_hoveredDropdownOption = -1;
                    return true;
                }
            }
        }
    }

    return false;
}

bool SettingsView::HandleMouseWheel(short delta) {
    if (m_maxScrollOffsetY <= 0.0f) return false;

    float oldOffset = m_scrollOffsetY;
    m_scrollOffsetY -= static_cast<float>(delta) * 0.5f;
    if (m_scrollOffsetY < 0.0f) m_scrollOffsetY = 0.0f;
    if (m_scrollOffsetY > m_maxScrollOffsetY) m_scrollOffsetY = m_maxScrollOffsetY;

    return (oldOffset != m_scrollOffsetY);
}

bool SettingsView::HandleChar(wchar_t ch) {
    if (!m_isSearchFocused) return false;

    if (ch >= 32 && ch != 127) {
        m_searchQuery.push_back(ch);
        m_scrollOffsetY = 0.0f;
        return true;
    }
    return false;
}

bool SettingsView::HandleKeyDown(WPARAM key) {
    if (key == VK_ESCAPE) {
        if (m_openDropdownItemIdx >= 0) {
            m_openDropdownItemIdx = -1;
            m_hoveredDropdownOption = -1;
            return true;
        }
        if (m_isSearchFocused) {
            m_searchQuery.clear();
            m_isSearchFocused = false;
            m_scrollOffsetY = 0.0f;
            return true;
        }
    }

    if (!m_isSearchFocused) return false;

    if (key == VK_BACK) {
        if (!m_searchQuery.empty()) {
            m_searchQuery.pop_back();
            m_scrollOffsetY = 0.0f;
            return true;
        }
    }
    return false;
}

bool SettingsView::HandleLButtonUp(POINT clientPt) {
    (void)clientPt;
    if (m_isDraggingScrollbar) {
        m_isDraggingScrollbar = false;
        return true;
    }
    return false;
}

LPCWSTR SettingsView::GetCursorForPoint(POINT clientPt) const {
    // 0. Scrollbar cursor
    if (m_maxScrollOffsetY > 0.0f &&
        clientPt.x >= m_rcScrollTrack.left - 6.0f && clientPt.x <= m_rcScrollTrack.right + 6.0f &&
        clientPt.y >= m_rcScrollTrack.top && clientPt.y <= m_rcScrollTrack.bottom) {
        return IDC_ARROW;
    }

    // 1. If dropdown is open, check dropdown popup menu
    if (m_openDropdownItemIdx >= 0) {
        if (clientPt.x >= m_openDropdownBounds.left && clientPt.x <= m_openDropdownBounds.right &&
            clientPt.y >= m_openDropdownBounds.top && clientPt.y <= m_openDropdownBounds.bottom) {
            return IDC_HAND;
        }
    }

    // 2. Search clear button
    if (m_searchClearBounds.right > m_searchClearBounds.left &&
        clientPt.x >= m_searchClearBounds.left && clientPt.x <= m_searchClearBounds.right &&
        clientPt.y >= m_searchClearBounds.top && clientPt.y <= m_searchClearBounds.bottom) {
        return IDC_HAND;
    }

    // 3. Search input box
    if (clientPt.x >= m_searchBoxBounds.left && clientPt.x <= m_searchBoxBounds.right &&
        clientPt.y >= m_searchBoxBounds.top && clientPt.y <= m_searchBoxBounds.bottom) {
        return IDC_IBEAM;
    }

    // 3b. Scope switcher tabs
    for (const auto& rc : m_scopeBounds) {
        if (clientPt.x >= rc.left && clientPt.x <= rc.right &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            return IDC_HAND;
        }
    }

    // 4. Sidebar categories (full row width)
    for (const auto& rc : m_categoryBounds) {
        if (clientPt.x >= m_viewBounds.left && clientPt.x <= rc.right + 20.0f &&
            clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            return IDC_HAND;
        }
    }

    // 5. Card interactive controls (toggle switch, stepper minus/plus, dropdown pill)
    auto filtered = GetFilteredItemIndices();
    for (size_t idx : filtered) {
        const auto& it = m_items[idx];
        if (clientPt.x >= it.cardBounds.left && clientPt.x <= it.cardBounds.right &&
            clientPt.y >= it.cardBounds.top && clientPt.y <= it.cardBounds.bottom) {
            if (it.control == SettingControl::Toggle) {
                if (clientPt.x >= it.controlBounds.left && clientPt.x <= it.controlBounds.right &&
                    clientPt.y >= it.controlBounds.top && clientPt.y <= it.controlBounds.bottom) {
                    return IDC_HAND;
                }
            } else if (it.control == SettingControl::NumberStepper) {
                if ((clientPt.x >= it.minusBounds.left && clientPt.x <= it.minusBounds.right &&
                     clientPt.y >= it.minusBounds.top && clientPt.y <= it.minusBounds.bottom) ||
                    (clientPt.x >= it.plusBounds.left && clientPt.x <= it.plusBounds.right &&
                     clientPt.y >= it.plusBounds.top && clientPt.y <= it.plusBounds.bottom)) {
                    return IDC_HAND;
                }
            } else if (it.control == SettingControl::Dropdown) {
                if (clientPt.x >= it.controlBounds.left && clientPt.x <= it.controlBounds.right &&
                    clientPt.y >= it.controlBounds.top && clientPt.y <= it.controlBounds.bottom) {
                    return IDC_HAND;
                }
            }
            break;
        }
    }

    return nullptr;
}

} // namespace phide::ui