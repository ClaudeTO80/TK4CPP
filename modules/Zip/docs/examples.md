# Examples {#examples}

Eight complete programs in `examples/`. Each is a small, real tool; all of them
are built by the test suite and were run against real data. Click a name to see
the full annotated source with cross-references to the API.

| Program | Layer | What it shows |
|---|---|---|
| @ref 01_backup_directory.cpp | core | recursive backup with portable UTF-8 names, exclusions, `Store` for already-compressed files, never archiving the output itself |
| @ref 02_list_archive.cpp | core | an `unzip -l` style table from Zip::Entry::stat(): sizes, method, mtime, encryption |
| @ref 03_extract.cpp | core | per-entry extraction that keeps going after errors and turns `zipCode()` into actionable messages |
| @ref 04_plugin_package.cpp | core | building and loading a package entirely in memory: `add(span)`, `contains()`, `readAll()` |
| @ref 05_update_archive.cpp | core | in-place `replace` / `rename` / `remove` / `add`, and a filtered copy with `copyFrom()` |
| @ref 06_encrypted_archive.cpp | core | AES-256 with the password from the environment, feature detection, wrong versus missing password |
| @ref 07_verify_and_tail.cpp | core | streaming CRC verification, and reading the last N bytes with `seek()` |
| @ref 08_secure_backup.cpp | high-level | a complete backup tool using **only** the high-level API: filters, password, progress bar, verified write, per-entry report, changing the password in place |

## Trying them

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/libzip
cmake --build build

# a backup with a password, verified before it is put in place
ZIP_PASSWORD='long random passphrase' ./build/examples/secure_backup pack project backup.zip \
    --exclude .git --exclude node_modules --exclude '*.tmp'

./build/examples/secure_backup list   backup.zip       # no password needed
ZIP_PASSWORD='long random passphrase' ./build/examples/secure_backup verify backup.zip
ZIP_PASSWORD='long random passphrase' ./build/examples/secure_backup unpack backup.zip restored
```

Passwords are read from the environment, never from `argv` (see @ref security).

## Code shown in this manual

Every snippet in the guides (`docs/snippets/`) is a real program that the test
suite builds and runs, so the manual cannot show code that does not compile.
