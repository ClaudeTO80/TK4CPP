/**
 * @file Process.hpp
 * @brief Cross-platform child process execution for the TK4CPP framework.
 *
 * Windows is backed by @c CreateProcessW plus a Job Object, POSIX by @c posix_spawn
 * (or @c fork/exec where @c posix_spawn cannot express the request). The module has no
 * third-party dependencies and never goes through a shell: arguments are passed as an
 * argv vector.
 *
 * @par Error handling
 * Operations that can fail return @c std::expected<T, Error>. A child that runs and
 * exits non-zero is *not* an error: it is reported through @ref ExitStatus.
 *
 * @par Threading
 * Every public member of @ref Process is thread-safe. Callbacks run on internal worker
 * threads; see @ref Options::on_stdout.
 */
#ifndef TK4CPP_PROCESS_HPP
#define TK4CPP_PROCESS_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32) && defined(TK4CPP_PROCESS_SHARED)
#  ifdef TK4CPP_PROCESS_BUILDING
#    define TK4CPP_PROCESS_API __declspec(dllexport)
#  else
#    define TK4CPP_PROCESS_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define TK4CPP_PROCESS_API __attribute__((visibility("default")))
#else
#  define TK4CPP_PROCESS_API
#endif

#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4251)
#endif

namespace TK4CPP::Process {

/** @brief Machine-readable failure category. */
enum class ErrorCode {
    InvalidArgument,        ///< Empty executable, embedded NUL, malformed environment entry, invalid UTF-8, ...
    InvalidState,           ///< Operation not allowed in the current lifecycle state (e.g. start() twice).
    ExecutableNotFound,     ///< Executable could not be located.
    PermissionDenied,       ///< Executable exists but cannot be executed.
    WorkingDirectoryFailed, ///< Working directory does not exist or is not accessible.
    SpawnFailed,            ///< The operating system refused to create the process.
    PipeFailed,             ///< Creating or configuring an I/O pipe failed.
    OutputFileFailed,       ///< Opening or writing a redirection file failed.
    ProcessTreeFailed,      ///< Process-tree ownership was required (TreePolicy::Required) but unavailable.
    WaitFailed,             ///< The exit status could not be retrieved (e.g. child reaped by someone else).
    StdinClosed,            ///< Writing to a closed or broken stdin pipe.
    QueueFull,              ///< Pending stdin data would exceed Options::max_stdin_queue_bytes.
    Unsupported             ///< Request not expressible on this platform.
};

/** @brief Structured error carrying the OS error code when available. */
struct Error {
    ErrorCode code = ErrorCode::SpawnFailed;
    int native_error = 0;   ///< @c errno on POSIX, @c GetLastError() on Windows; 0 when not applicable.
    std::string message;    ///< Human readable, UTF-8.
};

/** @brief Alias for @c std::expected specialised with @ref Error. */
template <class T>
using Expected = std::expected<T, Error>;

/** @brief How the child's standard input is connected. */
enum class StdinMode {
    Null,    ///< Connected to the null device (default). The child sees immediate EOF.
    Inherit, ///< Shares the parent's stdin.
    Pipe     ///< A pipe fed through Process::write_stdin() / Options::stdin_data.
};

/** @brief Process-tree ownership policy. */
enum class TreePolicy {
    None,       ///< Only the direct child is controlled.
    BestEffort, ///< Own the whole tree when possible; silently degrade to the direct child otherwise (default).
    Required    ///< Own the whole tree or fail Process::start() with ErrorCode::ProcessTreeFailed.
};

/** @brief How a process ended. */
struct ExitStatus {
    /** @brief Termination kind. */
    enum class Kind { Exited, Signaled, Unknown };
    Kind kind = Kind::Unknown;
    int code = -1;   ///< Exit code when kind == Exited.
    int signal = 0;  ///< Terminating signal number (POSIX) when kind == Signaled.

    /** @brief True when the process exited normally with code 0. */
    [[nodiscard]] bool success() const noexcept { return kind == Kind::Exited && code == 0; }
    /** @brief Legacy single-integer view: the exit code, or the negated signal number, or -1 if unknown. */
    [[nodiscard]] int legacy_code() const noexcept {
        return kind == Kind::Exited ? code : (kind == Kind::Signaled ? -signal : -1);
    }
};

/** @brief Outcome of Process::run_sync() for a child that was actually launched. */
struct Result {
    ExitStatus status;                       ///< How the child ended (also set after timeout/cancel kills).
    std::string standard_output;             ///< Captured stdout (bounded by Options::max_capture_bytes).
    std::string standard_error;              ///< Captured stderr (empty when merged into stdout).
    bool stdout_truncated = false;           ///< Capture limit reached; later bytes were dropped from RAM only.
    bool stderr_truncated = false;           ///< Same for stderr.
    bool timed_out = false;                  ///< The timeout elapsed and the child was terminated.
    bool cancelled = false;                  ///< Options::cancel was triggered and the child was terminated.
    bool output_incomplete = false;          ///< Output pipes were still held open by a detached descendant.
    long long pid = -1;                      ///< OS process id of the child.
    std::chrono::milliseconds duration{0};   ///< Wall-clock time between launch and exit.
    std::optional<Error> warning;            ///< Non-fatal problem (e.g. output file write failure).

    /** @brief True only for a clean exit code 0 that was neither timed out nor cancelled. */
    [[nodiscard]] bool success() const noexcept { return status.success() && !timed_out && !cancelled; }
};

/** @brief Receives raw output chunks (not line-aligned). The view is valid only during the call. */
using OutputCallback = std::function<void(std::string_view)>;
/** @brief Invoked once when the child has exited and its output is fully drained (or abandoned). */
using ExitCallback = std::function<void(const ExitStatus&)>;

/** @brief Launch, I/O and lifecycle policy. Aggregate: designated initialisers work. */
struct Options {
    /** @name Output routing
     *  stdout/stderr are always drained concurrently. Every enabled sink receives every byte
     *  (callback, file and in-memory capture are independent, not mutually exclusive).
     *  @{ */
    std::string stdout_filepath;          ///< UTF-8 path; stdout is also written here when non-empty.
    std::string stderr_filepath;          ///< UTF-8 path; ignored when merge_stderr_to_stdout is set.
    bool append_to_files = false;         ///< Append instead of truncating the files at start().
    OutputCallback on_stdout;             ///< Runs on an internal thread; calls are serialised across streams.
    OutputCallback on_stderr;             ///< Runs on an internal thread. Exceptions are swallowed.
    bool merge_stderr_to_stdout = false;  ///< Route stderr into the stdout pipe (ordering preserved).
    std::size_t max_capture_bytes = 16u * 1024u * 1024u; ///< Per-stream RAM cap; 0 disables capture.
    /** @} */

    /** @name Input
     *  @{ */
    StdinMode stdin_mode = StdinMode::Null;
    std::string stdin_data;               ///< If non-empty, implies StdinMode::Pipe: written then closed at start().
    std::size_t max_stdin_queue_bytes = 64u * 1024u * 1024u; ///< Backpressure limit for write_stdin().
    /** @} */

    /** @name Environment and location
     *  @{ */
    std::string working_directory;        ///< UTF-8; empty inherits the parent's directory.
    bool inherit_environment = true;      ///< When false the child starts with only @ref environment.
    std::vector<std::string> environment; ///< "NAME=VALUE" entries overriding/adding to the base environment.
    std::vector<std::string> unset_environment; ///< Variable names removed from the base environment.
    /** @} */

    /** @name Lifecycle
     *  @{ */
    TreePolicy tree_policy = TreePolicy::BestEffort; ///< Process group (POSIX) / Job Object (Windows).
    std::chrono::milliseconds terminate_grace{1500}; ///< Graceful-stop window before Process::terminate() kills.
    std::chrono::milliseconds output_drain_timeout{2000}; ///< Max wait for pipes to reach EOF after the child exits.
    bool kill_tree_on_exit = true;        ///< Kill descendants still alive when the direct child exits (needs an owned tree).
    bool close_inherited_handles = true;  ///< Do not leak unrelated parent descriptors/handles to the child.
    bool create_no_window = true;         ///< Windows only: hide the console window of console programs.
    ExitCallback on_exit;                 ///< Optional completion notification (internal thread).
    std::stop_token cancel;               ///< run_sync(): requesting stop terminates the child.
    /** @} */
};

/**
 * @brief Owner of one child process.
 *
 * Lifecycle: construct -> start() -> (write_stdin / wait / terminate / kill) -> destroy.
 * A Process can be started only once. Destroying a running Process kills it (and its tree
 * when owned) and waits until its internal threads stop; after the destructor returns no
 * callback will be invoked. Do not destroy a Process from inside one of its own callbacks.
 */
class TK4CPP_PROCESS_API Process {
public:
    /**
     * @brief Prepares (but does not launch) a process.
     * @param executable Program name (searched in PATH) or path. UTF-8.
     * @param arguments  argv[1..]; passed verbatim, never interpreted by a shell. UTF-8.
     * @param options    Launch/I-O policy.
     */
    explicit Process(std::string executable, std::vector<std::string> arguments = {}, Options options = {});
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;

    /** @brief Launches the child. Fails with a typed @ref Error and leaves no resource behind. */
    Expected<void> start();

    /** @brief True from a successful start() until the child has exited and its output is drained. */
    [[nodiscard]] bool is_running() const;

    /** @brief Blocks until the child exited and output is drained. Safe from several threads. */
    Expected<ExitStatus> wait();

    /** @brief Waits at most @p timeout. @return true if the process finished within the time. */
    bool wait_for(std::chrono::milliseconds timeout);

    /**
     * @brief Asks the child (and its tree) to stop, escalating to kill() after Options::terminate_grace.
     *
     * POSIX: SIGTERM to the process group, then SIGKILL. Windows: WM_CLOSE to the windows owned by
     * the tree; console programs without windows are killed immediately (there is no portable
     * console-independent graceful signal).
     * Blocks until the child is gone and its output drained.
     * @return true when the child exited within the grace period, false if it had to be killed.
     */
    bool terminate();

    /** @brief Forcefully kills the child and, when owned, its whole tree. Non-blocking. */
    void kill();

    /**
     * @brief Queues data for the child's stdin (StdinMode::Pipe). Never blocks.
     * @return ErrorCode::StdinClosed when closed/broken, ErrorCode::QueueFull on backpressure.
     */
    Expected<void> write_stdin(std::string_view data);

    /** @brief Closes stdin after already queued data has been delivered (child sees EOF). */
    void close_stdin();

    /** @brief OS process id, or -1 before start(). Remains available after exit. */
    [[nodiscard]] long long pid() const;
    /** @brief Exit status once finished, empty while running or never started. */
    [[nodiscard]] std::optional<ExitStatus> exit_status() const;
    /** @brief True if the whole process tree is owned (false when degraded or TreePolicy::None). */
    [[nodiscard]] bool tree_managed() const;

    /** @brief Bytes captured so far from stdout (bounded by Options::max_capture_bytes). */
    [[nodiscard]] std::string read_stdout() const;
    /** @brief Bytes captured so far from stderr. */
    [[nodiscard]] std::string read_stderr() const;
    [[nodiscard]] bool stdout_truncated() const;
    [[nodiscard]] bool stderr_truncated() const;
    /** @brief True if pipes had to be abandoned because a detached descendant kept them open. */
    [[nodiscard]] bool output_incomplete() const;
    /** @brief Most recent error or warning recorded by the background machinery, if any. */
    [[nodiscard]] std::optional<Error> last_error() const;

    /**
     * @brief Convenience: start, wait with timeout, collect output.
     *
     * Returns an @ref Error only when the process could not be launched. A timeout or
     * cancellation terminates the child and is reported through Result::timed_out /
     * Result::cancelled.
     * @param timeout Use @c std::chrono::milliseconds::max() for no limit.
     */
    static Expected<Result> run_sync(std::string executable,
                                     std::vector<std::string> arguments = {},
                                     std::chrono::milliseconds timeout = std::chrono::milliseconds::max(),
                                     Options options = {});

    /** @brief Implementation detail (opaque to users). */
    class Impl;
private:
    std::shared_ptr<Impl> m_impl;
};

} // namespace TK4CPP::Process

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
#endif // TK4CPP_PROCESS_HPP
