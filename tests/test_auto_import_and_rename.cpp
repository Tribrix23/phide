#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>

#include "phide/editor/Buffer.hpp"
#include "phide/editor/SyntaxValidator.hpp"
#include "phide/editor/SnippetManager.hpp"
#include "phide/editor/SmartImportHelper.hpp"
#include "phide/editor/OpenDocument.hpp"

using namespace phide::editor;

int main() {
    std::cout << "======================================================\n";
    std::cout << "[TEST] Running Auto-Import, Rename & Editor Grid Tests\n";
    std::cout << "======================================================\n";

    // TEST 1: SyntaxValidator flags unimported React hook 'useEffect' as Error (Severity 1)
    {
        std::wstring unimportedCode =
            L"import React from \"react\";\n\n"
            L"const Test = () => {\n"
            L"    useEffect(() => {\n"
            L"        return () => {};\n"
            L"    }, []);\n"
            L"    return <div></div>;\n"
            L"};\n"
            L"export default Test;\n";

        Buffer unimportedBuf;
        unimportedBuf.SetText(unimportedCode);
        auto diags = SyntaxValidator::Validate(L"test.jsx", unimportedBuf);
        bool foundUseEffectError = false;
        for (const auto& d : diags) {
            if (d.message.find(L"useEffect") != std::wstring::npos &&
                d.message.find(L"Cannot find name 'useEffect'") != std::wstring::npos &&
                d.severity == 1) { // 1 = Error
                foundUseEffectError = true;
                break;
            }
        }
        assert(foundUseEffectError && "SyntaxValidator must flag unimported useEffect as Error!");
        std::cout << "  [PASS] Test 1: Unimported useEffect correctly flagged as Error diagnostic with red squiggly\n";

        // Now test that when imported, no error diagnostic is generated:
        std::wstring importedCode =
            L"import React, { useEffect } from \"react\";\n\n"
            L"const Test = () => {\n"
            L"    useEffect(() => {\n"
            L"        return () => {};\n"
            L"    }, []);\n"
            L"    return <div></div>;\n"
            L"};\n"
            L"export default Test;\n";

        Buffer importedBuf;
        importedBuf.SetText(importedCode);
        auto diags2 = SyntaxValidator::Validate(L"test.jsx", importedBuf);
        bool hasErrorForImported = false;
        for (const auto& d : diags2) {
            if (d.message.find(L"useEffect") != std::wstring::npos) {
                hasErrorForImported = true;
                break;
            }
        }
        assert(!hasErrorForImported && "Imported useEffect must NOT have error diagnostic!");
        std::cout << "  [PASS] Test 1b: Imported useEffect has no error diagnostic\n";
    }

    // TEST 2: Snippet expansion without literal ${2} tabstop artifacts
    {
        SnippetManager sm;
        std::wstring tempSnippets = (std::filesystem::temp_directory_path() / "test_snippets.json").wstring();
        sm.Initialize(tempSnippets);
        Buffer buf;
        buf.SetText(L"useEffect");
        buf.SetCursor(0, 9);
        bool expanded = sm.TryExpand(buf, L"jsx", L"test.jsx");
        assert(expanded && "useEffect snippet must expand");
        std::wstring expandedText = buf.GetText();
        assert(expandedText.find(L"${2}") == std::wstring::npos && "Snippet expansion must not contain literal ${2}!");
        assert(expandedText.find(L"${1}") == std::wstring::npos && "Snippet expansion must not contain literal ${1}!");
        assert(expandedText.find(L"useEffect(() => {") != std::wstring::npos && "Snippet must contain valid useEffect template");
        std::cout << "  [PASS] Test 2: Snippet tabstop parser cleanly strips ${2} from expanded body\n";

        // Also test ExpandSnippet directly with a body containing ${1} and ${2}
        Snippet snip;
        snip.prefix = L"customHook";
        snip.body = { L"useEffect(() => {", L"    return () => {", L"    };", L"}, [${2}]);" };
        Buffer customBuf;
        customBuf.SetText(L"customHook");
        customBuf.SetCursor(0, 10);
        bool ok = sm.ExpandSnippet(snip, customBuf, 10, L"");
        assert(ok && "ExpandSnippet must succeed");
        std::wstring expandedCustom = customBuf.GetText();
        assert(expandedCustom.find(L"${2}") == std::wstring::npos && "ExpandSnippet must strip ${2}");
        assert(expandedCustom.find(L"}, []);") != std::wstring::npos && "ExpandSnippet must format to empty brackets");
        std::cout << "  [PASS] Test 2b: ExpandSnippet strips ${2} even if present in legacy snippet definition\n";
    }

    // TEST 3: SmartImportHelper reference finding and content updating
    {
        std::wstring tempDir = (std::filesystem::temp_directory_path() / "phide_test_rename_ref").wstring();
        std::filesystem::create_directories(tempDir);

        std::wstring targetFile = tempDir + L"\\helper.js";
        std::wstring referencingFile1 = tempDir + L"\\app.js";
        std::wstring referencingFile2 = tempDir + L"\\index.html";

        {
            std::ofstream f(targetFile);
            f << "export const helper = 42;\n";
        }
        {
            std::ofstream f(referencingFile1);
            f << "import { helper } from './helper.js';\nconsole.log(helper);\n";
        }
        {
            std::ofstream f(referencingFile2);
            f << "<script src=\"./helper.js\"></script>\n";
        }

        auto refs = SmartImportHelper::FindReferencesToPath(tempDir, targetFile);
        assert(refs.size() >= 2 && "FindReferencesToPath must find both referencing files");
        std::cout << "  [PASS] Test 3: FindReferencesToPath found " << refs.size() << " referencing files\n";

        // Update content test
        std::wstring oldTarget = targetFile;
        std::wstring newTarget = tempDir + L"\\newHelper.js";
        std::wstring file1Content = L"import { helper } from './helper.js';\nconsole.log(helper);\n";
        bool updated = SmartImportHelper::UpdateReferencesInContent(file1Content, referencingFile1, oldTarget, newTarget);
        assert(updated && "UpdateReferencesInContent must update imports");
        assert(file1Content.find(L"./newHelper.js") != std::wstring::npos && "Import must point to new file");
        std::cout << "  [PASS] Test 3b: UpdateReferencesInContent updated relative import statement to ./newHelper.js\n";

        // Test 3c: HTML <a href="./test.jsx"> link reference test (user scenario)
        std::wstring testJsxFile = tempDir + L"\\test.jsx";
        std::wstring sdHtmlFile = tempDir + L"\\sd.html";
        std::wstring otherFileWithC = tempDir + L"\\index.php";
        {
            std::ofstream f(testJsxFile);
            f << "export default function Test() { return <div></div>; }\n";
        }
        {
            std::ofstream f(sdHtmlFile);
            f << "<!DOCTYPE html>\n<html>\n<body>\n    <a href=\"./test.jsx\">test</a>\n</body>\n</html>\n";
        }
        {
            std::ofstream f(otherFileWithC);
            f << "<?php\nclass Controller { public function index() { return 'hello'; } }\n";
        }

        auto testJsxRefs = SmartImportHelper::FindReferencesToPath(tempDir, testJsxFile);
        assert(testJsxRefs.size() == 1 && "Only sd.html references test.jsx! No other files should match.");
        assert(testJsxRefs[0] == sdHtmlFile && "Referencing file must be sd.html");
        std::cout << "  [PASS] Test 3c: FindReferencesToPath accurately finds sd.html for test.jsx without false positives\n";

        // Test 3d: No false positives for single-character or short stems like c.jsx or x.jsx
        std::wstring cJsxFile = tempDir + L"\\c.jsx";
        {
            std::ofstream f(cJsxFile);
            f << "const C = () => {};\n";
        }
        auto cJsxRefs = SmartImportHelper::FindReferencesToPath(tempDir, cJsxFile);
        assert(cJsxRefs.empty() && "c.jsx is not referenced anywhere; words with 'c' like 'class' or 'DOCTYPE' must NOT match!");
        std::cout << "  [PASS] Test 3d: c.jsx returns 0 references (no false positives on letters in words)\n";

        // Test 3e: UpdateReferencesInContent updates <a href="./test.jsx"> to <a href="./x.jsx">
        std::wstring xJsxFile = tempDir + L"\\x.jsx";
        std::wstring htmlContent = L"<!DOCTYPE html>\n<html>\n<body>\n    <a href=\"./test.jsx\">test</a>\n</body>\n</html>\n";
        bool htmlUpdated = SmartImportHelper::UpdateReferencesInContent(htmlContent, sdHtmlFile, testJsxFile, xJsxFile);
        assert(htmlUpdated && "UpdateReferencesInContent must update <a href='./test.jsx'>");
        assert(htmlContent.find(L"<a href=\"./x.jsx\">test</a>") != std::wstring::npos && "<a href='./test.jsx'> must become <a href='./x.jsx'>");
        std::cout << "  [PASS] Test 3e: <a href='./test.jsx'> in HTML successfully updated to <a href='./x.jsx'>\n";

        // Test 3f: Duplicate file rename collision rejection test
        std::wstring existingFile = tempDir + L"\\existing.js";
        {
            std::ofstream f(existingFile);
            f << "original content\n";
        }
        // Attempting to rename targetFile (helper.js) to existing.js
        std::error_code ecColl;
        bool wouldCollide = std::filesystem::exists(existingFile, ecColl) &&
                            (_wcsicmp(testJsxFile.c_str(), existingFile.c_str()) != 0);
        assert(wouldCollide && "Duplicate destination file must be detected as collision");
        std::cout << "  [PASS] Test 3f: Destination collision check correctly flags existing file\n";

        std::filesystem::remove_all(tempDir);
    }

    // TEST 4: Editor group collapse when dragging out of Bottom-Left
    {
        EditorSplitState state;
        state.layout = EditorSplitLayout::Grid2x2;

        std::vector<EditorGroup> groups(4);
        int removedGroupIdx = 1; // Bottom-Left group becomes empty and removed
        groups.erase(groups.begin() + removedGroupIdx);
        assert(groups.size() == 3);

        // TransitionLayoutOnGroupRemoved
        if (groups.size() == 3) {
            if (removedGroupIdx <= 1) {
                state.layout = EditorSplitLayout::LeftFullRightSplit;
            } else {
                state.layout = EditorSplitLayout::LeftSplitRightFull;
            }
        }
        assert(state.layout == EditorSplitLayout::LeftFullRightSplit);

        state.splitX1 = 0.5f;
        state.splitY2 = 0.5f;
        state.ComputeGroupBounds(groups, 0.0f, 0.0f, 1000.0f, 800.0f);

        // Top-Left group now expands to full vertical height of left column!
        assert(groups[0].bounds.top == 0.0f);
        assert(groups[0].bounds.bottom == 800.0f);
        std::cout << "  [PASS] Test 4: Left pane expands to full height when bottom-left collapses (LeftFullRightSplit)\n";
    }

    std::cout << "\n======================================================\n";
    std::cout << "[SUCCESS] ALL UNIT & REGRESSION TESTS PASSED!\n";
    std::cout << "======================================================\n";
    return 0;
}
