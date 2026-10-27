#include "TestSupport.h"

#include <TK4CPP/Scheduler/SqliteJobStore.h>

#include <algorithm>
#include <set>

using namespace T;

namespace {

/// Floors an instant to a whole hour.
TimePoint floorHour(TimePoint tp) {
    return std::chrono::floor<std::chrono::hours>(tp);
}

struct Fixture {
    std::shared_ptr<Recorder> rec = std::make_shared<Recorder>();
    JobScheduler sched;

    explicit Fixture(JobScheduler::Options o = {}) : sched(std::move(o)) { sched.addListener(rec); }
};

JobScheduler::Options opts(std::shared_ptr<IJobStore> store = nullptr, Millis threshold = 60'000ms,
                           std::size_t workers = 4) {
    JobScheduler::Options o;
    o.store = std::move(store);
    o.misfireThreshold = threshold;
    o.workerThreads = workers;
    return o;
}

} // namespace

TEST(Scheduler, sched_lifecycle_and_argument_errors) {
    JobScheduler s;
    CHECK(!s.running());
    CHECK(s.addJob({"j", "t", {}, false}).error().code == ErrorCode::InvalidState); // not started
    CHECK(s.registerJob("t", [](const JobContext&) {}).has_value());
    CHECK(!s.registerJob("", [](const JobContext&) {}).has_value());
    CHECK(!s.registerJob("x", nullptr).has_value());
    CHECK(s.start().has_value());
    CHECK(s.running());
    CHECK(s.start().error().code == ErrorCode::InvalidState); // double start

    CHECK(s.addJob({"", "t", {}, false}).error().code == ErrorCode::InvalidArgument);
    CHECK(s.addJob({"j", "unknown", {}, false}).error().code == ErrorCode::UnknownJobType);
    CHECK(s.addJob({"j", "t", {}, false}).has_value());
    CHECK(s.addJob({"j", "t", {}, false}).error().code == ErrorCode::AlreadyExists);
    CHECK(s.addJob({"j", "t", {{"a", "1"}}, false}, true).has_value()); // replace
    CHECK(s.getJob("j")->data.at("a") == "1");

    auto good = Trigger::interval("t1", "j", 1h).value();
    CHECK(s.addTrigger(good).has_value());
    CHECK(s.addTrigger(good).error().code == ErrorCode::AlreadyExists);
    CHECK(s.addTrigger(Trigger::once("t2", "missing", utc(2030, 1, 1))).error().code == ErrorCode::NotFound);
    CHECK(Trigger::cron("c", "j", "nonsense").error().code == ErrorCode::InvalidCron);
    CHECK(Trigger::interval("i", "j", 0ms).error().code == ErrorCode::InvalidArgument);
    CHECK(Trigger::interval("i", "j", 1s, std::nullopt, 0).error().code == ErrorCode::InvalidArgument);
    Trigger badCron;
    badCron.id = "bc";
    badCron.jobId = "j";
    badCron.spec = CronSpec{"61 * * * *"};
    CHECK(s.addTrigger(badCron).error().code == ErrorCode::InvalidCron);
    Trigger never;
    never.id = "never";
    never.jobId = "j";
    never.spec = CronSpec{"0 0 31 2 *"};
    CHECK(s.addTrigger(never).error().code == ErrorCode::InvalidArgument);

    CHECK(s.removeTrigger("nope").error().code == ErrorCode::NotFound);
    CHECK(s.pauseTrigger("nope").error().code == ErrorCode::NotFound);
    CHECK(s.resumeTrigger("nope").error().code == ErrorCode::NotFound);
    CHECK(s.removeJob("nope").error().code == ErrorCode::NotFound);
    CHECK(s.triggerJobNow("nope").error().code == ErrorCode::NotFound);

    s.shutdown();
    CHECK(!s.running());
    CHECK(s.start().error().code == ErrorCode::InvalidState); // single use
    CHECK(s.addJob({"k", "t", {}, false}).error().code == ErrorCode::InvalidState);
    s.shutdown(); // idempotent
}

TEST(Scheduler, sched_interval_fires_exact_count_then_completes) {
    Fixture f;
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::interval("t", "j", 20ms, std::nullopt, 5).value()).has_value());
    CHECK(waitFor([&] { return count == 5; }));
    std::this_thread::sleep_for(200ms);
    CHECK(count == 5);                                  // never more than repeatCount
    CHECK(!f.sched.getTrigger("t").has_value());        // completed triggers are removed
    CHECK(f.rec->with([](auto& r) { return r.completed.size(); }) == 1);
    f.sched.shutdown();
}

TEST(Scheduler, sched_cron_second_resolution) {
    Fixture f;
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::cron("t", "j", "* * * * * *").value()).has_value());
    CHECK(waitFor([&] { return count >= 3; }, 5000ms));
    // each fire must land on a whole second (within a generous bound for scheduling jitter)
    f.rec->with([&](auto& r) {
        for (const auto& c : r.started) {
            const auto ms = toEpochMillis(c.scheduledTime) % 1000;
            CHECK(ms == 0);
            CHECK(c.actualTime >= c.scheduledTime);
            CHECK(c.actualTime - c.scheduledTime < 250ms);
            CHECK(c.triggerId == "t");
            CHECK(!c.recovering);
        }
    });
    f.sched.shutdown();
}

TEST(Scheduler, sched_same_job_runs_in_parallel_with_itself) {
    Fixture f(opts(nullptr, 60'000ms, 4));
    std::atomic<int> current{0};
    std::atomic<int> maxSeen{0};
    std::atomic<int> done{0};
    CHECK(f.sched
              .registerJob("slow",
                           [&](const JobContext&) {
                               const int now = ++current;
                               int prev = maxSeen.load();
                               while (prev < now && !maxSeen.compare_exchange_weak(prev, now)) {
                               }
                               std::this_thread::sleep_for(200ms);
                               --current;
                               ++done;
                           })
              .has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "slow", {}, false}).has_value());
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(f.sched.addTrigger(Trigger::interval("t", "j", 40ms, std::nullopt, 6).value()).has_value());
    CHECK(waitFor([&] { return done == 6; }, 5000ms));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    std::printf("  6 x 200ms job fired every 40ms: max concurrency %d, wall time %lld ms (serial would be >= 1200 ms)\n",
                maxSeen.load(), static_cast<long long>(elapsed.count()));
    CHECK(maxSeen >= 3);
    CHECK(maxSeen <= 4);            // bounded by the pool size
    CHECK(elapsed < 900ms);
    f.sched.shutdown();
}

TEST(Scheduler, sched_slow_job_does_not_delay_other_triggers) {
    Fixture f(opts(nullptr, 60'000ms, 2));
    std::atomic<int> fast{0};
    CHECK(f.sched.registerJob("slow", [](const JobContext&) { std::this_thread::sleep_for(600ms); }).has_value());
    CHECK(f.sched.registerJob("fast", [&](const JobContext&) { ++fast; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"s", "slow", {}, false}).has_value());
    CHECK(f.sched.addJob({"f", "fast", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::once("ts", "s", Clock::now())).has_value());
    CHECK(f.sched.addTrigger(Trigger::interval("tf", "f", 20ms).value()).has_value());
    std::this_thread::sleep_for(400ms); // the slow job is still running here
    const int seen = fast.load();
    std::printf("  fast trigger fired %d times while the slow job was running (expected ~20)\n", seen);
    CHECK(seen >= 12);
    f.sched.shutdown();
}

TEST(Scheduler, sched_misfire_fire_now_for_once) {
    Fixture f(opts(nullptr, 1s));
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::once("t", "j", Clock::now() - 10min, MisfirePolicy::FireNow)).has_value());
    CHECK(waitFor([&] { return count == 1; }));
    std::this_thread::sleep_for(100ms);
    CHECK(count == 1);
    CHECK(f.rec->with([](auto& r) { return r.misfires; }) == std::vector<std::string>{"t"});
    CHECK(!f.sched.getTrigger("t").has_value());
    f.sched.shutdown();
}

TEST(Scheduler, sched_misfire_skip_for_once) {
    Fixture f(opts(nullptr, 1s));
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::once("t", "j", Clock::now() - 10min, MisfirePolicy::SkipToNext)).has_value());
    CHECK(waitFor([&] { return f.rec->with([](auto& r) { return !r.completed.empty(); }); }));
    std::this_thread::sleep_for(100ms);
    CHECK(count == 0);
    CHECK(f.rec->with([](auto& r) { return r.misfires.size(); }) == 1);
    f.sched.shutdown();
}

TEST(Scheduler, sched_within_threshold_is_not_a_misfire) {
    Fixture f(opts(nullptr, 60s));
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::once("t", "j", Clock::now() - 5s)).has_value());
    CHECK(waitFor([&] { return count == 1; }));
    CHECK(f.rec->with([](auto& r) { return r.misfires.size(); }) == 0);
    f.sched.shutdown();
}

TEST(Scheduler, sched_misfire_catch_up_fires_every_missed_occurrence) {
    Fixture f(opts(nullptr, 1s));
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    auto t = Trigger::cron("t", "j", "0 * * * *", MisfirePolicy::CatchUp).value();
    t.nextFire = floorHour(Clock::now()) - 3h; // -3h, -2h, -1h, 0h are all in the past: 4 occurrences
    CHECK(f.sched.addTrigger(t).has_value());
    CHECK(waitFor([&] { return count >= 4; }));
    std::this_thread::sleep_for(150ms);
    CHECK(count == 4);
    const auto after = f.sched.getTrigger("t");
    CHECK(after.has_value());
    if (after) {
        CHECK(after->nextFire == floorHour(Clock::now()) + 1h);
    }
    f.rec->with([&](auto& r) {
        std::vector<TimePoint> times;
        for (const auto& c : r.started) {
            times.push_back(c.scheduledTime);
        }
        std::sort(times.begin(), times.end());
        CHECK(times.size() == 4);
        for (std::size_t i = 1; i < times.size(); ++i) {
            CHECK(times[i] - times[i - 1] == 1h);
        }
    });
    f.sched.shutdown();
}

TEST(Scheduler, sched_misfire_fire_now_for_cron_fires_once_and_realigns) {
    Fixture f(opts(nullptr, 1s));
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    auto t = Trigger::cron("t", "j", "0 * * * *", MisfirePolicy::FireNow).value();
    t.nextFire = floorHour(Clock::now()) - 3h;
    CHECK(f.sched.addTrigger(t).has_value());
    CHECK(waitFor([&] { return count >= 1; }));
    std::this_thread::sleep_for(150ms);
    CHECK(count == 1);
    CHECK(f.sched.getTrigger("t")->nextFire == floorHour(Clock::now()) + 1h);
    f.sched.shutdown();
}

TEST(Scheduler, sched_pause_resume_remove) {
    Fixture f;
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::interval("t", "j", 15ms).value()).has_value());
    CHECK(waitFor([&] { return count >= 3; }));
    CHECK(f.sched.pauseTrigger("t").has_value());
    std::this_thread::sleep_for(60ms); // let in-flight fires land
    const int frozen = count.load();
    std::this_thread::sleep_for(200ms);
    CHECK(count == frozen);
    CHECK(f.sched.getTrigger("t")->state == TriggerState::Paused);
    CHECK(f.sched.resumeTrigger("t").has_value());
    CHECK(waitFor([&] { return count > frozen + 2; }));
    CHECK(f.sched.removeTrigger("t").has_value());
    std::this_thread::sleep_for(60ms);
    const int afterRemove = count.load();
    std::this_thread::sleep_for(150ms);
    CHECK(count == afterRemove);
    f.sched.shutdown();
}

TEST(Scheduler, sched_remove_job_removes_its_triggers) {
    Fixture f;
    std::atomic<int> count{0};
    CHECK(f.sched.registerJob("count", [&](const JobContext&) { ++count; }).has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "count", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::interval("t1", "j", 1h).value()).has_value());
    CHECK(f.sched.addTrigger(Trigger::interval("t2", "j", 1h).value()).has_value());
    CHECK(f.sched.removeJob("j").has_value());
    CHECK(f.sched.triggers().empty());
    CHECK(f.sched.jobs().empty());
    f.sched.shutdown();
}

TEST(Scheduler, sched_job_exception_is_reported_and_scheduling_continues) {
    Fixture f;
    std::atomic<int> count{0};
    CHECK(f.sched
              .registerJob("boom",
                           [&](const JobContext&) {
                               ++count;
                               throw std::runtime_error("boom");
                           })
              .has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "boom", {}, false}).has_value());
    CHECK(f.sched.addTrigger(Trigger::interval("t", "j", 20ms, std::nullopt, 3).value()).has_value());
    CHECK(waitFor([&] { return count == 3; }));
    CHECK(waitFor([&] { return f.rec->with([](auto& r) { return r.finished.size() == 3; }); }));
    f.rec->with([](auto& r) {
        for (const auto& fin : r.finished) {
            CHECK(fin.error.has_value() && *fin.error == "boom");
        }
    });
    f.sched.shutdown();
}

TEST(Scheduler, sched_listener_may_reenter_scheduler_and_may_throw) {
    struct Reentrant final : ISchedulerListener {
        JobScheduler* s{};
        std::atomic<int> calls{0};
        void onJobFinished(const JobContext& c, const std::optional<std::string>&, std::chrono::microseconds) override {
            ++calls;
            (void)s->getJob(c.jobId);        // would deadlock if called under the scheduler lock
            (void)s->pauseTrigger("nope");
            throw std::runtime_error("listener failure must be swallowed");
        }
    };
    JobScheduler s;
    auto l = std::make_shared<Reentrant>();
    l->s = &s;
    s.addListener(l);
    std::atomic<int> count{0};
    CHECK(s.registerJob("c", [&](const JobContext&) { ++count; }).has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "c", {}, false}).has_value());
    CHECK(s.addTrigger(Trigger::interval("t", "j", 20ms, std::nullopt, 3).value()).has_value());
    CHECK(waitFor([&] { return count == 3 && l->calls == 3; }));
    s.shutdown();
}

TEST(Scheduler, sched_trigger_job_now_and_job_data) {
    Fixture f;
    std::mutex m;
    JobData seen;
    std::string trig = "unset";
    CHECK(f.sched
              .registerJob("echo",
                           [&](const JobContext& c) {
                               std::lock_guard l(m);
                               seen = c.data;
                               trig = c.triggerId;
                           })
              .has_value());
    CHECK(f.sched.start().has_value());
    CHECK(f.sched.addJob({"j", "echo", {{"url", "http://x/y?a=1&b=2"}, {"n", "42"}}, false}).has_value());
    CHECK(f.sched.triggerJobNow("j").has_value());
    CHECK(waitFor([&] {
        std::lock_guard l(m);
        return !seen.empty();
    }));
    std::lock_guard l(m);
    CHECK(seen.at("url") == "http://x/y?a=1&b=2");
    CHECK(seen.at("n") == "42");
    CHECK(trig.empty());
    f.sched.shutdown();
}

TEST(Scheduler, sched_shutdown_waits_for_running_jobs) {
    JobScheduler s;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    CHECK(s.registerJob("slow",
                        [&](const JobContext&) {
                            started = true;
                            std::this_thread::sleep_for(300ms);
                            finished = true;
                        })
              .has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "slow", {}, false}).has_value());
    CHECK(s.triggerJobNow("j").has_value());
    CHECK(waitFor([&] { return started.load(); }));
    s.shutdown(false);
    CHECK(finished.load()); // a running job is never abandoned
}

TEST(Scheduler, sched_unknown_handler_for_loaded_job_is_reported) {
    TempDir dir;
    auto store = SqliteJobStore::open(dir.file("s.db")).value();
    {
        JobScheduler s(opts(store));
        CHECK(s.registerJob("t", [](const JobContext&) {}).has_value());
        CHECK(s.start().has_value());
        CHECK(s.addJob({"j", "t", {}, false}).has_value());
        CHECK(s.addTrigger(Trigger::interval("tr", "j", 25ms).value()).has_value());
        s.shutdown();
    }
    Fixture f(opts(store));
    CHECK(f.sched.start().has_value()); // handler "t" is NOT registered in this process
    CHECK(waitFor([&] { return f.rec->with([](auto& r) { return !r.errors.empty(); }); }));
    f.sched.shutdown();
}
