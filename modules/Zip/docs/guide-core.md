# The core layer {#guide_core}

[TOC]

The core layer lives in `ZipWrapper/Zip.h` and mirrors libzip with RAII and
value semantics. Use it when you need control that the
@ref guide_highlevel "high-level API" does not expose.

## The model {#core_model}

| Class | What it is | Owns the libzip handle? |
|---|---|---|
| Zip::Archive | an open archive | yes (`zip_t`) |
| Zip::Entry | a member of an archive: an address and an index | no |
| Zip::File | one entry opened for streaming reads | yes (`zip_file_t`) |
| Zip::Source | data waiting to be added | until handed to an archive |

Everything is movable and non-copyable. Every libzip failure becomes a
Zip::Exception (see @ref errors).

## Writing an archive {#core_writing}

@snippet core_basic.cpp write

Three details deserve attention:

- Zip::Archive::add() **copies** the bytes immediately, so your buffer can go away.
- Zip::Archive::addFile() records the path and reads the file **when the archive
  is closed**. A missing file is therefore reported by `close()`, not by
  `addFile()` (see @ref pitfalls). The file's modification time is kept.
- Names are UTF-8, relative and use `/` as separator on every platform. Build
  them from paths with `path.generic_u8string()`, not `path.string()`.

## Commit semantics {#commit}

Changes are only *recorded* until Zip::Archive::close():

- **`close()` commits.** libzip writes a temporary file and renames it over
  the target at the end, so an existing archive is never left half-written,
  even if `close()` fails or is cancelled.
- **The destructor discards.** An archive that goes out of scope without
  `close()` leaves no file at all, mirroring `zip_discard()`. This is the
  single most common mistake; see @ref pitfalls.
- **If `close()` throws**, the archive stays open and the destructor discards
  it.
- Zip::Archive::create() replaces an existing file only at `close()`.

## Lifetime {#lifetime}

Handles refer to their archive; they do not keep it alive.

@dot
digraph lifetime {
    rankdir=LR;
    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
    archive [label="Archive\n(owns zip_t)"];
    entry   [label="Entry\n(address + index)"];
    file    [label="File\n(owns zip_file_t)"];
    iter    [label="Archive::Iterator"];
    archive -> entry [label="entry()", fontsize=9];
    archive -> file  [label="openEntry()", fontsize=9];
    archive -> iter  [label="begin()", fontsize=9];
    entry -> file    [label="open()", fontsize=9];
}
@enddot

- Zip::Entry, Zip::File and Zip::Archive::Iterator **must not outlive** the Archive.
- **Moving an Archive invalidates its Entry handles and iterators.** An Entry
  stores the address of the Archive object; re-obtain handles from the
  moved-to object.
- Closing or discarding an archive invalidates all of its handles.

## Reading {#core_reading}

Read a whole entry with Zip::Entry::readAll(), or stream it:

@snippet core_stream.cpp stream

Zip::File::close() reports CRC mismatches and AES authentication failures, so
call it when you want to be sure the data was intact.

Seeking works only when libzip reports the stream as seekable, typically for
stored, unencrypted entries:

@snippet core_stream.cpp seek

## Modifying an existing archive {#core_modifying}

Open without Zip::OpenFlag::ReadOnly to edit in place:

@snippet core_modify.cpp update

> **Collect first, delete afterwards.** After Zip::Archive::remove() the
> entry is still counted by Zip::Archive::size(), and iterating over it throws
> `ZIP_ER_DELETED`. Gather the indices in a first pass, as above.

## Copying between archives {#core_copying}

@snippet core_modify.cpp copy

Zip::Archive::copyFrom() streams the data when the destination is closed, so
the **source must stay open until then**. If the source entry is encrypted,
call `source.setDefaultPassword()` first.

## Compression and encryption {#core_protection}

Not every libzip build supports every method; detect before you rely on one:

@snippet core_protect.cpp detect

Per-entry settings are recorded like any other change and applied by `close()`:

@snippet core_protect.cpp protect

`setDefaultPassword()` makes reads transparent for the rest of the archive's life:

@snippet core_protect.cpp default-password

Unsupported requests fail cleanly with `ZIP_ER_COMPNOTSUPP` or
`ZIP_ER_ENCRNOTSUPP` and write nothing (see @ref backends). For what encryption
does and does not protect, read @ref security.

## Progress and cancellation {#core_progress}

libzip does its work during `close()`, so that is where the callback fires:

@snippet core_basic.cpp progress

Returning `false` cancels: `close()` throws `ZIP_ER_CANCELLED` and an existing
output file is left untouched. An exception thrown by the callback also cancels
and is rethrown unchanged by `close()`. The callback runs on the thread that
calls `close()`.

## Extraction {#core_extraction}

Zip::Entry::extract() and Zip::Archive::extractAll() run with
Zip::ExtractOptions. The defaults are the safe ones:

- an absolute name, a `..` component, or a path that resolves through an
  existing symlink to somewhere outside the destination is refused **before**
  anything is created;
- an existing file is not overwritten;
- a failed write removes the partial file.

Zip::Archive::extractAll() stops at the first failing entry and keeps what was
already extracted. To carry on after errors, loop over the entries yourself or
use Zip::extractArchive() with `continueOnError` (see @ref guide_highlevel).

## Raw access

Zip::Archive::native() returns the `zip_t*` for libzip calls this wrapper does
not cover. Ownership stays with the Archive.

## Thread safety

libzip allows independent `zip_t` objects to be used concurrently, but not one
object from several threads. Zip::Archive adds no hidden locking: use one
Archive per thread or synchronise externally.
