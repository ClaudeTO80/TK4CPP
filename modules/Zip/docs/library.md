# Static and shared libraries {#library_build}

[TOC]

ZipWrapper is a **compiled library**: the declarations and the documentation
live in the headers under `include/ZipWrapper/`, the implementation in `src/`.
CMake builds it as a static library, a shared library, or both.

| Piece | Where | Installed |
|---|---|---|
| `Export.h` | `include/ZipWrapper/` | yes: visibility macros (see below) |
| `Zip.h`, `HighLevel.h` | `include/ZipWrapper/` | yes: the public API |
| `Zip.cpp`, `HighLevel.cpp` | `src/` | no |
| `Detail.h`, `HighLevelDetail.h` | `src/` | no: private helpers |

@note Everything on this page marked **verified** was run on Linux x86-64 with
g++ 13 and clang 18 (libc++), against libzip 1.11.4. Windows, macOS and MSVC
were **not** run; the Windows `dllexport`/`dllimport` branch of Export.h has not
been compiled by the author.

## Targets {#library_targets}

| Target | What it is | Built when |
|---|---|---|
| `TK4CPP::Zip_static` | static library `libtk4cpp_zip.a` (`tk4cpp_zip_static.lib` on Windows) | `TK4CPP_ZIP_BUILD_STATIC=ON` (default) |
| `TK4CPP::Zip_shared` | shared library `libtk4cpp_zip.so.1` (`tk4cpp_zip.dll` on Windows), `SOVERSION` = major version | `TK4CPP_ZIP_BUILD_SHARED=ON` (default) |
| `TK4CPP::Zip` | alias of one of the two | always |

`TK4CPP::Zip` is the **shared** library when `BUILD_SHARED_LIBS` is `ON` and
the shared library is built, otherwise the static one; if only one flavour is
built it is that one. Link `TK4CPP::Zip` unless you need to name a flavour. The
old name `ZipWrapper::ZipWrapper` is kept as another alias for the same target.

Both libraries link `libzip::zip` **publicly**: `Zip.h` includes `<zip.h>`, and
a static build has to hand libzip's own dependencies (zlib, bzip2, ...) on to
the final link. See @ref linking for what that means for your link line.

Building only one flavour:

```sh
cmake -S . -B build -DTK4CPP_ZIP_BUILD_SHARED=OFF   # static only
cmake -S . -B build -DTK4CPP_ZIP_BUILD_STATIC=OFF   # shared only
```

If both options are `OFF`, configuration stops with an error. When ZipWrapper
is a sub-module of TK4CPP, the defaults follow `TK4CPP_BUILD_STATIC` and
`TK4CPP_BUILD_SHARED` if the parent project defines them.

## What is exported {#library_export}

Export.h defines `ZIPWRAPPER_API`, which every public class and function
carries:

| Compiling | Defined | `ZIPWRAPPER_API` |
|---|---|---|
| static library, or code using it | `ZIPWRAPPER_STATIC` | empty |
| the shared library itself | `ZIPWRAPPER_BUILDING_SHARED` | `dllexport` (Windows), default visibility (GCC/Clang) |
| code using the shared library | nothing | `dllimport` (Windows), default visibility (GCC/Clang) |

CMake sets these for you: `ZIPWRAPPER_STATIC` is a PUBLIC definition of the
static target, `ZIPWRAPPER_BUILDING_SHARED` a PRIVATE one of the shared target.
Only when you compile the sources by hand must you define them yourself.

The shared library is built with `-fvisibility=hidden`, so only the public API
is exported. For the all-backends build used here, `nm -D` lists 91 exported
`Zip::` functions and none from `Zip::detail`. Zip::Exception is exported too,
so an exception thrown inside the shared library is caught by `catch (const
Zip::Exception&)` in your program (**verified**: the whole test suite, which
checks exactly that, passes against the shared library).

Small accessors (`Zip::Entry::index()`, `Zip::Archive::begin()`, the flag
operators, ...) stay inline in the headers; everything that touches libzip is
in the library.

## ABI limits of the shared library {#library_abi}

The public classes contain standard-library types (`std::string`,
`std::filesystem::path`, `std::function`, `std::vector`, ...). Therefore:

- build the shared library and its users with the **same compiler family,
  standard library and C++ standard** (here: C++23), and on Windows the same
  CRT (`/MD` against `/MD`, never mixed);
- a new version of the shared library is only a drop-in replacement for the same
  **major** version (`SOVERSION`), and only if the compiler/standard library
  did not change;
- if you cannot guarantee this, use the static library.

On MSVC, Export.h silences the resulting warnings C4251 and C4275 around the
exported classes (**not verified**: no MSVC build was seen).

## Static library with a static libzip {#library_staticlibzip}

The static wrapper works with a static libzip: the transitive dependencies of
`libzip.a` come along through the CMake package of libzip. **Verified**: a
static ZipWrapper built against a static libzip with all backends, installed,
then used through `find_package(TK4CPP_Zip)` by a program that listed an
archive and read AES support.

The **shared** wrapper against a *static* libzip failed in the same environment:

```
relocation R_X86_64_PC32 against symbol `ZSTD_copySequencesToSeqStoreExplicitBlockDelim'
can not be used when making a shared object; recompile with -fPIC
```

A shared object may only contain position-independent code, and the static
`libzstd.a` of the distribution is not. So for a shared ZipWrapper use a shared
libzip, or build libzip and every library it links with `-fPIC`.

## Runtime library path {#library_runtime}

An installed `libtk4cpp_zip.so` has to find `libzip.so`. The `RUNPATH` of your
executable is **not** consulted for the dependencies of a shared library (the
first install of this project failed with `libzip.so.5: cannot open shared
object file` for exactly this reason). The shared target therefore records
`$ORIGIN` and the directory of libzip as its install `RUNPATH` (system
directories are never recorded). Verified:

```
RUNPATH: [$ORIGIN:/opt/libzip/lib]
```

To choose differently, set `CMAKE_INSTALL_RPATH` (or `CMAKE_SKIP_INSTALL_RPATH`)
before adding the project; the project does not touch an rpath you defined. On
Windows put `tk4cpp_zip.dll` and `zip.dll` next to your executable (**not
verified**).

## Which flavour is tested {#library_tests}

When both libraries are built, CTest runs the unit tests against **each**:
`zip_tests` and `zip_highlevel_tests` use the default flavour, the suffixed
`zip_tests_shared` / `zip_highlevel_tests_shared` (or `_static`) the other one.
The snippets and the examples use the default flavour. Results: 109 and 179
checks, identical on the static and the shared library.

@see @ref cmake_integration, @ref linking
