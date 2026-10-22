/**
 * @file ProcessPosix.cpp
 * @brief POSIX back-end (Linux, macOS, *BSD): posix_spawn with a fork/exec fallback.
 *
 * Design:
 *  - one monitor thread blocks in waitid(WNOWAIT) so the zombie (and its pid/pgid) stays
 *    reserved while signals may still be sent; the status is reaped under the lifecycle lock;
 *  - one I/O thread multiplexes stdout, stderr, stdin and a wake-up pipe with poll(2);
 *  - the child gets a clean signal mask, SIGPIPE reset to default, and (optionally) no
 *    inherited descriptors above 2.
 */
#ifndef _WIN32

#include "ProcessImpl.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#  include <sys/syscall.h>
#endif
#if defined(__APPLE__)
#  include <crt_externs.h>
#else
extern char** environ;
#endif

// ---- feature detection (CMake injects TK4CPP_PROCESS_HAVE_*; heuristics otherwise) ----------
#ifndef TK4CPP_PROCESS_HAVE_ADDCHDIR
#  if defined(__APPLE__) || (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29)))
#    define TK4CPP_PROCESS_HAVE_ADDCHDIR 1
#  else
#    define TK4CPP_PROCESS_HAVE_ADDCHDIR 0
#  endif
#endif
#ifndef TK4CPP_PROCESS_HAVE_ADDCLOSEFROM
#  if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
#    define TK4CPP_PROCESS_HAVE_ADDCLOSEFROM 1
#  else
#    define TK4CPP_PROCESS_HAVE_ADDCLOSEFROM 0
#  endif
#endif
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#  define TK4CPP_HAVE_WAITID 1
#else
#  define TK4CPP_HAVE_WAITID 0
#endif
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#  define TK4CPP_HAVE_PIPE2 1
#else
#  define TK4CPP_HAVE_PIPE2 0
#endif

namespace TK4CPP::Process {
namespace {

using detail::fail;

// ---- small helpers ---------------------------------------------------------------------------

[[maybe_unused]] const char* strerror_result(int rc, const char* buf) { return rc == 0 ? buf : "unknown error"; } // XSI
const char* strerror_result(const char* r, const char*) { return r; }                           // GNU

/** @brief Thread-safe strerror. */
std::string errno_text(int e) {
    char buf[256];
    buf[0] = '\0';
    return strerror_result(strerror_r(e, buf, sizeof buf), buf);
}

/** @brief Move-only RAII file descriptor. */
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) noexcept : fd_(fd) {}
    Fd(Fd&& o) noexcept : fd_(o.release()) {}
    Fd& operator=(Fd&& o) noexcept { if (this != &o) reset(o.release()); return *this; }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() { reset(); }
    [[nodiscard]] int get() const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ >= 0; }
    int release() noexcept { const int f = fd_; fd_ = -1; return f; }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) { int saved = errno; ::close(fd_); errno = saved; }
        fd_ = fd;
    }
private:
    int fd_ = -1;
};

/** @brief Best-effort enlargement of a pipe buffer (Linux): fewer context switches for chatty children. */
void grow_pipe(int fd) {
#if defined(__linux__) && defined(F_SETPIPE_SZ)
    fcntl(fd, F_SETPIPE_SZ, 1 << 20);
#else
    (void)fd;
#endif
}

bool set_nonblocking(int fd) {
    const int fl = fcntl(fd, F_GETFL, 0);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}

/** @brief Creates a close-on-exec pipe whose descriptors are both > 2 (safe if stdio is closed). */
bool make_pipe(Fd& r, Fd& w) {
    int p[2];
#if TK4CPP_HAVE_PIPE2
    if (pipe2(p, O_CLOEXEC) != 0) return false;
#else
    if (pipe(p) != 0) return false;
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
#endif
    Fd a(p[0]), b(p[1]);
    for (Fd* f : {&a, &b}) {
        if (f->get() < 3) {
            const int n = fcntl(f->get(), F_DUPFD_CLOEXEC, 3);
            if (n < 0) return false;
            f->reset(n);
        }
    }
    r = std::move(a);
    w = std::move(b);
    return true;
}

ErrorCode classify_spawn_errno(int e) {
    switch (e) {
        case ENOENT: case ENOTDIR: return ErrorCode::ExecutableNotFound;
        case EACCES: case EPERM:   return ErrorCode::PermissionDenied;
        case E2BIG:                return ErrorCode::InvalidArgument;
        default:                   return ErrorCode::SpawnFailed;
    }
}

std::vector<std::string> build_environment(const Options& o) {
    std::vector<std::string> env;
    if (o.inherit_environment) {
#if defined(__APPLE__)
        char** base = *_NSGetEnviron();
#else
        char** base = environ;
#endif
        for (char** p = base; p && *p; ++p) env.emplace_back(*p);
    }
    auto name_matches = [](const std::string& entry, std::string_view name) {
        return entry.size() > name.size() && entry[name.size()] == '=' &&
               entry.compare(0, name.size(), name) == 0;
    };
    for (const auto& u : o.unset_environment)
        std::erase_if(env, [&](const std::string& e) { return name_matches(e, u); });
    for (const auto& kv : o.environment) {
        const std::string_view name(kv.data(), kv.find('='));
        auto it = std::find_if(env.begin(), env.end(), [&](const std::string& e) { return name_matches(e, name); });
        if (it == env.end()) env.push_back(kv); else *it = kv;
    }
    return env;
}

/** @brief Everything needed to create the child; all pointers must stay valid during the call. */
struct SpawnRequest {
    const std::string* exe;
    char* const* argv;
    char* const* envp;
    const std::string* cwd;       ///< empty => inherit
    StdinMode in_mode;
    int in_r;                     ///< read end of the stdin pipe when in_mode == Pipe
    int out_w, err_w;
    bool group;                   ///< new process group
    bool close_others;
};

// ---- spawn back-end 1: posix_spawn --------------------------------------------------------------

Expected<pid_t> spawn_with_posix_spawn(const SpawnRequest& r) {
    posix_spawn_file_actions_t fa;
    if (int rc = posix_spawn_file_actions_init(&fa); rc != 0)
        return fail(ErrorCode::SpawnFailed, rc, "posix_spawn_file_actions_init: " + errno_text(rc));
    struct FaGuard { posix_spawn_file_actions_t* p; ~FaGuard() { posix_spawn_file_actions_destroy(p); } } fa_guard{&fa};
    posix_spawnattr_t at;
    if (int rc = posix_spawnattr_init(&at); rc != 0)
        return fail(ErrorCode::SpawnFailed, rc, "posix_spawnattr_init: " + errno_text(rc));
    struct AtGuard { posix_spawnattr_t* p; ~AtGuard() { posix_spawnattr_destroy(p); } } at_guard{&at};

    int rc = 0;
#if TK4CPP_PROCESS_HAVE_ADDCHDIR
    if (!r.cwd->empty()) rc = posix_spawn_file_actions_addchdir_np(&fa, r.cwd->c_str());
#endif
    if (rc == 0) {
        switch (r.in_mode) {
            case StdinMode::Null:    rc = posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0); break;
            case StdinMode::Pipe:    rc = posix_spawn_file_actions_adddup2(&fa, r.in_r, STDIN_FILENO); break;
            case StdinMode::Inherit:
#if defined(__APPLE__) && defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
                if (r.close_others) rc = posix_spawn_file_actions_addinherit_np(&fa, STDIN_FILENO);
#endif
                break;
        }
    }
    if (rc == 0) rc = posix_spawn_file_actions_adddup2(&fa, r.out_w, STDOUT_FILENO);
    if (rc == 0) rc = posix_spawn_file_actions_adddup2(&fa, r.err_w, STDERR_FILENO);
#if TK4CPP_PROCESS_HAVE_ADDCLOSEFROM
    if (rc == 0 && r.close_others) rc = posix_spawn_file_actions_addclosefrom_np(&fa, 3);
#endif
    if (rc != 0) return fail(ErrorCode::SpawnFailed, rc, "posix_spawn file actions: " + errno_text(rc));

    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    sigset_t empty, def;
    sigemptyset(&empty);
    sigemptyset(&def);
    sigaddset(&def, SIGPIPE);   // a host that ignores SIGPIPE must not make children ignore it too
    posix_spawnattr_setsigmask(&at, &empty);
    posix_spawnattr_setsigdefault(&at, &def);
    if (r.group) {
        flags = static_cast<short>(flags | POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&at, 0);
    }
#if defined(__APPLE__) && defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    if (r.close_others) flags = static_cast<short>(flags | POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
    posix_spawnattr_setflags(&at, flags);

    pid_t pid = -1;
    rc = posix_spawnp(&pid, r.exe->c_str(), &fa, &at, r.argv, r.envp);
    if (rc != 0)
        return fail(classify_spawn_errno(rc), rc, "Cannot execute '" + *r.exe + "': " + errno_text(rc));
    return pid;
}

// ---- spawn back-end 2: fork/exec (portable fallback, exact error reporting) ----------------------

/** @brief Resolves @p exe like execvp would, in the parent (allocation allowed here). */
Expected<std::string> resolve_executable(const std::string& exe) {
    if (exe.find('/') != std::string::npos) return exe;
    const char* path_env = std::getenv("PATH");
    const std::string path = path_env ? path_env : "/usr/local/bin:/usr/bin:/bin";
    int best_errno = ENOENT;
    std::size_t pos = 0;
    while (pos <= path.size()) {
        std::size_t end = path.find(':', pos);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(pos, end - pos);
        if (dir.empty()) dir = ".";
        const std::string cand = dir + "/" + exe;
        struct stat st{};
        if (stat(cand.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            if (access(cand.c_str(), X_OK) == 0) return cand;
            best_errno = EACCES;
        }
        pos = end + 1;
    }
    return fail(classify_spawn_errno(best_errno), best_errno, "Cannot execute '" + exe + "': " + errno_text(best_errno));
}

/** @brief Child side of fork(). Only async-signal-safe calls; never returns. */
[[noreturn]] void fork_child(const SpawnRequest& r, const char* path, int errfd, int max_fd, const sigset_t* empty) {
    auto die = [&](int stage) {
        int e[2] = {errno, stage};
        ssize_t ignored = write(errfd, e, sizeof e);
        (void)ignored;
        _exit(127);
    };
    if (r.group && setpgid(0, 0) != 0) die(1);
    switch (r.in_mode) {
        case StdinMode::Null: {
            const int fd = open("/dev/null", O_RDONLY);
            if (fd < 0) die(2);
            if (fd != STDIN_FILENO) { if (dup2(fd, STDIN_FILENO) < 0) die(2); close(fd); }
            break;
        }
        case StdinMode::Pipe:    if (dup2(r.in_r, STDIN_FILENO) < 0) die(2); break;
        case StdinMode::Inherit: break;
    }
    if (dup2(r.out_w, STDOUT_FILENO) < 0) die(2);
    if (dup2(r.err_w, STDERR_FILENO) < 0) die(2);
    if (!r.cwd->empty() && chdir(r.cwd->c_str()) != 0) die(3);
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGPIPE, &sa, nullptr);
    if (r.close_others) {
#if defined(__linux__) && defined(SYS_close_range)
        // close_range(3, errfd-1) and (errfd+1, ~0): errfd is close-on-exec and must survive until exec.
        bool done = false;
        if (errfd > 3) done = syscall(SYS_close_range, 3U, static_cast<unsigned>(errfd - 1), 0U) == 0;
        else done = true;
        if (done && syscall(SYS_close_range, static_cast<unsigned>(errfd + 1), ~0U, 0U) == 0) goto closed;
#endif
        for (int fd = 3; fd < max_fd; ++fd) if (fd != errfd) close(fd);
#if defined(__linux__) && defined(SYS_close_range)
    closed:;
#endif
    }
    pthread_sigmask(SIG_SETMASK, empty, nullptr);
    execve(path, r.argv, r.envp);
    die(4);
    _exit(127);
}

Expected<pid_t> spawn_with_fork(const SpawnRequest& r) {
    auto path = resolve_executable(*r.exe);
    if (!path) return std::unexpected(path.error());
    Fd ep_r, ep_w;
    if (!make_pipe(ep_r, ep_w)) return fail(ErrorCode::PipeFailed, errno, "error pipe: " + errno_text(errno));

    long lim = sysconf(_SC_OPEN_MAX);
    const int max_fd = static_cast<int>(lim <= 0 ? 1024 : std::min<long>(lim, 65536));
    sigset_t all, old, empty;
    sigfillset(&all);
    sigemptyset(&empty);
    pthread_sigmask(SIG_SETMASK, &all, &old);   // no handler may run in the child before exec
    const pid_t pid = fork();
    if (pid == 0) fork_child(r, path->c_str(), ep_w.get(), max_fd, &empty);
    const int fork_errno = errno;
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
    if (pid < 0) return fail(ErrorCode::SpawnFailed, fork_errno, "fork: " + errno_text(fork_errno));
    ep_w.reset();

    int e[2] = {0, 0};
    ssize_t n;
    do { n = read(ep_r.get(), e, sizeof e); } while (n < 0 && errno == EINTR);
    if (n == static_cast<ssize_t>(sizeof e)) {
        int st = 0;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        static const char* const stages[] = {"", "setpgid", "stdio redirection", "chdir", "exec"};
        const char* stage = (e[1] >= 1 && e[1] <= 4) ? stages[e[1]] : "spawn";
        ErrorCode code = e[1] == 3 ? ErrorCode::WorkingDirectoryFailed : classify_spawn_errno(e[0]);
        return fail(code, e[0], std::string("Cannot execute '") + *r.exe + "' (" + stage + "): " + errno_text(e[0]));
    }
    return pid;
}

} // namespace

// ============================================================================================
// FileSink
// ============================================================================================
namespace detail {

Expected<void> FileSink::open(const std::string& path, bool append) {
    close();
    const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC);
    int fd;
    do { fd = ::open(path.c_str(), flags, 0666); } while (fd < 0 && errno == EINTR);
    if (fd < 0) return fail(ErrorCode::OutputFileFailed, errno, "Cannot open '" + path + "': " + errno_text(errno));
    fd_ = fd;
    return {};
}
bool FileSink::is_open() const noexcept { return fd_ >= 0; }
bool FileSink::write(std::string_view data) noexcept {
    while (!data.empty()) {
        const ssize_t n = ::write(fd_, data.data(), data.size());
        if (n < 0) { if (errno == EINTR) continue; return false; }
        data.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
}
void FileSink::close() noexcept { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

} // namespace detail

// ============================================================================================
// Native state and Impl hooks
// ============================================================================================

struct Process::Impl::Native {
    pid_t child = -1;
    bool group = false;
    Fd out_r, err_r, in_w, wake_r, wake_w;
    std::mutex wake_mu;                    ///< Guards wake_w against use-after-close (fd reuse).
    std::thread io;
    std::atomic<bool> stop{false};
    detail::Latch io_done;
    int wait_errno = 0;
#if !TK4CPP_HAVE_WAITID
    int raw_status = 0;
    bool have_status = false;
#endif
};

namespace {
void do_wake(std::mutex& m, Fd& w) {
    std::lock_guard<std::mutex> l(m);
    if (w) { const char b = 1; ssize_t ignored = ::write(w.get(), &b, 1); (void)ignored; }
}
}

static void io_main(Process::Impl& self, Process::Impl::Native& n);

Expected<void> Process::Impl::platform_start() {
    auto nat = std::make_unique<Native>();
    Native& n = *nat;
    const Options& o = options_;

    if (!o.working_directory.empty()) {
        struct stat st{};
        if (stat(o.working_directory.c_str(), &st) != 0)
            return fail(ErrorCode::WorkingDirectoryFailed, errno,
                        "Working directory '" + o.working_directory + "': " + errno_text(errno));
        if (!S_ISDIR(st.st_mode) || access(o.working_directory.c_str(), X_OK) != 0)
            return fail(ErrorCode::WorkingDirectoryFailed, S_ISDIR(st.st_mode) ? EACCES : ENOTDIR,
                        "Working directory '" + o.working_directory + "' is not an accessible directory");
    }

    std::vector<std::string> env = build_environment(o);
    std::vector<char*> envp;
    envp.reserve(env.size() + 1);
    for (auto& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::vector<std::string> argv_store;
    argv_store.reserve(arguments_.size() + 1);
    argv_store.push_back(executable_);
    argv_store.insert(argv_store.end(), arguments_.begin(), arguments_.end());
    std::vector<char*> argv;
    argv.reserve(argv_store.size() + 1);
    for (auto& a : argv_store) argv.push_back(a.data());
    argv.push_back(nullptr);

    Fd out_w, err_w, in_r;
    auto pipe_fail = [&](const char* what) {
        return fail(ErrorCode::PipeFailed, errno, std::string("Cannot create ") + what + " pipe: " + errno_text(errno));
    };
    if (!make_pipe(n.out_r, out_w)) return pipe_fail("stdout");
    if (!o.merge_stderr_to_stdout && !make_pipe(n.err_r, err_w)) return pipe_fail("stderr");
    if (wants_stdin_pipe() && !make_pipe(in_r, n.in_w)) return pipe_fail("stdin");
    if (!make_pipe(n.wake_r, n.wake_w)) return pipe_fail("wake-up");
    for (int fd : {n.out_r.get(), n.err_r.get(), n.in_w.get(), n.wake_r.get(), n.wake_w.get()})
        if (fd >= 0 && !set_nonblocking(fd)) return pipe_fail("non-blocking");
    grow_pipe(n.out_r.get());
    if (n.err_r) grow_pipe(n.err_r.get());
    if (n.in_w) grow_pipe(n.in_w.get());

    n.group = o.tree_policy != TreePolicy::None;
    SpawnRequest req{&executable_, argv.data(), envp.data(), &o.working_directory,
                     wants_stdin_pipe() ? StdinMode::Pipe : o.stdin_mode,
                     in_r.get(), out_w.get(), o.merge_stderr_to_stdout ? out_w.get() : err_w.get(),
                     n.group, o.close_inherited_handles};

    bool use_fork = false;
#if defined(TK4CPP_PROCESS_FORCE_FORK)
    use_fork = true;
#elif !TK4CPP_PROCESS_HAVE_ADDCHDIR
    use_fork = !o.working_directory.empty();
#endif
    auto spawned = use_fork ? spawn_with_fork(req) : spawn_with_posix_spawn(req);
    if (!spawned) return std::unexpected(spawned.error());
    n.child = *spawned;
    out_w.reset();
    err_w.reset();
    in_r.reset();

    pid_ = static_cast<long long>(n.child);
    tree_managed_ = n.group;

    n.io_done.add(1);
    try {
        n.io = std::thread([this, &n] { io_main(*this, n); });
    } catch (const std::exception& ex) {
        ::kill(n.group ? -n.child : n.child, SIGKILL);
        int st = 0;
        while (waitpid(n.child, &st, 0) < 0 && errno == EINTR) {}
        return fail(ErrorCode::SpawnFailed, 0, std::string("Cannot create I/O thread: ") + ex.what());
    }
    native_ = nat.release();
    return {};
}

void Process::Impl::platform_wait_exit() {
    Native& n = *native_;
#if TK4CPP_HAVE_WAITID
    siginfo_t info;
    std::memset(&info, 0, sizeof info);
    while (waitid(P_PID, static_cast<id_t>(n.child), &info, WEXITED | WNOWAIT) != 0) {
        if (errno == EINTR) continue;
        n.wait_errno = errno;   // typically ECHILD: SIGCHLD ignored or child reaped by foreign code
        break;
    }
#else
    int st = 0;
    pid_t r;
    do { r = waitpid(n.child, &st, 0); } while (r < 0 && errno == EINTR);
    if (r == n.child) { n.raw_status = st; n.have_status = true; } else n.wait_errno = errno;
#endif
}

bool Process::Impl::platform_terminate_locked() {
    Native& n = *native_;
    if (::kill(n.group ? -n.child : n.child, SIGTERM) != 0 && errno != ESRCH)
        note_error({ErrorCode::SpawnFailed, errno, "SIGTERM: " + errno_text(errno)});
    return true;
}

void Process::Impl::platform_kill_locked() {
    Native& n = *native_;
    if (::kill(n.group ? -n.child : n.child, SIGKILL) != 0 && errno != ESRCH)
        note_error({ErrorCode::SpawnFailed, errno, "SIGKILL: " + errno_text(errno)});
}

ExitStatus Process::Impl::platform_reap_locked() {
    Native& n = *native_;
    ExitStatus es;
    int st = 0;
    bool got = false;
#if TK4CPP_HAVE_WAITID
    if (n.wait_errno == 0) {
        pid_t r;
        do { r = waitpid(n.child, &st, 0); } while (r < 0 && errno == EINTR);
        got = r == n.child;
        if (!got) n.wait_errno = errno;
    }
#else
    if (n.have_status) { st = n.raw_status; got = true; }
#endif
    if (!got) {
        note_error({ErrorCode::WaitFailed, n.wait_errno,
                    "Exit status unavailable (SIGCHLD ignored or child reaped elsewhere): " + errno_text(n.wait_errno)});
        return es;   // Kind::Unknown
    }
    if (WIFEXITED(st)) { es.kind = ExitStatus::Kind::Exited; es.code = WEXITSTATUS(st); }
    else if (WIFSIGNALED(st)) { es.kind = ExitStatus::Kind::Signaled; es.signal = WTERMSIG(st); es.code = -1; }
    return es;
}

bool Process::Impl::platform_drain(std::chrono::milliseconds timeout) {
    if (!native_) return true;
    Native& n = *native_;
    bool clean = n.io_done.wait_for(timeout);
    if (!clean) {
        n.stop.store(true);
        do_wake(n.wake_mu, n.wake_w);
        n.io_done.wait_for(std::chrono::seconds(5));
    }
    if (n.io.joinable()) n.io.join();
    return clean;
}

void Process::Impl::platform_wake_stdin() {
    if (native_) do_wake(native_->wake_mu, native_->wake_w);
}

void Process::Impl::platform_release() {
    if (!native_) return;
    Native& n = *native_;
    if (n.io.joinable()) return;   // workers still running: the monitor releases after joining them
    n.out_r.reset();
    n.err_r.reset();
    n.in_w.reset();
    n.wake_r.reset();
    std::lock_guard<std::mutex> l(n.wake_mu);
    n.wake_w.reset();
}

void Process::Impl::platform_destroy_native() {
    delete native_;
    native_ = nullptr;
}

// ============================================================================================
// I/O thread
// ============================================================================================

static void io_main(Process::Impl& self, Process::Impl::Native& n) {
    struct Done { detail::Latch& l; ~Done() { l.count_down(); } } done{n.io_done};
    detail::tl_worker_owner = &self;

    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);                 // writes to a dead child must return EPIPE, not kill us
    pthread_sigmask(SIG_BLOCK, &blocked, nullptr);

    std::vector<char> buf(256 * 1024);
    std::string wbuf;
    std::size_t woff = 0;
    bool final_pass = false;
    std::chrono::steady_clock::time_point final_deadline{};

    auto read_some = [&](Fd& fd, bool is_err) {
        // Keep reading while the pipe is full-ish: saves a poll(2) per chunk under heavy output
        // while the bounded loop keeps stdout/stderr/stdin fair.
        for (int round = 0; round < 8 && fd; ++round) {
            const ssize_t r = ::read(fd.get(), buf.data(), buf.size());
            if (r > 0) {
                self.consume(std::string_view(buf.data(), static_cast<std::size_t>(r)), is_err);
            } else if (r == 0) { fd.reset(); }
            else {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                    self.note_error({ErrorCode::PipeFailed, errno, "read: " + errno_text(errno)});
                    fd.reset();
                }
                break;
            }
        }
    };
    auto close_in = [&](bool shut) {
        n.in_w.reset();
        wbuf.clear();
        woff = 0;
        if (shut) self.stdin_queue.shut();
    };

    for (;;) {
        if (!final_pass && n.stop.load()) {
            final_pass = true;
            final_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }
        if (n.in_w) {
            if (final_pass || self.stdin_queue.is_shut()) close_in(false);
            else {
                if (woff >= wbuf.size()) { wbuf.clear(); woff = 0; self.stdin_queue.take(wbuf); }
                if (wbuf.empty() && self.stdin_queue.finished()) close_in(false);
            }
        }
        if (!n.out_r && !n.err_r && !n.in_w) break;
        if (final_pass && std::chrono::steady_clock::now() >= final_deadline) break;

        pollfd fds[4];
        int nfds = 0, i_out = -1, i_err = -1, i_in = -1;
        if (n.out_r) { i_out = nfds; fds[nfds++] = {n.out_r.get(), POLLIN, 0}; }
        if (n.err_r) { i_err = nfds; fds[nfds++] = {n.err_r.get(), POLLIN, 0}; }
        if (n.in_w && woff < wbuf.size()) { i_in = nfds; fds[nfds++] = {n.in_w.get(), POLLOUT, 0}; }
        const int i_wake = nfds;
        fds[nfds++] = {n.wake_r.get(), POLLIN, 0};

        const int rc = ::poll(fds, static_cast<nfds_t>(nfds), final_pass ? 0 : -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            self.note_error({ErrorCode::PipeFailed, errno, "poll: " + errno_text(errno)});
            break;
        }
        if (final_pass && rc == 0) break;

        if (fds[i_wake].revents & POLLIN) {
            char tmp[64];
            while (::read(n.wake_r.get(), tmp, sizeof tmp) > 0) {}
        }
        if (i_out >= 0 && (fds[i_out].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) read_some(n.out_r, false);
        if (i_err >= 0 && (fds[i_err].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) read_some(n.err_r, true);
        if (i_in >= 0 && n.in_w) {
            const short ev = fds[i_in].revents;
            if (ev & (POLLERR | POLLHUP | POLLNVAL)) close_in(true);    // reader side gone
            else if (ev & POLLOUT) {
                const ssize_t w = ::write(n.in_w.get(), wbuf.data() + woff, wbuf.size() - woff);
                if (w > 0) { woff += static_cast<std::size_t>(w); self.stdin_queue.note_sent(static_cast<std::size_t>(w)); }
                else if (w < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) close_in(true);
            }
        }
    }
}

} // namespace TK4CPP::Process

#endif // !_WIN32
