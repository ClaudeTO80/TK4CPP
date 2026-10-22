#pragma once

/**
 * @file LogContext.h
 * @brief Scope-based begin/end logging with elapsed time.
 */

#include <chrono>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>

#include "TK4CPP/Logging/LogManager.h"

namespace TK4CPP::Logging {

/**
 * @brief Logs a `BEGIN` line on construction and an `END` line, with the elapsed
 *        time, on destruction.
 *
 * Both lines are emitted at Info level and carry the source location of the
 * constructor call, so `%s:%#` in the pattern points at the scope being traced.
 */
class ScopedLogContext {
public:
    /**
     * @brief Opens a traced scope.
     * @param context    Free text identifying the scope (e.g. a request id).
     * @param loggerName Logger used for both lines.
     * @param where      Call site; leave the default.
     */
    explicit ScopedLogContext(std::string context,
                              std::string_view loggerName = "default",
                              std::source_location where = std::source_location::current());

    /** @brief Logs the `END` line. */
    ~ScopedLogContext();

    ScopedLogContext(const ScopedLogContext&) = delete;
    ScopedLogContext& operator=(const ScopedLogContext&) = delete;

    /** @brief Time elapsed since construction. */
    std::chrono::milliseconds elapsed() const;

private:
    std::shared_ptr<spdlog::logger>       _logger;
    std::string                           _context;
    std::source_location                  _where;
    std::chrono::steady_clock::time_point _start = std::chrono::steady_clock::now();
};

} // namespace TK4CPP::Logging
