/**
 * @file Export.h
 * @brief Symbol visibility macro for the TK4CPP::ThreadPool library.
 *
 * The CMake target defines TK4CPP_TP_SHARED for shared builds (both when
 * building and when consuming the library) and TK4CPP_TP_BUILDING only while
 * the library itself is being compiled. Static builds leave the macro empty.
 */
#pragma once

#if defined(TK4CPP_TP_SHARED)
#  if defined(_WIN32)
#    if defined(TK4CPP_TP_BUILDING)
#      define TK4CPP_TP_API __declspec(dllexport)
#    else
#      define TK4CPP_TP_API __declspec(dllimport)
#    endif
#  elif defined(__GNUC__) || defined(__clang__)
#    define TK4CPP_TP_API __attribute__((visibility("default")))
#  else
#    define TK4CPP_TP_API
#  endif
#else
#  define TK4CPP_TP_API
#endif
