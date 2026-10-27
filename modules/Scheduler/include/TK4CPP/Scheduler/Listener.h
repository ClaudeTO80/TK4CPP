/**
 * @file Listener.h
 * @brief Observer interface for scheduler events.
 */
#pragma once

#include <TK4CPP/Scheduler/Types.h>

#include <chrono>
#include <optional>
#include <string_view>

namespace TK4CPP::Scheduler {

/**
 * @brief Receives scheduler events.
 *
 * Callbacks run on the dispatcher thread or on worker threads, never while the scheduler's
 * internal lock is held, so they may call back into the scheduler. They must be thread-safe
 * and should return quickly. Exceptions thrown by a listener are swallowed.
 */
class ISchedulerListener {
public:
    virtual ~ISchedulerListener() = default;

    /// @brief A handler is about to run.
    virtual void onJobStarted(const JobContext& context) { (void)context; }

    /**
     * @brief A handler returned or threw.
     * @param context Execution context.
     * @param error   Exception message if the handler threw, empty otherwise.
     * @param elapsed Handler run time.
     */
    virtual void onJobFinished(const JobContext& context, const std::optional<std::string>& error,
                               std::chrono::microseconds elapsed) {
        (void)context; (void)error; (void)elapsed;
    }

    /// @brief A trigger was found later than the misfire threshold (before the policy is applied).
    virtual void onMisfire(const Trigger& trigger, TimePoint scheduled) { (void)trigger; (void)scheduled; }

    /// @brief A trigger has no further occurrences.
    virtual void onTriggerComplete(const Trigger& trigger) { (void)trigger; }

    /// @brief Non-fatal problem (store failure, missing handler, lost in-flight execution...).
    virtual void onError(std::string_view message) { (void)message; }
};

} // namespace TK4CPP::Scheduler
