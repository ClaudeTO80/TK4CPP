// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: archiving a directory with the high-level API.

#include <ZipWrapper/HighLevel.h>

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void write(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "zipwrapper_snippet_hl_archive";
    fs::remove_all(dir);
    const fs::path project = dir / "project";
    write(project / "src/main.cpp", "int main() {}\n");
    write(project / "docs/guide.md", "# guide\n");
    write(project / "scratch.tmp", "junk");
    write(project / ".git/HEAD", "ref");

    // The password normally comes from the environment; fixed here for the demo.
    const bool aes = Zip::Archive::encryptionSupported(Zip::Encryption::AES256);

    //! [archive]
    Zip::ArchiveOptions options;
    options.filter.exclude = {".git", "node_modules", "*.tmp"};
    if (aes) options.password = std::string("correct horse");   // or Zip::passwordFromEnv()
    options.overwriteOutput = true;      // default is false: never destroy a file silently
    options.verifyAfterWrite = true;     // written to <output>.partial, verified, then moved into place
    options.progress = [](double fraction) {
        std::cerr << "\r  " << static_cast<int>(fraction * 100) << "%";
        return true;                     // return false to cancel
    };

    const auto report = Zip::archiveDirectory(project, dir / "backup.zip", options);
    std::cout << report.files << " files, " << report.encrypted << " encrypted, "
              << report.inputBytes << " -> " << report.archiveBytes << " bytes\n";
    //! [archive]

    const auto files = Zip::listFiles(dir / "backup.zip");
    const bool ok = report.files == 2 && files.size() == 2 && report.encrypted == (aes ? 2u : 0u);
    fs::remove_all(dir);
    return ok ? 0 : 1;
}
