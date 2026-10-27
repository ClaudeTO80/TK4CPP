/**
 * @file JobStore.h
 * @brief Persistence interface and the in-memory implementation.
 */
#pragma once

#include <TK4CPP/Scheduler/Export.h>

#include <TK4CPP/Scheduler/Error.h>
#include <TK4CPP/Scheduler/Types.h>

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace TK4CPP::Scheduler {

/// @brief Everything a store holds; handed to the scheduler at start-up.
struct StoreSnapshot {
    std::vector<JobDetail> jobs;
    std::vector<Trigger> triggers;
    std::vector<InflightFire> inflight;
};

/**
 * @brief Persistence backend of the scheduler.
 *
 * The scheduler keeps the working set in memory and writes every change through to the store.
 * Implementations must be thread-safe. Each method is atomic on its own.
 */
class IJobStore {
public:
    virtual ~IJobStore() = default;

    /// @brief Loads the whole persisted state.
    [[nodiscard]] virtual Result<StoreSnapshot> load() = 0;

    /// @brief Inserts or updates a job. Must NOT delete the job's triggers when updating.
    [[nodiscard]] virtual Result<void> putJob(const JobDetail& job) = 0;

    /// @brief Removes a job together with all its triggers.
    [[nodiscard]] virtual Result<void> eraseJob(std::string_view jobId) = 0;

    /// @brief Inserts or updates a trigger.
    [[nodiscard]] virtual Result<void> putTrigger(const Trigger& trigger) = 0;

    /// @brief Removes a trigger.
    [[nodiscard]] virtual Result<void> eraseTrigger(std::string_view triggerId) = 0;

    /**
     * @brief Atomically persists "this fire is about to run".
     * @param trigger  Updated trigger (next fire advanced), if the fire came from a trigger.
     * @param inflight In-flight record, if the job requests crash recovery.
     */
    [[nodiscard]] virtual Result<void> beginFire(const std::optional<Trigger>& trigger,
                                                 const std::optional<InflightFire>& inflight) = 0;

    /// @brief Removes an in-flight record once the execution finished.
    [[nodiscard]] virtual Result<void> endFire(std::string_view fireId) = 0;
};

/// @brief Volatile store: nothing survives the process, but it can be shared between schedulers.
class TK4CPP_SCHEDULER_EXPORT RamJobStore final : public IJobStore {
public:
    [[nodiscard]] Result<StoreSnapshot> load() override;
    [[nodiscard]] Result<void> putJob(const JobDetail& job) override;
    [[nodiscard]] Result<void> eraseJob(std::string_view jobId) override;
    [[nodiscard]] Result<void> putTrigger(const Trigger& trigger) override;
    [[nodiscard]] Result<void> eraseTrigger(std::string_view triggerId) override;
    [[nodiscard]] Result<void> beginFire(const std::optional<Trigger>& trigger,
                                         const std::optional<InflightFire>& inflight) override;
    [[nodiscard]] Result<void> endFire(std::string_view fireId) override;

private:
    std::mutex mutex_;
    std::map<std::string, JobDetail, std::less<>> jobs_;
    std::map<std::string, Trigger, std::less<>> triggers_;
    std::map<std::string, InflightFire, std::less<>> inflight_;
};

} // namespace TK4CPP::Scheduler
