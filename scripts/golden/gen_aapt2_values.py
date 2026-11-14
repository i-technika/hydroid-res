#!/usr/bin/env python3
"""Compile thousands of literal values with the SDK's aapt2 into golden/aapt2/values.tsv.

Each input is written under one tag, which decides the formats aapt2 tries:

  color     <color>                  color
  bool      <bool>                   boolean
  integer   <integer>                integer
  dimen     <dimen>                  float, dimension, fraction
  fraction  <fraction>               float, dimension, fraction (the same as <dimen>)
  any       <item type="drawable">   every format, falling back to a string

and the file records the Res_value type and data aapt2 wrote, or "-" when it found no literal
(a compile error, or a string under "any"). The C++ test Aapt2ValueTest checks hydroid's parsers
against this file, so CI needs no Android SDK. Inputs are deterministic: hand-picked edge cases
plus seeded random values.

Usage: python scripts/golden/gen_aapt2_values.py
Needs an Android SDK with build-tools (aapt2, aapt) and a platform (android.jar).
"""

import os
import random
import re
import struct
import subprocess
import sys
import tempfile
from fractions import Fraction
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "golden" / "aapt2" / "values.tsv"
EXE = ".exe" if os.name == "nt" else ""

UNITS = ["px", "dp", "dip", "sp", "pt", "in", "mm", "%", "%p"]

# tag -> (element, extra attributes)
TAGS = {
    "color": ("color", ""),
    "bool": ("bool", ""),
    "integer": ("integer", ""),
    "dimen": ("dimen", ""),
    "fraction": ("fraction", ""),
    "any": ("item", ' type="drawable"'),
}

COMPLEX_EDGE_CASES = [
    # The issue #3 table, the parser tests and the units.
    "1px", "16dp", "14sp", "12pt", "2in", "25mm", "8dip", "-4dp", "+4dp", "0.5dp", ".5dp",
    "5.dp", "1.5e2px", "0.1dp", "1234.5678dp", "9000000dp", "1.5dp", "300000.5dp", "-1.5dp",
    "8388607dp", "8388608dp", "-0dp", "-0.0dp", "50%", "50%p", "-25%", "100%", "30%", "18%p",
    # Rounding of f * 2^23 + .5f: 2^-24 rounds up, 3 * 2^-24, 2^-25 rounds to zero.
    "0.000000059604644775390625dp", "0.000000178813934326171875dp",
    "0.0000000298023223876953125dp",
    # Radix boundaries, the mantissa wrapping, and the largest magnitudes aapt2 encodes.
    "1.00000011920928955078125dp", "0.99999994dp", "255.99999dp", "256.5dp", "65535.99dp",
    "65536.5dp", "1e-8dp", "-1e-8dp", "16777216dp", "-8388609dp", "123456789dp", "1e10dp",
    "-1e10dp", "2e12dp",
    "0.333333%", "33.3333333%p", "99.99999%", "0.0001%", "1e5%", "-150%p",
]

FLOAT_EDGE_CASES = [
    # Plain floats, signs and zeros.
    "", "1.5", "-2", "5", "0", "-0", "-0.0", "+0", ".5", "5.", "+.5", "-.5", "1e10", "1E5",
    "1.5E+2", "1.5e-2", "5.e3", "0.1", "0.2", "0.3", "3.14159265358979", "123456789",
    "16777216", "16777217", "16777219", "00012.5000", " 1.5 ",
    # Float range: FLT_MAX, the midpoint to 2^128, subnormals and underflow.
    "1e38", "3.4028234e38", "3.40282346638528859811704183484516925440e38",
    "3.4028235677973366e38", "1e-38", "1.17549435e-38", "1e-40",
    "1.4e-45", "1.401298464324817e-45", "7.006492321624085e-46", "7.006492321624086e-46",
    "7e-46", "1e-46", "1e-50", "1e-400", "-1e-400", "0e999999999999",
    # Not numbers, or not only numbers.
    ".", "-.", "+", "-", "e5", ".e5", "1e", "1e+", "1e-", "--1", "+-1", "1..5", "1.5.5", "1,5",
    "1_000", "16 dp", "16DP", "16Dp", "16dpx", "16em", "16px5", ".dp", "-dp", "16%%", "1,5dp",
    "1e+dp", "dp", "16 %", "16pX", "1.5f", "1.5d", "inf", "nan", "infinity", "Infinity",
    # Just under 2^41: the largest magnitude whose complex encoding is defined.
    "2199023124480dp", "-2199023124480dp",
    # Decimal to float conversion that a double in between would round twice.
    "1.0000000596046447753906251", "1.0000000596046447753906250",
    "1.0000000596046447753906249", "1.0000000596046447753906251px",
    "1.0000000596046447753906251dp", "1.0000001788139343261718749",
]

# Accepted by aapt2 through strtof, rejected by hydroid on purpose (see ValueParser.h). Kept out
# of the random samples so that Aapt2ValueTest can list every one of them.
DELIBERATE_REJECTS = {
    "dimen": [
        "-inf", "+inf", "-nan", "+nan", "-infinity", "+Infinity", "-INF", "-NaN", "-nan(1)",
        "-infdp", "+nan%", "0x10", "0X10", "0x10px", "-0x10px", "0x1p4", "0x1.8p1dp", "0x.8",
        "1e39", "-1e39", "1e39dp", "-1e39dp", "1e39%", "1e400", "3.4028236e38",
        "3.4028235677973367e38",
        "3e12dp", "-3e12dp", "2199023255552dp", "1e20px",
    ],
    "fraction": ["-inf", "0x10%", "1e39%"],
    # 0x100000000 is past an integer, so aapt2 reads it as a hex float, 2^32.
    "any": ["-0x10", "0X10", "0x100000000", "-1e39dp"],
}


def long_numbers() -> list:
    # aapt2's stringToFloat takes at most 126 characters.
    out = []
    for unit in ["", "dp", "%"]:
        for length in [125, 126, 127, 130]:
            out.append("0." + "0" * (length - len(unit) - 3) + "1" + unit)
            out.append("1." + "0" * (length - len(unit) - 2) + unit)
    return out


def float_bits(f: float) -> int:
    return struct.unpack("<I", struct.pack("<f", f))[0]


def float_of(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def exact_decimal(x: Fraction) -> str:
    """The finite decimal of a dyadic rational, digits as needed."""
    k = 0
    while x.denominator != 1 and (x * 10 ** k).denominator != 1:
        k += 1
    n = x * 10 ** k
    assert n.denominator == 1
    s = str(n.numerator).rjust(k + 1, "0")
    return s if k == 0 else s[:-k] + "." + s[-k:]


def double_rounding_cases(rng: random.Random, count: int) -> list:
    """Decimals a hair above or below the midpoint of two floats.

    The hair is far below a double's precision, so going through a double lands exactly on the
    midpoint and ties to even, which is the wrong float half of the time; strtof gets it right.
    """
    out = []
    while len(out) < count:
        bits = rng.randrange(float_bits(2.0 ** -12), float_bits(2.0 ** 22))
        lo, hi = Fraction(float_of(bits)), Fraction(float_of(bits + 1))
        mid = exact_decimal((lo + hi) / 2)
        hair = 20 + len(mid.split(".")[0])
        text = mid + "0" * hair + "1" if rng.random() < 0.5 else mid[:-1] + "4" + "9" * hair
        unit = rng.choice(["", "", "", "px", "dp", "%"])
        if len(text + unit) <= 126:
            out.append(text + unit)
    return out


def complex_inputs() -> list:
    # Same seed and draws as the original complex corpus (#24), so those rows stay.
    rng = random.Random(3)
    texts = list(COMPLEX_EDGE_CASES)
    for _ in range(3000):
        digits = rng.randint(0, 8)
        v = round(rng.choice([1, -1]) * 10 ** rng.uniform(-7, 9), digits)
        if rng.random() < 0.15:
            text = f"{v:.{rng.randint(1, 6)}e}"
        else:
            text = f"{v:.{digits}f}"
        texts.append(text + rng.choice(UNITS))
    return texts


def color_inputs(rng: random.Random) -> list:
    texts = [
        "#f80", "#8f80", "#6200ee", "#806200ee", "#fff", "#ffff", "#ffffff", "#ffffffff",
        "#FF6200EE", "#aBcDeF", " #fff ", "#0000", "#00ffffff", "#000", "#00000000", "", "#",
        "#f", "#ff", "#fffff", "#fffffff", "#fffffffff", "fff", "ffffff", "#ggg", "# fff",
        "#ff ff", "#fff ff", "0xffffff", "#-ff", "#+fff", "##fff", "#fff#", "#0x12", "#ffg",
        "#12345g78", "#FfFfFf", "#AbC", "#0Ff0",
    ]
    for _ in range(500):
        length = rng.choice([3, 4, 6, 8, 3, 4, 6, 8, 0, 1, 2, 5, 7, 9, 10])
        chars = "0123456789abcdefABCDEF"
        text = "".join(rng.choice(chars if rng.random() < 0.97 else "gGxz-+.")
                       for _ in range(length))
        texts.append(("#" if rng.random() < 0.95 else "") + text)
    return texts


def integer_inputs(rng: random.Random) -> list:
    texts = [
        "", "0", "42", "-42", " 7 ", "2147483647", "-2147483648", "2147483648", "-2147483649",
        "4294967295", "4294967296", "0x10", "0x7f010000", "0xFfFfFfFf", "0xffffffff",
        "0x100000000", "0x0", "0x00000000000000000000000001", "0x7fffffff", "0x80000000",
        "-0", "-00", "007", "0" * 130 + "42", "0x", "0X10", "-0x10", "+5", "-", "+", "0xg",
        "4 2", "1.0", "1e3", "x10", "true", "--1", "- 1", "0x-1", "0x+1", "00x10", "0x0x1",
        "99999999999999999999", "1_000", "1,000", "12a", "0x1g", "#ff",
    ]
    for _ in range(300):
        v = rng.choice([1, -1]) * int(10 ** rng.uniform(0, 10.5))
        texts.append(str(v))
    for _ in range(150):
        digits = rng.randint(1, 10)
        texts.append("0x" + "".join(rng.choice("0123456789abcdefABCDEF") for _ in range(digits)))
    return texts


def bool_inputs() -> list:
    texts = []
    for word in ["true", "false"]:
        for mask in range(2 ** len(word)):
            texts.append("".join(c.upper() if mask >> i & 1 else c for i, c in enumerate(word)))
    return texts + ["", "yes", "no", "1", "0", " true ", "truee", "tru", "on", "off", "null"]


def hex_float(text: str) -> bool:
    """Whether strtof may read text as a hex float that hydroid rejects on purpose.

    Under "any", hex past an integer (0X10, -0x10, 0x100000000) falls through to strtof. Only
    what hydroid's integer parser takes, 0x and at most 32 bits, is kept.
    """
    m = re.fullmatch(r"\s*0x([0-9a-fA-F]+)\s*", text)
    if m:
        return int(m[1], 16) > 0xFFFFFFFF
    return re.match(r"\s*[+-]?0[xX]", text) is not None


def any_inputs(rng: random.Random, *corpora) -> list:
    # Every format at once: aapt2 tries color, bool, integer, float, then keeps a string.
    texts = ["hello", "5", "-5", "#fff", "true", "1.5", "16dp", "50%", "50%p", "0x10",
             "2147483648", "-2147483649", "0xffffffff", "1e3", "TRUE", "tRUE",
             "#ggg", "", "1.0000000596046447753906251",
             # A float 1 or more off the double is kept as a string: the edge of that check.
             "16777216.5", "16777217.0", "16777218.0", "33554433.5", "1e10",
             # Floats under <dimen> but strings here: aapt2 keeps no float inf or NaN for "any".
             "-inf", "+nan", "1e39"]
    for corpus in corpora:
        corpus = [t for t in corpus if not hex_float(t)]
        texts += rng.sample(corpus, min(len(corpus), 60))
    return texts


def inputs() -> list:
    """(tag, text) pairs, without duplicates."""
    rng = random.Random(5)
    complex_texts = complex_inputs()
    floats = FLOAT_EDGE_CASES + long_numbers() + double_rounding_cases(rng, 300)
    colors = color_inputs(rng)
    integers = integer_inputs(rng)
    bools = bool_inputs()
    pairs = [("color", t) for t in colors]
    pairs += [("bool", t) for t in bools]
    pairs += [("integer", t) for t in integers]
    pairs += [("fraction" if t.endswith(("%", "%p")) else "dimen", t) for t in complex_texts]
    pairs += [("dimen", t) for t in floats]
    pairs += [("fraction", t) for t in ["16dp", "1.5", "5"]]
    pairs += [("any", t) for t in any_inputs(rng, colors, integers, bools, floats)]
    pairs += [(tag, t) for tag, texts in DELIBERATE_REJECTS.items() for t in texts]
    for tag, text in pairs:
        # The values XML would change these before aapt2 parses them (quotes, escapes,
        # references, collapsed or trimmed whitespace), and the TSV cannot hold tabs.
        if re.search(r"[\"'\\@?\t\r\n]", text) or text.startswith(("@", "?")):
            sys.exit(f"bad input {text!r}")
    return list(dict.fromkeys(pairs))


def find_sdk() -> Path:
    sdks = [os.environ.get(env) for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT")]
    # Android Studio's default SDK locations on Windows, macOS and Linux.
    sdks += [os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk"),
             str(Path.home() / "Library" / "Android" / "sdk"),
             str(Path.home() / "Android" / "Sdk")]
    for sdk in filter(None, sdks):
        if (Path(sdk) / "build-tools").is_dir():
            return Path(sdk)
    sys.exit("Android SDK not found: set ANDROID_HOME")


def newest(paths) -> Path:
    def key(p):
        return [int(n) if n.isdigit() else -1 for n in re.split(r"[.-]", p.name.split("-")[-1])]
    paths = sorted(paths, key=key)
    if not paths:
        sys.exit("no build-tools with aapt2 and aapt, or no platform with android.jar")
    return paths[-1]


def write_values(res: Path, pairs, keep) -> None:
    """One file per tag, value i on line i + 2, so compile errors point back at their input."""
    values = res / "values"
    values.mkdir(parents=True, exist_ok=True)
    for tag, (element, attrs) in TAGS.items():
        with open(values / f"{tag}.xml", "w", encoding="utf-8", newline="\n") as xml:
            xml.write("<resources>\n")
            for i, (t, text) in enumerate(pairs):
                if t == tag and i in keep:
                    xml.write(f'<{element}{attrs} name="v{i}">{escape(text)}</{element}>\n')
                else:
                    xml.write("\n")
            xml.write("</resources>\n")


def compile_errors(aapt2: Path, tmp: Path, pairs, keep) -> set:
    write_values(tmp / "res", pairs, keep)
    result = subprocess.run([str(aapt2), "compile", "--dir", str(tmp / "res"),
                             "-o", str(tmp / "compiled.zip")], capture_output=True, text=True)
    failed = set()
    for m in re.finditer(r"(\w+)\.xml:(\d+): error: (.*)", result.stderr):
        i = int(m[2]) - 2
        if not (0 <= i < len(pairs)) or pairs[i][0] != m[1] or i not in keep:
            sys.exit(f"unexpected aapt2 error: {m[0]}")
        failed.add(i)
    if result.returncode != 0 and not failed:
        sys.exit(f"aapt2 compile failed:\n{result.stderr}")
    return failed


def main() -> None:
    sdk = find_sdk()
    tools = newest(p for p in (sdk / "build-tools").iterdir()
                   if (p / f"aapt2{EXE}").exists() and (p / f"aapt{EXE}").exists())
    platform = newest(p for p in (sdk / "platforms").iterdir() if (p / "android.jar").exists())
    aapt2 = tools / f"aapt2{EXE}"
    pairs = inputs()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        # Drop what aapt2 rejects until the rest compiles.
        keep = set(range(len(pairs)))
        while failed := compile_errors(aapt2, tmp, pairs, keep):
            keep -= failed
        (tmp / "AndroidManifest.xml").write_text(
            '<manifest xmlns:android="http://schemas.android.com/apk/res/android" '
            'package="com.probe"/>\n')
        subprocess.run([str(aapt2), "link", "-I", str(platform / "android.jar"),
                        "--manifest", str(tmp / "AndroidManifest.xml"),
                        "-o", str(tmp / "out.apk"), str(tmp / "compiled.zip")], check=True)
        dump = subprocess.run([str(tools / f"aapt{EXE}"), "dump", "--values", "resources",
                               str(tmp / "out.apk")],
                              check=True, capture_output=True, text=True).stdout

    written = {}
    for m in re.finditer(r":\w+/v(\d+): t=0x([0-9a-f]{2}) d=0x([0-9a-f]{8})", dump):
        written[int(m[1])] = (int(m[2], 16), m[3])
    if set(written) != keep:
        sys.exit(f"aapt dump listed {len(written)} of {len(keep)} values")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as out:
        out.write(f"# Generated by scripts/golden/gen_aapt2_values.py with build-tools "
                  f"{tools.name}. Do not edit by hand.\n")
        out.write("# tag\ttext\ttype\tdata (Res_value as aapt2 writes it; - for no literal)\n")
        for i, (tag, text) in enumerate(pairs):
            t, d = written.get(i, (None, None))
            if t is None or (tag == "any" and t == 0x03):  # rejected, or a string under "any"
                out.write(f"{tag}\t{text}\t-\t-\n")
            else:
                out.write(f"{tag}\t{text}\t0x{t:02x}\t0x{d}\n")
    print(f"wrote {len(pairs)} values ({len(pairs) - len(keep)} rejected) to "
          f"{OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
