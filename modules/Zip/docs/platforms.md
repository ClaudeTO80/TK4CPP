# Platforms and verification status {#platforms}

[TOC]

This page says what was actually checked, on what, and what was **not**.

## Verified

Everything below ran on Linux x86-64 (an Ubuntu 24.04 container).

| What | Detail |
|---|---|
| Compilers | g++ 13.3 with libstdc++; clang 18.1 with libc++ 18 |
| Flags | `-std=c++23 -Wall -Wextra -Wpedantic`, no warnings from this project's code |
| libzip | 1.11.4 in three configurations: shared with OpenSSL, bzip2, lzma and zstd; shared with zlib only; static with all backends |
| Sanitizers | AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer clean (g++) |
| Static and shared library | the unit tests (109 and 179 checks) pass identically against the static and the shared ZipWrapper library with libzip shared (all backends and zlib-only) and with the static ZipWrapper against a static libzip; installed packages were consumed through `find_package(TK4CPP_Zip)` for static-only, shared-only and both-flavour installs |
| Unit tests | the core layer and the high-level layer, on the three libzip builds; tests that need an optional feature print `SKIP` on a build that lacks it |
| Documentation code | every snippet in the guides is built and run as a test |
| Interoperability | AES archives written by this library (AES-256, and AES-128 after Zip::changeProtection()) open correctly with **pyzipper 0.4.0** and **7-Zip 23.01**; an archive written by pyzipper (AES-256) is extracted correctly here; checked with a non-ASCII password |
| Test strength | the high-level tests were also run (before the library was split into header and sources) against deliberately broken copies of the header (symlink handling, password check, selective encryption, output exclusion, method preservation, `continueOnError`). Two defects that initially went undetected led to new test cases |

## Partly verified: Windows

- **MSVC.** Two portability problems were reported by an MSVC build and were
  fixed: a test used `file_clock::from_sys`, which MSVC's
  `file_time_type::clock` does not have, and `std::getenv` triggered warning
  C4996 (Zip::passwordFromEnv() now uses `_wdupenv_s` and converts to UTF-8
  there). A complete MSVC build of the fixed code has **not** been seen by the
  author.
- **MinGW-w64.** The Windows-only code paths (`#if defined(_WIN32)`) were
  syntax-checked with the MinGW-w64 GCC 13 cross compiler. That is not MSVC, and
  nothing was run on Windows.
- **Not run-tested on Windows:** non-ASCII environment variables and file names,
  and the behaviour of libzip itself with non-ASCII paths.

## Not verified

- running anything on Windows or macOS;
- MSVC as a complete build, and other compilers or older standard libraries
  (`HighLevel.h` needs `<format>` with chrono formatting, in `formatListing`);
- a shared ZipWrapper against a static libzip: it **does not link** here because the distribution's `libzstd.a` is not position-independent (see @ref library_staticlibzip);
- the Windows DLL build (`dllexport`/`dllimport` in Export.h) and macOS shared libraries;
- libzip versions other than 1.11.4;
- other crypto backends (GnuTLS, mbed TLS, Common Crypto, Windows CNG);
- vcpkg and Conan packaging;
- multi-threaded use (Zip::Archive is not thread-safe by design);
- AES-192 and traditional PKWARE encryption round trips;
- the failure branch of Zip::ArchiveOptions::verifyAfterWrite: its success path
  and cleanup are tested, but a failing verification cannot be provoked without
  a test hook.

If you build on a platform listed here as unverified and something fails, the
compiler message with the file and line is all that is needed to fix it.
