#include "phide/editor/IntelliSenseEngine.hpp"
#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace phide::editor {

IntelliSenseEngine::IntelliSenseEngine() {
    PopulateHtmlTags();
    PopulateHtmlAttributes();
    PopulateJavaScriptKeywords();
    PopulateJavaScriptGlobals();
    PopulateJavaScriptDomApis();
    PopulateJavaScriptPrototypeMethods();
    PopulateCommonVocabulary();
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

void IntelliSenseEngine::PopulateCommonVocabulary() {
    struct WordDef { const wchar_t* word; const wchar_t* desc; CompletionKind kind; };
    WordDef words[] = {
        // Common web & programming vocabulary (requested by user: "hello", "log", "continuity", etc.)
        { L"hello", L"Greeting / sample greeting string", CompletionKind::Variable },
        { L"help", L"Help documentation / usage assistant", CompletionKind::Function },
        { L"header", L"Page or section header block / HTTP headers", CompletionKind::Tag },
        { L"heading", L"Document heading typography style", CompletionKind::Property },
        { L"height", L"Vertical dimension in pixels or viewport units", CompletionKind::Property },
        { L"hidden", L"Specifies that element is hidden from view", CompletionKind::Attribute },
        { L"href", L"Target URL or destination hyperlink", CompletionKind::Attribute },
        { L"hover", L"Hover interaction pseudo-class or state", CompletionKind::Property },
        { L"handle", L"Event handler or resource handle reference", CompletionKind::Function },
        { L"handler", L"Callback function invoked upon event trigger", CompletionKind::Function },
        { L"helper", L"Utility helper function or class", CompletionKind::Function },
        { L"host", L"Host name or server address", CompletionKind::Variable },
        { L"hostname", L"Domain host name without port", CompletionKind::Property },
        { L"html", L"HyperText Markup Language root document", CompletionKind::Tag },
        { L"http", L"Hypertext Transfer Protocol client or URL", CompletionKind::Variable },
        { L"https", L"Secure Hypertext Transfer Protocol", CompletionKind::Variable },

        { L"login", L"Authentication login operation or form", CompletionKind::Function },
        { L"logout", L"De-authentication session termination", CompletionKind::Function },
        { L"layout", L"CSS grid or flexbox layout container", CompletionKind::Property },
        { L"load", L"Loads resource, module, or document", CompletionKind::Function },
        { L"loader", L"Resource loader or loading spinner component", CompletionKind::Variable },
        { L"loading", L"Specifies lazy or eager resource loading", CompletionKind::Attribute },
        { L"local", L"Local scope variable or storage access", CompletionKind::Variable },
        { L"location", L"Window location URL interface", CompletionKind::Variable },
        { L"localStorage", L"Persistent web browser key-value storage", CompletionKind::Variable },
        { L"loop", L"Loop iteration or continuous media playback", CompletionKind::Keyword },
        { L"length", L"Count of elements in array or string", CompletionKind::Property },
        { L"log", L"Outputs message or debugging info to console", CompletionKind::Method },

        { L"data", L"Arbitrary data payload or attributes", CompletionKind::Variable },
        { L"date", L"Calendar date representation", CompletionKind::Variable },
        { L"default", L"Default fallback value or case", CompletionKind::Keyword },
        { L"define", L"Defines new symbol, macro, or module", CompletionKind::Keyword },
        { L"delete", L"Deletes property or removes record", CompletionKind::Keyword },
        { L"description", L"Detailed textual description or metadata", CompletionKind::Property },
        { L"detail", L"Event detail object or expanded view", CompletionKind::Property },
        { L"details", L"Disclosure widget container for details", CompletionKind::Tag },
        { L"dialog", L"Modal or non-modal popup dialog box", CompletionKind::Tag },
        { L"direction", L"Layout flow direction (row, column)", CompletionKind::Property },
        { L"disabled", L"Disables user interaction on element", CompletionKind::Attribute },
        { L"display", L"CSS display layout mode (flex, grid, block)", CompletionKind::Property },
        { L"dispatch", L"Dispatches action or event to listeners", CompletionKind::Function },
        { L"dispatchEvent", L"Dispatches synthetic event to DOM element", CompletionKind::Method },
        { L"div", L"Generic division block container <div></div>", CompletionKind::Tag },
        { L"document", L"DOM root Document object interface", CompletionKind::Variable },
        { L"done", L"Completion flag or promise fulfillment", CompletionKind::Property },
        { L"download", L"Specifies target file will be downloaded", CompletionKind::Attribute },
        { L"drag", L"Drag interaction event or state", CompletionKind::Property },
        { L"drop", L"Drop interaction target or handler", CompletionKind::Function },
        { L"duration", L"Animation or audio/video length in seconds", CompletionKind::Property },

        { L"element", L"Generic DOM element or array item", CompletionKind::Variable },
        { L"email", L"Email address validation or input field", CompletionKind::Property },
        { L"empty", L"Checks if collection or buffer has 0 items", CompletionKind::Function },
        { L"enable", L"Enables disabled feature or control", CompletionKind::Function },
        { L"end", L"Terminating position or end boundary", CompletionKind::Property },
        { L"enter", L"Enter key press or entrance transition", CompletionKind::Property },
        { L"entry", L"Key-value tuple entry in map or directory", CompletionKind::Variable },
        { L"error", L"Error object or error diagnostic message", CompletionKind::Variable },
        { L"event", L"Dispatched event payload or handler arg", CompletionKind::Variable },
        { L"exec", L"Executes shell command or regex pattern", CompletionKind::Function },
        { L"execute", L"Executes task or callback function", CompletionKind::Function },
        { L"exit", L"Terminates current process or closes view", CompletionKind::Function },
        { L"expand", L"Expands folded block or drawer panel", CompletionKind::Function },
        { L"export", L"Exports module members or exports file", CompletionKind::Keyword },

        { L"file", L"File handle, path, or upload descriptor", CompletionKind::Variable },
        { L"filter", L"Filters collection by predicate function", CompletionKind::Method },
        { L"find", L"Finds first matching element in array", CompletionKind::Method },
        { L"first", L"First element of sequence or list", CompletionKind::Property },
        { L"fixed", L"Fixed position layout or precision", CompletionKind::Property },
        { L"flex", L"Flexible CSS box model layout display", CompletionKind::Property },
        { L"float", L"Floating point number or CSS float", CompletionKind::Property },
        { L"focus", L"Brings keyboard or cursor focus to element", CompletionKind::Method },
        { L"font", L"Typography font family, size or style", CompletionKind::Property },
        { L"footer", L"Page or section footer container", CompletionKind::Tag },
        { L"form", L"Interactive form submission container", CompletionKind::Tag },
        { L"format", L"Formats string or data representation", CompletionKind::Function },
        { L"frame", L"Animation frame or window coordinate frame", CompletionKind::Variable },
        { L"function", L"Declares callable function or method", CompletionKind::Keyword },

        { L"get", L"Retrieves value or property getter", CompletionKind::Function },
        { L"global", L"Global execution scope or variable", CompletionKind::Keyword },
        { L"grid", L"CSS grid layout two-dimensional container", CompletionKind::Property },
        { L"group", L"Groups elements, tabs, or permissions", CompletionKind::Variable },

        { L"id", L"Unique identifier string", CompletionKind::Attribute },
        { L"image", L"Image bitmap or graphical asset", CompletionKind::Variable },
        { L"img", L"HTML image element <img>", CompletionKind::Tag },
        { L"import", L"Imports module or external package", CompletionKind::Keyword },
        { L"include", L"Includes header or checks containment", CompletionKind::Function },
        { L"index", L"Array or list numeric index offset", CompletionKind::Variable },
        { L"info", L"Informational log message or metadata", CompletionKind::Method },
        { L"init", L"Initializes component or configuration", CompletionKind::Function },
        { L"inline", L"Inline CSS flow display mode", CompletionKind::Property },
        { L"input", L"Interactive user input form control", CompletionKind::Tag },
        { L"insert", L"Inserts element or text at position", CompletionKind::Function },
        { L"instance", L"Instantiated object reference", CompletionKind::Variable },
        { L"int", L"Integer numeric primitive type", CompletionKind::Keyword },
        { L"interface", L"TypeScript interface contract declaration", CompletionKind::Keyword },
        { L"interval", L"Recurring timer interval identifier", CompletionKind::Variable },
        { L"is", L"Type checking or equality predicate", CompletionKind::Function },
        { L"item", L"Individual item within list or collection", CompletionKind::Variable },
        { L"items", L"Collection or array of items", CompletionKind::Variable },

        { L"json", L"JSON serialization utility or data", CompletionKind::Variable },
        { L"key", L"Object key, map key, or keyboard key", CompletionKind::Property },
        { L"keys", L"List of keys in object or dictionary", CompletionKind::Method },
        { L"label", L"Caption label for form input", CompletionKind::Tag },
        { L"last", L"Final element in sequence or list", CompletionKind::Property },
        { L"left", L"Horizontal left coordinate or alignment", CompletionKind::Property },
        { L"level", L"Hierarchy depth or logging severity", CompletionKind::Property },
        { L"line", L"Line number or line string in editor", CompletionKind::Property },
        { L"link", L"External resource stylesheet / icon link", CompletionKind::Tag },
        { L"list", L"Sequential collection list or container", CompletionKind::Variable },
        { L"listen", L"Registers event listener callback", CompletionKind::Function },
        { L"listener", L"Event listener handler callback function", CompletionKind::Function },

        { L"main", L"Central primary content element", CompletionKind::Tag },
        { L"map", L"Key-value map or array transformation", CompletionKind::Method },
        { L"margin", L"Outer CSS margin spacing around element", CompletionKind::Property },
        { L"match", L"Matches regular expression or pattern", CompletionKind::Method },
        { L"max", L"Maximum allowable limit or value", CompletionKind::Property },
        { L"message", L"Diagnostic message or notification text", CompletionKind::Property },
        { L"min", L"Minimum allowable lower bound value", CompletionKind::Property },
        { L"modal", L"Modal overlay dialog blocking input", CompletionKind::Variable },
        { L"module", L"Encapsulated ES module or package", CompletionKind::Keyword },
        { L"mount", L"Mounts component to DOM hierarchy", CompletionKind::Function },
        { L"mouse", L"Mouse pointer position or interaction", CompletionKind::Property },
        { L"move", L"Moves item or viewport coordinates", CompletionKind::Function },

        { L"name", L"Identifier name of symbol or form input", CompletionKind::Property },
        { L"nav", L"Navigation section containing links", CompletionKind::Tag },
        { L"navigation", L"Navigation bar or router service", CompletionKind::Variable },
        { L"new", L"Instantiates new class constructor", CompletionKind::Keyword },
        { L"next", L"Advances iterator to next element", CompletionKind::Method },
        { L"node", L"DOM tree node or Node.js runtime", CompletionKind::Variable },
        { L"null", L"Intentional absence of object value", CompletionKind::Keyword },
        { L"number", L"Numeric primitive value or constructor", CompletionKind::Keyword },

        { L"object", L"Key-value object record dictionary", CompletionKind::Keyword },
        { L"offset", L"Byte or pixel displacement offset", CompletionKind::Property },
        { L"on", L"Attaches event listener handler", CompletionKind::Function },
        { L"open", L"Opens document, dialog, or network socket", CompletionKind::Function },
        { L"option", L"Dropdown selection item in select", CompletionKind::Tag },
        { L"order", L"Flexbox or grid item display ordering", CompletionKind::Property },
        { L"origin", L"URL protocol, hostname and port origin", CompletionKind::Property },
        { L"out", L"Output buffer or output directory", CompletionKind::Variable },
        { L"output", L"Result of calculation or script stdout", CompletionKind::Tag },
        { L"overlay", L"Backdrop overlay scrim layer", CompletionKind::Variable },

        { L"pad", L"Pads string or array with filler items", CompletionKind::Method },
        { L"padding", L"Inner CSS spacing between content and border", CompletionKind::Property },
        { L"page", L"Web page document or pagination index", CompletionKind::Variable },
        { L"panel", L"Sidebar drawer or dockable view panel", CompletionKind::Variable },
        { L"param", L"Function argument parameter value", CompletionKind::Variable },
        { L"parent", L"Parent node or enclosing class instance", CompletionKind::Property },
        { L"parse", L"Parses string into structured object", CompletionKind::Method },
        { L"path", L"Filesystem path or SVG vector shape", CompletionKind::Property },
        { L"pattern", L"Regex pattern or architectural design", CompletionKind::Property },
        { L"payload", L"Network request or message data payload", CompletionKind::Variable },
        { L"pending", L"Promise or task awaiting completion", CompletionKind::Property },
        { L"pixel", L"Screen pixel metric unit", CompletionKind::Property },
        { L"placeholder", L"Input watermark placeholder hint text", CompletionKind::Attribute },
        { L"point", L"Two-dimensional (x, y) coordinate point", CompletionKind::Variable },
        { L"pop", L"Removes and returns last array element", CompletionKind::Method },
        { L"position", L"CSS positioning model (relative, absolute)", CompletionKind::Property },
        { L"post", L"HTTP POST request submission", CompletionKind::Function },
        { L"prefix", L"Leading substring prefix or namespace", CompletionKind::Property },
        { L"prev", L"Previous item or history state", CompletionKind::Function },
        { L"previous", L"Prior element in sequence", CompletionKind::Property },
        { L"primary", L"Primary theme accent or default item", CompletionKind::Property },
        { L"print", L"Prints document or outputs string", CompletionKind::Function },
        { L"process", L"Operating system process interface", CompletionKind::Variable },
        { L"progress", L"Progress indicator bar element", CompletionKind::Tag },
        { L"project", L"Workspace project descriptor", CompletionKind::Variable },
        { L"prompt", L"Interactive user input dialog or terminal prompt", CompletionKind::Function },
        { L"prop", L"Component property or DOM attribute", CompletionKind::Property },
        { L"properties", L"Collection of object properties", CompletionKind::Property },
        { L"property", L"Configurable object member property", CompletionKind::Property },
        { L"push", L"Appends item to array or pushes history", CompletionKind::Method },

        { L"query", L"Database or DOM selector query string", CompletionKind::Variable },
        { L"querySelector", L"Selects first DOM element matching CSS selector", CompletionKind::Method },
        { L"querySelectorAll", L"Selects all DOM elements matching CSS selector", CompletionKind::Method },
        { L"queue", L"First-in first-out task processing queue", CompletionKind::Variable },

        { L"radius", L"Border curvature radius in pixels", CompletionKind::Property },
        { L"range", L"Span of numeric values or text selection", CompletionKind::Variable },
        { L"read", L"Reads data from stream or file", CompletionKind::Function },
        { L"ready", L"DOM ready or service initialization flag", CompletionKind::Property },
        { L"record", L"Typed dictionary record structure", CompletionKind::Variable },
        { L"redirect", L"Redirects browser navigation to URL", CompletionKind::Function },
        { L"redo", L"Reapplies previously undone action", CompletionKind::Function },
        { L"ref", L"Reference pointer or React DOM ref", CompletionKind::Property },
        { L"refresh", L"Reloads data from backend or filesystem", CompletionKind::Function },
        { L"register", L"Registers event, service worker or component", CompletionKind::Function },
        { L"release", L"Releases acquired resource or lock", CompletionKind::Function },
        { L"reload", L"Reloads current window or page document", CompletionKind::Function },
        { L"remove", L"Removes element from DOM or collection", CompletionKind::Method },
        { L"render", L"Paints graphics or renders template UI", CompletionKind::Function },
        { L"renderer", L"Direct2D or WebGL rendering pipeline", CompletionKind::Variable },
        { L"replace", L"Replaces substring or array element", CompletionKind::Method },
        { L"request", L"Asynchronous HTTP network request", CompletionKind::Variable },
        { L"reset", L"Restores defaults or resets form state", CompletionKind::Function },
        { L"resolve", L"Resolves promise with fulfilled value", CompletionKind::Function },
        { L"response", L"HTTP response representation with body", CompletionKind::Variable },
        { L"result", L"Computation or query return value", CompletionKind::Variable },
        { L"return", L"Exits function returning value", CompletionKind::Keyword },
        { L"right", L"Horizontal right coordinate or alignment", CompletionKind::Property },
        { L"root", L"Top-level root element or directory", CompletionKind::Property },
        { L"route", L"Client-side navigation URL route", CompletionKind::Variable },
        { L"router", L"Single Page Application routing manager", CompletionKind::Variable },
        { L"row", L"Horizontal grid or table row container", CompletionKind::Tag },
        { L"run", L"Executes build, test, or program script", CompletionKind::Function },

        { L"save", L"Persists buffer changes to disk", CompletionKind::Function },
        { L"scale", L"DPI scale multiplier or zoom factor", CompletionKind::Property },
        { L"scope", L"Lexical variable scope or closure", CompletionKind::Variable },
        { L"screen", L"Display screen resolution and geometry", CompletionKind::Variable },
        { L"script", L"JavaScript executable script container", CompletionKind::Tag },
        { L"scroll", L"Scrolls viewport coordinates", CompletionKind::Function },
        { L"search", L"Finds substring or performs file search", CompletionKind::Function },
        { L"section", L"Thematic standalone document section", CompletionKind::Tag },
        { L"select", L"Dropdown selection control menu", CompletionKind::Tag },
        { L"selected", L"Specifies option is currently chosen", CompletionKind::Attribute },
        { L"selector", L"CSS element selector query string", CompletionKind::Property },
        { L"send", L"Sends network packet or IPC message", CompletionKind::Function },
        { L"server", L"Backend HTTP server listening instance", CompletionKind::Variable },
        { L"service", L"Background service or API client", CompletionKind::Variable },
        { L"session", L"User login session state or token", CompletionKind::Variable },
        { L"set", L"Mutates value or defines property setter", CompletionKind::Function },
        { L"setting", L"User preferences configuration option", CompletionKind::Property },
        { L"settings", L"Application configuration dictionary", CompletionKind::Variable },
        { L"share", L"Shares resource or invokes Web Share API", CompletionKind::Function },
        { L"shift", L"Removes and returns first array item", CompletionKind::Method },
        { L"show", L"Makes hidden view or dialog visible", CompletionKind::Function },
        { L"sidebar", L"Collapsible side drawer or rail navigation", CompletionKind::Variable },
        { L"size", L"Count of items or byte magnitude", CompletionKind::Property },
        { L"slider", L"Scrollbar thumb or numeric slider control", CompletionKind::Variable },
        { L"snippet", L"Code expansion template snippet", CompletionKind::Snippet },
        { L"sort", L"Sorts array in-place by comparator", CompletionKind::Method },
        { L"source", L"Source URL or media stream resource", CompletionKind::Tag },
        { L"span", L"Generic inline text styling container <span>", CompletionKind::Tag },
        { L"speed", L"Animation or playback playback rate", CompletionKind::Property },
        { L"split", L"Splits string by delimiter into array", CompletionKind::Method },
        { L"start", L"Initializes task or starting position", CompletionKind::Function },
        { L"state", L"Reactive component or application state", CompletionKind::Variable },
        { L"status", L"HTTP status code or operational health", CompletionKind::Property },
        { L"stop", L"Stops timer, animation, or process", CompletionKind::Function },
        { L"storage", L"Web storage key-value persistence interface", CompletionKind::Variable },
        { L"store", L"Centralized reactive state store", CompletionKind::Variable },
        { L"stream", L"Continuous readable or writable stream", CompletionKind::Variable },
        { L"string", L"Textual string primitive representation", CompletionKind::Keyword },
        { L"style", L"Inline CSS styling rules or style tag", CompletionKind::Tag },
        { L"submit", L"Submits form data to action endpoint", CompletionKind::Function },
        { L"success", L"Indicates successful operation result", CompletionKind::Property },
        { L"summary", L"Summary header for disclosure details", CompletionKind::Tag },
        { L"sync", L"Synchronizes state with remote repository", CompletionKind::Function },
        { L"syntax", L"Syntax highlighting rules or parser", CompletionKind::Variable },

        { L"tab", L"Editor tab or tab stop indentation", CompletionKind::Variable },
        { L"table", L"Tabular data table container <table>", CompletionKind::Tag },
        { L"tag", L"HTML or XML element tag name", CompletionKind::Property },
        { L"target", L"Event target element or build destination", CompletionKind::Property },
        { L"task", L"Asynchronous job or scheduled task", CompletionKind::Variable },
        { L"template", L"Declared HTML fragment for client cloning", CompletionKind::Tag },
        { L"terminal", L"Integrated command shell terminal console", CompletionKind::Variable },
        { L"test", L"Executes unit test or checks regex match", CompletionKind::Function },
        { L"text", L"Textual string content or text node", CompletionKind::Property },
        { L"theme", L"Visual palette theme configuration", CompletionKind::Variable },
        { L"time", L"Current timestamp or datetime element", CompletionKind::Tag },
        { L"timeout", L"Millisecond delay duration before timeout", CompletionKind::Property },
        { L"timer", L"Active timer handle returned by setTimeout", CompletionKind::Variable },
        { L"title", L"Document title in browser tab or tooltip", CompletionKind::Tag },
        { L"toggle", L"Inverts boolean flag or toggles panel", CompletionKind::Function },
        { L"token", L"Lexical syntax token or auth token", CompletionKind::Variable },
        { L"tooltip", L"Floating advisory description balloon", CompletionKind::Variable },
        { L"top", L"Vertical top coordinate or stack peak", CompletionKind::Property },
        { L"total", L"Accumulated sum or count of items", CompletionKind::Property },
        { L"track", L"Scrollbar track or media subtitle track", CompletionKind::Tag },
        { L"transition", L"Smooth CSS state interpolation transition", CompletionKind::Property },
        { L"tree", L"Hierarchical tree view file structure", CompletionKind::Variable },
        { L"trigger", L"Triggers event or executes hook", CompletionKind::Function },
        { L"true", L"Boolean true truth value literal", CompletionKind::Keyword },
        { L"type", L"Data type or control type attribute", CompletionKind::Attribute },

        { L"undo", L"Reverts previous user action in buffer", CompletionKind::Function },
        { L"unit", L"CSS metric unit or test unit", CompletionKind::Property },
        { L"unknown", L"TypeScript unknown type guard", CompletionKind::Keyword },
        { L"update", L"Updates existing record or UI view", CompletionKind::Function },
        { L"upload", L"Transfers file from client to server", CompletionKind::Function },
        { L"url", L"Uniform Resource Locator web address", CompletionKind::Variable },
        { L"user", L"User profile or authenticated account", CompletionKind::Variable },
        { L"util", L"Utility helper functions module", CompletionKind::Variable },
        { L"utility", L"Helper library or convenience utility", CompletionKind::Variable },

        { L"val", L"Current value or immutable binding", CompletionKind::Variable },
        { L"valid", L"Indicates form control passed validation", CompletionKind::Property },
        { L"validate", L"Validates syntax or form input rules", CompletionKind::Function },
        { L"validator", L"Syntax or schema validation engine", CompletionKind::Variable },
        { L"value", L"Current value stored in control or variable", CompletionKind::Property },
        { L"var", L"Declares function-scoped variable", CompletionKind::Keyword },
        { L"variable", L"Named value container in program memory", CompletionKind::Variable },
        { L"version", L"Semantic software version string", CompletionKind::Property },
        { L"view", L"User interface view or viewport", CompletionKind::Variable },
        { L"viewport", L"Visible screen canvas viewing rectangle", CompletionKind::Property },
        { L"visible", L"Indicates whether element is currently rendered", CompletionKind::Property },

        { L"wait", L"Pauses execution until signal arrives", CompletionKind::Function },
        { L"warn", L"Emits warning diagnostic to console", CompletionKind::Method },
        { L"warning", L"Warning alert or diagnostic issue", CompletionKind::Variable },
        { L"watch", L"Observes file changes or reactive property", CompletionKind::Function },
        { L"watcher", L"Background filesystem watcher thread", CompletionKind::Variable },
        { L"weight", L"Font weight boldness magnitude", CompletionKind::Property },
        { L"white", L"Pure white color value #FFFFFF", CompletionKind::Property },
        { L"widget", L"Interactive graphical UI widget control", CompletionKind::Variable },
        { L"width", L"Horizontal dimension in pixels or ratio", CompletionKind::Property },
        { L"win", L"Operating system window or win result", CompletionKind::Variable },
        { L"window", L"Global browser window execution context", CompletionKind::Variable },
        { L"word", L"Individual textual token or word wrap", CompletionKind::Property },
        { L"work", L"Background worker thread or task", CompletionKind::Variable },
        { L"working", L"Current working directory or busy state", CompletionKind::Property },
        { L"workspace", L"Active project workspace root folder", CompletionKind::Variable },
        { L"wrap", L"Enables automatic soft line wrapping", CompletionKind::Property },
        { L"write", L"Writes data to stream, file, or socket", CompletionKind::Function }
    };

    for (const auto& w : words) {
        IntelliSenseItem item;
        item.label = w.word;
        item.detail = w.desc;
        if (w.kind == CompletionKind::Tag) {
            item.insertText = std::wstring(L"<") + w.word + L">$0</" + w.word + L">";
        } else {
            item.insertText = w.word;
        }
        item.kind = w.kind;
        m_commonVocabulary.push_back(std::move(item));
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

    std::vector<IntelliSenseItem> rawCandidates;

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
            for (const auto& tag : m_htmlTags) rawCandidates.push_back(tag);
        } else {
            for (const auto& attr : m_htmlAttributes) rawCandidates.push_back(attr);
        }
    } else if (isHtml) {
        // In HTML outside of tags (e.g. typing "di" or "hel" directly in document)
        // 1. Suggest HTML tags (div, dialog, header, head, p, span, etc.) with highest priority!
        for (const auto& tag : m_htmlTags) {
            IntelliSenseItem it = tag;
            it.insertText = std::wstring(L"<") + tag.label + L">$0</" + tag.label + L">";
            it.score += 300; // Prioritize tags in HTML so 'div' beats 'dispatchEvent'
            rawCandidates.push_back(std::move(it));
        }
        // 2. Suggest common web & programming vocabulary (hello, help, etc.)
        for (const auto& v : m_commonVocabulary) rawCandidates.push_back(v);
        // 3. Suggest JS globals & keywords
        for (const auto& g : m_jsGlobals) rawCandidates.push_back(g);
        for (const auto& kw : m_jsKeywords) rawCandidates.push_back(kw);
        for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
    } else if (isJs) {
        size_t dotPos = (cursorCol > prefix.length() && cursorCol > 0) ? cursorCol - prefix.length() - 1 : std::wstring::npos;
        bool afterDot = (dotPos != std::wstring::npos && dotPos < currentLine.length() && currentLine[dotPos] == L'.');

        if (afterDot) {
            for (const auto& p : m_jsPrototypes) rawCandidates.push_back(p);
            for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
        } else {
            for (const auto& g : m_jsGlobals) rawCandidates.push_back(g);
            for (const auto& kw : m_jsKeywords) rawCandidates.push_back(kw);
            for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
            for (const auto& v : m_commonVocabulary) rawCandidates.push_back(v);
        }
    } else if (isPy) {
        IntelliSenseItem itSelf{ L"self", L"Current class instance reference", L"self", CompletionKind::Property, 100 };
        rawCandidates.push_back(std::move(itSelf));

        IntelliSenseItem itReset{ L"reset_parameters", L"(): void - Initializes neural module weights", L"reset_parameters()", CompletionKind::Method, 90 };
        rawCandidates.push_back(std::move(itReset));

        IntelliSenseItem itForward{ L"forward", L"(*args, **kwargs): Tensor", L"forward($0)", CompletionKind::Method, 85 };
        rawCandidates.push_back(std::move(itForward));
        for (const auto& v : m_commonVocabulary) rawCandidates.push_back(v);
    } else {
        for (const auto& v : m_commonVocabulary) rawCandidates.push_back(v);
    }

    // Matching & Scoring Engine: Exact prefix (1000), Substring match (500), Fuzzy match (100)
    std::vector<IntelliSenseItem> matches;
    std::unordered_set<std::wstring> seenLabels;

    for (const auto& item : rawCandidates) {
        if (seenLabels.count(item.label)) continue;

        if (prefixLower.empty()) {
            IntelliSenseItem scored = item;
            scored.score += 50;
            scored.matchStart = 0;
            scored.matchLen = 0;
            seenLabels.insert(item.label);
            matches.push_back(std::move(scored));
            continue;
        }

        std::wstring candLower = item.label;
        for (auto& c : candLower) c = static_cast<wchar_t>(towlower(c));

        size_t p = candLower.find(prefixLower);
        if (p == 0) {
            // Exact prefix match: Highest priority (score ~ 1000)
            IntelliSenseItem scored = item;
            scored.score += 1000 - static_cast<int>(item.label.length());
            scored.matchStart = 0;
            scored.matchLen = prefix.length();
            seenLabels.insert(item.label);
            matches.push_back(std::move(scored));
        } else if (p != std::wstring::npos) {
            // Substring match: Medium priority (score ~ 500)
            IntelliSenseItem scored = item;
            scored.score += 500 - static_cast<int>(p * 10 + item.label.length());
            scored.matchStart = p;
            scored.matchLen = prefix.length();
            seenLabels.insert(item.label);
            matches.push_back(std::move(scored));
        } else {
            // Fuzzy match: all chars of prefix appear in order (score ~ 100)
            size_t pi = 0;
            size_t firstMatch = std::wstring::npos;
            for (size_t ci = 0; ci < candLower.length() && pi < prefixLower.length(); ++ci) {
                if (candLower[ci] == prefixLower[pi]) {
                    if (firstMatch == std::wstring::npos) firstMatch = ci;
                    pi++;
                }
            }
            if (pi == prefixLower.length()) {
                IntelliSenseItem scored = item;
                scored.score += 100 - static_cast<int>(item.label.length());
                scored.matchStart = (firstMatch != std::wstring::npos) ? firstMatch : 0;
                scored.matchLen = prefix.length();
                seenLabels.insert(item.label);
                matches.push_back(std::move(scored));
            }
        }
    }

    // Sort descending by score
    std::sort(matches.begin(), matches.end(), [](const IntelliSenseItem& a, const IntelliSenseItem& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.label < b.label;
    });

    if (matches.size() > 50) {
        matches.resize(50);
    }

    return matches;
}

} // namespace phide::editor
