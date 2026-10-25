# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Claudio

cmake_minimum_required(VERSION 3.23)

project(TK4CPP_Zip
    VERSION     1.0.0
    DESCRIPTION "C++23 RAII wrapper around libzip, built as a static and/or shared library"
    LANGUAGES   CXX
)

# Works both as a TK4CPP sub-module (add_subdirectory from the root CMakeLists.txt) and
# standalone. The root options TK4CPP_BUILD_TESTS/INSTALL and TK4CPP_CXX_STANDARD are honoured
# when defined; otherwise the defaults below apply. The module is a compiled library: it can
# be built as a static library, a shared library, or both (see the TK4CPP_ZIP_BUILD_* options).

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ── Framework-aware defaults ────────────────────────────────────────────────
if(NOT DEFINED TK4CPP_CXX_STANDARD)
    set(TK4CPP_CXX_STANDARD 23)
endif()
if(DEFINED TK4CPP_BUILD_TESTS)
    set(_zip_tests_default ${TK4CPP_BUILD_TESTS})
else()
    set(_zip_tests_default ${PROJECT_IS_TOP_LEVEL})
endif()
if(DEFINED TK4CPP_INSTALL)
    set(_zip_install_default ${TK4CPP_INSTALL})
else()
    set(_zip_install_default ${PROJECT_IS_TOP_LEVEL})
endif()

if(DEFINED TK4CPP_BUILD_SHARED)
    set(_zip_shared_default ${TK4CPP_BUILD_SHARED})
else()
    set(_zip_shared_default ON)
endif()
if(DEFINED TK4CPP_BUILD_STATIC)
    set(_zip_static_default ${TK4CPP_BUILD_STATIC})
else()
    set(_zip_static_default ON)
endif()

# ── Options ─────────────────────────────────────────────────────────────────
option(TK4CPP_ZIP_BUILD_STATIC   "Build the static library"                     ${_zip_static_default})
option(TK4CPP_ZIP_BUILD_SHARED   "Build the shared library"                     ${_zip_shared_default})
option(TK4CPP_ZIP_BUILD_TESTS    "Build the Zip tests"                           ${_zip_tests_default})
option(TK4CPP_ZIP_BUILD_EXAMPLES "Build the Zip example programs"                ${PROJECT_IS_TOP_LEVEL})
option(TK4CPP_ZIP_INSTALL        "Generate install rules and the CMake package"  ${_zip_install_default})
option(TK4CPP_ZIP_BUILD_DOCS     "Build the Doxygen documentation of the Zip module" OFF)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# ── Advanced Dependency Management for libzip (Vendored / System) ───────────
set(TK4CPP_LIBZIP_SOURCE "AUTO" CACHE STRING "Where libzip comes from: AUTO, SYSTEM or VENDORED")
set_property(CACHE TK4CPP_LIBZIP_SOURCE PROPERTY STRINGS AUTO SYSTEM VENDORED)

set(TK4CPP_LIBZIP_ROOT        "" CACHE PATH     "libzip installation prefix")
set(TK4CPP_LIBZIP_DIR         "" CACHE PATH     "Directory containing libzipConfig.cmake")
set(TK4CPP_LIBZIP_INCLUDE_DIR "" CACHE PATH     "libzip include directory (containing zip.h)")
set(TK4CPP_LIBZIP_LIBRARY     "" CACHE FILEPATH "libzip library file")
set(TK4CPP_LIBZIP_EXTRA_LIBS  "" CACHE STRING   "Extra libraries needed by libzip when static (e.g. z;bz2;lzma;crypto)")

set(_custom_libzip FALSE)
if(TK4CPP_LIBZIP_ROOT OR TK4CPP_LIBZIP_DIR OR TK4CPP_LIBZIP_INCLUDE_DIR OR TK4CPP_LIBZIP_LIBRARY)
    set(_custom_libzip TRUE)
    if(TK4CPP_LIBZIP_SOURCE STREQUAL "VENDORED")
        message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_SOURCE=VENDORED conflicts with TK4CPP_LIBZIP_ROOT/DIR/INCLUDE_DIR/LIBRARY")
    endif()
    if(TK4CPP_LIBZIP_INCLUDE_DIR AND NOT TK4CPP_LIBZIP_LIBRARY OR TK4CPP_LIBZIP_LIBRARY AND NOT TK4CPP_LIBZIP_INCLUDE_DIR)
        message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_INCLUDE_DIR and TK4CPP_LIBZIP_LIBRARY must be given together")
    endif()
endif()

set(_use_vendored_zip FALSE)
set(libzip_FOUND FALSE)

if(_custom_libzip AND NOT TK4CPP_LIBZIP_INCLUDE_DIR AND NOT TK4CPP_LIBZIP_LIBRARY)
    if(TK4CPP_LIBZIP_DIR)
        set(libzip_DIR "${TK4CPP_LIBZIP_DIR}" CACHE PATH "" FORCE)
        find_package(libzip 1.11 CONFIG NO_DEFAULT_PATH)
    else()
        find_package(libzip 1.11 CONFIG QUIET NO_DEFAULT_PATH PATHS "${TK4CPP_LIBZIP_ROOT}")
    endif()
    
    if(NOT libzip_FOUND AND TK4CPP_LIBZIP_ROOT AND NOT TK4CPP_LIBZIP_DIR)
        find_path(_tk4cpp_lz_inc zip.h NO_DEFAULT_PATH PATHS "${TK4CPP_LIBZIP_ROOT}" PATH_SUFFIXES include)
        find_library(_tk4cpp_lz_lib NAMES zip libzip NO_DEFAULT_PATH PATHS "${TK4CPP_LIBZIP_ROOT}" PATH_SUFFIXES lib lib64)
        if(_tk4cpp_lz_inc AND _tk4cpp_lz_lib)
            set(TK4CPP_LIBZIP_INCLUDE_DIR "${_tk4cpp_lz_inc}")
            set(TK4CPP_LIBZIP_LIBRARY     "${_tk4cpp_lz_lib}")
        endif()
    endif()
    if(NOT libzip_FOUND AND NOT (TK4CPP_LIBZIP_INCLUDE_DIR AND TK4CPP_LIBZIP_LIBRARY))
        message(FATAL_ERROR "TK4CPP Zip: no libzip >= 1.11 found with TK4CPP_LIBZIP_ROOT='${TK4CPP_LIBZIP_ROOT}' TK4CPP_LIBZIP_DIR='${TK4CPP_LIBZIP_DIR}'")
    endif()
endif()

if(TK4CPP_LIBZIP_INCLUDE_DIR AND TK4CPP_LIBZIP_LIBRARY)
    if(NOT EXISTS "${TK4CPP_LIBZIP_INCLUDE_DIR}/zip.h")
        message(FATAL_ERROR "TK4CPP Zip: ${TK4CPP_LIBZIP_INCLUDE_DIR}/zip.h not found")
    endif()
    if(NOT EXISTS "${TK4CPP_LIBZIP_LIBRARY}")
        message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_LIBRARY does not exist: ${TK4CPP_LIBZIP_LIBRARY}")
    endif()
    
    # Crea il target atteso internamente dal modulo Zip wrapper
    add_library(libzip::zip UNKNOWN IMPORTED)
    set_target_properties(libzip::zip PROPERTIES
        IMPORTED_LOCATION             "${TK4CPP_LIBZIP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TK4CPP_LIBZIP_INCLUDE_DIR}")
        
    if(TK4CPP_LIBZIP_EXTRA_LIBS)
        set_property(TARGET libzip::zip APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${TK4CPP_LIBZIP_EXTRA_LIBS})
    endif()
    set(libzip_FOUND TRUE)
    message(STATUS "TK4CPP Zip: using manual libzip: ${TK4CPP_LIBZIP_LIBRARY}")
elseif(_custom_libzip)
    message(STATUS "TK4CPP Zip: using the libzip selected by TK4CPP_LIBZIP_ROOT/DIR")
elseif(NOT TK4CPP_LIBZIP_SOURCE STREQUAL "VENDORED")
    find_package(libzip 1.11 CONFIG QUIET)
    if(NOT libzip_FOUND)
        if(TK4CPP_LIBZIP_SOURCE STREQUAL "SYSTEM")
            message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_SOURCE=SYSTEM but no system libzip >= 1.11 was found")
        endif()
        set(_use_vendored_zip TRUE)
    endif()
else()
    set(_use_vendored_zip TRUE)
endif()

if(_use_vendored_zip)
    # Assumiamo la stessa cartella centralizzata 3rdParty/libzip come convenzione del framework
    if(DEFINED TK4CPP_THIRD_PARTY_FOLDER)
        set(_libzip_dir "${TK4CPP_THIRD_PARTY_FOLDER}/libzip")
    else()
        set(_libzip_dir "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/libzip")
    endif()

    if(NOT EXISTS "${_libzip_dir}/CMakeLists.txt")
        message(FATAL_ERROR "TK4CPP Zip: vendored libzip not found in ${_libzip_dir}")
    endif()
    
    function(_tk4cpp_add_vendored_libzip)
        set(BUILD_SHARED_LIBS OFF)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
        set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
        set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
        set(ENABLE_WINDOWS_CRYPTO OFF CACHE BOOL "" FORCE)
        set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_DOC OFF CACHE BOOL "" FORCE)
        add_subdirectory("${_libzip_dir}" "${CMAKE_CURRENT_BINARY_DIR}/libzip" EXCLUDE_FROM_ALL)
    endfunction()
    _tk4cpp_add_vendored_libzip()
    message(STATUS "TK4CPP Zip: using the vendored libzip")
elseif(NOT _custom_libzip)
    message(STATUS "TK4CPP Zip: using the system libzip")
endif()

# ── Library targets ─────────────────────────────────────────────────────────
if(NOT TK4CPP_ZIP_BUILD_STATIC AND NOT TK4CPP_ZIP_BUILD_SHARED)
    message(FATAL_ERROR "TK4CPP::Zip: enable TK4CPP_ZIP_BUILD_STATIC and/or TK4CPP_ZIP_BUILD_SHARED")
endif()

set(_zip_sources
    src/Zip.cpp
    src/HighLevel.cpp)
set(_zip_private_headers
    src/Detail.h
    src/HighLevelDetail.h)
set(_zip_public_headers
    include/ZipWrapper/Export.h
    include/ZipWrapper/Zip.h
    include/ZipWrapper/HighLevel.h)

# Settings shared by both flavours.
function(_tk4cpp_zip_configure_library target)
    target_sources(${target} PRIVATE ${_zip_sources} ${_zip_private_headers} ${_zip_public_headers})
    target_compile_features(${target} PUBLIC cxx_std_${TK4CPP_CXX_STANDARD})
    target_include_directories(${target}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${target} PUBLIC libzip::zip)
    
    # Nascondi i simboli di libzip se vendored e build condivisa (Linux) per parità con modulo XML
    if(_use_vendored_zip AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        get_target_property(_type ${target} TYPE)
        if(_type STREQUAL "SHARED_LIBRARY")
            target_link_options(${target} PRIVATE "LINKER:--exclude-libs,ALL")
        endif()
    endif()

    set_target_properties(${target} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        FOLDER "TK4CPP/Zip")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(TK4CPP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(TK4CPP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

set(_zip_flavours "")

if(TK4CPP_ZIP_BUILD_STATIC)
    add_library(TK4CPP_Zip_static STATIC)
    add_library(TK4CPP::Zip_static ALIAS TK4CPP_Zip_static)
    _tk4cpp_zip_configure_library(TK4CPP_Zip_static)
    if(WIN32)
        set(_zip_static_name TK4CPP_ZIP_static)
    else()
        set(_zip_static_name TK4CPP_ZIP)
    endif()
    set_target_properties(TK4CPP_Zip_static PROPERTIES
        EXPORT_NAME Zip_static
