# CMake integration {#cmake_integration}

[TOC]

ZipWrapper is a compiled library with a static and a shared flavour (see
@ref library_build). The project is `TK4CPP_Zip`; the imported target you link
is `TK4CPP::Zip`. It carries the include directory, the C++23 requirement, the
export definitions and the link dependency on `libzip::zip`.

## Installed package

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/libzip
cmake --build build
ctest --test-dir build
cmake --install build --prefix /path/to/install
```

The install step puts the headers under `include/ZipWrapper/`, the libraries
under `lib/` (shared: `libtk4cpp_zip.so.1`, static: `libtk4cpp_zip.a`) and the
CMake package under `lib/cmake/TK4CPP_Zip/`. In your project:

```cmake
find_package(TK4CPP_Zip 1.0 CONFIG REQUIRED)    # also finds libzip >= 1.11
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE TK4CPP::Zip)
```

```cpp
#include <ZipWrapper/HighLevel.h>   // includes Zip.h
```

If ZipWrapper or libzip live outside the default search paths, add both to
`CMAKE_PREFIX_PATH`. The package version check is `SameMajorVersion`.

### Choosing the flavour in your project

The package defines `TK4CPP::Zip_static` and `TK4CPP::Zip_shared` (only those
that were installed) and `TK4CPP::Zip` as an alias of one of them. Before
`find_package` you may say which one you want:

```cmake
set(TK4CPP_ZIP_USE_STATIC_LIBS ON)     # or OFF for the shared library
find_package(TK4CPP_Zip 1.0 CONFIG REQUIRED)
```

Without the variable you get the flavour the package was built to prefer. If
the requested flavour was not installed, the other one is used instead. All of
this was **verified** with packages installed with both flavours, static only
and shared only.

## As a subdirectory

```cmake
add_subdirectory(third_party/zip-libzip)
target_link_libraries(myapp PRIVATE TK4CPP::Zip)
```

When ZipWrapper is not the top-level project, tests, examples and install rules
are off by default (unless the parent defines `TK4CPP_BUILD_TESTS` /
`TK4CPP_INSTALL`), so it only adds the library targets.

## Options

| Option | Default (top level) | Effect |
|---|---|---|
| `TK4CPP_ZIP_BUILD_STATIC` | `ON` | build the static library |
| `TK4CPP_ZIP_BUILD_SHARED` | `ON` | build the shared library |
| `TK4CPP_ZIP_BUILD_TESTS` | `ON` | the unit tests (against every built flavour) and the documentation snippets, registered with CTest |
| `TK4CPP_ZIP_BUILD_EXAMPLES` | `ON` | the eight programs of @ref examples |
| `TK4CPP_ZIP_INSTALL` | `ON` | install and export rules |
| `TK4CPP_ZIP_BUILD_DOCS` | `OFF` | the `docs` target that builds this manual (needs Doxygen and Graphviz) |
| `BUILD_SHARED_LIBS` | unset | decides which flavour `TK4CPP::Zip` is when both are built |
| `TK4CPP_WARNINGS_AS_ERRORS` | unset | adds `-Werror` / `/WX` to the library, tests and examples |

## libzip and its libraries

How libzip itself and the libraries it uses (zlib, bzip2, lzma, zstd, a crypto
backend) reach your include path and link line is covered in detail, with
verified commands, in @ref linking. In short: with CMake one `find_package`
is enough, and with a static libzip the dependencies are pulled in
transitively through libzip's own package file, which means their development
packages must be installed on the build machine.

## Building this manual

```sh
cmake -S . -B build -DTK4CPP_ZIP_BUILD_DOCS=ON -DCMAKE_PREFIX_PATH=/path/to/libzip
cmake --build build --target docs
```

or, without CMake and libzip, straight from the repository root:

```sh
doxygen Doxyfile      # writes build-docs/html/index.html
```

Doxygen 1.9.8 and Graphviz are needed. Warnings are treated as errors, so an
undocumented public symbol or a broken cross-reference fails the build.

## Compiler notes

The build files request C++23 (`cxx_std_23`) and add `-Wall -Wextra -Wpedantic`
(or `/W4 /permissive- /utf-8` on MSVC) to the library, the tests, the snippets and the examples.
See @ref platforms for the toolchains this was actually verified with.
