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

# libzip >= 1.11 is required (zip_source_zip_file, zip_file_set_encryption ...).
find_package(libzip 1.11 CONFIG REQUIRED)

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
    # libzip is PUBLIC: Zip.h includes <zip.h>, and a static build must hand
    # libzip's own dependencies (zlib, bzip2, ...) on to the final link.
    target_link_libraries(${target} PUBLIC libzip::zip)
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
    # The static archive and the DLL import library would both be tk4cpp_zip.lib on Windows.
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
    # An installed libtk4cpp_zip.so must find libzip.so on its own: the RUNPATH of the
    # executable is not consulted for the dependencies of a shared library. Unless the
    # project chooses otherwise, record the library's own directory and the directories
    # of the libraries it links (libzip). System directories are never recorded.
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

# TK4CPP::Zip is the flavour BUILD_SHARED_LIBS asks for, or the only one built.
if(BUILD_SHARED_LIBS AND TK4CPP_ZIP_BUILD_SHARED)
    set(_zip_default_flavour shared)
elseif(TK4CPP_ZIP_BUILD_STATIC)
    set(_zip_default_flavour static)
else()
    set(_zip_default_flavour shared)
endif()
add_library(TK4CPP::Zip ALIAS TK4CPP_Zip_${_zip_default_flavour})
add_library(ZipWrapper::ZipWrapper ALIAS TK4CPP_Zip_${_zip_default_flavour})   # kept for the existing tests/examples

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
    # The same suites run against every flavour that is built, so a static-only
    # or shared-only defect (visibility, export macros) cannot go unnoticed.
    # The default flavour keeps the plain names; the other one gets a suffix.
    foreach(_flavour IN LISTS _zip_flavours)
        if(_flavour STREQUAL _zip_default_flavour)
            set(_suffix "")
        else()
            set(_suffix "_${_flavour}")
        endif()
        _tk4cpp_zip_add_test(zip_tests${_suffix}           tests/test_zip.cpp        TK4CPP_Zip_${_flavour})
        _tk4cpp_zip_add_test(zip_highlevel_tests${_suffix} tests/test_highlevel.cpp  TK4CPP_Zip_${_flavour})
    endforeach()

    # Code shown in the documentation, built and run as tests.
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
        # The pages contain diagrams, so Doxygen's strict build cannot succeed
        # without Graphviz; fail early with a clear message instead.
        message(FATAL_ERROR "TK4CPP_ZIP_BUILD_DOCS needs Graphviz ('dot') for the diagrams in the manual")
    endif()

    # One source of truth: the Doxyfile at the top of the module. CMake appends the settings
    # that depend on the build (a later assignment wins in a Doxyfile), so the output goes to
    # the build tree, not the source tree.
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile _zip_doxyfile)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.docs
        "${_zip_doxyfile}\n"
        "# ---- added by CMake ----\n"
        "OUTPUT_DIRECTORY = ${CMAKE_CURRENT_BINARY_DIR}/docs\n"
        "PROJECT_NUMBER   = ${PROJECT_VERSION}\n"
        "HAVE_DOT         = ${_zip_have_dot}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)

    # The root project owns the target name 'docs'.
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
# Inside the framework the root decides where the CMake package files go (it aggregates the
# modules); standalone, use a directory name find_package(TK4CPP_Zip) can discover.
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

    # Flavour that TK4CPP::Zip refers to in the installed package (see the config template).
    set(TK4CPP_ZIP_DEFAULT_FLAVOUR "${_zip_default_flavour}")
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

message(STATUS "TK4CPP::Zip ${PROJECT_VERSION}: flavours=${_zip_flavours} default=${_zip_default_flavour}"
               " tests=${TK4CPP_ZIP_BUILD_TESTS} install=${TK4CPP_ZIP_INSTALL}")
