#include "TestSupport.h"

#include <TK4CPP/Scheduler/SqliteJobStore.h>

#include <csignal>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

using namespace T;

namespace {

JobScheduler::Options opts(std::shared_ptr<IJobStore> store, Millis threshold = 60'000ms, std::size_t workers = 4) {
    JobScheduler::Options o;
    o.store = std::move(store);
    o.misfireThreshold = threshold;
    o.workerThreads = workers;
    return o;
}

/// Spawns the crash-child helper executable, waits until its job is mid-execution, then SIGKILLs it.
bool runAndKillChild(const std::filesystem::path& db, const std::filesystem::path& marker, const char* mode) {
    const pid_t pid = ::fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        ::execl(TK4CPP_SCHEDULER_CRASH_CHILD, TK4CPP_SCHEDULER_CRASH_CHILD, db.c_str(), marker.c_str(), mode,
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    const bool started = waitFor([&] { return std::filesystem::exists(marker); }, 5000ms);
    ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return started && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
}

} // namespace

TEST(Persistence, persist_state_survives_restart) {
    TempDir dir;
    TimePoint startA{};
    {
        auto store = SqliteJobStore::open(dir.file("s.db")).value();
        JobScheduler s(opts(store));
        CHECK(s.registerJob("t", [](const JobContext&) {}).has_value());
        CHECK(s.start().has_value());
        CHECK(s.addJob({"j", "t", {{"k", "v"}}, true}).has_value());
        auto iv = Trigger::interval("iv", "j", 1h, utc(2040, 1, 1, 0, 0, 0), 10).value();
        CHECK(s.addTrigger(iv).has_value());
        CHECK(s.addTrigger(Trigger::cron("cr", "j", "0 3 * * *").value()).has_value());
        CHECK(s.addTrigger(Trigger::once("on", "j", utc(2041, 1, 1))).has_value());
        CHECK(s.pauseTrigger("cr").has_value());
        startA = s.getTrigger("iv")->nextFire.value();
        s.shutdown();
    }
    auto store = SqliteJobStore::open(dir.file("s.db")).value();
    JobScheduler s(opts(store));
    CHECK(s.registerJob("t", [](const JobContext&) {}).has_value());
    CHECK(s.start().has_value());
    CHECK(s.jobs().size() == 1);
    CHECK(s.getJob("j")->data.at("k") == "v");
    CHECK(s.getJob("j")->requestsRecovery);
    CHECK(s.triggers().size() == 3);
    CHECK(s.getTrigger("iv")->nextFire == startA);
    CHECK(s.getTrigger("iv")->remaining == 10);
    CHECK(s.getTrigger("cr")->state == TriggerState::Paused);
    CHECK(s.getTrigger("on")->nextFire == utc(2041, 1, 1));
    s.shutdown();
}

TEST(Persistence, persist_downtime_fire_now_within_threshold) {
    TempDir dir;
    auto store = SqliteJobStore::open(dir.file("s.db")).value();
    {
        JobScheduler s(opts(store));
        CHECK(s.registerJob("t", [](const JobContext&) {}).has_value());
        CHECK(s.start().has_value());
        CHECK(s.addJob({"j", "t", {}, false}).has_value());
        CHECK(s.addTrigger(Trigger::once("on", "j", Clock::now() + 250ms)).has_value());
        s.shutdown(); // goes down before the trigger is due
    }
    std::this_thread::sleep_for(500ms); // "downtime"
    auto rec = std::make_shared<Recorder>();
    JobScheduler s(opts(store));
    s.addListener(rec);
    std::atomic<int> count{0};
    CHECK(s.registerJob("t", [&](const JobContext&) { ++count; }).has_value());
    CHECK(s.start().has_value());
    CHECK(waitFor([&] { return count == 1; }));
    std::this_thread::sleep_for(150ms);
    CHECK(count == 1);
    CHECK(rec->with([](auto& r) { return r.misfires.size(); }) == 0); // late by ~250ms, threshold 60s
    s.shutdown();
}

TEST(Persistence, persist_downtime_skip_beyond_threshold) {
    TempDir dir;
    auto store = SqliteJobStore::open(dir.file("s.db")).value();
    {
        JobScheduler s(opts(store));
        CHECK(s.registerJob("t", [](const JobContext&) {}).has_value());
        CHECK(s.start().has_value());
        CHECK(s.addJob({"j", "t", {}, false}).has_value());
        auto iv = Trigger::interval("iv", "j", 1h, Clock::now() + 100ms, -1, MisfirePolicy::SkipToNext).value();
        CHECK(s.addTrigger(iv).has_value());
        s.shutdown();
    }
    std::this_thread::sleep_for(500ms);
    auto rec = std::make_shared<Recorder>();
    JobScheduler s(opts(store, 100ms)); // anything later than 100 ms is a misfire
    s.addListener(rec);
    std::atomic<int> count{0};
    CHECK(s.registerJob("t", [&](const JobContext&) { ++count; }).has_value());
    CHECK(s.start().has_value());
    CHECK(waitFor([&] { return rec->with([](auto& r) { return !r.misfires.empty(); }); }));
    std::this_thread::sleep_for(100ms);
    CHECK(count == 0);
    const auto t = s.getTrigger("iv");
    CHECK(t.has_value());
    if (t) {
        CHECK(*t->nextFire > Clock::now() + 59min); // realigned to the next hourly slot
        const auto slot = std::get<IntervalSpec>(t->spec);
        CHECK((*t->nextFire - slot.start) % 1h == 0ms); // still anchored on the original grid
    }
    s.shutdown();
}

TEST(Persistence, persist_crash_kill9_recovers_in_flight_job_once) {
    TempDir dir;
    const auto db = dir.file("s.db");
    const auto marker = dir.file("marker");
    CHECK(runAndKillChild(db, marker, "recover"));

    // The child died inside the handler: the database must still be consistent and hold one in-flight record.
    {
        auto store = SqliteJobStore::open(db).value();
        auto snap = store->load();
        CHECK(snap.has_value());
        if (snap) {
            CHECK(snap->inflight.size() == 1);
            CHECK(snap->jobs.size() == 1);
            CHECK(snap->triggers.empty()); // the one-shot trigger completed and was removed before the crash
        }
    }

    auto store = SqliteJobStore::open(db).value();
    auto rec = std::make_shared<Recorder>();
    JobScheduler s(opts(store));
    s.addListener(rec);
    std::mutex m;
    std::vector<JobContext> runs;
    CHECK(s.registerJob("hang",
                        [&](const JobContext& c) {
                            std::lock_guard l(m);
                            runs.push_back(c);
                        })
              .has_value());
    CHECK(s.start().has_value());
    CHECK(waitFor([&] {
        std::lock_guard l(m);
        return !runs.empty();
    }));
    std::this_thread::sleep_for(300ms);
    {
        std::lock_guard l(m);
        CHECK(runs.size() == 1); // re-run exactly once, and the one-shot trigger did not fire again
        if (!runs.empty()) {
            CHECK(runs[0].recovering);
            CHECK(runs[0].triggerId == "once");
            CHECK(runs[0].jobId == "j");
        }
    }
    // after a clean run the in-flight record is gone
    s.shutdown();
    auto snap = store->load();
    CHECK(snap.has_value() && snap->inflight.empty());

    // and a third start must not run it again
    JobScheduler s2(opts(store));
    std::atomic<int> again{0};
    CHECK(s2.registerJob("hang", [&](const JobContext&) { ++again; }).has_value());
    CHECK(s2.start().has_value());
    std::this_thread::sleep_for(300ms);
    CHECK(again == 0);
    s2.shutdown();
}

TEST(Persistence, persist_crash_kill9_without_recovery_does_not_rerun) {
    TempDir dir;
    const auto db = dir.file("s.db");
    const auto marker = dir.file("marker");
    CHECK(runAndKillChild(db, marker, "norecover"));
    auto store = SqliteJobStore::open(db).value();
    auto snap = store->load();
    CHECK(snap.has_value() && snap->inflight.empty());

    JobScheduler s(opts(store));
    std::atomic<int> runs{0};
    CHECK(s.registerJob("hang", [&](const JobContext&) { ++runs; }).has_value());
    CHECK(s.start().has_value());
    std::this_thread::sleep_for(400ms);
    CHECK(runs == 0); // at most once: the interrupted occurrence is lost by design
    s.shutdown();
}

TEST(Persistence, persist_shutdown_without_drain_keeps_recoverable_work) {
    TempDir dir;
    auto store = SqliteJobStore::open(dir.file("s.db")).value();
    std::atomic<int> firstRuns{0};
    {
        JobScheduler s(opts(store, 60'000ms, 1)); // a single worker: the second execution must queue
        CHECK(s.registerJob("w",
                            [&](const JobContext&) {
                                ++firstRuns;
                                std::this_thread::sleep_for(300ms);
                            })
                  .has_value());
        CHECK(s.start().has_value());
        CHECK(s.addJob({"j", "w", {}, true}).has_value());
        CHECK(s.triggerJobNow("j").has_value());
        CHECK(waitFor([&] { return firstRuns == 1; }));
        CHECK(s.triggerJobNow("j").has_value()); // queued behind the running one
        s.shutdown(false);                       // discards the queued execution
    }
    CHECK(firstRuns == 1);

    JobScheduler s(opts(store));
    std::mutex m;
    std::vector<bool> recovering;
    CHECK(s.registerJob("w",
                        [&](const JobContext& c) {
                            std::lock_guard l(m);
                            recovering.push_back(c.recovering);
                        })
              .has_value());
    CHECK(s.start().has_value());
    CHECK(waitFor([&] {
        std::lock_guard l(m);
        return !recovering.empty();
    }));
    std::this_thread::sleep_for(200ms);
    std::lock_guard l(m);
    CHECK(recovering.size() == 1);
    CHECK(!recovering.empty() && recovering[0]);
    s.shutdown();
}
