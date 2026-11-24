# CLAUDE.md

Guidance for Claude (and any other contributor) working in this repository. Read it before changing
code or build files. For a user-facing overview, see `README.md`.

## What this repository is

TK4CPP ("Toolkit 4 C++") is a modular C++23 framework built with CMake. Each module under
`modules/<Name>/` is an independent CMake component with its own namespace, exported as
`TK4CPP::<Name>`, and built as a shared library, a static library, or both. Some modules are written
from scratch (Process, Thread, Encode, Utility), others wrap third-party libraries (Logging → spdlog,
XML → libxml2, Zip → libzip, JWT → jwt-cpp).

Version 0.1.0, early development. The repository is mid-integration: several modules were imported
from standalone projects and still carry their old build files. Treat the *Known problems* section
below as authoritative for the state at the time of writing, and re-check before relying on it.

## Working rules

These come from the maintainer's preferences for this project:

1. **Compile and run before declaring anything correct.** A change to code or CMake is not done until
   it has been configured, built and, where tests exist, run. Say exactly what was run.
2. **Be honest about unverified paths.** If something was not built or run (Windows, Clang, shared
   variant, sanitizers, C++20), say so instead of implying it works.
3. **Back qualitative claims with numbers** (tests passed/total, error counts, timings).
4. **Separate test bugs from code bugs.** When a test fails, isolate it (a minimal reproduction, or
   compile the test without the library under test) before changing library code. Several failing
   tests here are stale tests, not library defects.
5. Source comments are **English, Doxygen style** (`@file`, `@brief`, `@param`, `@return`, `@throws`,
   `@note`). Conversation with the maintainer may be in Italian; code and comments stay English.
6. Use **C++23 with `std::expected`** for recoverable errors in new code, and keep **CMake with
   `find_package` support** working for every module you touch (installed package must be usable).
7. Do not edit anything under `3rdParty/` (submodules). Do not commit build directories.

## Environment and commands

Toolchain verified on Ubuntu 24.04: GCC 13.3, CMake 3.28, Ninja 1.11. Clang 18 is installed there
but a Clang 18 + libstdc++ 13 combination does not expose `std::expected` (see Process README), and
no Clang build has been run. The repository requires **CMake ≥ 3.23** (`CMakePresets.json` declares
3.21, which is lower than what the root `CMakeLists.txt` actually needs).

```sh
git submodule update --init --recursive      # jwt-cpp, spdlog, libxml2, libzip (all four are needed for a full build)

# Configuration that builds cleanly today (also with the `debug` preset, i.e. -Werror):
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DTK4CPP_BUILD_TESTS=OFF -DTK4CPP_BUILD_XML=OFF -DTK4CPP_BUILD_ZIP=OFF
cmake --build build

# Build and run the tests that work (static libraries only):
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DTK4CPP_BUILD_SHARED=OFF -DTK4CPP_BUILD_XML=OFF -DTK4CPP_BUILD_ZIP=OFF
cmake --build build-static -k 0              # -k 0: do not stop at the first stale test
ctest --test-dir build-static --output-on-failure
```

The second recipe configures and builds with tests on (the `Thread` test target name is fixed, see problem 1).

Zip needs libzip ≥ 1.11 installed and found by `find_package(libzip 1.11 CONFIG REQUIRED)`; the
`3rdParty/libzip` submodule is not consumed automatically. Build and install it, then pass
`-DCMAKE_PREFIX_PATH=<prefix>`:

```sh
cmake -S 3rdParty/libzip -B build-libzip -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
      -DENABLE_OPENSSL=ON -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF \
      -DCMAKE_INSTALL_PREFIX=/opt/libzip
cmake --build build-libzip && cmake --install build-libzip
```

Run a single test executable or a subset: `ctest --test-dir build -R <regex> --output-on-failure`.
Test executables land in `build/bin/`.

Presets: `debug` (Ninja, `-Werror`), `release`, `debug-clang`, `static`, `docs`, `msvc` (Windows).
The binary directory is `build/<preset>`.

Doxygen is not installed in the verified environment and no root `docs/CMakeLists.txt` exists, so
`TK4CPP_BUILD_DOCS=ON` only prints a warning. `modules/Zip` has its own Doxyfile and doc build
(`TK4CPP_ZIP_BUILD_DOCS`, needs Doxygen and Graphviz).

## Architecture

### Root build

- `CMakeLists.txt` defines the options, the ordered module list `TK4CPP_KNOWN_MODULES`, the default-off
  list (`Lua`), per-module `TK4CPP_BUILD_<MODULE>` options (created only if `modules/<Name>/` exists),
  GoogleTest discovery (system package first, otherwise FetchContent v1.15.2), `add_subdirectory` of
  each enabled module in list order, and the install of `TK4CPPConfig.cmake`.
- Inter-module dependencies are declared as `TK4CPP_MODULE_DEPENDS_<NAME>`; today only
  `TK4CPP_MODULE_DEPENDS_XML = Utility` is declared. **`Utility` also depends on `Thread`** (via
  `MODULE_DEPENDS` in its own `CMakeLists.txt`) but that is not recorded in the root list, so
  disabling `Thread` while `Utility` is on fails later with an obscure link-target error.
- Module order matters: it is the dependency order, and `TK4CPP_BUILT_MODULES` (a global property)
  is filled in that order for the installed `TK4CPPConfig.cmake`.

### `cmake/TK4CPPModule.cmake`

- `tk4cpp_add_module(NAME ... SOURCES ... MODULE_DEPENDS ... PUBLIC_LINK ... PRIVATE_LINK ...
  PUBLIC_PACKAGES ... PRIVATE_PACKAGES ... PRIVATE_SYSTEM_INCLUDES ... EXCLUDE_HEADERS ...
  STATIC_ONLY_INSTALL_TARGETS ...)` is the canonical way to define a module. It creates the targets
  `TK4CPP_<Name>` (shared, or the only variant) and `TK4CPP_<Name>_static` with aliases
  `TK4CPP::<Name>` / `TK4CPP::<Name>_static`; sets hidden visibility, PIC, `-Wall -Wextra -Wpedantic`
  (`/W4 /permissive- /utf-8`); generates `<build>/include/TK4CPP/<Name>/Export.h` with
  `generate_export_header(BASE_NAME TK4CPP_<NAME>)`; defines `TK4CPP_<NAME>_EXPORTS` when building the
  shared library and `TK4CPP_<NAME>_STATIC_DEFINE` (PUBLIC) for the static one; writes install rules,
  `TK4CPP_<Name>Targets.cmake` and `TK4CPP_<Name>Deps.cmake`; and adds `tests/` if it has a
  `CMakeLists.txt`.
- `tk4cpp_add_tests(MODULE <Name> SOURCES ... LINK ...)` builds one GoogleTest executable
  `TK4CPP_<Name>_tests` linked to `TK4CPP::<Name>` (the shared one if both exist) and `GTest::gtest_main`,
  and registers cases with `gtest_discover_tests`.
- `tk4cpp_target_defaults(<target>)` applies the warning flags and `-Werror` / `/WX` when
  `TK4CPP_WARNINGS_AS_ERRORS` is on.

### Two kinds of modules

| Kind | Modules | How they build |
| ---- | ------- | -------------- |
| Framework-native | Utility, JWT, XML, Encode, Thread | `tk4cpp_add_module(...)`; generated `Export.h`; install handled by the helper; discoverable through `find_package(TK4CPP COMPONENTS ...)` |
| Standalone-style (imported) | Logging, Process, Zip | Own `project()`, own targets, own install and package config; honour `TK4CPP_*` variables when defined, with defaults otherwise; add themselves to `TK4CPP_BUILT_MODULES` manually |

Standalone-style modules do **not** emit `TK4CPP_<Name>Deps.cmake` (Process emits it since 2026-11-24), so the aggregate
`find_package(TK4CPP COMPONENTS Logging)` still fails (problem 5). When touching these modules keep them
buildable standalone as well as through the root, or convert them to `tk4cpp_add_module` deliberately
and say so.

### Module map

| Module | Namespace | Public headers | Dependencies | Notes |
| ------ | --------- | -------------- | ------------ | ----- |
| Utility | `TK4CPP::Utility` | `TK4CPP/Utility/*.h, *.hpp` | Thread | `Properties.hpp` and `FolderEntry.hpp` are header-only; `wip/` is not built |
| JWT | `TK4CPP::JWT` | `TK4CPP/JWT/JWTHandler.h` | OpenSSL (private), jwt-cpp (vendored, private) | HS256 only |
| XML | `TK4CPP::XML` | `TK4CPP/XML/{XML,XmlLibxml2,doc}.h` | Utility; libxml2 (private) | `doc.h` is not installed; `old/` is not built |
| Encode | `TK4CPP::Encode` | `TK4CPP/Encode/{Base64,UrlEncoding,Io,Error}.h` | none | `StreamCodec<T>` CRTP base in `Io.h` |
| Logging | `TK4CPP::Logging` | `TK4CPP/Logging/*.h` | spdlog, Threads | `src/NotificationSink.{h,cpp}` is private; `LogLayer.h` is the umbrella header |
| Process | `TK4CPP::Process` | `TK4CPP/Process.hpp` | Threads | `ProcessPosix.cpp` / `ProcessWin32.cpp`; `TK4CPP_PROCESS_FORCE_FORK` selects the fork path in tests |
| Thread | `TK4CPP::TP` | `TK4CPP/ThreadPool/*.h` | Threads | CMake target is `TK4CPP::Thread`; macro is `TK4CPP_TP_API` |
| Zip | `Zip` | `ZipWrapper/{Zip,HighLevel,Export}.h` | libzip ≥ 1.11 | Core RAII layer plus high-level API; `docs/snippets` are built as tests |

Naming is not uniform. Watch for these: module `Thread` ↔ namespace `TK4CPP::TP` ↔ directory
`include/TK4CPP/ThreadPool/` ↔ README title "ThreadPool"; module `Zip` ↔ namespace `Zip` ↔ directory
`include/ZipWrapper/`; module `Process` has a single header at `include/TK4CPP/Process.hpp`.

## Conventions for code in this repo

- Namespace `TK4CPP::<Module>`; public headers in `include/TK4CPP/<Module>/`; private headers next to
  the sources in `src/`.
- Every public non-template class or free function in a shared-capable module must carry the module's
  export macro (`TK4CPP_<NAME>_EXPORT`, from the generated `TK4CPP/<Name>/Export.h`). Symbols are
  hidden by default, so a missing macro compiles fine and fails only when linking against the shared
  library on Linux.
- Do not leak third-party headers through public headers unless the module documents it
  (spdlog in Logging and libzip in Zip are the documented exceptions; jwt-cpp and libxml2 are not
  exposed).
- Prefer `std::expected<T, Error>` with an `enum class ErrorCode` for recoverable failures; keep
  exceptions for programming errors or where the module already documents them (`Value`,
  `Properties`, Zip's `Zip::Exception`, `LogManager::init`).
- Doxygen on every public symbol. Zip's doc build treats warnings as errors.
- Use `std::filesystem`, `std::jthread` / `std::stop_token`, and the standard library instead of
  Poco. Poco has been removed from Thread and Logging; `FolderHandler` was written to avoid it.
- Tests are GoogleTest, one executable per module (`TK4CPP_<Name>_tests`), except Process (plain
  executables with a helper program) and Zip (plain executables plus built documentation snippets).

## Known problems (re-verified 2026-11-19, commit `ccedb3d`, GCC 13.3, Linux)

Check each one again before working around it; fixing them is welcome and each fix should be verified
by building and running as described under *Working rules*.

1. **Fixed (re-verified 2026-11-19):** the Thread tests configure and build against `TK4CPP::Thread`;
   the static configuration with tests passes 88 of 88. Still open: `modules/Thread/examples/CMakeLists.txt`
   uses `TK4CPP::ThreadPool`, but the examples directory is not added by `tk4cpp_add_module`, so it is
   dead until wired up. `modules/Thread/ThreadConfig.cmake`, `modules/Thread/cmake/ThreadConfig.cmake{,.in}`
   and `TK4CPPThreadConfig.cmake.in` / `TK4CPP_ThreadConfig.cmake.in` are leftovers of the standalone
   project and are not used by the build.
2. **Fixed (re-verified 2026-11-19):** XML compiles with tests enabled and its tests pass 116 of 116
   (static, Zip off). The `FolderHandler::create` call at `modules/XML/include/TK4CPP/XML/XML.h` is
   resolved. Whether `FolderHandler` should offer a static helper is still an open API question.
3. **Fixed (verified 2026-10-28):** the Utility tests build and pass (Thread+Utility only, static+shared, 50 of 50 ctest entries
   including the new `Glob`, `Utf8`, `SizeFormat`, `TimeParse` and `FindTest` cases). The old stale-test note no longer applies.
4. **Fixed (re-verified 2026-11-19):** the shared-library build with tests configures, compiles and passes
   88 of 88 (Encode and Thread included). The earlier missing `TK4CPP_ENCODE_EXPORT` and `TK4CPP_TP_API`
   link failures do not reproduce on this tree. The export macros should still be reviewed on Windows,
   where they matter, since this has not been verified there.
5. **Aggregate package misses Logging and Zip (Process fixed 2026-11-24).** Process now installs `TK4CPP_ProcessDeps.cmake` when it is built inside the framework; verified with a consumer project doing `find_package(TK4CPP REQUIRED COMPONENTS Process Encode)` on a static-only install, and with `process_tests`, `process_tests_forkpath` and `TK4CPP_Encode_tests` (3 of 3 pass, GCC 13.3). The text below still describes Logging and Zip. `TK4CPPConfig.cmake`
   includes `TK4CPP_<Module>Deps.cmake` and `TK4CPP_<Module>Targets.cmake`; these three modules install
   `TK4CPP_<Module>Config.cmake` instead, so `find_package(TK4CPP REQUIRED COMPONENTS Logging)`
   errors with "include could not find requested file TK4CPP_LoggingDeps.cmake". The per-module
   `find_package(TK4CPP_Logging)` also fails by default because the file sits in
   `lib/cmake/TK4CPP/`, not `lib/cmake/TK4CPP_Logging/`; setting `TK4CPP_Logging_DIR` (likewise
   `TK4CPP_Process_DIR`, `TK4CPP_Zip_DIR`) to `<prefix>/lib/cmake/TK4CPP` works (verified with a
   consumer project). `modules/Process` and `modules/Thread` also still ship duplicate `*Config.cmake.in`
   templates with different names; only one of each is used.
6. **JWT signing key is hardcoded (still open, re-verified 2026-11-19).** `modules/JWT/src/JWTHandler.cpp`
   signs and verifies with the literal `"HiAll"` (lines 35 and 66). `JWTHandler` is fully static and has
   no key setter. Treat the module as a demonstration until the key is injectable; never put a real secret in source.
7. **Dependency declaration gap (still open, re-verified 2026-11-19).** `Utility → Thread` is missing from
   `TK4CPP_MODULE_DEPENDS_*` in the root `CMakeLists.txt`, which only declares `TK4CPP_MODULE_DEPENDS_XML`.
8. **Metadata placeholders (still open, re-verified 2026-11-19).** The root `project()` has
   `HOMEPAGE_URL "https://github.com/OWNER/TK4CPP"`; `THIRD_PARTY_NOTICES.md` lacks entries for spdlog and
   libzip. The `.github/workflows/docs.yml` workflow exists in the tree but was not checked against the
   docs layout described in the former README.
9. **Unverified:** Windows, macOS, Clang, `TK4CPP_CXX_STANDARD=20`, sanitizer builds
   (`TK4CPP_PROCESS_SANITIZE`), the Doxygen builds, the `msvc` and `debug-clang` presets, the `Logging` and `Process`
   standalone builds. Zip is verified on Linux only, with libzip 1.12.1 built from `3rdParty/libzip` (see the table below);
   other libzip versions are not verified.
10. **Scheduler is not part of the root build (found 2026-11-24).** `modules/Scheduler` exists with its own CMake, but
    `Scheduler` is not in `TK4CPP_KNOWN_MODULES`, so `add_subdirectory` never reaches it and the installed
    `TK4CPPConfig.cmake` does not list it. Its README describes a Quartz-style scheduler with SQLite persistence
    (`SqliteJobStore`) and a ThreadPool executor; none of that was built or tested in this session.

### Last verified results

| Check | Result |
| ----- | ------ |
| Static, tests ON, XML/Zip OFF (2026-11-19, `ccedb3d`) | build OK (67 of 67 steps); ctest 88 of 88 pass |
| Shared, tests ON, XML/Zip OFF (2026-11-19, `ccedb3d`) | build OK; ctest 88 of 88 pass |
| XML + static, tests ON, Zip OFF (2026-11-19, `ccedb3d`) | build OK, 0 errors; ctest 116 of 116 pass |
| Utility without `FileHandlerTest.cpp` | 21 of 21 pass (2026-10-23, not re-run) |
| Zip against libzip 1.12.1 built from `3rdParty/libzip` (OpenSSL, GnuTLS, MbedTLS, bzip2, LZMA, zstd off), static, GCC 13.3 | build OK; ctest 11 of 11 pass (2026-11-24, `b162be4`) |
| Process + Encode, static, tests ON | `process_tests`, `process_tests_forkpath`, `TK4CPP_Encode_tests` pass, 3 of 3 (2026-11-24, `b162be4`) |
| Process + Encode installed (static only), consumer project | `find_package(TK4CPP REQUIRED COMPONENTS Process Encode)` configures, builds and links (2026-11-24, `b162be4`) |
| Installed package + consumer project | `TK4CPP::Encode` via `find_package(TK4CPP COMPONENTS Utility Encode Thread)` builds and runs; Logging/Process/Zip need the `*_DIR` workaround (2026-10-23, not re-run) |

## Tips for common tasks

- **Adding a module:** follow `README.md` → *Adding a module*; copy `modules/JWT` or `modules/Encode`
  as a template; remember the export macro, the root module list, the test CMake and, if applicable,
  `TK4CPP_MODULE_DEPENDS_<NAME>`. Verify with a configure, a build of both variants, `ctest`, an
  install, and a throwaway consumer project that calls `find_package(TK4CPP COMPONENTS <Name>)`.
- **Changing a public header:** rebuild both the shared and the static variants; a missing export
  macro only shows up in the shared one.
- **Changing Process:** run both `process_tests` and `process_tests_forkpath`; the helper binary
  `process_test_helper` is passed to the tests on the command line.
- **Changing Thread:** event handlers run synchronously on worker threads; keep handlers short and
  thread-safe, never call `shutdown()` from a pool thread, and run the test suite several times
  (about 4 s here) to catch races.
- **Changing Logging:** it works standalone and inside the root; keep both configurations compiling.
  The bundled spdlog headers are public, so installed headers go to `include/tk4cpp/3rdParty`.
- **Changing Zip:** every snippet in `modules/Zip/docs/snippets/` is compiled and run as a test; the
  manual's code and the tests must stay in sync.
- **Changing XML's libxml2 selection logic:** exercise `AUTO`, `SYSTEM`, `VENDORED`, `ROOT`, `DIR` and
  the explicit include/library pair; combining any custom option with `VENDORED` must remain an error.
- **Reporting status:** state the compiler, configuration, and exact commands, plus counts of passed
  and failed tests, and list what was not run.
