#!/usr/bin/env python3
"""Make src/core/LocaleDataTables.inc from AOSP's LocaleDataLookup.cpp.

Android matches a locale by its script and picks between regions by their parents, from tables
that AOSP's tools/localedata/extract_icu_data.py makes of ICU's CLDR data and writes as C++
switch statements (frameworks/base/libs/androidfw/LocaleDataLookup.cpp). This reads those
switches and writes the same data as sorted arrays, which src/core/LocaleData.cpp searches:

  - the likely script of a language, or of a language and region
  - the representative locales: the one a language and script stand for
  - the parent of a locale in a script, where it is not the locale without its region
  - the depth of the deepest tree of parents

Usage: python scripts/gen_locale_data.py [<LocaleDataLookup.cpp>]
Without a file it downloads the one of COMMIT from android.googlesource.com. BRANCH is the
android<N>-release the matching code in src/core/ConfigMatch.cpp and LocaleData.cpp was ported
from, and COMMIT the commit of it golden/aosp/framework_res_dirs.txt lists. Raise both with
kEmulatedSdkVersion (include/hydroid/res/Version.h), port the matching code again and make the
tables again: SdkBaselineTest wants the header of the tables, and the files that name the release
they were ported from, to say these.
"""

import base64
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "src" / "core" / "LocaleDataTables.inc"
BRANCH = "android17-release"
COMMIT = "94b4c163b7dfe5ce3607f7bb8456f9573f7de57d"
SOURCE = "libs/androidfw/LocaleDataLookup.cpp"
URL = f"https://android.googlesource.com/platform/frameworks/base/+/{COMMIT}/{SOURCE}?format=TEXT"


def function_body(text, signature):
    """The text of the function whose definition starts with `signature`, up to its last brace."""
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def cases(body, suffix):
    """[(keys, returned)] for each run of `case 0x...<suffix>:` labels and the return after it."""
    out = []
    keys = []
    for line in body.splitlines():
        line = line.strip()
        m = re.match(rf"case 0x([0-9A-Fa-f]+){suffix}:", line)
        if m:
            keys.append(int(m.group(1), 16))
            continue
        m = re.match(r"return (.+);", line)
        if m and keys:
            out.append((keys, m.group(1)))
            keys = []
    if keys:
        raise SystemExit("case labels without a return")
    return out


def rows(values, fmt, per_line):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + " ".join(fmt(v) + "," for v in values[i : i + per_line]))
    return "\n".join(lines)


def main():
    if len(sys.argv) > 2:
        raise SystemExit(__doc__)
    if len(sys.argv) == 2:
        text = Path(sys.argv[1]).read_text(encoding="utf-8")
    else:
        with urllib.request.urlopen(URL) as response:
            text = base64.b64decode(response.read()).decode("utf-8")

    scripts = re.findall(r"/\* *\d+ *\*/ \{'(.)', '(.)', '(.)', '(.)'\},", text)
    scripts = ["".join(s) for s in scripts]
    if not scripts or len(scripts) > 255:
        raise SystemExit(f"{len(scripts)} script codes")

    likely = {}
    for keys, returned in cases(function_body(text, "const char* lookupLikelyScript("), "u"):
        m = re.fullmatch(r"SCRIPT_CODES\[ *(\d+)u\]", returned)
        if not m or int(m.group(1)) >= len(scripts):
            raise SystemExit(f"lookupLikelyScript returns {returned}")
        for key in keys:
            if key in likely:
                raise SystemExit(f"likely script given twice: {key:#x}")
            likely[key] = int(m.group(1))

    representative = []
    for keys, returned in cases(function_body(text, "bool isLocaleRepresentative("), "LLU"):
        if returned != "true":
            raise SystemExit(f"isLocaleRepresentative returns {returned}")
        representative += keys
    representative.sort()
    if len(set(representative)) != len(representative):
        raise SystemExit("representative locale given twice")

    # findParentLocalePackedKey: the script, packed, of each find<Script>Parent.
    parents = []
    dispatch = function_body(text, "uint32_t findParentLocalePackedKey(")
    for keys, returned in cases(dispatch, "u"):
        m = re.fullmatch(r"(find\w+Parent)\(packed_lang_region\)", returned)
        if not m or len(keys) != 1:
            raise SystemExit(f"findParentLocalePackedKey returns {returned}")
        table = {}
        for children, parent in cases(function_body(text, f"static uint32_t {m.group(1)}("), "u"):
            pm = re.fullmatch(r"0x([0-9A-Fa-f]+)u", parent)
            if not pm:
                raise SystemExit(f"{m.group(1)} returns {parent}")
            for child in children:
                table[child] = int(pm.group(1), 16)
        parents.append((keys[0], sorted(table.items())))
    parents.sort()
    defined = len(re.findall(r"static uint32_t find\w+Parent\(", text))
    if defined != len(parents):
        raise SystemExit(f"{defined} find<Script>Parent functions, {len(parents)} used")

    m = re.search(r"uint32_t getMaxAncestorTreeDepth\(\) \{\s*return (\d+);", text)
    if not m:
        raise SystemExit("no getMaxAncestorTreeDepth")
    depth = int(m.group(1))

    locales = sorted(likely)
    out = [
        "// Generated by scripts/gen_locale_data.py. Do not edit.",
        "//",
        f"// The tables of AOSP's frameworks/base/{SOURCE},",
        f"// {BRANCH} at {COMMIT},",
        "// which tools/localedata/extract_icu_data.py there makes of ICU's CLDR data:",
        "//   Copyright (C) 2025 The Android Open Source Project",
        "//   Licensed under the Apache License, Version 2.0",
        "// A locale is packed as android::packLocale packs one: the two bytes of the language as",
        "// ResTable_config holds it, then the two of the region. A script is its four letters.",
        "",
        f"// {len(scripts)} scripts.",
        "constexpr char kScriptCodes[][4] = {",
        rows(scripts, lambda s: "{" + ", ".join(f"'{c}'" for c in s) + "}", 4),
        "};",
        "",
        "// lookupLikelyScript: kLikelyScriptLocales[i], in order, has the script",
        f"// kScriptCodes[kLikelyScripts[i]]. {len(locales)} locales.",
        "constexpr uint32_t kLikelyScriptLocales[] = {",
        rows(locales, lambda v: f"0x{v:08X}u", 7),
        "};",
        "constexpr uint8_t kLikelyScripts[] = {",
        rows([likely[v] for v in locales], lambda v: f"{v:3}", 18),
        "};",
        "",
        "// isLocaleRepresentative: a locale, then its script, in order.",
        f"// {len(representative)} locales.",
        "constexpr uint64_t kRepresentativeLocales[] = {",
        rows(representative, lambda v: f"0x{v:016X}u", 4),
        "};",
        "",
        "// find<Script>Parent: {locale, parent}, in the order of the locales.",
        "struct LocaleParent {",
        "  uint32_t locale;",
        "  uint32_t parent;",
        "};",
    ]
    for script, table in parents:
        name = script.to_bytes(4, "big").decode("ascii")
        out += [
            f"// {name}: {len(table)} locales.",
            f"constexpr LocaleParent kParents{script:08X}[] = {{",
            rows(table, lambda v: f"{{0x{v[0]:08X}u, 0x{v[1]:08X}u}}", 3),
            "};",
        ]
    out += [
        "",
        "// findParentLocalePackedKey: the table of each script, in the order of the scripts.",
        "struct ScriptParents {",
        "  uint32_t script;",
        "  const LocaleParent* parents;",
        "  size_t size;",
        "};",
        "constexpr ScriptParents kScriptParents[] = {",
    ]
    for script, table in parents:
        out.append(f"    {{0x{script:08X}u, kParents{script:08X}, {len(table)}}},")
    out += [
        "};",
        "",
        "// getMaxAncestorTreeDepth.",
        f"constexpr size_t kMaxAncestorTreeDepth = {depth};",
        "",
    ]
    OUT.write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(
        f"{OUT.relative_to(ROOT)}: {len(scripts)} scripts, {len(locales)} likely scripts, "
        f"{len(representative)} representative locales, "
        f"{sum(len(t) for _, t in parents)} parents in {len(parents)} scripts, depth {depth}"
    )


if __name__ == "__main__":
    main()
