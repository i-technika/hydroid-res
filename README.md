# hydroid-res

The Android resource system for Hydroid: a C++17 library that reads an Android `res/` folder and
resolves values exactly as Android does, for UIKit, GTK4 and WinUI 3. Design and roadmap:
[Hydroid 리소스 시스템 설계 및 개발 로드맵](https://claude.ai/artifact/8T6XejqMo8TRPPcT42UZfF).

## Layout

| Path | What |
| --- | --- |
| `include/hydroid/res/` | Public C++ headers |
| `include/hydroid_res.h` | Stable C API, used by the Swift adapter |
| `src/core/` | Shared core: parser, ResourceTable, Config matching, Theme/Style |
| `src/capi/` | C API implementation |
| `tools/hydroid-rc/` | `res/` → `.hrt` binary table compiler |
| `tests/` | GoogleTest unit and golden tests |
| `golden/` | Android test app + instrumentation test that dumps Android's answers |
| `golden/data/` | The dumped golden JSON the C++ tests compare against |
| `scripts/golden/` | Runs the dump on a device and pulls the JSON |

## Build and test

Needs CMake 3.24+ and a C++17 compiler. pugixml, plus GoogleTest and nlohmann/json for the
tests, are fetched at configure time.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Golden data

Needs an emulator or device, the Android SDK and JDK 17+ (Android Studio's bundled JDK works).
Open `golden/` in Android Studio, or build it with `golden/gradlew`. Then:

```sh
python scripts/golden/pull_golden.py
```

This runs `GoldenDumpTest` over 252 conditions (7 densities × 3 languages × night × orientation ×
3 smallest widths) and writes `golden/data/*.json`.
