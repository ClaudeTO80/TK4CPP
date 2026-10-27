/**
 * @file example_http_server.cpp
 * @brief cpp-httplib server that owns a persistent JobScheduler and exposes it over REST.
 *
 * Endpoints (plain text / JSON responses):
 *   GET    /triggers                      list triggers
 *   POST   /triggers?id=&job=&cron=       create a cron trigger (5 or 6 fields, UTC)
 *   POST   /triggers?id=&job=&every_ms=   create a fixed-rate trigger
 *   POST   /triggers/{id}/pause|resume
 *   DELETE /triggers/{id}
 *   POST   /jobs/{id}/run                 run a job now
 *
 * Note: with cpp-httplib a POST must carry a Content-Length, otherwise the server waits for its
 * read timeout (5 s) before answering. With curl use `-d ''`, e.g.
 *   curl -d '' "http://127.0.0.1:8080/triggers?id=tick&job=hello&every_ms=500"
 *
 * Usage: example_http_server [port] [db-file]
 * Stop with SIGINT/SIGTERM: the scheduler is shut down cleanly before the process exits.
 */
#include <TK4CPP/Scheduler/JobScheduler.h>
#include <TK4CPP/Scheduler/SqliteJobStore.h>

#include <httplib.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>

using namespace TK4CPP::Scheduler;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_stop{false};

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default: out += c;
        }
    }
    return out;
}

std::string iso(const std::optional<TimePoint>& tp) {
    if (!tp) {
        return "null";
    }
    const auto secs = std::chrono::floor<std::chrono::seconds>(*tp);
    return "\"" + std::format("{:%FT%TZ}", secs) + "\"";
}

const char* stateName(TriggerState s) {
    switch (s) {
    case TriggerState::Normal: return "normal";
    case TriggerState::Paused: return "paused";
    case TriggerState::Complete: return "complete";
    }
    return "?";
}

std::string describe(const Result<void>& r) {
    return std::string(toString(r.error().code)) + ": " + r.error().message + "\n";
}

int statusFor(ErrorCode c) {
    switch (c) {
    case ErrorCode::NotFound: return 404;
    case ErrorCode::AlreadyExists: return 409;
    case ErrorCode::InvalidState: return 409;
    case ErrorCode::StoreFailure: return 500;
    default: return 400;
    }
}

} // namespace

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    const std::string dbPath = argc > 2 ? argv[2] : "scheduler.db";

    auto store = SqliteJobStore::open(dbPath);
    if (!store) {
        std::fprintf(stderr, "cannot open store: %s\n", store.error().message.c_str());
        return 1;
    }

    JobScheduler::Options options;
    options.store = *store;
    options.workerThreads = 4;
    JobScheduler scheduler(std::move(options));

    // The job body is plain application code; here it just logs.
    (void)scheduler.registerJob("log", [](const JobContext& ctx) {
        std::printf("[job] %s trigger=%s fire=%s%s\n", ctx.data.contains("msg") ? ctx.data.at("msg").c_str() : "-",
                    ctx.triggerId.c_str(), ctx.fireId.c_str(), ctx.recovering ? " RECOVERED" : "");
        std::fflush(stdout);
    });

    if (auto r = scheduler.start(); !r) {
        std::fprintf(stderr, "start failed: %s\n", r.error().message.c_str());
        return 1;
    }
    // Idempotent: replace=true keeps persisted triggers attached to the job.
    (void)scheduler.addJob({"hello", "log", {{"msg", "hello from the scheduler"}}, /*requestsRecovery=*/true}, true);

    httplib::Server svr;

    svr.Get("/triggers", [&](const httplib::Request&, httplib::Response& res) {
        std::ostringstream o;
        o << "[";
        bool first = true;
        for (const auto& t : scheduler.triggers()) {
            o << (first ? "" : ",") << "\n  {\"id\":\"" << jsonEscape(t.id) << "\",\"job\":\"" << jsonEscape(t.jobId)
              << "\",\"state\":\"" << stateName(t.state) << "\",\"next\":" << iso(t.nextFire)
              << ",\"last\":" << iso(t.lastFire) << ",\"remaining\":" << t.remaining << "}";
            first = false;
        }
        o << "\n]\n";
        res.set_content(o.str(), "application/json");
    });

    svr.Post("/triggers", [&](const httplib::Request& req, httplib::Response& res) {
        const auto id = req.get_param_value("id");
        const auto job = req.get_param_value("job");
        Result<Trigger> trigger = makeError(ErrorCode::InvalidArgument, "give either cron= or every_ms=");
        if (req.has_param("cron")) {
            trigger = Trigger::cron(id, job, req.get_param_value("cron"));
        } else if (req.has_param("every_ms")) {
            trigger = Trigger::interval(id, job, Millis{std::atoll(req.get_param_value("every_ms").c_str())});
        }
        if (!trigger) {
            res.status = statusFor(trigger.error().code);
            res.set_content(std::string(toString(trigger.error().code)) + ": " + trigger.error().message + "\n", "text/plain");
            return;
        }
        if (auto r = scheduler.addTrigger(std::move(*trigger)); !r) {
            res.status = statusFor(r.error().code);
            res.set_content(describe(r), "text/plain");
            return;
        }
        res.status = 201;
        res.set_content("created\n", "text/plain");
    });

    const auto act = [&](auto fn) {
        return [&scheduler, fn](const httplib::Request& req, httplib::Response& res) {
            const auto r = fn(scheduler, req.matches[1].str());
            if (!r) {
                res.status = statusFor(r.error().code);
                res.set_content(describe(r), "text/plain");
            } else {
                res.set_content("ok\n", "text/plain");
            }
        };
    };
    svr.Post(R"(/triggers/([^/]+)/pause)", act([](JobScheduler& s, const std::string& id) { return s.pauseTrigger(id); }));
    svr.Post(R"(/triggers/([^/]+)/resume)", act([](JobScheduler& s, const std::string& id) { return s.resumeTrigger(id); }));
    svr.Delete(R"(/triggers/([^/]+))", act([](JobScheduler& s, const std::string& id) { return s.removeTrigger(id); }));
    svr.Post(R"(/jobs/([^/]+)/run)", act([](JobScheduler& s, const std::string& id) { return s.triggerJobNow(id); }));

    std::signal(SIGINT, [](int) { g_stop = true; });
    std::signal(SIGTERM, [](int) { g_stop = true; });
    std::thread watcher([&] {
        while (!g_stop) {
            std::this_thread::sleep_for(50ms);
        }
        svr.stop();
    });

    std::printf("listening on 127.0.0.1:%d, store=%s\n", port, dbPath.c_str());
    std::fflush(stdout);
    svr.listen("127.0.0.1", port);

    g_stop = true;
    watcher.join();
    scheduler.shutdown(true); // waits for running jobs; their in-flight records are cleared
    std::printf("clean shutdown\n");
    return 0;
}
