#!/usr/bin/env python3
"""Run the golden dump on a connected device/emulator and copy the JSON into golden/data/.

Usage: python scripts/golden/pull_golden.py [--serial SERIAL] [--skip-run]
Needs adb on PATH (or ANDROID_HOME set) and a debuggable build of the golden app.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PACKAGE = "com.itechnika.hydroid.golden"
ROOT = Path(__file__).resolve().parents[2]
GOLDEN_PROJECT = ROOT / "golden"
OUT_DIR = GOLDEN_PROJECT / "data"


def find_adb() -> str:
    adb = shutil.which("adb")
    if adb:
        return adb
    for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        sdk = os.environ.get(env)
        if sdk:
            candidate = Path(sdk) / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb")
            if candidate.exists():
                return str(candidate)
    sys.exit("adb not found: add platform-tools to PATH or set ANDROID_HOME")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", help="adb device serial")
    parser.add_argument("--skip-run", action="store_true", help="only pull existing dump")
    args = parser.parse_args()

    adb = [find_adb()] + (["-s", args.serial] if args.serial else [])

    if not args.skip_run:
        gradlew = GOLDEN_PROJECT / ("gradlew.bat" if os.name == "nt" else "gradlew")
        task = ":app:connectedDebugAndroidTest"
        subprocess.run([str(gradlew), task], cwd=GOLDEN_PROJECT, check=True)

    listing = subprocess.run(
        adb + ["shell", "run-as", PACKAGE, "ls", "files/golden"],
        check=True, capture_output=True, text=True).stdout.split()
    names = [n for n in listing if n.endswith(".json")]
    if not names:
        sys.exit("no golden JSON on device; run the dump first")

    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True)
    for name in names:
        data = subprocess.run(
            adb + ["exec-out", "run-as", PACKAGE, "cat", f"files/golden/{name}"],
            check=True, capture_output=True).stdout
        (OUT_DIR / name).write_bytes(data)
    print(f"pulled {len(names)} files into {OUT_DIR.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
