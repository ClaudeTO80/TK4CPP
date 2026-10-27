#include "TestSupport.h"

#include <TK4CPP/Scheduler/CronExpression.h>

#include <random>

using namespace T;

namespace {

std::optional<TimePoint> nextOf(const char* expr, TimePoint after) {
    const auto e = CronExpression::parse(expr);
    if (!e) {
        ADD_FAILURE() << "parse failed for '" << expr << "': " << e.error().message;
        return std::nullopt;
    }
    return e->next(after);
}

} // namespace

TEST(Cron, cron_next_basic) {
    const auto t = utc(2026, 10, 26, 0, 0, 0);
    CHECK(nextOf("* * * * *", t) == utc(2026, 10, 26, 0, 1, 0));
    CHECK(nextOf("*/15 * * * *", t) == utc(2026, 10, 26, 0, 15, 0));
    CHECK(nextOf("*/15 * * * *", utc(2026, 10, 26, 0, 50, 0)) == utc(2026, 10, 26, 1, 0, 0));
    CHECK(nextOf("0 0 * * *", t) == utc(2026, 10, 27, 0, 0, 0)); // strictly after
    CHECK(nextOf("59 23 31 12 *", t) == utc(2026, 12, 31, 23, 59, 0));
    CHECK(nextOf("0 0 1 JAN *", t) == utc(2027, 1, 1, 0, 0, 0));
    CHECK(nextOf("5,35 * * * *", utc(2026, 10, 26, 0, 6, 0)) == utc(2026, 10, 26, 0, 35, 0));
    CHECK(nextOf("10-20/5 * * * *", utc(2026, 10, 26, 0, 0, 0)) == utc(2026, 10, 26, 0, 10, 0));
    CHECK(nextOf("10-20/5 * * * *", utc(2026, 10, 26, 0, 10, 0)) == utc(2026, 10, 26, 0, 15, 0));
    CHECK(nextOf("10-20/5 * * * *", utc(2026, 10, 26, 0, 20, 0)) == utc(2026, 10, 26, 1, 10, 0));
    CHECK(nextOf("5/20 * * * *", utc(2026, 10, 26, 0, 6, 0)) == utc(2026, 10, 26, 0, 25, 0));
}

TEST(Cron, cron_seconds_field) {
    const auto t = utc(2026, 10, 26, 0, 0, 0);
    CHECK(nextOf("*/10 * * * * *", t) == utc(2026, 10, 26, 0, 0, 10));
    CHECK(nextOf("*/10 * * * * *", utc(2026, 10, 26, 0, 0, 55)) == utc(2026, 10, 26, 0, 1, 0));
    CHECK(nextOf("30 15 10 * * *", t) == utc(2026, 10, 26, 10, 15, 30));
}

TEST(Cron, cron_weekdays) {
    // 2026-10-23 is a Friday, 2026-10-25 a Sunday.
    CHECK(nextOf("30 8 * * MON-FRI", utc(2026, 10, 23, 9, 0, 0)) == utc(2026, 10, 26, 8, 30, 0));
    CHECK(nextOf("0 0 * * 0", utc(2026, 10, 26, 0, 0, 0)) == utc(2026, 11, 1, 0, 0, 0));
    CHECK(nextOf("0 0 * * 7", utc(2026, 10, 26, 0, 0, 0)) == utc(2026, 11, 1, 0, 0, 0));
    CHECK(nextOf("0 0 * * sun", utc(2026, 10, 26, 0, 0, 0)) == utc(2026, 11, 1, 0, 0, 0));
}

TEST(Cron, cron_day_of_month_or_weekday_is_vixie_or) {
    // Both restricted: a day matches if the 13th OR a Friday. 2026-10-02 is the first Friday.
    CHECK(nextOf("0 0 13 * FRI", utc(2026, 10, 1, 0, 0, 0)) == utc(2026, 10, 2, 0, 0, 0));
    // After the last Friday of the month the 13th of the next month is still earlier than the next Friday.
    CHECK(nextOf("0 0 13 * FRI", utc(2026, 10, 31, 0, 0, 0)) == utc(2026, 11, 6, 0, 0, 0)); // Fri 6 Nov
    // A star in either field makes it AND.
    CHECK(nextOf("0 0 13 * *", utc(2026, 10, 1, 0, 0, 0)) == utc(2026, 10, 13, 0, 0, 0));
}

TEST(Cron, cron_leap_years_and_impossible_dates) {
    CHECK(nextOf("0 0 29 2 *", utc(2026, 1, 1)) == utc(2028, 2, 29));
    // 2100 is not a leap year: from March 2096 the next 29 Feb is 2104.
    CHECK(nextOf("0 0 29 2 *", utc(2096, 3, 1)) == utc(2104, 2, 29));
    CHECK(nextOf("0 0 31 2 *", utc(2026, 1, 1)) == std::nullopt);
    CHECK(nextOf("0 0 31 4 *", utc(2026, 1, 1)) == std::nullopt);
}

TEST(Cron, cron_utc_has_no_dst_gap) {
    // 2026-10-25 is the DST change in Europe/Rome; in UTC every hour exists exactly once.
    auto t = utc(2026, 10, 25, 0, 0, 0);
    int hits = 0;
    for (int i = 0; i < 24; ++i) {
        const auto n = nextOf("0 * * * *", t);
        CHECK(n.has_value());
        if (n) {
            ++hits;
            CHECK(*n - t == 1h);
            t = *n;
        }
    }
    CHECK(hits == 24);
}

TEST(Cron, cron_parse_errors) {
    for (const char* bad : {"", "* * * *", "* * * * * * *", "60 * * * *", "* 24 * * *", "* * 0 * *", "* * 32 * *",
                            "* * * 13 *", "* * * * 8", "*/0 * * * *", "a b c d e", "5-3 * * * *", "? * * * *",
                            "1-2-3 * * * *", ",1 * * * *", "1, * * * *", "1,,2 * * * *", "L * * * *", "* * * FOO *",
                            "*/-1 * * * *", "99999999999 * * * *"}) {
        const auto r = CronExpression::parse(bad);
        if (r) {
            std::fprintf(stderr, "  accepted invalid expression: '%s'\n", bad);
        }
        CHECK(!r);
        if (!r) {
            CHECK(r.error().code == ErrorCode::InvalidCron);
        }
    }
    CHECK(CronExpression::parse("  0   12  * * *  ").has_value()); // extra whitespace is fine
}

TEST(Cron, cron_next_matches_brute_force) {
    const std::vector<const char*> expressions{
        "* * * * *",      "*/7 * * * *",   "0 */3 * * *", "15 10 * * *",       "0 0 * * MON",
        "*/13 * * * * *", "5,35 * * * *",  "0 12 13 * FRI", "30 2 1,15 * *",    "0 0 29 2 *",
        "7 7 7 7 *",      "*/20 8-9 * * 1-5"};
    std::mt19937 rng(12345);
    int scanned = 0;
    int sampled = 0;
    for (int i = 0; i < 120; ++i) {
        const auto* expr = expressions[rng() % expressions.size()];
        const auto parsed = CronExpression::parse(expr);
        CHECK(parsed.has_value());
        if (!parsed) {
            continue;
        }
        // random start between 2026-01-01 and 2029-01-01, second resolution
        const auto start = utc(2026, 1, 1) + std::chrono::seconds{rng() % (3u * 365u * 86400u)};
        const auto n = parsed->next(start);
        if (!n) {
            continue;
        }
        CHECK(*n > start);
        CHECK(parsed->matches(*n));
        const auto gap = std::chrono::duration_cast<std::chrono::seconds>(*n - start).count();
        if (gap <= 200'000) {
            // exhaustive: no second in (start, n) may match
            bool clean = true;
            for (auto s = start + 1s; s < *n; s += 1s) {
                if (parsed->matches(s)) {
                    clean = false;
                    break;
                }
            }
            CHECK(clean);
            ++scanned;
        } else {
            ++sampled;
        }
    }
    std::printf("  brute-force cross-check: %d exhaustive, %d sampled\n", scanned, sampled);
    CHECK(scanned > 60);
}
