/**
 * @file 001_BasicUsage.cpp
 * @brief Minimal use of the pool: fire-and-forget tasks, priorities and a future.
 */
#include "TK4CPP/ThreadPool/ThreadPool.h"

#include <iostream>
#include <mutex>

using namespace TK4CPP::TP;

int main() {
    // One worker per hardware thread, no dynamic scaling.
    ThreadPool pool(ThreadPoolConfig::cpuBound());
    pool.start();

    std::mutex coutMutex;  // std::cout is not synchronised between tasks

    for (int i = 0; i < 20; ++i) {
        // submit() returns std::expected: ignoring a refusal would silently lose the task.
        auto r = pool.submit(
            [i, &coutMutex] {
                std::lock_guard lk(coutMutex);
                std::cout << "Task " << i << '\n';
            },
            "task-" + std::to_string(i));
        if (!r) {
            std::cerr << "submit failed: " << toString(r.error()) << '\n';
            return 1;
        }
    }

    // A high priority task jumps ahead of the queued normal ones and hands back a result.
    auto fut = pool.submitWithFuture([] { return 42; }, "high-prio", Priority::High);
    if (!fut) {
        std::cerr << "submit failed: " << toString(fut.error()) << '\n';
        return 1;
    }
    std::cout << "high-prio result: " << fut->get() << '\n';

    pool.shutdown();  // graceful: waits for every queued task
    std::cout << "completed tasks: " << pool.stats().totalCompleted << '\n';
    return 0;
}
