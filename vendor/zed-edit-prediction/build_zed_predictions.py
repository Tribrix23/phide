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

def clean_snippet_body(body_lines) -> str:
    if isinstance(body_lines, str):
        lines = body_lines.splitlines()
    else:
        lines = list(body_lines)
    cleaned = []
    for line in lines:
        # Convert ${1:default} -> default, $1 -> ""
        line = re.sub(r"\$\{\d+:([^}|]+)\}", r"\1", line)
        line = re.sub(r"\$\{\d+\|([^,|]+)[^}]*\}", r"\1", line)
        line = re.sub(r"\$\{?[A-Za-z0-9_]+\}?", "", line)
        line = line.replace("\\$", "$").replace("\t", "    ")
        cleaned.append(line)
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

def main():
    sources = [
        (REPO_ROOT / "vendor" / "vscode-php-intellisense" / "php.code-snippets", "php"),
        (BASE_DIR / "friendly_php.json", "php"),
        (BASE_DIR / "friendly_python.json", "py"),
        (BASE_DIR / "friendly_cpp.json", "cpp"),
        (BASE_DIR / "friendly_rust.json", "rs"),
        (BASE_DIR / "friendly_sql.json", "sql"),
        (BASE_DIR / "javascript.code-snippets", "js"),
        (BASE_DIR / "typescript.code-snippets", "ts"),
        (REPO_ROOT / "vendor" / "vscode-react-intellisense" / "xabikos_react_snippets.json", "react"),
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
