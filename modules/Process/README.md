# TK4CPP::Process

Cross-platform child-process execution for C++23. No third-party dependencies, no shell.

| Platform | Spawn | Tree ownership | Graceful stop |
|----------|-------|----------------|---------------|
| Linux / macOS / *BSD | `posix_spawn` (fork/exec fallback) | process group | `SIGTERM` → `SIGKILL` |
| Windows | `CreateProcessW` + explicit handle list | Job Object | `WM_CLOSE` → `TerminateJobObject` |

## Features

- `std::expected<T, Error>` API with typed `ErrorCode` (not found, permission, bad cwd, ...). A child that exits non-zero is *not* an error.
- Thread-safe lifecycle: `start`, `is_running`, `wait`, `wait_for` (condition variable, no polling), `terminate`, `kill`.
- stdout/stderr drained concurrently (no pipe deadlocks); callback, file and bounded in-memory capture are independent sinks; truncation flags.
- stdin: `Null`, `Inherit`, or `Pipe` with a non-blocking `write_stdin()` + backpressure, `close_stdin()`, `Options::stdin_data`.
- Process-tree ownership; descendants are swept when the direct child exits (`kill_tree_on_exit`).
- Resilience: output drain timeout (a detached descendant can no longer hang `wait()`), clean signal mask/`SIGPIPE` in the child, no leaked descriptors/handles, survives closed fd 0-2, exit status lost to a foreign `waitpid`/ignored `SIGCHLD` is reported instead of crashing.
- `run_sync()` with timeout and `std::stop_token` cancellation; `on_exit` callback for async use.
- Install/export package, shared-library export macros, CTest suite (also run against the fork/exec path).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /opt/tk4cpp
# sanitizers: -DTK4CPP_PROCESS_SANITIZE=address,undefined   (or thread)
```

Requires a C++23 compiler with `<expected>` (GCC 12+, MSVC 19.33+, Clang with a libstdc++/libc++ that ships it; note Clang 18 + libstdc++ 13 does not expose `std::expected`).

```cmake
find_package(TK4CPPProcess CONFIG REQUIRED)
target_link_libraries(app PRIVATE TK4CPP::Process)
```

## Usage

```cpp
#include <TK4CPP/Process.hpp>
using namespace TK4CPP::Process;

Options opt;
opt.working_directory = "/srv/build";
opt.environment = {"CI=1"};
opt.on_stdout = [](std::string_view chunk) { /* stream */ };

auto r = Process::run_sync("git", {"status", "--short"}, std::chrono::seconds(10), opt);
if (!r)                { /* could not launch: r.error().code / .message */ }
else if (r->timed_out) { /* killed after 10 s */ }
else if (!r->success()){ /* r->status.code or r->status.signal */ }
```

Async with stdin:

```cpp
Options o; o.stdin_mode = StdinMode::Pipe; o.on_exit = [](const ExitStatus&){ /* ... */ };
Process p("sort", {}, o);
if (auto s = p.start(); !s) return report(s.error());
p.write_stdin("b\na\n"); p.close_stdin();
p.wait();               // output complete here
auto text = p.read_stdout();
```

## Behaviour notes

- Arguments are verbatim (UTF-8). Windows `.bat`/`.cmd`: arguments containing `" % ^ & | < > CR LF` are rejected. Windows only auto-appends `.exe`.
- Relative executables are resolved against the *parent's* `PATH`; with `working_directory` set on POSIX a relative path (with `/`) is resolved from that directory.
- Callbacks run on internal threads, serialised across stdout/stderr, exceptions are swallowed. Do not destroy the `Process` from a callback. After `~Process()` returns no callback runs.
- `terminate()` blocks until the child is gone and output drained; returns `false` if it had to escalate to kill. On Windows console programs without windows are killed immediately (no portable graceful signal).
- `TreePolicy::BestEffort` (default) silently falls back to the direct child if a Job Object cannot be assigned (nested-job restrictions); check `tree_managed()`. Use `Required` to fail instead.
- `StdinMode::Inherit` with a tree-managed child on a POSIX terminal puts the child in a background process group; a read from the tty will stop it (`SIGTTIN`). Use `TreePolicy::None` for interactive passthrough.
- To let a daemon outlive its launcher use `tree_policy = None` (or `kill_tree_on_exit = false`) and avoid pipes it would keep open (`output_drain_timeout` bounds the wait).
- If the host sets `SIGCHLD` to `SIG_IGN` (or reaps with `waitpid(-1)`), the exit status is unavailable: `ExitStatus::Kind::Unknown` plus `last_error()`.
