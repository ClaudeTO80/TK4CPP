# TK4CPP — Toolkit 4 C++

A cross-compiler C++ framework that gives you high-level, easy-to-use APIs backed by optimised
low-level implementations. Some parts are written from scratch, others are consistent wrappers
around third-party libraries. MIT licensed.

> **Status:** early development (0.1.0). APIs may still change.

## Modules

| Module    | Status | Description |
| --------- | ------ | ----------- |
| `Utility` | usable | Typed `Value`/`Properties`, file helpers, `Version`, UUID v4/v6/v7, system info |
| `JWT`     | usable | JSON Web Token creation/validation (HS256), on top of vendored jwt-cpp |
| `XML`     | usable | XML documents: abstract interfaces plus a libxml2 backend (XXE-safe parsing, no network access) |
| `Logging`, `DataLayer`, `Chart`, `ArgsParse`, `Lua` | planned | Not in the tree yet; already reserved in the root `CMakeLists.txt` |

Every module is built as a shared library, a static library, or both.

## Build

Requires CMake ≥ 3.21 and a C++20/23 compiler (GCC ≥ 13, Clang ≥ 18, MSVC 2022). `JWT` needs OpenSSL. `XML` uses the system libxml2 (≥ 2.9) when present, otherwise the copy in `3rdParty/libxml2` (`-DTK4CPP_LIBXML2_SOURCE=AUTO|SYSTEM|VENDORED`). To use a libxml2 you built and installed yourself, see [Using a pre-installed libxml2](#using-a-pre-installed-libxml2).

```sh
cmake --preset release            # or: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build --preset release
ctest --preset release
```

| Option | Default | |
| ------ | ------- | - |
| `TK4CPP_BUILD_SHARED` / `TK4CPP_BUILD_STATIC` | ON / ON (follows `BUILD_SHARED_LIBS` when set) | library variants |
| `TK4CPP_BUILD_<MODULE>` | ON | enable a module (`TK4CPP_BUILD_JWT=OFF`, …) |
| `TK4CPP_BUILD_TESTS` | ON if top-level | GoogleTest suites (system package, else downloaded) |
| `TK4CPP_BUILD_DOCS` | OFF | Doxygen, target `docs` → `build/docs/html` |
| `TK4CPP_CXX_STANDARD` | 23 | `20` or `23` |
| `TK4CPP_WARNINGS_AS_ERRORS` | OFF | |

## Use

```cmake
find_package(TK4CPP REQUIRED COMPONENTS Utility JWT XML)
target_link_libraries(app PRIVATE TK4CPP::Utility TK4CPP::JWT)   # *_static for the static variant
```

or `add_subdirectory(TK4CPP)` / `FetchContent` — the same `TK4CPP::<Module>` aliases exist in-tree.

## Layout

```
CMakeLists.txt          root: options, module list, install/config
cmake/                  tk4cpp_add_module(), tk4cpp_add_tests(), TK4CPPConfig.cmake.in
modules/<Name>/         CMakeLists.txt, include/TK4CPP/<Name>/, src/, tests/
3rdParty/               vendored header-only dependencies (see THIRD_PARTY_NOTICES.md)
docs/                   Doxyfile.in, mainpage.md  (published to GitHub Pages by .github/workflows/docs.yml)
```

### Adding a module

1. Create `modules/<Name>/` with `include/TK4CPP/<Name>/`, `src/`, optional `tests/`.
2. Write `modules/<Name>/CMakeLists.txt` calling `tk4cpp_add_module(NAME <Name> SOURCES … )`
   (see `modules/Utility`). It generates `TK4CPP/<Name>/Export.h` with the `TK4CPP_<NAME>_EXPORT` macro.
3. Add `<Name>` to `TK4CPP_KNOWN_MODULES` in the root `CMakeLists.txt` (order = dependency order).
4. Decorate public non-template classes/functions with `TK4CPP_<NAME>_EXPORT`. Symbols are hidden by default.

## Conventions

* Source comments are English, Doxygen style (`@brief`, `@param`, `@return`).
* Headers live under `include/TK4CPP/<Module>/`; namespace `TK4CPP::<Module>`.

## License

MIT — see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Using a pre-installed libxml2

Point the `XML` module at a libxml2 that was installed earlier (e.g. `cmake --install <build> --prefix /opt/libxml2`).
The library must be found there: there is no fallback to the system or vendored copy.

| Option | Meaning |
| ------ | ------- |
| `TK4CPP_LIBXML2_ROOT` | Installation prefix. Uses the CMake package files in it; if there are none, looks for `libxml/xmlversion.h` and `libxml2` below that prefix only |
| `TK4CPP_LIBXML2_DIR` | Directory containing `libxml2-config.cmake` (same as `-DLibXml2_DIR=...`) |
| `TK4CPP_LIBXML2_INCLUDE_DIR` + `TK4CPP_LIBXML2_LIBRARY` | Explicit include directory (the one containing `libxml/`) and library file; must be given together, no package file is used |
| `TK4CPP_LIBXML2_STATIC_LIB` | With the explicit pair: the library is static (defines `LIBXML_STATIC`) |
| `TK4CPP_LIBXML2_EXTRA_LIBS` | With the explicit pair: extra libraries a static libxml2 needs (`z;iconv;lzma`, ...) |

```sh
cmake -S . -B build -DTK4CPP_LIBXML2_ROOT=/opt/libxml2
cmake -S . -B build -DTK4CPP_LIBXML2_INCLUDE_DIR=/opt/libxml2/include/libxml2 \
                    -DTK4CPP_LIBXML2_LIBRARY=/opt/libxml2/lib/libxml2.so
```

A consumer of the *static* TK4CPP XML library has to find the same libxml2 again (`CMAKE_PREFIX_PATH`, `LibXml2_DIR`, ...).
Combining these options with `TK4CPP_LIBXML2_SOURCE=VENDORED` is an error.
