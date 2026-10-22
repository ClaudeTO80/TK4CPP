// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file Detail.h
 * @brief Private helpers shared by the library's translation units.
 *
 * This header is **not installed**: it is an implementation detail and may
 * change without notice.
 */

#pragma once

#include <ZipWrapper/Zip.h>

#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace Zip::detail {


/** @brief Throw an Exception from a zip_error_t and release it. */
[[noreturn]] inline void throwZipError(zip_error_t& ze,
                                       std::string_view operation) {
    const int z = zip_error_code_zip(&ze);
    const int sy = zip_error_code_system(&ze);
    std::string msg(operation);
    msg += ": ";
    const char* s = zip_error_strerror(&ze);
    msg += s ? s : "unknown error";
    zip_error_fini(&ze);
    throw Exception(z, sy, std::move(msg));
}

/** @brief Convert a path to a UTF-8 std::string. */
[[nodiscard]] inline std::string toUtf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

/** @brief Convert a UTF-8 archive name to a path (u8path replacement). */
[[nodiscard]] inline std::filesystem::path fromUtf8(std::string_view s) {
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

/** @brief True if @p candidate equals @p root or lies below it (lexically). */
[[nodiscard]] inline bool isInside(const std::filesystem::path& root,
                                   const std::filesystem::path& candidate) {
    const auto rel = candidate.lexically_relative(root);
    if (rel.empty()) return false;
    return *rel.begin() != "..";
}

/**
 * @brief State shared with libzip's progress/cancel callbacks.
 *
 * libzip is a C library: an exception must never unwind through its stack
 * frames. The trampolines below catch everything, remember the exception and
 * ask libzip to cancel; Archive::close() rethrows it afterwards.
 */
struct CallbackState {
    std::function<bool(double)> progress;
    std::exception_ptr error;
    bool cancel = false;
};

inline void progressTrampoline(zip_t*, double fraction, void* userData) noexcept {
    auto* state = static_cast<CallbackState*>(userData);
    if (state->cancel) return;
    try {
        if (!state->progress(fraction)) state->cancel = true;
    } catch (...) {
        state->error = std::current_exception();
        state->cancel = true;
    }
}

inline int cancelTrampoline(zip_t*, void* userData) noexcept {
    return static_cast<CallbackState*>(userData)->cancel ? 1 : 0;
}

} // namespace Zip::detail
