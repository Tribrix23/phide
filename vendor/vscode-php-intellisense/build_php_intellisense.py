import json
import re
from pathlib import Path

BASE_DIR = Path(__file__).parent
REPO_ROOT = BASE_DIR.parent.parent

def parse_ts_entries(ts_path: Path, export_name: str) -> dict:
    text = ts_path.read_text(encoding="utf-8", errors="replace")
    idx = text.find(f"export const {export_name}")
    if idx == -1:
        return {}
    sub = text[idx:]
    end_idx = sub.find("\n};")
    if end_idx != -1:
        sub = sub[:end_idx + 3]

    entries = {}
    # Match keys like:  name: { description: '...', signature: '...' },
    pattern = re.compile(r"^\t([A-Za-z0-9_$]+):\s*\{(.*?)\n\t\},?", re.MULTILINE | re.DOTALL)
    for m in pattern.finditer(sub):
        key = m.group(1)
        body = m.group(2)
        desc = ""
        sig = ""
        d_m = re.search(r"description:\s*'((?:\\'|[^'])*)'", body)
        if d_m:
            desc = d_m.group(1).replace("\\'", "'").replace("\\r\\n", " ").replace("\\n", " ").strip()
        s_m = re.search(r"signature:\s*'((?:\\'|[^'])*)'", body)
        if s_m:
            sig = s_m.group(1).replace("\\'", "'").strip()
        entries[key] = {"description": desc, "signature": sig}
    return entries

def parse_phpstorm_stub_docs(stub_files) -> dict:
    """Extract PHPDoc summaries for constants and methods in JetBrains phpstorm-stubs."""
    docs = {}
    for sf in stub_files:
        if not sf.exists():
            continue
        content = sf.read_text(encoding="utf-8", errors="replace")
        # Find /** ... */ followed by const NAME or function NAME
        pattern = re.compile(r"/\*\*(.*?)\*/\s*(?:public\s+|protected\s+|private\s+|static\s+|final\s+)*(?:const\s+([A-Z0-9_]+)|function\s+([A-Za-z0-9_]+))", re.DOTALL)
        for m in pattern.finditer(content):
            doc_raw = m.group(1)
            const_name = m.group(2)
            func_name = m.group(3)
            lines = []
            for line in doc_raw.splitlines():
                line = re.sub(r"^\s*\*\s?", "", line).strip()
                if not line or line.startswith("@"):
                    continue
                line = re.sub(r"<[^>]+>", "", line)
                lines.append(line)
                if len(lines) >= 2:
                    break
            summary = " ".join(lines).strip()
            if summary:
                if const_name and const_name not in docs:
                    docs[const_name] = summary
                if func_name and func_name not in docs:
                    docs[func_name] = summary
    return docs

def c_escape(s: str) -> str:
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\r", " ").replace("\n", " ").replace("\t", " ")
    # Collapse multiple spaces
    s = re.sub(r"\s+", " ", s).strip()
    if len(s) > 220:
        s = s[:217] + "..."
    # Keep printable ASCII / safe UTF-8 characters for wide string literals
    return "".join(ch if 32 <= ord(ch) < 127 else " " for ch in s)

def main():
    globals_ts = BASE_DIR / "phpGlobals.ts"
    funcs_ts = BASE_DIR / "phpGlobalFunctions.ts"
    refl_json = BASE_DIR / "php_reflection.json"

    vsc_vars = parse_ts_entries(globals_ts, "globalvariables")
    vsc_consts = parse_ts_entries(globals_ts, "compiletimeconstants")
    vsc_keywords = parse_ts_entries(globals_ts, "keywords")
    vsc_funcs = parse_ts_entries(funcs_ts, "globalfunctions")

    stub_docs = parse_phpstorm_stub_docs([
        BASE_DIR / "stub_PDO.php",
        BASE_DIR / "stub_mysqli.php",
        BASE_DIR / "stub_Core_c.php",
        BASE_DIR / "stub_date_c.php",
        BASE_DIR / "stub_SPL_c1.php",
    ])

    refl = json.loads(refl_json.read_text(encoding="utf-8"))

    print(f"VS Code Parsed: {len(vsc_vars)} vars, {len(vsc_consts)} consts, {len(vsc_keywords)} keywords, {len(vsc_funcs)} functions")
    print(f"Stubs Parsed: {len(stub_docs)} PHPDoc summaries")

    # 1. Merge Global Symbols (Variables, Keywords, Functions, Classes, Global Constants)
    # CompletionKind mapping:
    # 0 = Keyword, 1 = Function, 2 = Method, 3 = Class, 4 = Variable, 5 = Constant
    global_items = []
    seen_global = set()

    # 1a. Superglobals & Global variables from VS Code phpGlobals.ts
    for name, info in vsc_vars.items():
        if name in seen_global:
            continue
        seen_global.add(name)
        desc = info["description"] or "PHP Superglobal / Global Variable"
        global_items.append((name, desc, 4))

    # 1b. Keywords from VS Code phpGlobals.ts + PHP 8 keywords
    extra_keywords = [
        "match", "fn", "readonly", "enum", "never", "mixed", "iterable", "void",
        "true", "false", "null", "self", "parent", "static", "from"
    ]
    for name, info in vsc_keywords.items():
        if name in seen_global:
            continue
        seen_global.add(name)
        desc = info["description"] or "PHP Language Keyword"
        global_items.append((name, desc, 0))
    for kw in extra_keywords:
        if kw not in seen_global:
            seen_global.add(kw)
            global_items.append((kw, "PHP Language Keyword", 0))

    # 1c. Built-in Functions: merge PHP 8.5 Reflection (exact PHP 8.5 types) + VS Code phpGlobalFunctions.ts (rich descriptions!)
    all_func_names = sorted(set(refl["functions"].keys()) | set(vsc_funcs.keys()))
    for fn in all_func_names:
        if fn in seen_global or fn.startswith("__"):
            continue
        seen_global.add(fn)
        r_info = refl["functions"].get(fn)
        v_info = vsc_funcs.get(fn)
        sig = r_info["sig"] if r_info else (v_info["signature"] if v_info else "()")
        desc = v_info["description"] if (v_info and v_info["description"]) else (stub_docs.get(fn, ""))
        if sig and desc:
            detail = f"{sig} — {desc}"
        elif sig:
            detail = sig
        elif desc:
            detail = desc
        else:
            detail = "PHP Function"
        global_items.append((fn, detail, 1))

    # 1d. Classes, Interfaces, Traits, Enums from PHP 8.5 Reflection
    for cls_name, c_info in sorted(refl["classes"].items()):
        if cls_name in seen_global:
            continue
        seen_global.add(cls_name)
        doc = stub_docs.get(cls_name, f"PHP {c_info['kind']} ({c_info['ext']})")
        global_items.append((cls_name, doc, 3))

    # 1e. Compile-time & Global Constants from VS Code phpGlobals.ts + PHP 8.5 Reflection
    for cname, info in vsc_consts.items():
        if cname in seen_global:
            continue
        seen_global.add(cname)
        desc = info["description"] or "PHP Compile-Time / Global Constant"
        global_items.append((cname, desc, 5))

    for cname, cinfo in sorted(refl["constants"].items()):
        if cname in seen_global:
            continue
        seen_global.add(cname)
        doc = stub_docs.get(cname, "")
        detail = f"{cinfo['type']} ({cinfo['val']}) [{cinfo['cat']}]"
        if doc:
            detail += f" — {doc}"
        global_items.append((cname, detail, 5))

    # 2. Class Constants & Static Methods (ClassName::Member)
    class_static_items = []
    for full_key, cc in sorted(refl["class_constants"].items()):
        cls_name = cc["class"]
        const_name = cc["name"]
        doc = stub_docs.get(const_name, "")
        detail = f"{cls_name}::{const_name} = {cc['val']} ({cc['type']})"
        if doc:
            detail += f" — {doc}"
        class_static_items.append((cls_name, const_name, detail, 5))
        # Also expose PDO::* directly in global_items so searching PDO::ATTR_ERRMODE works globally too
        if cls_name in ("PDO", "DateTime", "DateTimeInterface") and full_key not in seen_global:
            seen_global.add(full_key)
            global_items.append((full_key, detail, 5))

    # 3. Class Methods (both instance -> and static ::)
    class_method_items = []
    for full_key, cm in sorted(refl["class_methods"].items()):
        cls_name = cm["class"]
        m_name = cm["name"]
        sig = cm["sig"]
        is_static = cm["static"]
        doc = stub_docs.get(m_name, "")
        detail = f"{cls_name}::{'::' if is_static else '->'}{m_name}{sig}".replace("::->", "->").replace("::::", "::")
        if doc:
            detail += f" — {doc}"
        class_method_items.append((cls_name, m_name, detail, 1 if is_static else 0))

    print(f"Total Generated Global PHP Symbols: {len(global_items)}")
    print(f"Total Generated Class Constants: {len(class_static_items)}")
    print(f"Total Generated Class Methods: {len(class_method_items)}")

    # Write header file
    hpp_path = REPO_ROOT / "include" / "phide" / "editor" / "PhpIntelliSenseData.hpp"
    hpp_content = """#pragma once

#include "phide/editor/IntelliSenseEngine.hpp"
#include <string>
#include <vector>

namespace phide::editor {

struct PhpClassConstantEntry {
    const wchar_t* className;
    const wchar_t* memberName;
    const wchar_t* detail;
};

struct PhpClassMethodEntry {
    const wchar_t* className;
    const wchar_t* methodName;
    const wchar_t* detail;
    bool isStatic;
};

/**
 * @brief Populates the global PHP IntelliSense symbol table generated from
 * Microsoft VS Code (php-language-features), JetBrains phpstorm-stubs, and PHP 8.5 Reflection.
 */
void PopulateBuiltInPhpIntelliSense(std::vector<IntelliSenseItem>& outSymbols);

/**
 * @brief Returns static constants and static methods for a given PHP class (e.g., "PDO", "DateTime") after `::`.
 */
std::vector<IntelliSenseItem> QueryPhpClassStaticMembers(const std::wstring& className);

/**
 * @brief Returns instance methods for a given PHP class (e.g., "PDO", "PDOStatement", "PDOException", "mysqli") after `->`.
 */
std::vector<IntelliSenseItem> QueryPhpClassInstanceMethods(const std::wstring& receiverClass);

} // namespace phide::editor
"""
    hpp_path.write_text(hpp_content, encoding="utf-8")

    # Write C++ source file
    cpp_path = REPO_ROOT / "src" / "editor" / "PhpIntelliSenseData.cpp"
    lines = [
        '// AUTO-GENERATED FROM VS CODE PHP-LANGUAGE-FEATURES + JETBRAINS PHPSTORM-STUBS + PHP 8.5 REFLECTION',
        '#include "phide/editor/PhpIntelliSenseData.hpp"',
        '#include <cwctype>',
        '#include <unordered_set>',
        '',
        'namespace phide::editor {',
        '',
        'namespace {',
        '',
        'struct RawPhpGlobal {',
        '    const wchar_t* name;',
        '    const wchar_t* desc;',
        '    int kindCode; // 0=Keyword, 1=Function, 2=Method, 3=Class, 4=Variable, 5=Constant',
        '};',
        '',
        'static const RawPhpGlobal kPhpGlobals[] = {'
    ]

    for name, detail, kcode in global_items:
        lines.append(f'    {{ L"{c_escape(name)}", L"{c_escape(detail)}", {kcode} }},')

    lines.extend([
        '};',
        '',
        'static const PhpClassConstantEntry kPhpClassConstants[] = {'
    ])

    for cls_name, const_name, detail, _ in class_static_items:
        lines.append(f'    {{ L"{c_escape(cls_name)}", L"{c_escape(const_name)}", L"{c_escape(detail)}" }},')

    lines.extend([
        '};',
        '',
        'static const PhpClassMethodEntry kPhpClassMethods[] = {'
    ])

    for cls_name, m_name, detail, is_static in class_method_items:
        lines.append(f'    {{ L"{c_escape(cls_name)}", L"{c_escape(m_name)}", L"{c_escape(detail)}", {"true" if is_static else "false"} }},')

    lines.extend([
        '};',
        '',
        'static bool EqualsIgnoreCase(const std::wstring& a, const wchar_t* b) {',
        '    size_t i = 0;',
        '    while (i < a.length() && b[i] != L\'\\0\') {',
        '        if (towlower(a[i]) != towlower(b[i])) return false;',
        '        ++i;',
        '    }',
        '    return i == a.length() && b[i] == L\'\\0\';',
        '}',
        '',
        '} // namespace',
        '',
        'void PopulateBuiltInPhpIntelliSense(std::vector<IntelliSenseItem>& outSymbols) {',
        '    outSymbols.reserve(outSymbols.size() + sizeof(kPhpGlobals) / sizeof(kPhpGlobals[0]));',
        '    for (const auto& g : kPhpGlobals) {',
        '        IntelliSenseItem item;',
        '        item.label = g.name;',
        '        item.detail = g.desc;',
        '        item.insertText = g.name;',
        '        switch (g.kindCode) {',
        '            case 0: item.kind = CompletionKind::Keyword; item.score = 300; break;',
        '            case 1: item.kind = CompletionKind::Function; item.score = 420; break;',
        '            case 2: item.kind = CompletionKind::Method; item.score = 400; break;',
        '            case 3: item.kind = CompletionKind::Class; item.score = 450; break;',
        '            case 4: item.kind = CompletionKind::Variable; item.score = 500; break;',
        '            case 5: item.kind = CompletionKind::Constant; item.score = 430; break;',
        '            default: item.kind = CompletionKind::Function; item.score = 350; break;',
        '        }',
        '        item.origin = L"vscode-php";',
        '        outSymbols.push_back(std::move(item));',
        '    }',
        '}',
        '',
        'std::vector<IntelliSenseItem> QueryPhpClassStaticMembers(const std::wstring& className) {',
        '    std::vector<IntelliSenseItem> results;',
        '    std::unordered_set<std::wstring> seen;',
        '    bool filterByClass = !className.empty();',
        '',
        '    for (const auto& cc : kPhpClassConstants) {',
        '        if (!filterByClass || EqualsIgnoreCase(className, cc.className)) {',
        '            if (seen.insert(cc.memberName).second) {',
        '                IntelliSenseItem item;',
        '                item.label = cc.memberName;',
        '                item.detail = cc.detail;',
        '                item.insertText = cc.memberName;',
        '                item.kind = CompletionKind::Constant;',
        '                item.score = 1800;',
        '                item.origin = L"vscode-php";',
        '                results.push_back(std::move(item));',
        '            }',
        '        }',
        '    }',
        '    for (const auto& cm : kPhpClassMethods) {',
        '        if (!cm.isStatic) continue;',
        '        if (!filterByClass || EqualsIgnoreCase(className, cm.className)) {',
        '            if (seen.insert(cm.methodName).second) {',
        '                IntelliSenseItem item;',
        '                item.label = cm.methodName;',
        '                item.detail = cm.detail;',
        '                item.insertText = cm.methodName;',
        '                item.kind = CompletionKind::Method;',
        '                item.score = 1750;',
        '                item.origin = L"vscode-php";',
        '                results.push_back(std::move(item));',
        '            }',
        '        }',
        '    }',
        '    return results;',
        '}',
        '',
        'std::vector<IntelliSenseItem> QueryPhpClassInstanceMethods(const std::wstring& receiverClass) {',
        '    std::vector<IntelliSenseItem> results;',
        '    std::unordered_set<std::wstring> seen;',
        '    bool hasReceiver = !receiverClass.empty();',
        '',
        '    // 1. Exact class match first (highest priority)',
        '    if (hasReceiver) {',
        '        for (const auto& cm : kPhpClassMethods) {',
        '            if (EqualsIgnoreCase(receiverClass, cm.className)) {',
        '                if (seen.insert(cm.methodName).second) {',
        '                    IntelliSenseItem item;',
        '                    item.label = cm.methodName;',
        '                    item.detail = cm.detail;',
        '                    item.insertText = cm.methodName;',
        '                    item.kind = CompletionKind::Method;',
        '                    item.score = 2000;',
        '                    item.origin = L"vscode-php";',
        '                    results.push_back(std::move(item));',
        '                }',
        '            }',
        '        }',
        '    }',
        '',
        '    // 2. Priority database/common classes (PDO, PDOStatement, PDOException, mysqli, mysqli_stmt, mysqli_result, DateTime, Exception)',
        '    static const wchar_t* kPriorityClasses[] = {',
        '        L"PDO", L"PDOStatement", L"PDOException", L"Exception", L"mysqli", L"mysqli_stmt", L"mysqli_result", L"DateTime"',
        '    };',
        '    for (const wchar_t* pCls : kPriorityClasses) {',
        '        for (const auto& cm : kPhpClassMethods) {',
        '            if (EqualsIgnoreCase(pCls, cm.className)) {',
        '                if (seen.insert(cm.methodName).second) {',
        '                    IntelliSenseItem item;',
        '                    item.label = cm.methodName;',
        '                    item.detail = cm.detail;',
        '                    item.insertText = cm.methodName;',
        '                    item.kind = CompletionKind::Method;',
        '                    item.score = hasReceiver ? 1400 : 1700;',
        '                    item.origin = L"vscode-php";',
        '                    results.push_back(std::move(item));',
        '                }',
        '            }',
        '        }',
        '    }',
        '',
        '    // 3. Remaining PHP class methods',
        '    for (const auto& cm : kPhpClassMethods) {',
        '        if (seen.insert(cm.methodName).second) {',
        '            IntelliSenseItem item;',
        '            item.label = cm.methodName;',
        '            item.detail = cm.detail;',
        '            item.insertText = cm.methodName;',
        '            item.kind = CompletionKind::Method;',
        '            item.score = 1200;',
        '            item.origin = L"vscode-php";',
        '            results.push_back(std::move(item));',
        '        }',
        '    }',
        '    return results;',
        '}',
        '',
        '} // namespace phide::editor',
        ''
    ])

    cpp_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {hpp_path} and {cpp_path} ({cpp_path.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
