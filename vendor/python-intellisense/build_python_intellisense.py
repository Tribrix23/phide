import inspect
import sys
import os
import re
import json
from pathlib import Path

BASE_DIR = Path(__file__).parent
REPO_ROOT = BASE_DIR.parent.parent

def c_escape(s: str) -> str:
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\r", " ").replace("\n", " ").replace("\t", " ")
    s = re.sub(r"\s+", " ", s).strip()
    if len(s) > 220:
        s = s[:217] + "..."
    return "".join(ch if 32 <= ord(ch) < 127 else " " for ch in s)

def extract_from_pyi(pyi_path: Path, module_name: str = ""):
    """Extract functions, classes, and methods from typeshed pyi."""
    if not pyi_path.exists():
        return [], {}
    content = pyi_path.read_text(encoding="utf-8", errors="replace")
    
    globals_list = []
    class_members = {} # class_name -> list of (member_name, sig, kind)
    
    current_class = None
    
    for line in content.splitlines():
        line_strip = line.strip()
        if not line_strip or line_strip.startswith("#"):
            continue
            
        # Class def
        m_cls = re.match(r"^class\s+([A-Za-z_][A-Za-z0-9_]*)(?:\((.*?)\))?:", line)
        if m_cls:
            cls_name = m_cls.group(1)
            bases = m_cls.group(2) or ""
            current_class = cls_name
            class_members[cls_name] = []
            prefix = f"{module_name}." if module_name and module_name != "builtins" else ""
            globals_list.append((f"{prefix}{cls_name}", f"class {cls_name}({bases})", 22)) # Class
            continue
            
        # Function def (top-level or in class)
        m_fn = re.match(r"^(\s*)def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*?)\)(?:\s*->\s*(.*?))?:", line)
        if m_fn:
            indent = len(m_fn.group(1))
            fn_name = m_fn.group(2)
            params = m_fn.group(3)
            ret = m_fn.group(4) or "None"
            sig = f"({params}) -> {ret}"
            if len(sig) > 120:
                sig = f"({params[:60]}...) -> {ret}"
                
            if indent > 0 and current_class:
                class_members[current_class].append((fn_name, f"def {fn_name}{sig}", 1)) # Method
            else:
                current_class = None
                prefix = f"{module_name}." if module_name and module_name != "builtins" else ""
                globals_list.append((f"{prefix}{fn_name}", f"def {fn_name}{sig}", 2)) # Function
            continue
            
        if not line.startswith(" ") and not line.startswith("\t"):
            current_class = None
            # Constant / variable
            m_var = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([^=]+)", line)
            if m_var:
                v_name = m_var.group(1)
                v_type = m_var.group(2).strip()
                prefix = f"{module_name}." if module_name and module_name != "builtins" else ""
                globals_list.append((f"{prefix}{v_name}", f"{v_name}: {v_type}", 23)) # Constant
                
    return globals_list, class_members

def extract_from_live_python():
    """Extract live symbols from Python runtime."""
    import builtins
    import math, os, sys, json, re, datetime, pathlib, subprocess, random, itertools, functools, typing
    
    live_globals = []
    module_members = {} # mod_name -> list of (name, detail, kind)
    
    # Builtins
    for name in dir(builtins):
        if name.startswith("__") and not name.endswith("__"):
            continue
        obj = getattr(builtins, name)
        doc = (inspect.getdoc(obj) or "").split("\n")[0]
        try:
            sig = str(inspect.signature(obj))
        except Exception:
            sig = "()"
            
        if inspect.isclass(obj):
            kind = 22 # Class
            detail = f"class {name}{sig} - {doc}"
        elif callable(obj):
            kind = 2 # Function
            detail = f"{name}{sig} -> built-in - {doc}"
        else:
            kind = 23 # Constant
            detail = f"built-in constant {name} - {doc}"
            
        live_globals.append((name, detail[:140], kind))
        
    # Standard library modules
    modules = {
        "os": os, "sys": sys, "json": json, "math": math, "re": re,
        "datetime": datetime, "pathlib": pathlib, "subprocess": subprocess,
        "random": random, "itertools": itertools, "functools": functools,
        "typing": typing
    }
    
    for mod_name, mod in modules.items():
        module_members[mod_name] = []
        mod_doc = (inspect.getdoc(mod) or "").split("\n")[0]
        live_globals.append((mod_name, f"module {mod_name} - {mod_doc}", 24)) # Module
        
        for attr in dir(mod):
            if attr.startswith("_"):
                continue
            try:
                val = getattr(mod, attr)
            except Exception:
                continue
            v_doc = (inspect.getdoc(val) or "").split("\n")[0]
            try:
                sig = str(inspect.signature(val))
            except Exception:
                sig = "()"
            if inspect.isclass(val):
                kind = 22
                d = f"class {mod_name}.{attr}{sig} - {v_doc}"
            elif callable(val):
                kind = 2
                d = f"{mod_name}.{attr}{sig} - {v_doc}"
            else:
                kind = 23
                d = f"{mod_name}.{attr} - {v_doc}"
            module_members[mod_name].append((attr, d[:140], kind))
            live_globals.append((f"{mod_name}.{attr}", d[:140], kind))
            
    return live_globals, module_members

def main():
    pyi_files = [
        (BASE_DIR / "builtins.pyi", "builtins"),
        (BASE_DIR / "os.pyi", "os"),
        (BASE_DIR / "sys.pyi", "sys"),
        (BASE_DIR / "json.pyi", "json"),
        (BASE_DIR / "math.pyi", "math"),
        (BASE_DIR / "typing.pyi", "typing"),
        (BASE_DIR / "collections.pyi", "collections"),
        (BASE_DIR / "asyncio.pyi", "asyncio"),
        (BASE_DIR / "re.pyi", "re"),
        (BASE_DIR / "datetime.pyi", "datetime"),
        (BASE_DIR / "pathlib.pyi", "pathlib"),
        (BASE_DIR / "subprocess.pyi", "subprocess"),
        (BASE_DIR / "random.pyi", "random"),
        (BASE_DIR / "itertools.pyi", "itertools"),
        (BASE_DIR / "functools.pyi", "functools"),
    ]
    
    all_globals = {} # name -> (detail, kind)
    all_module_members = {} # mod -> { member -> (detail, kind) }
    
    # 1. Extract typeshed
    for pf, mod in pyi_files:
        g_list, cls_m = extract_from_pyi(pf, mod)
        for name, detail, kind in g_list:
            all_globals[name] = (detail, kind)
        for cls_name, mems in cls_m.items():
            if mod not in all_module_members:
                all_module_members[mod] = {}
            for m_name, m_detail, m_kind in mems:
                all_module_members[mod][m_name] = (m_detail, m_kind)
                
    # 2. Extract live Python
    live_g, live_mod = extract_from_live_python()
    for name, detail, kind in live_g:
        if name not in all_globals or len(detail) > len(all_globals[name][0]):
            all_globals[name] = (detail, kind)
    for mod, mems in live_mod.items():
        if mod not in all_module_members:
            all_module_members[mod] = {}
        for m_name, m_detail, m_kind in mems:
            all_module_members[mod][m_name] = (m_detail, m_kind)

    # Python keywords
    keywords = [
        ("def", "Function definition statement", 16),
        ("class", "Class definition statement", 16),
        ("return", "Return value from function or generator", 16),
        ("yield", "Yield expression producing next value in generator", 16),
        ("import", "Import module or package into current namespace", 16),
        ("from", "Import specific symbols from module or package", 16),
        ("as", "Aliasing keyword for imports and with statements", 16),
        ("if", "Conditional branching statement", 16),
        ("elif", "Else-if conditional branch statement", 16),
        ("else", "Fallback conditional branch statement", 16),
        ("while", "Looping statement while condition is truthy", 16),
        ("for", "Looping statement over elements of iterable", 16),
        ("in", "Membership test operator or sequence iteration target", 16),
        ("try", "Exception handling block start", 16),
        ("except", "Exception handler clause catching specified errors", 16),
        ("finally", "Cleanup block always executed upon leaving try block", 16),
        ("with", "Context manager entry block (invokes __enter__/__exit__)", 16),
        ("async", "Coroutine function definition keyword (async def)", 16),
        ("await", "Pauses coroutine execution until awaitable completes", 16),
        ("pass", "Null operation statement doing nothing", 16),
        ("break", "Terminates nearest enclosing loop", 16),
        ("continue", "Continues to next iteration of nearest enclosing loop", 16),
        ("lambda", "Anonymous inline function expression", 16),
        ("global", "Declares variable to reference global module scope", 16),
        ("nonlocal", "Declares variable to reference enclosing non-global scope", 16),
        ("assert", "Tests condition; raises AssertionError if false", 16),
        ("del", "Deletes reference to variable, item, or slice", 16),
        ("is", "Object identity operator comparing memory references", 16),
        ("not", "Logical NOT boolean inversion operator", 16),
        ("and", "Logical AND short-circuit conjunction operator", 16),
        ("or", "Logical OR short-circuit disjunction operator", 16),
        ("True", "Boolean true singleton literal", 16),
        ("False", "Boolean false singleton literal", 16),
        ("None", "NoneType singleton representing absence of value", 16),
        ("match", "Structural pattern matching statement (Python 3.10+)", 16),
        ("case", "Pattern case arm in match statement (Python 3.10+)", 16),
    ]
    for kw, desc, kind in keywords:
        all_globals[kw] = (desc, kind)

    print(f"Total Python Global Symbols: {len(all_globals)}")
    
    out_hpp = REPO_ROOT / "include" / "phide" / "editor" / "PythonIntelliSenseData.hpp"
    out_cpp = REPO_ROOT / "src" / "editor" / "PythonIntelliSenseData.cpp"
    
    hpp_content = """#pragma once

#include <string>
#include <vector>
#include "phide/editor/IntelliSenseEngine.hpp"

namespace phide::editor {

/**
 * @brief Retrieves the compiled official VS Code & Typeshed Python symbol library.
 */
const std::vector<IntelliSenseItem>& GetPythonGlobalSymbols();

/**
 * @brief Retrieves member functions and attributes of a standard Python module (e.g. "os", "sys", "json", "math").
 */
std::vector<IntelliSenseItem> QueryPythonModuleMembers(const std::wstring& moduleName);

} // namespace phide::editor
"""
    out_hpp.write_text(hpp_content, encoding="utf-8")
    
    cpp_lines = [
        '#include "phide/editor/PythonIntelliSenseData.hpp"',
        '#include <unordered_map>',
        '',
        'namespace phide::editor {',
        '',
        'struct PySymbolDef {',
        '    const wchar_t* label;',
        '    const wchar_t* detail;',
        '    CompletionKind kind;',
        '};',
        '',
        f'static const PySymbolDef kPyGlobalSymbols[] = {{',
    ]
    
    for name, (detail, kind) in sorted(all_globals.items()):
        cpp_lines.append(f'    {{ L"{c_escape(name)}", L"{c_escape(detail)}", static_cast<CompletionKind>({kind}) }},')
    cpp_lines.append('};')
    cpp_lines.append('')
    
    # Module members
    cpp_lines.append('static const PySymbolDef kPyModuleMembers[] = {')
    for mod, mems in sorted(all_module_members.items()):
        for m_name, (m_detail, m_kind) in sorted(mems.items()):
            cpp_lines.append(f'    {{ L"{c_escape(mod)}.{c_escape(m_name)}", L"{c_escape(m_detail)}", static_cast<CompletionKind>({m_kind}) }},')
    cpp_lines.append('};')
    cpp_lines.append('')
    
    cpp_lines.append('''const std::vector<IntelliSenseItem>& GetPythonGlobalSymbols() {
    static std::vector<IntelliSenseItem> s_symbols;
    if (s_symbols.empty()) {
        s_symbols.reserve(sizeof(kPyGlobalSymbols) / sizeof(kPyGlobalSymbols[0]));
        for (const auto& def : kPyGlobalSymbols) {
            IntelliSenseItem item;
            item.label = def.label;
            item.detail = def.detail;
            item.insertText = def.label;
            item.kind = def.kind;
            item.origin = L"python";
            item.score = 500;
            s_symbols.push_back(std::move(item));
        }
    }
    return s_symbols;
}

std::vector<IntelliSenseItem> QueryPythonModuleMembers(const std::wstring& moduleName) {
    std::vector<IntelliSenseItem> results;
    std::wstring prefix = moduleName + L".";
    for (const auto& def : kPyModuleMembers) {
        if (wcsncmp(def.label, prefix.c_str(), prefix.length()) == 0) {
            IntelliSenseItem item;
            item.label = def.label + prefix.length(); // strip module prefix
            item.detail = def.detail;
            item.insertText = item.label;
            item.kind = def.kind;
            item.origin = L"python";
            item.score = 800;
            results.push_back(std::move(item));
        }
    }
    return results;
}

} // namespace phide::editor
''')
    
    out_cpp.write_text("\n".join(cpp_lines), encoding="utf-8")
    print(f"Wrote {out_hpp} and {out_cpp} ({os.path.getsize(out_cpp)} bytes)")

if __name__ == "__main__":
    main()
