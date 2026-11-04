import json
import re
import os
from pathlib import Path

BASE_DIR = Path(__file__).parent
REPO_ROOT = BASE_DIR.parent.parent

def c_escape(s: str) -> str:
    if not s:
        return 'L""'
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\r", " ").replace("\n", " ").replace("\t", " ")
    s = re.sub(r"\s+", " ", s).strip()
    if len(s) > 220:
        s = s[:217] + "..."
    cleaned = "".join(ch if 32 <= ord(ch) < 127 else " " for ch in s)
    return f'L"{cleaned}"'

def parse_react_dts(dts_path: Path):
    if not dts_path.exists():
        return {}, {}
    content = dts_path.read_text(encoding="utf-8", errors="replace")
    
    globals_map = {} # name -> (detail, kind, insert_text)
    jsx_attrs = {}   # name -> (detail, kind, insert_text, score)
    
    # 1. Match functions: export function useState<...>(...): ...
    fn_pattern = re.compile(r"export\s+function\s+([A-Za-z0-9_$]+)\s*(<[^>]+>)?\s*\((.*?)\)\s*:\s*([^;]+);", re.DOTALL)
    for m in fn_pattern.finditer(content):
        fn_name = m.group(1)
        type_params = m.group(2) or ""
        params = re.sub(r"\s+", " ", m.group(3)).strip()
        ret = re.sub(r"\s+", " ", m.group(4)).strip()
        if len(params) > 70:
            params = params[:67] + "..."
        sig = f"{fn_name}{type_params}({params}): {ret}"
        globals_map[fn_name] = (f"React Hook / API: {sig}", 2, f"{fn_name}($0)") # Function
        
    # 2. Match interfaces and type aliases
    type_pattern = re.compile(r"export\s+(?:interface|type)\s+([A-Za-z0-9_$]+)\s*(<[^>]+>)?", re.MULTILINE)
    for m in type_pattern.finditer(content):
        t_name = m.group(1)
        t_params = m.group(2) or ""
        if t_name not in globals_map:
            globals_map[t_name] = (f"React Type: {t_name}{t_params}", 22, t_name) # Class/Type
            
    # 3. Match JSX HTMLAttributes<T>
    match_html = re.search(r"interface HTMLAttributes<T>.*?\{(.*?)\n    \}", content, re.DOTALL)
    if match_html:
        for m in re.finditer(r"^\s*([A-Za-z0-9_$]+)\??:\s*([^;]+);", match_html.group(1), re.MULTILINE):
            name = m.group(1)
            t = re.sub(r"\s+", " ", m.group(2)).strip()
            score = 1500
            insert = f'{name}="$0"'
            if name == "className":
                score = 4500
                insert = 'className="$0"'
            elif name == "style":
                score = 3000
                insert = "style={{ $0 }}"
            elif name in ("key", "ref"):
                score = 2500
                insert = f"{name}={{$0}}"
            elif "boolean" in t:
                insert = name
            jsx_attrs[name] = (f"React JSX Prop: {name}: {t}", 0, insert, score)

    # 4. Match JSX DOMAttributes<T> (Events, dangerouslySetInnerHTML)
    match_dom = re.search(r"interface DOMAttributes<T>.*?\{(.*?)\n    \}", content, re.DOTALL)
    if match_dom:
        for m in re.finditer(r"^\s*([A-Za-z0-9_$]+)\??:\s*([^;]+);", match_dom.group(1), re.MULTILINE):
            name = m.group(1)
            t = re.sub(r"\s+", " ", m.group(2)).strip()
            score = 2000
            if name == "children":
                continue
            if name == "dangerouslySetInnerHTML":
                insert = "dangerouslySetInnerHTML={{ __html: $0 }}"
                score = 2200
            elif name.startswith("on"):
                insert = f"{name}={{(e) => $0}}"
                score = 2500
            else:
                insert = f"{name}={{$0}}"
            jsx_attrs[name] = (f"React JSX Event / Prop: {name}: {t}", 0, insert, score)

    # 5. Core common element attributes
    common_elem_attrs = [
        ("src", 'src="$0"', "Image, script, audio or video source URL", 2800),
        ("href", 'href="$0"', "Hyperlink target URL", 2800),
        ("alt", 'alt="$0"', "Alternative text for accessible image fallback", 2600),
        ("target", 'target="_blank"', "Browsing context for hyperlink navigation", 2200),
        ("rel", 'rel="noopener noreferrer"', "Relationship between current and linked resource", 2200),
        ("type", 'type="$0"', "Type of input control, button, or script", 2800),
        ("name", 'name="$0"', "Name of form control submitted with form data", 2500),
        ("value", 'value={$0}', "Current value of controlled form input", 2700),
        ("placeholder", 'placeholder="$0"', "Short hint displayed inside input before typing", 2600),
        ("disabled", "disabled", "Disables user interaction and form submission", 2400),
        ("readOnly", "readOnly", "Prevents user modification of input value", 2200),
        ("required", "required", "Specifies that input field must be filled out before submit", 2200),
        ("autoFocus", "autoFocus", "Automatically gives focus to element on render", 2200),
        ("tabIndex", "tabIndex={$0}", "Tab navigation order index", 2000),
        ("title", 'title="$0"', "Advisory tooltip text shown on hover", 2000),
        ("role", 'role="$0"', "WAI-ARIA accessibility role for screen readers", 2300),
        ("width", 'width="$0"', "Display width in pixels or percentage", 1800),
        ("height", 'height="$0"', "Display height in pixels or percentage", 1800),
        ("rows", "rows={$0}", "Number of visible text lines in textarea", 1800),
        ("cols", "cols={$0}", "Visible width of textarea in average character widths", 1800),
        ("action", 'action="$0"', "URL that processes form submission", 2000),
        ("method", 'method="post"', "HTTP method used to send form data", 2000),
        ("encType", 'encType="multipart/form-data"', "MIME type used to submit form to server", 1900),
        ("autoComplete", 'autoComplete="off"', "Browser autofill assistance control", 1900),
        ("id", 'id="$0"', "Unique identifier for element in document DOM", 3000),
    ]
    for attr, ins, desc, sc in common_elem_attrs:
        if attr not in jsx_attrs or attr in ("className", "id", "src", "href", "type", "value", "placeholder"):
            jsx_attrs[attr] = (f"React JSX Prop: {attr} - {desc}", 0, ins, sc)

    # Ensure className is top priority
    jsx_attrs["className"] = ("React JSX class attribute (replaces HTML 'class')", 0, 'className="$0"', 4500)

    return globals_map, jsx_attrs

def parse_react_snippets(json_path: Path):
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
    react_dts = BASE_DIR / "node_modules" / "@types" / "react" / "index.d.ts"
    react_dom_dts = BASE_DIR / "node_modules" / "@types" / "react-dom" / "index.d.ts"
    react_dom_client = BASE_DIR / "node_modules" / "@types" / "react-dom" / "client.d.ts"
    snippets_json = BASE_DIR / "xabikos_react_snippets.json"
    
    all_globals = {}
    all_jsx_attrs = {}
    
    for p in [react_dts, react_dom_dts, react_dom_client]:
        g, a = parse_react_dts(p)
        all_globals.update(g)
        all_jsx_attrs.update(a)
        
    # Curated hooks
    curated_hooks = [
        ("useState", "useState<T>(initialState: T | (() => T)): [T, Dispatch<SetStateAction<T>>]", 2, "const [${1:state}, set${1/(.*)/${1:/capitalize}/}] = useState(${2:initialState});"),
        ("useEffect", "useEffect(effect: () => void | (() => void), deps?: DependencyList): void", 2, "useEffect(() => {\n    $0\n}, [${1}]);"),
        ("useContext", "useContext<T>(context: Context<T>): T", 2, "const ${1:value} = useContext(${2:MyContext});"),
        ("useReducer", "useReducer<R>(reducer: R, initialArg: any, init?: any): [State, Dispatch]", 2, "const [state, dispatch] = useReducer(${1:reducer}, ${2:initialState});"),
        ("useCallback", "useCallback<T extends Function>(callback: T, deps: DependencyList): T", 2, "const ${1:memoizedCallback} = useCallback(\n    () => {\n        $0\n    },\n    [${2}],\n);"),
        ("useMemo", "useMemo<T>(factory: () => T, deps: DependencyList): T", 2, "const ${1:memoizedValue} = useMemo(() => ${2:computeExpensiveValue(a, b)}, [${3}]);"),
        ("useRef", "useRef<T>(initialValue: T): MutableRefObject<T>", 2, "const ${1:myRef} = useRef(${2:null});"),
        ("useImperativeHandle", "useImperativeHandle<T, R>(ref: Ref<T>, init: () => R, deps?: DependencyList): void", 2, "useImperativeHandle(${1:ref}, () => ({\n    $0\n}));"),
        ("useLayoutEffect", "useLayoutEffect(effect: EffectCallback, deps?: DependencyList): void", 2, "useLayoutEffect(() => {\n    $0\n}, [${1}]);"),
        ("useId", "useId(): string", 2, "const ${1:id} = useId();"),
        ("useTransition", "useTransition(): [boolean, TransitionStartFunction]", 2, "const [isPending, startTransition] = useTransition();"),
        ("useDeferredValue", "useDeferredValue<T>(value: T): T", 2, "const ${1:deferredValue} = useDeferredValue(${2:value});"),
    ]
    for name, sig, kind, ins in curated_hooks:
        all_globals[name] = (f"React Hook: {sig}", kind, ins)

    # Curated core APIs
    curated_apis = [
        ("Component", "class Component<P, S> { render(): ReactNode; }", 22, "Component"),
        ("PureComponent", "class PureComponent<P, S> { render(): ReactNode; }", 22, "PureComponent"),
        ("memo", "function memo<P>(Component: ComponentType<P>): NamedExoticComponent<P>", 2, "memo(${1:Component})"),
        ("forwardRef", "function forwardRef<T, P>(render: ForwardRefRenderFunction<T, P>): ForwardRefExoticComponent<P>", 2, "forwardRef<${1:HTMLDivElement}, ${2:Props}>((props, ref) => {\n    return <div ref={ref} {...props} />;\n})"),
        ("lazy", "function lazy<T extends ComponentType<any>>(factory: () => Promise<{ default: T }>): LazyExoticComponent<T>", 2, "lazy(() => import('${1:./Component}'))"),
        ("createContext", "function createContext<T>(defaultValue: T): Context<T>", 2, "createContext(${1:defaultValue})"),
        ("Fragment", "React.Fragment (<>...</>)", 22, "Fragment"),
        ("Suspense", "<Suspense fallback={<Loading />}>...</Suspense>", 22, "<Suspense fallback={${1:<Loading />}}>\n    $0\n</Suspense>"),
        ("StrictMode", "<StrictMode>...</StrictMode>", 22, "<StrictMode>\n    $0\n</StrictMode>"),
        ("createRoot", "function createRoot(container: Element | DocumentFragment): Root", 2, "createRoot(document.getElementById('${1:root}')!).render(<${2:App} />);"),
        ("hydrateRoot", "function hydrateRoot(container: Element | Document, children: ReactNode): Root", 2, "hydrateRoot(document.getElementById('${1:root}')!, <${2:App} />);"),
        ("createPortal", "function createPortal(children: ReactNode, container: Element): ReactPortal", 2, "createPortal($0, document.body)"),
    ]
    for name, sig, kind, ins in curated_apis:
        all_globals[name] = (f"React API: {sig}", kind, ins)

    # Snippets
    snippets = parse_react_snippets(snippets_json)
    
    # Generate Header
    hpp_path = REPO_ROOT / "include" / "phide" / "editor" / "ReactIntelliSenseData.hpp"
    hpp_content = """#pragma once

#include <string>
#include <vector>
#include "phide/editor/IntelliSenseEngine.hpp"

namespace phide::editor {

/**
 * @brief Retrieves the compiled official DefinitelyTyped @types/react & React snippets library.
 */
const std::vector<IntelliSenseItem>& GetReactGlobalSymbols();

/**
 * @brief Retrieves the compiled official React JSX attributes (e.g. className, style, onClick, etc.).
 */
const std::vector<IntelliSenseItem>& GetReactJsxAttributes();

} // namespace phide::editor
"""
    hpp_path.write_text(hpp_content, encoding="utf-8")
    print(f"Wrote {hpp_path}")

    # Generate Implementation
    cpp_path = REPO_ROOT / "src" / "editor" / "ReactIntelliSenseData.cpp"
    lines = [
        '#include "phide/editor/ReactIntelliSenseData.hpp"',
        "",
        "namespace phide::editor {",
        "",
        "const std::vector<IntelliSenseItem>& GetReactGlobalSymbols() {",
        "    static const std::vector<IntelliSenseItem> s_reactGlobals = {",
    ]
    
    # 1. Global symbols (Hooks, APIs, Types)
    for name, (detail, kind, ins) in sorted(all_globals.items()):
        kind_str = "CompletionKind::Function" if kind == 2 else ("CompletionKind::Class" if kind == 22 else "CompletionKind::Variable")
        lines.append(f'        {{ {c_escape(name)}, {c_escape(detail)}, {c_escape(ins)}, {kind_str}, 1200, 0, 0, L"react", {c_escape(detail)} }},')
        
    # 2. Snippets
    for prefix, desc, body in snippets:
        lines.append(f'        {{ {c_escape(prefix)}, {c_escape(desc)}, {c_escape(body)}, CompletionKind::Snippet, 1600, 0, 0, L"react-snippets", {c_escape(desc)} }},')
        
    lines.extend([
        "    };",
        "    return s_reactGlobals;",
        "}",
        "",
        "const std::vector<IntelliSenseItem>& GetReactJsxAttributes() {",
        "    static const std::vector<IntelliSenseItem> s_jsxAttrs = {",
    ])
    
    # 3. JSX Attributes
    for name, (detail, kind, ins, score) in sorted(all_jsx_attrs.items(), key=lambda x: -x[1][3]):
        lines.append(f'        {{ {c_escape(name)}, {c_escape(detail)}, {c_escape(ins)}, CompletionKind::Property, {score}, 0, 0, L"react-jsx", {c_escape(detail)} }},')
        
    lines.extend([
        "    };",
        "    return s_jsxAttrs;",
        "}",
        "",
        "} // namespace phide::editor",
        ""
    ])
    
    cpp_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {cpp_path} ({len(all_globals)} globals, {len(all_jsx_attrs)} JSX attributes, {len(snippets)} snippets)")

if __name__ == "__main__":
    main()
