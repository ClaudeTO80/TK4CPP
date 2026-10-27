#include <TK4CPP/Scheduler/JobStore.h>

namespace TK4CPP::Scheduler {

Result<StoreSnapshot> RamJobStore::load() {
    std::lock_guard lock(mutex_);
    StoreSnapshot snapshot;
    for (const auto& [id, job] : jobs_) {
        snapshot.jobs.push_back(job);
    }
    for (const auto& [id, trigger] : triggers_) {
        snapshot.triggers.push_back(trigger);
    }
    for (const auto& [id, fire] : inflight_) {
        snapshot.inflight.push_back(fire);
    }
    return snapshot;
}

Result<void> RamJobStore::putJob(const JobDetail& job) {
    std::lock_guard lock(mutex_);
    jobs_[job.id] = job;
    return {};
}

Result<void> RamJobStore::eraseJob(std::string_view jobId) {
    std::lock_guard lock(mutex_);
    if (const auto it = jobs_.find(jobId); it != jobs_.end()) {
        jobs_.erase(it);
    }
    std::erase_if(triggers_, [&](const auto& entry) { return entry.second.jobId == jobId; });
    return {};
}

Result<void> RamJobStore::putTrigger(const Trigger& trigger) {
    std::lock_guard lock(mutex_);
    if (!jobs_.contains(trigger.jobId)) {
        return makeError(ErrorCode::StoreFailure, "trigger references unknown job '" + trigger.jobId + "'");
    }
    triggers_[trigger.id] = trigger;
    return {};
}

Result<void> RamJobStore::eraseTrigger(std::string_view triggerId) {
    std::lock_guard lock(mutex_);
    if (const auto it = triggers_.find(triggerId); it != triggers_.end()) {
        triggers_.erase(it);
    }
    return {};
}

Result<void> RamJobStore::beginFire(const std::optional<Trigger>& trigger,
                                    const std::optional<InflightFire>& inflight) {
    std::lock_guard lock(mutex_);
    if (trigger) {
        triggers_[trigger->id] = *trigger;
    }
    if (inflight) {
        inflight_[inflight->fireId] = *inflight;
    }
    return {};
}

Result<void> RamJobStore::endFire(std::string_view fireId) {
    std::lock_guard lock(mutex_);
    if (const auto it = inflight_.find(fireId); it != inflight_.end()) {
        inflight_.erase(it);
    }
    return {};
}

} // namespace TK4CPP::Scheduler
