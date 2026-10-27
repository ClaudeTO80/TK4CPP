/**
 * @file Types.h
 * @brief Core value types: time aliases, job details, triggers and fire contexts.
 *
 * All instants are UTC (std::chrono::system_clock). Persisted instants have millisecond
 * resolution; the scheduler truncates every instant it stores to milliseconds.
 */
#pragma once

#include <TK4CPP/Scheduler/Export.h>

#include <TK4CPP/Scheduler/Error.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace TK4CPP::Scheduler {

using Clock = std::chrono::system_clock; ///< Wall clock, interpreted as UTC.
using TimePoint = Clock::time_point;     ///< UTC instant.
using Millis = std::chrono::milliseconds;

/// @brief Converts an instant to milliseconds since the Unix epoch.
[[nodiscard]] inline std::int64_t toEpochMillis(TimePoint tp) noexcept {
    return std::chrono::duration_cast<Millis>(tp.time_since_epoch()).count();
}

/// @brief Converts milliseconds since the Unix epoch to an instant.
[[nodiscard]] inline TimePoint fromEpochMillis(std::int64_t ms) noexcept {
    return TimePoint{std::chrono::duration_cast<Clock::duration>(Millis{ms})};
}

/// @brief Key/value parameters handed to a job on every execution.
using JobData = std::map<std::string, std::string>;

/**
 * @brief Persistent description of *what* to run.
 *
 * The behaviour itself is a handler registered under @ref type; only data is persisted.
 */
struct JobDetail {
    std::string id;                ///< Unique job id.
    std::string type;              ///< Name of the registered handler.
    JobData data;                  ///< Parameters passed to the handler.
    bool requestsRecovery{false};  ///< Re-run once after a crash if the execution was in flight.
};

/// @brief What to do when a trigger fires later than the misfire threshold allows.
enum class MisfirePolicy : int {
    FireNow = 0,    ///< Fire once immediately, then continue from the current time.
    SkipToNext = 1, ///< Do not fire; continue with the next future occurrence.
    CatchUp = 2     ///< Fire every missed occurrence, one after the other, as fast as possible.
};

/// @brief Life-cycle state of a trigger.
enum class TriggerState : int {
    Normal = 0,   ///< Eligible for firing.
    Paused = 1,   ///< Kept but never fired until resumed.
    Complete = 2  ///< No further occurrences.
};

/// @brief Cron schedule (UTC). See @ref CronExpression for the supported syntax.
struct CronSpec {
    std::string expression;
};

/// @brief Fixed-rate schedule anchored at @ref start: fires at start + k * every.
struct IntervalSpec {
    Millis every{1000};
    TimePoint start{};
};

/// @brief Single execution at a given instant.
struct OnceSpec {
    TimePoint at{};
};

/// @brief The "when" of a trigger.
using TriggerSpec = std::variant<CronSpec, IntervalSpec, OnceSpec>;

/**
 * @brief Persistent description of *when* to run a job.
 *
 * Use the static factories to build well-formed instances. If @ref nextFire is empty when the
 * trigger is added to a scheduler, it is computed; if it is set it is honoured as-is (useful
 * to restore state or to test misfire handling).
 */
struct TK4CPP_SCHEDULER_EXPORT Trigger {
    std::string id;                                   ///< Unique trigger id.
    std::string jobId;                                ///< Job to run.
    TriggerSpec spec{OnceSpec{}};                     ///< Schedule.
    MisfirePolicy misfire{MisfirePolicy::FireNow};    ///< Late-fire policy.
    TriggerState state{TriggerState::Normal};         ///< Life-cycle state.
    std::optional<TimePoint> nextFire;                ///< Next scheduled instant.
    std::optional<TimePoint> lastFire;                ///< Last time the trigger fired.
    std::int64_t remaining{-1};                       ///< Fires left; -1 means unlimited.

    /**
     * @brief Creates a cron trigger.
     * @param id         Trigger id.
     * @param jobId      Job to run.
     * @param expression 5 fields (min hour dom month dow) or 6 fields with leading seconds, UTC.
     * @param misfire    Late-fire policy.
     */
    [[nodiscard]] static Result<Trigger> cron(std::string id, std::string jobId,
                                              std::string_view expression,
                                              MisfirePolicy misfire = MisfirePolicy::FireNow);

    /**
     * @brief Creates a fixed-rate trigger.
     * @param id          Trigger id.
     * @param jobId       Job to run.
     * @param every       Period (at least 1 ms).
     * @param start       First occurrence; defaults to "now" (fires immediately).
     * @param repeatCount Total number of fires, or -1 for unlimited.
     * @param misfire     Late-fire policy.
     */
    [[nodiscard]] static Result<Trigger> interval(std::string id, std::string jobId, Millis every,
                                                  std::optional<TimePoint> start = std::nullopt,
                                                  std::int64_t repeatCount = -1,
                                                  MisfirePolicy misfire = MisfirePolicy::FireNow);

    /**
     * @brief Creates a one-shot trigger.
     * @param id      Trigger id.
     * @param jobId   Job to run.
     * @param at      Execution instant (may be in the past: the misfire policy decides).
     * @param misfire Late-fire policy.
     */
    [[nodiscard]] static Trigger once(std::string id, std::string jobId, TimePoint at,
                                      MisfirePolicy misfire = MisfirePolicy::FireNow);
};

/// @brief Information handed to a job handler for one execution.
struct JobContext {
    std::string jobId;          ///< Executing job.
    std::string triggerId;      ///< Trigger that fired; empty for manual fires.
    std::string fireId;         ///< Unique id of this execution.
    JobData data;               ///< Copy of the job parameters.
    TimePoint scheduledTime{};  ///< Instant the execution was planned for.
    TimePoint actualTime{};     ///< Instant the handler was entered.
    bool recovering{false};     ///< True when re-run after a crash.
};

/// @brief Job behaviour. Exceptions are caught and reported to listeners.
using JobHandler = std::function<void(const JobContext&)>;

/// @brief Durable record of an execution that started but did not finish (crash recovery).
struct InflightFire {
    std::string fireId;
    std::string triggerId;
    std::string jobId;
    TimePoint scheduled{};
};

/**
 * @brief First occurrence of a freshly created trigger.
 * @return Cron: first match strictly after @p now. Interval: @c start if not before @p now,
 *         otherwise the next anchored slot. Once: the configured instant.
 */
[[nodiscard]] TK4CPP_SCHEDULER_EXPORT std::optional<TimePoint> firstFire(const TriggerSpec& spec, TimePoint now);

/// @brief Next occurrence strictly after @p after (empty for Once or exhausted cron).
[[nodiscard]] TK4CPP_SCHEDULER_EXPORT std::optional<TimePoint> nextFireAfter(const TriggerSpec& spec, TimePoint after);

/// @brief Serialises a spec as (kind, text) for persistence.
[[nodiscard]] TK4CPP_SCHEDULER_EXPORT std::pair<std::string, std::string> encodeSpec(const TriggerSpec& spec);

/// @brief Inverse of @ref encodeSpec.
[[nodiscard]] TK4CPP_SCHEDULER_EXPORT Result<TriggerSpec> decodeSpec(std::string_view kind, std::string_view text);

} // namespace TK4CPP::Scheduler
