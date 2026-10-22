# The high-level API {#guide_highlevel}

[TOC]

`ZipWrapper/HighLevel.h` offers one call per task. It is built on the
@ref guide_core "core layer" and includes it, so you can mix both freely.

The password is given **once, in the options**. Write functions encrypt with it;
read functions use it only for entries that are actually encrypted, so the same
code works on plain and protected archives. Before relying on the protection,
read @ref security.

| Task | Function |
|---|---|
| archive a folder or a list of files | Zip::archiveDirectory(), Zip::archiveFiles() |
| list files, totals, a printable table | Zip::listEntries(), Zip::listFiles(), Zip::summarize(), Zip::formatListing() |
| read one member | Zip::readBytes(), Zip::readText() |
| extract | Zip::extractArchive() |
| check integrity | Zip::verifyArchive() |
| add, change or remove a password | Zip::changeProtection() |

## Archiving {#hl_archiving}

@snippet hl_archive.cpp archive

What Zip::archiveDirectory() does for you:

- **Recursion with filters.** `filter.exclude` drops a path and its whole
  subtree; `filter.include` selects files. See @ref patterns.
- **Deterministic output.** Entries are sorted by name, because directory
  iteration order is unspecified.
- **Never archives its own output**, even when the output file lives inside the
  directory being archived (the nightly-backup-into-the-source case).
- **Symlinks and special files are skipped** and listed in
  Zip::ArchiveReport::skipped, never silently followed.
- **Compression policy.** Deflate level 6 by default; formats that are already
  compressed (`.jpg`, `.zip`, `.mp4`, ...) are stored. If the method you ask
  for is missing in your libzip, Deflate is used and a note appears in
  Zip::ArchiveReport::warnings. This fallback is harmless; the encryption one
  would not be, so there is none (see below).
- **Output safety.** `overwriteOutput` defaults to `false`: nothing is
  destroyed unless you ask. If the operation fails or is cancelled, an existing
  output is left untouched.
- **Verified write.** With `verifyAfterWrite` the archive is written to
  `<output>.partial`, verified, and only then moved into place, so a failed
  check cannot replace a good archive with a bad one.
- **Progress and cancel.** The callback receives the completed fraction;
  returning `false` throws `ZIP_ER_CANCELLED` and leaves the output untouched.

To archive an explicit list, with names relative to a base directory, use
Zip::archiveFiles().

### Protecting with a password

Setting `options.password` encrypts every file with AES-256. Use
`encryption` for AES-128 or AES-192, and `encryptOnly` to protect only some
files:

```cpp
Zip::ArchiveOptions o;
o.password = Zip::passwordFromEnv();          // unset => no encryption at all
o.encryptOnly = {"*.key", "secrets/**"};      // everything else stays readable
```

The request is validated **before anything is written**: an empty password, a
non-AES method, or an AES method your libzip cannot write is rejected with
`ZIP_ER_INVAL` or `ZIP_ER_ENCRNOTSUPP`. There is never a silent fallback to a
weaker method or to no encryption. Traditional PKWARE ("ZipCrypto") is not
offered at all.

## Listing {#hl_listing}

Listing never needs a password: names, sizes, methods and the encryption flag
are readable without it.

@snippet hl_read_extract.cpp list

Zip::EntryInfo records are detached from the archive, so you can keep them
after it is closed.

## Reading one member {#hl_reading}

@snippet hl_read_extract.cpp read

Without a password an encrypted entry fails with `ZIP_ER_NOPASSWD`; with a wrong
one, `ZIP_ER_WRONGPASSWD`.

## Extracting {#hl_extracting}

@snippet hl_read_extract.cpp extract

Zip::extractArchive() always runs with path-traversal, absolute-path and
symlink-escape protection; there is deliberately no option to turn it off.
By default the first problem is thrown. With `continueOnError` every problem is
collected in Zip::UnpackReport::failures instead, and the rest is still
extracted. Existing files are never overwritten unless `overwrite` is set.

## Verifying {#hl_verifying}

@snippet hl_read_extract.cpp verify

Zip::verifyArchive() streams every member and compares size and CRC-32 with the
archive. Two points to read the result correctly:

- **WinZip AES (AE-2) entries store no CRC.** Their integrity is guaranteed by
  an authentication code that libzip checks while reading, so only the size is
  compared here.
- **Encrypted entries without a password are `skipped`, never "verified".**
  `ok()` can be `true` while `skipped` is not zero, so check both.

## Changing protection {#hl_protection}

Add, change or remove the password of an existing archive, in place:

@snippet hl_protect.cpp protect

Or write a protected copy and leave the original untouched:

@snippet hl_protect.cpp copy

The in-place form writes `<name>.protect-tmp` next to the original and swaps it
in only after it was completely written; on any failure the original is left
byte for byte as it was. Entries are recompressed, but names, contents,
modification times and compression methods are preserved.

## Reports instead of surprises

Operations return a report rather than printing or hiding things:
Zip::ArchiveReport, Zip::UnpackReport, Zip::VerifyReport. Use them to show
progress summaries, to decide the exit code of a tool, or to log what was left
out. The complete program @ref 08_secure_backup.cpp shows all of them in use.
