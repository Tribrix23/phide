import json
import re
import os
from pathlib import Path

BASE_DIR = Path(__file__).parent
REPO_ROOT = BASE_DIR.parent.parent

def c_escape(s: str) -> str:
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\r", " ").replace("\n", " ").replace("\t", " ")
    s = re.sub(r"\s+", " ", s).strip()
    if len(s) > 220:
        s = s[:217] + "..."
    return "".join(ch if 32 <= ord(ch) < 127 else " " for ch in s)

def parse_react_dts(dts_path: Path):
    """Parse React TypeScript declaration file."""
    if not dts_path.exists():
        return {}
    content = dts_path.read_text(encoding="utf-8", errors="replace")
    
    symbols = {} # name -> (detail, kind)
    
    # 1. Match functions: function useState<...>(...): ...
    fn_pattern = re.compile(r"export\s+function\s+([A-Za-z0-9_$]+)\s*(<[^>]+>)?\s*\((.*?)\)\s*:\s*([^;]+);", re.DOTALL)
    for m in fn_pattern.finditer(content):
        fn_name = m.group(1)
        type_params = m.group(2) or ""
        params = re.sub(r"\s+", " ", m.group(3)).strip()
        ret = re.sub(r"\s+", " ", m.group(4)).strip()
        if len(params) > 70:
            params = params[:67] + "..."
        sig = f"{fn_name}{type_params}({params}): {ret}"
        symbols[fn_name] = (f"React Hook / API: {sig}", 2) # Function
        
    # 2. Match interfaces and type aliases
    type_pattern = re.compile(r"export\s+(?:interface|type)\s+([A-Za-z0-9_$]+)\s*(<[^>]+>)?", re.MULTILINE)
    for m in type_pattern.finditer(content):
        t_name = m.group(1)
        t_params = m.group(2) or ""
        if t_name not in symbols:
            symbols[t_name] = (f"React Type: {t_name}{t_params}", 22) # Class/Type
            
    # 3. Match JSX / HTML attributes & event handlers in HTMLAttributes & DOMAttributes
    # e.g.: onClick?: MouseEventHandler<T> | undefined;
    attr_pattern = re.compile(r"^\s*([A-Za-z0-9_$]+)\??:\s*([^;]+);", re.MULTILINE)
    for m in attr_pattern.finditer(content):
        attr_name = m.group(1)
        attr_type = re.sub(r"\s+", " ", m.group(2)).strip()
        if attr_name in ("type", "key", "ref", "children") or attr_name.startswith("on") or attr_name.startswith("aria-"):
            if len(attr_type) > 80:
                attr_type = attr_type[:77] + "..."
            symbols[attr_name] = (f"React JSX Prop: {attr_name}: {attr_type}", 0) # Property
            
    return symbols

def parse_react_snippets(json_path: Path):
    """Parse React snippets from JSON."""
    if not json_path.exists():
        return []
    try:
        data = json.loads(json_path.read_text(encoding="utf-8"))
    except Exception:
        return []
    snippets = []
    for name, item in data.items():
        if not isinstance(item, dict):
            continue
        prefix = item.get("prefix", "")
        desc = item.get("description", name)
        body = item.get("body", "")
        if isinstance(body, list):
            body = "\n".join(body)
        if prefix and body:
            snippets.append((prefix, desc, body))
    return snippets

def main():
    react_index = BASE_DIR / "react_index.d.ts"
    react_dom = BASE_DIR / "react_dom_index.d.ts"
    react_client = BASE_DIR / "react_dom_client.d.ts"
    react_global = BASE_DIR / "react_global.d.ts"
    snippets_json = BASE_DIR / "xabikos_react_snippets.json"
    
    all_symbols = {}
    
    for p in [react_index, react_dom, react_client, react_global]:
        syms = parse_react_dts(p)
        for k, v in syms.items():
            all_symbols[k] = v
            
    # Core React hooks with curated human-friendly signatures
    curated_hooks = [
        ("useState", "useState<T>(initialState: T | (() => T)): [T, Dispatch<SetStateAction<T>>]", 2),
        ("useEffect", "useEffect(effect: () => void | (() => void), deps?: DependencyList): void", 2),
        ("useContext", "useContext<T>(context: Context<T>): T", 2),
        ("useReducer", "useReducer<R>(reducer: R, initialArg: any, init?: any): [State, Dispatch]", 2),
        ("useCallback", "useCallback<T extends Function>(callback: T, deps: DependencyList): T", 2),
        ("useMemo", "useMemo<T>(factory: () => T, deps: DependencyList): T", 2),
        ("useRef", "useRef<T>(initialValue: T): MutableRefObject<T>", 2),
        ("useImperativeHandle", "useImperativeHandle<T, R extends T>(ref: Ref<T>, init: () => R, deps?: DependencyList): void", 2),
        ("useLayoutEffect", "useLayoutEffect(effect: () => void | (() => void), deps?: DependencyList): void", 2),
        ("useId", "useId(): string (Unique ID generator for accessibility attributes)", 2),
        ("useDeferredValue", "useDeferredValue<T>(value: T): T (Defers updating value until urgent renders finish)", 2),
        ("useTransition", "useTransition(): [boolean, TransitionStartFunction] (Concurrent transition hook)", 2),
        ("useSyncExternalStore", "useSyncExternalStore<Snapshot>(subscribe, getSnapshot): Snapshot", 2),
        ("useInsertionEffect", "useInsertionEffect(effect: () => void, deps?: DependencyList): void", 2),
        ("useActionState", "useActionState<State, Payload>(action, initialState): [State, (payload: Payload) => void, boolean]", 2),
        ("useOptimistic", "useOptimistic<Passthrough>(passthrough: Passthrough): [Passthrough, (action: any) => void]", 2),
        ("React", "namespace React: Core library for building interactive component-based user interfaces", 24),
        ("createRoot", "createRoot(container: Element | DocumentFragment): Root (React 18 Concurrent Root client entry)", 2),
        ("hydrateRoot", "hydrateRoot(container: Element, children: ReactNode): Root (Server-side hydration entry)", 2),
        ("createPortal", "createPortal(children: ReactNode, container: Element): ReactPortal", 2),
        ("memo", "memo<P>(Component: ComponentType<P>): MemoExoticComponent (High-order component memoizer)", 2),
        ("forwardRef", "forwardRef<T, P>(render: ForwardRefRenderFunction<T, P>): ForwardRefExoticComponent", 2),
        ("lazy", "lazy<T>(factory: () => Promise<{ default: T }>): LazyExoticComponent (Code splitting loader)", 2),
        ("startTransition", "startTransition(scope: TransitionFunction): void", 2),
        ("Fragment", "<Fragment>...</Fragment>: Groups child elements without adding extra DOM nodes", 22),
        ("Suspense", "<Suspense fallback={<Spinner />}>: Displays fallback UI while children are loading", 22),
        ("StrictMode", "<StrictMode>: Enables runtime checks and warnings during development", 22),
        ("Component", "class Component<P, S>: React base class for stateful class components", 22),
        ("PureComponent", "class PureComponent<P, S>: Component with shallow prop and state comparison", 22),
    ]
    for name, detail, kind in curated_hooks:
        all_symbols[name] = (detail, kind)
        
    snippets = parse_react_snippets(snippets_json)
    for pfx, desc, body in snippets:
        all_symbols[pfx] = (f"React Snippet ({pfx}): {desc}", 19) # Snippet
        
    print(f"Total React IntelliSense Symbols: {len(all_symbols)}")
    
    out_hpp = REPO_ROOT / "include" / "phide" / "editor" / "ReactIntelliSenseData.hpp"
    out_cpp = REPO_ROOT / "src" / "editor" / "ReactIntelliSenseData.cpp"
    
    hpp_content = """#pragma once

#include <string>
#include <vector>
#include "phide/editor/IntelliSenseEngine.hpp"

namespace phide::editor {

/**
 * @brief Retrieves the compiled official DefinitelyTyped @types/react & React snippets library.
 */
const std::vector<IntelliSenseItem>& GetReactGlobalSymbols();

} // namespace phide::editor
"""
    out_hpp.write_text(hpp_content, encoding="utf-8")
    
    cpp_lines = [
        '#include "phide/editor/ReactIntelliSenseData.hpp"',
        '',
        'namespace phide::editor {',
        '',
        'struct ReactSymbolDef {',
        '    const wchar_t* label;',
        '    const wchar_t* detail;',
        '    CompletionKind kind;',
        '};',
        '',
        f'static const ReactSymbolDef kReactGlobalSymbols[] = {{',
    ]
    
    for name, (detail, kind) in sorted(all_symbols.items()):
        cpp_lines.append(f'    {{ L"{c_escape(name)}", L"{c_escape(detail)}", static_cast<CompletionKind>({kind}) }},')
    cpp_lines.append('};')
    cpp_lines.append('')
    
    cpp_lines.append('''const std::vector<IntelliSenseItem>& GetReactGlobalSymbols() {
    static std::vector<IntelliSenseItem> s_symbols;
    if (s_symbols.empty()) {
        s_symbols.reserve(sizeof(kReactGlobalSymbols) / sizeof(kReactGlobalSymbols[0]));
        for (const auto& def : kReactGlobalSymbols) {
            IntelliSenseItem item;
            item.label = def.label;
            item.detail = def.detail;
            item.insertText = def.label;
            item.kind = def.kind;
            item.origin = L"react";
            item.score = 650;
            s_symbols.push_back(std::move(item));
        }
    }
    return s_symbols;
}

} // namespace phide::editor
''')
    
    out_cpp.write_text("\n".join(cpp_lines), encoding="utf-8")
    print(f"Wrote {out_hpp} and {out_cpp} ({os.path.getsize(out_cpp)} bytes)")

if __name__ == "__main__":
    main()
