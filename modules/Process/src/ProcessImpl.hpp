/**
 * @file ProcessImpl.hpp
 * @brief Internal implementation details shared by the platform back-ends. Not installed.
 */
#ifndef TK4CPP_PROCESS_IMPL_HPP
#define TK4CPP_PROCESS_IMPL_HPP

#include "TK4CPP/Process.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace TK4CPP::Process {
namespace detail {

/** @brief Builds a failed Expected with a fully populated Error. */
inline std::unexpected<Error> fail(ErrorCode code, int native, std::string message) {
    return std::unexpected<Error>(Error{code, native, std::move(message)});
}

/** @brief True if @p s contains an embedded NUL (would silently truncate a C string). */
inline bool has_nul(std::string_view s) noexcept { return s.find('\0') != std::string_view::npos; }

/** @brief Counting latch with a timed wait (std::latch has none). */
class Latch {
public:
    void add(int n = 1) { std::lock_guard<std::mutex> l(m_); count_ += n; }
    void count_down() {
        std::lock_guard<std::mutex> l(m_);
        if (--count_ <= 0) cv_.notify_all();
    }
    /** @return true if the counter reached zero before the timeout. */
    bool wait_for(std::chrono::milliseconds t) {
        std::unique_lock<std::mutex> l(m_);
        return cv_.wait_for(l, t, [&] { return count_ <= 0; });
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    int count_ = 0;
};

/**
 * @brief Thread-safe producer/consumer buffer behind Process::write_stdin().
 *
 * Producers never block. The consumer is the platform I/O machinery, which drains the
 * queue as fast as the child accepts data.
 */
class StdinQueue {
public:
    Expected<void> push(std::string_view data, std::size_t limit, bool force = false) {
        std::lock_guard<std::mutex> l(m_);
        if (shut_ || closing_) return fail(ErrorCode::StdinClosed, 0, "stdin is closed");
        if (!force && unsent_ + data.size() > limit)
            return fail(ErrorCode::QueueFull, 0, "stdin queue limit exceeded");
        pending_.append(data);
        unsent_ += data.size();
        cv_.notify_all();
        return {};
    }
    /** @brief No further data will be pushed; the consumer closes the pipe once drained. */
    void close() { std::lock_guard<std::mutex> l(m_); closing_ = true; cv_.notify_all(); }
    /** @brief The child is gone: discard pending data and reject new writes. */
    void shut() { std::lock_guard<std::mutex> l(m_); shut_ = true; closing_ = true; pending_.clear(); unsent_ = 0; cv_.notify_all(); }
    /** @brief Non-blocking: swaps all pending bytes into @p dst (which must be empty). */
    bool take(std::string& dst) {
        std::lock_guard<std::mutex> l(m_);
        if (pending_.empty()) return false;
        dst.swap(pending_);
        return true;
    }
    /** @brief Records bytes actually delivered to the pipe (backpressure accounting). */
    void note_sent(std::size_t n) { std::lock_guard<std::mutex> l(m_); unsent_ = n > unsent_ ? 0 : unsent_ - n; }
    /** @brief True once close() was requested and nothing is pending. */
    bool finished() const { std::lock_guard<std::mutex> l(m_); return closing_ && pending_.empty(); }
    bool is_shut() const { std::lock_guard<std::mutex> l(m_); return shut_; }
    /** @brief Blocking consumer API: waits for data (-> true, in @p dst) or end of stream (-> false). */
    bool wait_take(std::string& dst) {
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [&] { return !pending_.empty() || closing_; });
        if (pending_.empty()) return false;
        dst.swap(pending_);
        return true;
    }
private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::string pending_;
    std::size_t unsent_ = 0;
    bool closing_ = false, shut_ = false;
};

/** @brief Minimal native-file output sink (UTF-8 paths on every platform, no iostream). */
class FileSink {
public:
    FileSink() = default;
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    ~FileSink() { close(); }
    Expected<void> open(const std::string& utf8_path, bool append);
    [[nodiscard]] bool is_open() const noexcept;
    bool write(std::string_view data) noexcept;
    void close() noexcept;
private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

/** @brief Thread-local marker: set on every internal worker thread of an Impl. */
extern thread_local const void* tl_worker_owner;

} // namespace detail

/** @brief Shared implementation object. Lives as long as the Process or any worker thread. */
class Process::Impl : public std::enable_shared_from_this<Impl> {
public:
    struct Native;   ///< Platform specific state, defined in the back-end translation unit.

    Impl(std::string exe, std::vector<std::string> args, Options opts);
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // ---- public API forwarding targets -------------------------------------------------
    Expected<void> start();
    bool is_running() const;
    Expected<ExitStatus> wait();
    bool wait_for(std::chrono::milliseconds timeout);
    bool terminate();
    void kill();
    void shutdown();                        ///< Destructor path: kill, wait, join.
    Expected<void> write_stdin(std::string_view data);
    void close_stdin();
    long long pid() const;
    std::optional<ExitStatus> exit_status() const;
    bool tree_managed() const;
    std::string read_stdout() const;
    std::string read_stderr() const;
    bool stdout_truncated() const;
    bool stderr_truncated() const;
    bool output_incomplete() const;
    std::optional<Error> last_error() const;
    std::chrono::milliseconds duration() const;

    // ---- used by the platform back-ends ------------------------------------------------
    /** @brief Delivers a chunk to capture buffer, file sink and callback. Called from I/O threads. */
    void consume(std::string_view data, bool is_err);
    void note_error(Error e);
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    [[nodiscard]] const std::string& executable() const noexcept { return executable_; }
    [[nodiscard]] const std::vector<std::string>& arguments() const noexcept { return arguments_; }
    [[nodiscard]] bool wants_stdin_pipe() const noexcept;

    detail::StdinQueue stdin_queue;
    long long pid_ = -1;                    ///< Written by platform_start() only.
    bool tree_managed_ = false;             ///< Written by platform_start() only.

    // ---- platform hooks (ProcessPosix.cpp / ProcessWin32.cpp) -----------------------------
    Expected<void> platform_start();        ///< Spawn the child and start I/O workers. mu_ is held.
    void platform_wait_exit();              ///< Block until the child has exited (without reaping on POSIX).
    bool platform_terminate_locked();       ///< Graceful request. @return false if none is possible. mu_ held.
    void platform_kill_locked();            ///< Hard kill of child (+tree). mu_ held, child not yet reaped.
    ExitStatus platform_reap_locked();      ///< Collect the status. mu_ held.
    bool platform_drain(std::chrono::milliseconds timeout); ///< Wait for EOF on pipes; abandon after timeout.
    void platform_wake_stdin();             ///< stdin_queue changed.
    void platform_release();                ///< Close every OS handle/descriptor (idempotent).
    void platform_destroy_native();         ///< Free the Native block (no workers may be running).

private:
    enum class State { Created, Running, Finished, Failed };
    void monitor_main();
    Expected<void> start_locked();

    const std::string executable_;
    const std::vector<std::string> arguments_;
    const Options options_;

    mutable std::mutex mu_;
    mutable std::condition_variable cv_;
    State state_ = State::Created;
    bool reaped_ = false;
    bool monitor_done_ = false;
    bool output_incomplete_ = false;
    ExitStatus status_;
    std::chrono::steady_clock::time_point started_at_{}, ended_at_{};
    std::thread monitor_;

    mutable std::mutex diag_mu_;
    std::optional<Error> error_;

    mutable std::mutex out_mu_;
    std::string out_buf_, err_buf_;
    bool out_trunc_ = false, err_trunc_ = false;
    detail::FileSink out_file_, err_file_;
    bool err_file_shared_ = false;          ///< stderr_filepath == stdout_filepath: share one sink.
    std::mutex cb_mu_;

public:
    Native* native_ = nullptr;              ///< Owned; created by platform_start(), freed by platform_destroy_native().
};

} // namespace TK4CPP::Process
#endif
