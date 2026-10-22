/**
 * @file ThreadPoolConfig.h
 * @brief Tunable parameters of a ThreadPool.
 */
#pragma once

#include "Types.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

namespace TK4CPP::TP {

/** @brief Configuration passed to the ThreadPool constructor. */
struct ThreadPoolConfig {
    std::size_t minThreads    = 2;    ///< Workers started by start() and never retired.
    std::size_t maxThreads    = 8;    ///< Upper bound for dynamic scaling (raised to minThreads if lower; at least 1).
    std::size_t maxQueueSize  = 0;    ///< Queue capacity, 0 = unbounded.
    OverflowPolicy overflowPolicy = OverflowPolicy::Reject;  ///< Behaviour when the bounded queue is full.
    std::string name = "ThreadPool";  ///< Prefix of the OS thread names ("name-<id>").

    /// A worker above minThreads retires after being idle for this long (0 = never retire).
    std::chrono::seconds idleTimeout{ 60 };

    /** @brief Default configuration. */
    static ThreadPoolConfig defaults() { return {}; }

    /** @brief One worker per hardware thread, no dynamic scaling. */
    static ThreadPoolConfig cpuBound() {
        ThreadPoolConfig c;
        c.minThreads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        c.maxThreads = c.minThreads;
        return c;
    }

    /** @brief Few workers at rest, up to 4x hardware threads under load. */
    static ThreadPoolConfig ioBound() {
        ThreadPoolConfig c;
        c.minThreads = 4;
        c.maxThreads = std::max<std::size_t>(c.minThreads, 4u * std::thread::hardware_concurrency());
        return c;
    }
};

} // namespace TK4CPP::TP
