#include "TestSupport.h"

#include <TK4CPP/Scheduler/SqliteJobStore.h>

#include <fstream>

using namespace T;

namespace {

void exerciseStore(IJobStore& store) {
    JobDetail job{"j1", "echo", {{"k", "v"}, {"quote", "it's \"x\""}, {"empty", ""}, {"nl", "a\nb"}, {"utf8", "caffè ☕"}}, true};
    CHECK(store.putJob(job).has_value());

    // a trigger for an unknown job is rejected
    auto orphan = Trigger::once("t0", "nope", utc(2030, 1, 1));
    CHECK(!store.putTrigger(orphan).has_value());

    auto cron = Trigger::cron("tc", "j1", "*/5 * * * * *", MisfirePolicy::CatchUp).value();
    cron.nextFire = utc(2030, 1, 1, 0, 0, 5);
    cron.lastFire = utc(2030, 1, 1, 0, 0, 0);
    auto interval = Trigger::interval("ti", "j1", 250ms, utc(2030, 1, 1), 7, MisfirePolicy::SkipToNext).value();
    interval.nextFire = utc(2030, 1, 1);
    interval.state = TriggerState::Paused;
    auto once = Trigger::once("to", "j1", utc(2031, 2, 3, 4, 5, 6));
    once.nextFire = utc(2031, 2, 3, 4, 5, 6);
    CHECK(store.putTrigger(cron).has_value());
    CHECK(store.putTrigger(interval).has_value());
    CHECK(store.putTrigger(once).has_value());

    // updating a job must NOT drop its triggers (regression: INSERT OR REPLACE + cascade)
    job.data["k"] = "v2";
    CHECK(store.putJob(job).has_value());

    auto snap = store.load();
    CHECK(snap.has_value());
    if (!snap) {
        return;
    }
    CHECK(snap->jobs.size() == 1);
    CHECK(snap->triggers.size() == 3);
    if (snap->jobs.size() == 1) {
        CHECK(snap->jobs[0].data.size() == 5);
        CHECK(snap->jobs[0].data["k"] == "v2");
        CHECK(snap->jobs[0].data["quote"] == "it's \"x\"");
        CHECK(snap->jobs[0].data["empty"].empty());
        CHECK(snap->jobs[0].data["nl"] == "a\nb");
        CHECK(snap->jobs[0].data["utf8"] == "caffè ☕");
        CHECK(snap->jobs[0].requestsRecovery);
    }
    for (const auto& t : snap->triggers) {
        if (t.id == "tc") {
            CHECK(std::holds_alternative<CronSpec>(t.spec));
            CHECK(std::get<CronSpec>(t.spec).expression == "*/5 * * * * *");
            CHECK(t.misfire == MisfirePolicy::CatchUp);
            CHECK(t.nextFire == utc(2030, 1, 1, 0, 0, 5));
            CHECK(t.lastFire == utc(2030, 1, 1, 0, 0, 0));
            CHECK(t.remaining == -1);
        } else if (t.id == "ti") {
            CHECK(std::holds_alternative<IntervalSpec>(t.spec));
            CHECK(std::get<IntervalSpec>(t.spec).every == 250ms);
            CHECK(std::get<IntervalSpec>(t.spec).start == utc(2030, 1, 1));
            CHECK(t.state == TriggerState::Paused);
            CHECK(t.remaining == 7);
            CHECK(t.misfire == MisfirePolicy::SkipToNext);
            CHECK(!t.lastFire.has_value());
        } else if (t.id == "to") {
            CHECK(std::holds_alternative<OnceSpec>(t.spec));
            CHECK(std::get<OnceSpec>(t.spec).at == utc(2031, 2, 3, 4, 5, 6));
        } else {
            CHECK(false);
        }
    }

    // beginFire: trigger update + in-flight record in one call; endFire clears it
    cron.nextFire = utc(2030, 1, 1, 0, 0, 10);
    CHECK(store.beginFire(cron, InflightFire{"f1", "tc", "j1", utc(2030, 1, 1, 0, 0, 5)}).has_value());
    snap = store.load();
    CHECK(snap.has_value() && snap->inflight.size() == 1);
    if (snap && snap->inflight.size() == 1) {
        CHECK(snap->inflight[0].fireId == "f1");
        CHECK(snap->inflight[0].scheduled == utc(2030, 1, 1, 0, 0, 5));
    }
    for (const auto& t : snap->triggers) {
        if (t.id == "tc") {
            CHECK(t.nextFire == utc(2030, 1, 1, 0, 0, 10));
        }
    }
    CHECK(store.endFire("f1").has_value());
    CHECK(store.endFire("f1").has_value()); // idempotent
    snap = store.load();
    CHECK(snap.has_value() && snap->inflight.empty());

    CHECK(store.eraseTrigger("to").has_value());
    snap = store.load();
    CHECK(snap.has_value() && snap->triggers.size() == 2);

    // erasing the job removes its triggers
    CHECK(store.eraseJob("j1").has_value());
    snap = store.load();
    CHECK(snap.has_value() && snap->jobs.empty() && snap->triggers.empty());
}

} // namespace

TEST(Store, store_ram_contract) {
    RamJobStore store;
    exerciseStore(store);
}

TEST(Store, store_sqlite_contract) {
    TempDir dir;
    auto store = SqliteJobStore::open(dir.file("s.db"));
    CHECK(store.has_value());
    if (store) {
        exerciseStore(**store);
    }
}

TEST(Store, store_sqlite_survives_reopen) {
    TempDir dir;
    {
        auto store = SqliteJobStore::open(dir.file("s.db"));
        CHECK(store.has_value());
        if (!store) {
            return;
        }
        CHECK((*store)->putJob(JobDetail{"j", "t", {{"a", "b"}}, false}).has_value());
        auto t = Trigger::once("t1", "j", utc(2032, 1, 1));
        t.nextFire = utc(2032, 1, 1);
        CHECK((*store)->putTrigger(t).has_value());
    }
    auto store = SqliteJobStore::open(dir.file("s.db"));
    CHECK(store.has_value());
    if (!store) {
        return;
    }
    auto snap = (*store)->load();
    CHECK(snap.has_value() && snap->jobs.size() == 1 && snap->triggers.size() == 1);
}

TEST(Store, store_sqlite_rejects_garbage_file) {
    TempDir dir;
    {
        std::ofstream f(dir.file("garbage.db"), std::ios::binary);
        f << "this is definitely not a sqlite database, just some text padding it out to be long enough......"
             "......................................................................................................";
    }
    auto store = SqliteJobStore::open(dir.file("garbage.db"));
    CHECK(!store.has_value());
    if (!store) {
        CHECK(store.error().code == ErrorCode::StoreFailure);
    }
    auto missing = SqliteJobStore::open(dir.file("no/such/dir/x.db"));
    CHECK(!missing.has_value());
}
