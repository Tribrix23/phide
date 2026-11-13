import json
import re
from pathlib import Path

BASE_DIR = Path(__file__).parent
REPO_ROOT = BASE_DIR.parent.parent

def strip_json_comments(text: str) -> str:
    out = []
    for line in text.splitlines():
        s = line.lstrip()
        if s.startswith("//"):
            continue
        out.append(line)
    return "\n".join(out)

_DOLLAR_SENTINEL = "\x00PHIDE_LITERAL_DOLLAR\x00"
_TM_VARS = {
    "TM_SELECTED_TEXT", "TM_CURRENT_LINE", "TM_CURRENT_WORD", "TM_LINE_INDEX",
    "TM_LINE_NUMBER", "TM_FILENAME", "TM_FILENAME_BASE", "TM_DIRECTORY",
    "TM_FILEPATH", "RELATIVE_FILEPATH", "CLIPBOARD", "WORKSPACE_NAME",
    "WORKSPACE_FOLDER", "CURSOR_INDEX", "CURSOR_NUMBER",
    "CURRENT_YEAR", "CURRENT_YEAR_SHORT", "CURRENT_MONTH", "CURRENT_MONTH_NAME",
    "CURRENT_MONTH_NAME_SHORT", "CURRENT_DATE", "CURRENT_DAY_NAME",
    "CURRENT_DAY_NAME_SHORT", "CURRENT_HOUR", "CURRENT_MINUTE", "CURRENT_SECOND",
    "CURRENT_SECONDS_UNIX", "CURRENT_TIMEZONE_OFFSET", "RANDOM", "RANDOM_HEX", "UUID",
    "BLOCK_COMMENT_START", "BLOCK_COMMENT_END", "LINE_COMMENT",
}

def _strip_brace_transforms(line: str) -> str:
    """Resolve TextMate variable regex transforms such as ${TM_FILENAME_BASE/...} cleanly."""
    out = []
    i = 0
    n = len(line)
    while i < n:
        if line[i:i+2] == "${":
            # Scan balanced braces
            depth = 1
            j = i + 2
            while j < n and depth > 0:
                if line[j] == "\\" and j + 1 < n:
                    j += 2
                    continue
                if line[j] == "{":
                    depth += 1
                elif line[j] == "}":
                    depth -= 1
                j += 1
            if depth == 0:
                inner = line[i+2:j-1]
                # Regex transform only applies to TextMate vars ${TM_.../...} or tabstop ${N/...}
                m = re.match(r"^([A-Z_]+|\d+)/", inner)
                if m:
                    var_name = m.group(1)
                    if var_name == "TM_FILENAME_BASE":
                        out.append("_FILE_H")
                    elif var_name in _TM_VARS:
                        out.append("")
                    i = j
                    continue
        out.append(line[i])
        i += 1
    return "".join(out)

def clean_snippet_body(body_lines) -> str:
    if isinstance(body_lines, str):
        lines = body_lines.splitlines()
    else:
        lines = list(body_lines)

    # First collect tabstop defaults across the entire snippet (e.g. ${1:ClassName} -> $1)
    defaults = {}
    raw_joined = "\n".join(lines).replace("\\$", _DOLLAR_SENTINEL)
    for m in re.finditer(r"\$\{(\d+):([^{}]+)\}", raw_joined):
        defaults.setdefault(m.group(1), m.group(2))

    cleaned = []
    for line in lines:
        # Preserve escaped literal dollar \$ (PHP variables like \$this->var = \$var;) BEFORE stripping tabstops
        line = line.replace("\\$", _DOLLAR_SENTINEL)
        line = _strip_brace_transforms(line)

        # Iteratively resolve nested ${N|a,b,c|} and ${N:default} innermost-first
        for _ in range(12):
            new_line = re.sub(
                r"\$\{\d+\|([^}|]*)\|\}",
                lambda m: m.group(1).split(",")[0],
                line,
            )
            def _sub_placeholder(m):
                defaults.setdefault(m.group(1), m.group(2))
                return m.group(2)
            new_line = re.sub(r"\$\{(\d+):([^{}]*)\}", _sub_placeholder, new_line)
            new_line = re.sub(r"\$\{\d+\}", "", new_line)
            def _sub_tm(m):
                vname, dflt = m.group(1), (m.group(2) or "")
                if vname in _TM_VARS:
                    return dflt if dflt else ("Component" if "FILENAME_BASE" in vname else "")
                return m.group(0)
            new_line = re.sub(r"\$\{([A-Z_]+)(?::([^{}]*))?\}", _sub_tm, new_line)
            if "`" not in new_line:
                # Unwrap bare lowercase VS Code/TextMate snippet placeholders like ${type}
                new_line = re.sub(r"\$\{([a-z_][a-zA-Z0-9_]*)\}", r"\1", new_line)
            if new_line == line:
                break
            line = new_line

        # Resolve back-referenced numeric tabstops ($1, $2, ...) cleanly, removing empty $0
        def _sub_num(m):
            idx = m.group(1)
            if idx == "0":
                return ""
            return defaults.get(idx, "")
        line = re.sub(r"\$(\d+)", _sub_num, line)
        line = re.sub(r"\$(TM_[A-Z_]+|CLIPBOARD|UUID|RANDOM(?:_HEX)?)\b", "", line)

        # Restore literal $ signs and normalize tabs/spacing
        line = line.replace(_DOLLAR_SENTINEL, "$").replace("\t", "    ")
        line = re.sub(r"\bdef fname\(", "def name(", line)
        # Fix minor empty comparison operand in PHP for-loop snippet: `$i < ;` -> `$i < $count;`
        line = line.replace("$i < ;", "$i < $count;")
        # Collapse extra internal double spaces introduced by empty choices without altering leading indentation
        lead_len = len(line) - len(line.lstrip(" "))
        line = line[:lead_len] + re.sub(r"  +", " ", line[lead_len:])
        cleaned.append(line.rstrip())
    return "\n".join(cleaned).strip("\r\n")

def c_escape_multiline(s: str) -> str:
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "")
    s = s.replace("\n", "\\n")
    return "".join(ch if (32 <= ord(ch) < 127 or ch == "\n") else " " for ch in s)

def load_snippet_file(path: Path, lang_tag: str) -> list:
    if not path.exists():
        return []
    raw = strip_json_comments(path.read_text(encoding="utf-8", errors="replace"))
    try:
        data = json.loads(raw)
    except Exception as e:
        print(f"Warning: could not parse {path.name}: {e}")
        return []
    entries = []
    for name, item in data.items():
        if not isinstance(item, dict):
            continue
        prefix = item.get("prefix", "")
        if isinstance(prefix, list):
            prefixes = [str(p).strip() for p in prefix if str(p).strip()]
        else:
            prefixes = [str(prefix).strip()] if str(prefix).strip() else []
        body = clean_snippet_body(item.get("body", []))
        desc = str(item.get("description", name)).strip()
        if not body or len(body) < 4:
            continue
        for p in prefixes:
            if len(p) >= 2:
                entries.append((lang_tag, p, body, desc or name))
    return entries

def _first_existing(*paths: Path) -> Path:
    for p in paths:
        if p.exists():
            return p
    return paths[0]

def main():
    sources = [
        (_first_existing(REPO_ROOT / "vendor" / "php-intellisense" / "php.code-snippets",
                         REPO_ROOT / "vendor" / "vscode-php-intellisense" / "php.code-snippets"), "php"),
        (BASE_DIR / "friendly_php.json", "php"),
        (BASE_DIR / "cstrap_python_base.json", "py"),
        (BASE_DIR / "cstrap_python_comprehension.json", "py"),
        (BASE_DIR / "cstrap_python_unittest.json", "py"),
        (BASE_DIR / "friendly_python.json", "py"),
        (BASE_DIR / "friendly_cpp.json", "cpp"),
        (BASE_DIR / "friendly_rust.json", "rs"),
        (BASE_DIR / "friendly_sql.json", "sql"),
        (BASE_DIR / "javascript.code-snippets", "js"),
        (BASE_DIR / "typescript.code-snippets", "ts"),
        (_first_existing(REPO_ROOT / "vendor" / "react-intellisense" / "xabikos_react_snippets.json",
                         REPO_ROOT / "vendor" / "vscode-react-intellisense" / "xabikos_react_snippets.json"), "react"),
    ]
    all_entries = []
    seen = set()
    for path, lang in sources:
        loaded = load_snippet_file(path, lang)
        print(f"Loaded {len(loaded)} prediction templates from {path.name} ({lang})")
        for lang_tag, prefix, body, desc in loaded:
            key = (lang_tag, prefix)
            if key in seen:
                continue
            seen.add(key)
            all_entries.append((lang_tag, prefix, body, desc))

    out_hpp = REPO_ROOT / "include" / "phide" / "editor" / "ZedPredictionData.hpp"
    lines = [
        "#pragma once",
        "// AUTO-GENERATED FROM vendor/zed-edit-prediction/ AND vendor/vscode-php-intellisense/",
        "// DO NOT EDIT MANUALLY — RUN vendor/zed-edit-prediction/build_zed_predictions.py",
        "",
        "#include <cstddef>",
        "",
        "namespace phide::editor {",
        "",
        "struct ZedPredictionTemplate {",
        "    const wchar_t* lang;",
        "    const wchar_t* trigger;",
        "    const wchar_t* expansion;",
        "    const wchar_t* summary;",
        "};",
        "",
        f"inline constexpr size_t kZedPredictionTemplateCount = {len(all_entries)};",
        "inline constexpr ZedPredictionTemplate kZedPredictionTemplates[] = {",
    ]
    for lang_tag, prefix, body, desc in all_entries:
        lines.append(
            f'    {{ L"{c_escape_multiline(lang_tag)}", L"{c_escape_multiline(prefix)}", '
            f'L"{c_escape_multiline(body)}", L"{c_escape_multiline(desc[:120])}" }},'
        )
    lines.append("};")
    lines.append("")
    lines.append("} // namespace phide::editor")
    out_hpp.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {len(all_entries)} templates to {out_hpp}")

if __name__ == "__main__":
    main()
