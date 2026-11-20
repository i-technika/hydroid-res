#!/usr/bin/env python3
"""Have the SDK's aapt2 compile thousands of strings into golden/aapt2/strings.json.

Each input is the XML inside one element, which decides how aapt2 reads it:

  string  <string name="...">              a string, @null, @empty or a reference; the format
                                           check of %s %s
  item    <item type="string" name="...">  also any literal, and no format check
  style   <item name="str"> of a <style>   as text for an attribute of format="string"

and the file records how `aapt2 dump resources` prints the value (with the indent of the lines
after the first taken off), or null where aapt2 rejects it, with the first error aapt2 gives of
that input on its own. The C++ test Aapt2TableTest loads the same strings and checks that
hydroid's ResourceTable dumps each the same, or fails with a first error that says what aapt2's
does, so CI needs no Android SDK. Inputs are hand-picked edge cases of whitespace, quotes,
escapes, tags and references, and seeded random mixes of them.

Usage: python scripts/golden/gen_aapt2_strings.py [--any-host]
Needs an Android SDK with the build-tools (aapt2) and platform gen_aapt2_values.py pins, on its
GOLDEN_HOST. With --any-host it runs on any OS and writes build/aapt2-<os>-<cpu>/ instead.
"""

import json
import os
import random
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from gen_aapt2_table import LINK_FLAGS
from gen_aapt2_values import BUILD_TOOLS, EXE, HOST, MACHINE, PLATFORM, out_path, sdk_tools

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "golden" / "aapt2" / "strings.json"
PACKAGE = "com.probe"
# What every input may refer to: @string/target and ?attr/str, which the style rows set.
PRELUDE = '<string name="target">t</string>\n<attr name="str" format="string"/>\n'
NAMESPACES = ('xmlns:xliff="urn:oasis:names:tc:xliff:document:1.2" '
              'xmlns:other="urn:example:other"')

EDGE_CASES = [
    # Whitespace: collapsed to one space, trimmed at the ends when there are no tags.
    "", " ", "plain", "  lead", "trail  ", "  both  ", "a   b", "a\tb", "a\nb", "a \n\t b",
    "\n  indented\n  lines\n", "a\u00a0b", "\u00a0nbsp\u00a0", "&#9;tab&#10;nl&#13;cr",
    # Quotes keep whitespace and are dropped.
    '"  quoted  "', 'x "  in  " y', '"unbalanced  quote', '"a"b"c"', '  "  "  ', '""',
    '"it\'s"', "\"a\nb\"",
    # Escapes.
    "\\n", "\\t", "\\'", '\\"', "\\\\", "\\@", "\\?", "\\#", "\\a\\b\\x\\z", "end\\",
    "\\u00e9", "\\u00E9x", "\\u0041\\u0042", "\\u004", "\\u12", "\\u", "\\uZZZZ", "\\u12x",
    "\\ud83d\\ude00", "a\\\nb", "\\u0020a", "a\\u0020", " \\n a ", "\\\\n", "it\\'s",
    # Apostrophes, which need a \ or quotes.
    "it's", "'", "a 'b' c",
    # Entities, CDATA and comments.
    "&lt;b&gt; &amp; &#x41;", "<![CDATA[  <b>raw</b>  ]]>", "a<!-- c -->b", "<!-- c -->  a",
    # Tags: spans in UTF-16 units, and the whitespace of each piece of text.
    "<b>bold</b>", "a <b>b</b> c", " <b> lead</b>  trail  ", "<b></b>x", "x<b></b>", "<b/>",
    "a<b/>b", "<b><i>x</i></b>", "a<b><i>c</i></b>", "a  <b>  b  </b>  c", '"a <b>b  b</b> c"',
    '"<b>x</b>"', "<b>x</b>   ", "a <b>b</b><i> c</i>", "😀<b>a</b>", "한<b>글</b>",
    '<font color="#f00" face="x y">f</font>', '<a href="http://x/?a=1&amp;b=2">link</a>',
    "<b>it\\'s</b>", "<b>it's</b>", "<b>@string/target</b>",
    # XLIFF, which is not a span, and tags of other namespaces, which are dropped.
    '<xliff:g id="n">%1$s</xliff:g>', 'a<xliff:g>b</xliff:g>  ', '<xliff:g>  a  </xliff:g>',
    'a  <xliff:g>  b</xliff:g>', '<xliff:x>x</xliff:x>', 'a<other:foo>b</other:foo>c',
    '<xliff:g><b>x</b></xliff:g>', '<xliff:g>a</xliff:g><xliff:g>b</xliff:g>',
    '<xliff:g><xliff:g>x</xliff:g></xliff:g>',
    # References, @null and @empty, and what only looks like one.
    "@string/target", "  @string/target  ", "@com.probe:string/target", "@*string/target",
    "@null", "@empty", " @null ", "?attr/str", "?str", "@+id/made", "@+id/1bad", "@hello",
    "@string/", "@colour/x", "\\@string/target", "@ string/target", "#fff", "5", "1.5",
    "true", "16dp", "0x10",
    # Format arguments.
    "%s", "%d", "%s %s", "%1$s %2$s", "%1$s %s", "%% %s %s", "%n %s", "%-5d %s", "%&lt;s %s",
    "%1s %2s", "%tY %s %s", "%s %%", "100%", "%", "%%%", "%1$s %1$s",
]

WORDS = ["a", "b", "word", "Hello", "x1", "é", "한글", "😀", "\u00a0", "%s", "%d", "%1$s",
         "%2$d", "%%", "%n", "%-3d", "100%", "#fff", "5", "1.5", "true", "16dp", "@", "?", "#"]
SPACES = [" ", "  ", "\t", "\n", "\n    ", " \n "]
ESCAPES = ["\\'", '\\"', "\\\\", "\\n", "\\t", "\\u00e9", "\\u0041", "\\u12", "\\@", "\\?",
           "\\#", "\\z"]
ENTITIES = ["&amp;", "&lt;", "&gt;", "&#x41;", "&#10;", "&#9;", "&quot;"]
# Not @+id/: text after it makes a name no id can have, which aapt2 reports as an error but builds
# anyway, and it reports only 20 errors a run.
STARTS = ["@string/target", "@null", "@empty", "?attr/str", "@hello", "\\@x", ""]
TAGS = ["b", "i", "u", 'font color="#f00"', 'xliff:g id="x"', "other:x"]


def random_text(rng: random.Random) -> str:
    out = [rng.choice(STARTS) if rng.random() < 0.15 else ""]
    stack = []
    for _ in range(rng.randint(0, 9)):
        r = rng.random()
        if r < 0.3:
            out.append(rng.choice(WORDS))
        elif r < 0.5:
            out.append(rng.choice(SPACES))
        elif r < 0.6:
            out.append('"')
        elif r < 0.7:
            out.append(rng.choice(ESCAPES))
        elif r < 0.75:
            out.append(rng.choice(ENTITIES))
        elif r < 0.78:
            out.append("'")
        elif r < 0.8:
            out.append("<!-- c -->")
        elif r < 0.9 and len(stack) < 3:
            tag = rng.choice(TAGS)
            if tag.startswith("xliff") and any(t.startswith("xliff") for t in stack):
                continue
            out.append(f"<{tag}>")
            stack.append(tag)
        elif stack:
            out.append(f"</{stack.pop().split()[0]}>")
    while stack:
        out.append(f"</{stack.pop().split()[0]}>")
    return "".join(out)


def inputs() -> list:
    """(tag, text) pairs, without duplicates."""
    rng = random.Random(11)
    texts = EDGE_CASES + [random_text(rng) for _ in range(1500)]
    pairs = []
    for text in texts:
        pairs += [("string", text), ("item", text), ("style", text)]
    for tag, text in pairs:
        if "\r" in text or "]]>" in text.replace("<![CDATA[  <b>raw</b>  ]]>", ""):
            sys.exit(f"bad input {text!r}")
    return list(dict.fromkeys(pairs))


def element(i: int, tag: str, text: str) -> str:
    if tag == "string":
        return f'<string name="s{i}">{text}</string>'
    if tag == "item":
        return f'<item type="string" name="s{i}">{text}</item>'
    return f'<style name="s{i}"><item name="str">{text}</item></style>'


def write_values(path: Path, pairs, keep) -> dict:
    """The values file of the kept inputs; returns the input of each line, for errors."""
    lines = {}
    with open(path, "w", encoding="utf-8", newline="\n") as xml:
        xml.write(f"<resources {NAMESPACES}>\n{PRELUDE}")
        line = 4
        for i, (tag, text) in enumerate(pairs):
            if i not in keep:
                continue
            e = element(i, tag, text)
            for n in range(e.count("\n") + 1):
                lines[line + n] = i
            line += e.count("\n") + 1
            xml.write(e + "\n")
        xml.write("</resources>\n")
    return lines


def build(aapt2: Path, jar: Path, tmp: Path, pairs, keep):
    """({rejected input: its errors, in order}, None), or ({}, dump) when the kept inputs all
    build."""
    res = tmp / "res" / "values"
    res.mkdir(parents=True, exist_ok=True)
    lines = write_values(res / "strings.xml", pairs, keep)
    result = subprocess.run([str(aapt2), "compile", "--dir", str(tmp / "res"), "-o",
                             str(tmp / "c.zip")], capture_output=True, text=True, encoding="utf-8")
    stage = "compile"
    if result.returncode == 0:
        stage = "link"
        (tmp / "AndroidManifest.xml").write_text(
            '<manifest xmlns:android="http://schemas.android.com/apk/res/android" '
            f'package="{PACKAGE}"/>\n')
        result = subprocess.run([str(aapt2), "link", "-I", str(jar), "--manifest",
                                 str(tmp / "AndroidManifest.xml"), *LINK_FLAGS, "-o",
                                 str(tmp / "out.apk"), str(tmp / "c.zip")],
                                capture_output=True, text=True, encoding="utf-8")
        if result.returncode == 0:
            dump = subprocess.run([str(aapt2), "dump", "resources", str(tmp / "out.apk")],
                                  check=True, capture_output=True, text=True,
                                  encoding="utf-8").stdout
            return {}, dump.replace("\r", "")
    failed = {}
    # An error runs to the next line aapt2 starts with error:, warn: or note:, as it prints the
    # newlines of a name it does not find: "resource attr/a\nb (aka com.probe:attr/a\nb) not found".
    errors = r"strings\.xml:(\d+): error: (.*?)(?=\n[^\n]*\b(?:error|warn|note): |\n*\Z)"
    for m in re.finditer(errors, result.stderr.replace("\r", ""), re.S):
        # aapt2 reports @+id/ of a name no id can have, but leaves the text a string and builds.
        if re.match(r"resource 'id/.*' has invalid entry name", m[2]):
            continue
        if int(m[1]) not in lines:
            sys.exit(f"aapt2 {stage} error outside an input:\n{result.stderr}")
        # Without the temporary folder a file name starts with, as gen_aapt2_table.py writes it.
        error = re.sub(r"[^\s']*[\\/]res[\\/]", "res/", m[2]).rstrip(".")
        failed.setdefault(lines[int(m[1])], []).append(error)
    if not failed:
        sys.exit(f"aapt2 {stage} failed:\n{result.stderr}")
    return failed, None


def first_error(aapt2: Path, jar: Path, tmp: Path, pairs, i: int) -> str:
    """The first error aapt2 gives of input i on its own, as the test loads each it rejects."""
    failed, dump = build(aapt2, jar, tmp / f"alone{i}", pairs, {i})
    if dump is not None:
        sys.exit(f"aapt2 rejects {pairs[i]!r} among the others, takes it on its own")
    return failed[i][0]


def values_of(dump: str, pairs) -> dict:
    """How the dump prints the value of each input: the entry's lines, without their indent."""
    out = {}
    blocks = re.split(r"\n(?=    resource |  type )", dump)
    for block in blocks:
        m = re.match(r"    resource 0x[0-9a-f]{8} (\w+)/s(\d+)\n      \(\) (.*)", block, re.S)
        if not m:
            continue
        i = int(m[2])
        indent = 8 if pairs[i][0] == "style" else 6
        # The dump indents every line with something on it, which empty lines are not.
        first, *rest = m[3].rstrip("\n").split("\n")
        for line in rest:
            if line and not line.startswith(" " * indent):
                sys.exit(f"unexpected indent in {block!r}")
        out[i] = "\n".join([first] + [line[indent:] for line in rest])
    return out


def main() -> None:
    out_file = out_path(OUT)
    tools, jar = sdk_tools("aapt2")
    aapt2 = tools / f"aapt2{EXE}"
    pairs = inputs()
    keep = set(range(len(pairs)))
    with tempfile.TemporaryDirectory() as tmp:
        while True:
            failed, dump = build(aapt2, jar, Path(tmp), pairs, keep)
            if dump is not None:
                break
            keep -= set(failed)
        rejected = sorted(set(range(len(pairs))) - keep)
        with ThreadPoolExecutor(os.cpu_count()) as pool:
            errors = dict(zip(rejected, pool.map(
                lambda i: first_error(aapt2, jar, Path(tmp), pairs, i), rejected)))
    values = values_of(dump, pairs)
    if set(values) != keep:
        sys.exit(f"the dump has {len(values)} of {len(keep)} inputs")

    out_file.parent.mkdir(parents=True, exist_ok=True)
    header = (f"Generated by scripts/golden/gen_aapt2_strings.py with build-tools {BUILD_TOOLS} "
              f"and {PLATFORM} on {HOST} ({MACHINE}). Do not edit by hand.")
    with open(out_file, "w", encoding="utf-8", newline="\n") as out:
        out.write(f'{{"generated": {json.dumps(header)},\n "package": "{PACKAGE}",\n'
                  f' "prelude": {json.dumps(PRELUDE)},\n "rows": [\n')
        # [tag, text, value], or [tag, text, null, first error] where aapt2 rejects it.
        rows = [json.dumps([tag, text, values[i]] if i in values else [tag, text, None, errors[i]],
                           ensure_ascii=False)
                for i, (tag, text) in enumerate(pairs)]
        out.write(",\n".join("  " + r for r in rows) + "\n]}\n")
    print(f"wrote {len(pairs)} strings ({len(pairs) - len(keep)} rejected) to "
          f"{out_file.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
