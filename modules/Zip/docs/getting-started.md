# Getting started {#getting_started}

[TOC]

## 1. Get libzip

ZipWrapper needs **libzip 1.11 or newer**. Use your package manager, or build
it from source and point CMake at it with `CMAKE_PREFIX_PATH` (see
@ref cmake_integration). Which compression methods and whether AES encryption
are available depends on how libzip was configured; see @ref backends.

## 2. Build the tests and examples

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/libzip
cmake --build build
ctest --test-dir build
```

This builds the static and the shared library (`TK4CPP_ZIP_BUILD_STATIC` /
`TK4CPP_ZIP_BUILD_SHARED`, see @ref library_build); then `ctest` runs the unit
tests against each of them and every code snippet shown in this manual.
Tests that need an optional libzip feature print `SKIP` when your build lacks it.

## 3. Link it in your project

```cmake
find_package(TK4CPP_Zip 1.0 CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE TK4CPP::Zip)
```

ZipWrapper is a compiled library, so it has to be linked; libzip and its own
libraries come along through CMake. Without CMake see @ref linking.

## 4. Write an archive and read it back

This is the core layer: explicit and close to libzip.

@snippet core_basic.cpp write

Reading it back, and extracting:

@snippet core_basic.cpp read

@snippet core_basic.cpp extract

Two details matter right away:

- **`close()` commits.** An archive that goes out of scope without it is
  discarded and no file is written (see @ref lifetime).
- **Nothing is read from disk until `close()`** for files added with
  `addFile()`; bytes added with `add()` are copied immediately.

## 5. Or do the same in one call

The high-level API handles whole tasks:

@snippet hl_archive.cpp archive

## 6. Handle errors

Every libzip failure is a Zip::Exception with a numeric code:

@snippet core_errors.cpp errors

Some errors surface later than you might expect, because libzip does its work
when the archive is closed:

@snippet core_errors.cpp lazy-error

See @ref errors for the full list of codes and when they occur.

## Where next

- The core layer in depth: @ref guide_core
- Tasks made simple: @ref guide_highlevel
- Eight complete programs: @ref examples
