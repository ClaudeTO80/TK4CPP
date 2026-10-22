/**
 * @file 005_InputOutputFiles.cpp
 * @brief Tasks that read an input file and write an output file, errors reported via std::expected.
 */
#include "TK4CPP/ThreadPool/ThreadPool.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace TK4CPP::TP;
namespace fs = std::filesystem;

namespace {

/** @brief First @p n Fibonacci numbers (n <= 93 fits in 64 bits). */
std::vector<std::uint64_t> fibonacci(int n) {
    std::vector<std::uint64_t> v;
    v.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        v.push_back(i < 2 ? static_cast<std::uint64_t>(i) : v[i - 1] + v[i - 2]);
    return v;
}

/**
 * @brief Reads a number from @p dir/input.txt and writes the Fibonacci series to @p dir/output.txt.
 * @return The output path, or a message describing what failed.
 */
std::expected<fs::path, std::string> processFolder(const fs::path& dir) {
    std::ifstream in(dir / "input.txt");
    int n = 0;
    if (!in || !(in >> n)) return std::unexpected("cannot read a number from " + (dir / "input.txt").string());
    if (n < 1 || n > 93)   return std::unexpected("n out of range [1, 93]: " + std::to_string(n));

    std::ofstream out(dir / "output.txt");
    if (!out) return std::unexpected("cannot write " + (dir / "output.txt").string());
    out << "Index,Value\n";
    const auto series = fibonacci(n);
    for (std::size_t i = 0; i < series.size(); ++i) out << i << ',' << series[i] << '\n';
    return dir / "output.txt";
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "tk4cpp_threadpool_example_005";
    fs::remove_all(root);

    ThreadPoolConfig cfg;
    cfg.minThreads = 2;
    cfg.maxThreads = 2;
    ThreadPool pool(cfg);
    pool.start();

    std::mt19937 gen{ std::random_device{}() };
    std::uniform_int_distribution<int> dist(10, 90);

    // One folder per job; the last one holds an invalid value to show the error path.
    std::vector<std::future<std::expected<fs::path, std::string>>> results;
    for (int job = 0; job < 5; ++job) {
        const fs::path dir = root / ("job_" + std::to_string(job));
        fs::create_directories(dir);
        std::ofstream(dir / "input.txt") << (job == 4 ? 1000 : dist(gen));

        auto fut = pool.submitWithFuture([dir] { return processFolder(dir); }, "fib-" + std::to_string(job));
        if (!fut) { std::cerr << "submit failed: " << toString(fut.error()) << '\n'; return 1; }
        results.push_back(std::move(*fut));
    }

    for (auto& f : results) {
        auto r = f.get();
        if (r) std::cout << "ok:     " << r->string() << " (" << fs::file_size(*r) << " bytes)\n";
        else   std::cout << "failed: " << r.error() << '\n';
    }

    pool.shutdown();
    fs::remove_all(root);
    return 0;
}
