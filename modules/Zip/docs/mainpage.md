# ZipWrapper {#mainpage}

ZipWrapper is a **C++23** library (static and shared) that makes [libzip](https://libzip.org)
pleasant to use from C++. It comes in two layers that you can mix freely:

| Layer | Header | Use it when you want to... |
|---|---|---|
| @ref zip_highlevel "High-level API" | `ZipWrapper/HighLevel.h` | archive a folder, list files, read or extract with a password, verify, change a password: **one call per task** |
| @ref zip_core "Core layer" | `ZipWrapper/Zip.h` | control the archive directly: entries, streaming reads, in-place edits, per-entry compression and encryption |

@dot
digraph layers {
    rankdir=TB;
    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
    app  [label="Your application"];
    hl   [label="High-level API  (HighLevel.h)\narchiveDirectory · listEntries · readBytes\nextractArchive · verifyArchive · changeProtection"];
    core [label="Core layer  (Zip.h)\nArchive · Entry · File · Source"];
    lib  [label="libzip (C library)\nzlib · bzip2 · lzma · zstd · AES crypto backend", style="rounded,dashed"];
    app -> hl;
    app -> core;
    hl -> core;
    core -> lib;
}
@enddot

## What you get

- **RAII everywhere.** Archives, files and sources release their libzip handles
  automatically; no manual `zip_close` / `zip_discard`.
- **Recursive archiving** of folders with include/exclude patterns, deterministic
  output, a compression policy and optional verification before the result is
  moved into place.
- **File lists** that never need a password: names, sizes, methods and the
  "is it encrypted" flag, plus an `unzip -l` style table.
- **Transparent password protection.** Give the password once in the options;
  reading works the same on plain and protected archives. AES only, never a
  silent downgrade (see @ref security).
- **Safe extraction.** Absolute paths, `..` components and symlink escapes are
  refused, with a per-entry report.
- **Honest errors.** Every failure is a Zip::Exception carrying libzip's code
  (see @ref errors), and behaviour that depends on how libzip was built is
  detectable at run time (see @ref backends).

## A first look

Archive a project, encrypted, verified before it replaces anything:

@snippet hl_archive.cpp archive

Read it back, whether or not it is encrypted:

@snippet hl_read_extract.cpp read

## Where to go next

- @subpage getting_started : build, link and run your first program
- @subpage cmake_integration : `find_package(TK4CPP_Zip)`, `add_subdirectory`, options, choosing static or shared
- @subpage library_build : the static and shared libraries, export macros, ABI limits, runtime path
- @subpage linking : include and link libzip and the libraries it uses (CMake, pkg-config, compiler)
- @subpage guide_core : the core layer: lifetimes, commit semantics, streaming, editing
- @subpage guide_highlevel : the high-level API task by task
- @subpage patterns : the include/exclude pattern language with checked examples
- @subpage security : what the protection does **and does not** do
- @subpage backends : what depends on how your libzip was built
- @subpage errors : the meaning of every error code this library raises
- @subpage pitfalls : behaviours that surprise people, each one verified
- @subpage examples : eight complete programs
- @subpage platforms : what was verified, on what, and what was not

## Requirements

- A C++23 compiler. `HighLevel.h` uses `<format>` with chrono formatting
  (in `formatListing`), which needs a recent standard library.
- libzip 1.11 or newer. zlib is mandatory; bzip2, lzma, zstd and an AES crypto
  backend are optional (see @ref backends).
- CMake 3.20 or newer if you use the provided build files.

## License

ZipWrapper is released under the **MIT License** (see the `LICENSE` file at the
root of the repository). The libraries it builds on keep their own licenses:
libzip is BSD 3-Clause, and zlib, bzip2, liblzma, zstd and the crypto backend
you link each have theirs; check them when you distribute a binary.
