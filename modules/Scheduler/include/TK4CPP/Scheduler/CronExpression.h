/**
 * @file CronExpression.h
 * @brief UTC cron expression parser and next-occurrence calculator.
 */
#pragma once

#include <TK4CPP/Scheduler/Export.h>

#include <TK4CPP/Scheduler/Error.h>
#include <TK4CPP/Scheduler/Types.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace TK4CPP::Scheduler {

/**
 * @brief Parsed cron expression, evaluated in UTC.
 *
 * Syntax: `[sec] min hour day-of-month month day-of-week` (5 or 6 whitespace separated fields).
 * Each field accepts `*`, a value, a range `a-b`, a list `a,b,c`, and steps `*&#47;n`, `a-b/n`, `a/n`.
 * Month and weekday names (JAN..DEC, SUN..SAT, case-insensitive) are accepted; weekday 0 and 7
 * are both Sunday. As in Vixie cron, when both day-of-month and day-of-week are restricted
 * (neither starts with `*`) a day matches if *either* matches.
 *
 * Not supported (Quartz-only): `?`, `L`, `W`, `#`, year field.
 */
class TK4CPP_SCHEDULER_EXPORT CronExpression {
public:
    /// @brief Parses an expression; returns ErrorCode::InvalidCron on failure.
    [[nodiscard]] static Result<CronExpression> parse(std::string_view expression);

    /**
     * @brief First matching second strictly after @p after.
     * @return The instant, or empty if none exists within about nine years
     *         (e.g. `0 0 31 2 *`).
     */
    [[nodiscard]] std::optional<TimePoint> next(TimePoint after) const;

    /// @brief True if the second containing @p tp matches the expression.
    [[nodiscard]] bool matches(TimePoint tp) const;

    /// @brief The original expression text.
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    CronExpression() = default;

    [[nodiscard]] bool dayMatches(std::chrono::year_month_day ymd, std::chrono::sys_days day) const;

    std::string text_;
    std::uint64_t seconds_{0};
    std::uint64_t minutes_{0};
    std::uint32_t hours_{0};
    std::uint32_t daysOfMonth_{0};
    std::uint16_t months_{0};
    std::uint8_t daysOfWeek_{0};
    bool domStar_{true};
    bool dowStar_{true};
};

} // namespace TK4CPP::Scheduler
