#include "phide/editor/IntelliSenseEngine.hpp"
#include <algorithm>
#include <cwctype>

namespace phide::editor {

IntelliSenseEngine::IntelliSenseEngine() {
    PopulateHtmlTags();
    PopulateHtmlAttributes();
    PopulateJavaScriptKeywords();
    PopulateJavaScriptGlobals();
    PopulateJavaScriptDomApis();
    PopulateJavaScriptPrototypeMethods();
}

void IntelliSenseEngine::PopulateHtmlTags() {
    // Standard HTML5 tags (all 120+ elements)
    struct TagDef { const wchar_t* name; const wchar_t* desc; };
    TagDef tags[] = {
        { L"a", L"Anchor hyperlink element" },
        { L"abbr", L"Abbreviation or acronym" },
        { L"address", L"Contact information for author/owner" },
        { L"area", L"Hyperlink area within an image map" },
        { L"article", L"Self-contained composition in document" },
        { L"aside", L"Indirectly related sidebar content" },
        { L"audio", L"Sound or audio stream container" },
        { L"b", L"Bring attention to text (bold)" },
        { L"base", L"Base URL for all relative URLs" },
        { L"bdi", L"Bidirectional isolation element" },
        { L"bdo", L"Bidirectional override element" },
        { L"blockquote", L"Extended block quotation" },
        { L"body", L"Document body content" },
        { L"br", L"Line break" },
        { L"button", L"Clickable button control" },
        { L"canvas", L"Bitmap graphics canvas API" },
        { L"caption", L"Table title or caption" },
        { L"cite", L"Title of a work or reference" },
        { L"code", L"Inline code fragment" },
        { L"col", L"Column within a table column group" },
        { L"colgroup", L"Group of columns within a table" },
        { L"data", L"Machine-readable translation of content" },
        { L"datalist", L"Set of predefined options for input" },
        { L"dd", L"Description details element" },
        { L"del", L"Deleted text representation" },
        { L"details", L"Disclosure widget for extra info" },
        { L"dfn", L"Defining instance of a term" },
        { L"dialog", L"Dialog box or subwindow modal" },
        { L"div", L"Generic block-level division container" },
        { L"dl", L"Description list container" },
        { L"dt", L"Description term element" },
        { L"em", L"Emphasized text" },
        { L"embed", L"External application integration container" },
        { L"fieldset", L"Group of form controls with legend" },
        { L"figcaption", L"Caption or legend for a figure" },
        { L"figure", L"Self-contained illustrative figure" },
        { L"footer", L"Footer for nearest section or page" },
        { L"form", L"Interactive form container for user input" },
        { L"h1", L"Top level section heading" },
        { L"h2", L"Second level section heading" },
        { L"h3", L"Third level section heading" },
        { L"h4", L"Fourth level section heading" },
        { L"h5", L"Fifth level section heading" },
        { L"h6", L"Sixth level section heading" },
        { L"head", L"Machine-readable metadata container" },
        { L"header", L"Introductory content or navigation banner" },
        { L"hgroup", L"Heading and subheading multi-level group" },
        { L"hr", L"Thematic break or horizontal rule" },
        { L"html", L"Root element of an HTML document" },
        { L"i", L"Idiomatic text or icon container" },
        { L"iframe", L"Nested browsing context / inline frame" },
        { L"img", L"Image embed element" },
        { L"input", L"Interactive data input control" },
        { L"ins", L"Inserted text representation" },
        { L"kbd", L"User keyboard input representation" },
        { L"label", L"Caption for an input form control" },
        { L"legend", L"Caption for a fieldset control" },
        { L"li", L"List item element" },
        { L"link", L"External resource stylesheet / icon link" },
        { L"main", L"Dominant central content of document body" },
        { L"map", L"Client-side image map container" },
        { L"mark", L"Highlighted or marked text for reference" },
        { L"meta", L"Document-level metadata representation" },
        { L"meter", L"Scalar measurement within known range" },
        { L"nav", L"Navigation section containing links" },
        { L"noscript", L"Fallback content when script is disabled" },
        { L"object", L"External multi-media or plugin resource" },
        { L"ol", L"Ordered enumerated list container" },
        { L"optgroup", L"Group of options within select dropdown" },
        { L"option", L"Item in a select dropdown or datalist" },
        { L"output", L"Result of calculation or user action" },
        { L"p", L"Paragraph text block" },
        { L"picture", L"Responsive image source container" },
        { L"pre", L"Preformatted text with preserved whitespace" },
        { L"progress", L"Completion progress indicator bar" },
        { L"q", L"Inline quotation" },
        { L"rp", L"Parentheses fallback for ruby annotation" },
        { L"rt", L"Ruby pronunciation annotation text" },
        { L"ruby", L"Ruby pronunciation container" },
        { L"s", L"Strikethrough or non-accurate text" },
        { L"samp", L"Sample output from computer program" },
        { L"script", L"Embedded or external client-side script" },
        { L"search", L"Search and filtering section container" },
        { L"section", L"Generic standalone thematic section" },
        { L"select", L"Dropdown selection control menu" },
        { L"slot", L"Web Component placeholder slot" },
        { L"small", L"Side comment or fine print" },
        { L"source", L"Media resource for picture, audio or video" },
        { L"span", L"Generic inline styling container" },
        { L"strong", L"Strong importance or urgency" },
        { L"style", L"Document-level CSS stylesheet rules" },
        { L"sub", L"Subscript typography" },
        { L"summary", L"Summary or disclosure heading for details" },
        { L"sup", L"Superscript typography" },
        { L"svg", L"Scalable Vector Graphics container" },
        { L"table", L"Tabular data grid table representation" },
        { L"tbody", L"Body content row group within a table" },
        { L"td", L"Table data cell" },
        { L"template", L"Declared HTML fragment for client cloning" },
        { L"textarea", L"Multi-line plain text editing control" },
        { L"tfoot", L"Footer row group within a table" },
        { L"th", L"Table header cell" },
        { L"thead", L"Header row group within a table" },
        { L"time", L"Specific date, time, or duration value" },
        { L"title", L"Document title displayed in browser tab" },
        { L"tr", L"Table row container" },
        { L"track", L"Timed text track subtitles for media" },
        { L"u", L"Unarticulated annotation or underline" },
        { L"ul", L"Unordered bulleted list container" },
        { L"var", L"Mathematical or programming variable" },
        { L"video", L"Movie clip or video stream playback" },
        { L"wbr", L"Word break opportunity" }
    };

    for (const auto& t : tags) {
        IntelliSenseItem item;
        item.label = t.name;
        item.detail = t.desc;
        item.insertText = t.name;
        item.kind = CompletionKind::Tag;
        m_htmlTags.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateHtmlAttributes() {
    struct AttrDef { const wchar_t* name; const wchar_t* desc; };
    AttrDef attrs[] = {
        { L"class", L"Space-separated list of CSS class names" },
        { L"id", L"Unique document-wide identifier" },
        { L"style", L"Inline CSS styling rules" },
        { L"href", L"Target URL or destination anchor" },
        { L"src", L"Source URL of external resource or image" },
        { L"type", L"Type of control, media or script" },
        { L"name", L"Form control name sent with HTTP request" },
        { L"value", L"Default or current value of form control" },
        { L"placeholder", L"Hint text displayed when input is empty" },
        { L"alt", L"Alternative text representation for image" },
        { L"title", L"Advisory information displayed as tooltip" },
        { L"target", L"Browsing context (_blank, _self, _parent)" },
        { L"rel", L"Relationship between current and linked resource" },
        { L"disabled", L"Disables user interaction with form control" },
        { L"required", L"Specifies that input field must be filled" },
        { L"readonly", L"Specifies that input cannot be modified" },
        { L"checked", L"Pre-selects radio button or checkbox" },
        { L"selected", L"Pre-selects option in select dropdown" },
        { L"multiple", L"Allows multiple selections or file uploads" },
        { L"autofocus", L"Automatically focuses control on page load" },
        { L"autocomplete", L"Controls browser autofill behavior" },
        { L"width", L"Horizontal dimension in pixels or ratio" },
        { L"height", L"Vertical dimension in pixels or ratio" },
        { L"loading", L"Image/iframe loading strategy (lazy, eager)" },
        { L"role", L"WAI-ARIA accessible component role" },
        { L"tabindex", L"Keyboard focus tab navigation order" },
        { L"hidden", L"Hides element from rendering and accessibility" },
        { L"draggable", L"Enables drag-and-drop interaction" },
        { L"contenteditable", L"Allows user to edit element contents" },
        { L"onclick", L"Executed when mouse clicks the element" },
        { L"onchange", L"Executed when form control value commits change" },
        { L"oninput", L"Executed immediately on user value input" },
        { L"onsubmit", L"Executed when form is submitted" },
        { L"onkeydown", L"Executed when keyboard key is depressed" },
        { L"onkeyup", L"Executed when keyboard key is released" },
        { L"onload", L"Executed after element finishes loading" },
        { L"onerror", L"Executed when resource fails to load" },
        { L"aria-label", L"Defines string value that labels current element" },
        { L"aria-hidden", L"Hides element from assistive screen readers" },
        { L"aria-expanded", L"Indicates if grouping element is expanded" }
    };

    for (const auto& a : attrs) {
        IntelliSenseItem item;
        item.label = a.name;
        item.detail = a.desc;
        item.insertText = std::wstring(a.name) + L"=\"$0\"";
        item.kind = CompletionKind::Attribute;
        m_htmlAttributes.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateJavaScriptKeywords() {
    const wchar_t* keywords[] = {
        L"async", L"await", L"break", L"case", L"catch", L"class", L"const", L"continue",
        L"debugger", L"default", L"delete", L"do", L"else", L"export", L"extends",
        L"finally", L"for", L"function", L"if", L"import", L"in", L"instanceof",
        L"let", L"new", L"return", L"super", L"switch", L"this", L"throw", L"try",
        L"typeof", L"var", L"void", L"while", L"with", L"yield"
    };
    for (const auto* kw : keywords) {
        IntelliSenseItem item;
        item.label = kw;
        item.detail = L"JavaScript Keyword";
        item.insertText = kw;
        item.kind = CompletionKind::Keyword;
        m_jsKeywords.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateJavaScriptGlobals() {
    struct GlobalDef { const wchar_t* name; const wchar_t* desc; };
    GlobalDef globals[] = {
        { L"document", L"DOM Document object interface for page structure" },
        { L"window", L"Global browser window and execution context" },
        { L"console", L"Console debugging logging utility" },
        { L"fetch", L"Promise-based asynchronous HTTP network request" },
        { L"JSON", L"JavaScript Object Notation serialization utility" },
        { L"Math", L"Mathematical constants and functions library" },
        { L"Promise", L"Asynchronous operation eventual result representation" },
        { L"Object", L"JavaScript foundational object constructor" },
        { L"Array", L"Ordered collection constructor and static methods" },
        { L"String", L"String primitive wrapper and utility methods" },
        { L"Number", L"Numeric representation and precision methods" },
        { L"Boolean", L"Boolean truth value constructor" },
        { L"Date", L"Date and time calendar timestamp constructor" },
        { L"RegExp", L"Regular expression pattern matching constructor" },
        { L"Map", L"Key-value collection supporting arbitrary keys" },
        { L"Set", L"Collection of unique primitive values or references" },
        { L"setTimeout", L"Schedules callback execution after millisecond delay" },
        { L"setInterval", L"Repeats callback execution at fixed intervals" },
        { L"clearTimeout", L"Cancels previously scheduled timeout timer" },
        { L"clearInterval", L"Cancels previously scheduled interval timer" },
        { L"localStorage", L"Persistent web storage surviving browser restart" },
        { L"sessionStorage", L"Session web storage cleared on tab close" },
        { L"navigator", L"User agent, browser state, and platform info" },
        { L"location", L"Current document URL and navigation interface" },
        { L"history", L"Browser session history navigation stack" },
        { L"URL", L"URL parsing, resolution and formatting constructor" },
        { L"URLSearchParams", L"Query string utility parser and manipulator" },
        { L"FormData", L"Constructs multipart/form-data for network upload" },
        { L"AbortController", L"Aborts asynchronous signals including fetch" },
        { L"Event", L"DOM event interface constructor" },
        { L"CustomEvent", L"User-defined synthetic DOM event with detail" },
        { L"requestAnimationFrame", L"Requests repaint callback before next screen frame" },
        { L"self", L"Current global window/worker or class instance" }
    };

    for (const auto& g : globals) {
        IntelliSenseItem item;
        item.label = g.name;
        item.detail = g.desc;
        item.insertText = g.name;
        item.kind = CompletionKind::Variable;
        m_jsGlobals.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateJavaScriptDomApis() {
    struct DomDef { const wchar_t* name; const wchar_t* signature; CompletionKind kind; };
    DomDef doms[] = {
        { L"getElementById", L"(elementId: string): HTMLElement | null", CompletionKind::Method },
        { L"querySelector", L"(selectors: string): Element | null", CompletionKind::Method },
        { L"querySelectorAll", L"(selectors: string): NodeListOf<Element>", CompletionKind::Method },
        { L"getElementsByClassName", L"(classNames: string): HTMLCollectionOf<Element>", CompletionKind::Method },
        { L"getElementsByTagName", L"(qualifiedName: string): HTMLCollectionOf<Element>", CompletionKind::Method },
        { L"createElement", L"(tagName: string): HTMLElement", CompletionKind::Method },
        { L"createTextNode", L"(data: string): Text", CompletionKind::Method },
        { L"createDocumentFragment", L"(): DocumentFragment", CompletionKind::Method },
        { L"appendChild", L"(node: Node): Node", CompletionKind::Method },
        { L"removeChild", L"(child: Node): Node", CompletionKind::Method },
        { L"replaceChild", L"(newChild: Node, oldChild: Node): Node", CompletionKind::Method },
        { L"insertBefore", L"(newNode: Node, referenceNode: Node | null): Node", CompletionKind::Method },
        { L"cloneNode", L"(deep?: boolean): Node", CompletionKind::Method },
        { L"addEventListener", L"(type: string, listener: EventListener): void", CompletionKind::Method },
        { L"removeEventListener", L"(type: string, listener: EventListener): void", CompletionKind::Method },
        { L"dispatchEvent", L"(event: Event): boolean", CompletionKind::Method },
        { L"setAttribute", L"(qualifiedName: string, value: string): void", CompletionKind::Method },
        { L"getAttribute", L"(qualifiedName: string): string | null", CompletionKind::Method },
        { L"removeAttribute", L"(qualifiedName: string): void", CompletionKind::Method },
        { L"hasAttribute", L"(qualifiedName: string): boolean", CompletionKind::Method },
        { L"getBoundingClientRect", L"(): DOMRect", CompletionKind::Method },
        { L"scrollIntoView", L"(arg?: boolean | ScrollIntoViewOptions): void", CompletionKind::Method },
        { L"focus", L"(options?: FocusOptions): void", CompletionKind::Method },
        { L"blur", L"(): void", CompletionKind::Method },
        { L"click", L"(): void", CompletionKind::Method },
        { L"innerHTML", L"string: HTML markup of element children", CompletionKind::Property },
        { L"innerText", L"string: Rendered text content of element", CompletionKind::Property },
        { L"textContent", L"string: Textual content of node and descendants", CompletionKind::Property },
        { L"classList", L"DOMTokenList: Token list of element CSS classes", CompletionKind::Property },
        { L"style", L"CSSStyleDeclaration: Inline CSS styling attributes", CompletionKind::Property },
        { L"dataset", L"DOMStringMap: Custom data-* attributes map", CompletionKind::Property },
        { L"parentElement", L"HTMLElement | null: Parent element node", CompletionKind::Property },
        { L"children", L"HTMLCollection: Child element nodes collection", CompletionKind::Property }
    };

    for (const auto& d : doms) {
        IntelliSenseItem item;
        item.label = d.name;
        item.detail = d.signature;
        item.insertText = d.name;
        item.kind = d.kind;
        m_jsDomApis.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateJavaScriptPrototypeMethods() {
    struct ProtoDef { const wchar_t* name; const wchar_t* sig; };
    ProtoDef protos[] = {
        // Array methods
        { L"map", L"<U>(callback: (value: T, index: number) => U): U[]" },
        { L"filter", L"(predicate: (value: T, index: number) => boolean): T[]" },
        { L"reduce", L"<U>(callback: (acc: U, cur: T) => U, initialValue: U): U" },
        { L"forEach", L"(callback: (value: T, index: number) => void): void" },
        { L"find", L"(predicate: (value: T) => boolean): T | undefined" },
        { L"findIndex", L"(predicate: (value: T) => boolean): number" },
        { L"includes", L"(searchElement: T, fromIndex?: number): boolean" },
        { L"indexOf", L"(searchElement: T, fromIndex?: number): number" },
        { L"push", L"(...items: T[]): number" },
        { L"pop", L"(): T | undefined" },
        { L"shift", L"(): T | undefined" },
        { L"unshift", L"(...items: T[]): number" },
        { L"splice", L"(start: number, deleteCount?: number, ...items: T[]): T[]" },
        { L"slice", L"(start?: number, end?: number): T[]" },
        { L"concat", L"(...items: ConcatArray<T>[]): T[]" },
        { L"join", L"(separator?: string): string" },
        { L"sort", L"(compareFn?: (a: T, b: T) => number): this" },
        { L"reverse", L"(): this" },
        { L"some", L"(predicate: (value: T) => boolean): boolean" },
        { L"every", L"(predicate: (value: T) => boolean): boolean" },
        { L"flat", L"<A>(depth?: number): A[]" },
        { L"flatMap", L"<U>(callback: (value: T) => U[]): U[]" },
        // String methods
        { L"split", L"(separator: string | RegExp, limit?: number): string[]" },
        { L"trim", L"(): string" },
        { L"trimStart", L"(): string" },
        { L"trimEnd", L"(): string" },
        { L"toLowerCase", L"(): string" },
        { L"toUpperCase", L"(): string" },
        { L"startsWith", L"(searchString: string, position?: number): boolean" },
        { L"endsWith", L"(searchString: string, endPosition?: number): boolean" },
        { L"replace", L"(searchValue: string | RegExp, replaceValue: string): string" },
        { L"replaceAll", L"(searchValue: string | RegExp, replaceValue: string): string" },
        { L"substring", L"(start: number, end?: number): string" },
        { L"charAt", L"(pos: number): string" },
        { L"charCodeAt", L"(index: number): number" },
        { L"padStart", L"(targetLength: number, padString?: string): string" },
        { L"padEnd", L"(targetLength: number, padString?: string): string" },
        { L"match", L"(matcher: RegExp): RegExpMatchArray | null" },
        // Promise methods
        { L"then", L"<TResult>(onfulfilled: (value: T) => TResult): Promise<TResult>" },
        { L"catch", L"<TResult>(onrejected: (reason: any) => TResult): Promise<TResult>" },
        { L"finally", L"(onfinally?: () => void): Promise<T>" },
        // JSON methods
        { L"stringify", L"(value: any, replacer?: any, space?: any): string" },
        { L"parse", L"(text: string, reviver?: any): any" },
        // Console methods
        { L"log", L"(...data: any[]): void" },
        { L"error", L"(...data: any[]): void" },
        { L"warn", L"(...data: any[]): void" },
        { L"info", L"(...data: any[]): void" },
        { L"debug", L"(...data: any[]): void" },
        { L"table", L"(tabularData?: any, properties?: string[]): void" }
    };

    for (const auto& p : protos) {
        IntelliSenseItem item;
        item.label = p.name;
        item.detail = p.sig;
        item.insertText = p.name;
        item.kind = CompletionKind::Method;
        m_jsPrototypes.push_back(std::move(item));
    }
}

std::wstring IntelliSenseEngine::ExtractQueryPrefix(const std::wstring& line, size_t cursorCol) {
    if (line.empty() || cursorCol == 0) return L"";
    size_t col = (std::min)(cursorCol, line.length());
    size_t start = col;
    while (start > 0) {
        wchar_t c = line[start - 1];
        if (iswalnum(c) || c == L'_' || c == L'$' || c == L'-') {
            start--;
        } else {
            break;
        }
    }
    return line.substr(start, col - start);
}

std::vector<IntelliSenseItem> IntelliSenseEngine::QueryCompletions(
    const std::wstring& fileName,
    const std::wstring& currentLine,
    size_t cursorCol
) const {
    std::wstring ext;
    auto pos = fileName.rfind(L'.');
    if (pos != std::wstring::npos) {
        ext = fileName.substr(pos);
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    }

    std::wstring prefix = ExtractQueryPrefix(currentLine, cursorCol);
    std::wstring prefixLower = prefix;
    for (auto& c : prefixLower) c = static_cast<wchar_t>(towlower(c));

    std::vector<IntelliSenseItem> matches;

    auto matchesPrefix = [&](const std::wstring& candidate) {
        if (prefixLower.empty()) return true;
        std::wstring candLower = candidate;
        for (auto& c : candLower) c = static_cast<wchar_t>(towlower(c));
        return (candLower.find(prefixLower) == 0); // Starts with prefix
    };

    bool isHtml = (ext == L".html" || ext == L".htm" || ext == L".php");
    bool isJs = (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" || ext == L".mjs" || ext == L".cjs" || isHtml);
    bool isPy = (ext == L".py");

    // Check context on line: if typing after '<' or in tag
    size_t lastOpenTag = currentLine.rfind(L'<', cursorCol > 0 ? cursorCol - 1 : 0);
    size_t lastCloseTag = currentLine.rfind(L'>', cursorCol > 0 ? cursorCol - 1 : 0);
    bool insideTag = (isHtml && lastOpenTag != std::wstring::npos && (lastCloseTag == std::wstring::npos || lastOpenTag > lastCloseTag));

    if (insideTag) {
        // Tag name or attributes
        size_t spaceAfterOpen = currentLine.find_first_of(L" \t", lastOpenTag);
        if (spaceAfterOpen == std::wstring::npos || cursorCol <= spaceAfterOpen) {
            // Suggest HTML tags
            for (const auto& tag : m_htmlTags) {
                if (matchesPrefix(tag.label)) {
                    matches.push_back(tag);
                }
            }
        } else {
            // Suggest HTML attributes
            for (const auto& attr : m_htmlAttributes) {
                if (matchesPrefix(attr.label)) {
                    matches.push_back(attr);
                }
            }
        }
    } else if (isJs) {
        // JavaScript / TypeScript context
        // Check if after a dot '.'
        size_t dotPos = (cursorCol > prefix.length() && cursorCol > 0) ? cursorCol - prefix.length() - 1 : std::wstring::npos;
        bool afterDot = (dotPos != std::wstring::npos && dotPos < currentLine.length() && currentLine[dotPos] == L'.');

        if (afterDot) {
            // Suggest prototype methods and DOM properties
            for (const auto& p : m_jsPrototypes) {
                if (matchesPrefix(p.label)) matches.push_back(p);
            }
            for (const auto& d : m_jsDomApis) {
                if (matchesPrefix(d.label)) matches.push_back(d);
            }
        } else {
            // Suggest globals, keywords, DOM APIs
            for (const auto& g : m_jsGlobals) {
                if (matchesPrefix(g.label)) matches.push_back(g);
            }
            for (const auto& kw : m_jsKeywords) {
                if (matchesPrefix(kw.label)) matches.push_back(kw);
            }
            for (const auto& d : m_jsDomApis) {
                if (matchesPrefix(d.label)) matches.push_back(d);
            }
        }
    } else if (isPy) {
        // Python context: self, keywords, common methods (Image 5)
        IntelliSenseItem itSelf{ L"self", L"Current class instance reference", L"self", CompletionKind::Property, 100 };
        if (matchesPrefix(L"self")) matches.push_back(std::move(itSelf));

        IntelliSenseItem itReset{ L"reset_parameters", L"(): void - Initializes neural module weights", L"reset_parameters()", CompletionKind::Method, 90 };
        if (matchesPrefix(L"reset_parameters")) matches.push_back(std::move(itReset));

        IntelliSenseItem itForward{ L"forward", L"(*args, **kwargs): Tensor", L"forward($0)", CompletionKind::Method, 85 };
        if (matchesPrefix(L"forward")) matches.push_back(std::move(itForward));
    }

    // Limit to top 25 matches for snappy UI performance
    if (matches.size() > 25) {
        matches.resize(25);
    }

    return matches;
}

} // namespace phide::editor
