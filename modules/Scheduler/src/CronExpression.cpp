#include <TK4CPP/Scheduler/CronExpression.h>

#include <array>
#include <cctype>
#include <charconv>
#include <span>
#include <vector>

namespace TK4CPP::Scheduler {
namespace {

using FieldResult = std::expected<std::uint64_t, std::string>;

constexpr std::array<std::string_view, 12> kMonthNames{"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
constexpr std::array<std::string_view, 7> kWeekdayNames{"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

std::optional<unsigned> parseNumber(std::string_view token) {
    if (token.empty()) {
        return std::nullopt;
    }
    unsigned value = 0;
    const char* end = token.data() + token.size();
    const auto [ptr, ec] = std::from_chars(token.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<unsigned> parseValue(std::string_view token, std::span<const std::string_view> names,
                                   unsigned nameBase) {
    if (token.empty()) {
        return std::nullopt;
    }
    if (std::isalpha(static_cast<unsigned char>(token.front())) != 0) {
        std::string upper(token);
        for (auto& c : upper) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == upper) {
                return nameBase + static_cast<unsigned>(i);
            }
        }
        return std::nullopt;
    }
    return parseNumber(token);
}

FieldResult parseField(std::string_view field, unsigned lo, unsigned hi,
                       std::span<const std::string_view> names, unsigned nameBase) {
    std::uint64_t mask = 0;
    std::size_t pos = 0;
    while (true) {
        const auto comma = field.find(',', pos);
        const auto part = field.substr(pos, comma == std::string_view::npos ? std::string_view::npos
                                                                            : comma - pos);
        const auto bad = [&](const char* what) {
            return std::unexpected(std::string(what) + " in '" + std::string(part) + "'");
        };

        std::string_view range = part;
        unsigned step = 1;
        const auto slash = part.find('/');
        if (slash != std::string_view::npos) {
            range = part.substr(0, slash);
            const auto s = parseNumber(part.substr(slash + 1));
            if (!s || *s == 0) {
                return bad("invalid step");
            }
            step = *s;
        }

        unsigned first = 0;
        unsigned last = 0;
        if (range == "*") {
            first = lo;
            last = hi;
        } else {
            const auto dash = range.find('-');
            if (dash == std::string_view::npos) {
                const auto v = parseValue(range, names, nameBase);
                if (!v) {
                    return bad("invalid value");
                }
                first = *v;
                last = (slash != std::string_view::npos) ? hi : *v;
            } else {
                const auto a = parseValue(range.substr(0, dash), names, nameBase);
                const auto b = parseValue(range.substr(dash + 1), names, nameBase);
                if (!a || !b) {
                    return bad("invalid range");
                }
                first = *a;
                last = *b;
            }
        }
        if (first < lo || last > hi || first > last) {
            return bad("value out of range");
        }
        for (unsigned v = first; v <= last;) {
            mask |= (std::uint64_t{1} << v);
            if (last - v < step) {
                break; // also protects against unsigned overflow of v += step
            }
            v += step;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return mask;
}

[[nodiscard]] bool bit(std::uint64_t mask, long position) noexcept {
    return ((mask >> static_cast<unsigned>(position)) & 1U) != 0U;
}

} // namespace

Result<CronExpression> CronExpression::parse(std::string_view expression) {
    std::vector<std::string_view> fields;
    std::size_t i = 0;
    while (i < expression.size()) {
        while (i < expression.size() && std::isspace(static_cast<unsigned char>(expression[i])) != 0) {
            ++i;
        }
        if (i >= expression.size()) {
            break;
        }
        std::size_t j = i;
        while (j < expression.size() && std::isspace(static_cast<unsigned char>(expression[j])) == 0) {
            ++j;
        }
        fields.push_back(expression.substr(i, j - i));
        i = j;
    }
    if (fields.size() != 5 && fields.size() != 6) {
        return makeError(ErrorCode::InvalidCron,
                         "expected 5 or 6 fields, got " + std::to_string(fields.size()));
    }
    const std::size_t off = fields.size() == 6 ? 1 : 0;

    CronExpression e;
    e.text_ = std::string(expression);

    const auto fail = [](const char* name, const std::string& message) {
        return makeError(ErrorCode::InvalidCron, std::string(name) + " field: " + message);
    };

    if (off == 1) {
        const auto r = parseField(fields[0], 0, 59, {}, 0);
        if (!r) {
            return fail("seconds", r.error());
        }
        e.seconds_ = *r;
    } else {
        e.seconds_ = 1; // second 0
    }
    const auto minutes = parseField(fields[off + 0], 0, 59, {}, 0);
    if (!minutes) {
        return fail("minutes", minutes.error());
    }
    const auto hours = parseField(fields[off + 1], 0, 23, {}, 0);
    if (!hours) {
        return fail("hours", hours.error());
    }
    const auto dom = parseField(fields[off + 2], 1, 31, {}, 0);
    if (!dom) {
        return fail("day-of-month", dom.error());
    }
    const auto month = parseField(fields[off + 3], 1, 12, kMonthNames, 1);
    if (!month) {
        return fail("month", month.error());
    }
    auto dow = parseField(fields[off + 4], 0, 7, kWeekdayNames, 0);
    if (!dow) {
        return fail("day-of-week", dow.error());
    }
    if ((*dow & (std::uint64_t{1} << 7)) != 0) { // 7 is Sunday as well
        *dow = (*dow | 1U) & ~(std::uint64_t{1} << 7);
    }

    e.minutes_ = *minutes;
    e.hours_ = static_cast<std::uint32_t>(*hours);
    e.daysOfMonth_ = static_cast<std::uint32_t>(*dom);
    e.months_ = static_cast<std::uint16_t>(*month);
    e.daysOfWeek_ = static_cast<std::uint8_t>(*dow);
    e.domStar_ = fields[off + 2].front() == '*';
    e.dowStar_ = fields[off + 4].front() == '*';
    return e;
}

bool CronExpression::dayMatches(std::chrono::year_month_day ymd, std::chrono::sys_days day) const {
    const bool domOk = ((daysOfMonth_ >> static_cast<unsigned>(ymd.day())) & 1U) != 0U;
    const bool dowOk = ((daysOfWeek_ >> std::chrono::weekday{day}.c_encoding()) & 1U) != 0U;
    // Vixie cron: if either field is a star, both must match; otherwise either may match.
    return (domStar_ || dowStar_) ? (domOk && dowOk) : (domOk || dowOk);
}

bool CronExpression::matches(TimePoint tp) const {
    using namespace std::chrono;
    const sys_seconds t = floor<seconds>(tp);
    const sys_days today = floor<days>(t);
    const year_month_day ymd{today};
    const hh_mm_ss<seconds> hms{t - today};
    return bit(seconds_, hms.seconds().count()) && bit(minutes_, hms.minutes().count()) &&
           bit(hours_, hms.hours().count()) && bit(months_, static_cast<unsigned>(ymd.month())) &&
           dayMatches(ymd, today);
}

std::optional<TimePoint> CronExpression::next(TimePoint after) const {
    using namespace std::chrono;
    sys_seconds t = floor<seconds>(after) + seconds{1};
    const sys_seconds horizon = t + days{366 * 9}; // covers 29 Feb across a skipped leap year (2096 -> 2104)

    while (t <= horizon) {
        const sys_days today = floor<days>(t);
        const year_month_day ymd{today};

        if (!bit(months_, static_cast<unsigned>(ymd.month()))) {
            const year_month_day firstOfMonth{ymd.year(), ymd.month(), day{1}};
            t = sys_days{firstOfMonth + months{1}};
            continue;
        }
        if (!dayMatches(ymd, today)) {
            t = today + days{1};
            continue;
        }
        const hh_mm_ss<seconds> hms{t - today};
        const long h = hms.hours().count();
        const long m = hms.minutes().count();
        const long s = hms.seconds().count();
        if (!bit(hours_, h)) {
            t = today + hours{h + 1};
            continue;
        }
        if (!bit(minutes_, m)) {
            t = today + hours{h} + minutes{m + 1};
            continue;
        }
        if (!bit(seconds_, s)) {
            t += seconds{1};
            continue;
        }
        return t;
    }
    return std::nullopt;
}

} // namespace TK4CPP::Scheduler
