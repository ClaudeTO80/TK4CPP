/**
 * @file example_basic.cpp
 * @brief Minimal usage: persistent scheduler with a cron job and an interval job.
 *
 * Run it twice: the second run picks up the jobs and triggers stored in scheduler.db and the
 * "ensure" pattern below leaves their state (next fire time, remaining count) untouched.
 */
#include <TK4CPP/Scheduler/JobScheduler.h>
#include <TK4CPP/Scheduler/SqliteJobStore.h>

#include <cstdio>
#include <thread>

using namespace TK4CPP::Scheduler;
using namespace std::chrono_literals;

int main() {
    auto store = SqliteJobStore::open("scheduler.db");
    if (!store) {
        std::fprintf(stderr, "cannot open store: %s\n", store.error().message.c_str());
        return 1;
    }

    JobScheduler::Options options;
    options.store = *store;
    options.misfireThreshold = 30s;
    JobScheduler scheduler(std::move(options));

    // 1. Handlers must be registered before start(): persisted triggers may be due immediately.
    (void)scheduler.registerJob("report", [](const JobContext& ctx) {
        std::printf("[report] %s fire=%s%s\n", ctx.data.at("name").c_str(), ctx.fireId.c_str(),
                    ctx.recovering ? " (recovered after crash)" : "");
    });

    // 2. start() loads persisted state and re-runs interrupted recoverable jobs.
    if (auto r = scheduler.start(); !r) {
        std::fprintf(stderr, "start failed: %s\n", r.error().message.c_str());
        return 1;
    }

    // 3. Ensure the desired jobs exist, without resetting the persisted state of existing triggers.
    (void)scheduler.addJob({"nightly", "report", {{"name", "nightly"}}, /*requestsRecovery=*/true}, /*replace=*/true);
    if (!scheduler.getTrigger("nightly-cron")) {
        (void)scheduler.addTrigger(Trigger::cron("nightly-cron", "nightly", "*/5 * * * * *").value());
    }

    std::this_thread::sleep_for(12s);
    scheduler.shutdown();
    return 0;
}
