#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace TK4CPP::Utility {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// FolderEntry — nodo dell'albero di cartelle
// ---------------------------------------------------------------------------
struct FolderEntry {
    fs::path                path;
    std::string             name;
    std::uintmax_t          size{0};
    int                     depth{0};
    bool                    isSymlink{false};
    fs::file_time_type      lastModified{};
    std::vector<FolderEntry> children;  // popolato da tree()
};

// ---------------------------------------------------------------------------
// FolderFilter — criteri di selezione unificati
// ---------------------------------------------------------------------------
struct FolderFilter {
    std::string namePattern;                              // glob-style (see Glob.hpp): "*.log", "build_*", "img[0-9].png"
    int  minDepth{0};
    int  maxDepth{-1};                                    // -1 = illimitato
    bool excludeHidden{false};                            // ignora "." prefisso
    std::function<bool(fs::path const&)> predicate;       // filtro custom arbitrario

    // --- Added in 0.1.0 (all optional, every default keeps the previous behaviour) -----------------------
    bool caseSensitive{true};                             // applies to every name condition below (ASCII folding)
    std::string nameContains;                             // the name contains this text
    std::vector<std::string> namePatterns;                // the name matches at least one of these globs (AND with namePattern)
    std::vector<std::string> excludePatterns;             // the name matches none of these globs

    /// Kind of entries selected.
    enum class Kind { Any, Files, Directories };
    Kind kind{Kind::Any};

    std::optional<std::uintmax_t> minSize;                // bytes, inclusive; regular files only (directories never pass)
    std::optional<std::uintmax_t> maxSize;                // bytes, inclusive; regular files only
    std::optional<std::int64_t> modifiedAfter;            // Unix seconds, inclusive
    std::optional<std::int64_t> modifiedBefore;           // Unix seconds, inclusive
    std::optional<std::int64_t> createdAfter;             // Unix seconds, inclusive; entries with unknown creation time never pass
    std::optional<std::int64_t> createdBefore;            // Unix seconds, inclusive

    /// True if matching needs file metadata (one lstat, plus statx for creation times) beyond the name.
    [[nodiscard]] bool needsMetadata() const noexcept {
        return kind != Kind::Any || minSize || maxSize || modifiedAfter || modifiedBefore || createdAfter || createdBefore;
    }
    /// True if a creation-time bound is set.
    [[nodiscard]] bool usesCreation() const noexcept { return createdAfter || createdBefore; }
};

// ---------------------------------------------------------------------------
// CopyOptions — parametri per copy() / merge()
// ---------------------------------------------------------------------------
struct CopyOptions {
    bool overwrite{false};
    bool recursive{true};
    bool followSymlinks{false};
    bool preserveTimestamps{true};
    std::optional<FolderFilter> filter;
};

// ---------------------------------------------------------------------------
// FolderStats — risultato di stats()
// ---------------------------------------------------------------------------
struct FolderStats {
    std::uintmax_t     totalSize{0};
    std::size_t        fileCount{0};
    std::size_t        folderCount{0};
    int                maxDepth{0};
    std::size_t        emptyFolders{0};
    fs::file_time_type lastModified{};
};

// ---------------------------------------------------------------------------
// FolderDiff — risultato di diff()
// ---------------------------------------------------------------------------
struct FolderDiff {
    std::vector<fs::path> onlyInA;
    std::vector<fs::path> onlyInB;
    std::vector<fs::path> modified;   // stesso path relativo, diverso in A e B
};

} // namespace TK4CPP::Utility
