// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file 03_extract.cpp
 * @brief Extract an archive entry by entry, reporting each failure clearly.
 *
 * Usage: extract <archive.zip> <dest_dir> [--overwrite] [--password PW]
 *
 * Unlike Archive::extractAll(), which stops at the first error, this loops
 * over the entries so that one bad member does not hide the others. The exit
 * code is non-zero if anything failed.
 *
 * Demonstrates:
 *  - ExtractOptions (overwrite, password)
 *  - Entry::extract() and the built-in path-traversal / symlink protection
 *  - mapping Zip::Exception::zipCode() to actionable messages
 */

#include <ZipWrapper/Zip.h>

#include <iostream>

namespace {

/** @brief Turn a libzip error code into advice the user can act on. */
std::string_view hint(int zipCode) {
    switch (zipCode) {
        case ZIP_ER_NOPASSWD:    return "entry is encrypted: pass --password";
        case ZIP_ER_WRONGPASSWD: return "wrong password";
        case ZIP_ER_EXISTS:      return "file exists: pass --overwrite";
        case ZIP_ER_INVAL:       return "refused: unsafe path or invalid entry";
        case ZIP_ER_CRC:         return "data is corrupted (CRC mismatch)";
        case ZIP_ER_COMPNOTSUPP: return "compression method not built into this libzip";
        case ZIP_ER_ENCRNOTSUPP: return "encryption method not built into this libzip";
        default:                 return "";
    }
}

struct Args {
    std::string archive;
    std::string destination;
    Zip::ExtractOptions options;
};

/** @brief Minimal argument parser; returns false on bad usage. */
bool parseArgs(int argc, char** argv, Args& args) {
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--overwrite") {
            args.options.overwrite = true;
        } else if (a == "--password" && i + 1 < argc) {
            args.options.password = argv[++i];
        } else if (a.starts_with("--")) {
            return false;
        } else {
            positional.push_back(a);
        }
    }
    if (positional.size() != 2) return false;
    args.archive = positional[0];
    args.destination = positional[1];
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        std::cerr << "usage: extract <archive.zip> <dest_dir> "
                     "[--overwrite] [--password PW]\n";
        return 2;
    }

    try {
        auto archive = Zip::Archive::open(args.archive, Zip::OpenFlag::ReadOnly);
        std::size_t ok = 0;
        std::size_t failed = 0;

        for (std::size_t i = 0; i < archive.size(); ++i) {
            const auto entry = archive.entry(i);
            const std::string name = entry.name();
            try {
                entry.extract(args.destination, args.options);
                std::cout << "  ok    " << name << '\n';
                ++ok;
            } catch (const Zip::Exception& e) {
                std::cout << "  FAIL  " << name << ": " << e.what();
                if (const auto h = hint(e.zipCode()); !h.empty())
                    std::cout << "  [" << h << ']';
                std::cout << '\n';
                ++failed;
            } catch (const std::filesystem::filesystem_error& e) {
                std::cout << "  FAIL  " << name << ": " << e.what() << '\n';
                ++failed;
            }
        }

        std::cout << ok << " extracted, " << failed << " failed\n";
        return failed == 0 ? 0 : 1;
    } catch (const Zip::Exception& e) {
        std::cerr << "cannot open archive (" << e.zipCode() << "): " << e.what()
                  << '\n';
        return 1;
    }
}
