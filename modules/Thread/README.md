# TK4CPP::ThreadPool

Prioritised, dynamically scaling thread pool with typed event notifications.
C++23, **no third-party dependencies** (only the standard library and `Threads`).

## Features
- Priorities (`Low/Normal/High`), strict FIFO among equal priorities.
- Dynamic scaling: a worker is added as soon as a task is queued and nobody is idle (up to `maxThreads`);
  workers above `minThreads` retire after `idleTimeout`.
- `std::expected` based submission: `submit()` / `submitWithFuture()` tell you *why* a task was refused.
- Bounded queue with `OverflowPolicy::Reject` or `Block`.
- Cancellation: `ITask::cancel()` for queued tasks, `std::stop_token` for running ones, `shutdownNow()`.
- Per-pool `EventBus` (thread-safe, RAII `Connection`), ready-made observers:
  `PoolLogger`, `MetricsCollector`, `RetryHandler`, `FileLogger`.

## Build / use
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # add -DBUILD_SHARED_LIBS=ON for a shared library
cmake --build build && ctest --test-dir build
cmake --install build --prefix /opt/tk4cpp
```
```cmake
find_package(TK4CPPThreadPool 2 REQUIRED)
target_link_libraries(app PRIVATE TK4CPP::ThreadPool)
```
Inside a super-project use `add_subdirectory(ThreadPool)`; the same `TK4CPP::ThreadPool` alias is available.
Sanitizers: `-DTK4CPP_THREADPOOL_SANITIZE="address;undefined"` or `-DTK4CPP_THREADPOOL_SANITIZE=thread`.

## Quick start
```cpp
using namespace TK4CPP::TP;
ThreadPool pool(ThreadPoolConfig::cpuBound());
MetricsCollector metrics(pool);
auto conn = pool.events().on<TaskFailedEvent>([](const TaskFailedEvent& e) { /* ... */ });
pool.start();
if (auto f = pool.submitWithFuture([] { return 42; }, "answer", Priority::High)) std::cout << f->get();
pool.shutdown();            // graceful; shutdownNow() cancels what is still queued
```

## Rules to know
- Event handlers run **synchronously on the emitting thread** (usually a worker) and may run concurrently: keep them short and thread-safe.
  Destroying a `Connection` waits for in-flight handlers on other threads, so observers can be destroyed safely.
- Do not call `shutdown()` from a pool thread (throws `std::logic_error`).
- With `OverflowPolicy::Block`, `submit()` called from a pool thread falls back to `Reject` to avoid self-deadlock.
- A task that throws is `Failed` and can be resubmitted; exceptions of `submitWithFuture` tasks go to the future.
- `waitForAll()` returns when every accepted task has finished *and* its events have been delivered.
- Windows: thread naming is a no-op and the DLL export path (`TK4CPP_TP_API`) is **not verified** (Linux/GCC only so far).

## Migration from the Poco based version
| Before | Now |
|---|---|
| `Poco::Runnable`, `Poco::Thread`, `Poco::Mutex/Condition`, `Poco::Timer` | `std::jthread`, `std::mutex`, `std::condition_variable`; no monitor timer (scaling is event driven) |
| `Poco::Notification*`, `addObserver(..., NObserver)` (`Notification.h`) | removed; use `pool.events().on<...>()` |
| `EventManager::instance().taskCompleted += ...` (global singleton) | `pool.events().on<TaskCompletedEvent>(...)` (one bus per pool) |
| `bool submit(...)` | `std::expected<void, SubmitError> submit(...)` (`[[nodiscard]]`) |
| `ThreadPoolConfig::monitorInterval` | removed; `idleTimeout` is now honoured; new `overflowPolicy` |
| event durations in `milliseconds`, `PoolEvent` getters | `microseconds`; plain public fields |
| `ITask : Poco::Runnable`, `setWorkerId()` | plain class; `run(const EventBus*, workerId)` |
| `ConsoleLogger` / `PoolLogger` duplicates | single `PoolLogger` (streams or custom sink, e.g. to a spdlog bridge) |

Old examples 002–005 used `Process`, `ProcessHandler`, `TaskHandler` (Poco based, not part of this module); they were replaced by
in-process equivalents. `include/old`, `news/`, the recorded run folders and the process simulators are not part of this package.
