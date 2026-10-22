# Security notes {#security}

[TOC]

Read this page before relying on password protection or on extracting archives
you did not create. It states what the library does, what it deliberately does
not do, and what the ZIP format itself cannot give you.

> This library has not been security-audited. It is tested, including against
> two independent AES implementations (see @ref platforms), but tests are not a
> substitute for an audit.

## What the protection covers

Encryption protects the **contents** of each protected entry, and nothing else:

| Visible without the password | Protected |
|---|---|
| entry names and directory layout | the entry's data |
| sizes (a few bytes larger when encrypted) | |
| modification times | |
| number of entries | |
| compression and encryption method, and which entries are encrypted | |

Zip::listEntries() works without a password for exactly this reason. If the
**names or the structure are themselves sensitive**, ZIP encryption is not
enough: put everything inside one inner archive (so the outer one has a single,
opaque entry), or use a container format that encrypts metadata.

The archive structure is also **not authenticated**. WinZip AES authenticates
each entry's data, so tampering with an entry is detected when it is read; it
does not stop someone from rearranging, removing or renaming entries.

## What the library enforces

- **AES only.** Zip::Encryption::AES128, `AES192` and `AES256` (WinZip AES,
  AE-2). Traditional PKWARE encryption ("ZipCrypto") is known to be weak and the
  @ref zip_highlevel "high-level API" does not offer it.
- **No silent downgrade.** If the requested AES method is not available in your
  libzip, the call fails with `ZIP_ER_ENCRNOTSUPP` **before anything is
  written**. It never falls back to a weaker method or to no encryption.
- **Up-front validation.** An empty password or a non-AES method is rejected
  with `ZIP_ER_INVAL` before any file is touched.
- **No destruction by default.** `overwriteOutput` is `false`, and a failed or
  cancelled operation leaves an existing output untouched.

## Passwords

- **The key derivation is modest.** The format derives the key with
  PBKDF2-HMAC-SHA1 and a fixed, small iteration count, which does little to slow
  down offline guessing. Use a long random passphrase; the strength of the
  password matters more than choosing AES-256 over AES-128.
- **Do not put passwords on the command line.** `argv` is visible to other users
  in the process list. Read them from a secret store, or from the environment
  with Zip::passwordFromEnv(). Note that environment variables are visible to
  processes of the same user and are inherited by child processes.
- **Passwords are not wiped.** They live in ordinary heap memory (yours, this
  library's and libzip's, which keeps a copy until the archive is closed) and
  are not zeroed after use. If that is a concern for your threat model, this is
  not the right tool.
- **Non-ASCII passwords** are passed as UTF-8 bytes. Zip::passwordFromEnv()
  returns UTF-8 on every platform. Interoperability with `pyzipper` and 7-Zip was
  checked with a non-ASCII password on Linux; the Windows path is not run-tested
  (see @ref platforms).

## Extracting untrusted archives

Zip::Entry::extract() and everything built on it refuse:

- absolute names, including a leading root;
- any `..` component;
- any path that resolves, through an **existing symlink in the destination**, to
  a location outside it.

The checks run before anything is created. Two limits remain:

- **Decompression bombs.** There is no limit on the decompressed size. Before
  extracting something you do not trust, look at Zip::EntryInfo::size from
  Zip::listEntries() and refuse totals that are unreasonable for your use.
- **Not atomic against concurrent changes.** The validation cannot be made
  atomic against another process modifying the destination while extraction
  runs (time-of-check/time-of-use). Extract into a directory only you can write to.

Zip::extractArchive() has no option to relax these protections.

## Integrity

Zip::verifyArchive() checks size and CRC-32 of every entry. For WinZip AES
(AE-2) entries there is no CRC to compare (Zip::Stat::crc is 0); libzip checks
the authentication code while reading, which detects tampering with the entry's
data. Encrypted entries verified without a password are reported as `skipped`,
never as verified, so `ok()` alone does not mean everything was covered.

## Checklist

1. Use `AES256` (the default) and a long random passphrase.
2. Read the password from the environment or a secret store, not from `argv`.
3. Remember that names, sizes and dates are visible.
4. Handle `ZIP_ER_ENCRNOTSUPP` as "this machine cannot protect the data", not as
   "try again without encryption".
5. Extract untrusted archives into a private directory and check the declared sizes first.
6. After writing a backup, use `verifyAfterWrite`, or run Zip::verifyArchive() with the password.
