#include <TK4CPP/Scheduler/ThreadPoolAdapter.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace TK4CPP::Scheduler {

/// Book-keeping shared between the adapter and the task wrappers (which may outlive the adapter).
struct ThreadPoolAdapter::State {
    std::mutex mutex;
    std::condition_variable cv;
    /// Weak: the pool owns queued/running tasks, so no ownership cycle and a task destroyed by the
    /// pool without running simply expires.
    std::unordered_map<std::uint64_t, std::weak_ptr<TP::ITask>> live;
    std::atomic<std::uint64_t> next{0};
    bool closed{false};

    /// Drops entries of tasks that will never run or have ended. Caller holds @ref mutex.
    void sweepLocked() {
        for (auto it = live.begin(); it != live.end();) {
            const auto task = it->second.lock();
            const bool gone = !task || task->status() == TP::TaskStatus::Cancelled ||
                              task->status() == TP::TaskStatus::Completed ||
                              task->status() == TP::TaskStatus::Failed;
            it = gone ? live.erase(it) : std::next(it);
        }
    }
};

ThreadPoolAdapter::ThreadPoolAdapter(std::shared_ptr<TP::ThreadPool> pool)
    : ThreadPoolAdapter(std::move(pool), Options{}) {}

ThreadPoolAdapter::ThreadPoolAdapter(std::shared_ptr<TP::ThreadPool> pool, Options options)
    : pool_(std::move(pool)), options_(std::move(options)), state_(std::make_shared<State>()) {
    if (!pool_) {
        throw std::invalid_argument("ThreadPoolAdapter: the pool must not be null");
    }
    if (options_.startPoolIfNeeded && !pool_->isRunning()) {
        pool_->start();
    }
}

std::shared_ptr<ThreadPoolAdapter> ThreadPoolAdapter::createOwning(TP::ThreadPoolConfig config) {
    Options options;
    options.startPoolIfNeeded = true;
    options.stopPoolOnShutdown = true;
    return std::make_shared<ThreadPoolAdapter>(std::make_shared<TP::ThreadPool>(std::move(config)),
                                               std::move(options));
}

ThreadPoolAdapter::~ThreadPoolAdapter() {
    // Task wrappers only reference the shared State, so they stay valid if the adapter dies first.
    std::lock_guard lock(state_->mutex);
    state_->closed = true;
}

bool ThreadPoolAdapter::submit(std::function<void()> task, std::string_view label) {
    const auto state = state_;
    const std::uint64_t id = state->next.fetch_add(1);

    // Removes the bookkeeping entry when the wrapper ends, however it ends.
    struct Done {
        State& s;
        std::uint64_t id;
        ~Done() {
            {
                std::lock_guard lock(s.mutex);
                s.live.erase(id);
            }
            s.cv.notify_all();
        }
    };
    auto wrapper = [state, id, fn = std::move(task)]() {
        const Done done{*state, id};
        fn();
    };
    auto tpTask = std::make_shared<TP::FunctionTask>(TP::FunctionTask::Func(std::move(wrapper)),
                                                     options_.labelPrefix + std::string(label),
                                                     options_.priority);
    {
        std::lock_guard lock(state->mutex);
        if (state->closed) {
            ++rejected_;
            return false;
        }
        state->live.emplace(id, tpTask);
    }

    if (const auto accepted = pool_->submit(tpTask); !accepted) {
        {
            std::lock_guard lock(state->mutex);
            state->live.erase(id);
        }
        ++rejected_;
        return false;
    }
    return true;
}

void ThreadPoolAdapter::shutdown(bool drainQueue) {
    std::vector<std::shared_ptr<TP::ITask>> toCancel;
    {
        std::lock_guard lock(state_->mutex);
        state_->closed = true;
        if (!drainQueue) {
            for (const auto& entry : state_->live) {
                if (auto t = entry.second.lock()) {
                    toCancel.push_back(std::move(t));
                }
            }
        }
    }
    // A task that has not started becomes Cancelled and never runs; a running one is left alone
    // (cancel() only signals its stop token, which scheduler jobs do not poll).
    for (const auto& t : toCancel) {
        (void)t->cancel();
    }
    toCancel.clear();

    if (options_.stopPoolOnShutdown) {
        try {
            pool_->shutdown(drainQueue);
        } catch (const std::logic_error&) {
            // Called from a pool worker: the pool cannot join itself. The wait below would never
            // end either; this is a documented misuse, so just do not make it worse.
        }
    }

    std::unique_lock lock(state_->mutex);
    for (;;) {
        state_->sweepLocked();
        if (state_->live.empty()) {
            return;
        }
        // Woken by finishing wrappers; the timeout covers tasks the pool discarded behind our back.
        state_->cv.wait_for(lock, std::chrono::milliseconds{20});
    }
}

std::size_t ThreadPoolAdapter::outstanding() const {
    std::lock_guard lock(state_->mutex);
    state_->sweepLocked();
    return state_->live.size();
}

} // namespace TK4CPP::Scheduler
