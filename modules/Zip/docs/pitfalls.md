# Pitfalls {#pitfalls}

[TOC]

Behaviours that surprise people. Each one was reproduced against libzip 1.11.4
and is covered by a test or a snippet.

## 1. `close()` commits; the destructor discards {#pitfall_close}

An archive that goes out of scope without Zip::Archive::close() leaves **no
file at all**. This mirrors `zip_discard()` and is the most common mistake.

```cpp
{
    auto archive = Zip::Archive::create("out.zip");
    archive.add("x", bytes);
}                                   // no close(): out.zip was never written
```

If `close()` throws, the archive stays open and is discarded by the destructor;
an existing `out.zip` is left untouched. See @ref commit.

## 2. Moving an Archive invalidates its handles {#pitfall_move}

Zip::Entry stores the address of its Archive. After
`Zip::Archive b = std::move(a);` every Entry and iterator obtained from `a` is
invalid; re-obtain them from `b`. See @ref lifetime.

## 3. Collect first, delete afterwards {#pitfall_remove}

After Zip::Archive::remove() the entry is still counted by Zip::Archive::size(),
and iterating over it throws `ZIP_ER_DELETED`.

```cpp
std::vector<std::size_t> doomed;                  // gather the indices ...
for (const auto& e : archive)
    if (e.name().ends_with(".tmp")) doomed.push_back(e.index());
for (auto i : doomed) archive.remove(i);          // ... then delete
```

## 4. `copyFrom()` reads lazily {#pitfall_copy}

The data is streamed when the *destination* is closed, so the source archive
must stay open until then. If the source entry is encrypted, call
`source.setDefaultPassword()` first. See @ref core_copying.

## 5. File sources are opened lazily {#pitfall_lazy}

Zip::Archive::addFile() on a missing file **succeeds**; the error
(`ZIP_ER_READ`) is reported by `close()`. See @ref errors.

## 6. Names are UTF-8 with `/` separators {#pitfall_names}

Build entry names with `path.generic_u8string()`, not `path.string()`, which is
wrong on Windows (native separators and code page).

## 7. Do not pass passwords on the command line {#pitfall_argv}

`argv` is visible to other users in the process list. Use
Zip::passwordFromEnv() or a secret store. See @ref security.

## 8. AES (AE-2) entries carry no CRC {#pitfall_crc}

Zip::Stat::crc is 0 for them; libzip checks an authentication code while
reading, so a tampered entry makes `read()` or `close()` throw. Do not compare
the CRC yourself for those entries. Zip::verifyArchive() already handles this.

## 9. `ok()` is not "everything was checked" {#pitfall_skipped}

Zip::VerifyReport::ok() is `true` when no problem was found, even if encrypted
entries were skipped because no password was given. Also look at
`skipped`. See @ref hl_verifying.

## 10. With `include`, directories are not written {#pitfall_include}

A non-empty `include` list selects files only, so no directory entries are
added when archiving. Parent directories are implied by the file names and
re-created on extraction; an *empty* directory therefore disappears.
See @ref patterns.

## 11. `overwriteOutput` defaults to `false` {#pitfall_overwrite}

The high-level functions refuse to replace an existing file unless you ask,
with `ZIP_ER_EXISTS`. A scheduled backup that rewrites the same file must set
`overwriteOutput = true`.

## 12. `verifyAfterWrite` needs room next to the output {#pitfall_partial}

The archive is first written to `<output>.partial` and moved into place after it
passes verification. You need write permission in the output directory and
space for the file, and a leftover `.partial` from a crash is simply replaced
by the next run.

## 13. A missing AES backend can be hidden by a missing password {#pitfall_nopasswd}

Reading an encrypted entry without a password gives `ZIP_ER_NOPASSWD`, and that
check comes before the backend check. With the password supplied you would see
`ZIP_ER_ENCRNOTSUPP` on a libzip without a crypto backend. See @ref backends.

## 14. The iterator is single-pass {#pitfall_iterator}

Zip::Archive::Iterator dereferences to an Entry by value, so it is an input
iterator, not a forward iterator: use it with range-for, not with algorithms
that need to traverse twice.

## 15. Extracting untrusted archives {#pitfall_untrusted}

Path traversal and symlink escapes are blocked, but there is no limit on the
decompressed size and the validation cannot be atomic against concurrent changes
to the destination. See @ref security.
