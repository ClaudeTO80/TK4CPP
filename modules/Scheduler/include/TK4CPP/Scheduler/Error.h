/**
 * @file Error.h
 * @brief Error type and result alias used by the whole scheduler module.
 */
#pragma once

#include <expected>
#include <string>
#include <utility>

namespace TK4CPP::Scheduler {

/// @brief Error categories reported by the scheduler module.
enum class ErrorCode {
    InvalidArgument, ///< A parameter is empty, out of range or otherwise unusable.
    InvalidCron,     ///< A cron expression could not be parsed.
    NotFound,        ///< A job or trigger id is unknown.
    AlreadyExists,   ///< A job or trigger id is already in use.
    UnknownJobType,  ///< No handler is registered for the job type.
    InvalidState,    ///< The operation is not allowed in the current scheduler/trigger state.
    StoreFailure,    ///< The persistence backend reported an error.
    ExecutorRejected ///< The executor refused the execution (stopped, queue full).
};

/// @brief Error value carried by @ref Result.
struct Error {
    ErrorCode code;      ///< Machine readable category.
    std::string message; ///< Human readable description.
};

/// @brief Result alias: either a value or an @ref Error (C++23 std::expected).
template <class T>
using Result = std::expected<T, Error>;

/**
 * @brief Builds an unexpected value ready to be returned from a function returning @ref Result.
 * @param code    Error category.
 * @param message Human readable description.
 */
[[nodiscard]] inline std::unexpected<Error> makeError(ErrorCode code, std::string message) {
    return std::unexpected<Error>{Error{code, std::move(message)}};
}

/// @brief Returns a stable textual name for an error code.
[[nodiscard]] constexpr const char* toString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::InvalidCron: return "InvalidCron";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::UnknownJobType: return "UnknownJobType";
    case ErrorCode::InvalidState: return "InvalidState";
    case ErrorCode::StoreFailure: return "StoreFailure";
    case ErrorCode::ExecutorRejected: return "ExecutorRejected";
    }
    return "Unknown";
}

} // namespace TK4CPP::Scheduler
