/**
 * @file SqliteJobStore.h
 * @brief Durable SQLite implementation of IJobStore.
 */
#pragma once

#include <TK4CPP/Scheduler/Export.h>

#include <TK4CPP/Scheduler/JobStore.h>

#include <filesystem>
#include <memory>
#include <mutex>

struct sqlite3;

namespace TK4CPP::Scheduler {

/**
 * @brief SQLite-backed store (WAL journal, synchronous=FULL, foreign keys on).
 *
 * Every fire is committed in one transaction before the job runs, so a process crash
 * (including SIGKILL) never leaves the trigger and the in-flight record inconsistent.
 * One database file must be used by one process at a time.
 */
class TK4CPP_SCHEDULER_EXPORT SqliteJobStore final : public IJobStore {
public:
    /**
     * @brief Opens (creating if needed) the database and its schema.
     * @param path Database file.
     */
    [[nodiscard]] static Result<std::shared_ptr<SqliteJobStore>> open(const std::filesystem::path& path);

    ~SqliteJobStore() override;
    SqliteJobStore(const SqliteJobStore&) = delete;
    SqliteJobStore& operator=(const SqliteJobStore&) = delete;

    [[nodiscard]] Result<StoreSnapshot> load() override;
    [[nodiscard]] Result<void> putJob(const JobDetail& job) override;
    [[nodiscard]] Result<void> eraseJob(std::string_view jobId) override;
    [[nodiscard]] Result<void> putTrigger(const Trigger& trigger) override;
    [[nodiscard]] Result<void> eraseTrigger(std::string_view triggerId) override;
    [[nodiscard]] Result<void> beginFire(const std::optional<Trigger>& trigger,
                                         const std::optional<InflightFire>& inflight) override;
    [[nodiscard]] Result<void> endFire(std::string_view fireId) override;

private:
    explicit SqliteJobStore(sqlite3* db) : db_(db) {}
    [[nodiscard]] Result<void> initSchema();

    std::mutex mutex_;
    sqlite3* db_;
};

} // namespace TK4CPP::Scheduler
