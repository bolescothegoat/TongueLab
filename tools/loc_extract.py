#!/usr/bin/env python3
"""Builds Languages/English.toml (the translation template) from the menu source files.

Every string literal of the given C++ files that can appear in the menu becomes one entry:   "English text" = "English text"
A translator copies English.toml, renames it (german.toml ...) and changes only the right hand sides.

    python loc_extract.py <out English.toml> <ModName> <source.cpp> [...] [--extra file] [--skip file]
(--extra: more texts, one per line; --skip: literals that are not menu text, one per line)

Which literals count: they contain a letter, are not file/ids/internal names (see SKIP), and do not sit in a logger:: / GetModuleHandle call.
Review the output once after changing the menu: a missing text just shows in English, an extra one is harmless.
"""
import re
import sys

SKIP_EXACT = {
    "TongueLab", "FaceLab", "auto", "_generic", "generic NPCs", "Skyrim.esm", "FaceLab.dll", "TongueLab.log", "TongueLab.toml",
}
SKIP_CONTEXT = ("ChannelIndex(", "PushID(", "BeginChild(", "logger::", "GetModuleHandleA", "SetSection(", "std::format(", "fs::path", "LookupByID", "GetINISetting", "#include")


def tokenize(src: str):
    """yield (literal_text, line_context) for every string literal, comments and char literals skipped"""
    i, n = 0, len(src)
    out = []
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
        elif c == '"':
            j = i + 1
            buf = []
            while j < n and src[j] != '"':
                if src[j] == "\\" and j + 1 < n:
                    e = src[j + 1]
                    buf.append({"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'"}.get(e, e))
                    j += 2
                else:
                    buf.append(src[j])
                    j += 1
            ls = src.rfind("\n", 0, i) + 1
            le = src.find("\n", i)
            out.append(("".join(buf), src[ls:le if le >= 0 else n], i))
            i = j + 1
        else:
            i += 1
    return out


def keep(text: str, ctx: str) -> bool:
    if len(text) < 2 or not re.search(r"[A-Za-z]", text):
        return False
    if text in SKIP_EXACT or "##" in text:
        return False
    if any(m in ctx for m in SKIP_CONTEXT):
        return False
    rest = re.sub(r"%[-+ #0-9.]*[A-Za-z]|%%|\{\}", "", text)
    if not re.search(r"[A-Za-z]", rest):
        return False  # only placeholders: "%s", "%.2f", "{}"
    return True


def toml_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r") + '"'


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(2)
    args = sys.argv[1:]
    extra, skip = [], set()
    for flag, target in (("--extra", extra), ("--skip", skip)):
        while flag in args:
            i = args.index(flag)
            path = args[i + 1]
            del args[i:i + 2]
            for ln in open(path, encoding="utf-8").read().splitlines():
                if ln.strip() and not ln.startswith("#"):
                    (target.append if isinstance(target, list) else target.add)(ln)
    out_path, mod, files = args[0], args[1], args[2:]
    seen, lines = set(skip), []
    for f in files:
        src = open(f, encoding="utf-8").read()
        lits = tokenize(src)
        lines.append(f"\n# ---- {f.replace(chr(92), '/').split('/')[-1]} ----")
        for text, ctx, _ in lits:
            if text in seen or not keep(text, ctx):
                continue
            seen.add(text)
            lines.append(f"{toml_str(text)} = {toml_str(text)}")
    if extra:
        lines.append("\n# ---- texts that come from outside the menu file ----")
        for text in extra:
            if text not in seen:
                seen.add(text)
                lines.append(f"{toml_str(text)} = {toml_str(text)}")
    header = f"""# {mod} - language template (English).
#
# HOW TO TRANSLATE: copy this file, rename the copy after the language (german.toml, french.toml, russian.toml ...), then change only the
# text on the RIGHT of each "=". Leave the left side exactly as it is: it is how the mod finds the line.
#   - Keep the placeholders as they are, same order: %s %d %.2f %+.0f %% and {{}}. A line whose placeholders differ is ignored (and logged).
#   - Delete a line, or leave the right side empty, to keep that text in English.
#   - Save the file as UTF-8. Choose the language in the mod's General page (Language), or leave it on Automatic: the mod then uses the
#     language the game itself is set to (sLanguage in Skyrim.ini: english, german, french, russian, spanish, italian, polish, japanese, czech, chinese).
#   - Cyrillic, Chinese, Japanese, Korean, Thai, Turkish, Polish and Vietnamese letters show as ???? until that script is switched on in
#     SKSE Menu Framework's own settings: Data/SKSE/Plugins/SKSEMenuFramework.ini, section [Fonts], e.g. EnableCyrillic = true (restart the game).
#
[meta]
name = "English"

[strings]"""
    with open(out_path, "w", encoding="utf-8", newline="\n") as o:
        o.write(header + "\n".join(lines) + "\n")
    print(f"{len(seen) - len(skip)} strings -> {out_path}")


if __name__ == "__main__":
    main()
