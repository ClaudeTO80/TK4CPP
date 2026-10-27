cmake_minimum_required(VERSION 3.23)

project(TK4CPP_Process
    VERSION     0.1.0
    DESCRIPTION "Cross-platform process execution for TK4CPP"
    LANGUAGES   CXX
)

# Works both as a TK4CPP sub-module (add_subdirectory from the root CMakeLists.txt) and
# standalone. The root options TK4CPP_BUILD_SHARED/STATIC/TESTS/INSTALL, TK4CPP_CXX_STANDARD and
# TK4CPP_WARNINGS_AS_ERRORS are honoured when defined; otherwise the defaults below apply.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

# ── Framework-aware defaults ────────────────────────────────────────────────
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
    set(_process_tests_default ${TK4CPP_BUILD_TESTS})
else()
    set(_process_tests_default ${PROJECT_IS_TOP_LEVEL})
endif()
if(DEFINED TK4CPP_INSTALL)
    set(_process_install_default ${TK4CPP_INSTALL})
else()
    set(_process_install_default ${PROJECT_IS_TOP_LEVEL})
endif()

# ── Options ─────────────────────────────────────────────────────────────────
option(TK4CPP_PROCESS_BUILD_EXAMPLES "Build the Process example"                 ${PROJECT_IS_TOP_LEVEL})
option(TK4CPP_PROCESS_BUILD_TESTS    "Build the Process test suite"              ${_process_tests_default})
option(TK4CPP_PROCESS_INSTALL        "Generate install rules and the CMake package" ${_process_install_default})
set(TK4CPP_PROCESS_SANITIZE "" CACHE STRING
    "Sanitizers for the library and tests, e.g. \"address;undefined\" or \"thread\" (GCC/Clang only)")

if(NOT TK4CPP_BUILD_SHARED AND NOT TK4CPP_BUILD_STATIC)
    message(FATAL_ERROR "Process: enable at least one of TK4CPP_BUILD_SHARED / TK4CPP_BUILD_STATIC")
endif()

if(PROJECT_IS_TOP_LEVEL)
    # Executables and DLLs end up side by side so tests find the shared libraries on Windows.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# ── Sources and platform probes ─────────────────────────────────────────────
set(_process_sources src/Process.cpp src/ProcessImpl.hpp)
if(WIN32)
    list(APPEND _process_sources src/ProcessWin32.cpp)
else()
    list(APPEND _process_sources src/ProcessPosix.cpp)

    # Probe optional POSIX extensions once; the sources fall back to a portable path when missing.
    include(CheckCXXSymbolExists)
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_cxx_symbol_exists(posix_spawn_file_actions_addchdir_np     spawn.h TK4CPP_PROCESS_HAVE_ADDCHDIR_SYM)
    check_cxx_symbol_exists(posix_spawn_file_actions_addclosefrom_np spawn.h TK4CPP_PROCESS_HAVE_ADDCLOSEFROM_SYM)
    unset(CMAKE_REQUIRED_DEFINITIONS)
endif()

if(TK4CPP_PROCESS_SANITIZE AND NOT MSVC)
    string(REPLACE ";" "," _process_san "${TK4CPP_PROCESS_SANITIZE}")
endif()

# ── Library targets ─────────────────────────────────────────────────────────
# Applies everything that is common to the real libraries and the fork-path test variant.
function(_tk4cpp_process_configure tgt is_shared)
    target_compile_features(${tgt} PUBLIC cxx_std_${TK4CPP_CXX_STANDARD})
    target_include_directories(${tgt}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${tgt} PUBLIC Threads::Threads)
    set_target_properties(${tgt} PROPERTIES
        CXX_EXTENSIONS            OFF
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET     hidden
        VISIBILITY_INLINES_HIDDEN ON
        FOLDER                    "TK4CPP/Process")
    if(is_shared)
        target_compile_definitions(${tgt} PUBLIC TK4CPP_PROCESS_SHARED)
    endif()
    target_compile_definitions(${tgt} PRIVATE TK4CPP_PROCESS_BUILDING)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /permissive- /utf-8)
        if(TK4CPP_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
        if(TK4CPP_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
        if(_process_san)
            target_compile_options(${tgt} PUBLIC -fsanitize=${_process_san} -fno-omit-frame-pointer)
            target_link_options(${tgt} PUBLIC -fsanitize=${_process_san})
        endif()
    endif()
    if(WIN32)
        target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
    else()
        foreach(feat ADDCHDIR ADDCLOSEFROM)
            if(TK4CPP_PROCESS_HAVE_${feat}_SYM)
                target_compile_definitions(${tgt} PRIVATE TK4CPP_PROCESS_HAVE_${feat}=1)
            else()
                target_compile_definitions(${tgt} PRIVATE TK4CPP_PROCESS_HAVE_${feat}=0)
            endif()
        endforeach()
    endif()
endfunction()

set(_process_main "")
set(_process_targets "")
if(TK4CPP_BUILD_SHARED)
    add_library(TK4CPP_Process SHARED ${_process_sources})
    _tk4cpp_process_configure(TK4CPP_Process ON)
    set_target_properties(TK4CPP_Process PROPERTIES
        OUTPUT_NAME TK4CPP_Process
        EXPORT_NAME Process
        VERSION     ${PROJECT_VERSION}
        SOVERSION   ${PROJECT_VERSION_MAJOR}
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(_process_main TK4CPP_Process)
    list(APPEND _process_targets TK4CPP_Process)
endif()
if(TK4CPP_BUILD_STATIC)
    if(_process_main STREQUAL "")
        set(_static_name   TK4CPP_Process)
        set(_static_export Process)
        set(_process_main  TK4CPP_Process)
    else()
        set(_static_name   TK4CPP_Process_static)
        set(_static_export Process_static)
    endif()
    add_library(${_static_name} STATIC ${_process_sources})
    _tk4cpp_process_configure(${_static_name} OFF)
    set_target_properties(${_static_name} PROPERTIES
        OUTPUT_NAME TK4CPP_Process
        EXPORT_NAME ${_static_export})
    if(WIN32)
        set_target_properties(${_static_name} PROPERTIES OUTPUT_NAME TK4CPP_Process_static)
    endif()
    list(APPEND _process_targets ${_static_name})
endif()
add_library(TK4CPP::Process ALIAS ${_process_main})

get_property(_built GLOBAL PROPERTY TK4CPP_BUILT_MODULES)
if(NOT "Process" IN_LIST _built)
    set_property(GLOBAL APPEND PROPERTY TK4CPP_BUILT_MODULES Process)
endif()

# ── Example and tests ───────────────────────────────────────────────────────
if(TK4CPP_PROCESS_BUILD_EXAMPLES)
    add_executable(process_example example/main.cpp)
    target_link_libraries(process_example PRIVATE TK4CPP::Process)
    set_target_properties(process_example PROPERTIES FOLDER "TK4CPP/Process")
endif()

if(TK4CPP_PROCESS_BUILD_TESTS)
    enable_testing()
    add_executable(process_test_helper tests/test_helper.cpp)
    set_target_properties(process_test_helper PROPERTIES CXX_EXTENSIONS OFF FOLDER "TK4CPP/Process")
    target_compile_features(process_test_helper PRIVATE cxx_std_${TK4CPP_CXX_STANDARD})
    if(MINGW)
        # The tests start the helper with a stripped environment (PATH unset, empty, ...): a MinGW
        # helper linked dynamically could not find libstdc++-6.dll / libwinpthread-1.dll any more.
        target_link_options(process_test_helper PRIVATE -static)
    endif()
    if(WIN32)
        target_link_libraries(process_test_helper PRIVATE shell32)
        target_compile_definitions(process_test_helper PRIVATE UNICODE _UNICODE)
    endif()
    if(_process_san)
        target_compile_options(process_test_helper PRIVATE -fsanitize=${_process_san})
        target_link_options(process_test_helper PRIVATE -fsanitize=${_process_san})
    endif()

    add_executable(process_tests tests/process_tests.cpp)
    target_link_libraries(process_tests PRIVATE TK4CPP::Process)
    set_target_properties(process_tests PROPERTIES FOLDER "TK4CPP/Process")
    add_test(NAME process_tests COMMAND process_tests $<TARGET_FILE:process_test_helper>)
    set_tests_properties(process_tests PROPERTIES TIMEOUT 300)

    if(NOT WIN32)
        # Same suite against the fork/exec spawn path (used on platforms lacking posix_spawn chdir).
        add_library(TK4CPP_Process_Forkpath STATIC ${_process_sources})
        _tk4cpp_process_configure(TK4CPP_Process_Forkpath OFF)
        target_compile_definitions(TK4CPP_Process_Forkpath PRIVATE TK4CPP_PROCESS_FORCE_FORK)
        add_executable(process_tests_forkpath tests/process_tests.cpp)
        target_link_libraries(process_tests_forkpath PRIVATE TK4CPP_Process_Forkpath)
        set_target_properties(process_tests_forkpath PROPERTIES FOLDER "TK4CPP/Process")
        add_test(NAME process_tests_forkpath COMMAND process_tests_forkpath $<TARGET_FILE:process_test_helper>)
        set_tests_properties(process_tests_forkpath PROPERTIES TIMEOUT 300)
    endif()
endif()

# ── Install / find_package support ──────────────────────────────────────────
# Inside the framework the root decides where the CMake package files go (it aggregates the
# modules); standalone, use a directory name find_package(TK4CPP_Process) can discover.
if(DEFINED TK4CPP_INSTALL_CMAKEDIR)
    set(_process_cmakedir "${TK4CPP_INSTALL_CMAKEDIR}")
else()
    set(_process_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/TK4CPP_Process")
endif()

if(TK4CPP_PROCESS_INSTALL)
    install(TARGETS ${_process_targets}
        EXPORT   TK4CPP_ProcessTargets
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
                 COMPONENT   TK4CPP_Process_Runtime
                 NAMELINK_COMPONENT TK4CPP_Process_Development
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
                 COMPONENT   TK4CPP_Process_Development
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
                 COMPONENT   TK4CPP_Process_Runtime
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT   TK4CPP_Process_Development)

    install(EXPORT TK4CPP_ProcessTargets
        FILE        TK4CPP_ProcessTargets.cmake
        NAMESPACE   TK4CPP::
        DESTINATION ${_process_cmakedir}
        COMPONENT   TK4CPP_Process_Development)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TK4CPP_ProcessConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_ProcessConfig.cmake"
        INSTALL_DESTINATION ${_process_cmakedir})
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_ProcessConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_ProcessConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_ProcessConfigVersion.cmake"
        DESTINATION ${_process_cmakedir}
        COMPONENT   TK4CPP_Process_Development)
endif()

message(STATUS "TK4CPP::Process ${PROJECT_VERSION}: targets=${_process_targets}"
               " tests=${TK4CPP_PROCESS_BUILD_TESTS} install=${TK4CPP_PROCESS_INSTALL}")
