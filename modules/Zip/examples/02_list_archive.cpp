// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file 02_list_archive.cpp
 * @brief Print an `unzip -l` style listing of an archive.
 *
 * Usage: list_archive <archive.zip>
 *
 * Demonstrates:
 *  - Archive::open() read-only, range-for over entries, Entry::stat()
 *  - reading metadata: sizes, compression method, encryption, mtime
 */

#include <ZipWrapper/Zip.h>

#include <chrono>
#include <format>
#include <iostream>

namespace {

/** @brief Human readable name of a ZIP_CM_* method. */
std::string_view methodName(std::uint16_t method) {
    switch (method) {
        case ZIP_CM_STORE:   return "store";
        case ZIP_CM_DEFLATE: return "deflate";
        case ZIP_CM_BZIP2:   return "bzip2";
        case ZIP_CM_LZMA:    return "lzma";
        case ZIP_CM_ZSTD:    return "zstd";
        default:             return "other";
    }
}

/** @brief Space saved by compression, in percent (0 for empty entries). */
double savedPercent(const Zip::Stat& st) {
    if (st.size == 0) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(st.compressedSize) /
                              static_cast<double>(st.size));
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: list_archive <archive.zip>\n";
        return 2;
    }

    try {
        auto archive = Zip::Archive::open(argv[1], Zip::OpenFlag::ReadOnly);

        std::cout << std::format("{:>10} {:>10} {:>6} {:<8} {:<16} {}\n",
                                 "size", "packed", "saved", "method",
                                 "modified", "name");

        std::uint64_t totalSize = 0;
        std::uint64_t totalPacked = 0;
        std::size_t encrypted = 0;

        for (const auto& entry : archive) {
            const Zip::Stat st = entry.stat();
            totalSize += st.size;
            totalPacked += st.compressedSize;

            const bool isEncrypted = st.encryptionMethod != ZIP_EM_NONE;
            if (isEncrypted) ++encrypted;

            const std::chrono::sys_seconds mtime{
                std::chrono::seconds{st.modificationTime}};

            std::cout << std::format(
                "{:>10} {:>10} {:>5.0f}% {:<8} {:%Y-%m-%d %H:%M} {}{}\n",
                st.size, st.compressedSize, savedPercent(st),
                methodName(st.compressionMethod), mtime, st.name,
                isEncrypted ? " *" : "");
        }

        std::cout << std::format("{:>10} {:>10}  {} entries",
                                 totalSize, totalPacked, archive.size());
        if (encrypted) std::cout << std::format(", {} encrypted (*)", encrypted);
        std::cout << '\n';
        return 0;
    } catch (const Zip::Exception& e) {
        std::cerr << "zip error (" << e.zipCode() << "): " << e.what() << '\n';
        return 1;
    }
}
