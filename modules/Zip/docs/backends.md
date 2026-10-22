# libzip build options {#backends}

[TOC]

libzip is configurable. **zlib is mandatory**; bzip2, lzma, zstd and the AES
crypto backend (OpenSSL, GnuTLS, mbed TLS, Common Crypto or Windows CNG) are
optional. What works on a given machine therefore depends on how its libzip was
built. ZipWrapper compiles against any libzip 1.11 or newer and decides at
**run time**.

| Feature | Always available? | Needs in libzip |
|---|---|---|
| Zip::Compression::Store, Zip::Compression::Deflate | yes | zlib (mandatory) |
| Zip::Compression::BZip2 | no | bzip2 |
| Zip::Compression::Lzma | no | liblzma |
| Zip::Compression::Zstd | no | zstd |
| Zip::Encryption::AES128 / AES192 / AES256 | no | a crypto backend |
| Zip::Encryption::Traditional | yes | nothing (but it is weak; see @ref security) |

## Detecting what is available

@snippet core_protect.cpp detect

Zip::Archive::compressionSupported() and Zip::Archive::encryptionSupported()
test one method; Zip::Archive::supportedCompressions() and
Zip::Archive::supportedEncryptions() list them. Each takes a flag to ask about
writing (the default) or about reading. Zip::Archive::libzipVersion() reports
the version loaded at run time, which is useful in bug reports.

## What happens when something is missing

| Situation | Behaviour |
|---|---|
| ask for an unsupported compression or encryption | Zip::Exception with `ZIP_ER_COMPNOTSUPP` / `ZIP_ER_ENCRNOTSUPP`; nothing is written |
| high-level call with a password but no AES backend | `ZIP_ER_ENCRNOTSUPP` **before anything is written**; never a silent downgrade |
| high-level call asking for BZip2/Lzma/Zstd that is missing | Deflate is used and a note is added to Zip::ArchiveReport::warnings |
| read an entry that uses an unsupported method | the same exception from `open()` / `readAll()`; Zip::Entry::stat() and listing still work |
| read an encrypted entry without a password | `ZIP_ER_NOPASSWD`; this check comes **before** the backend check, so it can hide a missing AES backend |
| Zip::Archive::extractAll() reaches such an entry | stops there and keeps what was already extracted |
| Zip::extractArchive() with `continueOnError` | records the failure and goes on |

The difference between the two fallbacks is deliberate: using Deflate instead
of a missing compression method is harmless, while giving up encryption would
silently expose data.

## Portability advice

- If your archives must be readable everywhere, stay with Store and Deflate.
- On the consumer side, check `supportedCompressions(false)` (readable methods)
  before accepting an archive from elsewhere.
- Listing an archive always works, even when its entries use methods you cannot
  read, so a tool can report what it cannot extract.

## Linking against a static libzip

A static libzip needs its dependencies on the link line (`-lzip -lbz2 -llzma
-lzstd -lcrypto -lz`, minus what it was built without). With CMake or
`pkg-config --static` this is automatic; see @ref linking for the details and
for how to build libzip with or without each backend.

## How this was tested

The same test suites were run against three libzip builds: shared with all
backends, shared with zlib only, and static with all backends. On the zlib-only
build, tests that need an optional feature print `SKIP`, and the high-level tests
exercise the "refuse, never downgrade" path instead of AES. See @ref platforms.
