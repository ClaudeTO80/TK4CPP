#pragma once

/**
 * @file LogLevel.h
 * @brief Severity levels used by the TK4CPP logging module.
 */

namespace TK4CPP::Logging {

/**
 * @brief Log severity levels.
 *
 * The numeric values are aligned with `spdlog::level::level_enum`, so levels
 * can be compared with the usual relational operators (`Trace < Critical`).
 */
enum class Level {
    Trace = 0, ///< Very fine-grained diagnostic output.
    Debug,     ///< Debugging information.
    Info,      ///< Normal operational messages.
    Warn,      ///< Something unexpected, but recoverable.
    Error,     ///< An operation failed.
    Critical,  ///< The application is likely unable to continue.
    Off        ///< Disables logging when used as a threshold.
};

} // namespace TK4CPP::Logging
