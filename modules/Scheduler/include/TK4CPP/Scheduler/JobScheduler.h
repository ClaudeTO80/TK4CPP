/**
 * @file JobScheduler.h
 * @brief Quartz-style scheduler: jobs, triggers, misfire handling and crash recovery.
 */
#pragma once

#include <TK4CPP/Scheduler/Export.h>

#include <TK4CPP/Scheduler/Error.h>
#include <TK4CPP/Scheduler/Executor.h>
#include <TK4CPP/Scheduler/JobStore.h>
#include <TK4CPP/Scheduler/Listener.h>
#include <TK4CPP/Scheduler/Types.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace TK4CPP::Scheduler {

/**
 * @brief Schedules jobs on UTC triggers and executes them on an executor.
 *
 * Model (a reduced version of Quartz):
 *  - a *job* (@ref JobDetail) names a registered handler and carries parameters;
 *  - a *trigger* (@ref Trigger) says when a job runs; a job may have many triggers;
 *  - one dispatcher thread sleeps until the earliest trigger is due and hands executions to
 *    an @ref IExecutor, so a slow job never delays other schedules;
 *  - the same job may run concurrently with itself (no serialisation is applied);
 *  - every change is written through to an @ref IJobStore.
 *
 * Delivery semantics: a fire is persisted (trigger advanced) *before* the job runs, i.e. at
 * most once per occurrence. If JobDetail::requestsRecovery is set, an in-flight record is
 * written in the same transaction and, after a crash, the interrupted execution is re-run once
 * with JobContext::recovering == true (at least once for that job).
 *
 * Lifecycle: construct, registerJob(), start(), use, shutdown(). An instance is single-use:
 * it cannot be restarted after shutdown(). All mutating calls require a started scheduler.
 * Public methods are thread-safe, except that shutdown() must not be called concurrently
 * with itself.
 */
class TK4CPP_SCHEDULER_EXPORT JobScheduler {
public:
    /// @brief Construction options.
    struct Options {
        std::shared_ptr<IJobStore> store;       ///< Persistence; defaults to a private RamJobStore.
        std::shared_ptr<IExecutor> executor;    ///< Execution back-end; defaults to a thread pool.
        std::size_t workerThreads{4};           ///< Pool size when @ref executor is not given.
        /**
         * Tolerated lateness of a trigger. An occurrence later than this is a *misfire* and the
         * trigger's MisfirePolicy decides. An occurrence within the threshold is NOT a misfire: it
         * fires normally, and so does every further occurrence that is already due, back to back
         * (as in Quartz). After a downtime D < threshold a trigger with period P therefore runs
         * about D / P times in a burst. For sub-second periods keep the threshold small
         * (a few multiples of the period) or use MisfirePolicy::SkipToNext.
         */
        Millis misfireThreshold{60'000};
        bool removeCompletedTriggers{true};     ///< Delete triggers that have no further occurrence.
    };

    JobScheduler();
    explicit JobScheduler(Options options);
    ~JobScheduler();
    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    /**
     * @brief Registers the handler for a job type. May be called at any time; jobs loaded from
     *        the store need their handler registered before their first fire.
     */
    [[nodiscard]] Result<void> registerJob(std::string type, JobHandler handler);

    /// @brief Adds an event listener.
    void addListener(std::shared_ptr<ISchedulerListener> listener);

    /**
     * @brief Loads persisted state, re-runs recoverable in-flight executions and starts dispatching.
     *
     * Triggers whose next fire is in the past are handled by their misfire policy.
     */
    [[nodiscard]] Result<void> start();

    /**
     * @brief Stops dispatching and the executor.
     * @param waitForQueuedJobs If true, queued executions still run; if false they are discarded
     *        (recoverable ones will be re-run at the next start). Executions already running are
     *        never interrupted: this call waits for them in both cases.
     */
    void shutdown(bool waitForQueuedJobs = true);

    /// @brief True between a successful start() and shutdown().
    [[nodiscard]] bool running() const;

    /// @brief Adds a job. The type must be registered. With @p replace an existing job is updated.
    [[nodiscard]] Result<void> addJob(JobDetail job, bool replace = false);

    /// @brief Removes a job and all its triggers.
    [[nodiscard]] Result<void> removeJob(std::string_view jobId);

    /// @brief Adds a trigger for an existing job. Computes nextFire unless already set.
    [[nodiscard]] Result<void> addTrigger(Trigger trigger, bool replace = false);

    /// @brief Removes a trigger.
    [[nodiscard]] Result<void> removeTrigger(std::string_view triggerId);

    /// @brief Stops a trigger from firing without deleting it.
    [[nodiscard]] Result<void> pauseTrigger(std::string_view triggerId);

    /**
     * @brief Resumes a paused trigger.
     *
     * Cron and interval triggers continue from the next future occurrence (occurrences missed
     * while paused are skipped); a one-shot trigger keeps its instant and the misfire policy applies.
     */
    [[nodiscard]] Result<void> resumeTrigger(std::string_view triggerId);

    /// @brief Runs a job immediately, outside any trigger (triggerId is empty in the context).
    [[nodiscard]] Result<void> triggerJobNow(std::string_view jobId);

    /// @brief Copy of a job, if present.
    [[nodiscard]] std::optional<JobDetail> getJob(std::string_view jobId) const;
    /// @brief Copy of a trigger, if present.
    [[nodiscard]] std::optional<Trigger> getTrigger(std::string_view triggerId) const;
    /// @brief Copies of all jobs.
    [[nodiscard]] std::vector<JobDetail> jobs() const;
    /// @brief Copies of all triggers.
    [[nodiscard]] std::vector<Trigger> triggers() const;

private:
    struct PendingFire {
        std::string triggerId;
        std::string jobId;
        std::string fireId;
        TimePoint scheduled{};
        bool recovering{false};
        bool tracked{false}; ///< An in-flight record exists in the store.
    };
    using Notification = std::function<void(ISchedulerListener&)>;

    void dispatchLoop();
    bool processDue(Trigger& trigger, TimePoint now, std::vector<PendingFire>& fires,
                    std::vector<Notification>& notes);
    [[nodiscard]] bool submit(PendingFire fire);
    void runFire(const PendingFire& fire);
    void finishTracked(const PendingFire& fire);
    void notify(const Notification& notification);
    void notifyAll(const std::vector<Notification>& notifications);
    [[nodiscard]] std::string makeFireId(const std::string& triggerId, TimePoint scheduled);
    [[nodiscard]] Result<void> requireRunning() const; // mtx_ must be held

    Options opts_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::map<std::string, JobHandler, std::less<>> handlers_;
    std::map<std::string, JobDetail, std::less<>> jobs_;
    std::map<std::string, Trigger, std::less<>> triggers_;
    bool started_{false};
    bool stopped_{false};
    bool stopping_{false};
    bool dirty_{false};
    std::thread dispatcher_;

    std::mutex listenersMtx_;
    std::vector<std::shared_ptr<ISchedulerListener>> listeners_;

    std::atomic<std::uint64_t> fireCounter_{0};
    std::string nonce_;
};

} // namespace TK4CPP::Scheduler
