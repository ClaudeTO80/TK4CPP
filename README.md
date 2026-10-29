# TK4CPP — Toolkit 4 C++

[![CI](https://github.com/ClaudeTO80/TK4CPP/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/ClaudeTO80/TK4CPP/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/github/license/ClaudeTO80/TK4CPP)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=c%2B%2B&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![CMake ≥ 3.23](https://img.shields.io/badge/CMake-%E2%89%A5%203.23-064F8C?logo=cmake&logoColor=white)](https://cmake.org)
[![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS-lightgrey)](#build)
[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CMakeLists.txt)
[![Status](https://img.shields.io/badge/status-early%20development-orange)](#build-status)
[![Last commit](https://img.shields.io/github/last-commit/ClaudeTO80/TK4CPP)](https://github.com/ClaudeTO80/TK4CPP/commits/main)

TK4CPP is a modular, cross-compiler C++ framework. It offers high-level, easy-to-use APIs on top of
optimised low-level implementations. Some modules are written from scratch, others are consistent,
modern-C++ wrappers around well-known third-party libraries (spdlog, libxml2, libzip, jwt-cpp).

Every module is a separate CMake component with its own namespace, can be enabled or disabled
independently, and is built as a shared library, a static library, or both.

> **Status: early development (0.1.0).** APIs may still change. See [Build status](#build-status)
> for what has actually been verified.

- Language: C++23 (`std::expected`, `std::move_only_function`, `std::jthread`, ...)
- Build system: CMake ≥ 3.23, with `find_package(TK4CPP)` support
- License: MIT (see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md))

## Contents

- [Modules](#modules)
- [Getting the sources](#getting-the-sources)
- [Build](#build)
- [Build status](#build-status)
- [Using TK4CPP in your project](#using-tk4cpp-in-your-project)
- [Module guide](#module-guide)
- [Repository layout](#repository-layout)
- [Adding a module](#adding-a-module)
- [Conventions](#conventions)
- [Third-party software](#third-party-software)
- [License](#license)

## Modules

| Module    | CMake option             | Namespace          | Purpose |
| --------- | ------------------------ | ------------------ | ------- |
| `Utility` | `TK4CPP_BUILD_UTILITY`   | `TK4CPP::Utility`  | Typed `Value` / `Properties`, file and folder helpers, `Version`, UUID v4/v6/v7, system information |
| `JWT`     | `TK4CPP_BUILD_JWT`       | `TK4CPP::JWT`      | JSON Web Token creation and validation (HS256) on top of jwt-cpp |
| `XML`     | `TK4CPP_BUILD_XML`       | `TK4CPP::XML`      | XML documents: abstract interfaces plus a libxml2 backend |
| `Encode`  | `TK4CPP_BUILD_ENCODE`    | `TK4CPP::Encode`   | Base64 and URL percent-encoding codecs with incremental, stream, file and folder support |
| `Logging` | `TK4CPP_BUILD_LOGGING`   | `TK4CPP::Logging`  | spdlog-based logging with observers and notifications |
| `Process` | `TK4CPP_BUILD_PROCESS`   | `TK4CPP::Process`  | Cross-platform child-process execution (no shell), output capture, process-tree ownership |
| `Thread`  | `TK4CPP_BUILD_THREAD`    | `TK4CPP::TP`       | Prioritised, dynamically scaling thread pool with typed events (headers under `TK4CPP/ThreadPool/`) |
| `Zip`     | `TK4CPP_BUILD_ZIP`       | `Zip`              | C++23 RAII wrapper around libzip with a high-level one-call API (headers under `ZipWrapper/`) |

Reserved but **not present in the tree yet**: `DataLayer`, `Chart`, `ArgsParse`, `Lua` (off by
default when it arrives), `Crypto`. They are already listed in the root `CMakeLists.txt`; a module is
only configured when its directory exists under `modules/`.

Hard dependencies between modules:

| Module | Requires |
| ------ | -------- |
| `Utility` | `Thread` |
| `XML` | `Utility` |

> The root `CMakeLists.txt` currently declares only `XML → Utility` in `TK4CPP_MODULE_DEPENDS_*`.
> `Utility` links `Thread` through `MODULE_DEPENDS`, so leave `Thread` enabled whenever `Utility` is.

## Getting the sources

Third-party libraries are Git submodules in `3rdParty/`:

```sh
git clone --recurse-submodules https://github.com/ClaudeTO80/TK4CPP.git
# or, in an existing clone:
git submodule update --init --recursive
```

| Submodule             | Used by   | Needed |
| --------------------- | --------- | ------ |
| `3rdParty/jwt-cpp`    | `JWT`     | always when `JWT` is built (header-only) |
| `3rdParty/spdlog`     | `Logging` | unless `TK4CPP_LOGGING_USE_SYSTEM_SPDLOG=ON` (header-only) |
| `3rdParty/libxml2`    | `XML`     | only when no suitable system libxml2 ≥ 2.9 is found (see [XML](#xml)) |
| `3rdParty/libzip`     | `Zip`     | only when no suitable installed libzip ≥ 1.11 is found (see [Zip](#zip)) |

Without the submodules the configure step fails (for example `Logging: bundled spdlog not found`).

## Build

Requirements:

- CMake ≥ 3.23 and Ninja (the presets use Ninja)
- A C++23 compiler with a standard library that ships `<expected>` and `std::move_only_function`
  (verified: GCC 13.3; GCC 13+, MSVC 2022 and Clang with a recent libc++ are the intended targets)
- OpenSSL development files (`JWT`)

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Equivalent without presets:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Presets (`CMakePresets.json`): `debug` (warnings as errors), `release`, `debug-clang`, `static`
(static libraries only), `docs`, `msvc` (Visual Studio 2022, Windows only).

### Options

| Option | Default | Meaning |
| ------ | ------- | ------- |
| `TK4CPP_BUILD_SHARED` | `ON` (follows `BUILD_SHARED_LIBS` when set) | Build shared libraries |
| `TK4CPP_BUILD_STATIC` | `ON` (follows `BUILD_SHARED_LIBS` when set) | Build static libraries |
| `TK4CPP_BUILD_<MODULE>` | `ON` (`Lua`: `OFF`) | Enable one module, e.g. `TK4CPP_BUILD_JWT=OFF` |
| `TK4CPP_BUILD_TESTS` | `ON` when top-level | GoogleTest suites (system package, otherwise v1.15.2 is downloaded) |
| `TK4CPP_BUILD_DOCS` | `OFF` | Doxygen target `docs` (needs a root `docs/CMakeLists.txt`, not in the tree yet) |
| `TK4CPP_INSTALL` | `ON` when top-level | Generate install rules and the CMake package |
| `TK4CPP_CXX_STANDARD` | `23` | `20` or `23` (only `23` has been exercised) |
| `TK4CPP_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` |
| `TK4CPP_THIRD_PARTY_FOLDER` | `<source>/3rdParty` | Where bundled third-party sources live |

Library naming when both variants are built: the shared library is `TK4CPP::<Module>`, the static
one `TK4CPP::<Module>_static`. When only one variant is built it owns the plain name.

Module-specific options:

| Option | Module | Meaning |
| ------ | ------ | ------- |
| `TK4CPP_LIBXML2_SOURCE` = `AUTO` \| `SYSTEM` \| `VENDORED` | XML | Where libxml2 comes from (default `AUTO`) |
| `TK4CPP_LIBXML2_ROOT`, `_DIR`, `_INCLUDE_DIR`, `_LIBRARY`, `_STATIC_LIB`, `_EXTRA_LIBS` | XML | Use a pre-installed libxml2, see [XML](#xml) |
| `TK4CPP_LIBZIP_SOURCE` = `AUTO` \| `SYSTEM` \| `VENDORED` | Zip | Where libzip comes from (default `AUTO`) |
| `TK4CPP_LIBZIP_ROOT`, `_DIR`, `_INCLUDE_DIR`, `_LIBRARY`, `_EXTRA_LIBS` | Zip | Use a pre-installed libzip, see [Zip](#zip) |
| `TK4CPP_LOGGING_USE_SYSTEM_SPDLOG` | Logging | Use `find_package(spdlog)` instead of the bundled copy |
| `TK4CPP_LOGGING_BUILD_TESTS`, `_BUILD_EXAMPLES`, `_INSTALL` | Logging | Per-module switches |
| `TK4CPP_PROCESS_SANITIZE` | Process | e.g. `address;undefined` or `thread` (GCC/Clang) |
| `TK4CPP_PROCESS_BUILD_TESTS`, `_BUILD_EXAMPLES`, `_INSTALL` | Process | Per-module switches |
| `TK4CPP_ZIP_BUILD_STATIC`, `_BUILD_SHARED`, `_BUILD_TESTS`, `_BUILD_EXAMPLES`, `_BUILD_DOCS`, `_INSTALL` | Zip | Per-module switches |

## Build status

Continuous integration (`.github/workflows/ci.yml`) builds and tests every push to `main` and every
pull request on Linux (GCC, Clang), Windows (MSVC, MinGW-w64) and macOS (AppleClang), with
`TK4CPP_WARNINGS_AS_ERRORS=ON`. The badge at the top shows the current result.

Last verified locally on **2026-10-27** against commit `34069e8`: Ubuntu 24.04, GCC 13.3, CMake 3.28.3,
Ninja 1.11.1, Release, `TK4CPP_WARNINGS_AS_ERRORS=ON`, submodules initialised, default options
(shared and static variants of every module, tests on, all eight modules).

| Step | Result |
| ---- | ------ |
| Configure | succeeds; modules `Logging;Utility;JWT;XML;Encode;Process;Thread;Zip` |
| Build | 297 of 297 steps, 0 errors, about 2 min 34 s wall time |
| `ctest` | **110 of 110 tests pass** (about 4.5 s) |
| Install + consumer project | `find_package(TK4CPP COMPONENTS Utility Encode Thread)` builds and runs a program that links `TK4CPP::Encode` |

The 110 ctest entries cover every module: GoogleTest cases (Logging, Utility, JWT, XML), one
GoogleTest executable each for Encode and Thread, `process_tests` and `process_tests_forkpath` for
Process, and the Zip test programs (static and shared) plus the compiled documentation snippets.

**Not verified locally:** Windows, macOS, Clang, `TK4CPP_CXX_STANDARD=20`, sanitizer builds, the
Doxygen build, and the CMake presets other than the plain configuration above.

Known limitations:

- The aggregate package does not know `Logging`, `Process` and `Zip`; see
  [Installed package](#installed-package) for the workaround (checked: `find_package(TK4CPP COMPONENTS
  Logging)` still fails against an installed tree).
- The vendored libzip is built without any crypto backend (`ENABLE_OPENSSL=OFF`), so AES-protected
  archives need an installed libzip built with OpenSSL, see [Zip](#zip).
- `TK4CPP_BUILD_DOCS=ON` only prints a warning: there is no root `docs/CMakeLists.txt` yet, so the
  `docs` target does not exist and the `Documentation` workflow cannot build it.
- `JWT` uses a hardcoded HS256 key, see [JWT](#jwt).

## Using TK4CPP in your project

### Installed package

```sh
cmake --install build --prefix /opt/tk4cpp
```

```cmake
find_package(TK4CPP REQUIRED COMPONENTS Utility Encode Thread)
target_link_libraries(app PRIVATE TK4CPP::Utility TK4CPP::Encode)   # *_static for the static variant
```

Without `COMPONENTS`, every installed module is loaded. `TK4CPP::<Module>` is the shared library
when both variants were built, and `TK4CPP::<Module>_static` the static one.

**Known limitation:** `Logging`, `Process` and `Zip` install their own package files
(`TK4CPP_LoggingConfig.cmake`, ...) but not the `TK4CPP_<Module>Deps.cmake` files the aggregate
`TK4CPPConfig.cmake` expects, so `find_package(TK4CPP COMPONENTS Logging)` fails. Load them one by
one and point CMake at the shared package directory:

```cmake
set(_dir /opt/tk4cpp/lib/cmake/TK4CPP)
set(TK4CPP_Logging_DIR ${_dir})
set(TK4CPP_Process_DIR ${_dir})
set(TK4CPP_Zip_DIR     ${_dir})
find_package(TK4CPP_Logging REQUIRED)
find_package(TK4CPP_Process REQUIRED)
target_link_libraries(app PRIVATE TK4CPP::Logging TK4CPP::Process)
```

### In-tree

```cmake
add_subdirectory(TK4CPP)        # or FetchContent_Declare / FetchContent_MakeAvailable
target_link_libraries(app PRIVATE TK4CPP::Utility TK4CPP::Process)
```

The same `TK4CPP::<Module>` aliases exist in-tree.

## Module guide

### Utility

Namespace `TK4CPP::Utility`, headers in `TK4CPP/Utility/`.

- `Value`, `Properties`, `Property`, `Serializable`: typed, variant-based values and property sets
  (`Value.hpp`, `Properties.hpp`), with `TypeError` / `ConversionError` / `FileError` exceptions
  and `FileArtifact` for file-backed values.
- `FileHandler`: file operations with write and copy behaviours (`WriteBehavior`, `CopyBehavior`).
- `FolderHandler`: folder operations with `FolderFilter`, `CopyOptions`, `FolderStats`, `FolderDiff`.
  `FolderFilter` also selects by several name globs, exclusions, substring, case folding, kind, size range and
  modification/creation date range; `find(path, filter, std::stop_token, maxResults)` is a cancellable, capped search
  that never follows symbolic links.
- Search helpers: `globMatch`/`containsText` (`Glob.hpp`, `?` is one UTF-8 code point, linear-time), `formatSize`/`parseSize`
  (`SizeFormat.hpp`, binary, decimal and IEC units), `parseTimestamp` (`TimeParse.hpp`, `7d` or `YYYY-MM-DD[THH:MM[:SS]]`),
  `probeFile` (`FileStat.hpp`, one lstat plus creation time via `statx` on Linux and `st_birthtime` on macOS; unavailable on
  Windows), UTF-8 helpers (`Utf8.hpp`).
- `UUIDHandler`: `Generate_V4()`, `Generate_V6()`, `Generate_V7()`.
- `Version`: four-component version (`create(major, minor, patch, tweak)`, comparison).
- `System`: `freeMemory()`, `physicalCores()`, `logicalCores()`.

```cpp
#include <TK4CPP/Utility/UUIDHandler.h>
#include <TK4CPP/Utility/Version.h>

auto id = TK4CPP::Utility::UUIDHandler::Generate_V7();
auto v  = TK4CPP::Utility::Version::create(1, 2, 3, 0);   // std::shared_ptr<Version>
```

`modules/Utility/wip/` holds work-in-progress files that are not part of the build.

### JWT

Namespace `TK4CPP::JWT`, header `TK4CPP/JWT/JWTHandler.h`. A static-only `JWTHandler` creates and
validates HS256 tokens: `create(user, roles[, duration])`, `validate(token[, issuer][, claims])`,
`getUser(token)`, `setTokenDuration(...)`.

```cpp
#include <TK4CPP/JWT/JWTHandler.h>
using TK4CPP::JWT::JWTHandler;

auto token = JWTHandler::create("alice", {"admin", "dev"}, std::chrono::minutes(15));
if (JWTHandler::validate(token)) { auto user = JWTHandler::getUser(token); }
```

> **Security warning:** the HS256 signing key is a string literal compiled into
> `modules/JWT/src/JWTHandler.cpp`. Anyone with the source (or the binary) can forge tokens. Do not
> use this module to protect anything real until the key is configurable.

jwt-cpp is an implementation detail: none of its headers is part of the public API.

### XML

Namespace `TK4CPP::XML`, headers in `TK4CPP/XML/`. Abstract interfaces (`IXmlDocument`,
`IXmlElement`, `IXmlAttribute`), a libxml2 implementation (`LibXml2Document`, `LibXml2Element`,
`LibXml2Attribute`) and an `XMLManager` helper. libxml2 is a private dependency: no libxml2 header is
part of the public API. The design goal is XXE-safe parsing without network access.

libxml2 selection (`TK4CPP_LIBXML2_SOURCE`):

| Value | Behaviour |
| ----- | --------- |
| `AUTO` (default) | system libxml2 ≥ 2.9 if found, otherwise `3rdParty/libxml2` |
| `SYSTEM` | system package only, error if missing |
| `VENDORED` | always the bundled copy (built statically, symbols kept private on Linux) |

Using a libxml2 you built and installed yourself (never falls back to the system or vendored copy):

| Option | Meaning |
| ------ | ------- |
| `TK4CPP_LIBXML2_ROOT` | Installation prefix. Uses the CMake package files in it; if there are none, looks for `libxml/xmlversion.h` and the library below that prefix only |
| `TK4CPP_LIBXML2_DIR` | Directory containing `libxml2-config.cmake` (same as `-DLibXml2_DIR=...`) |
| `TK4CPP_LIBXML2_INCLUDE_DIR` + `TK4CPP_LIBXML2_LIBRARY` | Explicit include directory (the one containing `libxml/`) and library file; must be given together, no package file is used |
| `TK4CPP_LIBXML2_STATIC_LIB` | With the explicit pair: the library is static (defines `LIBXML_STATIC`) |
| `TK4CPP_LIBXML2_EXTRA_LIBS` | With the explicit pair: extra libraries a static libxml2 needs (`z;iconv;lzma`, ...) |

```sh
cmake -S . -B build -DTK4CPP_LIBXML2_ROOT=/opt/libxml2
cmake -S . -B build -DTK4CPP_LIBXML2_INCLUDE_DIR=/opt/libxml2/include/libxml2 \
                    -DTK4CPP_LIBXML2_LIBRARY=/opt/libxml2/lib/libxml2.so
```

Combining these options with `TK4CPP_LIBXML2_SOURCE=VENDORED` is an error. A consumer of the *static*
XML library has to find the same libxml2 again (`CMAKE_PREFIX_PATH`, `LibXml2_DIR`, ...).
`modules/XML/old/` contains a previous tinyxml2 backend that is not built.

### Encode

Namespace `TK4CPP::Encode`, headers `Base64.h`, `UrlEncoding.h`, `Io.h`, `Error.h`.

- `Base64`: standard and URL-safe alphabets, `Padding::{Required, Optional, None}`, `Base64::urlSafe()`
  preset (JWT style), one-shot and incremental `Encoder` / `Decoder`.
- `UrlEncoding`: percent-encoding with `Mode::Component` and `Mode::Form`, incremental
  `Encoder` / `Decoder`.
- `Io.h`: the `StreamCodec` base adds stream, file and folder processing (`FileOptions`,
  `FolderOptions`, `FolderReport`).
- Errors are returned through `std::expected<T, Error>` with a typed `ErrorCode`.

```cpp
#include <TK4CPP/Encode/Base64.h>
#include <TK4CPP/Encode/UrlEncoding.h>
using namespace TK4CPP::Encode;

std::string enc = Base64::encodeDefault("Hello, TK4CPP!");     // "SGVsbG8sIFRLNENQUCE="
if (auto dec = Base64{}.decodeToString(enc)) { /* *dec == "Hello, TK4CPP!" */ }

auto q = UrlEncoding::encode("a b&c", UrlEncoding::Mode::Component);   // "a%20b%26c"
```

A complete example is in `modules/Encode/examples/main.cpp`.

### Logging

Namespace `TK4CPP::Logging`, headers in `TK4CPP/Logging/` (include `LogLayer.h` for everything).
A thread-safe `LogManager` singleton on top of spdlog:

- `LogConfig` describes sinks and levels (`LogConfig::consoleOnly(level)`), `SinkConfig::Type` selects
  the sink kind; asynchronous mode is supported.
- `init(config)` throws on failure; `tryInit(config)` returns `std::expected<void, LogError>`.
  If `getLogger()` is called first, the manager initialises itself with `consoleOnly()`.
- `getLogger(name)` returns a `std::shared_ptr<spdlog::logger>`; `setLevel`, `setGlobalLevel`,
  `flushAll`, `shutdown`.
- Observers: `makeObserver()` / `subscribeLog()` deliver `LogNotification` records to callbacks
  (filtered by level and logger name); `subscribeSystem()` reports lifecycle events.
- `ScopedLogContext` and the `LL_GET_LOGGER` / `LL_LOG` macro family for source-located logging.
- `spdlog` is exposed in the public headers. The bundled copy is installed under a private prefix
  (`include/tk4cpp/3rdParty`) so it never collides with a system spdlog.

```cpp
#include <TK4CPP/Logging/LogLayer.h>
using namespace TK4CPP::Logging;

LogManager::instance().init(LogConfig::consoleOnly());
LL_GET_LOGGER("app")->info("hello {}", 42);
LogManager::instance().shutdown();
```

The module also builds standalone (it honours the `TK4CPP_*` variables when defined and has its own
defaults otherwise) and installs a `tk4cpp_Logging.pc` pkg-config file.

### Process

Namespace `TK4CPP::Process`, header `TK4CPP/Process.hpp`. Cross-platform child-process execution for
C++23, no third-party dependencies, no shell. See [modules/Process/README.md](modules/Process/README.md)
for the full behaviour notes and [modules/Process/CHANGELOG.md](modules/Process/CHANGELOG.md).

| Platform | Spawn | Tree ownership | Graceful stop |
| -------- | ----- | -------------- | ------------- |
| Linux / macOS / BSD | `posix_spawn` (fork/exec fallback) | process group | `SIGTERM` → `SIGKILL` |
| Windows | `CreateProcessW` + explicit handle list | Job Object | `WM_CLOSE` → `TerminateJobObject` |

Highlights: `std::expected` API with typed `ErrorCode` (a non-zero exit is not an error), thread-safe
lifecycle (`start`, `wait`, `wait_for`, `terminate`, `kill`), concurrent stdout/stderr draining with
callback / file / bounded in-memory sinks, `StdinMode::{Null, Inherit, Pipe}`, `run_sync()` with
timeout and `std::stop_token` cancellation, optional sanitizer builds.

```cpp
#include <TK4CPP/Process.hpp>
using namespace TK4CPP::Process;

Options opt;
opt.on_stdout = [](std::string_view chunk) { /* stream */ };
auto r = Process::run_sync("git", {"status", "--short"}, std::chrono::seconds(10), opt);
if (!r)                 { /* could not launch: r.error().code / .message */ }
else if (r->timed_out)  { /* killed after 10 s */ }
else if (!r->success()) { /* r->status.code or r->status.signal */ }
```

### Thread

Namespace `TK4CPP::TP`, headers in `TK4CPP/ThreadPool/`, CMake target `TK4CPP::Thread`. A prioritised,
dynamically scaling thread pool with typed event notifications and no third-party dependencies. See
[modules/Thread/README.md](modules/Thread/README.md) for the migration notes from the former Poco
based version.

- Priorities (`Low/Normal/High`), strict FIFO among equal priorities.
- Dynamic scaling between `minThreads` and `maxThreads`; idle workers retire after `idleTimeout`.
- `std::expected<..., SubmitError>` submission (`submit`, `submitWithFuture`), bounded queue with
  `OverflowPolicy::Reject` or `Block`.
- Cancellation via `ITask::cancel()`, `std::stop_token` and `shutdownNow()`.
- Per-pool `EventBus` (RAII `Connection`) with task, worker and pool events, plus the observers
  `PoolLogger`, `MetricsCollector`, `RetryHandler` and `FileLogger`.

```cpp
#include <TK4CPP/ThreadPool/ThreadPool.h>
using namespace TK4CPP::TP;

ThreadPool pool(ThreadPoolConfig::cpuBound());
pool.start();
if (auto f = pool.submitWithFuture([] { return 42; }, "answer", Priority::High)) { int v = f->get(); }
pool.shutdown();
```

Event handlers run synchronously on the emitting thread and may run concurrently; do not call
`shutdown()` from a pool thread. The Windows export path (`TK4CPP_TP_API`) is covered by CI only.

### Zip

Namespace `Zip`, headers `ZipWrapper/Zip.h` (core RAII layer: `Archive`, `Entry`, `File`, `Source`) and
`ZipWrapper/HighLevel.h` (one call per task). CMake targets: `TK4CPP::Zip` (default flavour),
`TK4CPP::Zip_static`, `TK4CPP::Zip_shared`, plus the legacy alias `ZipWrapper::ZipWrapper`.
The full manual lives in `modules/Zip/docs/` (Doxygen) and the module README
[modules/Zip/README.md](modules/Zip/README.md).

- Recursive archiving with include/exclude patterns, deterministic output, progress and cancellation,
  optional verification before the result replaces anything.
- Transparent AES password protection with no silent downgrade; password-free listing.
- Safe extraction: absolute paths, `..` components and symlink escapes are refused.
- Capabilities are detectable at run time because they depend on how libzip was built.

libzip ≥ 1.11 comes from one of two places (`TK4CPP_LIBZIP_SOURCE`):

| Value | Behaviour |
| ----- | --------- |
| `AUTO` (default) | an installed libzip when found, otherwise `3rdParty/libzip` |
| `SYSTEM` | an installed libzip only, error if missing |
| `VENDORED` | always the bundled copy (built statically, without any crypto backend) |

To use an installed libzip, pass `-DCMAKE_PREFIX_PATH=<prefix>` or one of `TK4CPP_LIBZIP_ROOT`,
`TK4CPP_LIBZIP_DIR`, or `TK4CPP_LIBZIP_INCLUDE_DIR` + `TK4CPP_LIBZIP_LIBRARY`
(+ `TK4CPP_LIBZIP_EXTRA_LIBS`). Combining these with `VENDORED` is an error.

AES encryption needs a libzip built with OpenSSL (or another crypto backend), which the vendored copy
does not have. Build and install your own and point the module at it:

```sh
cmake -S 3rdParty/libzip -B build-libzip -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
      -DENABLE_OPENSSL=ON -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF \
      -DCMAKE_INSTALL_PREFIX=/opt/libzip
cmake --build build-libzip && cmake --install build-libzip
cmake -S . -B build -DTK4CPP_LIBZIP_SOURCE=SYSTEM -DTK4CPP_LIBZIP_ROOT=/opt/libzip
```

ZIP encryption protects contents only (names, sizes and dates stay visible) and the library has not
been security-audited; read `modules/Zip/docs/security.md` before relying on it.

## Repository layout

```
.github/workflows/        ci.yml (build + test matrix), docs.yml (Doxygen to GitHub Pages)
CMakeLists.txt            root: options, module list, install and package config
CMakePresets.json         debug, release, debug-clang, static, docs, msvc
cmake/
  TK4CPPModule.cmake      tk4cpp_add_module(), tk4cpp_add_tests(), tk4cpp_target_defaults()
  TK4CPPConfig.cmake.in   aggregate find_package(TK4CPP) config
modules/<Name>/           one directory per module
  CMakeLists.txt
  include/TK4CPP/<Name>/  public headers
  src/                    implementation and private headers
  tests/                  GoogleTest suites (some modules: test/)
  examples/               optional examples
3rdParty/                 submodules: jwt-cpp, libxml2, libzip, spdlog
LICENSE
THIRD_PARTY_NOTICES.md
```

Deviations from the pattern: `Thread` keeps its headers under `include/TK4CPP/ThreadPool/`,
`Process` has a single header `include/TK4CPP/Process.hpp`, and `Zip` uses `include/ZipWrapper/`.

## Adding a module

1. Create `modules/<Name>/` with `include/TK4CPP/<Name>/`, `src/` and, optionally, `tests/`.
2. Write `modules/<Name>/CMakeLists.txt` calling `tk4cpp_add_module(NAME <Name> SOURCES ...)`
   (see `modules/JWT` for a small one, `modules/XML` for one with a private dependency). It builds the
   shared and/or static libraries, the `TK4CPP::<Name>` aliases, the install rules, the package
   `Deps` file, and generates `TK4CPP/<Name>/Export.h` with the `TK4CPP_<NAME>_EXPORT` macro.
3. Add `<Name>` to `TK4CPP_KNOWN_MODULES` in the root `CMakeLists.txt` (the order is the dependency
   order) and, if it needs other modules, set `TK4CPP_MODULE_DEPENDS_<NAME>`.
4. Decorate every public non-template class and function with `TK4CPP_<NAME>_EXPORT`. Symbols are
   hidden by default (`-fvisibility=hidden`), so an undecorated class cannot be used from the shared
   library on Linux.
5. For tests, create `tests/CMakeLists.txt` that calls `tk4cpp_add_tests(MODULE <Name> SOURCES ...)`
   and link only against targets that exist (`TK4CPP::<Name>`).

## Conventions

- Source comments are in English, Doxygen style (`@file`, `@brief`, `@param`, `@return`, `@throws`).
- Public headers live under `include/TK4CPP/<Module>/`; the namespace is `TK4CPP::<Module>`.
- Prefer `std::expected<T, Error>` for recoverable errors in new code; exceptions are used where the
  module documents them.
- Warnings: `-Wall -Wextra -Wpedantic` (`/W4 /permissive- /utf-8` on MSVC).

## Third-party software

| Library | Used by | License |
| ------- | ------- | ------- |
| [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) (+ picojson) | `JWT` | MIT (picojson: BSD-2-Clause) |
| [libxml2](https://gitlab.gnome.org/GNOME/libxml2) | `XML` | MIT |
| [spdlog](https://github.com/gabime/spdlog) | `Logging` | MIT |
| [libzip](https://libzip.org) | `Zip` | BSD-3-Clause |
| [GoogleTest](https://github.com/google/googletest) 1.15.2 | tests only | BSD-3-Clause |

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Its entries for spdlog and libzip are not
written yet.

## License

MIT — see [LICENSE](LICENSE).
