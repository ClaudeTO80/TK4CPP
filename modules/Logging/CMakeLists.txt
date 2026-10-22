cmake_minimum_required(VERSION 3.23)

project(TK4CPP_Logging
    VERSION     0.1.0
    DESCRIPTION "TK4CPP Logging library (spdlog based)"
    LANGUAGES   CXX
)

# Works both as a TK4CPP sub-module (add_subdirectory from the root CMakeLists.txt) and
# standalone. The root options TK4CPP_BUILD_SHARED/STATIC/TESTS/INSTALL, TK4CPP_CXX_STANDARD and
# TK4CPP_WARNINGS_AS_ERRORS are honoured when defined; otherwise the defaults below apply.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

# ── Framework-aware defaults ──────────────────────────────────────────────────
if(NOT DEFINED TK4CPP_BUILD_SHARED)
    if(DEFINED BUILD_SHARED_LIBS)
        set(TK4CPP_BUILD_SHARED ${BUILD_SHARED_LIBS})
    else()
        set(TK4CPP_BUILD_SHARED ON)
    endif()
endif()
if(NOT DEFINED TK4CPP_BUILD_STATIC)
    if(TK4CPP_BUILD_SHARED)
        set(TK4CPP_BUILD_STATIC OFF)
    else()
        set(TK4CPP_BUILD_STATIC ON)
    endif()
endif()
if(NOT DEFINED TK4CPP_CXX_STANDARD)
    set(TK4CPP_CXX_STANDARD 23)
endif()
if(NOT DEFINED TK4CPP_WARNINGS_AS_ERRORS)
    set(TK4CPP_WARNINGS_AS_ERRORS OFF)
endif()
if(DEFINED TK4CPP_BUILD_TESTS)
    set(_logging_tests_default ${TK4CPP_BUILD_TESTS})
else()
    set(_logging_tests_default ${PROJECT_IS_TOP_LEVEL})
endif()
if(DEFINED TK4CPP_INSTALL)
    set(_logging_install_default ${TK4CPP_INSTALL})
else()
    set(_logging_install_default ${PROJECT_IS_TOP_LEVEL})
endif()

# ── Options ───────────────────────────────────────────────────────────────────
option(TK4CPP_LOGGING_BUILD_TESTS    "Build the Logging unit tests"                 ${_logging_tests_default})
option(TK4CPP_LOGGING_BUILD_EXAMPLES "Build the Logging examples"                   OFF)
option(TK4CPP_LOGGING_INSTALL        "Generate install rules and the CMake package" ${_logging_install_default})
option(TK4CPP_LOGGING_USE_SYSTEM_SPDLOG
       "Use an installed spdlog (find_package) instead of the copy in 3rdParty/spdlog" OFF)

if(PROJECT_IS_TOP_LEVEL)
    # Executables and DLLs end up side by side so tests find the shared libraries on Windows.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

if(NOT TK4CPP_BUILD_SHARED AND NOT TK4CPP_BUILD_STATIC)
    message(FATAL_ERROR "Logging: enable at least one of TK4CPP_BUILD_SHARED / TK4CPP_BUILD_STATIC")
endif()

# ── spdlog ────────────────────────────────────────────────────────────────────
if(TK4CPP_LOGGING_USE_SYSTEM_SPDLOG)
    find_package(spdlog 1.9 CONFIG REQUIRED)
else()
    if(DEFINED TK4CPP_THIRD_PARTY_FOLDER)
        set(_logging_spdlog_include "${TK4CPP_THIRD_PARTY_FOLDER}/spdlog/include")
    else()
        get_filename_component(_logging_spdlog_include
            "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdParty/spdlog/include" ABSOLUTE)
    endif()
    if(NOT EXISTS "${_logging_spdlog_include}/spdlog/spdlog.h")
        message(FATAL_ERROR
            "Logging: bundled spdlog not found in '${_logging_spdlog_include}'. "
            "Set TK4CPP_THIRD_PARTY_FOLDER or TK4CPP_LOGGING_USE_SYSTEM_SPDLOG=ON.")
    endif()
endif()

# ── Library targets ───────────────────────────────────────────────────────────
file(GLOB _logging_sources CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB _logging_headers CONFIGURE_DEPENDS "include/TK4CPP/Logging/*.h")

function(_logging_configure tgt)
    target_include_directories(${tgt}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(TK4CPP_LOGGING_USE_SYSTEM_SPDLOG)
        target_link_libraries(${tgt} PUBLIC spdlog::spdlog)
    else()
        target_include_directories(${tgt} SYSTEM PUBLIC
            $<BUILD_INTERFACE:${_logging_spdlog_include}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/tk4cpp/3rdParty>)
    endif()
    target_link_libraries(${tgt} PUBLIC Threads::Threads)
    target_compile_features(${tgt} PUBLIC cxx_std_${TK4CPP_CXX_STANDARD})
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /utf-8 /W4)
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(TK4CPP_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${tgt} PRIVATE /WX)
        else()
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
    endif()
    set_target_properties(${tgt} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET default
        VERSION   ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        FOLDER    "TK4CPP/Logging")
endfunction()

set(_logging_main "")
if(TK4CPP_BUILD_SHARED)
    add_library(Logging SHARED ${_logging_sources} ${_logging_headers})
    _logging_configure(Logging)
    set_target_properties(Logging PROPERTIES
        OUTPUT_NAME TK4CPP_Logging
        EXPORT_NAME Logging
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(_logging_main Logging)
    set(_logging_targets Logging)
endif()
if(TK4CPP_BUILD_STATIC)
    if(_logging_main STREQUAL "")
        set(_static_name Logging)
        set(_static_export Logging)
        set(_logging_main Logging)
    else()
        set(_static_name Logging_static)
        set(_static_export Logging_static)
    endif()
    add_library(${_static_name} STATIC ${_logging_sources} ${_logging_headers})
    _logging_configure(${_static_name})
    set_target_properties(${_static_name} PROPERTIES
        OUTPUT_NAME TK4CPP_Logging
        EXPORT_NAME ${_static_export}
        VERSION "" SOVERSION "")
    if(WIN32)
        set_target_properties(${_static_name} PROPERTIES OUTPUT_NAME TK4CPP_Logging_static)
    endif()
    list(APPEND _logging_targets ${_static_name})
endif()
add_library(TK4CPP::Logging ALIAS ${_logging_main})

get_property(_built GLOBAL PROPERTY TK4CPP_BUILT_MODULES)
if(NOT "Logging" IN_LIST _built)
    set_property(GLOBAL APPEND PROPERTY TK4CPP_BUILT_MODULES Logging)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
# Inside the framework the root decides where the CMake package files go (it aggregates the
# modules); standalone, use a directory name find_package(TK4CPP_Logging) can discover.
if(DEFINED TK4CPP_INSTALL_CMAKEDIR)
    set(_logging_cmakedir "${TK4CPP_INSTALL_CMAKEDIR}")
else()
    set(_logging_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/TK4CPP_Logging")
endif()

if(TK4CPP_LOGGING_INSTALL)
    install(TARGETS ${_logging_targets}
        EXPORT   TK4CPP_LoggingTargets
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
                 COMPONENT   TK4CPP_Logging_Runtime
                 NAMELINK_COMPONENT TK4CPP_Logging_Development
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
                 COMPONENT   TK4CPP_Logging_Development
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
                 COMPONENT   TK4CPP_Logging_Runtime)

    install(DIRECTORY include/TK4CPP
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT   TK4CPP_Logging_Development
        FILES_MATCHING PATTERN "*.h")

    # The bundled spdlog is part of the public headers: install it in a private
    # prefix so it never collides with a system-wide spdlog.
    if(NOT TK4CPP_LOGGING_USE_SYSTEM_SPDLOG)
        install(DIRECTORY ${_logging_spdlog_include}/spdlog
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tk4cpp/3rdParty
            COMPONENT   TK4CPP_Logging_Development)
        set(TK4CPP_Logging_PC_REQUIRES "")
        set(TK4CPP_Logging_PC_CFLAGS "-I\${includedir}/tk4cpp/3rdParty")
    else()
        set(TK4CPP_Logging_PC_REQUIRES "spdlog")
        set(TK4CPP_Logging_PC_CFLAGS "")
    endif()

    install(EXPORT TK4CPP_LoggingTargets
        FILE        TK4CPP_LoggingTargets.cmake
        NAMESPACE   TK4CPP::
        DESTINATION ${_logging_cmakedir}
        COMPONENT   TK4CPP_Logging_Development)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TK4CPP_LoggingConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_LoggingConfig.cmake"
        INSTALL_DESTINATION ${_logging_cmakedir})
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_LoggingConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_LoggingConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_LoggingConfigVersion.cmake"
        DESTINATION ${_logging_cmakedir}
        COMPONENT   TK4CPP_Logging_Development)

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tk4cpp_Logging.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/tk4cpp_Logging.pc" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tk4cpp_Logging.pc"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        COMPONENT   TK4CPP_Logging_Development)
endif()

# ── Optional sub-projects ─────────────────────────────────────────────────────
if(TK4CPP_LOGGING_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

if(TK4CPP_LOGGING_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(TK4CPP_LOGGING_USE_SYSTEM_SPDLOG)
    set(_logging_spdlog_desc "system")
else()
    set(_logging_spdlog_desc "bundled")
endif()
message(STATUS "TK4CPP::Logging ${PROJECT_VERSION}: spdlog=${_logging_spdlog_desc}"
               " targets=${_logging_targets} tests=${TK4CPP_LOGGING_BUILD_TESTS} install=${TK4CPP_LOGGING_INSTALL}")
