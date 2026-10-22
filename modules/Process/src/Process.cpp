/**
 * @file Process.cpp
 * @brief Platform independent part of TK4CPP::Process: state machine, output routing, run_sync().
 */
#include "ProcessImpl.hpp"

#include <algorithm>
#include <utility>

namespace TK4CPP::Process {

namespace detail {
thread_local const void* tl_worker_owner = nullptr;
} // namespace detail

namespace {

using Clock = std::chrono::steady_clock;

/** @brief Rejects requests that cannot be honoured identically on every platform. */
Expected<void> validate(const std::string& exe, const std::vector<std::string>& args, const Options& o) {
    using detail::fail;
    using detail::has_nul;
    if (exe.empty()) return fail(ErrorCode::InvalidArgument, 0, "Executable path is empty");
    if (has_nul(exe)) return fail(ErrorCode::InvalidArgument, 0, "Executable contains an embedded NUL");
    for (const auto& a : args)
        if (has_nul(a)) return fail(ErrorCode::InvalidArgument, 0, "Argument contains an embedded NUL");
    if (has_nul(o.working_directory) || has_nul(o.stdout_filepath) || has_nul(o.stderr_filepath))
        return fail(ErrorCode::InvalidArgument, 0, "Path contains an embedded NUL");
    for (const auto& e : o.environment) {
        const auto eq = e.find('=');
        if (eq == std::string::npos || eq == 0 || has_nul(e))
            return fail(ErrorCode::InvalidArgument, 0, "Environment entries must use NAME=VALUE: '" + e + "'");
    }
    for (const auto& u : o.unset_environment)
        if (u.empty() || has_nul(u) || u.find('=') != std::string::npos)
            return fail(ErrorCode::InvalidArgument, 0, "Invalid name in unset_environment");
    return {};
}

} // namespace

// ============================================================================================
// Impl: lifecycle
// ============================================================================================

Process::Impl::Impl(std::string exe, std::vector<std::string> args, Options opts)
    : executable_(std::move(exe)), arguments_(std::move(args)), options_(std::move(opts)) {}

bool Process::Impl::wants_stdin_pipe() const noexcept {
    return options_.stdin_mode == StdinMode::Pipe || !options_.stdin_data.empty();
}

Expected<void> Process::Impl::start() {
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ != State::Created)
        return detail::fail(ErrorCode::InvalidState, 0, "Process can only be started once");
    auto r = start_locked();
    if (!r) {
        state_ = State::Failed;
        out_file_.close();
        err_file_.close();
        stdin_queue.shut();
        cv_.notify_all();
        {
            std::lock_guard<std::mutex> d(diag_mu_);
            error_ = r.error();
        }
    }
    return r;
}

Expected<void> Process::Impl::start_locked() {
    if (auto v = validate(executable_, arguments_, options_); !v) return v;

    if (!options_.stdout_filepath.empty())
        if (auto r = out_file_.open(options_.stdout_filepath, options_.append_to_files); !r) return r;
    if (!options_.merge_stderr_to_stdout && !options_.stderr_filepath.empty()) {
        if (options_.stderr_filepath == options_.stdout_filepath) err_file_shared_ = true;
        else if (auto r = err_file_.open(options_.stderr_filepath, options_.append_to_files); !r) return r;
    }

    if (auto r = platform_start(); !r) return r;
    started_at_ = Clock::now();
    state_ = State::Running;

    if (wants_stdin_pipe()) {
        if (!options_.stdin_data.empty()) {
            (void)stdin_queue.push(options_.stdin_data, 0, /*force=*/true);
            stdin_queue.close();
            platform_wake_stdin();
        }
    } else {
        stdin_queue.shut();
    }

    try {
        monitor_ = std::thread([self = shared_from_this()] {
            detail::tl_worker_owner = self.get();
            self->monitor_main();
        });
    } catch (const std::exception& ex) {
        // Extremely rare (resource exhaustion): do not leave an unobserved child behind.
        platform_kill_locked();
        platform_reap_locked();
        reaped_ = true;
        stdin_queue.shut();
        platform_wake_stdin();
        platform_drain(std::chrono::milliseconds(500));
        platform_release();
        return detail::fail(ErrorCode::SpawnFailed, 0, std::string("Cannot create monitor thread: ") + ex.what());
    }
    return {};
}

void Process::Impl::monitor_main() {
    platform_wait_exit();
    {
        std::lock_guard<std::mutex> lk(mu_);
        // The direct child is still a zombie here, so its pid/pgid cannot be recycled yet:
        // this is the only moment where sweeping leftover descendants is race-free.
        if (tree_managed_ && options_.kill_tree_on_exit) platform_kill_locked();
        status_ = platform_reap_locked();
        reaped_ = true;
        ended_at_ = Clock::now();
        cv_.notify_all();
    }
    stdin_queue.shut();
    platform_wake_stdin();
    const bool drained = platform_drain(options_.output_drain_timeout);
    platform_release();
    if (!drained)
        note_error({ErrorCode::WaitFailed, 0,
                    "Output pipes were still open after the child exited (detached descendant?); output abandoned"});
    {
        std::lock_guard<std::mutex> lk(mu_);
        output_incomplete_ = !drained;
        state_ = State::Finished;
        cv_.notify_all();
    }
    if (options_.on_exit) {
        try { options_.on_exit(status_); } catch (...) {}
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        monitor_done_ = true;
        cv_.notify_all();
    }
}

bool Process::Impl::is_running() const {
    std::lock_guard<std::mutex> lk(mu_);
    return state_ == State::Running;
}

Expected<ExitStatus> Process::Impl::wait() {
    std::unique_lock<std::mutex> lk(mu_);
    if (state_ == State::Created || state_ == State::Failed)
        return detail::fail(ErrorCode::InvalidState, 0, "Process has not been started");
    cv_.wait(lk, [&] { return state_ == State::Finished; });
    return status_;
}

bool Process::Impl::wait_for(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(mu_);
    if (state_ == State::Created || state_ == State::Failed) return false;
    if (timeout < std::chrono::milliseconds::zero()) timeout = std::chrono::milliseconds::zero();
    if (timeout >= std::chrono::hours(24 * 365)) {   // effectively infinite; also avoids time_point overflow
        cv_.wait(lk, [&] { return state_ == State::Finished; });
        return true;
    }
    return cv_.wait_for(lk, timeout, [&] { return state_ == State::Finished; });
}

void Process::Impl::kill() {
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ == State::Running && !reaped_) platform_kill_locked();
}

bool Process::Impl::terminate() {
    std::unique_lock<std::mutex> lk(mu_);
    if (state_ != State::Running) return state_ == State::Finished;
    bool graceful = true;
    if (!reaped_) {
        if (platform_terminate_locked()) {
            const auto grace = std::max(options_.terminate_grace, std::chrono::milliseconds::zero());
            graceful = cv_.wait_for(lk, grace, [&] { return reaped_; });
        } else {
            graceful = false;
        }
        if (!graceful && !reaped_) platform_kill_locked();
    }
    // Contract: when terminate() returns the child is gone and its output drained, so that
    // exit_status() / read_stdout() are final. A worker thread cannot wait for itself.
    if (detail::tl_worker_owner != this)
        cv_.wait(lk, [&] { return state_ == State::Finished; });
    return graceful;
}

void Process::Impl::shutdown() {
    const bool in_worker = detail::tl_worker_owner == this;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_ == State::Running && !reaped_) platform_kill_locked();
    }
    if (in_worker) {
        // Destroyed from one of our own callbacks: we cannot wait for ourselves. The shared
        // state stays alive until the worker threads unwind.
        if (monitor_.joinable() && monitor_.get_id() != std::this_thread::get_id()) monitor_.detach();
        return;
    }
    if (monitor_.joinable()) monitor_.join();
}

Process::Impl::~Impl() {
    if (monitor_.joinable()) {
        if (monitor_.get_id() == std::this_thread::get_id()) monitor_.detach();
        else monitor_.join();
    }
    platform_release();
    platform_destroy_native();
}

// ============================================================================================
// Impl: stdin and accessors
// ============================================================================================

Expected<void> Process::Impl::write_stdin(std::string_view data) {
    if (data.empty()) return {};
    if (!wants_stdin_pipe())
        return detail::fail(ErrorCode::StdinClosed, 0, "stdin is not in StdinMode::Pipe");
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_ != State::Running)
            return detail::fail(ErrorCode::InvalidState, 0, "Process is not running");
    }
    auto r = stdin_queue.push(data, options_.max_stdin_queue_bytes);
    if (r) platform_wake_stdin();
    return r;
}

void Process::Impl::close_stdin() {
    stdin_queue.close();
    platform_wake_stdin();
}

long long Process::Impl::pid() const { std::lock_guard<std::mutex> lk(mu_); return pid_; }

std::optional<ExitStatus> Process::Impl::exit_status() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ != State::Finished) return std::nullopt;
    return status_;
}

bool Process::Impl::tree_managed() const { std::lock_guard<std::mutex> lk(mu_); return tree_managed_; }
bool Process::Impl::output_incomplete() const { std::lock_guard<std::mutex> lk(mu_); return output_incomplete_; }

std::chrono::milliseconds Process::Impl::duration() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ == State::Created || state_ == State::Failed) return {};
    const auto end = reaped_ ? ended_at_ : Clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - started_at_);
}

std::string Process::Impl::read_stdout() const { std::lock_guard<std::mutex> l(out_mu_); return out_buf_; }
std::string Process::Impl::read_stderr() const { std::lock_guard<std::mutex> l(out_mu_); return err_buf_; }
bool Process::Impl::stdout_truncated() const { std::lock_guard<std::mutex> l(out_mu_); return out_trunc_; }
bool Process::Impl::stderr_truncated() const { std::lock_guard<std::mutex> l(out_mu_); return err_trunc_; }

std::optional<Error> Process::Impl::last_error() const {
    std::lock_guard<std::mutex> l(diag_mu_);
    return error_;
}

void Process::Impl::note_error(Error e) {
    std::lock_guard<std::mutex> l(diag_mu_);
    if (!error_) error_ = std::move(e);   // keep the first (root-cause) problem
}

void Process::Impl::consume(std::string_view data, bool is_err) {
    if (data.empty()) return;
    if (options_.merge_stderr_to_stdout) is_err = false;
    bool file_failed = false;
    {
        std::lock_guard<std::mutex> l(out_mu_);
        const std::size_t cap = options_.max_capture_bytes;
        if (cap != 0) {
            auto& buf = is_err ? err_buf_ : out_buf_;
            auto& trunc = is_err ? err_trunc_ : out_trunc_;
            const std::size_t room = buf.size() < cap ? cap - buf.size() : 0;
            const std::size_t take = std::min(room, data.size());
            buf.append(data.data(), take);
            if (take < data.size()) trunc = true;
        }
        detail::FileSink& f = (is_err && !err_file_shared_) ? err_file_ : out_file_;
        if (f.is_open() && !f.write(data)) {
            f.close();   // stop hammering a broken target; the child keeps running
            file_failed = true;
        }
    }
    if (file_failed)
        note_error({ErrorCode::OutputFileFailed, 0, "Writing the process output file failed; redirection stopped"});
    const auto& cb = is_err ? options_.on_stderr : options_.on_stdout;
    if (cb) {
        std::lock_guard<std::mutex> l(cb_mu_);
        try { cb(data); } catch (...) { /* a user callback must never kill the reader */ }
    }
}

// ============================================================================================
// Public facade
// ============================================================================================

Process::Process(std::string executable, std::vector<std::string> arguments, Options options)
    : m_impl(std::make_shared<Impl>(std::move(executable), std::move(arguments), std::move(options))) {}

Process::~Process() { if (m_impl) m_impl->shutdown(); }
Process::Process(Process&&) noexcept = default;

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        if (m_impl) m_impl->shutdown();
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

namespace {
Expected<void> moved_from() { return detail::fail(ErrorCode::InvalidState, 0, "Invalid moved-from Process"); }
}

Expected<void> Process::start() { return m_impl ? m_impl->start() : moved_from(); }
bool Process::is_running() const { return m_impl && m_impl->is_running(); }
Expected<ExitStatus> Process::wait() {
    if (!m_impl) return std::unexpected(moved_from().error());
    return m_impl->wait();
}
bool Process::wait_for(std::chrono::milliseconds t) { return m_impl && m_impl->wait_for(t); }
bool Process::terminate() { return m_impl && m_impl->terminate(); }
void Process::kill() { if (m_impl) m_impl->kill(); }
Expected<void> Process::write_stdin(std::string_view d) { return m_impl ? m_impl->write_stdin(d) : moved_from(); }
void Process::close_stdin() { if (m_impl) m_impl->close_stdin(); }
long long Process::pid() const { return m_impl ? m_impl->pid() : -1; }
std::optional<ExitStatus> Process::exit_status() const { return m_impl ? m_impl->exit_status() : std::nullopt; }
bool Process::tree_managed() const { return m_impl && m_impl->tree_managed(); }
std::string Process::read_stdout() const { return m_impl ? m_impl->read_stdout() : std::string{}; }
std::string Process::read_stderr() const { return m_impl ? m_impl->read_stderr() : std::string{}; }
bool Process::stdout_truncated() const { return m_impl && m_impl->stdout_truncated(); }
bool Process::stderr_truncated() const { return m_impl && m_impl->stderr_truncated(); }
bool Process::output_incomplete() const { return m_impl && m_impl->output_incomplete(); }
std::optional<Error> Process::last_error() const { return m_impl ? m_impl->last_error() : std::nullopt; }

Expected<Result> Process::run_sync(std::string executable, std::vector<std::string> arguments,
                                   std::chrono::milliseconds timeout, Options options) {
    const std::stop_token cancel = options.cancel;
    Process p(std::move(executable), std::move(arguments), std::move(options));
    if (auto s = p.start(); !s) return std::unexpected(s.error());

    Result r;
    const auto begin = Clock::now();
    const bool unlimited = timeout >= std::chrono::hours(24 * 365);
    constexpr auto kSlice = std::chrono::milliseconds(20);   // cancel/timeout polling granularity only
    for (;;) {
        if (p.wait_for(kSlice)) break;
        if (cancel.stop_requested()) { r.cancelled = true; break; }
        if (!unlimited && Clock::now() - begin >= timeout) { r.timed_out = true; break; }
    }
    if (r.cancelled || r.timed_out) (void)p.terminate();

    auto st = p.wait();
    r.status = st ? *st : ExitStatus{};
    r.standard_output = p.read_stdout();
    r.standard_error = p.read_stderr();
    r.stdout_truncated = p.stdout_truncated();
    r.stderr_truncated = p.stderr_truncated();
    r.output_incomplete = p.output_incomplete();
    r.pid = p.pid();
    r.duration = p.m_impl->duration();
    r.warning = p.last_error();
    return r;
}

} // namespace TK4CPP::Process
