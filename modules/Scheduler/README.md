# TK4CPP::Scheduler

Quartz-style job scheduler for small C++23 applications (for example behind a cpp-httplib server).
UTC only, SQLite persistence, jobs may run concurrently with themselves.

## Model

| Concept | Type | Notes |
|---|---|---|
| What | `JobDetail` (id, type, `JobData`, `requestsRecovery`) | Data only. Behaviour is a handler registered by *type name* (`registerJob`), so state can be persisted. |
| When | `Trigger` = `CronSpec` \| `IntervalSpec` \| `OnceSpec` + `MisfirePolicy` | One job can have many triggers. Interval triggers are anchored (`start + k*every`), so they do not drift. |
| Who | `JobScheduler` | One dispatcher thread sleeps until the earliest trigger, then hands executions to an `IExecutor`. The dispatcher never runs job code. |
| Where | `IJobStore`: `RamJobStore`, `SqliteJobStore` | Write-through. The working set lives in memory. |
| Observe | `ISchedulerListener` | Started / finished (with exception text) / misfire / trigger complete / error. Called outside the scheduler lock. |

Errors use `std::expected<T, Error>` (`Result<T>`); no exception crosses the public API.

## Cron dialect

`[sec] min hour dom month dow`, 5 or 6 fields, evaluated in **UTC**. Supports `*`, `a`, `a-b`, `a,b`, `*/n`, `a-b/n`, `a/n`,
`JAN..DEC`, `SUN..SAT`, `0` and `7` as Sunday. When both day-of-month and day-of-week are restricted a day matches if
**either** matches (Vixie cron). Not supported: `?`, `L`, `W`, `#`, year field.

## Guarantees

* **At most once per occurrence.** The trigger is advanced and persisted *before* the job runs. A crash between the two loses
  that occurrence rather than repeating it.
* **At least once for recoverable jobs.** With `JobDetail::requestsRecovery = true` an in-flight record is written in the same
  transaction as the trigger update. If the process dies (including `kill -9`) the execution is re-run once at the next
  `start()` with `JobContext::recovering == true`. Make such jobs idempotent.
* **Restart.** Jobs, triggers, `nextFire`, paused state and remaining repeat count survive restarts.
* **Misfire.** Late by more than `Options::misfireThreshold`: `FireNow` (fire once, realign), `SkipToNext`, or `CatchUp`
  (fire every missed occurrence). Late by less than the threshold: fire normally, and every other occurrence already due
  fires back to back (Quartz semantics). **After a downtime shorter than the threshold, a 500 ms trigger runs a burst of
  missed executions.** Lower the threshold or use `SkipToNext` for high-frequency triggers.
* **Concurrency.** The same job may run in parallel with itself, bounded by the executor size. Handlers must be thread-safe.
* **Shutdown.** `shutdown(true)` runs queued executions, `shutdown(false)` discards them (recoverable ones are re-run at the
  next start). Running executions are never interrupted.

## Limits (by design)

* One process per database file. No clustering.
* A job execution has no timeout or cancellation. Add it in the handler.
* Handlers must be registered **before** `start()`; an occurrence whose job type has no handler is reported to listeners and lost.
* The dispatcher scans all triggers on every wake-up (O(n)). Measured below; fine for thousands, not for millions.
* SQLite uses `synchronous=FULL`, so each fire costs one durable commit (about 1 ms here, depends on the disk).

## Usage

```cpp
auto store = SqliteJobStore::open("scheduler.db");
JobScheduler::Options o;
o.store = *store;
JobScheduler s(std::move(o));

s.registerJob("report", [](const JobContext& c) { /* ... */ });   // before start()
s.start();                                                         // loads state, recovers interrupted jobs

s.addJob({"nightly", "report", {{"name", "nightly"}}, true}, /*replace=*/true);   // idempotent
if (!s.getTrigger("nightly-cron"))                                                // keep persisted state on restart
    s.addTrigger(Trigger::cron("nightly-cron", "nightly", "0 3 * * *").value());

// ... on exit
s.shutdown();
```

`examples/example_http_server.cpp` exposes the scheduler over REST with cpp-httplib.

## Running jobs on the TK4CPP ThreadPool

`ThreadPoolAdapter` (`TK4CPP/Scheduler/ThreadPoolAdapter.h`) is an `IExecutor` over `TK4CPP::TP::ThreadPool`. The
Scheduler module depends on the Thread module (`TK4CPP_MODULE_DEPENDS_SCHEDULER`); the built-in `ThreadPoolExecutor` stays the
default when `Options::executor` is not set.

```cpp
auto pool = std::make_shared<TK4CPP::TP::ThreadPool>(TK4CPP::TP::ThreadPoolConfig::ioBound());
pool->start();

JobScheduler::Options o;
o.store    = *store;
o.executor = std::make_shared<ThreadPoolAdapter>(pool);   // borrowed pool: shared with the application
JobScheduler s(std::move(o));
```

* **Borrowed pool (default).** The adapter never starts or stops the pool. `shutdown(true)` waits only for the tasks *the
  scheduler* submitted; `shutdown(false)` cancels those that have not started, without waiting for other users' tasks.
* **Owned pool.** `ThreadPoolAdapter::createOwning(config)` creates, starts and finally stops a private pool.
* **Names and priority.** Each execution is a `TP::FunctionTask` named `scheduler:<jobId>` with `Options::priority`.
* **Refusals.** A stopped pool or a full bounded queue (`OverflowPolicy::Reject`) makes `submit` return `false`: listeners get
  `onError`, `triggerJobNow` returns `ErrorCode::ExecutorRejected`, a *recoverable* trigger-originated execution is retried at
  the next `start()`, a non-recoverable one is lost. `ThreadPoolAdapter::rejected()` counts them.
* **Do not use `OverflowPolicy::Block`.** It would block the dispatcher thread inside `submit`.

## CMake

```cmake
find_package(TK4CPP REQUIRED COMPONENTS Scheduler)
target_link_libraries(app PRIVATE TK4CPP::Scheduler)   # TK4CPP::Scheduler_static when both variants are built
```

Requires C++23 (`std::expected`), SQLite3, Threads and the Thread module. Examples: `-DTK4CPP_SCHEDULER_BUILD_EXAMPLES=ON`.
Tests: one GoogleTest executable `TK4CPP_Scheduler_tests` plus the helper `TK4CPP_Scheduler_crash_child` (started and
`SIGKILL`ed by the persistence tests; POSIX only).

## Verification (GCC 13.3, Linux, 2 cores; TK4CPP `origin/main` df935ca)

Module port, 47 GoogleTest cases (35 core + 12 ThreadPool adapter; the standalone suite counted 886 + 126 checks, GoogleTest
does not count assertions):

* Static-only, shared+static (tests link the shared Scheduler and Thread: symbol export verified), ASan+UBSan and TSan builds:
  all green, no sanitizer reports (TSan run repeated by hand: 0 warnings).
* `find_package(TK4CPP COMPONENTS Scheduler)` from an external project against a static-only and a shared+static install:
  builds, links and runs (5/5 fires through `ThreadPoolAdapter`).
* The properties below (cron cross-check, crash/`SIGKILL` recovery, mutation checks, saturated-CPU runs) were measured on the
  standalone version of this code before the port; the port changed headers (export macros), CMake and the test harness only,
  and the mutation and saturated-CPU runs were **not repeated** after it.

* Cron `next()` cross-checked against a brute-force second-by-second `matches()` scan on 75 random cases (+45 sampled).
* Crash tests spawn `TK4CPP_Scheduler_crash_child` and `SIGKILL` it inside a running job: the recoverable job is re-run exactly
  once with `recovering == true`, the non-recoverable one is not re-run, and the one-shot trigger never fires twice.
* Mutation checks (standalone): 6 injected bugs in the core and 8 in the adapter/executor contract; all caught except one
  equivalent mutant (an explicit bookkeeping `erase` redundant with task expiry).

Dispatch lateness (actual start minus scheduled instant), `tests/bench_latency`:

| Scenario | fires | mean | p99 | max |
|---|---|---|---|---|
| RAM, 1 trigger / 10 ms | 281 | 0.34 ms | 0.54 ms | 2.4 ms |
| RAM, 100 triggers / 100 ms | 2802 | 0.35 ms | 2.3 ms | 21.5 ms |
| RAM, 1000 triggers / 1 s | 3805 | 0.32 ms | 1.2 ms | 17.3 ms |
| SQLite, 1 trigger / 10 ms | 281 | 1.40 ms | 2.2 ms | 3.3 ms |
| SQLite, 100 triggers / 100 ms | 3105 | 1.09 ms | 14.0 ms | 38.0 ms |

Not verified: Windows/macOS/MSVC builds (the crash tests use fork/exec and are POSIX only; exported classes with STL members may warn C4251 on MSVC), clang, the full-repo build with all modules (3rdParty submodules not initialised), SQLite on a network filesystem, databases with millions of rows.
