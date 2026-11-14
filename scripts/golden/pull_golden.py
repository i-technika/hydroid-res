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
    sdks = [os.environ.get(env) for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT")]
    # Android Studio's default SDK locations on Windows, macOS and Linux.
    sdks += [os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk"),
             str(Path.home() / "Library" / "Android" / "sdk"),
             str(Path.home() / "Android" / "Sdk")]
    for sdk in filter(None, sdks):
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
        # Not connectedDebugAndroidTest: it uninstalls the app afterwards, deleting the dump.
        env = dict(os.environ)
        if args.serial:
            env["ANDROID_SERIAL"] = args.serial  # otherwise Gradle installs on every connected device
        subprocess.run([str(gradlew), ":app:installDebug", ":app:installDebugAndroidTest"],
                       cwd=GOLDEN_PROJECT, env=env, check=True)
        result = subprocess.run(
            adb + ["shell", "am", "instrument", "-w",
                   f"{PACKAGE}.test/androidx.test.runner.AndroidJUnitRunner"],
            check=True, capture_output=True, text=True)
        print(result.stdout.strip())
        if "OK (" not in result.stdout:
            sys.exit("golden dump test failed")

    listing = subprocess.run(
        adb + ["shell", "run-as", PACKAGE, "ls", "files/golden"],
        check=True, capture_output=True, text=True).stdout.split()
    names = [n for n in listing if n.endswith(".json")]
    if not names:
        sys.exit("no golden JSON on device; run the dump first")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for old in OUT_DIR.glob("*.json"):  # keep README.md and anything else checked in
        old.unlink()
    for name in names:
        data = subprocess.run(
            adb + ["exec-out", "run-as", PACKAGE, "cat", f"files/golden/{name}"],
            check=True, capture_output=True).stdout
        (OUT_DIR / name).write_bytes(data)
    print(f"pulled {len(names)} files into {OUT_DIR.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
