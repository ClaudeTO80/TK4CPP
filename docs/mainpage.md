# TK4CPP — Toolkit 4 C++ {#mainpage}

TK4CPP is an open source (MIT) C++ framework whose goal is to make everyday C++ development
pleasant at a high level, while providing optimised implementations underneath. Some parts are
written from scratch, others are thin, consistent wrappers around well-established third-party
libraries so that you do not have to learn each library's quirks.

* **Cross-compiler** — built and tested with GCC, Clang and MSVC.
* **Modular** — every module is an independent library, built as a static library, a shared
  library, or both.
* **CMake first** — configurable with CMake options, consumable with `find_package()`.

## Modules

| Module | Description |
| ------ | ----------- |
| `Utility` | Typed values and property bags, file helpers, versions, UUIDs, system information |
| `JWT` | JSON Web Token creation and validation |
| `XML` | XML documents: abstract interfaces and a libxml2 implementation |

More modules (Logging, DataLayer, Chart, ArgsParse, …) will be added over time.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/install
```

Useful options:

| Option | Default | Meaning |
| ------ | ------- | ------- |
| `TK4CPP_BUILD_SHARED` | `ON` | Build shared libraries |
| `TK4CPP_BUILD_STATIC` | `ON` | Build static libraries |
| `TK4CPP_BUILD_TESTS`  | `ON` when top-level | Build the unit tests |
| `TK4CPP_BUILD_<MODULE>` | `ON` | Enable one module, e.g. `TK4CPP_BUILD_JWT` |
| `TK4CPP_CXX_STANDARD` | `23` | C++ standard (`20` or `23`) |

## Using TK4CPP from CMake

```cmake
find_package(TK4CPP REQUIRED COMPONENTS Utility JWT XML)
target_link_libraries(my_app PRIVATE TK4CPP::Utility TK4CPP::JWT)
```

When both variants were built, `TK4CPP::<Module>` is the shared library and
`TK4CPP::<Module>_static` the static one.

Or as a subdirectory / `FetchContent` dependency:

```cmake
add_subdirectory(external/TK4CPP)
target_link_libraries(my_app PRIVATE TK4CPP::Utility)
```

## License

TK4CPP is released under the MIT license; see the `LICENSE` file in the repository.
