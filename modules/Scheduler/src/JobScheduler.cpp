#include <TK4CPP/Scheduler/JobScheduler.h>

#include <TK4CPP/Scheduler/CronExpression.h>

#include <algorithm>
#include <cstdio>
#include <random>

namespace TK4CPP::Scheduler {
namespace {

using namespace std::chrono_literals;

TimePoint nowMs() {
    return std::chrono::floor<Millis>(Clock::now());
}

TimePoint truncMs(TimePoint tp) {
    return std::chrono::floor<Millis>(tp);
}

std::string randomNonce() {
    std::random_device rd;
    const std::uint64_t value = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace

JobScheduler::JobScheduler() : JobScheduler(Options{}) {}

JobScheduler::JobScheduler(Options options) : opts_(std::move(options)), nonce_(randomNonce()) {
    if (!opts_.store) {
        opts_.store = std::make_shared<RamJobStore>();
    }
}

JobScheduler::~JobScheduler() {
    shutdown(true);
}

Result<void> JobScheduler::registerJob(std::string type, JobHandler handler) {
    if (type.empty() || !handler) {
        return makeError(ErrorCode::InvalidArgument, "job type and handler must be set");
    }
    std::lock_guard lock(mtx_);
    handlers_[std::move(type)] = std::move(handler);
    return {};
}

void JobScheduler::addListener(std::shared_ptr<ISchedulerListener> listener) {
    if (!listener) {
        return;
    }
    std::lock_guard lock(listenersMtx_);
    listeners_.push_back(std::move(listener));
}

void JobScheduler::notify(const Notification& notification) {
    std::vector<std::shared_ptr<ISchedulerListener>> copy;
    {
        std::lock_guard lock(listenersMtx_);
        copy = listeners_;
    }
    for (const auto& listener : copy) {
        try {
            notification(*listener);
        } catch (...) {
            // A misbehaving listener must never break scheduling.
        }
    }
}

void JobScheduler::notifyAll(const std::vector<Notification>& notifications) {
    for (const auto& n : notifications) {
        notify(n);
    }
}

std::string JobScheduler::makeFireId(const std::string& triggerId, TimePoint scheduled) {
    (void)triggerId;
    (void)scheduled;
    return nonce_ + "-" + std::to_string(fireCounter_.fetch_add(1, std::memory_order_relaxed));
}

Result<void> JobScheduler::requireRunning() const {
    if (!started_ || stopping_) {
        return makeError(ErrorCode::InvalidState, "scheduler is not running");
    }
    return {};
}

bool JobScheduler::running() const {
    std::lock_guard lock(mtx_);
    return started_ && !stopping_;
}

Result<void> JobScheduler::start() {
    std::vector<PendingFire> recovered;
    std::vector<Notification> notes;
    const auto problem = [&notes](std::string message) {
        notes.push_back([m = std::move(message)](ISchedulerListener& l) { l.onError(m); });
    };
    {
        std::lock_guard lock(mtx_);
        if (started_) {
            return makeError(ErrorCode::InvalidState,
                             stopped_ ? "scheduler was shut down and cannot be restarted" : "already started");
        }
        auto snapshot = opts_.store->load();
        if (!snapshot) {
            return std::unexpected(snapshot.error());
        }
        if (!opts_.executor) {
            opts_.executor = std::make_shared<ThreadPoolExecutor>(opts_.workerThreads);
        }
        const auto now = nowMs();

        for (auto& job : snapshot->jobs) {
            jobs_[job.id] = std::move(job);
        }
        for (auto& t : snapshot->triggers) {
            if (t.state == TriggerState::Complete && opts_.removeCompletedTriggers) {
                if (auto r = opts_.store->eraseTrigger(t.id); !r) {
                    problem("cannot remove completed trigger '" + t.id + "': " + r.error().message);
                }
                continue;
            }
            if (t.state == TriggerState::Normal) {
                bool valid = true;
                if (const auto* cron = std::get_if<CronSpec>(&t.spec); cron != nullptr) {
                    valid = CronExpression::parse(cron->expression).has_value();
                }
                if (valid && !t.nextFire) {
                    t.nextFire = firstFire(t.spec, now);
                    valid = t.nextFire.has_value();
                }
                if (!valid) {
                    t.state = TriggerState::Paused;
                    problem("trigger '" + t.id + "' has an unusable schedule and was paused");
                    if (auto r = opts_.store->putTrigger(t); !r) {
                        problem("cannot persist paused trigger '" + t.id + "': " + r.error().message);
                    }
                }
            }
            triggers_[t.id] = std::move(t);
        }
        for (const auto& f : snapshot->inflight) {
            const auto it = jobs_.find(f.jobId);
            if (it != jobs_.end() && it->second.requestsRecovery) {
                recovered.push_back(PendingFire{f.triggerId, f.jobId, f.fireId, f.scheduled, true, true});
            } else {
                problem("dropping in-flight execution of job '" + f.jobId + "' (job removed or not recoverable)");
                (void)opts_.store->endFire(f.fireId);
            }
        }
        started_ = true;
        dirty_ = true;
        dispatcher_ = std::thread([this] { dispatchLoop(); });
    }
    notifyAll(notes);
    for (auto& fire : recovered) {
        (void)submit(std::move(fire)); // a refusal is reported; the record stays for the next start
    }
    return {};
}

void JobScheduler::shutdown(bool waitForQueuedJobs) {
    {
        std::lock_guard lock(mtx_);
        if (!started_ || stopping_) {
            return;
        }
        stopping_ = true;
    }
    cv_.notify_all();
    if (dispatcher_.joinable()) {
        dispatcher_.join();
    }
    opts_.executor->shutdown(waitForQueuedJobs);
    std::lock_guard lock(mtx_);
    stopped_ = true;
}

void JobScheduler::dispatchLoop() {
    std::unique_lock lock(mtx_);
    while (!stopping_) {
        const auto now = nowMs();
        std::optional<TimePoint> earliest;
        for (const auto& [id, t] : triggers_) {
            if (t.state == TriggerState::Normal && t.nextFire && (!earliest || *t.nextFire < *earliest)) {
                earliest = t.nextFire;
            }
        }
        if (!earliest || *earliest > now) {
            // The cap makes the loop re-read the wall clock regularly, so clock jumps self-heal.
            const auto cap = now + 30s;
            const auto wake = earliest ? std::min(*earliest, cap) : cap;
            cv_.wait_until(lock, wake, [this] { return stopping_ || dirty_; });
            dirty_ = false;
            continue;
        }

        std::vector<PendingFire> fires;
        std::vector<Notification> notes;
        std::vector<std::string> removals;
        for (auto& [id, t] : triggers_) {
            if (t.state == TriggerState::Normal && t.nextFire && *t.nextFire <= now) {
                if (processDue(t, now, fires, notes)) {
                    removals.push_back(id);
                }
            }
        }
        for (const auto& id : removals) {
            triggers_.erase(id);
        }

        lock.unlock();
        notifyAll(notes);
        for (auto& fire : fires) {
            (void)submit(std::move(fire)); // a refusal is reported to the listeners by submit()
        }
        lock.lock();
    }
}

bool JobScheduler::processDue(Trigger& trigger, TimePoint now, std::vector<PendingFire>& fires,
                              std::vector<Notification>& notes) {
    const auto problem = [&notes](std::string message) {
        notes.push_back([m = std::move(message)](ISchedulerListener& l) { l.onError(m); });
    };
    const TimePoint scheduled = *trigger.nextFire;

    const auto jobIt = jobs_.find(trigger.jobId);
    if (jobIt == jobs_.end()) {
        problem("trigger '" + trigger.id + "' references missing job '" + trigger.jobId + "'; removed");
        (void)opts_.store->eraseTrigger(trigger.id);
        return true;
    }

    bool fire = true;
    std::optional<TimePoint> next;
    if ((now - scheduled) > opts_.misfireThreshold) {
        notes.push_back([t = trigger, scheduled](ISchedulerListener& l) { l.onMisfire(t, scheduled); });
        switch (trigger.misfire) {
        case MisfirePolicy::FireNow:
            next = nextFireAfter(trigger.spec, now);
            break;
        case MisfirePolicy::SkipToNext:
            fire = false;
            next = nextFireAfter(trigger.spec, now);
            break;
        case MisfirePolicy::CatchUp:
            next = nextFireAfter(trigger.spec, scheduled);
            break;
        }
    } else {
        next = nextFireAfter(trigger.spec, scheduled);
    }

    if (fire) {
        trigger.lastFire = now;
        if (trigger.remaining > 0 && --trigger.remaining == 0) {
            next.reset();
        }
    }
    if (next) {
        trigger.nextFire = truncMs(*next);
    } else {
        trigger.nextFire.reset();
        trigger.state = TriggerState::Complete;
        notes.push_back([t = trigger](ISchedulerListener& l) { l.onTriggerComplete(t); });
    }

    PendingFire pending;
    if (fire) {
        pending = PendingFire{trigger.id, trigger.jobId, makeFireId(trigger.id, scheduled), scheduled, false, false};
        std::optional<InflightFire> inflight;
        if (jobIt->second.requestsRecovery) {
            inflight = InflightFire{pending.fireId, trigger.id, trigger.jobId, scheduled};
            pending.tracked = true;
        }
        // One transaction: advance the trigger and record the execution as in flight.
        if (auto r = opts_.store->beginFire(trigger, inflight); !r) {
            problem("store failure while firing '" + trigger.id + "': " + r.error().message);
            pending.tracked = false;
        }
    } else if (auto r = opts_.store->putTrigger(trigger); !r) {
        problem("store failure while updating '" + trigger.id + "': " + r.error().message);
    }

    const bool remove = trigger.state == TriggerState::Complete && opts_.removeCompletedTriggers;
    if (remove) {
        if (auto r = opts_.store->eraseTrigger(trigger.id); !r) {
            problem("cannot remove completed trigger '" + trigger.id + "': " + r.error().message);
        }
    }
    if (fire) {
        fires.push_back(std::move(pending));
    }
    return remove;
}

bool JobScheduler::submit(PendingFire fire) {
    const std::string jobId = fire.jobId;
    const std::string fireId = fire.fireId;
    const bool tracked = fire.tracked;
    if (opts_.executor->submit([this, fire = std::move(fire)] { runFire(fire); }, jobId)) {
        return true;
    }
    // The occurrence was already persisted as fired. A tracked (recoverable) execution keeps its
    // in-flight record and is re-run at the next start(); an untracked one is lost.
    notify([m = "executor rejected the execution of job '" + jobId + "' (fire " + fireId + ")" +
                 (tracked ? "; it will be retried at the next start" : "; the occurrence is lost")](
               ISchedulerListener& l) { l.onError(m); });
    return false;
}

void JobScheduler::finishTracked(const PendingFire& fire) {
    if (!fire.tracked) {
        return;
    }
    if (auto r = opts_.store->endFire(fire.fireId); !r) {
        notify([m = "cannot clear in-flight record: " + r.error().message](ISchedulerListener& l) { l.onError(m); });
    }
}

void JobScheduler::runFire(const PendingFire& fire) {
    JobHandler handler;
    JobContext ctx;
    std::string missing;
    {
        std::lock_guard lock(mtx_);
        if (const auto jobIt = jobs_.find(fire.jobId); jobIt == jobs_.end()) {
            missing = "job '" + fire.jobId + "' was removed before its execution";
        } else if (const auto h = handlers_.find(jobIt->second.type); h == handlers_.end()) {
            missing = "no handler registered for job type '" + jobIt->second.type + "'";
        } else {
            handler = h->second;
            ctx.data = jobIt->second.data;
        }
    }
    if (!handler) {
        notify([m = std::move(missing)](ISchedulerListener& l) { l.onError(m); });
        finishTracked(fire);
        return;
    }

    ctx.jobId = fire.jobId;
    ctx.triggerId = fire.triggerId;
    ctx.fireId = fire.fireId;
    ctx.scheduledTime = fire.scheduled;
    ctx.actualTime = Clock::now();
    ctx.recovering = fire.recovering;

    notify([&ctx](ISchedulerListener& l) { l.onJobStarted(ctx); });

    std::optional<std::string> error;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        handler(ctx);
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "unknown exception";
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0);

    notify([&](ISchedulerListener& l) { l.onJobFinished(ctx, error, elapsed); });
    finishTracked(fire);
}

Result<void> JobScheduler::addJob(JobDetail job, bool replace) {
    if (job.id.empty() || job.type.empty()) {
        return makeError(ErrorCode::InvalidArgument, "job id and type must not be empty");
    }
    std::lock_guard lock(mtx_);
    if (auto r = requireRunning(); !r) {
        return r;
    }
    if (!handlers_.contains(job.type)) {
        return makeError(ErrorCode::UnknownJobType, "no handler registered for '" + job.type + "'");
    }
    if (!replace && jobs_.contains(job.id)) {
        return makeError(ErrorCode::AlreadyExists, "job '" + job.id + "' already exists");
    }
    if (auto r = opts_.store->putJob(job); !r) {
        return r;
    }
    jobs_[job.id] = std::move(job);
    return {};
}

Result<void> JobScheduler::removeJob(std::string_view jobId) {
    std::lock_guard lock(mtx_);
    if (auto r = requireRunning(); !r) {
        return r;
    }
    const auto it = jobs_.find(jobId);
    if (it == jobs_.end()) {
        return makeError(ErrorCode::NotFound, "job '" + std::string(jobId) + "' not found");
    }
    if (auto r = opts_.store->eraseJob(jobId); !r) {
        return r;
    }
    std::erase_if(triggers_, [&](const auto& entry) { return entry.second.jobId == jobId; });
    jobs_.erase(it);
    dirty_ = true;
    cv_.notify_all();
    return {};
}

Result<void> JobScheduler::addTrigger(Trigger trigger, bool replace) {
    if (trigger.id.empty() || trigger.jobId.empty()) {
        return makeError(ErrorCode::InvalidArgument, "trigger id and job id must not be empty");
    }
    if (const auto* cron = std::get_if<CronSpec>(&trigger.spec); cron != nullptr) {
        if (auto parsed = CronExpression::parse(cron->expression); !parsed) {
            return std::unexpected(parsed.error());
        }
    } else if (const auto* iv = std::get_if<IntervalSpec>(&trigger.spec); iv != nullptr && iv->every < Millis{1}) {
        return makeError(ErrorCode::InvalidArgument, "interval must be at least 1 ms");
    }

    std::lock_guard lock(mtx_);
    if (auto r = requireRunning(); !r) {
        return r;
    }
    if (!jobs_.contains(trigger.jobId)) {
        return makeError(ErrorCode::NotFound, "job '" + trigger.jobId + "' not found");
    }
    if (!replace && triggers_.contains(trigger.id)) {
        return makeError(ErrorCode::AlreadyExists, "trigger '" + trigger.id + "' already exists");
    }
    if (trigger.nextFire) {
        trigger.nextFire = truncMs(*trigger.nextFire);
    } else {
        trigger.nextFire = firstFire(trigger.spec, nowMs());
        if (!trigger.nextFire) {
            return makeError(ErrorCode::InvalidArgument, "the schedule never fires");
        }
    }
    if (auto r = opts_.store->putTrigger(trigger); !r) {
        return r;
    }
    triggers_[trigger.id] = std::move(trigger);
    dirty_ = true;
    cv_.notify_all();
    return {};
}

Result<void> JobScheduler::removeTrigger(std::string_view triggerId) {
    std::lock_guard lock(mtx_);
    if (auto r = requireRunning(); !r) {
        return r;
    }
    const auto it = triggers_.find(triggerId);
    if (it == triggers_.end()) {
        return makeError(ErrorCode::NotFound, "trigger '" + std::string(triggerId) + "' not found");
    }
    if (auto r = opts_.store->eraseTrigger(triggerId); !r) {
        return r;
    }
    triggers_.erase(it);
    dirty_ = true;
    cv_.notify_all();
    return {};
}

Result<void> JobScheduler::pauseTrigger(std::string_view triggerId) {
    std::lock_guard lock(mtx_);
    if (auto r = requireRunning(); !r) {
        return r;
    }
    const auto it = triggers_.find(triggerId);
    if (it == triggers_.end()) {
        return makeError(ErrorCode::NotFound, "trigger '" + std::string(triggerId) + "' not found");
    }
    if (it->second.state == TriggerState::Complete) {
        return makeError(ErrorCode::InvalidState, "trigger is complete");
    }
    Trigger updated = it->second;
    updated.state = TriggerState::Paused;
    if (auto r = opts_.store->putTrigger(updated); !r) {
        return r;
    }
    it->second = std::move(updated);
    dirty_ = true;
    cv_.notify_all();
    return {};
}

Result<void> JobScheduler::resumeTrigger(std::string_view triggerId) {
    std::lock_guard lock(mtx_);
    if (auto r = requireRunning(); !r) {
        return r;
    }
    const auto it = triggers_.find(triggerId);
    if (it == triggers_.end()) {
        return makeError(ErrorCode::NotFound, "trigger '" + std::string(triggerId) + "' not found");
    }
    if (it->second.state == TriggerState::Complete) {
        return makeError(ErrorCode::InvalidState, "trigger is complete");
    }
    Trigger updated = it->second;
    updated.state = TriggerState::Normal;
    if (!std::holds_alternative<OnceSpec>(updated.spec)) {
        updated.nextFire = nextFireAfter(updated.spec, nowMs());
        if (!updated.nextFire) {
            return makeError(ErrorCode::InvalidState, "the schedule has no future occurrence");
        }
    }
    if (auto r = opts_.store->putTrigger(updated); !r) {
        return r;
    }
    it->second = std::move(updated);
    dirty_ = true;
    cv_.notify_all();
    return {};
}

Result<void> JobScheduler::triggerJobNow(std::string_view jobId) {
    PendingFire fire;
    {
        std::lock_guard lock(mtx_);
        if (auto r = requireRunning(); !r) {
            return r;
        }
        const auto it = jobs_.find(jobId);
        if (it == jobs_.end()) {
            return makeError(ErrorCode::NotFound, "job '" + std::string(jobId) + "' not found");
        }
        const auto now = nowMs();
        fire = PendingFire{"", it->second.id, makeFireId("", now), now, false, false};
        if (it->second.requestsRecovery) {
            const InflightFire record{fire.fireId, "", fire.jobId, now};
            if (auto r = opts_.store->beginFire(std::nullopt, record); !r) {
                return r;
            }
            fire.tracked = true;
        }
    }
    const std::string fireId = fire.fireId;
    const bool tracked = fire.tracked;
    if (!submit(std::move(fire))) {
        if (tracked) {
            (void)opts_.store->endFire(fireId); // the caller is told it failed: do not recover it later
        }
        return makeError(ErrorCode::ExecutorRejected, "the executor refused the execution");
    }
    return {};
}

std::optional<JobDetail> JobScheduler::getJob(std::string_view jobId) const {
    std::lock_guard lock(mtx_);
    if (const auto it = jobs_.find(jobId); it != jobs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<Trigger> JobScheduler::getTrigger(std::string_view triggerId) const {
    std::lock_guard lock(mtx_);
    if (const auto it = triggers_.find(triggerId); it != triggers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<JobDetail> JobScheduler::jobs() const {
    std::lock_guard lock(mtx_);
    std::vector<JobDetail> out;
    out.reserve(jobs_.size());
    for (const auto& [id, job] : jobs_) {
        out.push_back(job);
    }
    return out;
}

std::vector<Trigger> JobScheduler::triggers() const {
    std::lock_guard lock(mtx_);
    std::vector<Trigger> out;
    out.reserve(triggers_.size());
    for (const auto& [id, trigger] : triggers_) {
        out.push_back(trigger);
    }
    return out;
}

} // namespace TK4CPP::Scheduler
