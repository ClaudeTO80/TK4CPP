/**
 * @file test_threadpool.cpp
 * @brief Self-contained test suite (no external framework). Exit code 0 = all passed.
 */
#include "TK4CPP/ThreadPool/Observers.h"
#include "TK4CPP/ThreadPool/PoolLogger.h"
#include "TK4CPP/ThreadPool/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <future>
#include <iostream>
#include <latch>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace TK4CPP::TP;
using namespace std::chrono_literals;

namespace {

std::atomic<int> g_failures{ 0 };
std::atomic<int> g_checks{ 0 };

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            ++g_failures;                                                                 \
            std::fprintf(stderr, "    CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                                 \
    } while (0)

struct TestCase { const char* name; std::function<void()> fn; };
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({ n, std::move(f) }); } };
#define TEST(name) \
    static void name(); \
    static Registrar reg_##name(#name, name); \
    static void name()

ThreadPoolConfig cfg(std::size_t minT, std::size_t maxT, std::size_t queue = 0) {
    ThreadPoolConfig c;
    c.minThreads = minT;
    c.maxThreads = maxT;
    c.maxQueueSize = queue;
    c.name = "test";
    return c;
}

/** Error of a SubmitResult, or a sentinel when it succeeded. */
SubmitError errorOf(const SubmitResult& r) {
    return r ? static_cast<SubmitError>(255) : r.error();
}

/** Polls @p pred for up to @p timeout. */
template<class P>
bool eventually(P pred, std::chrono::milliseconds timeout = 3000ms) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return pred();
}

// ─────────────────────────────────────────────────────────────────────────────

TEST(runs_all_tasks_and_counts) {
    ThreadPool pool(cfg(2, 4));
    pool.start();
    std::atomic<int> n{ 0 };
    for (int i = 0; i < 1000; ++i)
        CHECK(pool.submit([&] { ++n; }));
    pool.waitForAll();
    CHECK(n == 1000);
    CHECK(pool.stats().totalCompleted == 1000);
    CHECK(pool.stats().queuedTasks == 0);
    pool.shutdown();
}

TEST(submit_requires_running_pool) {
    ThreadPool pool(cfg(1, 1));
    CHECK(errorOf(pool.submit([] {})) == SubmitError::NotRunning);
    pool.start();
    CHECK(pool.submit([] {}));
    pool.shutdown();
    CHECK(errorOf(pool.submit([] {})) == SubmitError::NotRunning);
    CHECK(errorOf(pool.submit(TaskPtr{})) == SubmitError::InvalidTask);
    pool.start();
    CHECK(errorOf(pool.submit(TaskPtr{})) == SubmitError::InvalidTask);
}

TEST(restart_after_shutdown) {
    ThreadPool pool(cfg(2, 2));
    std::atomic<int> n{ 0 };
    for (int round = 0; round < 3; ++round) {
        pool.start();
        for (int i = 0; i < 50; ++i) CHECK(pool.submit([&] { ++n; }));
        pool.shutdown();
    }
    CHECK(n == 150);
}

TEST(priority_then_fifo_order) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    std::latch started(1);
    std::promise<void> gate;
    auto gateFuture = gate.get_future().share();
    CHECK(pool.submit([&] { started.count_down(); gateFuture.wait(); }, "gate"));
    started.wait();  // the single worker is now blocked; everything below queues up

    std::mutex m;
    std::vector<std::string> order;
    auto add = [&](std::string label, Priority p) {
        CHECK(pool.submit([&, label] { std::lock_guard lk(m); order.push_back(label); }, label, p));
    };
    add("low1", Priority::Low);
    add("norm1", Priority::Normal);
    add("high1", Priority::High);
    add("norm2", Priority::Normal);
    add("high2", Priority::High);
    add("low2", Priority::Low);
    gate.set_value();
    pool.waitForAll();

    const std::vector<std::string> expected{ "high1", "high2", "norm1", "norm2", "low1", "low2" };
    CHECK(order == expected);
    pool.shutdown();
}

TEST(future_value_and_exception) {
    ThreadPool pool(cfg(2, 2));
    pool.start();
    auto ok = pool.submitWithFuture([] { return 6 * 7; });
    CHECK(ok.has_value());
    CHECK(ok->get() == 42);

    auto bad = pool.submitWithFuture([]() -> int { throw std::runtime_error("boom"); });
    CHECK(bad.has_value());
    bool threw = false;
    try { bad->get(); } catch (const std::runtime_error& e) { threw = std::string(e.what()) == "boom"; }
    CHECK(threw);

    auto v = pool.submitWithFuture([] {});
    CHECK(v.has_value());
    v->get();

    std::function<int()> lvalue = [] { return 5; };
    auto lv = pool.submitWithFuture(lvalue);
    CHECK(lv.has_value() && lv->get() == 5);

    auto moveOnly = pool.submitWithFuture([p = std::make_unique<int>(9)] { return *p; });
    CHECK(moveOnly.has_value() && moveOnly->get() == 9);
    pool.shutdown();
}

TEST(bounded_queue_rejects_and_reports) {
    ThreadPoolConfig c = cfg(1, 1, 2);
    ThreadPool pool(c);
    pool.start();
    std::atomic<int> fullEvents{ 0 }, rejectedEvents{ 0 };
    auto c1 = pool.events().on<PoolQueueFullEvent>([&](const PoolQueueFullEvent&) { ++fullEvents; });
    auto c2 = pool.events().on<TaskRejectedEvent>([&](const TaskRejectedEvent& e) {
        if (e.reason == SubmitError::QueueFull) ++rejectedEvents;
    });

    std::latch started(1);
    std::promise<void> gate;
    auto gf = gate.get_future().share();
    CHECK(pool.submit([&] { started.count_down(); gf.wait(); }));
    started.wait();
    CHECK(pool.submit([] {}));
    CHECK(pool.submit([] {}));
    CHECK(errorOf(pool.submit([] {})) == SubmitError::QueueFull);
    CHECK(fullEvents == 1);
    CHECK(rejectedEvents == 1);
    CHECK(pool.stats().totalRejected == 1);
    gate.set_value();
    pool.waitForAll();
    pool.shutdown();
}

TEST(block_policy_waits_for_room) {
    ThreadPoolConfig c = cfg(1, 1, 1);
    c.overflowPolicy = OverflowPolicy::Block;
    ThreadPool pool(c);
    pool.start();
    std::atomic<int> n{ 0 };
    std::latch started(1);
    std::promise<void> gate;
    auto gf = gate.get_future().share();
    CHECK(pool.submit([&] { started.count_down(); gf.wait(); ++n; }));
    started.wait();
    CHECK(pool.submit([&] { ++n; }));  // fills the single queue slot

    std::atomic<bool> returned{ false };
    std::thread producer([&] {
        CHECK(pool.submit([&] { ++n; }));  // must block until room is available
        returned = true;
    });
    std::this_thread::sleep_for(100ms);
    CHECK(!returned);
    gate.set_value();
    producer.join();
    CHECK(returned);
    pool.waitForAll();
    CHECK(n == 3);
    pool.shutdown();
}

TEST(block_policy_unblocks_on_shutdown) {
    ThreadPoolConfig c = cfg(1, 1, 1);
    c.overflowPolicy = OverflowPolicy::Block;
    ThreadPool pool(c);
    pool.start();
    std::latch started(1);
    std::promise<void> gate;
    auto gf = gate.get_future().share();
    CHECK(pool.submit([&] { started.count_down(); gf.wait(); }));
    started.wait();
    CHECK(pool.submit([] {}));
    std::atomic<int> code{ -1 };
    std::thread producer([&] {
        code = static_cast<int>(errorOf(pool.submit([] {})));
    });
    std::this_thread::sleep_for(50ms);
    std::thread stopper([&] { pool.shutdown(false); });
    CHECK(eventually([&] { return code.load() != -1; }));
    gate.set_value();
    stopper.join();
    producer.join();
    CHECK(code == static_cast<int>(SubmitError::NotRunning));
}

TEST(cancel_pending_task) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    std::latch started(1);
    std::promise<void> gate;
    auto gf = gate.get_future().share();
    CHECK(pool.submit([&] { started.count_down(); gf.wait(); }));
    started.wait();

    std::atomic<bool> ran{ false };
    std::atomic<int> cancelledEvents{ 0 };
    auto conn = pool.events().on<TaskCancelledEvent>([&](const TaskCancelledEvent&) { ++cancelledEvents; });
    auto t = std::make_shared<FunctionTask>([&] { ran = true; }, "victim");
    CHECK(pool.submit(t));
    CHECK(t->status() == TaskStatus::Queued);
    CHECK(t->cancel());
    CHECK(t->isCancelled());
    gate.set_value();
    pool.waitForAll();
    CHECK(!ran);
    CHECK(cancelledEvents == 1);
    CHECK(pool.stats().totalCancelled == 1);
    CHECK(errorOf(pool.submit(t)) == SubmitError::InvalidTask);  // cancelled tasks cannot be reused
    pool.shutdown();
}

TEST(double_submit_is_rejected) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    std::latch started(1);
    std::promise<void> gate;
    auto gf = gate.get_future().share();
    CHECK(pool.submit([&] { started.count_down(); gf.wait(); }));
    started.wait();
    auto t = std::make_shared<FunctionTask>([] {}, "once");
    CHECK(pool.submit(t));
    CHECK(errorOf(pool.submit(t)) == SubmitError::InvalidTask);
    gate.set_value();
    pool.waitForAll();
    CHECK(t->status() == TaskStatus::Completed);
    pool.shutdown();
}

TEST(shutdown_now_discards_queue_and_stops_running) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    std::atomic<int> cancelled{ 0 };
    std::atomic<bool> sawStop{ false };
    std::atomic<int> executed{ 0 };
    auto conn = pool.events().on<TaskCancelledEvent>([&](const TaskCancelledEvent&) { ++cancelled; });

    struct Spinner : ITask {
        std::atomic<bool>& saw;
        std::latch& go;
        Spinner(std::atomic<bool>& s, std::latch& g) : ITask("spinner"), saw(s), go(g) {}
        void execute() override {
            go.count_down();
            while (!stopRequested()) std::this_thread::sleep_for(1ms);
            saw = true;
        }
    };
    std::latch go(1);
    CHECK(pool.submit(std::make_shared<Spinner>(sawStop, go)));
    go.wait();
    for (int i = 0; i < 10; ++i) CHECK(pool.submit([&] { ++executed; }));

    PoolShutdownEvent last(0, 0, 0, true, 0);
    auto c2 = pool.events().on<PoolShutdownEvent>([&](const PoolShutdownEvent& e) { last = e; });
    pool.shutdownNow();
    CHECK(sawStop);
    CHECK(executed == 0);
    CHECK(cancelled == 10);
    CHECK(!last.graceful);
    CHECK(last.pendingTasks == 10);
    CHECK(pool.stats().totalCancelled == 10);
    pool.waitForAll();  // must not hang: every accepted task is accounted for
}

TEST(graceful_shutdown_runs_everything) {
    ThreadPool pool(cfg(2, 2));
    pool.start();
    std::atomic<int> n{ 0 };
    for (int i = 0; i < 200; ++i) CHECK(pool.submit([&] { std::this_thread::sleep_for(100us); ++n; }));
    pool.shutdown(true);
    CHECK(n == 200);
}

TEST(destructor_is_graceful) {
    std::atomic<int> n{ 0 };
    {
        ThreadPool pool(cfg(2, 2));
        pool.start();
        for (int i = 0; i < 100; ++i) CHECK(pool.submit([&] { ++n; }));
    }
    CHECK(n == 100);
}

TEST(scale_up_to_max_under_load) {
    ThreadPool pool(cfg(1, 4));
    pool.start();
    CHECK(pool.workerCount() == 1);
    std::atomic<int> concurrent{ 0 }, peak{ 0 };
    std::latch allIn(4);
    for (int i = 0; i < 8; ++i)
        CHECK(pool.submit([&] {
            int c = ++concurrent;
            int p = peak.load();
            while (c > p && !peak.compare_exchange_weak(p, c)) {}
            allIn.count_down();
            std::this_thread::sleep_for(50ms);
            --concurrent;
        }));
    pool.waitForAll();
    CHECK(pool.workerCount() == 4);
    CHECK(peak == 4);
    pool.shutdown();
}

TEST(never_exceeds_max_threads) {
    ThreadPool pool(cfg(2, 3));
    pool.start();
    std::atomic<int> concurrent{ 0 }, peak{ 0 };
    for (int i = 0; i < 60; ++i)
        CHECK(pool.submit([&] {
            int c = ++concurrent;
            int p = peak.load();
            while (c > p && !peak.compare_exchange_weak(p, c)) {}
            std::this_thread::sleep_for(2ms);
            --concurrent;
        }));
    pool.waitForAll();
    CHECK(peak <= 3);
    CHECK(pool.workerCount() <= 3);
    pool.shutdown();
}

TEST(scale_down_after_idle_timeout) {
    ThreadPoolConfig c = cfg(1, 4);
    c.idleTimeout = 1s;  // seconds granularity; tests below use a shorter real wait via polling
    ThreadPool pool(c);
    pool.start();
    std::atomic<int> scaledDown{ 0 };
    auto conn = pool.events().on<PoolScaledDownEvent>([&](const PoolScaledDownEvent& e) { scaledDown += (int)e.removedWorkers; });
    for (int i = 0; i < 8; ++i) CHECK(pool.submit([] { std::this_thread::sleep_for(30ms); }));
    pool.waitForAll();
    CHECK(pool.workerCount() > 1);
    CHECK(eventually([&] { return pool.workerCount() == 1; }, 5000ms));  // retires down to minThreads, never below
    CHECK(scaledDown >= 1);
    std::this_thread::sleep_for(1500ms);
    CHECK(pool.workerCount() == 1);
    // The pool still works afterwards.
    auto f = pool.submitWithFuture([] { return 1; });
    CHECK(f && f->get() == 1);
    pool.shutdown();
}

TEST(zero_min_threads_no_lost_wakeup) {
    // Stress the retire/submit handshake: min=0, workers retire after 1s idle while tasks keep arriving.
    ThreadPoolConfig c = cfg(0, 2);
    c.idleTimeout = 1s;
    ThreadPool pool(c);
    pool.start();
    CHECK(pool.workerCount() == 0);
    std::atomic<int> n{ 0 };
    for (int round = 0; round < 4; ++round) {
        for (int i = 0; i < 20; ++i) CHECK(pool.submit([&] { ++n; }));
        CHECK(pool.waitForAll(5000ms));
        std::this_thread::sleep_for(round == 1 ? 1050ms : 10ms);  // one round straddles the idle timeout
    }
    CHECK(n == 80);
    pool.shutdown();
}

TEST(event_order_for_a_task) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    std::mutex m;
    std::vector<std::string> seq;
    auto push = [&](const char* s) { std::lock_guard lk(m); seq.push_back(s); };
    auto c1 = pool.events().on<TaskSubmittedEvent>([&](const TaskSubmittedEvent&) { push("submitted"); });
    auto c2 = pool.events().on<TaskStartedEvent>([&](const TaskStartedEvent&) { push("started"); });
    auto c3 = pool.events().on<TaskCompletedEvent>([&](const TaskCompletedEvent& e) {
        push("completed");
        CHECK(e.elapsed >= 5ms);
        CHECK(e.task && e.task->name() == "ordered");
    });
    CHECK(pool.submit([] { std::this_thread::sleep_for(5ms); }, "ordered"));
    pool.waitForAll();
    const std::vector<std::string> expected{ "submitted", "started", "completed" };
    CHECK(seq == expected);
    pool.shutdown();
}

TEST(failed_task_event_and_hook) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    std::string msg;
    std::atomic<int> failed{ 0 };
    auto c = pool.events().on<TaskFailedEvent>([&](const TaskFailedEvent& e) {
        msg = e.errorMessage;
        CHECK(e.exception != nullptr);
        ++failed;
    });
    auto t = std::make_shared<FunctionTask>([] { throw std::runtime_error("kaput"); }, "bad");
    CHECK(pool.submit(t));
    pool.waitForAll();
    CHECK(failed == 1);
    CHECK(msg == "kaput");
    CHECK(t->status() == TaskStatus::Failed);
    CHECK(pool.stats().totalFailed == 1);

    auto t2 = std::make_shared<FunctionTask>([] { throw 42; }, "weird");
    CHECK(pool.submit(t2));
    pool.waitForAll();
    CHECK(msg == "Unknown exception");
    pool.shutdown();
}

TEST(retry_handler_retries_then_succeeds) {
    ThreadPool pool(cfg(2, 2));
    pool.start();
    RetryHandler retry(pool, 3);
    std::atomic<int> attempts{ 0 };
    auto t = std::make_shared<FunctionTask>([&] {
        if (++attempts < 3) throw std::runtime_error("transient");
    }, "flaky");
    CHECK(pool.submit(t));
    CHECK(pool.waitForAll(3000ms));
    CHECK(attempts == 3);
    CHECK(t->status() == TaskStatus::Completed);
    CHECK(retry.retriesPerformed() == 2);
    CHECK(retry.retriesExhausted() == 0);
    pool.shutdown();
}

TEST(retry_handler_gives_up) {
    ThreadPool pool(cfg(2, 2));
    pool.start();
    RetryHandler retry(pool, 2);
    std::atomic<int> attempts{ 0 };
    auto t = std::make_shared<FunctionTask>([&] { ++attempts; throw std::runtime_error("always"); }, "doomed");
    CHECK(pool.submit(t));
    CHECK(pool.waitForAll(3000ms));
    CHECK(attempts == 3);  // 1 + 2 retries
    CHECK(t->status() == TaskStatus::Failed);
    CHECK(retry.retriesExhausted() == 1);
    pool.shutdown();
}

TEST(metrics_collector_numbers) {
    ThreadPool pool(cfg(2, 2));
    MetricsCollector metrics(pool);
    pool.start();
    for (int i = 0; i < 10; ++i) CHECK(pool.submit([] { std::this_thread::sleep_for(2ms); }));
    CHECK(pool.submit([] { throw std::runtime_error("x"); }));
    pool.waitForAll();
    const auto m = metrics.getMetrics();
    CHECK(m.totalCompleted == 10);
    CHECK(m.totalFailed == 1);
    CHECK(m.totalSubmitted == 11);
    CHECK(m.totalRejected == 0);
    CHECK(m.peakWorkers == 2);
    CHECK(m.avgExecutionTime >= 2ms);
    CHECK(m.maxExecutionTime >= m.avgExecutionTime);
    CHECK(m.totalExecutionTime >= 20ms);
    metrics.reset();
    CHECK(metrics.getMetrics().totalCompleted == 0);
    pool.shutdown();
}

TEST(metrics_counts_rejections_not_as_submitted) {
    ThreadPool pool(cfg(1, 1, 1));
    MetricsCollector metrics(pool);
    pool.start();
    std::latch started(1);
    std::promise<void> gate;
    auto gf = gate.get_future().share();
    CHECK(pool.submit([&] { started.count_down(); gf.wait(); }));
    started.wait();
    CHECK(pool.submit([] {}));
    for (int i = 0; i < 5; ++i) CHECK(!pool.submit([] {}));
    gate.set_value();
    pool.waitForAll();
    const auto m = metrics.getMetrics();
    CHECK(m.totalSubmitted == 2);
    CHECK(m.totalRejected == 5);
    CHECK(m.totalCompleted == 2);
    pool.shutdown();
}

TEST(events_are_per_pool) {
    ThreadPool a(cfg(1, 1)), b(cfg(1, 1));
    a.start(); b.start();
    std::atomic<int> onA{ 0 }, onB{ 0 };
    auto ca = a.events().on<TaskCompletedEvent>([&](const TaskCompletedEvent&) { ++onA; });
    auto cb = b.events().on<TaskCompletedEvent>([&](const TaskCompletedEvent&) { ++onB; });
    for (int i = 0; i < 5; ++i) CHECK(a.submit([] {}));
    for (int i = 0; i < 3; ++i) CHECK(b.submit([] {}));
    a.waitForAll(); b.waitForAll();
    CHECK(onA == 5);
    CHECK(onB == 3);
}

TEST(member_function_subscription_and_disconnect) {
    struct Obs {
        std::atomic<int> n{ 0 };
        void onDone(const TaskCompletedEvent&) { ++n; }
        void onDoneConst(const TaskCompletedEvent&) const {}
    } obs;
    ThreadPool pool(cfg(1, 1));
    pool.start();
    {
        auto conn = pool.events().on(obs, &Obs::onDone);
        auto conn2 = pool.events().on(obs, &Obs::onDoneConst);
        CHECK(pool.submit([] {}));
        pool.waitForAll();
        CHECK(obs.n == 1);
        CHECK(pool.events().hasListeners<TaskCompletedEvent>());
    }
    CHECK(!pool.events().hasListeners<TaskCompletedEvent>());
    CHECK(pool.submit([] {}));
    pool.waitForAll();
    CHECK(obs.n == 1);  // disconnected by scope exit
    pool.shutdown();
}

TEST(handler_may_disconnect_itself) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    std::atomic<int> calls{ 0 };
    Connection conn;
    conn = pool.events().on<TaskCompletedEvent>([&](const TaskCompletedEvent&) {
        ++calls;
        conn.disconnect();  // would deadlock if disconnect waited for itself
    });
    for (int i = 0; i < 3; ++i) { CHECK(pool.submit([] {})); pool.waitForAll(); }
    CHECK(calls == 1);
    pool.shutdown();
}

TEST(observer_destruction_races_with_events) {
    ThreadPool pool(cfg(4, 4));
    pool.start();
    std::atomic<bool> stop{ false };
    std::thread producer([&] {
        while (!stop) { (void)pool.submit([] {}); std::this_thread::sleep_for(50us); }
    });
    for (int i = 0; i < 200; ++i) {
        auto metrics = std::make_unique<MetricsCollector>(pool);  // subscribe ...
        std::this_thread::sleep_for(200us);
        metrics.reset();                                          // ... and destroy while events fly
    }
    stop = true;
    producer.join();
    pool.waitForAll();
    pool.shutdown();
    CHECK(true);  // reaching here without a crash/sanitizer report is the assertion
}

TEST(observer_may_outlive_pool) {
    std::unique_ptr<MetricsCollector> m;
    {
        ThreadPool pool(cfg(1, 1));
        m = std::make_unique<MetricsCollector>(pool);
        pool.start();
        CHECK(pool.submit([] {}));
        pool.waitForAll();
    }
    CHECK(m->getMetrics().totalCompleted == 1);
    m.reset();  // disconnecting from a dead pool must be harmless
}

TEST(handler_exception_does_not_break_pool) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    auto c = pool.events().on<TaskCompletedEvent>([](const TaskCompletedEvent&) { throw std::runtime_error("bad handler"); });
    std::atomic<int> n{ 0 };
    for (int i = 0; i < 5; ++i) CHECK(pool.submit([&] { ++n; }));
    pool.waitForAll();
    CHECK(n == 5);
    pool.shutdown();
}

TEST(wait_for_all_timeout) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    CHECK(pool.waitForAll(10ms));  // nothing pending
    std::promise<void> gate;
    auto gf = gate.get_future().share();
    CHECK(pool.submit([&] { gf.wait(); }));
    CHECK(!pool.waitForAll(50ms));
    gate.set_value();
    CHECK(pool.waitForAll(2000ms));
    pool.shutdown();
}

TEST(shutdown_from_worker_throws) {
    ThreadPool pool(cfg(1, 1));
    pool.start();
    std::atomic<bool> threw{ false };
    CHECK(pool.submit([&] {
        CHECK(pool.onWorkerThread());
        try { pool.shutdown(); } catch (const std::logic_error&) { threw = true; }
    }));
    pool.waitForAll();
    CHECK(threw);
    CHECK(!pool.onWorkerThread());
    pool.shutdown();
}

TEST(task_can_submit_more_tasks) {
    ThreadPool pool(cfg(2, 4));
    pool.start();
    std::atomic<int> n{ 0 };
    for (int i = 0; i < 20; ++i)
        CHECK(pool.submit([&] {
            for (int j = 0; j < 5; ++j) CHECK(pool.submit([&] { ++n; }));
        }));
    pool.waitForAll();
    CHECK(n == 100);
    pool.shutdown();
}

TEST(pool_logger_formats_lines) {
    ThreadPool pool(cfg(1, 1));
    std::ostringstream out, err;
    {
        PoolLogger::Options o;
        o.out = &out; o.err = &err;
        o.categories = LogCategory::All;
        PoolLogger logger(pool, o);
        pool.start();
        CHECK(pool.submit([] {}, "hello"));
        CHECK(pool.submit([] { throw std::runtime_error("oops"); }, "broken"));
        pool.waitForAll();
        pool.shutdown();
    }
    CHECK(out.str().find("[QUEUE] Task 'hello' queued") != std::string::npos);
    CHECK(out.str().find("[DONE] Task 'hello' completed") != std::string::npos);
    CHECK(out.str().find("[POOL] Started with 1 workers") != std::string::npos);
    CHECK(out.str().find("[SHUTDOWN] Graceful") != std::string::npos);
    CHECK(err.str().find("[ERROR] Task 'broken' failed: oops") != std::string::npos);
}

TEST(pool_logger_sink) {
    ThreadPool pool(cfg(1, 1));
    std::mutex m;
    std::vector<std::pair<LogLevel, std::string>> lines;
    PoolLogger::Options o;
    o.sink = [&](LogLevel l, std::string_view s) { std::lock_guard lk(m); lines.emplace_back(l, std::string(s)); };
    PoolLogger logger(pool, o);
    pool.start();
    CHECK(pool.submit([] { throw std::runtime_error("e"); }, "t"));
    pool.waitForAll();
    pool.shutdown();
    bool sawError = false;
    for (auto& [l, s] : lines) sawError |= (l == LogLevel::Error && s.find("failed: e") != std::string::npos);
    CHECK(sawError);
}

TEST(config_is_sanitised) {
    ThreadPoolConfig c;
    c.minThreads = 3;
    c.maxThreads = 1;  // invalid: below min
    ThreadPool pool(c);
    CHECK(pool.config().maxThreads == 3);
    ThreadPoolConfig z;
    z.minThreads = 0;
    z.maxThreads = 0;
    ThreadPool p2(z);
    CHECK(p2.config().maxThreads == 1);
    p2.start();
    auto f = p2.submitWithFuture([] { return 7; });
    CHECK(f && f->get() == 7);
    p2.shutdown();
}

TEST(stress_many_producers) {
    ThreadPool pool(cfg(2, 8));
    pool.start();
    std::atomic<int> n{ 0 };
    std::vector<std::thread> producers;
    for (int p = 0; p < 8; ++p)
        producers.emplace_back([&] {
            for (int i = 0; i < 2000; ++i)
                while (!pool.submit([&] { ++n; }, "s", static_cast<Priority>(i % 3 * 5))) std::this_thread::yield();
        });
    for (auto& t : producers) t.join();
    pool.waitForAll();
    CHECK(n == 16000);
    CHECK(pool.stats().totalCompleted == 16000);
    pool.shutdown();
}

} // namespace

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0, failedTests = 0;
    for (auto& t : registry()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
        const int before = g_failures.load();
        const auto t0 = std::chrono::steady_clock::now();
        std::printf("[ RUN  ] %s\n", t.name);
        std::fflush(stdout);
        try { t.fn(); }
        catch (const std::exception& e) { ++g_failures; std::fprintf(stderr, "    unexpected exception: %s\n", e.what()); }
        catch (...) { ++g_failures; std::fprintf(stderr, "    unexpected non-std exception\n"); }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        const bool ok = before == g_failures.load();
        std::printf("[ %s ] %s (%lld ms)\n", ok ? " OK " : "FAIL", t.name, static_cast<long long>(ms));
        ++ran;
        if (!ok) ++failedTests;
    }
    std::printf("\n%d tests, %d failed, %d checks, %d failed checks\n", ran, failedTests, g_checks.load(), g_failures.load());
    return g_failures == 0 ? 0 : 1;
}
