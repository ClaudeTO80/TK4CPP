# Including and linking libzip {#linking}

[TOC]

ZipWrapper is a compiled library (static and/or shared, see @ref library_build)
on top of **libzip**, and libzip in turn needs the libraries it was built with.
This page explains how to get all of them onto your include path and your link
line, with CMake, with pkg-config and with a bare compiler command.

@note Every command on this page marked **verified** was executed against
libzip 1.11.4 built from source on Linux (GCC 13, CMake 3.28, OpenSSL 3.0,
zlib 1.3, bzip2 1.0.8, liblzma 5.4, zstd 1.5). Anything marked **not verified**
(Windows, MSVC, vcpkg, Conan, the non-OpenSSL crypto backends) is written from
the libzip and tool documentation and has not been run for this project; treat
it as a starting point and check it on your toolchain.

## What has to be on the line {#linking_what}

| Piece | Include | Link | Needed when |
|---|---|---|---|
| ZipWrapper | `include/ZipWrapper/` (`#include <ZipWrapper/Zip.h>`) | `-ltk4cpp_zip` (plus `-DZIPWRAPPER_STATIC` for the static library) | always |
| libzip | the directory holding `zip.h` and `zipconf.h` | `-lzip` | always |
| zlib | not needed by you | `-lz` | static libzip only; mandatory in libzip |
| bzip2 | not needed by you | `-lbz2` | static libzip built with bzip2 |
| liblzma | not needed by you | `-llzma` | static libzip built with lzma |
| zstd | not needed by you | `-lzstd` | static libzip built with zstd |
| crypto backend | not needed by you | `-lcrypto` (OpenSSL; also `-lssl` as listed by libzip.pc), GnuTLS/Nettle, mbed TLS, or a system framework | static libzip built with AES support |

Three facts decide almost everything:

1. `Zip.h` includes `<zip.h>`, so the libzip header directory must be on your
   include path, in addition to ZipWrapper's own.
2. You **include** only ZipWrapper and libzip. The headers of zlib, bzip2,
   lzma, zstd and OpenSSL are used inside libzip's own build and never reach
   your code, so you do not need their `-I` flags, only (for a static libzip)
   their libraries at link time.
3. With a **shared** libzip the dynamic loader finds the dependencies for you,
   because they are recorded inside `libzip.so`. With a **static** `libzip.a`
   nothing is recorded: the linker needs every library listed, in the right
   order (libzip first, then the libraries it uses).

## Method 1: CMake with find_package (recommended) {#linking_cmake}

libzip installs a CMake package. ZipWrapper's own package file calls
`find_dependency(libzip 1.11 CONFIG)`, so one `find_package` is enough:

```cmake
cmake_minimum_required(VERSION 3.23)
project(myapp CXX)

find_package(TK4CPP_Zip 1.0 CONFIG REQUIRED)   # also finds libzip >= 1.11

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE TK4CPP::Zip)
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/opt/zipwrapper;/opt/libzip"
cmake --build build
```

*Verified* with a shared libzip and with a static libzip built with all
backends, for the static and for the shared ZipWrapper; every combination that
the build supports (see @ref library_staticlibzip for the one that does not)
produced a working program. `TK4CPP::Zip` is the static or the shared ZipWrapper
library; @ref cmake_integration explains how to choose.

Your own code that calls libzip directly links the imported target
`libzip::zip`; through `TK4CPP::Zip` it is already there, because ZipWrapper
links it publicly. ZipWrapper has no CMake-free header-only mode any more: the
library has to be linked.

### Telling CMake where libzip is

`find_package(libzip CONFIG)` looks for `lib/cmake/libzip/libzip-config.cmake`
under every prefix in `CMAKE_PREFIX_PATH`. Any of these work (all *verified*
except the environment variable form, which is standard CMake behaviour):

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/libzip                   # prefix
cmake -S . -B build -Dlibzip_DIR=/opt/libzip/lib/cmake/libzip         # exact dir
CMAKE_PREFIX_PATH=/opt/libzip cmake -S . -B build                     # environment
```

When libzip cannot be found, CMake stops with *"Could not find a package
configuration file provided by libzip ... Add the installation prefix of
libzip to CMAKE_PREFIX_PATH or set libzip_DIR"*; that is the message to look
for (*verified*).

### What `libzip::zip` pulls in {#linking_transitive}

The installed `libzip-config.cmake` records which optional libraries libzip
was built with and calls `find_dependency` for each one **when you configure
your project**. For the all-backends build used here it contains:

```cmake
find_dependency(ZLIB 1.1.2)
find_dependency(BZip2)
find_dependency(LibLZMA 5.2)
find_dependency(zstd 1.3.6)
find_dependency(Nettle 3.0)      # only if libzip was built with Nettle
find_dependency(GnuTLS)          # only if libzip was built with GnuTLS
find_dependency(MbedTLS 1.0)     # only if libzip was built with mbed TLS
find_dependency(OpenSSL)         # only if libzip was built with OpenSSL
```

Each line is guarded by what libzip was actually built with. The consequences:

- **Shared libzip**: `libzip::zip` has no extra link libraries (*verified*: the
  `INTERFACE_LINK_LIBRARIES` property is empty), so your link line only has
  `-lzip`. The `find_dependency` calls still run.
- **Static libzip**: `libzip::zip` carries the dependencies as imported
  targets (*verified*):

  ```
  $<LINK_ONLY:BZip2::BZip2>;$<LINK_ONLY:LibLZMA::LibLZMA>;
  $<LINK_ONLY:zstd::libzstd_static>;$<LINK_ONLY:OpenSSL::Crypto>;
  $<LINK_ONLY:ZLIB::ZLIB>
  ```

  so the final link line gets `-lbz2 -llzma -lzstd -lcrypto -lz` for free.
- Because the `find_dependency` calls run on **your** machine, the **development
  packages of every library libzip was built with must be installed there** when
  libzip is static, otherwise configuration fails. Simulating a missing zstd
  gave `find_package for module zstd called with REQUIRED ...` (*verified*).
  `zstd` is located through a small `Findzstd.cmake` that libzip installs under
  `lib/cmake/libzip/modules/`; libzip's package file adds that directory to
  `CMAKE_MODULE_PATH` itself, so you do not have to.

If one of the dependencies lives in a non-standard place, use the standard
CMake hints for that package: `ZLIB_ROOT`, `BZIP2_ROOT`, `LibLZMA_ROOT` (or
`CMAKE_PREFIX_PATH`), `zstd_ROOT`, `OPENSSL_ROOT_DIR` or `OpenSSL_ROOT`.
Passing `-DZLIB_ROOT=/usr -DOPENSSL_ROOT_DIR=/usr` configured successfully
against the static libzip (*verified*, but it only proves the variables are
accepted, since those libraries were in a default location).

## Method 2: pkg-config {#linking_pkgconfig}

libzip installs `lib/pkgconfig/libzip.pc`. For the all-backends build it reads:

```
Libs:  -L${libdir} -lzip
Libs.private:  -lbz2 -llzma -lzstd -lssl -lcrypto -lz
Cflags: -I${includedir}
```

For a **shared** libzip, `--libs` is enough; the private libraries are not
emitted:

ZipWrapper itself has no `.pc` file: list its library first and libzip after it
(shared ZipWrapper, shared libzip):

```sh
export PKG_CONFIG_PATH=/opt/libzip/lib/pkgconfig
g++ -std=c++23 -I/opt/zipwrapper/include main.cpp \
    -L/opt/zipwrapper/lib -ltk4cpp_zip \
    $(pkg-config --cflags --libs libzip) \
    -Wl,-rpath,/opt/zipwrapper/lib:/opt/libzip/lib -o myapp
```

For a **static** libzip you must add `--static`, which also prints
`Libs.private`:

```sh
export PKG_CONFIG_PATH=/opt/libzip-static/lib/pkgconfig
g++ -std=c++23 -DZIPWRAPPER_STATIC -I/opt/zipwrapper/include main.cpp \
    -L/opt/zipwrapper/lib -ltk4cpp_zip \
    $(pkg-config --cflags --libs --static libzip) -o myapp
```

Both were *verified* (the second with a static-only ZipWrapper install); the
`pkg-config` part of the second expands to
`-I.../include -L.../lib -lzip -lbz2 -llzma -lzstd -lssl -lcrypto -lz`.

@warning With **both** ZipWrapper flavours installed in the same directory,
`-ltk4cpp_zip` makes the linker pick the shared library, even if you defined
`ZIPWRAPPER_STATIC`: the program then needs `libtk4cpp_zip.so.1` at run time
(*verified*: it failed to start). To force the static library name the archive,
`-l:libtk4cpp_zip.a` (GNU ld, lld), or install only one flavour. On Windows the
static library is called `tk4cpp_zip_static.lib` and the DLL import library
`tk4cpp_zip.lib`, so the names do not clash (*not verified*).

@warning Forgetting `--static` with `libzip.a` fails at link time with
unresolved symbols such as `crc32` and `zError` (*verified*). Those come
from zlib, not from your code.

## Method 3: plain compiler command {#linking_manual}

Shared libzip, installed in `/opt/libzip` (*verified*):

```sh
# shared ZipWrapper
g++ -std=c++23 -I/opt/zipwrapper/include -I/opt/libzip/include main.cpp \
    -L/opt/zipwrapper/lib -ltk4cpp_zip -L/opt/libzip/lib -lzip \
    -Wl,-rpath,/opt/zipwrapper/lib:/opt/libzip/lib -o myapp

# static ZipWrapper (note the macro and the explicit archive name)
g++ -std=c++23 -DZIPWRAPPER_STATIC -I/opt/zipwrapper/include -I/opt/libzip/include main.cpp \
    -L/opt/zipwrapper/lib -l:libtk4cpp_zip.a -L/opt/libzip/lib -lzip \
    -Wl,-rpath,/opt/libzip/lib -o myapp
```

The second line was verified with pkg-config supplying the libzip flags; the
first line is the same command with explicit libzip flags.

Static libzip with all backends (*verified*). The order matters: **libzip
first, its dependencies after it**:

```sh
g++ -std=c++23 -DZIPWRAPPER_STATIC -I/opt/zipwrapper/include -I/opt/libzip-static/include \
    main.cpp -L/opt/zipwrapper/lib -l:libtk4cpp_zip.a \
    -L/opt/libzip-static/lib -lzip \
    -lbz2 -llzma -lzstd -lssl -lcrypto -lz -o myapp
```

(ZipWrapper first, then libzip, then libzip's libraries.) Without
`-DZIPWRAPPER_STATIC` the static library also links on Linux, because there
`ZIPWRAPPER_API` is a no-op for an archive; on Windows the macro is required.
The Windows statement is *not verified*.

Drop the libraries libzip was **not** built with. A libzip built with only
zlib links with `-lzip -lz`. The libraries to list are exactly those in the
`Libs.private` line of the installed `libzip.pc`, so when in doubt, read that
file:

```sh
grep Libs.private /opt/libzip-static/lib/pkgconfig/libzip.pc
```

On Linux, a static OpenSSL may additionally need `-ldl -lpthread`; this was not
needed with the shared OpenSSL used here and has *not been verified*.

### When `zipconf.h` or `zip.h` is not found

Both headers are installed in the same directory (`include/` of the libzip
prefix). A missing-header error means the `-I` flag is missing or points at the
wrong prefix.

## Running the program: finding the shared libraries {#linking_runtime}

A binary linked against a shared libzip, or against the shared ZipWrapper,
must find `libzip.so` and `libtk4cpp_zip.so` at start-up. The installed
`libtk4cpp_zip.so` records `$ORIGIN` and libzip's directory itself (see
@ref library_runtime).

| Platform | Mechanism | Status |
|---|---|---|
| Linux | `-Wl,-rpath,<libdir>` at link time, or `LD_LIBRARY_PATH`, or `ldconfig` for a system prefix | *verified* |
| macOS | `-Wl,-rpath,<libdir>` (the library's install name decides), or `DYLD_LIBRARY_PATH` | not verified |
| Windows | `zip.dll` next to the executable, or its directory on `PATH` | not verified |

Without any of these on Linux the program refuses to start (*verified*):

```
./myapp: error while loading shared libraries: libzip.so.5: cannot open shared object file: No such file or directory
```

With CMake, binaries in the **build tree** get the rpath automatically. For
the **installed** program, set `CMAKE_INSTALL_RPATH` or
`CMAKE_INSTALL_RPATH_USE_LINK_PATH` yourself.

The dependencies of libzip (zlib, bzip2, ...) are found by the loader through
`libzip.so` itself and are normally system libraries. On Linux you can check
what was linked in with `ldd libzip.so`; for the build used here it listed
libbz2, liblzma, libzstd, libcrypto and libz.

## Getting libzip {#linking_getlibzip}

ZipWrapper needs **libzip 1.11 or newer**. Older releases lack functions it
calls, and some distributions are far behind: Ubuntu 24.04's `libzip-dev` is
1.7.3 (*checked*), which is too old. Check with:

```sh
pkg-config --modversion libzip
```

or at run time with Zip::Archive::libzipVersion().

### Build from source with CMake (verified)

```sh
git clone --branch v1.11.4 https://github.com/nih-at/libzip.git
cmake -S libzip -B libzip-build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DENABLE_OPENSSL=ON \
    -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF \
    -DCMAKE_INSTALL_PREFIX=/opt/libzip
cmake --build libzip-build -j
cmake --install libzip-build
```

libzip's own build installs the headers, the library, `libzip.pc` and the
CMake package. Its options, and what happens to ZipWrapper:

| libzip option | Effect | Status |
|---|---|---|
| `BUILD_SHARED_LIBS=ON/OFF` | shared or static library | both *verified* |
| `ENABLE_BZIP2`, `ENABLE_LZMA`, `ENABLE_ZSTD` | optional compression methods; each is `ON` by default and is used if the library is found | `ON` and `OFF` *verified* |
| `ENABLE_OPENSSL` | AES through OpenSSL | `ON` *verified* |
| `ENABLE_GNUTLS`, `ENABLE_MBEDTLS`, `ENABLE_COMMONCRYPTO`, `ENABLE_WINDOWS_CRYPTO` | AES through another backend | `OFF` *verified*; `ON` not verified |

If every crypto backend is disabled or missing, libzip's configure step prints
*"neither Common Crypto, GnuTLS, mbed TLS, OpenSSL, nor Windows Cryptography
found; AES support disabled"* (*verified*) and AES entries cannot be written.
See @ref backends for what ZipWrapper does in that case. A zlib-only build
(`-DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF` and no crypto
backend) installs a `libzip.pc` whose `Libs.private` is just `-lz`
(*verified*).

To build the dependencies on Debian or Ubuntu:

```sh
sudo apt install build-essential cmake zlib1g-dev libbz2-dev liblzma-dev libzstd-dev libssl-dev
```

(the same package names were used here; the `-dev` packages carry the headers
and the unversioned `.so` symlinks that a source build needs).

### Package managers (not verified)

These were **not run** for this project. Check the package version first: it
must be at least 1.11.

- **vcpkg**: `vcpkg install libzip`. Optional features (bzip2, liblzma, zstd,
  and a crypto backend such as openssl, mbedtls or wincrypto) are enabled by
  vcpkg *features*; list them with `vcpkg search libzip` or read the port's
  `vcpkg.json`. Then configure with
  `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`; vcpkg puts
  the install prefix on the CMake search path, so `find_package(TK4CPP_Zip)` /
  `find_package(libzip CONFIG)` finds it.
- **Conan**: the `libzip` recipe on Conan Center exposes options for the same
  optional libraries; read `conan inspect libzip` for the option names of your
  version. Conan generates its own CMake config files, so the target name may
  differ from `libzip::zip`.
- **Homebrew / system packages**: `brew install libzip`, `apt install
  libzip-dev`, `dnf install libzip-devel`. Mind the version limit above.

### Windows and MSVC (not verified)

The steps are the same in principle: build libzip with CMake and the
Visual Studio generator (or take it from vcpkg), point `CMAKE_PREFIX_PATH` at
the install prefix, and make `zip.dll` available next to your executable. The
`_WIN32` branches of ZipWrapper were checked with a MinGW-w64 GCC syntax check
and against the MSVC diagnostics you reported; no Windows link was performed
here. See @ref platforms.

## Checklist when linking fails {#linking_troubleshooting}

| Symptom | Cause | Fix |
|---|---|---|
| `fatal error: zip.h: No such file or directory` | libzip's include directory missing | add `-I<prefix>/include`, or link `libzip::zip` / `TK4CPP::Zip` |
| `Could not find a package configuration file provided by "libzip"` | prefix unknown to CMake | `CMAKE_PREFIX_PATH` or `libzip_DIR` |
| `undefined reference to crc32` / `zError` / `BZ2_` / `lzma_` / `ZSTD_` / `EVP_` | static libzip without its dependencies | add them after `-lzip`, or use `pkg-config --static`, or the CMake target |
| `find_package for module zstd called with REQUIRED` (or BZip2, LibLZMA, OpenSSL ...) | static libzip's dependency not installed on the build machine | install its development package, or set `<PKG>_ROOT` |
| `error while loading shared libraries: libzip.so.5` | shared libzip not on the loader path | rpath, `LD_LIBRARY_PATH` or `ldconfig` |
| `no member named ... in namespace Zip`, missing `zip_*` function | libzip older than 1.11 | install libzip 1.11 or newer |
| `undefined reference to Zip::Archive::...` | the ZipWrapper library is not linked (it is no longer header-only) | link `TK4CPP::Zip`, or `-ltk4cpp_zip` before `-lzip` |
| `error while loading shared libraries: libtk4cpp_zip.so.1` | both flavours installed, `-ltk4cpp_zip` picked the shared one | `-l:libtk4cpp_zip.a`, or set `TK4CPP_ZIP_USE_STATIC_LIBS` |
| `relocation ... can not be used when making a shared object; recompile with -fPIC` | shared ZipWrapper built against a static libzip whose libraries are not PIC | use a shared libzip, or build with `-fPIC` (see @ref library_staticlibzip) |
| `ZIP_ER_COMPNOTSUPP` / `ZIP_ER_ENCRNOTSUPP` at run time | your libzip was built without that backend | see @ref backends |

@see @ref cmake_integration, @ref backends, @ref platforms
