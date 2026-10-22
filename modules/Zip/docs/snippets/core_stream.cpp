// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: streaming reads and seeking.

#include <ZipWrapper/Zip.h>

#include <iostream>

namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "zipwrapper_snippet_core_stream";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "data.zip";

    {
        std::vector<std::byte> big(200000);
        for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::byte>(i % 7);
        std::vector<std::byte> stored(1000);
        for (std::size_t i = 0; i < stored.size(); ++i) stored[i] = static_cast<std::byte>(i % 251);

        auto out = Zip::Archive::create(path);
        out.add("big.bin", big);
        out.setCompression(out.add("stored.bin", stored), Zip::Compression::Store);
        out.close();
    }

    //! [stream]
    auto archive = Zip::Archive::open(path, Zip::OpenFlag::ReadOnly);

    auto file = archive.entry("big.bin").open();
    std::vector<std::byte> buffer(64 * 1024);
    std::uint64_t total = 0;
    while (const auto n = file.read(buffer))   // 0 means end of entry
        total += n;
    file.close();   // also reports CRC / authentication failures
    //! [stream]

    //! [seek]
    auto stored = archive.openEntry("stored.bin");
    std::array<std::byte, 8> tail{};
    if (stored.seekable()) {            // stored, unencrypted entries are; compressed ones usually are not
        stored.seek(-8, SEEK_END);
        stored.read(tail);
    }
    //! [seek]

    const bool ok = total == 200000 && std::to_integer<int>(tail[7]) == 999 % 251;
    std::cout << "streamed " << total << " bytes\n";
    fs::remove_all(dir);
    return ok ? 0 : 1;
}
