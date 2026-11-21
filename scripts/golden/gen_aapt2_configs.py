#!/usr/bin/env python3
"""Have the SDK's aapt2 parse thousands of qualifier strings into golden/aapt2/configs.tsv.

Each input is the part of a resource directory name after its type, "ko-rKR" of values-ko-rKR.
It is compiled as res/values-<input>/ and linked into an APK, and `aapt2 dump resources` prints
the configuration aapt2 made of it, in aapt2's canonical form ("hdpi" -> "hdpi-v4"). The file
records that form, empty for the default configuration, or "-" when aapt2 rejected the input: a
compile error ("invalid configuration", or a minor version before v36), or a link error, as for
some locales compile takes.

The inputs are every directory of AOSP's framework res/ (golden/aosp/framework_res_dirs.txt) and
of the golden app, hand-picked edge cases for each qualifier, every numeric qualifier with every
number of numeric_qualifiers.tsv, and seeded random combinations, in order and out of it. The
C++ test Aapt2ConfigTest checks hydroid's parser against this file, so CI needs no Android SDK.

Usage: python scripts/golden/gen_aapt2_configs.py [--any-host]
Needs an Android SDK with the build-tools (aapt2) and platform gen_aapt2_values.py pins, on its
GOLDEN_HOST. With --any-host it runs on any OS and writes build/aapt2-<os>-<cpu>/configs.tsv
instead.
"""

import os
import random
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from gen_aapt2_values import BUILD_TOOLS, EXE, HOST, MACHINE, PLATFORM, out_path, sdk_tools

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "golden" / "aapt2" / "configs.tsv"
AOSP_DIRS = ROOT / "golden" / "aosp" / "framework_res_dirs.txt"
APP_RES = ROOT / "golden" / "app" / "src" / "main" / "res"
NUMERIC = Path(__file__).with_name("numeric_qualifiers.tsv")


def numeric_edges() -> list:
    """Each template of numeric_qualifiers.tsv with each of its values, which Aapt2ConfigTest
    reads too."""
    table = {}
    for line in NUMERIC.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            key, *cells = line.split("\t")
            table[key] = cells
    return [t.replace("{}", v) for t in table["templates"] for v in table["values"]]

# Each qualifier in aapt2's order, with values it takes and some it does not.
QUALIFIERS = [
    ("mcc", ["mcc310", "mcc001", "mcc999", "mcc450"]),
    ("mnc", ["mnc260", "mnc0", "mnc00", "mnc000", "mnc1", "mnc004", "mnc999"]),
    ("locale", ["en", "ko", "fil", "en-rUS", "ko-rKR", "zh-rTW", "en-rXC", "pt-rBR", "b+en",
                "b+sr+Latn", "b+es+419", "b+zh+Hant+TW", "b+de+CH+1901", "b+en+US+POSIX",
                "b+sl+rozaj", "b+fil+PH", "iw", "in"]),
    ("gender", ["feminine", "masculine", "neuter"]),
    ("layoutdir", ["ldltr", "ldrtl"]),
    ("sw", ["sw0dp", "sw320dp", "sw600dp", "sw720dp", "sw65535dp"]),
    ("w", ["w0dp", "w320dp", "w720dp", "w1280dp"]),
    ("h", ["h0dp", "h480dp", "h720dp"]),
    ("size", ["small", "normal", "large", "xlarge"]),
    ("long", ["long", "notlong"]),
    ("round", ["round", "notround"]),
    ("widecg", ["widecg", "nowidecg"]),
    ("hdr", ["highdr", "lowdr"]),
    ("orientation", ["port", "land", "square"]),
    ("uimode", ["car", "desk", "television", "appliance", "watch", "vrheadset"]),
    ("night", ["night", "notnight"]),
    ("density", ["ldpi", "mdpi", "tvdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi", "nodpi", "anydpi",
                 "400dpi", "1dpi", "65533dpi"]),
    ("touch", ["notouch", "stylus", "finger"]),
    ("keyshidden", ["keysexposed", "keyshidden", "keyssoft"]),
    ("keyboard", ["nokeys", "qwerty", "12key"]),
    ("navhidden", ["navexposed", "navhidden"]),
    ("navigation", ["nonav", "dpad", "trackball", "wheel"]),
    ("screensize", ["640x480", "640x640", "640x", "0x0", "65535x1"]),
    ("version", ["v0", "v1", "v4", "v21", "v34", "v36", "v36.1", "v65535"]),
]

EDGE_CASES = [
    "",
    # Each value alone, and "any", which every qualifier but the grammatical gender takes.
    *[value for _, values in QUALIFIERS for value in values],
    "any", "any-any", "any-any-any", "any-any-any-any", "en-any", "any-hdpi", "land-any",
    "any-en", "feminine-any", "en-any-any", "mcc310-any", "any-mnc260",
    "-".join(["any"] * 23), "-".join(["any"] * 24), "-".join(["any"] * 22) + "-v21",
    # Numbers: how many digits, zero, and past 16 bits, which numeric_qualifiers.tsv has.
    "mcc000", "mcc31", "mcc3100", "mcc", "mccabc", "mnc", "mnc1000", "mnc01x",
    *numeric_edges(),
    "sw1dp", "swdp", "sw600", "sw600px", "sw-600dp", "sw600dpx", "wdp", "hdp",
    "0dpi", "65534dpi", "dpi", "xxxxhdpi", "120dpi", "160dpi", "640dpi", "-1dpi",
    "480x640", "x480", "65536x0", "65536x", "131072x0", "131072x5", "1x0", "640x480x2",
    "640+480", "v", "v-1", "v021", "21",
    # Minor versions, which aapt2 takes from v36 on.
    "v21.1", "v35.1", "v36.0", "v36.1", "v36.01", "v37.2", "v36.-hdpi", "v.1", "v36.1.1",
    "v36.65535", "v36.65536", "v36.x", "v36-1", "hdpi-v36.1", "anydpi-v36.2", "watch-v36.1",
    # In AOSP main's framework res/ (1cdfff55, its last public commit), but in no release branch.
    "watch-v36",
    # Locales: the legacy form, BCP 47 with b+, and what each rejects.
    "eng", "e", "abcd", "en-US", "en-r419", "en-rUSA", "en-r1", "en-r12", "en-rus-rgb",
    "ca", "cat", "car", "ca-car", "en-car", "b+", "b+e", "b+english", "b+en+US", "b+en+abc",
    "b+en+123", "b+en+1234", "b+en+abcde", "b+en+abcdefgh", "b+en+abcdefghi", "b+en+a",
    "b+sr+Latn+RS", "b+sr+Latn+RS+x", "b+sr+1234+RS", "b+sr+Lat+RS", "b+de+CH+19",
    "b+de+CH+19011901x", "b+en+Latn+US+posix", "b+ar+u+nu+latn", "b+en-rUS", "b+en+US-ldrtl",
    "b+zh+Hant", "b+zh+hant+hk", "b+es+419+valencia", "b+ca+ES+valencia", "en+US",
    "b+en++US", "b++en", "b+en+",
    # Four subtags with a short second one: aapt2 keeps the region and drops the two after it.
    "b+de+DE+1901+1996", "b+ca+ES+valencia+1996", "b+es+419+1901+1996", "b+de+DE+x+abcdefghi",
    # Beside them: two variants without a region, of which three subtags keep the second, and
    # five subtags, which fail.
    "b+de+1901+1996", "b+sr+Latn+RS+ekavsk+1996", "b+de+DE+1901+1996+abcde",
    # Subtags BCP 47 does not allow, which aapt2 cuts short, packs wrongly or moves about.
    "b+e1", "b+e1+US", "en-r+x", "b+en+abcd", "b+en+xyz", "b+en+aaa", "b+sr+Latn+RS+",
    "b+sr+Latn++x", "b+sr+Latnx+RS+x", "b+sr+Lat+RS+x", "b+sr+Latn+R+x", "b+sr+Latn+abc+x",
    "b+sr+Latn+419+x", "b+sr+Latn+RS+abcdefghi", "b+de+CH+abcdefghi", "b+abc+Latn",
    "b+zh+Hant+TW+", "b+zh+Hant++", "b+fil+PH+1901", "b+de+abc+x+y", "b+de+DE+1901+",
    "v0.0", "v0.1", "v35.0", "v36.0-hdpi", "anydpi-v4", "anydpi-v21", "anydpi-v22",
    # aapt2 checks the minor version after it raises the version for a qualifier.
    "anydpi-v4.1", "feminine-v33.1", "feminine-v35.1", "watch-v7.3", "round-v36.1",
    # Out of order, twice, or with empty tokens.
    "hdpi-land", "land-port", "hdpi-hdpi", "v21-hdpi", "en-mcc310", "ldrtl-en", "night-car",
    "feminine-fr", "ldrtl-feminine", "-", "--", "hdpi-", "-hdpi", "hdpi--v21", "car-en",
    "fr-feminine", "fr-feminine-ldrtl", "en-neuter-v21",
    # "normal" is a screen size, not the normal UI mode, which has no qualifier (#55).
    "normal-car", "land-normal", "port-normal", "car-normal", "night-normal",
    # Everything at once.
    "mcc310-mnc260-en-rUS-feminine-ldrtl-sw600dp-w720dp-h480dp-large-long-round-widecg-highdr-"
    "land-car-night-xhdpi-finger-keysexposed-qwerty-navexposed-dpad-640x480-v26",
    "mcc310-mnc260-b+sr+Latn+RS-ldltr-sw320dp-w320dp-h320dp-small-notlong-notround-nowidecg-"
    "lowdr-port-watch-notnight-anydpi-notouch-keyssoft-nokeys-navhidden-nonav-v36",
    # Upper case: aapt2 lowercases each token first. (The lowercase twins are left out: on
    # Windows and macOS the two would be the same directory.)
    "PORT-TVDPI", "Ko-RKp", "B+SR+LATN+ME", "SW601DP", "V22", "401DPI", "FEMININE-LDRTL",
    "MCC311-MNC261", "XxHdPi-v5", "B+ES+419-NIGHT", "NoTouch-12KEY", "Hdpi-V3",
]


def random_inputs(rng: random.Random, count: int) -> list:
    out = []
    while len(out) < count:
        tokens = [rng.choice(values) for _, values in QUALIFIERS if rng.random() < 0.25]
        if not tokens:
            continue
        r = rng.random()
        if r < 0.1 and len(tokens) >= 2:  # two tokens out of order
            i, j = rng.sample(range(len(tokens)), 2)
            tokens[i], tokens[j] = tokens[j], tokens[i]
        elif r < 0.15:  # a token twice
            i = rng.randrange(len(tokens))
            tokens.insert(i, tokens[i])
        elif r < 0.25:  # a wildcard somewhere
            tokens.insert(rng.randrange(len(tokens) + 1), "any")
        text = "-".join(tokens)
        if rng.random() < 0.05:
            text = "".join(c.upper() if rng.random() < 0.5 else c for c in text)
        if len(text) <= 120:
            out.append(text)
    return out


def qualifiers_of(dirs) -> list:
    """The qualifiers of each directory name, "" for none."""
    return [d.split("-", 1)[1] if "-" in d else "" for d in dirs]


def inputs() -> list:
    aosp = [line.strip() for line in AOSP_DIRS.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.startswith("#")]
    app = sorted(p.name for p in APP_RES.iterdir() if p.is_dir())
    texts = qualifiers_of(aosp) + qualifiers_of(app) + EDGE_CASES
    texts += random_inputs(random.Random(7), 3000)
    seen, out = {}, []
    for text in texts:
        # A directory name: no path separators or characters Windows refuses, no trailing dot
        # or space, and only one spelling of each name up to case.
        if re.search(r'[\\/:*?"<>|\t\r\n]', text) or text.endswith((".", " ")):
            sys.exit(f"bad input {text!r}")
        if text.lower() not in seen:
            seen[text.lower()] = text
            out.append(text)
        elif text in EDGE_CASES and seen[text.lower()] != text:
            sys.exit(f"edge case {text!r} is {seen[text.lower()]!r} on a case-blind file system")
    return out


def write_res(res: Path, texts, keep) -> None:
    """values-<text>/c.xml holds c<i> for input i, and values/c.xml the one for ""."""
    res.mkdir(parents=True)
    for i, text in enumerate(texts):
        if i in keep:
            d = res / (f"values-{text}" if text else "values")
            d.mkdir()
            (d / "c.xml").write_text(f'<resources><integer name="c{i}">1</integer></resources>\n')


def build(aapt2: Path, android_jar: Path, tmp: Path, texts, keep):
    """Compiles and links the kept inputs: (rejected inputs, dump) with dump None on rejects."""
    work = Path(tempfile.mkdtemp(dir=tmp))
    write_res(work / "res", texts, keep)
    index = {f"values-{t}".lower(): i for i, t in enumerate(texts) if i in keep and t}
    result = subprocess.run([str(aapt2), "compile", "--dir", str(work / "res"),
                             "-o", str(work / "c.zip")], capture_output=True, text=True)
    failed = set()
    for m in re.finditer(r"[\\/](values-[^\\/]+)[\\/]c\.xml: error: ", result.stderr):
        failed.add(index[m[1].lower()])
    if result.returncode != 0:
        if not failed:
            sys.exit(f"aapt2 compile failed:\n{result.stderr}")
        return failed, None
    (work / "AndroidManifest.xml").write_text(
        '<manifest xmlns:android="http://schemas.android.com/apk/res/android" '
        'package="com.probe"/>\n')
    result = subprocess.run([str(aapt2), "link", "-I", str(android_jar),
                             "--manifest", str(work / "AndroidManifest.xml"),
                             # Keep a locale's value without a default, and every value as is.
                             "--no-resource-removal", "--no-resource-deduping",
                             "-o", str(work / "out.apk"), str(work / "c.zip")],
                            capture_output=True, text=True)
    if result.returncode != 0:
        # c.zip@values-b+ar+u+nu+latn_c.arsc.flat: error: ... invalid locale ...
        for m in re.finditer(r"@(values-.+)_c\.arsc\.flat: error", result.stderr):
            failed.add(index[m[1].lower()])
        if not failed:
            sys.exit(f"aapt2 link failed:\n{result.stderr}")
        return failed, None
    dump = subprocess.run([str(aapt2), "dump", "resources", str(work / "out.apk")],
                          check=True, capture_output=True, text=True).stdout
    return set(), dump


def main() -> None:
    out_file = out_path(OUT)
    tools, jar = sdk_tools("aapt2")
    aapt2 = tools / f"aapt2{EXE}"
    texts = inputs()

    # aapt2 takes minutes over thousands of directories, so chunks of them build side by side.
    def run(chunk):
        keep = set(chunk)
        while True:
            failed, dump = build(aapt2, jar, Path(tmp), texts, keep)
            if dump is not None:
                return keep, dump
            keep -= failed

    with tempfile.TemporaryDirectory() as tmp:
        chunks = [range(i, min(i + 200, len(texts))) for i in range(0, len(texts), 200)]
        with ThreadPoolExecutor(os.cpu_count()) as pool:
            results = list(pool.map(run, chunks))
    keep = set().union(*(k for k, _ in results))
    dump = "".join(d for _, d in results)

    # resource 0x7f010000 integer/c12
    #       (hdpi-v4) 1
    configs = {}
    for m in re.finditer(r"integer/c(\d+)\n((?:[ \t]+\(.*\) 1\n)+)", dump.replace("\r", "")):
        found = re.findall(r"\((.*)\) 1", m[2])
        if len(found) != 1:
            sys.exit(f"{texts[int(m[1])]!r}: expected one configuration, dump has {found}")
        # "b+e" comes out as "e" and a NUL: aapt2 writes two letters of any language that is
        # not three.
        configs[int(m[1])] = re.sub(r"[^\x21-\x7e]", lambda c: f"\\x{ord(c[0]):02x}", found[0])
    if set(configs) != keep:
        sys.exit(f"aapt2 dump listed {len(configs)} of {len(keep)} inputs")

    out_file.parent.mkdir(parents=True, exist_ok=True)
    with open(out_file, "w", encoding="utf-8", newline="\n") as out:
        out.write(f"# Generated by scripts/golden/gen_aapt2_configs.py with build-tools "
                  f"{BUILD_TOOLS} and {PLATFORM} on {HOST} ({MACHINE}). Do not edit by hand.\n")
        out.write("# qualifiers\tconfiguration (aapt2's canonical form; empty for the default, "
                  "- for rejected, \\xNN for a byte that is not printable)\n")
        for i, text in enumerate(texts):
            out.write(f"{text}\t{configs.get(i, '-')}\n")
    print(f"wrote {len(texts)} inputs ({len(texts) - len(keep)} rejected) to "
          f"{out_file.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
