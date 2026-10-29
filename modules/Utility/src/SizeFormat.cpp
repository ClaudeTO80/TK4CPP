#include <TK4CPP/Utility/SizeFormat.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace TK4CPP::Utility {

std::string formatSize(std::uintmax_t bytes, SizeUnits units, int precision)
{
    static constexpr char const* binary[]  = {"B", "KB",  "MB",  "GB",  "TB",  "PB"};
    static constexpr char const* decimal[] = {"B", "kB",  "MB",  "GB",  "TB",  "PB"};
    static constexpr char const* iec[]     = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double const base = units == SizeUnits::Decimal ? 1000.0 : 1024.0;
    char const* const* labels = units == SizeUnits::Decimal ? decimal : (units == SizeUnits::Iec ? iec : binary);

    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= base && unit < 5) { value /= base; ++unit; }

    char buffer[64];
    if (unit == 0) {
        std::snprintf(buffer, sizeof buffer, "%ju B", static_cast<std::uintmax_t>(bytes));
    } else {
        std::snprintf(buffer, sizeof buffer, "%.*f %s", std::clamp(precision, 0, 6), value, labels[unit]);
        // snprintf honours the C locale only if the program never called setlocale(); keep the output stable.
        for (char* c = buffer; *c; ++c) if (*c == ',') *c = '.';
    }
    return buffer;
}

std::optional<std::uintmax_t> parseSize(std::string_view text)
{
    std::size_t split = 0;
    while (split < text.size() && ((text[split] >= '0' && text[split] <= '9') || text[split] == '.')) ++split;
    if (split == 0) return std::nullopt;
    std::string const number(text.substr(0, split));
    if (std::count(number.begin(), number.end(), '.') > 1) return std::nullopt;

    std::string unit(text.substr(split));
    for (char& c : unit) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

    double multiplier = 1;
    if (unit.empty() || unit == "b") multiplier = 1;
    else if (unit == "k" || unit == "kb") multiplier = 1e3;
    else if (unit == "m" || unit == "mb") multiplier = 1e6;
    else if (unit == "g" || unit == "gb") multiplier = 1e9;
    else if (unit == "t" || unit == "tb") multiplier = 1e12;
    else if (unit == "kib") multiplier = 1024.0;
    else if (unit == "mib") multiplier = 1024.0 * 1024;
    else if (unit == "gib") multiplier = 1024.0 * 1024 * 1024;
    else if (unit == "tib") multiplier = 1024.0 * 1024 * 1024 * 1024;
    else return std::nullopt;

    double value = 0;
    auto const [end, ec] = std::from_chars(number.data(), number.data() + number.size(), value);
    if (ec != std::errc{} || end != number.data() + number.size()) return std::nullopt;
    double const bytes = std::round(value * multiplier);
    if (!(bytes >= 0) || bytes >= 18446744073709551615.0) return std::nullopt;
    return static_cast<std::uintmax_t>(bytes);
}

} // namespace TK4CPP::Utility
