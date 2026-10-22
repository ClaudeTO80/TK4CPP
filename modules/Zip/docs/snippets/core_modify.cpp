// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: modifying an archive in place and copying between archives.

#include <ZipWrapper/Zip.h>

#include <iostream>

namespace fs = std::filesystem;

static std::span<const std::byte> asBytes(std::string_view s) {
    return std::as_bytes(std::span{s.data(), s.size()});
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "zipwrapper_snippet_core_modify";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "demo.zip";
    const fs::path copyPath = dir / "clean.zip";

    {
        auto out = Zip::Archive::create(path);
        out.add("config.ini", asBytes("mode=fast\n"));
        out.add("old.txt", asBytes("old\n"));
        out.add("cache.tmp", asBytes("scratch"));
        out.add("logs/debug.tmp", asBytes("scratch"));
        out.close();
    }

    //! [update]
    auto archive = Zip::Archive::open(path);   // read/write
    archive.replace(archive.find("config.ini").value(), asBytes("mode=safe\n"));
    archive.rename(archive.find("old.txt").value(), "new.txt");

    std::vector<std::size_t> doomed;           // collect first ...
    for (const auto& entry : archive)
        if (entry.name().ends_with(".tmp")) doomed.push_back(entry.index());
    for (const auto index : doomed) archive.remove(index);   // ... delete afterwards

    archive.close();   // nothing above is written until here
    //! [update]

    //! [copy]
    auto src = Zip::Archive::open(path, Zip::OpenFlag::ReadOnly);
    auto dst = Zip::Archive::create(copyPath);
    for (const auto& entry : src)
        if (!entry.isDirectory()) dst.copyFrom(src, entry.index(), entry.name());
    dst.close();   // the data is streamed now, so src must still be open
    //! [copy]

    auto check = Zip::Archive::open(copyPath, Zip::OpenFlag::ReadOnly);
    const bool ok = check.size() == 2 && check.contains("config.ini") && check.contains("new.txt");
    std::cout << "entries after update and copy: " << check.size() << '\n';
    fs::remove_all(dir);
    return ok ? 0 : 1;
}
