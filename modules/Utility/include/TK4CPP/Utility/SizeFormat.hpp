/**
 * @file SizeFormat.hpp
 * @brief Human readable byte counts: formatting and parsing.
 * @since 0.1.0
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "TK4CPP/Utility/Export.h"

namespace TK4CPP::Utility
{
    /** @brief Unit system used by formatSize(). */
    enum class SizeUnits
    {
        Binary,  ///< Powers of 1024 with the labels B, KB, MB, GB, TB, PB (the historical FolderHandler::formatSize output).
        Decimal, ///< Powers of 1000 with the SI labels B, kB, MB, GB, TB, PB (what file managers and disk vendors show).
        Iec      ///< Powers of 1024 with the unambiguous labels B, KiB, MiB, GiB, TiB, PiB.
    };

    /**
     * @brief Format a byte count, e.g. "1.50 MB".
     * @param bytes     Value to format.
     * @param units     Unit system; see SizeUnits.
     * @param precision Digits after the decimal point for values of one kilo-unit or more (0..6);
     *                  plain bytes are always printed as an integer.
     * @return The formatted text (decimal point, never a locale-dependent separator).
     */
    [[nodiscard]] TK4CPP_UTILITY_EXPORT std::string formatSize(std::uintmax_t bytes,
                                                              SizeUnits units = SizeUnits::Binary,
                                                              int precision = 2);

    /**
     * @brief Parse a size such as "500", "10k", "1.5MB", "2GiB".
     * @details No suffix or "B" means bytes. <tt>k/kB, M/MB, G/GB, T/TB</tt> are powers of 1000 (the units file
     *          managers show); <tt>KiB, MiB, GiB, TiB</tt> are powers of 1024. Letters are case-insensitive.
     *          Decimals are rounded to the nearest byte.
     * @param text Text to parse; surrounding spaces are not accepted.
     * @return The byte count, or std::nullopt if @p text is malformed, negative or does not fit in 64 bits.
     */
    [[nodiscard]] TK4CPP_UTILITY_EXPORT std::optional<std::uintmax_t> parseSize(std::string_view text);
} // namespace TK4CPP::Utility
