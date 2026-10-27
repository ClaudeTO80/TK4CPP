/**
 * @file crash_child.cpp
 * @brief Helper process for the persistence tests: starts a scheduler whose job hangs, and is SIGKILLed by the test.
 *
 * Usage: `crash_child <db> <marker> <plain|recover>`. The marker file is created when the job
 * starts, so the parent knows the execution is in flight. The process never returns normally.
 */
#include <TK4CPP/Scheduler/JobScheduler.h>
#include <TK4CPP/Scheduler/SqliteJobStore.h>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

using namespace TK4CPP::Scheduler;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    if (argc != 4) {
        return 1;
    }
    const std::string db = argv[1];
    const std::string marker = argv[2];
    const std::string mode = argv[3];

    auto store = SqliteJobStore::open(db);
    if (!store) {
        return 2;
    }
    JobScheduler::Options o;
    o.store = *store;
    o.misfireThreshold = 60'000ms;
    o.workerThreads = 4;
    JobScheduler s(std::move(o));
    (void)s.registerJob("hang", [&](const JobContext&) {
        std::ofstream(marker) << "started\n";
        std::this_thread::sleep_for(60s);
    });
    if (!s.start()) {
        return 3;
    }
    if (!s.addJob({"j", "hang", {}, mode == "recover"}, true)) {
        return 4;
    }
    if (!s.addTrigger(Trigger::once("once", "j", Clock::now() + 100ms))) {
        return 5;
    }
    std::this_thread::sleep_for(60s);
    return 0;
}
