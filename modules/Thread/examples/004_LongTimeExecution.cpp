/**
 * @file 004_LongTimeExecution.cpp
 * @brief More jobs than workers: the queue and the 2-thread cap decide the schedule.
 *
 * Each job simulates the five phases of a long running job (copy in, prologue,
 * execution, epilogue, copy out). Durations are scaled to milliseconds.
 */
#include "TK4CPP/ThreadPool/PoolLogger.h"
#include "TK4CPP/ThreadPool/ThreadPool.h"

#include <array>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>

using namespace TK4CPP::TP;

namespace {

std::mutex g_coutMutex;

/**
 * @brief Simulates a job lasting @p units time units (1 unit = 10 ms) split in five phases.
 * @param id Job identifier, used in the output.
 * @param units Total duration in units.
 */
void simulateJob(int id, int units) {
    static constexpr std::array<std::string_view, 5> phases{
        "Copying input files", "Prologue", "Execution", "Epilogue", "Copying output files" };
    for (auto phase : phases) {
        {
            std::lock_guard lk(g_coutMutex);
            std::cout << "  job " << id << ": " << phase << '\n';
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(units * 10 / 5));
    }
}

} // namespace

int main() {
    ThreadPoolConfig cfg;
    cfg.minThreads = 2;
    cfg.maxThreads = 2;  // at most two jobs at a time
    ThreadPool pool(cfg);

    // Route the pool log through the same mutex as the job output so lines never interleave.
    PoolLogger::Options logOpts;
    logOpts.sink = [](LogLevel, std::string_view line) {
        std::lock_guard lk(g_coutMutex);
        std::cout << line << '\n';
    };
    PoolLogger logger(pool, logOpts);
    pool.start();

    const std::pair<int, int> jobs[] = { { 1, 10 }, { 2, 5 }, { 3, 20 }, { 4, 50 }, { 5, 15 } };
    for (const auto& [id, units] : jobs)
        (void)pool.submit([id, units] { simulateJob(id, units); }, "job-" + std::to_string(id));

    pool.waitForAll();
    pool.shutdown();
    return 0;
}
