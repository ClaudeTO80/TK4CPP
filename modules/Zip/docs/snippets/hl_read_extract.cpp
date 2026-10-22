// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: listing, reading, extracting and verifying.

#include <ZipWrapper/HighLevel.h>

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void write(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "zipwrapper_snippet_hl_read";
    fs::remove_all(dir);
    write(dir / "project/src/main.cpp", "int main() {}\n");
    write(dir / "project/docs/guide.md", "# guide\n");

    const bool aes = Zip::Archive::encryptionSupported(Zip::Encryption::AES256);
    const std::optional<std::string> password =
        aes ? std::optional<std::string>("correct horse") : std::nullopt;

    {
        Zip::ArchiveOptions o;
        o.password = password;
        Zip::archiveDirectory(dir / "project", dir / "backup.zip", o);
    }
    const fs::path archive = dir / "backup.zip";

    //! [list]
    const auto entries = Zip::listEntries(archive);          // never needs the password
    std::cout << Zip::formatListing(entries);
    if (Zip::summarize(entries).needsPassword())
        std::cout << "a password is required to read the encrypted entries\n";

    for (const auto& file : Zip::listFiles(archive, "*.cpp"))   // by pattern
        std::cout << file.name << '\n';
    //! [list]

    //! [read]
    // The same call works on plain and encrypted archives: the password is
    // used only for entries that are actually encrypted.
    const std::string text = Zip::readText(archive, "docs/guide.md", password);
    //! [read]

    //! [extract]
    Zip::UnpackOptions unpack;
    unpack.password = password;
    unpack.filter.include = {"docs/**"};     // only what is below docs/
    unpack.continueOnError = true;           // report every problem instead of stopping at the first
    const auto result = Zip::extractArchive(archive, dir / "restored", unpack);
    for (const auto& failure : result.failures)
        std::cerr << failure.name << ": " << failure.message << '\n';
    //! [extract]

    //! [verify]
    const auto check = Zip::verifyArchive(archive, password);
    std::cout << check.checked << " verified, " << check.skipped << " not verified, "
              << check.problems.size() << " problems\n";
    //! [verify]

    const bool ok = text == "# guide\n" && result.ok() && result.extracted == 1 &&
                    fs::exists(dir / "restored/docs/guide.md") && check.ok() && check.skipped == 0;
    fs::remove_all(dir);
    return ok ? 0 : 1;
}
