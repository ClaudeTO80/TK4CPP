# Errors {#errors}

[TOC]

Every libzip failure is reported as a Zip::Exception. Its `what()` starts with
the failing operation (for example `zip_open: No such file`), and
Zip::Exception::zipCode() returns the `ZIP_ER_*` code from `<zip.h>` for
programmatic handling. Zip::Exception::systemCode() carries an `errno` value when the
failure came from the operating system.

@snippet core_errors.cpp errors

## Codes this library raises

The table lists the codes you will meet through this API and where they come
from. Other `ZIP_ER_*` values can also be raised by libzip itself (for example
`ZIP_ER_NOZIP` when a file is not a ZIP archive) and are passed through
unchanged.

| Code | Raised when |
|---|---|
| `ZIP_ER_NOENT` | the archive file does not exist; an entry name is not in the archive (Zip::Archive::entry(), Zip::readBytes()); the directory given to Zip::archiveDirectory() or a path given to Zip::archiveFiles() does not exist |
| `ZIP_ER_EXISTS` | Zip::Archive::add() with a name already in use and `overwrite` unset; an output file exists and `overwriteOutput` is `false`; an extraction target exists and `overwrite` is unset; opening with Zip::OpenFlag::Create and Zip::OpenFlag::Exclusive on an existing file |
| `ZIP_ER_INVAL` | an invalid argument or state: an entry index out of range; **an unsafe path during extraction** (absolute, `..`, or escaping through a symlink); a compression level above 9; an empty password or a non-AES method in the high-level API; a path outside the base directory in Zip::archiveFiles(); the input and output of Zip::changeProtection() being the same file; **using an archive that is already closed** |
| `ZIP_ER_NOPASSWD` | an encrypted entry is read without a password |
| `ZIP_ER_WRONGPASSWD` | an encrypted entry is read with a wrong password |
| `ZIP_ER_COMPNOTSUPP` | a compression method is requested or encountered that this libzip build lacks (see @ref backends) |
| `ZIP_ER_ENCRNOTSUPP` | an encryption method is requested that this libzip build lacks; in the high-level API this is raised **before anything is written** |
| `ZIP_ER_CANCELLED` | a progress callback returned `false` (Zip::Archive::setProgressCallback(), Zip::ArchiveOptions::progress, Zip::UnpackOptions::progress) |
| `ZIP_ER_CRC` | Zip::ArchiveOptions::verifyAfterWrite found a problem in the archive it just wrote; libzip also reports data corruption detected while reading |
| `ZIP_ER_READ` | a file added with Zip::Archive::addFile() cannot be read; **reported by `close()`**, not by `addFile()` |
| `ZIP_ER_DELETED` | metadata is requested for an entry removed with Zip::Archive::remove() (typically by iterating after a removal) |
| `ZIP_ER_WRITE` | writing an extracted file failed |
| `ZIP_ER_OPEN` | an extraction target could not be opened for writing |
| `ZIP_ER_MEMORY` | a buffer for Zip::Source::fromBuffer() could not be allocated |

## Other exceptions

- `std::filesystem::filesystem_error` is thrown by extraction for filesystem
  failures (for example an unwritable destination). With
  Zip::UnpackOptions::continueOnError it is recorded in
  Zip::UnpackReport::failures with `zipCode == -1` instead.
- An exception thrown by a progress callback is rethrown unchanged by
  Zip::Archive::close() (it is **not** wrapped in a Zip::Exception).

## Two errors that arrive later than you expect

libzip does its work when an archive is closed, so these surface at `close()`:

@snippet core_errors.cpp lazy-error

If `close()` throws, the archive stays open and is discarded when it is
destroyed; an existing file at the destination is left untouched.

## Handling advice

- Treat `ZIP_ER_NOPASSWD` and `ZIP_ER_WRONGPASSWD` as "ask the user for a
  password", not as corruption.
- Treat `ZIP_ER_ENCRNOTSUPP` as "this machine cannot protect the data", and do
  **not** retry without encryption (see @ref security).
- Treat `ZIP_ER_INVAL` during extraction as a hostile or damaged archive.
- With the high-level API, prefer `continueOnError` plus the report when you want
  to show the user every problem at once.
