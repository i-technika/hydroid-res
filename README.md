# hydroid-res

The Android resource system for Hydroid: a C++17 library that reads an Android `res/` folder and
resolves values exactly as Android does, for UIKit, GTK4 and WinUI 3. Design and roadmap:
[Hydroid 리소스 시스템 설계 및 개발 로드맵](https://claude.ai/artifact/8T6XejqMo8TRPPcT42UZfF).

## Layout

| Path | What |
| --- | --- |
| `include/hydroid/res/` | Public C++ headers |
| `include/hydroid_res.h` | Stable C API, used by the Swift adapter |
| `src/core/` | Shared core: parsers, ResourceTable, Config matching, Theme/Style |
| `src/capi/` | C API implementation |
| `tools/hydroid-rc/` | `res/` → `.hrt` binary table compiler; for now `hydroid-rc dump <res>` and `hydroid-rc pick <res> <qualifiers>` |
| `tests/` | GoogleTest unit and golden tests |
| `golden/` | Android test app + instrumentation test that dumps Android's answers |
| `golden/data/` | The dumped golden JSON the C++ tests compare against |
| `golden/table/res/` | A `res/` folder with every kind of value and file, for the table tests |
| `golden/aapt2/` | What the SDK's aapt2 makes of literal values, resource folder names, strings and whole `res/` folders |
| `golden/aosp/` | The folder names of AOSP's framework `res/`, which must all parse |
| `scripts/golden/` | Runs the dump on a device and pulls the JSON; runs aapt2 for `golden/aapt2/` |
| `scripts/gen_locale_data.py` | Makes `src/core/LocaleDataTables.inc`, the locale tables matching goes by, from AOSP's `libs/androidfw/LocaleDataLookup.cpp` ([Locale tables](#locale-tables)) |

## Build and test

Needs CMake 3.24+, a C compiler and a C++17 compiler. pugixml 1.14 and expat 2.7.1, plus
GoogleTest 1.15.2 and nlohmann/json 3.11.3 for the tests, are fetched at configure time, even when
they are installed. To use installed packages instead, configure with `-DHYDROID_RES_USE_SYSTEM_DEPS=ON`: they must be
exactly those versions, built with their default options (pugixml's `PUGIXML_WCHAR_MODE`, for one,
changes its API), and found through their CMake package configs (point `CMAKE_PREFIX_PATH` at them
if needed). On MSVC they must also be built against the shared CRT, in the configuration you build
(Debug or Release).

hydroid-res is a static library unless `BUILD_SHARED_LIBS` is on. A shared build exports only the
public API, links the fetched pugixml and expat statically without exporting them, and, as the
top-level project, puts its DLL next to the executables in `build/<preset>/bin` (`bin/<config>` with
a multi-config generator such as Visual Studio). A project that uses the shared library without
CMake must define `HYDROID_RES_SHARED` when it includes the headers.

The version is `project()` in `CMakeLists.txt` and nowhere else. CMake generates
`hydroid/res/VersionNumber.h` from it into `build/<preset>/include`, which a project that uses
the headers without CMake must also put on its include path. Its `HYDROID_RES_VERSION` is the
version of the headers, and `hydroid_res_version()` that of the library. The shared library's
ABI version (its soname on Linux) is `major.minor` until 1.0, as a 0.x minor release may break
the ABI, and the major alone after. A library is compatible with the headers when its ABI
version is the same and, from 1.0, its minor is no older; the patch may differ either way. On
Windows the DLL name carries no version, so a consumer of the shared library should check this
when it starts.

On Windows, Visual Studio 2026 needs a CMake that knows its generator (CMake 4.3 does); with an
older CMake, configure with `-G Ninja` from a developer prompt.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

With GCC or Clang, the `sanitize` preset does the same under AddressSanitizer and
UndefinedBehaviorSanitizer, and stops at the first error; CI runs it with Clang on Linux. The
`tsan` preset does it under ThreadSanitizer, which finds the data races of code that several
threads call, as `Resources` is (#236); CI runs it with Clang on Linux in its full scope. On a
kernel that randomizes mmap with more bits than 28, as CI's runners do, the ThreadSanitizer of
some compiler releases stops every test with "unexpected memory mapping"; CI first runs
`sudo sysctl vm.mmap_rnd_bits=28`, which does for a local run too. Neither preset is offered on
Windows, where MSVC would ignore their flags.

With an Android NDK and Python at hand, `ctest` also runs `AConfiguration.MatchesNdk`, which
checks the values of `Config.h` against the NDK's `android/configuration.h`. It finds the NDK in
`HYDROID_RES_ANDROID_NDK`, `ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT` or the SDK's `ndk/`, and
configure says which it took; without one, the test is left out, or configure fails with
`-DHYDROID_RES_REQUIRE_NDK_TESTS=ON`, as in CI.

The C++ sources follow `.clang-format`, as clang-format 23.1.5 applies it (`pipx install
clang-format==23.1.5`); CI fails on any other formatting. To reformat:

```sh
git ls-files -z -- '*.h' '*.cpp' | xargs -0 clang-format -i
```

## Golden data

All of it comes from one API level, `kEmulatedSdkVersion` in `include/hydroid/res/Version.h`
(37): the emulator, the golden app's `compileSdk` and `targetSdk`, the build-tools and platform
the aapt2 scripts pin, the AOSP release and commit `golden/aosp` lists the framework folders of,
and the same release and commit `scripts/gen_locale_data.py` pins for the matching code and the
locale tables. `SdkBaseline` tests fail when one of them moves without the rest,
or when the aapt2 files were not made with the build-tools, platform and OS (`GOLDEN_HOST`,
Windows) that `scripts/golden/gen_aapt2_values.py` pins, or when this README names others.

The dump needs an API 37.0 emulator or device, the Android SDK and JDK 17+ (Android Studio's
bundled JDK works).
Open `golden/` in Android Studio, or build it with `golden/gradlew`. Then:

```sh
python scripts/golden/pull_golden.py
```

This runs `GoldenDumpTest` over 377 conditions and writes `golden/data/*.json`: a matrix of 252
(7 densities × 3 languages × night × orientation × 3 smallest widths), and 125 more that each
change one thing the matrix leaves alone, such as a locale of another region or script, a list
of languages, the screen's size or shape, the UI mode, the input devices or the mcc. Each file
records the `Configuration` the resources were resolved for and the value of every resource.
`GoldenMatchTest` builds the same device from it and checks that hydroid picks the same values.
To see by hand what hydroid picks for one device,
`hydroid-rc pick <res> <qualifiers> [--locales <tag,...>] [<package>]` takes the device as the
qualifiers of a folder name and its languages as BCP 47 tags, and prints the locale it chose and
the value of each resource, with the configuration it comes from:

```sh
hydroid-rc pick golden/app/src/main/res ko-rKR-land-night-xhdpi --locales ko-KR com.itechnika.hydroid.golden
```

A device whose qualifiers name no version is of `kEmulatedSdkVersion`, not the one `parseConfig`
raises them to (night is night-v8), and one whose qualifiers name a version is of that one, even
below it (night-v4 is of API 4, where values-night/ is not picked).
`HydroidRc.PicksWhatAndroidPicked` checks it against three conditions of `golden/data`, two of
them of two languages.

The `pick_` strings of the app's `values*/match.xml` are there for that: each has a value in
`values/` and in some `values-<qualifiers>/`, which says the folder it is in, so the dump shows
which folder Android took it from. A folder added for one needs a condition that picks it, or a
line in `GoldenMatch.EveryFolderIsPickedSomewhere` saying why none does. Two things have no
folder there, because the app is built by the Android Gradle plugin and dumped through the SDK:
a locale with a variant and no script, and the grammatical gender (the SDK only sets it for a
whole app). A variant with a script the plugin keeps, so `values-b+sr+Latn+RS+ekavsk/` and
`values-b+sr+Latn+RS/`, of the same region, are there for it: `sr-Latn-RS-ekavsk` takes the
first, only because its variant matches. `sr-Latn-RS` and `sr-Latn-RS-ijekavsk` take the
second, which they would take anyway as the first of two equal folders in the table, so that a
device with no variant prefers a folder with none only `ConfigMatchTest` checks.

The plugin (9.5.1) compiles values and most files (raw, layout, xml) with a compiler of its own,
not aapt2, and that writes the variant of a locale with no script as empty (#198). An app it
builds has `values-b+de+DE+1901` as `values-de`, `values-b+ca+ES+valencia` as `values-ca` and
`raw-b+de+DE+1901/x.txt` as `res/raw-de/x.txt`, and it fails on `values-b+de+1901`;
`values-b+sr+Latn+RS+ekavsk` keeps its variant. No option turns that compiler off. hydroid
follows aapt2, which keeps the variant, so from such a folder it can pick another value than
an app built by the plugin does. (aapt2 drops variants only after a region in a `b+` name of
four subtags, and hydroid with it: `values-b+de+DE+1901+1996` is `de-rDE`, #225.) The
`folder-locale-variant` case of `table_cases.json` checks what aapt2 makes of them, and
`ConfigMatchTest` the rules for variants, from AOSP's tests; the golden app checks only that a
matching variant wins, and only with a script.

The aapt2 files need only the SDK (build-tools 37.0.0 and platform android-37.0), no device, but
they must be made on Windows: aapt2 writes some rows otherwise on Linux and macOS (#62).

```sh
python scripts/golden/gen_aapt2_values.py    # golden/aapt2/values.tsv
python scripts/golden/gen_aapt2_configs.py   # golden/aapt2/configs.tsv
python scripts/golden/gen_aapt2_strings.py   # golden/aapt2/strings.json
python scripts/golden/gen_aapt2_table.py     # golden/aapt2/table.txt, app_table.txt, table_cases.json
```

The table files are what `aapt2 dump resources` prints of `golden/table/res`, of the golden app's
`res/` and of some 300 small folders, linked with `--no-auto-version`, `--no-version-vectors`,
`--no-version-transitions`, `--no-resource-removal` and `--no-resource-deduping`, which keep
aapt2 from adding or dropping values the folder does not say. `hydroid-rc dump <res> [<package>]`
prints a folder the same way, from hydroid's `ResourceTable`, to compare by hand.

On another OS the scripts stop. With `--any-host` they run anyway and write
`build/aapt2-<os>-<cpu>/` instead of `golden/aapt2/`, to compare with the golden files.

A folder added to the golden app goes into `configs.tsv` and `app_table.txt` too, so both
scripts run again after it.

## Locale tables

Android matches a locale by its script, and picks between regions by the parent of each locale
(es-MX under es-419 under es), from tables AOSP generates from ICU's CLDR data.
`src/core/LocaleDataTables.inc` holds the same tables, made from AOSP's
`libs/androidfw/LocaleDataLookup.cpp` at the commit of `android17-release` the script pins:

```sh
python scripts/gen_locale_data.py
```

It downloads that file from android.googlesource.com, so no test makes the tables again;
`SdkBaseline` checks only that their header names the release and commit the script pins.
The `locale tables` job of the full CI makes them again and fails when they are not the file in
the repository (#228): tables edited by hand, or a header moved to another commit without
them.
