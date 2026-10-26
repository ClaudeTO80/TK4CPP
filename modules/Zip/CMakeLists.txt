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
#
# libzip can come from two places, selected with TK4CPP_LIBZIP_SOURCE:
#   VENDORED : the copy in 3rdParty/libzip (or ${TK4CPP_THIRD_PARTY_FOLDER}/libzip), always.
#   SYSTEM   : a libzip already installed on the machine, never falls back to the vendored one.
#   AUTO     : the installed libzip when found, the vendored one otherwise (default).
# For SYSTEM/AUTO the installed copy can be pointed to with TK4CPP_LIBZIP_ROOT, TK4CPP_LIBZIP_DIR
# or TK4CPP_LIBZIP_INCLUDE_DIR + TK4CPP_LIBZIP_LIBRARY (+ TK4CPP_LIBZIP_EXTRA_LIBS).

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

# ── libzip dependency: vendored or installed on the machine ─────────────────
set(TK4CPP_LIBZIP_SOURCE "AUTO" CACHE STRING "Where libzip comes from: AUTO, SYSTEM or VENDORED")
set_property(CACHE TK4CPP_LIBZIP_SOURCE PROPERTY STRINGS AUTO SYSTEM VENDORED)

set(TK4CPP_LIBZIP_ROOT        "" CACHE PATH     "Prefix of an installed libzip (SYSTEM/AUTO only)")
set(TK4CPP_LIBZIP_DIR         "" CACHE PATH     "Directory containing libzip-config.cmake (SYSTEM/AUTO only)")
set(TK4CPP_LIBZIP_INCLUDE_DIR "" CACHE PATH     "libzip include directory containing zip.h (SYSTEM/AUTO only)")
set(TK4CPP_LIBZIP_LIBRARY     "" CACHE FILEPATH "libzip library file (SYSTEM/AUTO only)")
set(TK4CPP_LIBZIP_EXTRA_LIBS  "" CACHE STRING   "Extra libraries needed by libzip when static (e.g. z;bz2;lzma;crypto)")

if(NOT TK4CPP_LIBZIP_SOURCE MATCHES "^(AUTO|SYSTEM|VENDORED)$")
    message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_SOURCE must be AUTO, SYSTEM or VENDORED (got '${TK4CPP_LIBZIP_SOURCE}')")
endif()

set(_custom_libzip FALSE)
if(TK4CPP_LIBZIP_ROOT OR TK4CPP_LIBZIP_DIR OR TK4CPP_LIBZIP_INCLUDE_DIR OR TK4CPP_LIBZIP_LIBRARY)
    set(_custom_libzip TRUE)
    if(TK4CPP_LIBZIP_SOURCE STREQUAL "VENDORED")
        message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_SOURCE=VENDORED conflicts with TK4CPP_LIBZIP_ROOT/DIR/INCLUDE_DIR/LIBRARY")
    endif()
    if((TK4CPP_LIBZIP_INCLUDE_DIR AND NOT TK4CPP_LIBZIP_LIBRARY) OR (TK4CPP_LIBZIP_LIBRARY AND NOT TK4CPP_LIBZIP_INCLUDE_DIR))
        message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_INCLUDE_DIR and TK4CPP_LIBZIP_LIBRARY must be given together")
    endif()
endif()

set(_use_vendored_zip FALSE)
set(_libzip_origin    "")   # "manual", "custom" or "system", used for messages only
set(libzip_FOUND FALSE)

if(TK4CPP_LIBZIP_SOURCE STREQUAL "VENDORED")
    set(_use_vendored_zip TRUE)

elseif(TK4CPP_LIBZIP_INCLUDE_DIR AND TK4CPP_LIBZIP_LIBRARY)
    # Explicit header directory + library file.
    if(NOT EXISTS "${TK4CPP_LIBZIP_INCLUDE_DIR}/zip.h")
        message(FATAL_ERROR "TK4CPP Zip: ${TK4CPP_LIBZIP_INCLUDE_DIR}/zip.h not found")
    endif()
    if(NOT EXISTS "${TK4CPP_LIBZIP_LIBRARY}")
        message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_LIBRARY does not exist: ${TK4CPP_LIBZIP_LIBRARY}")
    endif()
    add_library(libzip::zip UNKNOWN IMPORTED)
    set_target_properties(libzip::zip PROPERTIES
        IMPORTED_LOCATION             "${TK4CPP_LIBZIP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TK4CPP_LIBZIP_INCLUDE_DIR}")
    if(TK4CPP_LIBZIP_EXTRA_LIBS)
        set_property(TARGET libzip::zip APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${TK4CPP_LIBZIP_EXTRA_LIBS})
    endif()
    set(libzip_FOUND TRUE)
    set(_libzip_origin "manual: ${TK4CPP_LIBZIP_LIBRARY}")

elseif(_custom_libzip)
    # A prefix or a config directory was given: look only there, never fall back silently.
    if(TK4CPP_LIBZIP_DIR)
        set(libzip_DIR "${TK4CPP_LIBZIP_DIR}" CACHE PATH "" FORCE)
        find_package(libzip 1.11 CONFIG QUIET NO_DEFAULT_PATH)
    else()
        find_package(libzip 1.11 CONFIG QUIET NO_DEFAULT_PATH PATHS "${TK4CPP_LIBZIP_ROOT}")
    endif()

    if(NOT libzip_FOUND AND TK4CPP_LIBZIP_ROOT AND NOT TK4CPP_LIBZIP_DIR)
        # No CMake package in the prefix: look for zip.h and the library directly.
        find_path(_tk4cpp_lz_inc zip.h NO_DEFAULT_PATH PATHS "${TK4CPP_LIBZIP_ROOT}" PATH_SUFFIXES include)
        find_library(_tk4cpp_lz_lib NAMES zip libzip NO_DEFAULT_PATH PATHS "${TK4CPP_LIBZIP_ROOT}" PATH_SUFFIXES lib lib64)
        if(_tk4cpp_lz_inc AND _tk4cpp_lz_lib)
            add_library(libzip::zip UNKNOWN IMPORTED)
            set_target_properties(libzip::zip PROPERTIES
                IMPORTED_LOCATION             "${_tk4cpp_lz_lib}"
                INTERFACE_INCLUDE_DIRECTORIES "${_tk4cpp_lz_inc}")
            if(TK4CPP_LIBZIP_EXTRA_LIBS)
                set_property(TARGET libzip::zip APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${TK4CPP_LIBZIP_EXTRA_LIBS})
            endif()
            set(libzip_FOUND TRUE)
        endif()
    endif()
    if(NOT libzip_FOUND)
        message(FATAL_ERROR "TK4CPP Zip: no libzip >= 1.11 found with TK4CPP_LIBZIP_ROOT='${TK4CPP_LIBZIP_ROOT}' TK4CPP_LIBZIP_DIR='${TK4CPP_LIBZIP_DIR}'")
    endif()
    set(_libzip_origin "custom location")

else()
    # SYSTEM or AUTO without hints: standard CMake search paths.
    find_package(libzip 1.11 CONFIG QUIET)
    if(libzip_FOUND)
        set(_libzip_origin "system")
    elseif(TK4CPP_LIBZIP_SOURCE STREQUAL "SYSTEM")
        message(FATAL_ERROR "TK4CPP Zip: TK4CPP_LIBZIP_SOURCE=SYSTEM but no installed libzip >= 1.11 was found "
                            "(use TK4CPP_LIBZIP_ROOT or TK4CPP_LIBZIP_DIR, or TK4CPP_LIBZIP_SOURCE=VENDORED)")
    else()
        set(_use_vendored_zip TRUE)
    endif()
endif()

if(_use_vendored_zip)
    if(DEFINED TK4CPP_THIRD_PARTY_FOLDER)
        set(_libzip_dir "${TK4CPP_THIRD_PARTY_FOLDER}/libzip")
    else()
        set(_libzip_dir "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/libzip")
    endif()

    if(NOT EXISTS "${_libzip_dir}/CMakeLists.txt")
        message(FATAL_ERROR "TK4CPP Zip: vendored libzip not found in ${_libzip_dir}")
    endif()

    # A function keeps BUILD_SHARED_LIBS and the other settings local to the libzip build.
    function(_tk4cpp_add_vendored_libzip)
        # libzip declares BUILD_SHARED_LIBS with option() under an old policy setting, which would
        # discard the local variable below and build it shared: CMP0077=NEW makes it honoured.
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(BUILD_SHARED_LIBS OFF)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
        set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
        set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
        set(ENABLE_WINDOWS_CRYPTO OFF CACHE BOOL "" FORCE)
        set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
        set(BUILD_OSSFUZZ OFF CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_DOC OFF CACHE BOOL "" FORCE)
        add_subdirectory("${_libzip_dir}" "${CMAKE_CURRENT_BINARY_DIR}/libzip" EXCLUDE_FROM_ALL)
    endfunction()
    _tk4cpp_add_vendored_libzip()

    # The vendored build exposes the target "zip"; give it the same name as the installed one.
    if(NOT TARGET libzip::zip)
        add_library(libzip::zip ALIAS zip)
    endif()
    message(STATUS "TK4CPP Zip: using the vendored libzip (${_libzip_dir})")
else()
    message(STATUS "TK4CPP Zip: using the installed libzip (${_libzip_origin})")
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

    # Do not re-export the symbols of the vendored static libzip from the shared library.
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
        OUTPUT_NAME ${_zip_static_name})
    target_compile_definitions(TK4CPP_Zip_static PUBLIC ZIPWRAPPER_STATIC)
    list(APPEND _zip_flavours static)
endif()

if(TK4CPP_ZIP_BUILD_SHARED)
    add_library(TK4CPP_Zip_shared SHARED)
    add_library(TK4CPP::Zip_shared ALIAS TK4CPP_Zip_shared)
    _tk4cpp_zip_configure_library(TK4CPP_Zip_shared)
    set_target_properties(TK4CPP_Zip_shared PROPERTIES
        EXPORT_NAME Zip_shared
        OUTPUT_NAME TK4CPP_ZIP
        VERSION     ${PROJECT_VERSION}
        SOVERSION   ${PROJECT_VERSION_MAJOR}
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    target_compile_definitions(TK4CPP_Zip_shared PRIVATE ZIPWRAPPER_BUILDING_SHARED)
    if(NOT DEFINED CMAKE_INSTALL_RPATH AND NOT CMAKE_SKIP_INSTALL_RPATH)
        if(APPLE)
            set(_zip_origin "@loader_path")
        else()
            set(_zip_origin "$ORIGIN")
        endif()
        set_target_properties(TK4CPP_Zip_shared PROPERTIES
            INSTALL_RPATH "${_zip_origin}"
            INSTALL_RPATH_USE_LINK_PATH ON)
    endif()
    list(APPEND _zip_flavours shared)
endif()

if(BUILD_SHARED_LIBS AND TK4CPP_ZIP_BUILD_SHARED)
    set(_zip_default_flavour shared)
elseif(TK4CPP_ZIP_BUILD_STATIC)
    set(_zip_default_flavour static)
else()
    set(_zip_default_flavour shared)
endif()
add_library(TK4CPP::Zip ALIAS TK4CPP_Zip_${_zip_default_flavour})
add_library(ZipWrapper::ZipWrapper ALIAS TK4CPP_Zip_${_zip_default_flavour})

get_property(_built GLOBAL PROPERTY TK4CPP_BUILT_MODULES)
if(NOT "Zip" IN_LIST _built)
    set_property(GLOBAL APPEND PROPERTY TK4CPP_BUILT_MODULES Zip)
endif()

# ── Tests and examples ──────────────────────────────────────────────────────
function(_tk4cpp_zip_add_test name source linktarget)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${linktarget})
    set_target_properties(${name} PROPERTIES FOLDER "TK4CPP/Zip")
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive- /utf-8)
        if(TK4CPP_WARNINGS_AS_ERRORS)
            target_compile_options(${name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
        if(TK4CPP_WARNINGS_AS_ERRORS)
            target_compile_options(${name} PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

if(TK4CPP_ZIP_BUILD_TESTS)
    enable_testing()
    foreach(_flavour IN LISTS _zip_flavours)
        if(_flavour STREQUAL _zip_default_flavour)
            set(_suffix "")
        else()
            set(_suffix "_${_flavour}")
        endif()
        _tk4cpp_zip_add_test(zip_tests${_suffix}           tests/test_zip.cpp        TK4CPP_Zip_${_flavour})
        _tk4cpp_zip_add_test(zip_highlevel_tests${_suffix} tests/test_highlevel.cpp  TK4CPP_Zip_${_flavour})
    endforeach()
    add_subdirectory(docs/snippets)
endif()

if(TK4CPP_ZIP_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# ── Documentation ───────────────────────────────────────────────────────────
if(TK4CPP_ZIP_BUILD_DOCS)
    find_package(Doxygen REQUIRED OPTIONAL_COMPONENTS dot)
    if(TARGET Doxygen::dot)
        set(_zip_have_dot YES)
    else()
        message(FATAL_ERROR "TK4CPP_ZIP_BUILD_DOCS needs Graphviz ('dot') for the diagrams in the manual")
    endif()
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile _zip_doxyfile)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.docs
        "${_zip_doxyfile}\n"
        "# ---- added by CMake ----\n"
        "OUTPUT_DIRECTORY = ${CMAKE_CURRENT_BINARY_DIR}/docs\n"
        "PROJECT_NUMBER   = ${PROJECT_VERSION}\n"
        "HAVE_DOT         = ${_zip_have_dot}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)
    if(PROJECT_IS_TOP_LEVEL)
        set(_zip_docs_target docs)
    else()
        set(_zip_docs_target TK4CPP_Zip_docs)
    endif()
    add_custom_target(${_zip_docs_target}
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.docs
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating the Zip documentation with Doxygen (warnings are errors)"
        VERBATIM)
endif()

# ── Install / find_package support ──────────────────────────────────────────
if(DEFINED TK4CPP_INSTALL_CMAKEDIR)
    set(_zip_cmakedir "${TK4CPP_INSTALL_CMAKEDIR}")
else()
    set(_zip_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/TK4CPP_Zip")
endif()

if(TK4CPP_ZIP_INSTALL)
    set(_zip_install_targets "")
    foreach(_flavour IN LISTS _zip_flavours)
        list(APPEND _zip_install_targets TK4CPP_Zip_${_flavour})
    endforeach()
    # The vendored libzip is a static dependency of the exported targets: install it with them.
    if(_use_vendored_zip AND TARGET zip)
        list(APPEND _zip_install_targets zip)
    endif()

    install(TARGETS ${_zip_install_targets}
        EXPORT   TK4CPP_ZipTargets
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
                 COMPONENT   TK4CPP_Zip_Runtime
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
                 COMPONENT   TK4CPP_Zip_Runtime
                 NAMELINK_COMPONENT TK4CPP_Zip_Development
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
                 COMPONENT   TK4CPP_Zip_Development
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY include/ZipWrapper
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT   TK4CPP_Zip_Development)
    install(EXPORT TK4CPP_ZipTargets
        FILE        TK4CPP_ZipTargets.cmake
        NAMESPACE   TK4CPP::
        DESTINATION ${_zip_cmakedir}
        COMPONENT   TK4CPP_Zip_Development)

    # Consumed by cmake/TK4CPP_ZipConfig.cmake.in: with an installed libzip the package config
    # must call find_dependency(libzip); with the vendored one nothing is needed.
    set(TK4CPP_ZIP_DEFAULT_FLAVOUR "${_zip_default_flavour}")
    set(TK4CPP_ZIP_USES_VENDORED_LIBZIP ${_use_vendored_zip})
    # The vendored (static) libzip is exported with its own dependencies (ZLIB, BZip2, LibLZMA):
    # collect the CMake packages the installed config has to find again.
    set(TK4CPP_ZIP_VENDORED_PACKAGES "")
    if(_use_vendored_zip AND TARGET zip)
        get_target_property(_zip_vendored_deps zip LINK_LIBRARIES)
        foreach(_dep IN LISTS _zip_vendored_deps)
            if(_dep MATCHES "^(ZLIB|BZip2|LibLZMA)::")
                list(APPEND TK4CPP_ZIP_VENDORED_PACKAGES ${CMAKE_MATCH_1})
            endif()
        endforeach()
        list(REMOVE_DUPLICATES TK4CPP_ZIP_VENDORED_PACKAGES)
    endif()
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TK4CPP_ZipConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_ZipConfig.cmake"
        INSTALL_DESTINATION ${_zip_cmakedir})
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_ZipConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_ZipConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_ZipConfigVersion.cmake"
        DESTINATION ${_zip_cmakedir}
        COMPONENT   TK4CPP_Zip_Development)
endif()

if(_use_vendored_zip)
    set(_zip_libzip_kind vendored)
else()
    set(_zip_libzip_kind installed)
endif()
message(STATUS "TK4CPP::Zip ${PROJECT_VERSION}: flavours=${_zip_flavours} default=${_zip_default_flavour}"
               " libzip=${_zip_libzip_kind}"
               " tests=${TK4CPP_ZIP_BUILD_TESTS} install=${TK4CPP_ZIP_INSTALL}")
