// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file 06_encrypted_archive.cpp
 * @brief Create an AES-256 encrypted archive and show how each failure mode
 *        surfaces when reading it back.
 *
 * Usage: ZIP_PASSWORD=... encrypted_archive [output.zip]
 *
 * The password comes from the environment, never from source code or
 * argv (argv is visible to other users in the process list).
 *
 * Demonstrates:
 *  - Zip::passwordFromEnv() and Archive::encryptionSupported() feature detection
 *  - Archive::setEncryption(), Entry::openEncrypted()
 *  - ZIP_ER_NOPASSWD vs ZIP_ER_WRONGPASSWD
 */

#include <ZipWrapper/Zip.h>

#include <iostream>

namespace {

std::span<const std::byte> asBytes(std::string_view s) {
    return std::as_bytes(std::span{s.data(), s.size()});
}

/** @brief Run @p action and describe the Zip::Exception it throws, if any. */
template <typename F>
void attempt(std::string_view what, F&& action) {
    try {
        action();
        std::cout << "  " << what << ": ok\n";
    } catch (const Zip::Exception& e) {
        const char* kind = e.zipCode() == ZIP_ER_NOPASSWD    ? "no password given"
                         : e.zipCode() == ZIP_ER_WRONGPASSWD ? "wrong password"
                                                             : "other error";
        std::cout << "  " << what << ": FAILED (" << kind << ") - " << e.what()
                  << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto passwordOpt = Zip::passwordFromEnv();
    if (!passwordOpt) {
        std::cerr << "set ZIP_PASSWORD in the environment\n";
        return 2;
    }
    const std::string& password = *passwordOpt;
    const std::filesystem::path path = argc > 1 ? argv[1] : "secure.zip";

    // Not every libzip build has a crypto backend (OpenSSL, GnuTLS, ...).
    if (!Zip::Archive::encryptionSupported(Zip::Encryption::AES256)) {
        std::cerr << "this libzip build has no AES-256 support\n";
        return 3;
    }

    try {
        {
            auto archive = Zip::Archive::create(path);
            // Encrypt only the sensitive member; README stays readable.
            const auto secret =
                archive.add("secret.txt", asBytes("the launch code is 0000\n"));
            archive.setEncryption(secret, Zip::Encryption::AES256, password);
            archive.add("README.txt", asBytes("public information\n"));
            archive.close();
        }

        auto archive = Zip::Archive::open(path, Zip::OpenFlag::ReadOnly);
        std::cout << "reading " << path.string() << ":\n";

        attempt("README.txt, no password",
                [&] { (void)archive.entry("README.txt").readAll(); });
        attempt("secret.txt, no password",
                [&] { (void)archive.entry("secret.txt").readAll(); });
        attempt("secret.txt, wrong password", [&] {
            (void)archive.entry("secret.txt")
                .openEncrypted(std::string(password) + "x");
        });
        std::string content;
        attempt("secret.txt, right password", [&] {
            auto file = archive.entry("secret.txt").openEncrypted(password);
            const auto bytes = file.readAll();
            file.close();  // reports authentication / CRC errors
            content.assign(reinterpret_cast<const char*>(bytes.data()),
                           bytes.size());
        });
        std::cout << "    content: " << content;
        return 0;
    } catch (const Zip::Exception& e) {
        std::cerr << "zip error (" << e.zipCode() << "): " << e.what() << '\n';
        return 1;
    }
}
