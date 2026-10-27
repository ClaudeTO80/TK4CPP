#include "TestSupport.h"

#include <TK4CPP/Scheduler/SqliteJobStore.h>
#include <TK4CPP/Scheduler/ThreadPoolAdapter.h>

#include <future>
#include <set>

namespace TP = TK4CPP::TP;
using namespace T;

namespace {

std::shared_ptr<TP::ThreadPool> makePool(std::size_t minThreads, std::size_t maxThreads, std::size_t queue = 0,
                                         bool start = true) {
    TP::ThreadPoolConfig c;
    c.minThreads = minThreads;
    c.maxThreads = maxThreads;
    c.maxQueueSize = queue;
    c.name = "tp-test";
    auto pool = std::make_shared<TP::ThreadPool>(c);
    if (start) {
        pool->start();
    }
    return pool;
}

JobScheduler::Options withExecutor(std::shared_ptr<IExecutor> executor, std::shared_ptr<IJobStore> store = nullptr) {
    JobScheduler::Options o;
    o.executor = std::move(executor);
    o.store = std::move(store);
    return o;
}

/// Runs @p fn on its own thread and reports whether it finished within @p limit (never blocks forever).
bool finishesWithin(std::chrono::milliseconds limit, std::function<void()> fn) {
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    std::thread([done, fn = std::move(fn)] {
        fn();
        done->set_value();
    }).detach();
    return future.wait_for(limit) == std::future_status::ready;
}

} // namespace

TEST(ThreadPoolAdapter, tp_jobs_run_on_pool_workers_as_named_tasks) {
    auto pool = makePool(2, 4);
    ThreadPoolAdapter::Options o;
    o.priority = TP::Priority::High;
    o.labelPrefix = "job/";
    auto adapter = std::make_shared<ThreadPoolAdapter>(pool, o);

    std::mutex m;
    std::set<std::string> names;
    std::set<TP::Priority> priorities;
    const auto conn = pool->events().on<TP::TaskSubmittedEvent>([&](const TP::TaskSubmittedEvent& e) {
        std::lock_guard l(m);
        names.insert(e.task->name());
        priorities.insert(e.task->priority());
    });

    JobScheduler s(withExecutor(adapter));
    std::atomic<int> count{0};
    std::atomic<int> onWorker{0};
    CHECK(s.registerJob("t",
                        [&](const JobContext&) {
                            ++count;
                            if (pool->onWorkerThread()) {
                                ++onWorker;
                            }
                        })
              .has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"report", "t", {}, false}).has_value());
    CHECK(s.addTrigger(Trigger::interval("i", "report", 20ms, std::nullopt, 5).value()).has_value());
    CHECK(waitFor([&] { return count == 5; }));
    CHECK(waitFor([&] { return adapter->outstanding() == 0; }));
    s.shutdown();

    CHECK(onWorker == 5); // every execution ran on a thread of the TK4CPP pool
    {
        std::lock_guard l(m);
        CHECK(names == std::set<std::string>{"job/report"});
        CHECK(priorities == std::set<TP::Priority>{TP::Priority::High});
    }
    CHECK(pool->stats().totalCompleted == 5);
    CHECK(pool->stats().totalFailed == 0);
    CHECK(adapter->rejected() == 0);
}

TEST(ThreadPoolAdapter, tp_same_job_runs_in_parallel_on_the_pool) {
    auto pool = makePool(4, 4);
    JobScheduler s(withExecutor(std::make_shared<ThreadPoolAdapter>(pool)));
    std::atomic<int> current{0};
    std::atomic<int> maxSeen{0};
    std::atomic<int> done{0};
    CHECK(s.registerJob("slow",
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
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "slow", {}, false}).has_value());
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(s.addTrigger(Trigger::interval("t", "j", 40ms, std::nullopt, 6).value()).has_value());
    CHECK(waitFor([&] { return done == 6; }, 5000ms));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    std::printf("  6 x 200ms job every 40ms on a 4-thread TK4CPP pool: max concurrency %d, wall time %lld ms\n",
                maxSeen.load(), static_cast<long long>(elapsed.count()));
    CHECK(maxSeen >= 3);
    CHECK(maxSeen <= 4);
    CHECK(elapsed < 900ms);
    s.shutdown();
}

TEST(ThreadPoolAdapter, tp_borrowed_pool_stays_usable_after_scheduler_shutdown) {
    auto pool = makePool(2, 2);
    {
        JobScheduler s(withExecutor(std::make_shared<ThreadPoolAdapter>(pool)));
        std::atomic<int> count{0};
        CHECK(s.registerJob("t", [&](const JobContext&) { ++count; }).has_value());
        CHECK(s.start().has_value());
        CHECK(s.addJob({"j", "t", {}, false}).has_value());
        CHECK(s.triggerJobNow("j").has_value());
        CHECK(waitFor([&] { return count == 1; }));
        s.shutdown();
    }
    CHECK(pool->isRunning());
    auto fut = pool->submitWithFuture([] { return 42; });
    CHECK(fut.has_value());
    if (fut) {
        CHECK(fut->get() == 42);
    }
    pool->shutdown();
}

TEST(ThreadPoolAdapter, tp_owned_pool_is_stopped_by_scheduler_shutdown) {
    TP::ThreadPoolConfig c;
    c.minThreads = 2;
    c.maxThreads = 2;
    auto adapter = ThreadPoolAdapter::createOwning(c);
    CHECK(adapter->pool()->isRunning()); // started by createOwning
    JobScheduler s(withExecutor(adapter));
    std::atomic<int> count{0};
    CHECK(s.registerJob("t", [&](const JobContext&) { ++count; }).has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "t", {}, false}).has_value());
    CHECK(s.triggerJobNow("j").has_value());
    CHECK(waitFor([&] { return count == 1; }));
    s.shutdown();
    CHECK(!adapter->pool()->isRunning());
    CHECK(adapter->outstanding() == 0);
}

TEST(ThreadPoolAdapter, tp_shutdown_waits_only_for_its_own_tasks) {
    auto pool = makePool(2, 2);
    std::atomic<bool> foreignDone{false};
    CHECK(pool->submit([&] {
                      std::this_thread::sleep_for(700ms);
                      foreignDone = true;
                  },
                      "foreign")
              .has_value());

    JobScheduler s(withExecutor(std::make_shared<ThreadPoolAdapter>(pool)));
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    CHECK(s.registerJob("t",
                        [&](const JobContext&) {
                            started = true;
                            std::this_thread::sleep_for(150ms);
                            finished = true;
                        })
              .has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "t", {}, false}).has_value());
    CHECK(s.triggerJobNow("j").has_value());
    CHECK(waitFor([&] { return started.load(); }));

    const auto t0 = std::chrono::steady_clock::now();
    s.shutdown(true);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    CHECK(finished.load());      // the scheduler's own job was waited for
    CHECK(!foreignDone.load());  // ...but not somebody else's task sharing the pool
    CHECK(elapsed < 500ms);
    std::printf("  shutdown(true) returned in %lld ms while a foreign 700 ms task was still running\n",
                static_cast<long long>(elapsed.count()));
    pool->shutdown();
}

TEST(ThreadPoolAdapter, tp_shutdown_false_cancels_queued_executions) {
    auto pool = makePool(1, 1); // one worker: further executions must queue
    auto adapter = std::make_shared<ThreadPoolAdapter>(pool);
    JobScheduler s(withExecutor(adapter));
    std::atomic<int> started{0};
    CHECK(s.registerJob("t",
                        [&](const JobContext&) {
                            ++started;
                            std::this_thread::sleep_for(300ms);
                        })
              .has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "t", {}, false}).has_value());
    CHECK(s.triggerJobNow("j").has_value());
    CHECK(waitFor([&] { return started == 1; }));
    CHECK(s.triggerJobNow("j").has_value());
    CHECK(s.triggerJobNow("j").has_value());
    CHECK(adapter->outstanding() == 3);

    s.shutdown(false);
    CHECK(started == 1);
    CHECK(adapter->outstanding() == 0);
    // The pool is still alive and its worker is free now: cancelled tasks must never run.
    std::this_thread::sleep_for(500ms);
    CHECK(started == 1);
    CHECK(pool->isRunning());
    pool->shutdown();
}

TEST(ThreadPoolAdapter, tp_shutdown_false_does_not_wait_for_foreign_tasks_ahead_in_the_queue) {
    auto pool = makePool(1, 1); // the only worker is busy with somebody else's task
    std::atomic<bool> foreignDone{false};
    CHECK(pool->submit([&] {
                      std::this_thread::sleep_for(800ms);
                      foreignDone = true;
                  },
                      "foreign")
              .has_value());
    auto adapter = std::make_shared<ThreadPoolAdapter>(pool);
    JobScheduler s(withExecutor(adapter));
    std::atomic<int> started{0};
    CHECK(s.registerJob("t", [&](const JobContext&) { ++started; }).has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "t", {}, false}).has_value());
    CHECK(s.triggerJobNow("j").has_value());
    CHECK(s.triggerJobNow("j").has_value());
    CHECK(adapter->outstanding() == 2); // both queued behind the foreign task

    const auto t0 = std::chrono::steady_clock::now();
    s.shutdown(false);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    std::printf("  shutdown(false) returned in %lld ms with the pool's only worker busy for 800 ms\n",
                static_cast<long long>(elapsed.count()));
    CHECK(elapsed < 300ms); // our cancelled tasks are not waited for until the worker frees up
    CHECK(!foreignDone.load());
    CHECK(adapter->outstanding() == 0);
    std::this_thread::sleep_for(900ms);
    CHECK(started == 0); // and they never ran
    pool->shutdown();
}

TEST(ThreadPoolAdapter, tp_stopped_pool_reports_rejection_and_scheduler_recovers) {
    auto pool = makePool(2, 2, 0, /*start=*/false);
    auto adapter = std::make_shared<ThreadPoolAdapter>(pool);
    auto rec = std::make_shared<Recorder>();
    JobScheduler s(withExecutor(adapter));
    s.addListener(rec);
    std::atomic<int> count{0};
    CHECK(s.registerJob("t", [&](const JobContext&) { ++count; }).has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "t", {}, false}).has_value());

    const auto manual = s.triggerJobNow("j");
    CHECK(!manual.has_value());
    if (!manual) {
        CHECK(manual.error().code == ErrorCode::ExecutorRejected);
    }
    CHECK(s.addTrigger(Trigger::interval("i", "j", 20ms).value()).has_value());
    CHECK(waitFor([&] { return adapter->rejected() >= 3; }));
    CHECK(count == 0);
    CHECK(rec->with([](auto& r) { return !r.errors.empty(); }));
    rec->with([](auto& r) {
        CHECK(!r.errors.empty());
        if (!r.errors.empty()) {
            CHECK(r.errors.front().find("executor rejected") != std::string::npos);
            CHECK(r.errors.front().find("'j'") != std::string::npos);
        }
    });

    pool->start(); // the application starts the pool late: scheduling resumes by itself
    CHECK(waitFor([&] { return count >= 3; }));
    s.shutdown();
    pool->shutdown();
}

TEST(ThreadPoolAdapter, tp_full_queue_rejects_without_blocking_the_caller) {
    auto pool = makePool(1, 1, /*queue=*/1);
    auto adapter = std::make_shared<ThreadPoolAdapter>(pool);
    JobScheduler s(withExecutor(adapter));
    std::atomic<int> started{0};
    CHECK(s.registerJob("t",
                        [&](const JobContext&) {
                            ++started;
                            std::this_thread::sleep_for(400ms);
                        })
              .has_value());
    CHECK(s.start().has_value());
    CHECK(s.addJob({"j", "t", {}, false}).has_value());

    CHECK(s.triggerJobNow("j").has_value()); // runs
    CHECK(waitFor([&] { return started == 1; }));
    CHECK(s.triggerJobNow("j").has_value()); // fills the single queue slot
    const auto t0 = std::chrono::steady_clock::now();
    const auto third = s.triggerJobNow("j");
    const auto fourth = s.triggerJobNow("j");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    CHECK(!third.has_value() && !fourth.has_value());
    if (!third) {
        CHECK(third.error().code == ErrorCode::ExecutorRejected);
    }
    CHECK(adapter->rejected() == 2);
    CHECK(pool->stats().totalRejected == 2);
    CHECK(elapsed < 100ms); // Reject policy: the caller is not blocked
    s.shutdown(true);
    CHECK(started == 2); // the running one and the queued one; the rejected ones never ran
    pool->shutdown();
}

TEST(ThreadPoolAdapter, tp_rejected_recoverable_fire_is_retried_after_restart) {
    TempDir dir;
    auto store = SqliteJobStore::open(dir.file("s.db")).value();
    auto rec = std::make_shared<Recorder>();
    {
        auto pool = makePool(2, 2, 0, /*start=*/false); // never started: every submission is refused
        JobScheduler s(withExecutor(std::make_shared<ThreadPoolAdapter>(pool), store));
        s.addListener(rec);
        CHECK(s.registerJob("t", [](const JobContext&) {}).has_value());
        CHECK(s.start().has_value());
        CHECK(s.addJob({"j", "t", {}, /*requestsRecovery=*/true}).has_value());
        CHECK(s.addTrigger(Trigger::once("once", "j", Clock::now() + 50ms)).has_value());
        CHECK(waitFor([&] { return rec->with([](auto& r) { return !r.errors.empty(); }); }));
        rec->with([](auto& r) {
            CHECK(!r.errors.empty());
            if (!r.errors.empty()) {
                CHECK(r.errors.front().find("retried at the next start") != std::string::npos);
            }
        });
        // A manual fire is different: the caller is told it failed, so it must not come back later.
        CHECK(!s.triggerJobNow("j").has_value());
        s.shutdown();
    }
    {
        auto snapshot = store->load();
        CHECK(snapshot.has_value() && snapshot->inflight.size() == 1); // only the trigger-originated one
    }

    auto pool = makePool(2, 2);
    JobScheduler s(withExecutor(std::make_shared<ThreadPoolAdapter>(pool), store));
    std::mutex m;
    std::vector<JobContext> runs;
    CHECK(s.registerJob("t",
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
        CHECK(runs.size() == 1);
        if (!runs.empty()) {
            CHECK(runs[0].recovering);
            CHECK(runs[0].triggerId == "once");
        }
    }
    s.shutdown();
    pool->shutdown();
}

TEST(ThreadPoolAdapter, tp_shutdown_does_not_hang_when_the_pool_discards_tasks_first) {
    auto pool = makePool(1, 1);
    auto adapter = std::make_shared<ThreadPoolAdapter>(pool);
    auto sched = std::make_shared<JobScheduler>(withExecutor(adapter));
    std::atomic<int> started{0};
    CHECK(sched->registerJob("t",
                             [&](const JobContext&) {
                                 ++started;
                                 std::this_thread::sleep_for(300ms);
                             })
              .has_value());
    CHECK(sched->start().has_value());
    CHECK(sched->addJob({"j", "t", {}, false}).has_value());
    CHECK(sched->triggerJobNow("j").has_value());
    CHECK(waitFor([&] { return started == 1; }));
    CHECK(sched->triggerJobNow("j").has_value());
    CHECK(sched->triggerJobNow("j").has_value());

    // The application cancels the queued tasks behind the adapter's back.
    std::thread external([&] { pool->shutdownNow(); });
    const bool finished = finishesWithin(5000ms, [&] { sched->shutdown(true); });
    CHECK(finished); // without the sweep of cancelled tasks this waited forever
    external.join();
    CHECK(started == 1);
    if (!finished) {
        std::fprintf(stderr, "  scheduler shutdown hung; leaking it on purpose\n");
        (void)new std::shared_ptr<JobScheduler>(sched); // keep it alive for the detached thread
    }
}

TEST(ThreadPoolAdapter, tp_adapter_contract_edges) {
    try {
        ThreadPoolAdapter bad(nullptr);
        CHECK(false);
    } catch (const std::invalid_argument&) {
        CHECK(true);
    }

    auto pool = makePool(1, 1, 0, /*start=*/false);
    ThreadPoolAdapter::Options o;
    o.startPoolIfNeeded = true;
    ThreadPoolAdapter adapter(pool, o);
    CHECK(pool->isRunning()); // started because of startPoolIfNeeded

    std::atomic<int> ran{0};
    CHECK(adapter.submit([&] { ++ran; }, "x"));
    CHECK(waitFor([&] { return ran == 1; }));
    CHECK(waitFor([&] { return adapter.outstanding() == 0; }));

    adapter.shutdown(true);
    CHECK(!adapter.submit([&] { ++ran; }, "late")); // closed
    CHECK(adapter.rejected() == 1);
    CHECK(pool->isRunning()); // borrowed: untouched
    adapter.shutdown(true);   // idempotent
    pool->shutdown();
    CHECK(ran == 1);
}
