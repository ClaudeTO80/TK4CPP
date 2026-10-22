#pragma once

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
    std::string namePattern;                              // glob-style: "*.log", "build_*"
    int  minDepth{0};
    int  maxDepth{-1};                                    // -1 = illimitato
    bool excludeHidden{false};                            // ignora "." prefisso
    std::function<bool(fs::path const&)> predicate;       // filtro custom arbitrario
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
