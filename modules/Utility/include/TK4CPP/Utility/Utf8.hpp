/**
 * @file Utf8.hpp
 * @brief UTF-8 helpers for file names and paths (header-only).
 * @details File names on POSIX are arbitrary byte strings, but JSON, browsers and most APIs need well-formed
 *          UTF-8. These helpers validate and repair names, and convert between std::filesystem::path and UTF-8
 *          text the same way on every platform.
 * @since 0.1.0
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace TK4CPP::Utility
{
    /**
     * @brief Length in bytes of the well-formed UTF-8 sequence starting at @p s[i].
     * @param s Text to inspect.
     * @param i Index of the first byte of the sequence; must be less than <tt>s.size()</tt>.
     * @return 1..4 for a valid sequence (overlong forms, surrogates and code points above U+10FFFF are
     *         rejected), 0 if the bytes at @p i are not valid UTF-8.
     */
    [[nodiscard]] inline std::size_t utf8SequenceLength(std::string_view s, std::size_t i) noexcept
    {
        auto const byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
        unsigned char const lead = byte(i);
        if (lead < 0x80) return 1;
        auto const continuation = [&](std::size_t k) { return k < s.size() && (byte(k) & 0xC0) == 0x80; };
        if (lead >= 0xC2 && lead <= 0xDF) return continuation(i + 1) ? 2 : 0;
        if (lead >= 0xE0 && lead <= 0xEF) {
            if (!continuation(i + 1) || !continuation(i + 2)) return 0;
            unsigned char const second = byte(i + 1);
            if (lead == 0xE0 && second < 0xA0) return 0; // overlong
            if (lead == 0xED && second > 0x9F) return 0; // UTF-16 surrogates
            return 3;
        }
        if (lead >= 0xF0 && lead <= 0xF4) {
            if (!continuation(i + 1) || !continuation(i + 2) || !continuation(i + 3)) return 0;
            unsigned char const second = byte(i + 1);
            if (lead == 0xF0 && second < 0x90) return 0; // overlong
            if (lead == 0xF4 && second > 0x8F) return 0; // above U+10FFFF
            return 4;
        }
        return 0;
    }

    /** @brief True if every byte of @p s belongs to a well-formed UTF-8 sequence. */
    [[nodiscard]] inline bool isValidUtf8(std::string_view s) noexcept
    {
        for (std::size_t i = 0; i < s.size();) {
            std::size_t const n = utf8SequenceLength(s, i);
            if (n == 0) return false;
            i += n;
        }
        return true;
    }

    /**
     * @brief Return @p value with every ill-formed byte replaced by U+FFFD.
     * @param value Text to repair (taken by value: a valid string is returned without copying).
     */
    [[nodiscard]] inline std::string toValidUtf8(std::string value)
    {
        std::size_t i = 0;
        while (i < value.size()) {
            std::size_t const n = utf8SequenceLength(value, i);
            if (n == 0) break;
            i += n;
        }
        if (i == value.size()) return value;

        std::string repaired(value, 0, i);
        repaired.reserve(value.size() + 8);
        while (i < value.size()) {
            std::size_t const n = utf8SequenceLength(value, i);
            if (n == 0) {
                repaired += "\xEF\xBF\xBD";
                ++i;
            } else {
                repaired.append(value, i, n);
                i += n;
            }
        }
        return repaired;
    }

    /** @brief ASCII-only lower-casing of one byte (locale independent, safe on UTF-8 sequences). */
    [[nodiscard]] constexpr char asciiLower(char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    /** @brief Convert a filesystem path to UTF-8 text with generic ('/') separators. */
    [[nodiscard]] inline std::string pathToUtf8(std::filesystem::path const& path)
    {
        auto const encoded = path.generic_u8string();
        return std::string(reinterpret_cast<char const*>(encoded.data()), encoded.size());
    }

    /** @brief Interpret @p value as UTF-8 and build a path from it on every supported platform. */
    [[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view value)
    {
        std::u8string encoded;
        encoded.reserve(value.size());
        for (unsigned char const byte : value) encoded.push_back(static_cast<char8_t>(byte));
        return std::filesystem::path(encoded);
    }
} // namespace TK4CPP::Utility
