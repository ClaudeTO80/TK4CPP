/**
 * @file main.cpp
 * @brief Minimal tour of the TK4CPP::Process API.
 *
 * Usage: process_example [program [args...]]   (defaults to a portable "print a line" command)
 */
#include <TK4CPP/Process.hpp>

#include <chrono>
#include <iostream>

using namespace TK4CPP::Process;

int main(int argc, char** argv) {
    std::string exe;
    std::vector<std::string> args;
    if (argc > 1) {
        exe = argv[1];
        args.assign(argv + 2, argv + argc);
    } else {
#ifdef _WIN32
        exe = "cmd.exe";
        args = {"/c", "echo", "TK4CPP Process ready"};
#else
        exe = "printf";
        args = {"%s\\n", "TK4CPP Process ready"};
#endif
    }

    // 1) One-shot: launch, stream output, bounded wait.
    Options options;
    options.on_stdout = [](std::string_view chunk) { std::cout << chunk << std::flush; };
    options.on_stderr = [](std::string_view chunk) { std::cerr << chunk << std::flush; };
    auto result = Process::run_sync(exe, args, std::chrono::seconds(10), options);
    if (!result) {   // the program could not even be launched
        std::cerr << "launch failed: " << result.error().message << '\n';
        return 2;
    }
    if (result->timed_out) { std::cerr << "timed out\n"; return 3; }
    if (!result->success()) {
        std::cerr << "exit code " << result->status.legacy_code() << " after " << result->duration.count() << " ms\n";
        return 1;
    }

    // 2) Long-lived child with stdin: a filter that upper-cases is out of scope, so just echo via 'cat'/'findstr'.
    //    (Shown for API shape; skipped when the user supplied their own command.)
    if (argc == 1) {
#ifndef _WIN32
        Options io;
        io.stdin_mode = StdinMode::Pipe;
        Process cat("cat", {}, io);
        if (auto s = cat.start(); !s) { std::cerr << s.error().message << '\n'; return 2; }
        (void)cat.write_stdin("hello through stdin\n");
        cat.close_stdin();
        if (auto st = cat.wait(); st && st->success()) std::cout << cat.read_stdout();
#endif
    }
    return 0;
}
