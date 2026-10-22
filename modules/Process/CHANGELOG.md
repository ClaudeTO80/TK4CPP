# Changelog

## 3.0.0

### Fixed (defects of 2.x)
- Windows: `CreateProcessW` was given the bare name as application name, which disables PATH search (`cmd.exe` could not start). Executable is now resolved explicitly.
- Windows: an empty argument was dropped from the command line.
- Windows: `bInheritHandles=TRUE` leaked every inheritable handle of the host to the child; now an explicit handle list is used.
- POSIX: `wait()` and `is_running()` both called `waitpid` and could steal each other's status (`ECHILD`). A single monitor thread now owns waiting.
- POSIX: `kill(-pgid)` after the leader was reaped could hit a recycled pgid. Signals are sent only while the zombie is unreaped (`waitid(WNOWAIT)`).
- POSIX: descendants outliving the child kept the pipes open, hanging `wait()` forever; now swept at exit and, failing that, bounded by `output_drain_timeout`.
- POSIX: child inherited the host's blocked signals and ignored `SIGPIPE`, plus every non-CLOEXEC descriptor.
- POSIX: pipes could land on fd 0-2 if the host had them closed.
- macOS: `POSIX_SPAWN_CLOEXEC_DEFAULT` closed an inherited stdin.
- Capture was silently disabled whenever a callback or file was set; sinks are now independent.
- Output files were re-opened on every chunk; now opened once, natively (UTF-8 paths on Windows).
- Tests used `assert` (compiled out in Release). Replaced by a framework-free suite with a cross-platform helper child.

### Changed (API break)
- C++23; `start()`/`wait()`/`run_sync()` return `std::expected`; `Result::success` is now a method; `ExitStatus` replaces the magic `-999`; callbacks take `std::string_view`; `manage_process_tree` → `TreePolicy`.
- `terminate()` blocks until the child is gone.

### Added
- stdin pipe, `on_exit`, `std::stop_token` cancellation, `unset_environment`/`inherit_environment`, `kill_tree_on_exit`, `output_drain_timeout`, truncation/incomplete flags, `duration`, `tree_managed()`.
- fork/exec backend for platforms without `posix_spawn_file_actions_addchdir_np`; Windows `WM_CLOSE` graceful stop; shared-library export macros; sanitizer option.
