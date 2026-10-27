// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: the basics of the core layer.
// Built and run by the test suite so the code shown in the manual always works.

#include <ZipWrapper/Zip.h>

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "zipwrapper_snippet_core_basic";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir / "readme.txt") << "hello\n"; }
    const std::vector<std::byte> bytes(1000, std::byte{42});

    //! [write]
    auto out = Zip::Archive::create(dir / "docs.zip");
    out.addFile("readme.txt", dir / "readme.txt");   // read from disk when the archive is closed
    out.add("blob.bin", bytes);                       // copied from memory right away
    out.mkdir("empty");                               // a directory entry
    out.close();                                      // commits; without it nothing is written
    //! [write]

    //! [read]
    auto in = Zip::Archive::open(dir / "docs.zip", Zip::OpenFlag::ReadOnly);
    for (const auto& entry : in)
        std::cout << entry.name() << ' ' << entry.size() << " bytes\n";

    const auto data = in.entry("blob.bin").readAll();   // the whole entry in memory
    //! [read]

    //! [extract]
    in.extractAll(dir / "out");   // refuses unsafe paths and existing files by default
    //! [extract]

    //! [progress]
    auto big = Zip::Archive::create(dir / "big.zip");
    for (int i = 0; i < 4; ++i) big.add("part" + std::to_string(i), bytes);
    big.setProgressCallback([](double fraction) {
        std::cerr << "\r  " << static_cast<int>(fraction * 100) << "%";
        return true;   // returning false cancels: close() then throws ZIP_ER_CANCELLED
    });
    big.close();       // the callback runs here, while libzip writes the archive
    //! [progress]

    const bool ok = data.size() == 1000 && fs::exists(dir / "out" / "readme.txt") &&
                    fs::exists(dir / "big.zip");
    // An open archive keeps its file open: Windows cannot delete it until it is closed.
    in.close();
    fs::remove_all(dir);
    return ok ? 0 : 1;
}
