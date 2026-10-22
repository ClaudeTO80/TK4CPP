// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: handling errors by code.

#include <ZipWrapper/Zip.h>

#include <iostream>

namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "zipwrapper_snippet_core_errors";
    fs::remove_all(dir);
    fs::create_directories(dir);

    int handled = 0;

    //! [errors]
    try {
        auto archive = Zip::Archive::open(dir / "missing.zip", Zip::OpenFlag::ReadOnly);
    } catch (const Zip::Exception& e) {
        switch (e.zipCode()) {
            case ZIP_ER_NOENT:
                std::cout << "no such archive\n";
                ++handled;
                break;
            case ZIP_ER_NOZIP:
                std::cout << "not a ZIP file\n";
                break;
            default:
                std::cout << "libzip error " << e.zipCode() << ": " << e.what() << '\n';
        }
    }
    //! [errors]

    //! [lazy-error]
    {
        auto archive = Zip::Archive::create(dir / "bad.zip");
        archive.addFile("data", dir / "no_such_file.bin");   // accepted: the file is opened later ...
        try {
            archive.close();                                 // ... so the error surfaces here
        } catch (const Zip::Exception& e) {
            std::cout << "close failed with code " << e.zipCode() << '\n';
            if (e.zipCode() == ZIP_ER_READ) ++handled;
        }                                                    // the archive is discarded on destruction
    }
    //! [lazy-error]

    const bool ok = handled == 2 && !fs::exists(dir / "bad.zip");
    fs::remove_all(dir);
    return ok ? 0 : 1;
}
