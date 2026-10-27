/**
 * @file TestSupport.h
 * @brief Helpers shared by the GoogleTest cases of the Scheduler module.
 */
#pragma once

#include <TK4CPP/Scheduler/JobScheduler.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace T {

using namespace TK4CPP::Scheduler;
using namespace std::chrono_literals;

/// Kept as a short alias so the assertions read the same as in the standalone suite.
#define CHECK(cond) EXPECT_TRUE(cond)

/// @brief Polls @p pred until true or @p timeout elapses.
inline bool waitFor(const std::function<bool()>& pred, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return pred();
}

/// @brief UTC instant from calendar fields.
inline TimePoint utc(int y, unsigned mo, unsigned d, unsigned h = 0, unsigned mi = 0, unsigned s = 0) {
    using namespace std::chrono;
    return sys_days{year{y} / month{mo} / day{d}} + hours{h} + minutes{mi} + seconds{s};
}

/// @brief Listener that records everything it sees.
class Recorder final : public ISchedulerListener {
public:
    struct Finished {
        JobContext ctx;
        std::optional<std::string> error;
    };

    void onJobStarted(const JobContext& c) override {
        std::lock_guard l(m);
        started.push_back(c);
    }
    void onJobFinished(const JobContext& c, const std::optional<std::string>& e,
                       std::chrono::microseconds) override {
        std::lock_guard l(m);
        finished.push_back({c, e});
    }
    void onMisfire(const Trigger& t, TimePoint) override {
        std::lock_guard l(m);
        misfires.push_back(t.id);
    }
    void onTriggerComplete(const Trigger& t) override {
        std::lock_guard l(m);
        completed.push_back(t.id);
    }
    void onError(std::string_view msg) override {
        std::lock_guard l(m);
        errors.emplace_back(msg);
    }

    template <class F>
    auto with(F&& f) {
        std::lock_guard l(m);
        return f(*this);
    }

    mutable std::mutex m;
    std::vector<JobContext> started;
    std::vector<Finished> finished;
    std::vector<std::string> misfires;
    std::vector<std::string> completed;
    std::vector<std::string> errors;
};

/// @brief Unique scratch path under the system temp dir; removed on destruction.
class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("tk4sched_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    [[nodiscard]] std::filesystem::path file(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

} // namespace T
