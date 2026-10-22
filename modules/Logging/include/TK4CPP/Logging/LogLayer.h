#pragma once

/**
 * @file LogLayer.h
 * @brief Umbrella header of the logging module, with the logging macros.
 *
 * The macros take a logger name followed by an `{fmt}` format string and its
 * arguments. The record is built only if the logger's level allows it, so the
 * arguments are **not evaluated** for suppressed levels. The call-site file,
 * line and function are attached to the record.
 */

#include "TK4CPP/Logging/LogConfig.h"
#include "TK4CPP/Logging/LogContext.h"
#include "TK4CPP/Logging/LogLevel.h"
#include "TK4CPP/Logging/LogManager.h"
#include "TK4CPP/Logging/LogNotification.h"
#include "TK4CPP/Logging/LogObserver.h"

#include <spdlog/spdlog.h>

/** @brief Returns the named logger (`std::shared_ptr<spdlog::logger>`). */
#define LL_GET_LOGGER(name) ::TK4CPP::Logging::LogManager::instance().getLogger(name)

/**
 * @brief Logs at an explicit `spdlog::level::level_enum`.
 * @param logger_name Name of the logger.
 * @param lvl         Severity.
 * @param ...         Format string and arguments.
 */
#define LL_LOG(logger_name, lvl, ...)                                                          \
    do {                                                                                       \
        const auto _ll_logger = LL_GET_LOGGER(logger_name);                                    \
        if (_ll_logger->should_log(lvl)) {                                                     \
            _ll_logger->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, lvl,    \
                            __VA_ARGS__);                                                      \
        }                                                                                      \
    } while (0)

/** @name Severity shortcuts @{ */
#define LL_TRACE(name, ...)    LL_LOG(name, ::spdlog::level::trace,    __VA_ARGS__)
#define LL_DEBUG(name, ...)    LL_LOG(name, ::spdlog::level::debug,    __VA_ARGS__)
#define LL_INFO(name, ...)     LL_LOG(name, ::spdlog::level::info,     __VA_ARGS__)
#define LL_WARN(name, ...)     LL_LOG(name, ::spdlog::level::warn,     __VA_ARGS__)
#define LL_ERROR(name, ...)    LL_LOG(name, ::spdlog::level::err,      __VA_ARGS__)
#define LL_CRITICAL(name, ...) LL_LOG(name, ::spdlog::level::critical, __VA_ARGS__)
/** @} */

#define LL_DETAIL_CAT2(a, b) a##b
#define LL_DETAIL_CAT(a, b) LL_DETAIL_CAT2(a, b)

/**
 * @brief Declares a ScopedLogContext lasting until the end of the enclosing block.
 * @param ctx_name Text identifying the scope.
 * @param ...      Optional logger name (default: "default").
 */
#define LL_SCOPE(ctx_name, ...) \
    ::TK4CPP::Logging::ScopedLogContext LL_DETAIL_CAT(_ll_scope_ctx_, __LINE__)(ctx_name __VA_OPT__(,) __VA_ARGS__)
