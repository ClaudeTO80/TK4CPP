// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file 01_backup_directory.cpp
 * @brief Zip a directory tree, skipping junk and not re-compressing media.
 *
 * Usage: backup_directory <source_dir> <output.zip>
 *
 * Demonstrates:
 *  - Archive::create(), addFile(), mkdir(), setCompression(), close()
 *  - building portable archive names ('/' separated, UTF-8, relative)
 *  - excluding the output archive when it lives inside the source tree
 */

#include <ZipWrapper/Zip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <iostream>

namespace fs = std::filesystem;

namespace {

/**
 * @brief Build the archive name for a path relative to the backup root.
 *
 * ZIP names must be relative, '/' separated and UTF-8 on every platform.
 * generic_u8string() guarantees all three; string() would not on Windows.
 */
std::string archiveName(const fs::path& relative) {
    const auto u8 = relative.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

/** @brief Entries that are never worth backing up. */
bool isExcluded(const fs::path& p) {
    const std::string name = p.filename().string();
    return name == ".git" || name == "node_modules" || name.ends_with(".tmp");
}

/** @brief Formats that are already compressed; deflating them wastes CPU. */
bool isAlreadyCompressed(const fs::path& p) {
    static constexpr std::array<std::string_view, 6> kExt{
        ".zip", ".jpg", ".jpeg", ".png", ".mp4", ".gz"};
    std::string ext = p.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return std::ranges::find(kExt, ext) != kExt.end();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: backup_directory <source_dir> <output.zip>\n";
        return 2;
    }
    const fs::path root = argv[1];
    const fs::path out = argv[2];

    try {
        // Resolved once, so the output file is never added to itself when it
        // is created inside the directory being backed up.
        const fs::path outResolved = fs::weakly_canonical(out);

        auto archive = Zip::Archive::create(out);
        std::size_t files = 0;
        std::uint64_t inputBytes = 0;

        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator(); ++it) {
            const fs::path& path = it->path();

            const bool isOutput = path.filename() == out.filename() &&
                                  fs::weakly_canonical(path) == outResolved;
            if (isExcluded(path) || isOutput || it->is_symlink()) {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }

            const std::string name = archiveName(fs::relative(path, root));

            if (it->is_directory()) {
                archive.mkdir(name);
            } else if (it->is_regular_file()) {
                const auto index = archive.addFile(name, path);
                archive.setCompression(
                    index,
                    isAlreadyCompressed(path) ? Zip::Compression::Store
                                              : Zip::Compression::Deflate,
                    6);
                ++files;
                inputBytes += it->file_size();
            }
        }

        // Nothing is written until close(); an exception before this point
        // (or forgetting it) discards the archive.
        archive.close();

        const auto packed = fs::file_size(out);
        std::cout << std::format("{} files, {} bytes -> {} bytes ({:.1f}%)\n",
                                 files, inputBytes, packed,
                                 inputBytes ? 100.0 * static_cast<double>(packed) /
                                                  static_cast<double>(inputBytes)
                                            : 0.0);
        return 0;
    } catch (const Zip::Exception& e) {
        std::cerr << "zip error (" << e.zipCode() << "): " << e.what() << '\n';
    } catch (const fs::filesystem_error& e) {
        std::cerr << "filesystem error: " << e.what() << '\n';
    }
    return 1;
}
