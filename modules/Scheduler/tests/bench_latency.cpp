/// Measures dispatch lateness (actual start - scheduled instant) and dispatcher cost with many triggers.
#include <TK4CPP/Scheduler/JobScheduler.h>
#include <TK4CPP/Scheduler/SqliteJobStore.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <thread>

using namespace TK4CPP::Scheduler;
using namespace std::chrono_literals;

namespace {

struct Stats {
    double mean{}, p50{}, p99{}, max{};
    std::size_t n{};
};

Stats summarize(std::vector<double> v) {
    Stats s;
    s.n = v.size();
    if (v.empty()) {
        return s;
    }
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v) {
        sum += x;
    }
    s.mean = sum / static_cast<double>(v.size());
    s.p50 = v[v.size() / 2];
    s.p99 = v[static_cast<std::size_t>(static_cast<double>(v.size() - 1) * 0.99)];
    s.max = v.back();
    return s;
}

void run(const char* label, std::shared_ptr<IJobStore> store, int triggers, Millis period, Millis duration) {
    JobScheduler::Options o;
    o.store = std::move(store);
    o.workerThreads = 4;
    JobScheduler s(std::move(o));
    std::mutex m;
    std::vector<double> lateness;
    (void)s.registerJob("noop", [&](const JobContext& c) {
        const double us = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(c.actualTime - c.scheduledTime).count());
        std::lock_guard l(m);
        lateness.push_back(us);
    });
    (void)s.start();
    (void)s.addJob({"j", "noop", {}, false});
    const auto base = std::chrono::floor<Millis>(Clock::now()) + 200ms;
    for (int i = 0; i < triggers; ++i) {
        // spread the phases so triggers do not all fall on the same millisecond
        const auto phase = Millis{(period.count() * i) / triggers};
        (void)s.addTrigger(Trigger::interval("t" + std::to_string(i), "j", period, base + phase).value());
    }
    std::this_thread::sleep_for(duration);
    s.shutdown();
    std::lock_guard l(m);
    const auto st = summarize(lateness);
    std::printf("%-34s triggers=%-5d fires=%-6zu lateness us: mean=%8.0f p50=%8.0f p99=%8.0f max=%8.0f\n", label,
                triggers, st.n, st.mean, st.p50, st.p99, st.max);
}

} // namespace

int main() {
    run("RAM,   1 trigger  / 10 ms", std::make_shared<RamJobStore>(), 1, 10ms, 3s);
    run("RAM, 100 triggers / 100 ms", std::make_shared<RamJobStore>(), 100, 100ms, 3s);
    run("RAM,1000 triggers / 1000 ms", std::make_shared<RamJobStore>(), 1000, 1000ms, 4s);

    const auto db = std::filesystem::temp_directory_path() / "tk4sched_bench.db";
    std::filesystem::remove(db);
    std::filesystem::remove(db.string() + "-wal");
    std::filesystem::remove(db.string() + "-shm");
    run("SQLite, 1 trigger  / 10 ms", SqliteJobStore::open(db).value(), 1, 10ms, 3s);
    run("SQLite, 100 triggers / 100 ms", SqliteJobStore::open(db).value(), 100, 100ms, 3s);
    std::filesystem::remove(db);
    std::filesystem::remove(db.string() + "-wal");
    std::filesystem::remove(db.string() + "-shm");
    return 0;
}
