// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file 05_update_archive.cpp
 * @brief Modify an archive in place, then write a filtered copy of it.
 *
 * Usage: update_archive [workdir]       (default: current directory)
 *
 * The program is self-contained: it first builds a sample archive
 * (demo.zip), updates it, and finally writes demo_clean.zip.
 *
 * Demonstrates:
 *  - Archive::open() read/write, replace(), rename(), remove(), add()
 *  - the "collect first, delete after" rule
 *  - Archive::copyFrom() to build a filtered copy without recompressing
 */

#include <ZipWrapper/Zip.h>

#include <iostream>

namespace fs = std::filesystem;

namespace {

std::span<const std::byte> asBytes(std::string_view s) {
    return std::as_bytes(std::span{s.data(), s.size()});
}

bool isTemporary(std::string_view name) { return name.ends_with(".tmp"); }

void printNames(const char* title, const fs::path& path) {
    auto archive = Zip::Archive::open(path, Zip::OpenFlag::ReadOnly);
    std::cout << title << ":\n";
    for (const auto& entry : archive) std::cout << "  " << entry.name() << '\n';
}

/** @brief Build the sample archive we are going to modify. */
void makeSample(const fs::path& path) {
    auto archive = Zip::Archive::create(path);
    archive.add("config.ini", asBytes("mode=fast\n"));
    archive.add("notes.txt", asBytes("keep me\n"));
    archive.add("cache.tmp", asBytes("scratch"));
    archive.add("old_name.txt", asBytes("renamed content\n"));
    archive.add("logs/debug.tmp", asBytes("scratch"));
    archive.close();
}

/** @brief Update the archive in place. Changes are committed by close(). */
void updateInPlace(const fs::path& path) {
    auto archive = Zip::Archive::open(path);  // read/write

    // 1. Replace the content of an existing member.
    archive.replace(archive.find("config.ini").value(),
                    asBytes("mode=safe\nretries=3\n"));

    // 2. Rename a member.
    archive.rename(archive.find("old_name.txt").value(), "new_name.txt");

    // 3. Remove all temporary files.
    //    Collect first: after remove(), size() still counts the deleted
    //    entry and iterating over it throws ZIP_ER_DELETED.
    std::vector<std::size_t> doomed;
    for (const auto& entry : archive)
        if (isTemporary(entry.name())) doomed.push_back(entry.index());
    for (const auto index : doomed) archive.remove(index);

    // 4. Add a member only if it is not there yet.
    if (!archive.contains("CHANGELOG.txt"))
        archive.add("CHANGELOG.txt", asBytes("1.1: switched to safe mode\n"));

    archive.close();
}

/**
 * @brief Write a copy of @p source without temporary files.
 *
 * copyFrom() streams the data when the destination is closed, so the source
 * archive must stay open until then.
 */
void purgedCopy(const fs::path& source, const fs::path& destination) {
    auto src = Zip::Archive::open(source, Zip::OpenFlag::ReadOnly);
    auto dst = Zip::Archive::create(destination);

    for (const auto& entry : src) {
        const std::string name = entry.name();
        if (isTemporary(name)) continue;
        if (entry.isDirectory())
            dst.mkdir(name);
        else
            dst.copyFrom(src, entry.index(), name);
    }
    dst.close();  // reads from src: src is still alive here
}

} // namespace

int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? argv[1] : ".";
    const fs::path demo = dir / "demo.zip";
    const fs::path clean = dir / "demo_clean.zip";

    try {
        makeSample(demo);
        printNames("before", demo);

        updateInPlace(demo);
        printNames("after update", demo);

        {
            auto archive = Zip::Archive::open(demo, Zip::OpenFlag::ReadOnly);
            const auto config = archive.entry("config.ini").readAll();
            std::cout << "config.ini now:\n"
                      << std::string(
                             reinterpret_cast<const char*>(config.data()),
                             config.size());
        }  // closed before the file is rewritten below

        // Add an extra temp file back so the filtered copy has work to do.
        {
            auto rw = Zip::Archive::open(demo);
            rw.add("late.tmp", asBytes("scratch"));
            rw.close();
        }
        purgedCopy(demo, clean);
        printNames("filtered copy", clean);
        return 0;
    } catch (const Zip::Exception& e) {
        std::cerr << "zip error (" << e.zipCode() << "): " << e.what() << '\n';
        return 1;
    }
}
