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

Needs CMake 3.24+ and a C++17 compiler. pugixml 1.14, plus GoogleTest 1.15.2 and nlohmann/json
3.11.3 for the tests, are fetched at configure time, even when they are installed. To use
installed packages instead, configure with `-DHYDROID_RES_USE_SYSTEM_DEPS=ON`: they must be
exactly those versions, built with their default options (pugixml's `PUGIXML_WCHAR_MODE`, for
one, changes its API), and found through their CMake package configs (point
`CMAKE_PREFIX_PATH` at them if needed). On MSVC they must also be built against the shared CRT,
in the configuration you build (Debug or Release).

hydroid-res is a static library unless `BUILD_SHARED_LIBS` is on. A shared build exports only the
public API, links the fetched pugixml statically without exporting it, and puts its DLL next to the
executables in `build/<preset>/bin`. A project that uses the shared library without CMake must
define `HYDROID_RES_SHARED` when it includes the headers.

On Windows, Visual Studio 2026 needs a CMake that knows its generator (CMake 4.3 does); with an
older CMake, configure with `-G Ninja` from a developer prompt.

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
