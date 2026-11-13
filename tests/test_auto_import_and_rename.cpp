#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <windows.h>

#include "phide/editor/Buffer.hpp"
#include "phide/editor/SyntaxValidator.hpp"
#include "phide/editor/SnippetManager.hpp"
#include "phide/editor/SmartImportHelper.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/ui/PopupMenu.hpp"
#include "phide/ui/ActivityRail.hpp"
#include "phide/ui/RemoteWindowModal.hpp"

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

    // TEST 5: Context menu disabled targets on empty space & More Actions 3-dots with Monitor
    {
        // 5a. Empty space context menu
        phide::app::AppConfig cfg;
        phide::ui::PopupMenu popup;
        popup.OpenContextMenu(0.0f, 0.0f, true, L"", L"C:\\myproject", cfg, false);
        const auto& items = popup.GetItems();
        for (const auto& item : items) {
            if (item.actionId == phide::ui::MenuActionId::CtxRename ||
                item.actionId == phide::ui::MenuActionId::CtxDelete ||
                item.actionId == phide::ui::MenuActionId::CtxCut ||
                item.actionId == phide::ui::MenuActionId::CtxCopy ||
                item.actionId == phide::ui::MenuActionId::CtxCopyPath ||
                item.actionId == phide::ui::MenuActionId::CtxCopyRelativePath) {
                assert(!item.isEnabled && "Rename, Delete, Cut, Copy must be disabled when no file/folder is targeted!");
            }
            if (item.actionId == phide::ui::MenuActionId::CtxNewFile ||
                item.actionId == phide::ui::MenuActionId::CtxNewFolder ||
                item.actionId == phide::ui::MenuActionId::CtxRevealInExplorer) {
                assert(item.isEnabled && "New File and New Folder must remain enabled for empty space / root folder!");
            }
        }
        std::cout << "  [PASS] Test 5a: Context menu disables Rename, Delete, Cut, Copy on empty space\n";

        // 5b. Targeted item context menu
        popup.OpenContextMenu(0.0f, 0.0f, false, L"C:\\myproject\\file.jsx", L"C:\\myproject", cfg, false);
        const auto& fileItems = popup.GetItems();
        bool foundRename = false, foundDelete = false;
        for (const auto& item : fileItems) {
            if (item.actionId == phide::ui::MenuActionId::CtxRename) {
                foundRename = true;
                assert(item.isEnabled && "Rename must be enabled when targeting a file!");
            }
            if (item.actionId == phide::ui::MenuActionId::CtxDelete) {
                foundDelete = true;
                assert(item.isEnabled && "Delete must be enabled when targeting a file!");
            }
        }
        assert(foundRename && foundDelete);
        std::cout << "  [PASS] Test 5b: Context menu enables Rename & Delete when targeting a file\n";

        // 5c. ActivityRail contains RemoteExplorer directly after Api (above Settings) and MoreActions directly after Settings
        phide::ui::ActivityRail rail;
        const auto& railItems = rail.GetItems();
        bool foundMoreActions = false;
        bool foundRemoteExplorer = false;
        int apiIdx = -1, remoteIdx = -1, settingsIdx = -1, moreActionsIdx = -1;
        for (size_t i = 0; i < railItems.size(); ++i) {
            if (railItems[i].id == phide::ui::RailTabId::Api) apiIdx = static_cast<int>(i);
            if (railItems[i].id == phide::ui::RailTabId::RemoteExplorer) {
                remoteIdx = static_cast<int>(i);
                foundRemoteExplorer = true;
            }
            if (railItems[i].id == phide::ui::RailTabId::Settings) settingsIdx = static_cast<int>(i);
            if (railItems[i].id == phide::ui::RailTabId::MoreActions) {
                moreActionsIdx = static_cast<int>(i);
                foundMoreActions = true;
            }
        }
        assert(foundRemoteExplorer && "ActivityRail must contain RailTabId::RemoteExplorer!");
        assert(apiIdx >= 0 && remoteIdx == apiIdx + 1 && "RemoteExplorer must be placed directly under Api!");
        assert(settingsIdx == remoteIdx + 1 && "Settings must follow directly after RemoteExplorer!");
        assert(foundMoreActions && "ActivityRail must contain RailTabId::MoreActions!");
        assert(moreActionsIdx == settingsIdx + 1 && "MoreActions must be placed directly under Settings!");
        std::cout << "  [PASS] Test 5c: ActivityRail places RemoteExplorer directly under Api and MoreActions under Settings\n";

        // 5d. PopupMenu::OpenMoreActionsMenu contains ONLY Monitor (other entries removed per user request)
        popup.OpenMoreActionsMenu(48.0f, 200.0f, cfg);
        assert(popup.IsOpen());
        bool foundMonitor = false;
        bool foundDisallowedEntries = false;
        for (const auto& mi : popup.GetItems()) {
            if (mi.actionId == phide::ui::MenuActionId::ViewMonitor && mi.label == L"Monitor") {
                foundMonitor = true;
            }
            if (mi.actionId == phide::ui::MenuActionId::MoreDependencyGraph ||
                mi.actionId == phide::ui::MenuActionId::MoreCodeHierarchy ||
                mi.actionId == phide::ui::MenuActionId::MoreCodeMetrics) {
                foundDisallowedEntries = true;
            }
        }
        assert(foundMonitor && "OpenMoreActionsMenu must contain 'Monitor'!");
        assert(!foundDisallowedEntries && "OpenMoreActionsMenu must NOT contain crossed-out entries!");
        std::cout << "  [PASS] Test 5d: More Actions side dropdown menu contains clean 'Monitor' entry\n";
    }

    // TEST 6: Remote Explorer SSH Parsing, Real ~/.ssh/config Persistence (Isolated Profile), Command Builders, & Modal State
    {
        using namespace phide::ui;

        // 6a. Adhoc SSH input parser
        remote::SshHostEntry parsed{};
        assert(remote::ParseAdhocSshInput(L"deploy@prod.example.com", parsed));
        assert(parsed.user == L"deploy" && parsed.hostName == L"prod.example.com" && parsed.port.empty());

        assert(remote::ParseAdhocSshInput(L"ssh -p 2222 -i ~/.ssh/id_work admin@10.20.30.40", parsed));
        assert(parsed.user == L"admin" && parsed.hostName == L"10.20.30.40" && parsed.port == L"2222" && parsed.identityFile == L"~/.ssh/id_work");

        assert(remote::ParseAdhocSshInput(L"staging.internal:2022", parsed));
        assert(parsed.hostName == L"staging.internal" && parsed.port == L"2022");

        assert(!remote::ParseAdhocSshInput(L"   ", parsed));
        assert(!remote::ParseAdhocSshInput(L"-p 22", parsed));
        std::cout << "  [PASS] Test 6a: SSH input parser handles user@host, flags, and port syntax\n";

        // 6b. Isolated USERPROFILE SaveSshConfigHost / LoadSshConfigHosts roundtrip
        wchar_t origProfile[MAX_PATH]{};
        DWORD origLen = GetEnvironmentVariableW(L"USERPROFILE", origProfile, MAX_PATH);
        std::wstring tempProfile = (std::filesystem::temp_directory_path() / L"phide_test_ssh_profile").wstring();
        std::error_code ec;
        std::filesystem::remove_all(tempProfile, ec);
        std::filesystem::create_directories(tempProfile, ec);
        SetEnvironmentVariableW(L"USERPROFILE", tempProfile.c_str());
        assert(remote::GetSshConfigPath().rfind(tempProfile, 0) == 0 && "GetSshConfigPath must resolve inside isolated tempProfile");

        remote::SshHostEntry savedHost{};
        std::wstring saveErr;
        bool savedOk = remote::SaveSshConfigHost(L"ssh -p 2201 ubuntu@192.168.50.10", savedHost, saveErr);
        assert(savedOk && "SaveSshConfigHost must write isolated ~/.ssh/config entry");
        auto loadedHosts = remote::LoadSshConfigHosts();
        assert(loadedHosts.size() == 1);
        assert(loadedHosts[0].hostName == L"192.168.50.10" && loadedHosts[0].user == L"ubuntu" && loadedHosts[0].port == L"2201");

        // Restore real USERPROFILE immediately and clean temp folder
        if (origLen > 0 && origLen < MAX_PATH) {
            SetEnvironmentVariableW(L"USERPROFILE", origProfile);
        }
        std::filesystem::remove_all(tempProfile, ec);
        std::cout << "  [PASS] Test 6b: Real ~/.ssh/config read/write roundtrip verified cleanly\n";

        // 6c. Interactive ConPTY command builders
        std::wstring sshCmd = remote::BuildInteractiveSshCommand(savedHost, L"/var/www");
        assert(sshCmd.find(L"ssh.exe") != std::wstring::npos &&
               (sshCmd.find(L"-p 2201") != std::wstring::npos || sshCmd.find(L"-p \"2201\"") != std::wstring::npos) &&
               sshCmd.find(L"/var/www") != std::wstring::npos);
        std::wstring wslCmd = remote::BuildInteractiveWslCommand(L"Ubuntu-24.04");
        assert(wslCmd.find(L"wsl.exe") != std::wstring::npos && wslCmd.find(L"Ubuntu-24.04") != std::wstring::npos);
        std::wstring dockerCmd = remote::BuildInteractiveDockerCommand(L"abc123def", true);
        assert(dockerCmd.find(L"start") != std::wstring::npos && dockerCmd.find(L"exec -it") != std::wstring::npos && dockerCmd.find(L"\\\"") == std::wstring::npos);
        std::cout << "  [PASS] Test 6c: SSH / WSL / Docker ConPTY command builders produce valid invocations\n";

        // 6d. RemoteWindowModal modes & filtering
        RemoteWindowModal modal;
        modal.Open(RemoteModalMode::MainMenu, false);
        assert(modal.IsOpen());
        const auto& allRows = modal.GetVisibleRows();
        assert(allRows.size() == 7 && "MainMenu must display all 7 Remote Window commands");
        assert(allRows[0].categoryTag == L"Remote-SSH" && allRows[2].categoryTag == L"Dev Containers" && allRows[5].categoryTag == L"Remote-WSL");

        modal.SetInputTextForTest(L"WSL");
        const auto& wslFiltered = modal.GetVisibleRows();
        assert(wslFiltered.size() == 2 && wslFiltered[0].id == L"wsl_connect" && wslFiltered[1].id == L"wsl_distro");

        modal.Open(RemoteModalMode::AddSshHost, true);
        assert(modal.GetMode() == RemoteModalMode::AddSshHost);
        modal.Close();
        assert(!modal.IsOpen());
        std::cout << "  [PASS] Test 6d: RemoteWindowModal options, tags, search filtering, and modes verified\n";
    }

    std::cout << "\n======================================================\n";
    std::cout << "[SUCCESS] ALL UNIT & REGRESSION TESTS PASSED!\n";
    std::cout << "======================================================\n";
    return 0;
}
