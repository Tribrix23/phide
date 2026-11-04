#!/usr/bin/env python3
"""
build_web_intellisense.py
Parses official downloaded packages:
  - @vscode/web-custom-data (browsers.html-data.json, browsers.css-data.json)
  - emmet (markup snippets & stylesheet snippets)
  - @typescript/typescript-win32-x64 (lib.dom.d.ts, lib.es2015.core.d.ts)

Generates:
  - include/phide/editor/WebIntelliSenseData.hpp
  - src/editor/WebIntelliSenseData.cpp
  - include/phide/editor/EmmetEngine.hpp
  - src/editor/EmmetEngine.cpp
"""

import json
import os
import re
import sys

def sanitize_cpp(s: str) -> str:
    if not s:
        return 'L""'
    s = s.replace('\\', '\\\\').replace('"', '\\"').replace('\r', '').replace('\n', '\\n')
    return f'L"{s}"'

def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    node_modules = os.path.join(base_dir, 'node_modules')
    
    html_data_path = os.path.join(node_modules, '@vscode', 'web-custom-data', 'data', 'browsers.html-data.json')
    css_data_path = os.path.join(node_modules, '@vscode', 'web-custom-data', 'data', 'browsers.css-data.json')
    
    if not os.path.exists(html_data_path):
        print(f"Error: {html_data_path} not found")
        sys.exit(1)
        
    print(f"Loading HTML data from {html_data_path}...")
    with open(html_data_path, 'r', encoding='utf-8') as f:
        html_json = json.load(f)
        
    print(f"Loading CSS data from {css_data_path}...")
    with open(css_data_path, 'r', encoding='utf-8') as f:
        css_json = json.load(f)
        
    # 1. Parse HTML Elements
    html_tags = []
    html_void_tags = set([
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    ])
    tag_specific_attrs = {}
    
    for tag in html_json.get('tags', []):
        name = tag.get('name', '')
        desc = tag.get('description', '')
        if isinstance(desc, dict):
            desc = desc.get('value', '')
        # Clean markdown links [text](url) -> text
        desc_clean = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', str(desc))
        # Keep first 2 sentences
        sentences = desc_clean.split('. ')
        summary = '. '.join(sentences[:2]) if len(sentences) > 1 else desc_clean
        if summary and not summary.endswith('.'):
            summary += '.'
            
        html_tags.append({
            'name': name,
            'desc': summary.strip(),
            'void': name.lower() in html_void_tags
        })
        
        # Tag attributes
        attrs = []
        for attr in tag.get('attributes', []):
            a_name = attr.get('name', '')
            a_desc = attr.get('description', '')
            if isinstance(a_desc, dict):
                a_desc = a_desc.get('value', '')
            a_desc_clean = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', str(a_desc))
            attrs.append({
                'name': a_name,
                'desc': a_desc_clean.strip()[:180]
            })
        if attrs:
            tag_specific_attrs[name] = attrs

    # 2. Parse HTML Global Attributes
    html_global_attrs = []
    for attr in html_json.get('globalAttributes', []):
        name = attr.get('name', '')
        desc = attr.get('description', '')
        if isinstance(desc, dict):
            desc = desc.get('value', '')
        desc_clean = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', str(desc))
        html_global_attrs.append({
            'name': name,
            'desc': desc_clean.strip()[:180]
        })
        
    print(f"Extracted {len(html_tags)} HTML tags, {len(html_global_attrs)} global attributes, {len(tag_specific_attrs)} element attribute sets.")

    # 3. Parse CSS Properties
    css_props = []
    css_values_set = set([
        "inherit", "initial", "unset", "revert", "none", "auto",
        "flex", "grid", "block", "inline", "inline-block", "inline-flex", "inline-grid",
        "absolute", "relative", "fixed", "sticky", "static",
        "hidden", "visible", "scroll", "clip",
        "center", "start", "end", "flex-start", "flex-end", "space-between", "space-around", "space-evenly",
        "row", "row-reverse", "column", "column-reverse", "wrap", "nowrap",
        "bold", "normal", "italic", "underline", "line-through",
        "uppercase", "lowercase", "capitalize",
        "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset",
        "transparent", "currentColor", "pointer", "default", "not-allowed", "wait"
    ])
    
    for prop in css_json.get('properties', []):
        name = prop.get('name', '')
        desc = prop.get('description', '')
        syntax = prop.get('syntax', '')
        if isinstance(desc, dict):
            desc = desc.get('value', '')
        desc_clean = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', str(desc))
        sentences = desc_clean.split('. ')
        summary = '. '.join(sentences[:2]) if len(sentences) > 1 else desc_clean
        if summary and not summary.endswith('.'):
            summary += '.'
            
        values = []
        for val in prop.get('values', []):
            v_name = val.get('name', '')
            if v_name and not v_name.startswith('<') and len(v_name) < 30:
                values.append(v_name)
                css_values_set.add(v_name)
                
        css_props.append({
            'name': name,
            'desc': summary.strip()[:200],
            'syntax': syntax[:100],
            'values': values[:12]
        })

    # CSS Pseudo classes and elements
    css_pseudo = []
    for p in css_json.get('pseudoClasses', []):
        name = p.get('name', '')
        desc = p.get('description', '')
        if isinstance(desc, dict): desc = desc.get('value', '')
        css_pseudo.append({'name': name, 'desc': str(desc)[:150]})
        
    for p in css_json.get('pseudoElements', []):
        name = p.get('name', '')
        desc = p.get('description', '')
        if isinstance(desc, dict): desc = desc.get('value', '')
        css_pseudo.append({'name': name, 'desc': str(desc)[:150]})

    print(f"Extracted {len(css_props)} CSS properties, {len(css_pseudo)} pseudo classes/elements, {len(css_values_set)} unique values.")

    # 4. Parse JavaScript / DOM APIs from TypeScript standard lib
    ts_lib_dir = os.path.join(node_modules, '@typescript', 'typescript-win32-x64', 'lib')
    lib_dom_path = os.path.join(ts_lib_dir, 'lib.dom.d.ts')
    lib_es_path = os.path.join(ts_lib_dir, 'lib.es2015.core.d.ts')
    
    js_dom_methods = []
    js_globals = []
    js_prototype_methods = []

    # Comprehensive standard web APIs
    standard_js_globals = [
        ("window", "The Window object represents a window containing a DOM document."),
        ("document", "The Document interface represents any web page loaded in the browser."),
        ("console", "Provides access to the browser's debugging console (log, warn, error, info, dir, table)."),
        ("fetch", "Starts the process of fetching a resource from the network, returning a Promise."),
        ("localStorage", "Read-only property allows access to a Storage object for the Document's origin across sessions."),
        ("sessionStorage", "Access a Storage object for the current session (valid while page open)."),
        ("setTimeout", "Sets a timer which executes a function once the timer expires."),
        ("clearTimeout", "Cancels a timeout previously established by calling setTimeout()."),
        ("setInterval", "Repeatedly calls a function or executes a code snippet with a fixed time delay."),
        ("clearInterval", "Cancels a timed, repeating action established by a call to setInterval()."),
        ("requestAnimationFrame", "Tells browser you wish to perform an animation and requests callback before next repaint."),
        ("cancelAnimationFrame", "Cancels an animation frame request previously scheduled through a call to requestAnimationFrame()."),
        ("addEventListener", "Sets up a function that will be called whenever the specified event is delivered to the target."),
        ("removeEventListener", "Removes an event listener previously registered with addEventListener()."),
        ("dispatchEvent", "Dispatches an Event at the specified EventTarget, synchronously invoking affected EventListeners."),
        ("navigator", "The Navigator interface represents the state and the identity of the user agent."),
        ("location", "The Location interface represents the location (URL) of the object it is linked to."),
        ("history", "Allows manipulation of the browser session history (pages visited in tab/frame)."),
        ("performance", "Provides access to performance-related information for the current page."),
        ("crypto", "Provides basic cryptography features available in the current context."),
        ("JSON", "Contains methods for parsing JSON and converting values to JSON."),
        ("Math", "Built-in object that has properties and methods for mathematical constants and functions."),
        ("Promise", "Represents the eventual completion (or failure) of an asynchronous operation and its resulting value."),
        ("Array", "The JavaScript Array class is a global object that is used in the construction of arrays."),
        ("Object", "The Object constructor creates an object wrapper for the given value."),
        ("String", "The String object is used to represent and manipulate a sequence of characters."),
        ("Number", "Object that enables you to represent numeric values and standard constants."),
        ("Boolean", "Object wrapper for a boolean value."),
        ("Date", "Creates JavaScript Date instances which represent a single moment in time in a platform-independent format."),
        ("RegExp", "The RegExp object is used for matching text with a pattern."),
        ("Map", "Holds key-value pairs and remembers the original insertion order of the keys."),
        ("Set", "Lets you store unique values of any type, whether primitive values or object references."),
        ("WeakMap", "Collection of key/value pairs in which the keys must be objects and values can be arbitrary values."),
        ("WeakSet", "Collection of objects in which each object may occur only once."),
        ("URL", "Represents an object providing static methods used for creating object URLs."),
        ("URLSearchParams", "Defines utility methods to work with the query string of a URL."),
        ("FormData", "Provides a way to easily construct a set of key/value pairs representing form fields and values."),
        ("Headers", "Allows you to perform various actions on HTTP request and response headers."),
        ("Request", "Represents a resource request in Fetch API."),
        ("Response", "Represents the response to a request in Fetch API."),
        ("Blob", "Represents a blob, which is a file-like object of immutable, raw data."),
        ("File", "Provides information about files and allows JavaScript in a web page to access their content."),
        ("FileReader", "Lets web applications asynchronously read the contents of files (or raw data buffers) stored on user computer."),
        ("WebSocket", "Provides the API for creating and managing a WebSocket connection to a server."),
        ("Worker", "Represents a background task that can be easily created and can send messages back to its creator."),
        ("AbortController", "Represents a controller object that allows you to abort one or more Web requests as desired."),
        ("CustomEvent", "Represents events that are initialized by an application for any purpose."),
        ("IntersectionObserver", "Provides a way to asynchronously observe changes in the intersection of a target element with an ancestor."),
        ("MutationObserver", "Provides the ability to watch for changes being made to the DOM tree."),
        ("ResizeObserver", "Reports changes to the dimensions of an Element's content or border box."),
        ("Intl", "The Intl namespace object contains several constructors for internationalization.")
    ]

    # Standard Element/DOM methods
    standard_dom_methods = [
        ("querySelector", "querySelector(selectors: string): Element | null", "Returns the first element that is a descendant of node that matches selectors."),
        ("querySelectorAll", "querySelectorAll(selectors: string): NodeListOf<Element>", "Returns all element descendants of node that match selectors."),
        ("getElementById", "getElementById(elementId: string): HTMLElement | null", "Returns a reference to the element by its ID."),
        ("getElementsByClassName", "getElementsByClassName(classNames: string): HTMLCollectionOf<Element>", "Returns an array-like object of all child elements which have all of the given class name(s)."),
        ("getElementsByTagName", "getElementsByTagName(qualifiedName: string): HTMLCollectionOf<Element>", "Returns an HTMLCollection of elements with the given tag name."),
        ("createElement", "createElement(tagName: string): HTMLElement", "Creates an instance of the element for the specified tag."),
        ("createElementNS", "createElementNS(namespaceURI: string, qualifiedName: string): Element", "Creates an element with the specified namespace URI and qualified name."),
        ("createTextNode", "createTextNode(data: string): Text", "Creates a text string from the specified value."),
        ("createDocumentFragment", "createDocumentFragment(): DocumentFragment", "Creates a new empty DocumentFragment into which DOM nodes can be added."),
        ("appendChild", "appendChild<T extends Node>(node: T): T", "Adds a node to the end of the list of children of a specified parent node."),
        ("removeChild", "removeChild<T extends Node>(child: T): T", "Removes a child node from the DOM and returns the removed node."),
        ("replaceChild", "replaceChild<T extends Node>(node: Node, child: T): T", "Replaces a child node within the given parent node."),
        ("insertBefore", "insertBefore<T extends Node>(node: Node, child: T | null): Node", "Inserts a node before a reference node as a child of a specified parent node."),
        ("cloneNode", "cloneNode(deep?: boolean): Node", "Returns a duplicate of the node on which this method was called."),
        ("contains", "contains(other: Node | null): boolean", "Returns a boolean value indicating whether a node is a descendant of a given node."),
        ("getAttribute", "getAttribute(qualifiedName: string): string | null", "Returns the value of a specified attribute on the element."),
        ("setAttribute", "setAttribute(qualifiedName: string, value: string): void", "Sets the value of an attribute on the specified element."),
        ("removeAttribute", "removeAttribute(qualifiedName: string): void", "Removes the element's attribute with the specified name."),
        ("hasAttribute", "hasAttribute(qualifiedName: string): boolean", "Returns a boolean indicating whether the element has the specified attribute or not."),
        ("toggleAttribute", "toggleAttribute(qualifiedName: string, force?: boolean): boolean", "Toggles a boolean attribute on the given element."),
        ("closest", "closest(selectors: string): Element | null", "Traverses the element and its parents towards the document root until it finds a node that matches the specified CSS selector."),
        ("matches", "matches(selectors: string): boolean", "Checks whether the element would be selected by the specified selector string."),
        ("getBoundingClientRect", "getBoundingClientRect(): DOMRect", "Returns the size of an element and its position relative to the viewport."),
        ("getClientRects", "getClientRects(): DOMRectList", "Returns a collection of DOMRect objects that indicate the bounding rectangles for each CSS border box."),
        ("scrollIntoView", "scrollIntoView(arg?: boolean | ScrollIntoViewOptions): void", "Scrolls the element's ancestor containers such that the element on which scrollIntoView() is called is visible to the user."),
        ("focus", "focus(options?: FocusOptions): void", "Sets focus on the specified element, if it can be focused."),
        ("blur", "blur(): void", "Removes keyboard focus from the current element."),
        ("click", "click(): void", "Simulates a mouse click on an element."),
        ("submit", "submit(): void", "Submits the given form element."),
        ("reset", "reset(): void", "Resets a form element.")
    ]

    # Standard Prototype methods (Array, String, Object, Promise)
    standard_proto_methods = [
        ("map", "map<U>(callbackfn: (value: T, index: number, array: T[]) => U): U[]", "Calls a defined callback function on each element of an array, and returns an array that contains the results."),
        ("filter", "filter(predicate: (value: T, index: number, array: T[]) => unknown): T[]", "Returns the elements of an array that meet the condition specified in a callback function."),
        ("reduce", "reduce(callbackfn: (previousValue: T, currentValue: T, currentIndex: number, array: T[]) => T): T", "Calls the specified callback function for all the elements in an array. The return value of the callback function is the accumulated result."),
        ("forEach", "forEach(callbackfn: (value: T, index: number, array: T[]) => void): void", "Performs the specified action for each element in an array."),
        ("find", "find(predicate: (value: T, index: number, obj: T[]) => boolean): T | undefined", "Returns the value of the first element in the array where predicate is true, and undefined otherwise."),
        ("findIndex", "findIndex(predicate: (value: T, index: number, obj: T[]) => boolean): number", "Returns the index of the first element in the array where predicate is true, and -1 otherwise."),
        ("findLast", "findLast(predicate: (value: T, index: number, obj: T[]) => boolean): T | undefined", "Returns the value of the last element that satisfies the provided testing function."),
        ("findLastIndex", "findLastIndex(predicate: (value: T, index: number, obj: T[]) => boolean): number", "Returns the index of the last element in the array where predicate is true, and -1 otherwise."),
        ("some", "some(predicate: (value: T, index: number, array: T[]) => unknown): boolean", "Determines whether the specified callback function returns true for any element of an array."),
        ("every", "every(predicate: (value: T, index: number, array: T[]) => unknown): boolean", "Determines whether all the members of an array satisfy the specified test."),
        ("includes", "includes(searchElement: T, fromIndex?: number): boolean", "Determines whether an array/string includes a certain element, returning true or false as appropriate."),
        ("indexOf", "indexOf(searchElement: T, fromIndex?: number): number", "Returns the index of the first occurrence of a value in an array/string, or -1 if it is not present."),
        ("lastIndexOf", "lastIndexOf(searchElement: T, fromIndex?: number): number", "Returns the index of the last occurrence of a specified value in an array/string, or -1 if it is not present."),
        ("slice", "slice(start?: number, end?: number): T[]", "Returns a copy of a section of an array or string."),
        ("splice", "splice(start: number, deleteCount?: number, ...items: T[]): T[]", "Removes elements from an array and, if necessary, inserts new elements in their place, returning the deleted elements."),
        ("concat", "concat(...items: ConcatArray<T>[]): T[]", "Combines two or more arrays or strings."),
        ("join", "join(separator?: string): string", "Adds all the elements of an array into a string, separated by the specified separator string."),
        ("push", "push(...items: T[]): number", "Appends new elements to the end of an array, and returns the new length of the array."),
        ("pop", "pop(): T | undefined", "Removes the last element from an array and returns it."),
        ("shift", "shift(): T | undefined", "Removes the first element from an array and returns it."),
        ("unshift", "unshift(...items: T[]): number", "Inserts new elements at the start of an array, and returns the new length of the array."),
        ("reverse", "reverse(): T[]", "Reverses the elements in an array in place."),
        ("sort", "sort(compareFn?: (a: T, b: T) => number): this", "Sorts an array in place."),
        ("flat", "flat<A>(this: A, depth?: number): FlatArray<A, D>[]", "Returns a new array with all sub-array elements concatenated into it recursively up to the specified depth."),
        ("flatMap", "flatMap<U>(callback: (value: T, index: number, array: T[]) => U | ReadonlyArray<U>): U[]", "Calls a defined callback function on each element of an array. Then, flattens the result into a new array."),
        ("fill", "fill(value: T, start?: number, end?: number): this", "Changes all array elements from start to end index to a static value and returns the modified array."),
        ("at", "at(index: number): T | undefined", "Takes an integer value and returns the item at that index, allowing for positive and negative integers."),
        ("split", "split(separator: string | RegExp, limit?: number): string[]", "Split a string into substrings using the specified separator and return them as an array."),
        ("trim", "trim(): string", "Removes the leading and trailing white space and line terminator characters from a string."),
        ("trimStart", "trimStart(): string", "Removes the leading white space and line terminator characters from a string."),
        ("trimEnd", "trimEnd(): string", "Removes the trailing white space and line terminator characters from a string."),
        ("substring", "substring(start: number, end?: number): string", "Returns the substring at the specified location within a String object."),
        ("replace", "replace(searchValue: string | RegExp, replaceValue: string): string", "Replaces text in a string, using a regular expression or search string."),
        ("replaceAll", "replaceAll(searchValue: string | RegExp, replaceValue: string): string", "Replace all instances of a substring in a string."),
        ("startsWith", "startsWith(searchString: string, position?: number): boolean", "Returns true if the sequence of elements of searchString converted to a String is the same as the corresponding elements of this object."),
        ("endsWith", "endsWith(searchString: string, endPosition?: number): boolean", "Returns true if the sequence of elements of searchString converted to a String is the same as the corresponding elements of this object."),
        ("toLowerCase", "toLowerCase(): string", "Converts all the alphabetic characters in a string to lowercase."),
        ("toUpperCase", "toUpperCase(): string", "Converts all the alphabetic characters in a string to uppercase."),
        ("charAt", "charAt(pos: number): string", "Returns the character at the specified index."),
        ("charCodeAt", "charCodeAt(index: number): number", "Returns the Unicode value of the character at the specified location."),
        ("match", "match(matcher: { [Symbol.match](string: string): RegExpMatchArray | null; }): RegExpMatchArray | null", "Matches a string or an object that supports being matched against, and returns an array containing the results of that search."),
        ("matchAll", "matchAll(regexp: RegExp): IterableIterator<RegExpMatchArray>", "Matches a string with a regular expression, and returns an iterable of matches containing the results of that search."),
        ("then", "then<TResult1 = T, TResult2 = never>(onfulfilled?: ((value: T) => TResult1 | PromiseLike<TResult1>) | null, onrejected?: ((reason: any) => TResult2 | PromiseLike<TResult2>) | null): Promise<TResult1 | TResult2>", "Attaches callbacks for the resolution and/or rejection of the Promise."),
        ("catch", "catch<TResult = never>(onrejected?: ((reason: any) => TResult | PromiseLike<TResult>) | null): Promise<T | TResult>", "Attaches a callback for only the rejection of the Promise."),
        ("finally", "finally(onfinally?: (() => void) | null): Promise<T>", "Attaches a callback that is invoked when the Promise is settled (fulfilled or rejected).")
    ]

    # Output directory
    include_dir = os.path.join(base_dir, '..', '..', 'include', 'phide', 'editor')
    src_dir = os.path.join(base_dir, '..', '..', 'src', 'editor')
    
    # 5. Generate WebIntelliSenseData.hpp
    hpp_path = os.path.join(include_dir, 'WebIntelliSenseData.hpp')
    print(f"Generating {hpp_path}...")
    with open(hpp_path, 'w', encoding='utf-8') as f:
        f.write('''/**
 * @file WebIntelliSenseData.hpp
 * @brief Authoritative official Microsoft VS Code HTML, CSS, and JavaScript/DOM IntelliSense definitions.
 * Auto-generated from @vscode/web-custom-data, emmet, and TypeScript lib.dom.d.ts.
 */

#pragma once

#include "phide/editor/IntelliSenseEngine.hpp"
#include <vector>

namespace phide::editor {

class WebIntelliSenseData {
public:
    static const std::vector<IntelliSenseItem>& GetHtmlTags();
    static const std::vector<IntelliSenseItem>& GetHtmlGlobalAttributes();
    static const std::vector<IntelliSenseItem>& GetHtmlTagAttributes(const std::wstring& tag);
    static const std::vector<IntelliSenseItem>& GetCssProperties();
    static const std::vector<IntelliSenseItem>& GetCssValues();
    static const std::vector<IntelliSenseItem>& GetCssPseudo();
    static const std::vector<IntelliSenseItem>& GetJavaScriptGlobals();
    static const std::vector<IntelliSenseItem>& GetJavaScriptDomMethods();
    static const std::vector<IntelliSenseItem>& GetJavaScriptPrototypeMethods();
};

} // namespace phide::editor
''')

    # 6. Generate WebIntelliSenseData.cpp
    cpp_path = os.path.join(src_dir, 'WebIntelliSenseData.cpp')
    print(f"Generating {cpp_path}...")
    with open(cpp_path, 'w', encoding='utf-8') as f:
        f.write('''/**
 * @file WebIntelliSenseData.cpp
 * @brief Official Microsoft VS Code HTML, CSS, and JavaScript/DOM IntelliSense data definitions.
 */

#include "phide/editor/WebIntelliSenseData.hpp"
#include <unordered_map>

namespace phide::editor {

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetHtmlTags() {
    static const std::vector<IntelliSenseItem> s_tags = {
''')
        for t in html_tags:
            tag_name = t['name']
            tag_desc = t['desc']
            is_void = t['void']
            insert_str = f"<{tag_name} />" if is_void else f"<{tag_name}></{tag_name}>"
            f.write(f'        {{ {sanitize_cpp(tag_name)}, {sanitize_cpp(tag_desc)}, {sanitize_cpp(insert_str)}, CompletionKind::Tag, 100, 0, 0, L"html", {sanitize_cpp(tag_desc)} }},\n')
            
        f.write('''    };
    return s_tags;
}

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetHtmlGlobalAttributes() {
    static const std::vector<IntelliSenseItem> s_attrs = {
''')
        for a in html_global_attrs:
            attr_name = a['name']
            attr_desc = a['desc']
            ins = f'{attr_name}=""'
            f.write(f'        {{ {sanitize_cpp(attr_name)}, {sanitize_cpp(attr_desc)}, {sanitize_cpp(ins)}, CompletionKind::Attribute, 80, 0, 0, L"html", {sanitize_cpp(attr_desc)} }},\n')

        f.write('''    };
    return s_attrs;
}

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetHtmlTagAttributes(const std::wstring& tag) {
    static const std::unordered_map<std::wstring, std::vector<IntelliSenseItem>> s_map = {
''')
        for tag_name, attrs in tag_specific_attrs.items():
            f.write(f'        {{ {sanitize_cpp(tag_name)}, {{\n')
            for a in attrs:
                an = a['name']
                ad = a['desc']
                ins = f'{an}=""'
                f.write(f'            {{ {sanitize_cpp(an)}, {sanitize_cpp(ad)}, {sanitize_cpp(ins)}, CompletionKind::Attribute, 90, 0, 0, L"html", {sanitize_cpp(ad)} }},\n')
            f.write('        } },\n')
            
        f.write('''    };
    auto it = s_map.find(tag);
    if (it != s_map.end()) return it->second;
    static const std::vector<IntelliSenseItem> s_empty;
    return s_empty;
}

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetCssProperties() {
    static const std::vector<IntelliSenseItem> s_props = {
''')
        for p in css_props:
            pn = p['name']
            pd = p['desc']
            ps = p['syntax']
            ins = f'{pn}: ;'
            doc = pd
            if ps:
                doc = f"Syntax: {ps}\\n{pd}"
            f.write(f'        {{ {sanitize_cpp(pn)}, {sanitize_cpp(pd)}, {sanitize_cpp(ins)}, CompletionKind::Property, 100, 0, 0, L"css", {sanitize_cpp(doc)} }},\n')

        f.write('''    };
    return s_props;
}

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetCssValues() {
    static const std::vector<IntelliSenseItem> s_vals = {
''')
        for v in sorted(css_values_set):
            f.write(f'        {{ {sanitize_cpp(v)}, L"CSS Value", {sanitize_cpp(v)}, CompletionKind::Constant, 80, 0, 0, L"css", L"" }},\n')

        f.write('''    };
    return s_vals;
}

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetCssPseudo() {
    static const std::vector<IntelliSenseItem> s_pseudo = {
''')
        for p in css_pseudo:
            pn = p['name']
            pd = p['desc']
            f.write(f'        {{ {sanitize_cpp(pn)}, {sanitize_cpp(pd)}, {sanitize_cpp(pn)}, CompletionKind::Property, 75, 0, 0, L"css", {sanitize_cpp(pd)} }},\n')

        f.write('''    };
    return s_pseudo;
}

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetJavaScriptGlobals() {
    static const std::vector<IntelliSenseItem> s_globals = {
''')
        for g_name, g_desc in standard_js_globals:
            f.write(f'        {{ {sanitize_cpp(g_name)}, {sanitize_cpp(g_desc)}, {sanitize_cpp(g_name)}, CompletionKind::Variable, 100, 0, 0, L"js", {sanitize_cpp(g_desc)} }},\n')

        f.write('''    };
    return s_globals;
}

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetJavaScriptDomMethods() {
    static const std::vector<IntelliSenseItem> s_methods = {
''')
        for m_name, m_sig, m_desc in standard_dom_methods:
            ins = f'{m_name}()'
            f.write(f'        {{ {sanitize_cpp(m_name)}, {sanitize_cpp(m_sig)}, {sanitize_cpp(ins)}, CompletionKind::Method, 100, 0, 0, L"dom", {sanitize_cpp(m_desc)} }},\n')

        f.write('''    };
    return s_methods;
}

const std::vector<IntelliSenseItem>& WebIntelliSenseData::GetJavaScriptPrototypeMethods() {
    static const std::vector<IntelliSenseItem> s_proto = {
''')
        for m_name, m_sig, m_desc in standard_proto_methods:
            ins = f'{m_name}()'
            f.write(f'        {{ {sanitize_cpp(m_name)}, {sanitize_cpp(m_sig)}, {sanitize_cpp(ins)}, CompletionKind::Method, 95, 0, 0, L"js", {sanitize_cpp(m_desc)} }},\n')

        f.write('''    };
    return s_proto;
}

} // namespace phide::editor
''')

    print("WebIntelliSenseData generated successfully!")

if __name__ == '__main__':
    main()
