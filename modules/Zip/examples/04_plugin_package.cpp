// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file 04_plugin_package.cpp
 * @brief Build a plugin package from in-memory data and load it without
 *        extracting anything to disk.
 *
 * Usage: plugin_package [output.zip]      (default: hello-plugin.zip)
 *
 * Demonstrates:
 *  - Archive::add() with std::span<const std::byte> (text and binary)
 *  - Archive::contains(), Entry::readAll() to read a member into memory
 *  - selecting members by prefix while iterating
 */

#include <ZipWrapper/Zip.h>

#include <iostream>
#include <optional>

namespace {

/** @brief View a string as bytes, without copying. */
std::span<const std::byte> asBytes(std::string_view s) {
    return std::as_bytes(std::span{s.data(), s.size()});
}

/** @brief Read a member as text; nullopt if the member does not exist. */
std::optional<std::string> readText(Zip::Archive& archive,
                                    std::string_view name) {
    if (!archive.contains(name)) return std::nullopt;
    const auto bytes = archive.entry(name).readAll();
    return std::string(reinterpret_cast<const char*>(bytes.data()),
                       bytes.size());
}

/** @brief Create the package. Everything comes from memory. */
void pack(const std::filesystem::path& path) {
    constexpr std::string_view kManifest =
        R"({"name": "hello-plugin", "version": "1.2.0", "entry": "scripts/main.lua"})";
    constexpr std::string_view kScript = "print('hello from the plugin')\n";

    std::vector<std::byte> icon(256);  // stand-in for a real binary asset
    for (std::size_t i = 0; i < icon.size(); ++i)
        icon[i] = static_cast<std::byte>(i);

    auto archive = Zip::Archive::create(path);
    archive.add("manifest.json", asBytes(kManifest));
    archive.add("scripts/main.lua", asBytes(kScript));
    archive.add("assets/icon.bin", icon);
    archive.close();
}

/** @brief Load and validate the package. */
bool load(const std::filesystem::path& path) {
    auto archive = Zip::Archive::open(path, Zip::OpenFlag::ReadOnly);

    const auto manifest = readText(archive, "manifest.json");
    if (!manifest) {
        std::cerr << "invalid package: manifest.json is missing\n";
        return false;
    }
    std::cout << "manifest: " << *manifest << '\n';

    if (!archive.contains("scripts/main.lua")) {
        std::cerr << "invalid package: entry script is missing\n";
        return false;
    }
    std::cout << "script  : " << *readText(archive, "scripts/main.lua");

    for (const auto& entry : archive) {
        const std::string name = entry.name();
        if (name.starts_with("assets/"))
            std::cout << "asset   : " << name << " (" << entry.readAll().size()
                      << " bytes loaded)\n";
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path path = argc > 1 ? argv[1] : "hello-plugin.zip";
    try {
        pack(path);
        return load(path) ? 0 : 1;
    } catch (const Zip::Exception& e) {
        std::cerr << "zip error (" << e.zipCode() << "): " << e.what() << '\n';
        return 1;
    }
}
