/**
 * @file 002_Observers.cpp
 * @brief Observing a pool: PoolLogger, MetricsCollector, RetryHandler and a custom handler.
 */
#include "TK4CPP/ThreadPool/Observers.h"
#include "TK4CPP/ThreadPool/PoolLogger.h"
#include "TK4CPP/ThreadPool/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace TK4CPP::TP;
using namespace std::chrono_literals;

/** @brief Custom observer: counts slow tasks. Handlers run on worker threads, hence the atomic. */
class SlowTaskCounter {
public:
    explicit SlowTaskCounter(ThreadPool& pool)
        : _connection(pool.events().on(*this, &SlowTaskCounter::onCompleted)) {}

    [[nodiscard]] int count() const { return _slow.load(); }

private:
    void onCompleted(const TaskCompletedEvent& e) {
        if (e.elapsed > 20ms) ++_slow;
    }

    std::atomic<int> _slow{ 0 };
    Connection       _connection;  // declared last: disconnects before _slow is destroyed
};

int main() {
    ThreadPoolConfig cfg;
    cfg.minThreads = 1;
    cfg.maxThreads = 4;
    cfg.name = "demo";

    ThreadPool pool(cfg);

    PoolLogger::Options logOpts;
    logOpts.categories = LogCategory::All;
    logOpts.timestamps = true;
    PoolLogger logger(pool, logOpts);        // prints every event

    MetricsCollector metrics(pool);          // aggregates numbers
    RetryHandler retry(pool, 2);             // resubmits failing tasks up to 2 times
    SlowTaskCounter slow(pool);              // user defined observer

    pool.start();

    for (int i = 0; i < 6; ++i)
        (void)pool.submit([i] { std::this_thread::sleep_for(std::chrono::milliseconds(10 * (i + 1))); },
                          "work-" + std::to_string(i));

    std::atomic<int> attempts{ 0 };
    (void)pool.submit([&] {
        if (++attempts < 3) throw std::runtime_error("transient failure");
    }, "flaky", Priority::High);

    pool.waitForAll();
    pool.shutdown();

    const auto m = metrics.getMetrics();
    std::cout << "\nsubmitted " << m.totalSubmitted << ", completed " << m.totalCompleted
              << ", failed " << m.totalFailed << ", peak workers " << m.peakWorkers << '\n'
              << "avg " << m.avgExecutionTime.count() / 1000.0 << " ms, max "
              << m.maxExecutionTime.count() / 1000.0 << " ms\n"
              << "retries performed " << retry.retriesPerformed()
              << ", slow tasks (>20ms) " << slow.count() << '\n';
    return 0;
}
