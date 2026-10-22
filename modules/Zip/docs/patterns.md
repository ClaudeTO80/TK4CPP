# Include and exclude patterns {#patterns}

[TOC]

Zip::PathFilter selects entries for archiving, listing and extraction. It is
used through `ArchiveOptions::filter`, `ListOptions::filter` and
`UnpackOptions::filter`.

Patterns are matched against **archive-style paths**: relative to the archived
or extracted root, `/` separated, on every platform. Matching is case-sensitive.

## The rules

| Syntax | Meaning |
|---|---|
| `*` | any run of characters **inside one** path component |
| `?` | exactly one character inside a component |
| `**` | any number of whole directories, **including none** |
| no `/` in the pattern | matches a single component (see below) |
| a `/` in the pattern | matches the whole relative path, anchored at the root |
| leading `./` or `/`, trailing `/` | ignored |

### `exclude`: drop a path and everything below it

A pattern without `/` matches **any component** of the path, in the manner of
`.gitignore`, so `node_modules` removes that directory at any depth together with
its whole subtree. A pattern with `/` is matched against the whole relative
path and also against every directory prefix, so `build/cache` removes
everything below that one directory.

@snippet hl_patterns.cpp exclude

### `include`: select files

When `include` is non-empty only **files** matching one of the patterns are
selected. A pattern without `/` matches the **file name**, at any depth; a
pattern with `/` is matched against the whole relative path.

@snippet hl_patterns.cpp include

Two consequences are worth remembering:

- **Directories never match a non-empty `include` list**, so no directory
  entries are written when archiving with `include`. The parent directories are
  implied by the file names and are re-created on extraction.
- An empty `include` list selects everything.

### Combining them

@snippet hl_patterns.cpp combine

`exclude` always wins: an entry must not be excluded *and* must pass the
include list. Zip::PathFilter::accepts() applies both.

## Practical notes

- To skip a directory but keep its name out of the output entirely, use
  `exclude`; the walk does not descend into it.
- A pattern consisting of `**` alone matches every file.
- The matcher is plain component-wise globbing: there are no character classes
  (`[a-z]`) and no negation. Express "everything except" with `exclude`.
- Patterns with several `**` are fine for ordinary trees, but they are matched
  by backtracking; do not generate them from untrusted input.

Every example above is a real, checked program (`docs/snippets/hl_patterns.cpp`)
that runs as part of the test suite.
