// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file 07_verify_and_tail.cpp
 * @brief Two streaming tools that never load a whole entry into memory.
 *
 * Usage:
 *   verify_and_tail verify <archive.zip>
 *   verify_and_tail tail   <archive.zip> <entry> <bytes>
 *
 * `verify` streams every member and compares size and CRC-32 with the values
 * stored in the archive (like `zip -T`). Encrypted members are checked when
 * ZIP_PASSWORD is set in the environment; members that cannot be checked are
 * reported as SKIP and never counted as verified.
 *
 * `tail` prints the last N bytes of a member. It seeks when libzip reports
 * the stream as seekable (typically stored, unencrypted members) and falls
 * back to reading and discarding otherwise.
 *
 * Demonstrates: File::read(), File::seekable(), File::seek(), File::close().
 */

#include <ZipWrapper/Zip.h>

#include <array>
#include <cstdlib>
#include <optional>
#include <iostream>

namespace {

/** @brief Table-driven CRC-32 (IEEE 802.3), the checksum ZIP stores. */
class Crc32 {
public:
    void update(std::span<const std::byte> data) {
        for (const std::byte b : data)
            crc_ = kTable[(crc_ ^ std::to_integer<std::uint32_t>(b)) & 0xffu] ^
                   (crc_ >> 8);
    }
    [[nodiscard]] std::uint32_t value() const { return ~crc_; }

private:
    static constexpr std::array<std::uint32_t, 256> kTable = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc_ = 0xffffffffu;
};

/** @brief Outcome of checking one member. */
struct Verdict {
    enum class Kind { Ok, Skipped, Bad } kind = Kind::Ok;
    std::string message;
};

/** @brief True for WinZip AES methods (AE-1/AE-2). */
bool isAes(std::uint16_t method) {
    return method == ZIP_EM_AES_128 || method == ZIP_EM_AES_192 ||
           method == ZIP_EM_AES_256;
}

/**
 * @brief Verify one member by streaming it.
 *
 * WinZip AES entries (AE-2) store no CRC-32: their integrity is guaranteed
 * by an authentication code that libzip checks while reading, so a
 * tampered entry makes read()/close() throw. For those members only the
 * size is compared here.
 */
Verdict verifyEntry(const Zip::Entry& entry,
                    const std::optional<std::string>& password) {
    const Zip::Stat st = entry.stat();
    const bool encrypted = st.encryptionMethod != ZIP_EM_NONE;
    if (encrypted && !password)
        return {Verdict::Kind::Skipped, "encrypted, set ZIP_PASSWORD"};

    Zip::File file = encrypted ? entry.openEncrypted(*password) : entry.open();
    Crc32 crc;
    std::uint64_t total = 0;
    std::vector<std::byte> buffer(64 * 1024);
    for (;;) {
        const auto n = file.read(buffer);
        if (!n) break;
        crc.update(std::span<const std::byte>(buffer).first(n));
        total += n;
    }
    file.close();  // libzip also reports its own CRC / authentication failure

    if (total != st.size) return {Verdict::Kind::Bad, "size mismatch"};
    if (!isAes(st.encryptionMethod) && crc.value() != st.crc)
        return {Verdict::Kind::Bad, "CRC mismatch"};
    return {};
}

int verify(const char* archivePath) {
    auto archive = Zip::Archive::open(archivePath, Zip::OpenFlag::ReadOnly);
    const auto password = Zip::passwordFromEnv();
    std::size_t bad = 0;
    std::size_t skipped = 0;

    for (const auto& entry : archive) {
        if (entry.isDirectory()) continue;
        Verdict v;
        try {
            v = verifyEntry(entry, password);
        } catch (const Zip::Exception& e) {
            v = {Verdict::Kind::Bad, e.what()};
        }
        switch (v.kind) {
            case Verdict::Kind::Ok:
                std::cout << "  OK    " << entry.name() << '\n';
                break;
            case Verdict::Kind::Skipped:
                std::cout << "  SKIP  " << entry.name() << ": " << v.message << '\n';
                ++skipped;
                break;
            case Verdict::Kind::Bad:
                std::cout << "  BAD   " << entry.name() << ": " << v.message << '\n';
                ++bad;
                break;
        }
    }

    if (bad) {
        std::cout << "archive is CORRUPTED\n";
    } else if (skipped) {
        std::cout << "no corruption found, but " << skipped
                  << " entries were NOT verified\n";
    } else {
        std::cout << "archive is intact\n";
    }
    return bad ? 1 : 0;
}

int tail(const char* archivePath, const char* name, std::uint64_t count) {
    auto archive = Zip::Archive::open(archivePath, Zip::OpenFlag::ReadOnly);
    const auto entry = archive.entry(name);
    const std::uint64_t size = entry.size();
    const std::uint64_t skip = size > count ? size - count : 0;

    Zip::File file = entry.open();
    if (file.seekable()) {
        file.seek(static_cast<std::int64_t>(skip));
    } else {
        // Compressed streams cannot jump: read and discard the prefix.
        std::vector<std::byte> sink(64 * 1024);
        for (std::uint64_t left = skip; left > 0;) {
            const auto want = static_cast<std::size_t>(
                std::min<std::uint64_t>(left, sink.size()));
            const auto n = file.read(std::span(sink).first(want));
            if (!n) break;
            left -= n;
        }
    }

    std::array<std::byte, 4096> buffer{};
    while (const auto n = file.read(buffer))
        std::cout.write(reinterpret_cast<const char*>(buffer.data()),
                        static_cast<std::streamsize>(n));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "verify")
            return verify(argv[2]);
        if (argc == 5 && std::string_view(argv[1]) == "tail")
            return tail(argv[2], argv[3], std::strtoull(argv[4], nullptr, 10));
    } catch (const Zip::Exception& e) {
        std::cerr << "zip error (" << e.zipCode() << "): " << e.what() << '\n';
        return 1;
    }
    std::cerr << "usage:\n  verify_and_tail verify <archive.zip>\n"
                 "  verify_and_tail tail <archive.zip> <entry> <bytes>\n";
    return 2;
}
