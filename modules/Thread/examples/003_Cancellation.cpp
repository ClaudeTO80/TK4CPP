/**
 * @file 003_Cancellation.cpp
 * @brief Cancelling queued tasks, cooperative stop of a running task and immediate shutdown.
 */
#include "TK4CPP/ThreadPool/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <latch>
#include <thread>

using namespace TK4CPP::TP;
using namespace std::chrono_literals;

/** @brief Long running task that polls its stop token. */
class Crawler : public ITask {
public:
    Crawler(std::latch& started) : ITask("crawler"), _started(started) {}

protected:
    void execute() override {
        _started.count_down();
        int steps = 0;
        while (!stopRequested()) {        // cooperative cancellation point
            std::this_thread::sleep_for(5ms);
            ++steps;
        }
        std::cout << "crawler stopped after " << steps << " steps\n";
    }

private:
    std::latch& _started;
};

int main() {
    ThreadPoolConfig cfg;
    cfg.minThreads = 1;
    cfg.maxThreads = 1;  // a single worker, so queued tasks really wait
    ThreadPool pool(cfg);

    auto cancelledSub = pool.events().on<TaskCancelledEvent>([](const TaskCancelledEvent& e) {
        std::cout << "cancelled: " << e.task->name() << '\n';
    });

    pool.start();

    std::latch started(1);
    (void)pool.submit(std::make_shared<Crawler>(started));
    started.wait();  // the worker is now busy inside Crawler::execute()

    // These wait in the queue behind the crawler.
    auto victim = std::make_shared<FunctionTask>([] { std::cout << "never printed\n"; }, "victim");
    (void)pool.submit(victim);
    for (int i = 0; i < 3; ++i)
        (void)pool.submit([] { std::cout << "never printed\n"; }, "queued-" + std::to_string(i));

    std::cout << "cancel(victim) -> " << std::boolalpha << victim->cancel() << '\n';

    // Immediate shutdown: queued tasks are cancelled, the running one gets its stop token signalled.
    pool.shutdownNow();
    std::cout << "cancelled in total: " << pool.stats().totalCancelled << '\n';
    return 0;
}
