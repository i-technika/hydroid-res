#!/usr/bin/env python3
"""Check the static_asserts of src/core/Config.cpp against the NDK's AConfiguration (#53).

Config.cpp asserts each value of Config.h's enums and constants equal to a number, and names after
it the ACONFIGURATION_ constant the number comes from. The compiler checks Config.h against the
number; this checks the number against that constant in the NDK's android/configuration.h, and
that every enumerator and constant of Config.h has an assert. tests/CMakeLists.txt runs it as
AConfiguration.MatchesNdk when an NDK is at hand.

Usage: python scripts/check_aconfiguration.py <android/configuration.h> <Config.h> <Config.cpp>
"""

import re
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip().splitlines()[-1])
    ndk_h, config_h, config_cpp = (Path(a).read_text(encoding="utf-8") for a in sys.argv[1:])

    ndk = {m[1]: int(m[2], 0) for m in re.finditer(
        r"^\s*(ACONFIGURATION_\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)", ndk_h, re.M)}
    # Every enumerator, with its value written or not, and every constant.
    enums = list(re.finditer(r"enum\s+class\s+(\w+)(?:\s*:\s*[\w:]+)?\s*\{([^}]*)\}", config_h))
    if len(enums) != len(re.findall(r"\benum\s+class\b", config_h)):
        sys.exit("Config.h has an enum class this script cannot read")
    declared = {f"{m[1]}::{e.split('=')[0].strip()}" for m in enums
                for e in re.sub(r"//[^\n]*", "", m[2]).split(",") if e.strip()}
    declared |= set(re.findall(r"(?:inline\s+)?constexpr\s+[\w:]+\s+(k\w+)\s*=", config_h))

    # static_assert(value(Orientation::Port) == 1);  // ORIENTATION_PORT
    # static_assert(kDensityAny == 0xfffe);          // DENSITY_ANY
    asserts = re.findall(r"static_assert\((?:value\((\w+::\w+)\)|(k\w+)) == (0x[0-9a-fA-F]+|\d+)\);"
                         r"\s*// (\w+)", config_cpp)
    errors, seen = [], set()
    for enumerator, constant, number, name in asserts:
        what = enumerator or constant
        seen.add(what)
        if f"ACONFIGURATION_{name}" not in ndk:
            errors.append(f"{what}: ACONFIGURATION_{name} is not in the NDK")
        elif ndk[f"ACONFIGURATION_{name}"] != int(number, 0):
            errors.append(f"{what}: Config.cpp says {number}, "
                          f"ACONFIGURATION_{name} is {ndk[f'ACONFIGURATION_{name}']}")
    errors += [f"{what}: no static_assert in Config.cpp" for what in sorted(declared - seen)]
    errors += [f"{what}: asserted in Config.cpp, not in Config.h"
               for what in sorted(seen - declared)]
    if errors:
        sys.exit("\n".join(errors))
    print(f"{len(asserts)} values of Config.h match the NDK's AConfiguration")


if __name__ == "__main__":
    main()
