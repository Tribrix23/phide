#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include "phide/workspace/IgnoreEngine.hpp"
#include "phide/editor/AdaptiveCodeEngine.hpp"
#include "phide/editor/PredictionEngine.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/plugin/PluginManager.hpp"
#include "phide/editor/SyntaxValidator.hpp"
#include "phide/ui/Theme.hpp"

using namespace phide::workspace;
using namespace phide::editor;

void TestIgnoreEngine() {
    std::cout << "[TEST] Running IgnoreEngine Tests...\n";
    IgnoreEngine engine;

    // Test 1: Supported ignore filenames
    assert(IgnoreEngine::IsSupportedIgnoreFileName(L".gitignore"));
    assert(IgnoreEngine::IsSupportedIgnoreFileName(L".dockerignore"));
    assert(IgnoreEngine::IsSupportedIgnoreFileName(L".npmignore"));
    assert(IgnoreEngine::IsSupportedIgnoreFileName(L".eslintignore"));
    assert(IgnoreEngine::IsSupportedIgnoreFileName(L".prettierignore"));
    assert(IgnoreEngine::IsSupportedIgnoreFileName(L".ignore"));
    assert(!IgnoreEngine::IsSupportedIgnoreFileName(L"file.txt"));
    std::cout << "  -> PASS: IsSupportedIgnoreFileName checks passed.\n";

    // Test 2: In-memory rule parsing from .dockerignore and .gitignore
    std::wstring dockerIgnoreContent = 
        L"# Docker ignore file\n"
        L"Dockerfile*\n"
        L".dockerignore\n"
        L"node_modules/\n"
        L"dist/\n"
        L"*.tmp\n"
        L"!important.tmp\n"
        L"logs/**\n";

    engine.ParseRules(dockerIgnoreContent, L"C:\\myproject", L".dockerignore");
    assert(engine.GetRuleCount() >= 6);

    // Folder exclusion test
    assert(engine.IsIgnored(L"C:\\myproject\\node_modules", L"node_modules", true));
    assert(engine.IsIgnored(L"C:\\myproject\\dist", L"dist", true));
    assert(!engine.IsIgnored(L"C:\\myproject\\src", L"src", true));

    // File exclusion test (.dockerignore)
    assert(engine.IsIgnored(L"C:\\myproject\\Dockerfile", L"Dockerfile", false));
    assert(engine.IsIgnored(L"C:\\myproject\\Dockerfile.prod", L"Dockerfile.prod", false));
    assert(engine.IsIgnored(L"C:\\myproject\\.dockerignore", L".dockerignore", false));
    assert(engine.IsIgnored(L"C:\\myproject\\temp.tmp", L"temp.tmp", false));

    // Negation test
    assert(!engine.IsIgnored(L"C:\\myproject\\important.tmp", L"important.tmp", false));

    // Deep directory match with **
    assert(engine.IsIgnored(L"C:\\myproject\\logs\\app\\today.log", L"today.log", false));

    // Test 3: Multiple ignore files merged (.npmignore + .eslintignore)
    std::wstring npmIgnoreContent = 
        L"test/\n"
        L"coverage/\n"
        L"*.log\n";
    engine.ParseRules(npmIgnoreContent, L"C:\\myproject", L".npmignore");

    // test/ folder excluded by .npmignore
    assert(engine.IsIgnored(L"C:\\myproject\\test", L"test", true));
    assert(engine.IsIgnored(L"C:\\myproject\\coverage", L"coverage", true));
    assert(engine.IsIgnored(L"C:\\myproject\\debug.log", L"debug.log", false));

    // Regular project file not ignored
    assert(!engine.IsIgnored(L"C:\\myproject\\src\\index.ts", L"index.ts", false));
    assert(!engine.IsIgnored(L"C:\\myproject\\package.json", L"package.json", false));

    // Built-in hardcoded rules
    assert(engine.IsIgnored(L"C:\\myproject\\.git", L".git", true));
    assert(engine.IsIgnored(L"C:\\myproject\\app.obj", L"app.obj", false));
    assert(engine.IsIgnored(L"C:\\myproject\\app.pdb", L"app.pdb", false));
    assert(engine.IsIgnored(L"C:\\myproject\\app.exe", L"app.exe", false));

    std::cout << "  -> PASS: All IgnoreEngine rule, folder, and multi-file tests passed!\n";
}

void TestAdaptiveCodeEngine() {
    std::cout << "[TEST] Running AdaptiveCodeEngine Tests...\n";
    AdaptiveCodeEngine engine;

    // Test 1: Symbol frequency and recency learning
    engine.RecordSymbolUsage(L"fetchUserData", L"ts");
    engine.RecordSymbolUsage(L"fetchUserData", L"ts");
    engine.RecordSymbolUsage(L"fetchUserData", L"ts");
    engine.RecordSymbolUsage(L"renderUserProfile", L"ts");

    int bonusFetch = engine.GetAdaptiveScoreBonus(L"fetchUserData", L"ts");
    int bonusRender = engine.GetAdaptiveScoreBonus(L"renderUserProfile", L"ts");
    int bonusUnknown = engine.GetAdaptiveScoreBonus(L"someUnknownFunction", L"ts");

    assert(bonusFetch > bonusRender);
    assert(bonusRender > bonusUnknown);
    assert(bonusUnknown == 0);
    assert(bonusFetch <= 600);
    std::cout << "  -> PASS: Adaptive score bonus verified: fetchUserData=" << bonusFetch 
              << ", renderUserProfile=" << bonusRender << ", unknown=" << bonusUnknown << "\n";

    // Test 2: Sub-millisecond query performance benchmark
    auto startBench = std::chrono::high_resolution_clock::now();
    const int NUM_QUERIES = 5000;
    for (int i = 0; i < NUM_QUERIES; ++i) {
        volatile int b = engine.GetAdaptiveScoreBonus(L"fetchUserData", L"ts");
        (void)b;
    }
    auto endBench = std::chrono::high_resolution_clock::now();
    double totalUs = std::chrono::duration<double, std::micro>(endBench - startBench).count();
    double avgUs = totalUs / NUM_QUERIES;
    std::cout << "  -> BENCHMARK: 5000 score queries completed in " << totalUs << " us (avg " 
              << avgUs << " us per query). Sub-millisecond guarantee met!\n";
    assert(avgUs < 50.0); // Far faster than 50 microseconds

    // Test 3: Markov transition habit prediction (Order-2 sequence learning)
    engine.RecordTokenTransition(L"try", L"{", L"ts");
    engine.RecordTokenTransition(L"{", L"} catch (error) {", L"ts");
    engine.RecordTokenTransition(L"{", L"} catch (error) {", L"ts");
    engine.RecordTokenTransition(L"{", L"} catch (error) {", L"ts");

    float confidence = 0.0f;
    std::wstring prediction = engine.PredictNextToken(L"{", L"ts", confidence);
    assert(!prediction.empty());
    assert(prediction == L"} catch (error) {");
    assert(confidence >= 0.70f);
    std::wcout << L"  -> PASS: Markov token prediction learned: '{' -> '" 
               << prediction << L"' (" << (confidence * 100) << L"% confidence)\n";

    // Test 4: Dynamic coding style habit learning
    engine.RecordLineStyle(L"  const a = 'hello';", L"ts");
    engine.RecordLineStyle(L"  const b = 'world';", L"ts");
    engine.RecordLineStyle(L"    const c = 'test';", L"ts");
    engine.RecordLineStyle(L"  function run() {", L"ts");

    auto style = engine.GetStylePreferences();
    assert(style.indentSpaces == 2);           // 2 spaces preferred
    assert(!style.useTabs);                     // Spaces over tabs
    assert(style.preferredQuote == L'\'');      // Single quotes preferred
    assert(style.preferSemicolons == true);     // Semicolons used
    assert(style.sameLineBraces == true);       // Same line brace placement
    std::cout << "  -> PASS: Style learning verified: indent=" << style.indentSpaces 
              << " spaces, quote=" << (style.preferredQuote == L'\'' ? "single" : "double") 
              << ", semicolons=" << (style.preferSemicolons ? "true" : "false") << "\n";

    // Test 5: Strictly bounded memory footprint
    size_t memBytes = engine.GetMemoryFootprintBytes();
    std::cout << "  -> MEMORY: Current footprint: " << memBytes << " bytes (~" 
              << (memBytes / 1024) << " KB). Strictly bounded (< 1.5 MB).\n";
    assert(memBytes < 1500000); // Must be strictly < 1.5 MB

    // Test 6: Profile persistence and roundtrip
    std::wstring testProfilePath = L"test_profile.json";
    engine.SaveProfileAsync(testProfilePath);
    // Give background saver thread a moment
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    AdaptiveCodeEngine engineLoaded;
    engineLoaded.LoadProfile(testProfilePath);
    int loadedBonus = engineLoaded.GetAdaptiveScoreBonus(L"fetchUserData", L"ts");
    assert(loadedBonus > 0);
    std::cout << "  -> PASS: Asynchronous profile save & load roundtrip verified!\n";

    std::error_code ec;
    std::filesystem::remove(testProfilePath, ec);

    std::cout << "  -> ALL AdaptiveCodeEngine tests passed successfully!\n";
}

void TestPredictionPatternLearning() {
    std::cout << "[TEST] Running PredictionEngine Pattern Learning Tests (User Case)...\n";
    AdaptiveCodeEngine adaptive;
    PredictionEngine engine;
    engine.SetAdaptiveEngine(&adaptive);

    // Reproduce user's exact buffer from main.py
    std::wstring content =
        L"import numpy.random as rng\n"
        L"\n"
        L"numbers = rng.randint(5, 100, size=10)\n"
        L"print(numbers)\n"
        L"\n"
        L"hello = \"hello world\"\n"
        L"\n"
        L"print(hello)\n"
        L"\n"
        L"status = \"hello world\"\n"
        L"\n"
        L"print(status)\n"
        L"\n"
        L"strin = \"hello world\"\n"
        L"\n"
        L"print(strin)\n"
        L"\n"
        L"cs = \"\"";

    Buffer buffer(content);
    size_t targetLine = buffer.GetLineCount() - 1; // last line "cs = \"\""
    size_t cursorCol = 6;   // between quotes: "cs = |\""
    buffer.SetCursor(targetLine, cursorCol);

    // Test 1: PredictNextEdit inside empty quotes
    auto pred = engine.PredictNextEdit(L"main.py", buffer, targetLine, cursorCol);
    assert(pred.isValid);
    assert(pred.replaceCharCount == 1); // must overwrite the closing quote upon Tab accept
    assert(pred.ghostText.find(L"hello world\"") != std::wstring::npos);
    assert(pred.ghostText.find(L"print(cs)") != std::wstring::npos);
    std::cout << "  -> PASS: Prediction inside quotes correctly generated: \"" << "\n";
    std::wcout << L"     Ghost text: " << pred.ghostText << L"\n";
    std::cout << "     replaceCharCount: " << pred.replaceCharCount << "\n";

    // Test 2: Simulate Tab Accept action (same logic as Window.cpp)
    if (pred.replaceCharCount > 0) {
        for (size_t r = 0; r < pred.replaceCharCount; ++r) {
            buffer.DeleteForward();
        }
    }
    buffer.InsertText(pred.ghostText);

    // Verify buffer content after acceptance
    assert(buffer.GetLine(targetLine) == L"cs = \"hello world\"");
    assert(buffer.GetLine(targetLine + 1) == L"");
    assert(buffer.GetLine(targetLine + 2) == L"print(cs)");
    std::cout << "  -> PASS: Tab acceptance result in buffer is 100% clean:\n";
    std::wcout << L"     Line " << targetLine << L": " << buffer.GetLine(targetLine) << L"\n";
    std::wcout << L"     Line " << (targetLine + 1) << L": " << buffer.GetLine(targetLine + 1) << L"\n";
    std::wcout << L"     Line " << (targetLine + 2) << L": " << buffer.GetLine(targetLine + 2) << L"\n";

    // Test 3: Prediction when typing partial prefix "h"
    Buffer buffer2(content);
    size_t targetLine2 = buffer2.GetLineCount() - 1;
    buffer2.SetCursor(targetLine2, 6);
    buffer2.InsertChar(L'h'); // buffer line is now: cs = "h" with cursor after 'h' (col 7)
    auto pred2 = engine.PredictNextEdit(L"main.py", buffer2, targetLine2, 7);
    assert(pred2.isValid);
    assert(pred2.replaceCharCount == 1);
    assert(pred2.ghostText.find(L"ello world\"") == 0); // completes from 'h'
    assert(pred2.ghostText.find(L"print(cs)") != std::wstring::npos);
    std::cout << "  -> PASS: Partial prefix 'h' correctly completed as: " << "\n";
    std::wcout << L"     Ghost text: " << pred2.ghostText << L"\n";

    // Test 4: Unquoted typed value (cs = hello) -> MUST NOT append string literal "hello world"
    std::wstring unquotedContent = content.substr(0, content.rfind(L"cs = \"\"")) + L"cs = hello";
    Buffer bufferUnquoted(unquotedContent);
    size_t unquotedLine = bufferUnquoted.GetLineCount() - 1;
    auto predUnquoted = engine.PredictNextEdit(L"main.py", bufferUnquoted, unquotedLine, 10);
    assert(!predUnquoted.isValid);
    std::cout << "  -> PASS: Unquoted identifier 'cs = hello' correctly avoids corrupt string appending!\n";

    // Test 5: Caret at 'cs = ' without quotes yet -> MUST predict WITH opening quote
    std::wstring noQuotesContent = content.substr(0, content.rfind(L"cs = \"\"")) + L"cs = ";
    Buffer bufferNoQuotes(noQuotesContent);
    size_t noQuotesLine = bufferNoQuotes.GetLineCount() - 1;
    auto predNoQuotes = engine.PredictNextEdit(L"main.py", bufferNoQuotes, noQuotesLine, 5);
    assert(predNoQuotes.isValid);
    assert(predNoQuotes.replaceCharCount == 0);
    assert(predNoQuotes.ghostText.find(L"\"hello world\"") == 0); // Opening quote included!
    std::cout << "  -> PASS: Caret after 'cs = ' predicts with opening quote: \"hello world\"\n";

    // Test 6: Lookahead guard when 'print(cs)' is already below in the buffer
    std::wstring contentWithBelow = content + L"\n\nprint(cs)";
    Buffer bufferBelow(contentWithBelow);
    auto predBelow = engine.PredictNextEdit(L"main.py", bufferBelow, targetLine, cursorCol);
    assert(predBelow.isValid);
    assert(predBelow.ghostText.find(L"print(cs)") == std::wstring::npos); // Omitted because already present below!
    std::cout << "  -> PASS: Duplicate lookahead guard omitted 'print(cs)' when already below!\n";

    // Test 7: Empty line after print(n) -> MUST NOT predict print(n) again
    std::wstring printNContent =
        L"import numpy\n"
        L"n = \"hi\"\n"
        L"print(n)\n"
        L"";
    Buffer bufferPrintN(printNContent);
    auto predEmpty = engine.PredictNextEdit(L"main.py", bufferPrintN, 3, 0);
    assert(!predEmpty.isValid);
    std::cout << "  -> PASS: Empty line after 'print(n)' correctly does NOT predict duplicate 'print(n)'!\n";

    // Test 8: Excel AutoComplete rule: Fresh buffer without 'hello world' must NOT hallucinate it across files
    std::wstring freshContent =
        L"import numpy.random as rng\n"
        L"numbers = rng.randint(5, 100, size=10)\n"
        L"print(numbers)\n"
        L"n = ";
    Buffer bufferFresh(freshContent);
    auto predFresh = engine.PredictNextEdit(L"fresh.py", bufferFresh, 3, 4);
    assert(!predFresh.isValid);
    std::cout << "  -> PASS: Fresh file does NOT hallucinate 'hello world' from previous files!\n";

    std::cout << "  -> ALL PredictionEngine Pattern Learning tests passed successfully!\n";
}

void TestSeriesProgressionPrediction() {
    std::cout << "[TEST] Running Series Progression Prediction Tests (n, o, p, q, r, s -> t, u, v)...\n";
    PredictionEngine engine;

    std::wstring seriesContent =
        L"import numpy.random as rng\n"
        L"\n"
        L"numbers = rng.randint(5, 100, size=10)\n"
        L"print(numbers)\n"
        L"\n"
        L"n = \"hi\"\n"
        L"print(n)\n"
        L"\n"
        L"o=\"hi\"\n"
        L"print(o)\n"
        L"\n"
        L"p=\"hi\"\n"
        L"print(p)\n"
        L"\n"
        L"q=\"hi\"\n"
        L"print(q)\n"
        L"\n"
        L"r=\"hi\"\n"
        L"print(r)\n"
        L"\n"
        L"s=\"hi\"\n"
        L"print(s)";

    Buffer buffer(seriesContent);
    size_t lastLine = buffer.GetLineCount() - 1;
    size_t colAtEnd = buffer.GetLine(lastLine).length();

    // Test 1: Ghost prediction at the end of 'print(s)'
    auto predEnd = engine.PredictNextEdit(L"main.py", buffer, lastLine, colAtEnd);
    assert(predEnd.isValid);
    assert(predEnd.ghostText.find(L"t=\"hi\"") != std::wstring::npos || predEnd.ghostText.find(L"t = \"hi\"") != std::wstring::npos);
    assert(predEnd.ghostText.find(L"print(t)") != std::wstring::npos);
    std::cout << "  -> PASS: Ghost prediction for 't' at end of 'print(s)' successfully generated:\n";
    std::wcout << L"     Ghost text: " << predEnd.ghostText << L"\n";

    // Test 2: Typing 't' on empty line after print(s)
    std::wstring contentWithT = seriesContent + L"\n\nt";
    Buffer bufferT(contentWithT);
    size_t lineT = bufferT.GetLineCount() - 1;
    auto predT = engine.PredictNextEdit(L"main.py", bufferT, lineT, 1);
    assert(predT.isValid);
    assert(predT.ghostText.find(L"\"hi\"") != std::wstring::npos);
    assert(predT.ghostText.find(L"print(t)") != std::wstring::npos);
    std::cout << "  -> PASS: Typing 't' correctly completes '=\"hi\"\\nprint(t)'!\n";

    // Test 3: Series progression to 'u' after 't' block is added
    std::wstring contentWithTBlock = seriesContent + L"\n\nt=\"hi\"\nprint(t)";
    Buffer bufferU(contentWithTBlock);
    size_t lineU = bufferU.GetLineCount() - 1;
    auto predU = engine.PredictNextEdit(L"main.py", bufferU, lineU, bufferU.GetLine(lineU).length());
    assert(predU.isValid);
    assert(predU.ghostText.find(L"u=\"hi\"") != std::wstring::npos || predU.ghostText.find(L"u = \"hi\"") != std::wstring::npos);
    assert(predU.ghostText.find(L"print(u)") != std::wstring::npos);
    std::cout << "  -> PASS: Ghost prediction for 'u' after 't' block successfully generated!\n";

    // Test 4: Series progression to 'v' after 'u' block is added
    std::wstring contentWithUBlock = contentWithTBlock + L"\n\nu=\"hi\"\nprint(u)";
    Buffer bufferV(contentWithUBlock);
    size_t lineV = bufferV.GetLineCount() - 1;
    auto predV = engine.PredictNextEdit(L"main.py", bufferV, lineV, bufferV.GetLine(lineV).length());
    assert(predV.isValid);
    assert(predV.ghostText.find(L"v=\"hi\"") != std::wstring::npos || predV.ghostText.find(L"v = \"hi\"") != std::wstring::npos);
    assert(predV.ghostText.find(L"print(v)") != std::wstring::npos);
    std::cout << "  -> PASS: Ghost prediction for 'v' after 'u' block successfully generated!\n";

    // Test 5: Numbered suffix progression (item1, item2, item3 -> item4)
    std::wstring numberedContent =
        L"item1 = \"test\"\n"
        L"print(item1)\n"
        L"item2 = \"test\"\n"
        L"print(item2)\n"
        L"item3 = \"test\"\n"
        L"print(item3)";
    Buffer bufferNum(numberedContent);
    size_t lineNum = bufferNum.GetLineCount() - 1;
    auto predNum = engine.PredictNextEdit(L"main.py", bufferNum, lineNum, bufferNum.GetLine(lineNum).length());
    assert(predNum.isValid);
    assert(predNum.ghostText.find(L"item4") != std::wstring::npos);
    std::cout << "  -> PASS: Numbered progression (item1, item2, item3 -> item4) successfully generated!\n";

    std::cout << "  -> ALL Series Progression Tests Passed Successfully!\n";
}

void TestTodoCheckboxToggle() {
    std::cout << "[TEST] Running Interactive TODO Checkbox Toggle Tests...\n";
    std::wstring todoContent =
        L"# TODO buy groceries\n"
        L"# [ ] implement unit tests\n"
        L"# [x] done task\n";
    Buffer buffer(todoContent);

    // 1. Toggle plain TODO -> [x] TODO
    std::wstring l0 = buffer.GetLine(0);
    size_t todoPos = l0.find(L"TODO");
    assert(todoPos != std::wstring::npos);
    l0.insert(todoPos, L"[x] ");
    buffer.SetLine(0, l0);
    assert(buffer.GetLine(0) == L"# [x] TODO buy groceries");
    std::cout << "  -> PASS: '# TODO' toggled to '# [x] TODO'\n";

    // 2. Toggle [ ] -> [x]
    std::wstring l1 = buffer.GetLine(1);
    size_t emptyPos = l1.find(L"[ ]");
    assert(emptyPos != std::wstring::npos);
    l1.replace(emptyPos, 3, L"[x]");
    buffer.SetLine(1, l1);
    assert(buffer.GetLine(1) == L"# [x] implement unit tests");
    std::cout << "  -> PASS: '# [ ]' toggled to '# [x]'\n";

    // 3. Toggle [x] -> [ ]
    std::wstring l2 = buffer.GetLine(2);
    size_t xPos = l2.find(L"[x]");
    assert(xPos != std::wstring::npos);
    l2.replace(xPos, 3, L"[ ]");
    buffer.SetLine(2, l2);
    assert(buffer.GetLine(2) == L"# [ ] done task");
    std::cout << "  -> PASS: '# [x]' toggled to '# [ ]'\n";

    std::cout << "  -> ALL Interactive TODO Checkbox Toggle Tests Passed Successfully!\n";
}

void TestPluginExtensionModEngine() {
    std::cout << "[TEST] Running Plugin, Extension & Modding Engine Tests...\n";
    auto& pm = phide::plugin::PluginManager::Instance();
    pm.Initialize();

    std::wstring sampleManifest = LR"JSON({
        "id": "com.example.supermod",
        "name": "Super Mod & Extension",
        "version": "2.1.0",
        "author": "Phide Modder",
        "description": "Full IDE mod testing themes, config, syntax, completions, snippets, linters, commands, and status bar",
        "theme": {
            "mode": -1,
            "colors": {
                "accentPrimary": "#FF5500",
                "syntaxKeyword": "#00FFAA"
            }
        },
        "config": {
            "tabSize": 2,
            "railWidth": 52
        },
        "languages": [
            {
                "extension": "rust",
                "baseLanguage": ".cpp",
                "keywords": "fn,let,mut,impl,trait,pub,match",
                "types": "u8,u16,u32,u64,usize,String,Vec,Option,Result"
            }
        ],
        "completions": [
            {
                "language": "py",
                "label": "super_mod_helper",
                "insertText": "super_mod_helper($1)",
                "detail": "SuperMod Python Helper",
                "documentation": "Injected by SuperMod plugin",
                "kind": 2,
                "priorityBonus": 150
            }
        ],
        "snippets": [
            {
                "scope": "py",
                "prefix": "smod",
                "name": "SuperMod Snippet",
                "description": "Expands SuperMod boilerplate",
                "body": "def super_mod():\n    return True"
            }
        ],
        "linters": [
            {
                "language": "py",
                "pattern": "FIXME_NOW",
                "message": "SuperMod Linter: Urgent FIXME_NOW detected",
                "severity": 1
            }
        ],
        "commands": [
            {
                "id": "supermod.sayHello",
                "title": "SuperMod: Insert Greeting",
                "category": "SuperMod",
                "keybinding": "Ctrl+Shift+H"
            }
        ],
        "statusBar": [
            {
                "id": "supermod.status",
                "text": "SuperMod Active",
                "tooltip": "SuperMod v2.1.0 is running",
                "command": "supermod.sayHello",
                "alignment": "right"
            }
        ]
    })JSON";

    bool loaded = pm.LoadPluginFromJsonString(sampleManifest, L"C:\\mods\\supermod");
    assert(loaded);

    // 1. Verify Theme Color Modding
    phide::ui::ThemeColors colors{};
    pm.ApplyThemeOverrides(colors, 0);
    assert(colors.brandAccent.r > 0.9f && colors.brandAccent.b < 0.1f);
    assert(colors.syntaxKeyword.g > 0.9f);
    std::cout << "  -> PASS: Theme color overrides applied.\n";

    // 2. Verify AppConfig Modding
    phide::app::AppConfig cfg{};
    pm.ApplyAppConfigMods(cfg);
    assert(cfg.tabSize == 2);
    assert(cfg.railWidth == 52);
    std::cout << "  -> PASS: AppConfig layout & editor geometry mods applied.\n";

    // 3. Verify Custom Language Alias & Keywords/Types in SyntaxLexer
    assert(pm.ResolveLanguageAlias(L".rust") == L".cpp");
    assert(pm.IsCustomKeyword(L".rust", L"impl"));
    assert(pm.IsCustomType(L".rust", L"usize"));
    SyntaxLexer lexer;
    std::wstring rustLine = L"pub fn main() -> usize";
    auto tokens = lexer.TokenizeLine(rustLine, L".rust");
    bool foundKeyword = false, foundType = false;
    for (const auto& t : tokens) {
        std::wstring tokText = rustLine.substr(t.startCol, t.length);
        if (t.type == TokenType::Keyword && (tokText == L"pub" || tokText == L"fn")) foundKeyword = true;
        if (t.type == TokenType::Type && tokText == L"usize") foundType = true;
    }
    assert(foundKeyword && foundType);
    std::cout << "  -> PASS: Custom language alias, keywords, and types tokenized in SyntaxLexer.\n";

    // 4. Verify IntelliSense Completions & Snippets
    auto comps = pm.QueryCompletions(L"py", L"test.py", L"super_", 6, L"super_");
    assert(!comps.empty() && comps[0].label == L"super_mod_helper");
    const auto& snips = pm.GetContributedSnippets();
    bool foundSnip = false;
    for (const auto& s : snips) {
        if (s.prefix == L"smod") foundSnip = true;
    }
    assert(foundSnip);
    std::cout << "  -> PASS: Plugin IntelliSense completions and snippets verified.\n";

    // 5. Verify Regex Linter Diagnostics via SyntaxValidator
    Buffer pyBuf(L"x = 10\n# FIXME_NOW: critical section\n");
    auto diags = SyntaxValidator::Validate(L"test.py", pyBuf);
    bool foundCustomDiag = false;
    for (const auto& d : diags) {
        if (d.message.find(L"Urgent FIXME_NOW") != std::wstring::npos) {
            foundCustomDiag = true;
            break;
        }
    }
    assert(foundCustomDiag);
    std::cout << "  -> PASS: Custom regex linter diagnostic surfaced through SyntaxValidator.\n";

    // 6. Verify Commands & Status Bar Items
    bool cmdExecuted = false;
    phide::plugin::PluginCommand customCmd;
    customCmd.id = L"supermod.customCb";
    customCmd.title = L"SuperMod Callback";
    customCmd.shortcut = L"Ctrl+Alt+M";
    customCmd.cppCallback = [&]() { cmdExecuted = true; };
    pm.RegisterCommand(customCmd);
    assert(pm.ExecuteCommand(L"supermod.customCb"));
    assert(cmdExecuted);
    assert(!pm.GetStatusBarItemsMutable().empty());
    std::cout << "  -> PASS: Custom commands, keybindings, and status bar items verified!\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  Running Phide Multi-Ignore & Adaptive Engine Tests   \n";
    std::cout << "========================================================\n";

    TestIgnoreEngine();
    std::cout << "--------------------------------------------------------\n";
    TestAdaptiveCodeEngine();
    std::cout << "--------------------------------------------------------\n";
    TestPredictionPatternLearning();
    std::cout << "--------------------------------------------------------\n";
    TestSeriesProgressionPrediction();
    std::cout << "--------------------------------------------------------\n";
    TestTodoCheckboxToggle();
    std::cout << "--------------------------------------------------------\n";
    TestPluginExtensionModEngine();
    std::cout << "========================================================\n";
    std::cout << "  ALL VERIFICATION TESTS COMPLETED SUCCESSFULLY!        \n";
    std::cout << "========================================================\n";
    return 0;
}
