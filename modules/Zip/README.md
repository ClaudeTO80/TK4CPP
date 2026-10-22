# ZipWrapper

**C++23** library (static and/or shared) that makes [libzip](https://libzip.org) pleasant to
use from C++. Two layers, freely mixable:

- **High-level API** (`ZipWrapper/HighLevel.h`): one call per task. Archive a
  folder recursively, list files, read or extract with a password, verify, add
  or change a password.
- **Core layer** (`ZipWrapper/Zip.h`): RAII wrappers (`Archive`, `Entry`, `File`,
  `Source`) for when you need control: streaming reads, in-place edits,
  per-entry compression and encryption.

```cpp
#include <ZipWrapper/HighLevel.h>

// Archive a project: filters, AES-256 if a password is set, verified before it
// replaces anything.
Zip::ArchiveOptions options;
options.filter.exclude = {".git", "node_modules", "*.tmp"};
options.password = Zip::passwordFromEnv();     // unset => no encryption at all
options.overwriteOutput = true;
options.verifyAfterWrite = true;
auto report = Zip::archiveDirectory("project", "backup.zip", options);

// File lists never need the password.
std::cout << Zip::formatListing(Zip::listEntries("backup.zip"));

// Same call on plain and encrypted archives.
auto text = Zip::readText("backup.zip", "docs/guide.md", Zip::passwordFromEnv());
```

## Features

- **RAII everywhere.** No manual `zip_close` / `zip_discard`; every libzip failure
  is a `Zip::Exception` carrying libzip's code.
- **Recursive archiving** with include/exclude patterns, deterministic output,
  a compression policy (already-compressed formats are stored), progress and
  cancellation, and optional verification before the result is moved into place.
- **Transparent password protection.** Give the password once; reading works the
  same on plain and protected archives. WinZip AES only, with **no silent
  downgrade**: if the AES method you asked for is unavailable, the call fails
  before anything is written.
- **Safe extraction.** Absolute paths, `..` components and symlink escapes are
  refused, with a per-entry report instead of a single exception.
- **Detectable capabilities.** Which compression and encryption methods exist
  depends on how libzip was built; the library tells you at run time.

## Documentation

The full manual is generated with Doxygen from this repository:

```sh
doxygen Doxyfile                                    # -> build-docs/html/index.html
# or, with CMake
cmake -S . -B build -DTK4CPP_ZIP_BUILD_DOCS=ON -DCMAKE_PREFIX_PATH=/path/to/libzip
cmake --build build --target docs                   # -> build/docs/html/index.html
```

It needs Doxygen 1.9.8 and Graphviz. Warnings are treated as errors, so an
undocumented public symbol, a broken cross-reference or a code snippet that no
longer exists fails the build.

| Page | Content |
|---|---|
| Getting started | build, link and run a first program |
| CMake integration | `find_package(TK4CPP_Zip)`, `add_subdirectory`, options, choosing static or shared |
| Static and shared libraries | targets, export macros, ABI limits, runtime path, verified combinations |
| Including and linking libzip | CMake, pkg-config, compiler lines; libzip's dependencies; runtime path; building libzip |
| The core layer | lifetimes, commit semantics, streaming, editing, copying |
| The high-level API | every task, with reports instead of surprises |
| Patterns | the include/exclude language, with checked examples |
| Security notes | what the protection does **and does not** do |
| libzip build options | what depends on how your libzip was built |
| Errors | what each `ZIP_ER_*` code means here |
| Pitfalls | fifteen behaviours that surprise people |
| Examples | eight complete programs |
| Platforms | what was verified, and what was not |

Every code snippet in the manual lives in `docs/snippets/` and is built and run
by the test suite, so the documentation cannot show code that does not compile.

## Requirements

- A C++23 compiler. `HighLevel.h` uses `<format>` with chrono formatting (in
  `formatListing`), which needs a recent standard library.
- libzip **1.11 or newer** (zlib is mandatory; bzip2, lzma, zstd and an AES
  crypto backend are optional).
- CMake 3.20 or newer for the provided build files.

## Build, test, use

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/libzip
cmake --build build
ctest --test-dir build            # unit tests + every documentation snippet
cmake --install build --prefix /path/to/install
```

In your project:

```cmake
find_package(TK4CPP_Zip 1.0 CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE TK4CPP::Zip)
```

Or `add_subdirectory(zip-libzip)` and link the same target. ZipWrapper is a
**compiled library**, not header-only: the declarations are in
`include/ZipWrapper/`, the implementation in `src/`, and CMake builds a static
library, a shared library, or both. `TK4CPP::Zip` is the shared one when
`BUILD_SHARED_LIBS` is `ON`, otherwise the static one; `TK4CPP::Zip_static` and
`TK4CPP::Zip_shared` name a flavour explicitly. Set
`TK4CPP_ZIP_USE_STATIC_LIBS` before `find_package` to choose in your project.
Details, ABI limits and the runtime path are on the "Static and shared
libraries" page (`docs/library.md`).

| CMake option | Default (top level) | Effect |
|---|---|---|
| `TK4CPP_ZIP_BUILD_STATIC` | `ON` | build the static library |
| `TK4CPP_ZIP_BUILD_SHARED` | `ON` | build the shared library |
| `TK4CPP_ZIP_BUILD_TESTS` | `ON` | unit tests (run against every built flavour) and documentation snippets |
| `TK4CPP_ZIP_BUILD_EXAMPLES` | `ON` | the eight example programs |
| `TK4CPP_ZIP_INSTALL` | `ON` | install and export rules |
| `TK4CPP_ZIP_BUILD_DOCS` | `OFF` | the `docs` target (needs Doxygen and Graphviz) |

## Including and linking libzip

ZipWrapper is a library you link (`-ltk4cpp_zip`, or the CMake target
`TK4CPP::Zip`). On top of it, **libzip** must be on the include path (`Zip.h`
includes `<zip.h>`) and on the link line, and a *static* libzip also needs the
libraries it was built with. Full details, including
troubleshooting, are in the Doxygen page "Including and linking libzip"
(`docs/linking.md`). Commands marked *verified* were run against libzip 1.11.4
on Linux; Windows, vcpkg and Conan are **not verified**.

| Piece | Link flag | Needed |
|---|---|---|
| ZipWrapper | `-ltk4cpp_zip` (+ `-DZIPWRAPPER_STATIC` for the static library) | always |
| libzip | `-lzip` | always |
| zlib | `-lz` | static libzip (mandatory in libzip) |
| bzip2 / liblzma / zstd | `-lbz2` / `-llzma` / `-lzstd` | static libzip built with them |
| crypto backend | `-lcrypto` (OpenSSL), or GnuTLS/Nettle, mbed TLS | static libzip built with AES |

**CMake (recommended).** One call finds ZipWrapper and libzip; a static libzip
brings its dependencies in through its own CMake package:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/opt/zipwrapper;/opt/libzip"   # or -Dlibzip_DIR=/opt/libzip/lib/cmake/libzip
```

**pkg-config.** Add `--static` for a static libzip, otherwise the link fails
with unresolved `crc32`/`zError`:

```sh
g++ -std=c++23 -I/opt/zipwrapper/include main.cpp -L/opt/zipwrapper/lib -ltk4cpp_zip $(pkg-config --cflags --libs libzip) -Wl,-rpath,/opt/zipwrapper/lib:/opt/libzip/lib -o myapp     # shared libzip
g++ -std=c++23 -DZIPWRAPPER_STATIC -I/opt/zipwrapper/include main.cpp -L/opt/zipwrapper/lib -l:libtk4cpp_zip.a $(pkg-config --cflags --libs --static libzip) -o myapp   # static libzip
```

**Plain compiler.** Libzip first, its dependencies after it:

```sh
g++ -std=c++23 -I/opt/zipwrapper/include -I/opt/libzip/include main.cpp -L/opt/zipwrapper/lib -ltk4cpp_zip -L/opt/libzip/lib -lzip -Wl,-rpath,/opt/zipwrapper/lib:/opt/libzip/lib -o myapp
g++ -std=c++23 -DZIPWRAPPER_STATIC -I/opt/zipwrapper/include -I/opt/libzip-static/include main.cpp -L/opt/zipwrapper/lib -l:libtk4cpp_zip.a -L/opt/libzip-static/lib -lzip -lbz2 -llzma -lzstd -lssl -lcrypto -lz -o myapp
```

A shared libzip must be found at run time (rpath, `LD_LIBRARY_PATH`, or
`ldconfig` on Linux; `zip.dll` next to the executable on Windows).

**Getting libzip 1.11+** (distribution packages are often older; Ubuntu 24.04
ships 1.7.3):

```sh
git clone --branch v1.11.4 https://github.com/nih-at/libzip.git
cmake -S libzip -B libzip-build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DENABLE_OPENSSL=ON \
      -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF -DCMAKE_INSTALL_PREFIX=/opt/libzip
cmake --build libzip-build -j && cmake --install libzip-build
```

Optional backends are switched with `ENABLE_BZIP2`, `ENABLE_LZMA`,
`ENABLE_ZSTD`, `ENABLE_OPENSSL` (also `ENABLE_GNUTLS`, `ENABLE_MBEDTLS`,
`ENABLE_COMMONCRYPTO`, `ENABLE_WINDOWS_CRYPTO`); what works at run time is
described in the "libzip build options" page.

## Examples

Eight complete programs in `examples/`, all built by the test suite:
`backup_directory`, `list_archive`, `extract`, `plugin_package`, `update_archive`,
`encrypted_archive`, `verify_and_tail` and `secure_backup`, a full backup tool
(pack, list, cat, unpack, verify, passwd) written **only** with the high-level API.

```sh
ZIP_PASSWORD='long random passphrase' ./build/examples/secure_backup pack project backup.zip \
    --exclude .git --exclude node_modules
./build/examples/secure_backup list backup.zip          # no password needed
```

## Security in one paragraph

ZIP encryption protects the **contents** only. Names, sizes, dates and the number
of entries stay visible without the password, the archive structure is not
authenticated, and the key derivation of the format is modest, so use a long random
passphrase. Passwords are not wiped from memory and should never be passed on the
command line. Extraction blocks path traversal and symlink escapes but does not
limit decompressed size. This library has not been security-audited. Read the
Security notes page before relying on it.

## Verification status

Verified on Linux x86-64 with g++ 13.3 (libstdc++) and clang 18.1 (libc++ 18),
`-std=c++23 -Wall -Wextra -Wpedantic`, against libzip 1.11.4 in three
configurations (shared with all backends, shared with zlib only, static with all
backends), with AddressSanitizer and UndefinedBehaviorSanitizer (g++). The unit
tests pass identically (109 and 179 checks) against the static and the shared
ZipWrapper library; a shared ZipWrapper against a *static* libzip does not link
with the distribution's non-PIC `libzstd.a` (see "Static and shared libraries").
AES archives were checked for interoperability in both directions with
pyzipper 0.4.0 and 7-Zip 23.01, using a non-ASCII password.

**Not verified:** running anything on Windows or macOS, a complete MSVC build
(two MSVC portability problems that were reported have been fixed), other
libzip versions and crypto backends, vcpkg/Conan, multi-threaded use, and the
failure branch of `verifyAfterWrite`. The Platforms page has the exact list.

## License

ZipWrapper is released under the **MIT License**; see [`LICENSE`](LICENSE). Every
source file carries an `SPDX-License-Identifier: MIT` line.

The libraries it builds on keep their own licenses: libzip is BSD 3-Clause, and
zlib, bzip2, liblzma, zstd and the crypto backend you link (for example OpenSSL)
each have theirs. The MIT license of this wrapper does not change those, so
check them when you distribute a binary.

## Repository layout

```
LICENSE               MIT License
include/ZipWrapper/   public headers: Zip.h (core), HighLevel.h (high-level), Export.h
src/                  implementation (Zip.cpp, HighLevel.cpp) and private helper headers
docs/                 Doxygen pages (*.md), groups and examples (*.dox)
docs/snippets/        code shown in the manual, built and run as tests
examples/             eight complete programs
tests/                unit tests for both layers
cmake/                package configuration templates (TK4CPP_ZipConfig.cmake.in)
Doxyfile              Doxygen configuration (warnings are errors)
```
