// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: compression, encryption and feature detection.

#include <ZipWrapper/Zip.h>

#include <iostream>

namespace fs = std::filesystem;

static std::span<const std::byte> asBytes(std::string_view s) {
    return std::as_bytes(std::span{s.data(), s.size()});
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "zipwrapper_snippet_core_protect";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "secure.zip";

    //! [detect]
    std::cout << "libzip " << Zip::Archive::libzipVersion() << '\n';
    for (const auto method : Zip::Archive::supportedCompressions())
        std::cout << "can write compression method " << static_cast<int>(method) << '\n';
    const bool aes = Zip::Archive::encryptionSupported(Zip::Encryption::AES256);
    //! [detect]

    if (!aes) {
        std::cout << "this libzip build has no AES support: skipping the encryption part\n";
        fs::remove_all(dir);
        return 0;
    }

    //! [protect]
    {
        auto archive = Zip::Archive::create(path);
        const auto index = archive.add("secret.txt", asBytes("the launch code is 0000\n"));
        archive.setCompression(index, Zip::Compression::Deflate, 9);
        archive.setEncryption(index, Zip::Encryption::AES256, "correct horse");
        archive.close();
    }

    auto archive = Zip::Archive::open(path, Zip::OpenFlag::ReadOnly);
    const auto text = archive.entry("secret.txt").openEncrypted("correct horse").readAll();
    //! [protect]

    //! [default-password]
    archive.setDefaultPassword("correct horse");               // from now on ...
    const auto again = archive.entry("secret.txt").readAll();  // ... plain calls just work
    //! [default-password]

    const bool ok = text == again && !text.empty();
    fs::remove_all(dir);
    return ok ? 0 : 1;
}
