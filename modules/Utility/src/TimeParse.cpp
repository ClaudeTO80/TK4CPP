#include <TK4CPP/Utility/TimeParse.hpp>

#include <charconv>
#include <cstdio>
#include <ctime>
#include <string>

namespace TK4CPP::Utility {

namespace {
bool allDigits(std::string_view s)
{
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}
} // namespace

std::optional<std::int64_t> parseTimestamp(std::string_view text, bool endOfDay,
                                           std::chrono::system_clock::time_point now)
{
    using namespace std::chrono;
    if (text.empty()) return std::nullopt;

    // Relative age: "7d", "36h", "2w".
    if (text.size() > 1 && std::string_view("smhdw").find(text.back()) != std::string_view::npos) {
        auto const digits = text.substr(0, text.size() - 1);
        if (allDigits(digits)) {
            std::int64_t n = 0;
            auto const [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
            if (ec != std::errc{} || end != digits.data() + digits.size() || n > 100000000) return std::nullopt;
            char const unit = text.back();
            std::int64_t const seconds = unit == 's' ? 1 : unit == 'm' ? 60 : unit == 'h' ? 3600 : unit == 'd' ? 86400 : 604800;
            return duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() - n * seconds;
        }
    }

    // Absolute local time.
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, consumed = 0;
    std::string const s(text);
    bool hasTime = false;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2d%n", &year, &month, &day, &consumed) != 3) return std::nullopt;
    std::string_view const rest = std::string_view(s).substr(static_cast<std::size_t>(consumed));
    if (!rest.empty()) {
        if (rest.front() != 'T' && rest.front() != ' ') return std::nullopt;
        std::string const tail(rest.substr(1));
        int n = 0;
        int got = std::sscanf(tail.c_str(), "%2d:%2d:%2d%n", &hour, &minute, &second, &n);
        if (got == 3 && static_cast<std::size_t>(n) == tail.size()) {
            hasTime = true;
        } else {
            second = 0;
            got = std::sscanf(tail.c_str(), "%2d:%2d%n", &hour, &minute, &n);
            if (got != 2 || static_cast<std::size_t>(n) != tail.size()) return std::nullopt;
            hasTime = true;
        }
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60 || year < 1)
        return std::nullopt;
    if (!hasTime && endOfDay) { hour = 23; minute = 59; second = 59; }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;
    std::time_t const t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return std::nullopt;
    if (tm.tm_mday != day || tm.tm_mon != month - 1) return std::nullopt; // mktime normalised an impossible date
    return static_cast<std::int64_t>(t);
}

} // namespace TK4CPP::Utility
