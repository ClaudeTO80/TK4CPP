// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file Export.h
 * @brief Symbol visibility for the static and the shared library.
 *
 * ZipWrapper is built as a static library, a shared library, or both (see
 * @ref linking). This header is the only place that knows which one a
 * translation unit is compiled for:
 *
 * | Situation                              | Defined by CMake               | ZIPWRAPPER_API expands to |
 * |----------------------------------------|--------------------------------|---------------------------|
 * | building or using the **static** library | `ZIPWRAPPER_STATIC` (PUBLIC)   | nothing                   |
 * | building the **shared** library        | `ZIPWRAPPER_BUILDING_SHARED`   | `__declspec(dllexport)` on Windows, default visibility elsewhere |
 * | using the **shared** library           | nothing                        | `__declspec(dllimport)` on Windows, default visibility elsewhere |
 *
 * The CMake targets set these definitions for you. If you compile the sources
 * by hand, define `ZIPWRAPPER_STATIC` for a static build and
 * `ZIPWRAPPER_BUILDING_SHARED` when building the DLL or shared object.
 *
 * @note The public classes expose standard-library types (`std::string`,
 * `std::filesystem::path`, `std::function`, ...). A shared build therefore
 * requires the library and its users to be built with the same compiler
 * family, the same standard library and, on Windows, the same CRT. If that
 * cannot be guaranteed, use the static library.
 */

#pragma once

/**
 * @def ZIPWRAPPER_API
 * @brief Marks a symbol as part of the library's public binary interface.
 */
#if defined(ZIPWRAPPER_STATIC)
#    define ZIPWRAPPER_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#    if defined(ZIPWRAPPER_BUILDING_SHARED)
#        define ZIPWRAPPER_API __declspec(dllexport)
#    else
#        define ZIPWRAPPER_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) || defined(__clang__)
#    define ZIPWRAPPER_API __attribute__((visibility("default")))
#else
#    define ZIPWRAPPER_API
#endif

/**
 * @def ZIPWRAPPER_DISABLE_DLL_WARNINGS_BEGIN
 * @brief Silences MSVC C4251/C4275 around exported classes whose members are
 *        standard-library types (see the note in this file).
 */
/**
 * @def ZIPWRAPPER_DISABLE_DLL_WARNINGS_END
 * @brief Restores the warning state saved by ZIPWRAPPER_DISABLE_DLL_WARNINGS_BEGIN.
 */
#if defined(_MSC_VER)
#    define ZIPWRAPPER_DISABLE_DLL_WARNINGS_BEGIN \
         __pragma(warning(push)) __pragma(warning(disable : 4251 4275))
#    define ZIPWRAPPER_DISABLE_DLL_WARNINGS_END __pragma(warning(pop))
#else
#    define ZIPWRAPPER_DISABLE_DLL_WARNINGS_BEGIN
#    define ZIPWRAPPER_DISABLE_DLL_WARNINGS_END
#endif
