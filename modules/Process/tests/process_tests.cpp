/**
 * @file process_tests.cpp
 * @brief Self-contained test suite (no framework, not based on assert(): works in Release).
 *
 * Usage: process_tests PATH_TO_process_test_helper [test-name-substring]
 */
#include "TK4CPP/Process.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#  include <csignal>
#  include <fcntl.h>
#  include <unistd.h>
#endif

using namespace TK4CPP::Process;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

std::string g_helper;
int g_failures = 0;
std::string g_current;

struct TestCase { const char* name; void (*fn)(); };
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Registrar { Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); } };

#define TEST(name) \
    static void test_##name(); \
    static Registrar reg_##name(#name, &test_##name); \
    static void test_##name()

#define CHECK(cond) \
    do { if (!(cond)) { ++g_failures; std::fprintf(stderr, "  FAIL %s:%d: %s  [%s]\n", __FILE__, __LINE__, #cond, g_current.c_str()); } } while (0)
#define REQUIRE(cond) \
    do { if (!(cond)) { ++g_failures; std::fprintf(stderr, "  FAIL(required) %s:%d: %s  [%s]\n", __FILE__, __LINE__, #cond, g_current.c_str()); return; } } while (0)

using Clock = std::chrono::steady_clock;
std::chrono::milliseconds since(Clock::time_point t) { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t); }

std::string trim_eol(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}
std::string normalize_eol(const std::string& s) {
    std::string o;
    for (char c : s) if (c != '\r') o += c;
    return o;
}

Expected<Result> run(std::vector<std::string> args, Options o = {}, std::chrono::milliseconds t = 20s) {
    return Process::run_sync(g_helper, std::move(args), t, std::move(o));
}

fs::path temp_dir() {
    static const fs::path d = [] {
        auto p = fs::temp_directory_path() / ("tk4cpp_proc_test_" + std::to_string(Clock::now().time_since_epoch().count()));
        fs::create_directories(p);
        return p;
    }();
    return d;
}
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

#ifdef __linux__
int count_open_fds() {
    int n = 0;
    for (auto& e : fs::directory_iterator("/proc/self/fd")) { (void)e; ++n; }
    return n;
}
#endif

// ------------------------------------------------------------------------------------------------

TEST(basic_capture_and_exit_code) {
    auto r = run({"echo", "hello"});
    REQUIRE(r.has_value());
    CHECK(r->success());
    CHECK(r->standard_output == "hello");
    CHECK(r->status.kind == ExitStatus::Kind::Exited);
    CHECK(r->pid > 0);
    auto f = run({"exit", "7"});
    REQUIRE(f.has_value());
    CHECK(!f->success());
    CHECK(f->status.code == 7);
    CHECK(f->status.legacy_code() == 7);
    CHECK(!f->timed_out);
}

TEST(launch_errors_are_typed) {
    auto r = Process::run_sync("tk4cpp-definitely-not-an-executable", {}, 5s);
    REQUIRE(!r.has_value());
    CHECK(r.error().code == ErrorCode::ExecutableNotFound);
    CHECK(!r.error().message.empty());

    auto e = Process::run_sync("", {});
    REQUIRE(!e.has_value());
    CHECK(e.error().code == ErrorCode::InvalidArgument);

    auto nul = Process::run_sync(g_helper, {std::string("a\0b", 3)});
    REQUIRE(!nul.has_value());
    CHECK(nul.error().code == ErrorCode::InvalidArgument);

    Options o;
    o.working_directory = (temp_dir() / "does" / "not" / "exist").string();
    auto cwd = run({"cwd"}, o);
    REQUIRE(!cwd.has_value());
    CHECK(cwd.error().code == ErrorCode::WorkingDirectoryFailed);

    Options bad_env;
    bad_env.environment = {"NOEQUALS"};
    auto be = run({"cwd"}, bad_env);
    REQUIRE(!be.has_value());
    CHECK(be.error().code == ErrorCode::InvalidArgument);

    Options bad_file;
    bad_file.stdout_filepath = (temp_dir() / "nodir" / "x.txt").string();
    auto bf = run({"echo", "x"}, bad_file);
    REQUIRE(!bf.has_value());
    CHECK(bf.error().code == ErrorCode::OutputFileFailed);
}

TEST(lifecycle_state_checks) {
    Process p(g_helper, {"echo", "x"});
    auto w = p.wait();
    REQUIRE(!w.has_value());
    CHECK(w.error().code == ErrorCode::InvalidState);
    CHECK(!p.wait_for(10ms));
    CHECK(!p.is_running());
    REQUIRE(p.start().has_value());
    auto again = p.start();
    REQUIRE(!again.has_value());
    CHECK(again.error().code == ErrorCode::InvalidState);
    auto st = p.wait();
    REQUIRE(st.has_value());
    CHECK(st->success());
    CHECK(p.exit_status().has_value());
    CHECK(!p.is_running());
    CHECK(p.pid() > 0);

    Process moved(std::move(p));
    auto bad = p.start();   // NOLINT(bugprone-use-after-move): moved-from objects must fail cleanly
    REQUIRE(!bad.has_value());
    CHECK(bad.error().code == ErrorCode::InvalidState);
    CHECK(moved.read_stdout() == "x");
}

TEST(arguments_are_passed_verbatim) {
    const std::vector<std::string> tricky = {
        "with space", "quo\"te", "back\\slash", "trail\\", "trail\\\\", "\"", "", "$HOME", "*", "a&b|c<d>e^f%g%",
        "tab\there", "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80", "--flag=val ue"};
    std::vector<std::string> args = {"echo-args"};
    args.insert(args.end(), tricky.begin(), tricky.end());
    auto r = run(args);
    REQUIRE(r.has_value());
    std::string expected;
    for (const auto& t : tricky) expected += "[" + t + "]\n";
    CHECK(normalize_eol(r->standard_output) == expected);
}

TEST(environment_control) {
    Options o;
    o.environment = {"TK4_TEST_VAR=hello world", "TK4_TEST_EMPTY="};
    auto r = run({"echo-env", "TK4_TEST_VAR"}, o);
    REQUIRE(r.has_value());
    CHECK(r->standard_output == "hello world");
    auto e = run({"echo-env", "TK4_TEST_EMPTY"}, o);
    REQUIRE(e.has_value());
    CHECK(e->standard_output.empty());      // set but empty

    Options u;
    u.environment = {"TK4_TEST_VAR=1"};
    u.unset_environment = {"TK4_TEST_VAR"};  // unset applied first, then override re-adds: override wins
    auto uo = run({"echo-env", "TK4_TEST_VAR"}, u);
    REQUIRE(uo.has_value());
    CHECK(uo->standard_output == "1");

    Options un;
    un.unset_environment = {"PATH"};
    auto up = run({"echo-env", "PATH"}, un);
    REQUIRE(up.has_value());
    CHECK(up->standard_output == "<unset>");
}

TEST(working_directory) {
    Options o;
    o.working_directory = fs::weakly_canonical(temp_dir()).string();
    auto r = run({"cwd"}, o);
    REQUIRE(r.has_value());
    CHECK(fs::weakly_canonical(fs::path(trim_eol(r->standard_output))) == fs::weakly_canonical(temp_dir()));
}

TEST(stderr_separate_and_merged) {
    auto sep = run({"order"});
    REQUIRE(sep.has_value());
    CHECK(normalize_eol(sep->standard_output) == "o0\no1\no2\no3\no4\n");
    CHECK(normalize_eol(sep->standard_error) == "e0\ne1\ne2\ne3\ne4\n");
    Options m;
    m.merge_stderr_to_stdout = true;
    auto mg = run({"order"}, m);
    REQUIRE(mg.has_value());
    CHECK(mg->standard_error.empty());
    CHECK(normalize_eol(mg->standard_output).size() == 30);
    // Within each stream order is preserved even if the two interleave.
    CHECK(mg->standard_output.find("o4") != std::string::npos);
    CHECK(mg->standard_output.find("e4") != std::string::npos);
}

TEST(huge_output_on_both_streams_does_not_deadlock) {
    const std::size_t n = 8u * 1024 * 1024;
    Options o;
    o.max_capture_bytes = 64u * 1024 * 1024;
    const auto t0 = Clock::now();
    auto r = run({"both-bytes", std::to_string(n)}, o, 60s);
    REQUIRE(r.has_value());
    CHECK(r->success());
    CHECK(r->standard_output.size() == n);
    CHECK(r->standard_error.size() == n);
    CHECK(!r->stdout_truncated);
    CHECK(since(t0) < 30s);
}

TEST(capture_limit_truncates_but_streams_everything) {
    const std::size_t n = 3u * 1024 * 1024;
    Options o;
    o.max_capture_bytes = 100000;
    std::atomic<std::size_t> streamed{0};
    o.on_stdout = [&](std::string_view s) { streamed += s.size(); };
    auto r = run({"stdout-bytes", std::to_string(n)}, o);
    REQUIRE(r.has_value());
    CHECK(r->standard_output.size() == 100000);
    CHECK(r->stdout_truncated);
    CHECK(!r->stderr_truncated);
    CHECK(streamed == n);

    Options zero;
    zero.max_capture_bytes = 0;
    auto z = run({"echo", "abc"}, zero);
    REQUIRE(z.has_value());
    CHECK(z->standard_output.empty());
    CHECK(z->success());
}

TEST(callbacks_survive_exceptions_and_are_serialised) {
    std::atomic<int> concurrent{0};
    std::atomic<bool> overlap{false};
    Options o;
    o.on_stdout = [&](std::string_view) {
        if (concurrent.fetch_add(1) != 0) overlap = true;
        std::this_thread::sleep_for(1ms);
        --concurrent;
        throw std::runtime_error("callback failure must be contained");
    };
    o.on_stderr = o.on_stdout;
    auto r = run({"both-bytes", "200000"}, o);
    REQUIRE(r.has_value());
    CHECK(r->success());
    CHECK(!overlap);
}

TEST(on_exit_fires_once_after_output_drained) {
    std::atomic<int> calls{0};
    std::atomic<std::size_t> seen_at_exit{0};
    std::atomic<std::size_t> total{0};
    Options o;
    o.on_stdout = [&](std::string_view s) { total += s.size(); };
    o.on_exit = [&](const ExitStatus& st) { ++calls; seen_at_exit = total.load(); CHECK(st.success()); };
    {
        Process p(g_helper, {"stdout-bytes", "500000"}, o);
        REQUIRE(p.start().has_value());
        REQUIRE(p.wait().has_value());
    }   // destructor guarantees the callback already completed
    CHECK(calls == 1);
    CHECK(seen_at_exit == 500000);
}

TEST(stdin_pipe_roundtrip_large) {
    Options o;
    o.stdin_mode = StdinMode::Pipe;
    o.max_capture_bytes = 64u * 1024 * 1024;
    Process p(g_helper, {"cat"}, o);
    REQUIRE(p.start().has_value());
    std::string payload;
    for (int i = 0; i < 1024 * 1024; ++i) payload += static_cast<char>('a' + (i * 7) % 26);
    for (int rep = 0; rep < 4; ++rep) REQUIRE(p.write_stdin(payload).has_value());   // 4 MiB >> pipe buffer: must not block
    p.close_stdin();
    REQUIRE(p.wait_for(30s));
    const std::string out = p.read_stdout();
    CHECK(out.size() == payload.size() * 4);
    CHECK(out.compare(0, payload.size(), payload) == 0);
    auto late = p.write_stdin("x");
    CHECK(!late.has_value());
}

TEST(stdin_data_option_and_null_stdin) {
    Options o;
    o.stdin_data = "from option";
    auto r = run({"cat"}, o);
    REQUIRE(r.has_value());
    CHECK(r->standard_output == "from option");
    auto n = run({"cat"});          // StdinMode::Null: immediate EOF, no hang
    REQUIRE(n.has_value());
    CHECK(n->standard_output.empty());
    CHECK(n->success());
    Process p(g_helper, {"sleep", "50"});
    REQUIRE(p.start().has_value());
    auto w = p.write_stdin("x");    // not in Pipe mode
    REQUIRE(!w.has_value());
    CHECK(w.error().code == ErrorCode::StdinClosed);
    p.wait();
}

TEST(stdin_backpressure_limit) {
    Options o;
    o.stdin_mode = StdinMode::Pipe;
    o.max_stdin_queue_bytes = 1024;
    Process p(g_helper, {"sleep", "400"}, o);   // never reads stdin
    REQUIRE(p.start().has_value());
    std::string big(1u << 20, 'z');
    std::size_t rejected = 0;
    for (int i = 0; i < 4; ++i) {
        auto r = p.write_stdin(big);
        if (!r) { CHECK(r.error().code == ErrorCode::QueueFull); ++rejected; }
    }
    CHECK(rejected == 4);
    p.kill();
    p.wait();
}

TEST(write_to_dead_child_reports_closed_not_sigpipe) {
    Options o;
    o.stdin_mode = StdinMode::Pipe;
    Process p(g_helper, {"exit", "0"}, o);
    REQUIRE(p.start().has_value());
    REQUIRE(p.wait_for(10s));
    auto w = p.write_stdin("data");
    CHECK(!w.has_value());
    // the host process must still be alive here (no SIGPIPE termination)
}

TEST(timeout_terminates_child) {
    const auto t0 = Clock::now();
    Options o;
    o.terminate_grace = 300ms;
    auto r = Process::run_sync(g_helper, {"sleep", "60000"}, 200ms, o);
    REQUIRE(r.has_value());
    CHECK(r->timed_out);
    CHECK(!r->success());
    CHECK(since(t0) < 10s);
    CHECK(r->status.kind != ExitStatus::Kind::Exited || r->status.code != 0);
}

TEST(cancel_token_terminates_child) {
    std::stop_source src;
    Options o;
    o.cancel = src.get_token();
    o.terminate_grace = 300ms;
    std::thread t([&] { std::this_thread::sleep_for(150ms); src.request_stop(); });
    const auto t0 = Clock::now();
    auto r = run({"sleep", "60000"}, o);
    t.join();
    REQUIRE(r.has_value());
    CHECK(r->cancelled);
    CHECK(!r->success());
    CHECK(since(t0) < 10s);
}

TEST(kill_reports_signal) {
    Process p(g_helper, {"sleep", "60000"});
    REQUIRE(p.start().has_value());
    CHECK(p.is_running());
    CHECK(!p.wait_for(30ms));
    p.kill();
    auto st = p.wait();
    REQUIRE(st.has_value());
    CHECK(!st->success());
#ifndef _WIN32
    CHECK(st->kind == ExitStatus::Kind::Signaled);
    CHECK(st->signal == SIGKILL);
    CHECK(st->legacy_code() == -SIGKILL);
#endif
    p.kill();   // no-op after exit, must not touch a recycled pid
}

#ifndef _WIN32
TEST(terminate_graceful_and_escalation) {
    {
        Process p(g_helper, {"sleep", "60000"});
        REQUIRE(p.start().has_value());
        std::this_thread::sleep_for(50ms);
        CHECK(p.terminate());                  // SIGTERM is enough
        CHECK(p.exit_status()->signal == SIGTERM);
    }
    {
        Options o;
        o.terminate_grace = 250ms;
        Process p(g_helper, {"ignore-term"}, o);
        REQUIRE(p.start().has_value());
        for (int i = 0; i < 200 && p.read_stdout().empty(); ++i) std::this_thread::sleep_for(10ms);
        const auto t0 = Clock::now();
        CHECK(!p.terminate());                 // had to escalate
        CHECK(since(t0) >= 200ms);
        CHECK(p.exit_status()->signal == SIGKILL);
    }
}

TEST(crash_is_reported_as_signal) {
    auto r = run({"crash"});
    REQUIRE(r.has_value());
    CHECK(r->status.kind == ExitStatus::Kind::Signaled);
    CHECK(r->status.signal == SIGSEGV);
    CHECK(!r->success());
}
#endif

long long parse_grandchild(const std::string& out) {
    const auto pos = out.find("grandchild=");
    return pos == std::string::npos ? -1 : std::atoll(out.c_str() + pos + 11);
}
bool pid_alive(long long pid) {
    auto r = Process::run_sync(g_helper, {"alive", std::to_string(pid)}, 10s);
    return r && r->success();
}

TEST(tree_is_killed_when_direct_child_exits) {
    const auto t0 = Clock::now();
    auto r = run({"spawn-child"});     // exits at once; grandchild (sleep 60s) holds the output pipe
    REQUIRE(r.has_value());
    CHECK(r->success());
    CHECK(!r->output_incomplete);
    CHECK(since(t0) < 8s);
    const long long gc = parse_grandchild(r->standard_output);
    REQUIRE(gc > 0);
    for (int i = 0; i < 100 && pid_alive(gc); ++i) std::this_thread::sleep_for(50ms);
    CHECK(!pid_alive(gc));
}

TEST(tree_none_abandons_pipes_after_drain_timeout) {
    Options o;
    o.tree_policy = TreePolicy::None;
    o.output_drain_timeout = 300ms;
    const auto t0 = Clock::now();
    auto r = run({"spawn-child"}, o);
    REQUIRE(r.has_value());
    CHECK(r->success());                // the direct child's status is still correct
    CHECK(r->output_incomplete);        // but we did not hang on the orphan holding the pipe
    CHECK(since(t0) < 8s);
    CHECK(r->warning.has_value());
    const long long gc = parse_grandchild(r->standard_output);
    if (gc > 0) (void)Process::run_sync(g_helper, {"kill-pid", std::to_string(gc)}, 10s);
}

TEST(kill_tree_on_exit_can_be_disabled) {
    Options o;
    o.kill_tree_on_exit = false;
    o.output_drain_timeout = 200ms;
    auto r = run({"spawn-child"}, o);
    REQUIRE(r.has_value());
    const long long gc = parse_grandchild(r->standard_output);
    REQUIRE(gc > 0);
    CHECK(pid_alive(gc));               // survivor: the caller opted out of cleanup
    (void)Process::run_sync(g_helper, {"kill-pid", std::to_string(gc)}, 10s);
}

TEST(file_redirection) {
    const auto out = temp_dir() / "out.txt";
    const auto err = temp_dir() / "err.txt";
    Options o;
    o.stdout_filepath = out.string();
    o.stderr_filepath = err.string();
    auto r = run({"order"}, o);
    REQUIRE(r.has_value());
    CHECK(normalize_eol(slurp(out)) == "o0\no1\no2\no3\no4\n");
    CHECK(normalize_eol(slurp(err)) == "e0\ne1\ne2\ne3\ne4\n");
    CHECK(r->standard_output.size() > 0);          // capture and file are independent sinks

    Options app;
    app.stdout_filepath = out.string();
    app.append_to_files = true;
    REQUIRE(run({"echo", "tail"}, app).has_value());
    CHECK(normalize_eol(slurp(out)) == "o0\no1\no2\no3\no4\ntail");

    Options fresh;
    fresh.stdout_filepath = out.string();
    REQUIRE(run({"echo", "fresh"}, fresh).has_value());
    CHECK(slurp(out) == "fresh");                  // truncated by default

    const auto both = temp_dir() / "both.txt";
    Options same;
    same.stdout_filepath = same.stderr_filepath = both.string();
    REQUIRE(run({"order"}, same).has_value());
    CHECK(slurp(both).size() == 30);               // one shared sink: nothing overwritten
}

TEST(destroying_running_process_kills_it_and_stops_callbacks) {
    std::atomic<int> after_destroy{0};
    std::atomic<bool> destroyed{false};
    long long pid = -1;
    const auto t0 = Clock::now();
    {
        Options o;
        o.on_stdout = [&](std::string_view) { if (destroyed) ++after_destroy; };
        o.on_exit = [&](const ExitStatus&) { if (destroyed) ++after_destroy; };
        Process p(g_helper, {"ignore-term"}, o);
        REQUIRE(p.start().has_value());
        pid = p.pid();
        std::this_thread::sleep_for(50ms);
    }
    destroyed = true;
    CHECK(since(t0) < 5s);
    std::this_thread::sleep_for(100ms);
    CHECK(after_destroy == 0);
    CHECK(!pid_alive(pid));
}

TEST(concurrent_lifecycle_calls_are_safe) {
    Process p(g_helper, {"sleep", "300"});
    REQUIRE(p.start().has_value());
    std::vector<std::thread> ts;
    std::atomic<int> ok{0};
    for (int i = 0; i < 6; ++i)
        ts.emplace_back([&, i] {
            for (int k = 0; k < 50; ++k) { (void)p.is_running(); (void)p.read_stdout(); (void)p.pid(); }
            if (i % 3 == 0) { if (p.wait()) ++ok; }
            else if (i % 3 == 1) { if (p.wait_for(5s)) ++ok; }
            else { p.kill(); ++ok; }
        });
    for (auto& t : ts) t.join();
    CHECK(ok == 6);
    CHECK(!p.is_running());
}

TEST(many_parallel_run_sync) {
    std::vector<std::thread> ts;
    std::atomic<int> good{0};
    for (int i = 0; i < 16; ++i)
        ts.emplace_back([&, i] {
            for (int k = 0; k < 5; ++k) {
                auto r = run({"echo", std::to_string(i * 100 + k)}, {}, 30s);
                if (r && r->success() && r->standard_output == std::to_string(i * 100 + k)) ++good;
            }
        });
    for (auto& t : ts) t.join();
    CHECK(good == 80);
}

#ifdef __linux__
TEST(no_descriptor_leak_over_many_launches) {
    (void)run({"echo", "warmup"});
    const int before = count_open_fds();
    for (int i = 0; i < 150; ++i) {
        Options o;
        o.stdin_mode = StdinMode::Pipe;
        o.stdin_data = "x";
        auto r = run({"cat"}, o);
        REQUIRE(r.has_value());
    }
    for (int i = 0; i < 30; ++i) { (void)Process::run_sync("tk4cpp-nope", {}); }
    CHECK(count_open_fds() == before);
}
#endif

#ifndef _WIN32
TEST(child_gets_clean_signal_state) {
    // Host ignores SIGPIPE and blocks SIGUSR1: the child must see defaults.
    struct sigaction ign{}, old{};
    ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ign, &old);
    sigset_t blk, oldmask;
    sigemptyset(&blk);
    sigaddset(&blk, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &blk, &oldmask);
    auto r = run({"signal-state"});
    pthread_sigmask(SIG_SETMASK, &oldmask, nullptr);
    sigaction(SIGPIPE, &old, nullptr);
    REQUIRE(r.has_value());
    CHECK(r->standard_output == "sigpipe=default usr1=open\n");
}

TEST(inheritable_descriptors_do_not_leak) {
    const auto path = temp_dir() / "leak.txt";
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT, 0600);      // deliberately NOT close-on-exec
    REQUIRE(fd >= 3);
    auto r = run({"list-fds"});
    REQUIRE(r.has_value());
    CHECK(r->standard_output == "fds=\n");
    ::close(fd);
}

TEST(process_group_is_isolated) {
    auto r = run({"pgid"});
    REQUIRE(r.has_value());
    long long pg = 0, pid = 0;
    std::sscanf(r->standard_output.c_str(), "%lld %lld", &pg, &pid);
    CHECK(pg == pid);                      // child leads its own group
    Options o;
    o.tree_policy = TreePolicy::None;
    auto n = run({"pgid"}, o);
    REQUIRE(n.has_value());
    std::sscanf(n->standard_output.c_str(), "%lld %lld", &pg, &pid);
    CHECK(pg != pid);
}

TEST(survives_closed_standard_descriptors) {
    // A daemon-like host may have fd 0/1/2 closed: pipes must not land on 0..2 and clash with dup2.
    const int saved0 = dup(0), saved1 = dup(1), saved2 = dup(2);
    close(0); close(1); close(2);
    auto r = run({"echo", "still works"});
    dup2(saved0, 0); dup2(saved1, 1); dup2(saved2, 2);
    close(saved0); close(saved1); close(saved2);
    REQUIRE(r.has_value());
    CHECK(r->success());
    CHECK(r->standard_output == "still works");
}
#endif

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s HELPER [filter]\n", argv[0]); return 2; }
    g_helper = argv[1];
    const std::string filter = argc > 2 ? argv[2] : "";
    int ran = 0;
    for (const auto& t : registry()) {
        if (!filter.empty() && std::string_view(t.name).find(filter) == std::string_view::npos) continue;
        g_current = t.name;
        const int before = g_failures;
        const auto t0 = Clock::now();
        try { t.fn(); } catch (const std::exception& e) { ++g_failures; std::fprintf(stderr, "  EXCEPTION %s: %s\n", t.name, e.what()); }
        std::fprintf(stderr, "[%s] %-58s %5lld ms\n", g_failures == before ? " OK " : "FAIL", t.name, static_cast<long long>(since(t0).count()));
        ++ran;
    }
    std::error_code ec;
    fs::remove_all(temp_dir(), ec);
    std::fprintf(stderr, "\n%d tests, %d failed checks\n", ran, g_failures);
    return g_failures == 0 ? 0 : 1;
}
