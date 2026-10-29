/**
 * @file TimeParse.hpp
 * @brief Parsing of user supplied points in time ("2026-03-15", "7d").
 * @since 0.1.0
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

#include "TK4CPP/Utility/Export.h"

namespace TK4CPP::Utility
{
    /**
     * @brief Parse a point in time into Unix seconds.
     * @details Accepted forms:
     *          - a relative age <tt>\<N\>\<unit\></tt> with unit <tt>s, m, h, d, w</tt>, meaning "N units before @p now";
     *          - <tt>YYYY-MM-DD</tt>, <tt>YYYY-MM-DDTHH:MM</tt>, <tt>YYYY-MM-DDTHH:MM:SS</tt> (a space may replace the
     *            <tt>T</tt>), interpreted as local time.
     *          A date without a time is the start of that day, or its last second when @p endOfDay is set (use it for
     *          "before" bounds so that <tt>2026-03-31</tt> includes the whole day). Impossible dates such as
     *          2026-02-31 are rejected instead of being normalised.
     * @param text     Text to parse.
     * @param endOfDay See above.
     * @param now      Reference for relative forms.
     * @return Seconds since the Unix epoch, or std::nullopt if the text is not understood.
     */
    [[nodiscard]] TK4CPP_UTILITY_EXPORT std::optional<std::int64_t> parseTimestamp(
        std::string_view text, bool endOfDay = false,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
} // namespace TK4CPP::Utility
