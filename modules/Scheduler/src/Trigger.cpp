#include <TK4CPP/Scheduler/CronExpression.h>
#include <TK4CPP/Scheduler/Types.h>

#include <charconv>

namespace TK4CPP::Scheduler {
namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

TimePoint truncMs(TimePoint tp) {
    return std::chrono::floor<Millis>(tp);
}

std::optional<std::int64_t> parseInt(std::string_view text) {
    std::int64_t value = 0;
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (text.empty() || ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace

Result<Trigger> Trigger::cron(std::string id, std::string jobId, std::string_view expression,
                              MisfirePolicy misfire) {
    if (id.empty() || jobId.empty()) {
        return makeError(ErrorCode::InvalidArgument, "trigger id and job id must not be empty");
    }
    const auto parsed = CronExpression::parse(expression);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    Trigger t;
    t.id = std::move(id);
    t.jobId = std::move(jobId);
    t.spec = CronSpec{std::string(expression)};
    t.misfire = misfire;
    return t;
}

Result<Trigger> Trigger::interval(std::string id, std::string jobId, Millis every,
                                  std::optional<TimePoint> start, std::int64_t repeatCount,
                                  MisfirePolicy misfire) {
    if (id.empty() || jobId.empty()) {
        return makeError(ErrorCode::InvalidArgument, "trigger id and job id must not be empty");
    }
    if (every < Millis{1}) {
        return makeError(ErrorCode::InvalidArgument, "interval must be at least 1 ms");
    }
    if (repeatCount == 0 || repeatCount < -1) {
        return makeError(ErrorCode::InvalidArgument, "repeat count must be -1 (unlimited) or positive");
    }
    Trigger t;
    t.id = std::move(id);
    t.jobId = std::move(jobId);
    t.spec = IntervalSpec{every, truncMs(start.value_or(Clock::now()))};
    t.misfire = misfire;
    t.remaining = repeatCount;
    return t;
}

Trigger Trigger::once(std::string id, std::string jobId, TimePoint at, MisfirePolicy misfire) {
    Trigger t;
    t.id = std::move(id);
    t.jobId = std::move(jobId);
    t.spec = OnceSpec{truncMs(at)};
    t.misfire = misfire;
    return t;
}

std::optional<TimePoint> nextFireAfter(const TriggerSpec& spec, TimePoint after) {
    return std::visit(
        Overloaded{
            [&](const CronSpec& c) -> std::optional<TimePoint> {
                const auto expr = CronExpression::parse(c.expression);
                if (!expr) {
                    return std::nullopt;
                }
                return expr->next(after);
            },
            [&](const IntervalSpec& i) -> std::optional<TimePoint> {
                if (i.every < Millis{1}) {
                    return std::nullopt;
                }
                if (after < i.start) {
                    return i.start;
                }
                const auto slots = (after - i.start) / i.every + 1;
                return i.start + i.every * slots;
            },
            [](const OnceSpec&) -> std::optional<TimePoint> { return std::nullopt; }},
        spec);
}

std::optional<TimePoint> firstFire(const TriggerSpec& spec, TimePoint now) {
    return std::visit(
        Overloaded{
            [&](const CronSpec&) { return nextFireAfter(spec, now); },
            [&](const IntervalSpec& i) -> std::optional<TimePoint> {
                if (i.start >= now) {
                    return i.start;
                }
                return nextFireAfter(spec, now);
            },
            [](const OnceSpec& o) -> std::optional<TimePoint> { return o.at; }},
        spec);
}

std::pair<std::string, std::string> encodeSpec(const TriggerSpec& spec) {
    return std::visit(
        Overloaded{
            [](const CronSpec& c) { return std::pair<std::string, std::string>{"cron", c.expression}; },
            [](const IntervalSpec& i) {
                return std::pair<std::string, std::string>{
                    "interval", std::to_string(i.every.count()) + "|" + std::to_string(toEpochMillis(i.start))};
            },
            [](const OnceSpec& o) {
                return std::pair<std::string, std::string>{"once", std::to_string(toEpochMillis(o.at))};
            }},
        spec);
}

Result<TriggerSpec> decodeSpec(std::string_view kind, std::string_view text) {
    if (kind == "cron") {
        return TriggerSpec{CronSpec{std::string(text)}};
    }
    if (kind == "once") {
        const auto at = parseInt(text);
        if (!at) {
            return makeError(ErrorCode::StoreFailure, "corrupt once spec: " + std::string(text));
        }
        return TriggerSpec{OnceSpec{fromEpochMillis(*at)}};
    }
    if (kind == "interval") {
        const auto bar = text.find('|');
        if (bar == std::string_view::npos) {
            return makeError(ErrorCode::StoreFailure, "corrupt interval spec: " + std::string(text));
        }
        const auto every = parseInt(text.substr(0, bar));
        const auto start = parseInt(text.substr(bar + 1));
        if (!every || !start) {
            return makeError(ErrorCode::StoreFailure, "corrupt interval spec: " + std::string(text));
        }
        return TriggerSpec{IntervalSpec{Millis{*every}, fromEpochMillis(*start)}};
    }
    return makeError(ErrorCode::StoreFailure, "unknown trigger kind: " + std::string(kind));
}

} // namespace TK4CPP::Scheduler
