/**
 * @file test_helper.cpp
 * @brief Tiny cross-platform child program driven by process_tests (no shell needed).
 *
 * Usage: process_test_helper MODE [args...]
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <csignal>
#  include <fcntl.h>
#  include <spawn.h>
#  include <unistd.h>
extern char** environ;
#endif

namespace {

void put(const std::string& s, FILE* f = stdout) { std::fwrite(s.data(), 1, s.size(), f); std::fflush(f); }
void sleep_ms(long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

#ifdef _WIN32
std::vector<std::string> get_args() {
    std::vector<std::string> out;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    LocalFree(w);
    return out;
}
#endif

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    auto args = get_args();
#else
    std::vector<std::string> args(argv, argv + argc);
#endif
    if (args.size() < 2) { put("usage\n", stderr); return 64; }
    const std::string& mode = args[1];

    if (mode == "echo-args") {                      // one argument per line, bracketed
        for (size_t i = 2; i < args.size(); ++i) put("[" + args[i] + "]\n");
        return 0;
    }
    if (mode == "echo") { for (size_t i = 2; i < args.size(); ++i) put(args[i]); return 0; }
    if (mode == "echo-env") {
        const char* v = std::getenv(args.at(2).c_str());
        put(v ? std::string(v) : std::string("<unset>"));
        return 0;
    }
    if (mode == "cwd") {
#ifdef _WIN32
        char buf[MAX_PATH * 4];
        const DWORD n = GetCurrentDirectoryA(sizeof buf, buf);
        put(std::string(buf, n));
#else
        char buf[4096];
        put(getcwd(buf, sizeof buf) ? buf : "?");
#endif
        return 0;
    }
    if (mode == "exit") return std::atoi(args.at(2).c_str());
    if (mode == "stdout-bytes" || mode == "stderr-bytes" || mode == "both-bytes") {
        const size_t n = static_cast<size_t>(std::atoll(args.at(2).c_str()));
        std::string chunk(4096, 'x');
        size_t done = 0;
        while (done < n) {
            const size_t k = std::min(chunk.size(), n - done);
            if (mode != "stderr-bytes") std::fwrite(chunk.data(), 1, k, stdout);
            if (mode != "stdout-bytes") std::fwrite(chunk.data(), 1, k, stderr);
            done += k;
        }
        std::fflush(stdout); std::fflush(stderr);
        return 0;
    }
    if (mode == "order") {                          // stdout/stderr interleaving with merge
        for (int i = 0; i < 5; ++i) { put("o" + std::to_string(i) + "\n"); put("e" + std::to_string(i) + "\n", stderr); }
        return 0;
    }
    if (mode == "cat") {
        char buf[8192];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0) { std::fwrite(buf, 1, n, stdout); std::fflush(stdout); }
        return 0;
    }
    if (mode == "sleep") { sleep_ms(std::atol(args.at(2).c_str())); return 0; }
    if (mode == "ignore-term") {                    // ignores graceful termination; used for escalation tests
#ifndef _WIN32
        std::signal(SIGTERM, SIG_IGN);
#endif
        put("ready\n");
        sleep_ms(60000);
        return 0;
    }
    if (mode == "crash") {
#ifdef _WIN32
        std::abort();
#else
        std::signal(SIGSEGV, SIG_DFL);   // sanitizer runtimes install their own handler
        raise(SIGSEGV);
#endif
        return 1;
    }
    if (mode == "spawn-child") {                    // grandchild inherits our stdout/stderr pipes
        const std::string self = args[0];
#ifdef _WIN32
        std::wstring cmd = L"\"" + std::wstring(self.begin(), self.end()) + L"\" sleep 60000";
        STARTUPINFOW si{}; si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 3;
        put("grandchild=" + std::to_string(pi.dwProcessId) + "\n");
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
#else
        pid_t pid;
        char* av[] = {const_cast<char*>(self.c_str()), const_cast<char*>("sleep"), const_cast<char*>("60000"), nullptr};
        if (posix_spawn(&pid, self.c_str(), nullptr, nullptr, av, environ) != 0) return 3;
        put("grandchild=" + std::to_string(pid) + "\n");
#endif
        return 0;                                   // exit immediately, leaving the grandchild behind
    }
    if (mode == "kill-pid") {
        const long pid = std::atol(args.at(2).c_str());
#ifdef _WIN32
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
        if (h) { TerminateProcess(h, 1); CloseHandle(h); }
#else
        ::kill(static_cast<pid_t>(pid), SIGKILL);
#endif
        return 0;
    }
    if (mode == "alive") {                          // exit 0 if pid exists
        const long pid = std::atol(args.at(2).c_str());
#ifdef _WIN32
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (!h) return 1;
        const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
        CloseHandle(h);
        return alive ? 0 : 1;
#else
        return (::kill(static_cast<pid_t>(pid), 0) == 0) ? 0 : 1;
#endif
    }
#ifndef _WIN32
    if (mode == "signal-state") {                   // reports what the child inherited
        struct sigaction sa;
        sigaction(SIGPIPE, nullptr, &sa);
        sigset_t cur;
        sigprocmask(SIG_BLOCK, nullptr, &cur);
        put(std::string("sigpipe=") + (sa.sa_handler == SIG_IGN ? "ignored" : "default") +
            " usr1=" + (sigismember(&cur, SIGUSR1) ? "blocked" : "open") + "\n");
        return 0;
    }
    if (mode == "list-fds") {
        std::string out;
        for (int fd = 3; fd < 256; ++fd) if (fcntl(fd, F_GETFD) != -1) out += std::to_string(fd) + " ";
        put("fds=" + out + "\n");
        return 0;
    }
    if (mode == "pgid") { put(std::to_string(getpgrp()) + " " + std::to_string(getpid()) + "\n"); return 0; }
#endif
    put("unknown mode\n", stderr);
    return 64;
}
