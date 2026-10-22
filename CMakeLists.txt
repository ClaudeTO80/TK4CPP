cmake_minimum_required(VERSION 3.23)

project(TK4CPP
    VERSION      0.1.0
    DESCRIPTION  "TK4CPP - Toolkit 4 C++: a cross-compiler C++ framework with high-level APIs and optimised low-level implementations"
    HOMEPAGE_URL "https://github.com/OWNER/TK4CPP"
    LANGUAGES    CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(PROJECT_IS_TOP_LEVEL)
    message(" _________  ___  ____  _    _      ______  _______  _______   ")
    message("|  _   _  ||_  ||_  _|| |  | |   .' ___  ||_   __ \\|_   __ \\  ")
    message("|_/ | | \\_|  | |_/ /  | |__| |_ / .'   \\_|  | |__) | | |__) | ")
    message("    | |      |  __'.  |____   _|| |         |  ___/  |  ___/  ")
    message("   _| |_    _| |  \\ \\_    _| |_ \\ `.___.'\\ _| |_    _| |_     ")
    message("  |_____|  |____||____|  |_____| `.____ .'|_____|  |_____|    ")
endif()

# ─────────────────────────────────────────────
#  Options
# ─────────────────────────────────────────────
# BUILD_SHARED_LIBS (the standard CMake switch, set by vcpkg/Conan/...) decides the default
# variant; with no preference both variants are built.
if(DEFINED BUILD_SHARED_LIBS)
    set(_tk4cpp_default_shared ${BUILD_SHARED_LIBS})
    if(BUILD_SHARED_LIBS)
        set(_tk4cpp_default_static OFF)
    else()
        set(_tk4cpp_default_static ON)
    endif()
else()
    set(_tk4cpp_default_shared ON)
    set(_tk4cpp_default_static ON)
endif()

if(PROJECT_IS_TOP_LEVEL)
    set(_tk4cpp_default_top ON)
else()
    set(_tk4cpp_default_top OFF)
endif()

option(TK4CPP_BUILD_SHARED        "Build the modules as shared libraries" ${_tk4cpp_default_shared})
option(TK4CPP_BUILD_STATIC        "Build the modules as static libraries" ${_tk4cpp_default_static})
option(TK4CPP_BUILD_TESTS         "Build the unit tests"                  ${_tk4cpp_default_top})
option(TK4CPP_BUILD_DOCS          "Build the Doxygen documentation (target 'docs')" OFF)
option(TK4CPP_INSTALL             "Generate install targets"              ${_tk4cpp_default_top})
option(TK4CPP_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors"     OFF)
set(TK4CPP_CXX_STANDARD "23" CACHE STRING "C++ standard required by TK4CPP (20 or 23)")
set_property(CACHE TK4CPP_CXX_STANDARD PROPERTY STRINGS 20 23)
set(TK4CPP_THIRD_PARTY_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty" CACHE PATH
    "Folder holding the bundled third-party libraries (spdlog, libxml2, jwt-cpp, ...)")

# ─────────────────────────────────────────────
#  Modules
# ─────────────────────────────────────────────
# Build order = dependency order. A module is only configured if its directory exists under
# modules/, so planned modules can stay listed here until they are added.
# Add a new module: create modules/<Name>/ (see modules/Utility) and append its name here.
set(TK4CPP_KNOWN_MODULES
    Logging
    Utility
    JWT
    XML
    DataLayer
    Encode
    Chart
    ArgsParse
    Lua
    Process
    Thread
    Zip
    Crypto
)
set(TK4CPP_MODULES_DEFAULT_OFF Lua)

# Hard dependencies between modules: TK4CPP_MODULE_DEPENDS_<Name> lists the modules <Name> needs.
# They must be enabled too (and come earlier in TK4CPP_KNOWN_MODULES).
set(TK4CPP_MODULE_DEPENDS_XML Utility)

foreach(_m IN LISTS TK4CPP_KNOWN_MODULES)
    string(TOUPPER "${_m}" _M)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/modules/${_m}")
        if(_m IN_LIST TK4CPP_MODULES_DEFAULT_OFF)
            set(_default OFF)
        else()
            set(_default ON)
        endif()
        option(TK4CPP_BUILD_${_M} "Build the ${_m} module" ${_default})
    endif()
endforeach()

# ─────────────────────────────────────────────
#  Global settings
# ─────────────────────────────────────────────
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(TK4CPPModule)

set(TK4CPP_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/TK4CPP"
    CACHE PATH "Where the TK4CPP CMake package files are installed")

if(PROJECT_IS_TOP_LEVEL)
    # Executables and DLLs end up side by side so tests find the shared libraries on Windows.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

# ─────────────────────────────────────────────
#  Tests (GoogleTest: system package first, downloaded otherwise)
# ─────────────────────────────────────────────
if(TK4CPP_BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(googletest)
    endif()
    include(GoogleTest)
endif()

# ─────────────────────────────────────────────
#  Add the modules
# ─────────────────────────────────────────────
foreach(_m IN LISTS TK4CPP_KNOWN_MODULES)
    string(TOUPPER "${_m}" _M)
    if(TK4CPP_BUILD_${_M})
        foreach(_dep IN LISTS TK4CPP_MODULE_DEPENDS_${_M})
            string(TOUPPER "${_dep}" _DEP)
            if(NOT TK4CPP_BUILD_${_DEP})
                message(FATAL_ERROR
                    "TK4CPP: module ${_m} requires module ${_dep}: set TK4CPP_BUILD_${_DEP}=ON "
                    "or disable TK4CPP_BUILD_${_M}")
            endif()
        endforeach()
        message(STATUS "TK4CPP: adding module ${_m}")
        add_subdirectory(modules/${_m})
    endif()
endforeach()

# ─────────────────────────────────────────────
#  Documentation
# ─────────────────────────────────────────────
if(TK4CPP_BUILD_DOCS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/CMakeLists.txt")
        add_subdirectory(docs)
    else()
        message(WARNING "TK4CPP: TK4CPP_BUILD_DOCS is ON but docs/CMakeLists.txt does not exist")
    endif()
endif()

# ─────────────────────────────────────────────
#  Install: TK4CPPConfig.cmake, TK4CPPConfigVersion.cmake, LICENSE
# ─────────────────────────────────────────────
if(TK4CPP_INSTALL)
    get_property(_built GLOBAL PROPERTY TK4CPP_BUILT_MODULES)
    string(REPLACE ";" " " TK4CPP_BUILT_MODULES_STR "${_built}")

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPPConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TK4CPPConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/TK4CPPConfig.cmake"
        INSTALL_DESTINATION "${TK4CPP_INSTALL_CMAKEDIR}")

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/TK4CPPConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/TK4CPPConfigVersion.cmake"
        DESTINATION "${TK4CPP_INSTALL_CMAKEDIR}")

    foreach(_doc LICENSE THIRD_PARTY_NOTICES.md)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_doc}")
            install(FILES "${_doc}" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/TK4CPP")
        endif()
    endforeach()
endif()

# ─────────────────────────────────────────────
#  Summary
# ─────────────────────────────────────────────
get_property(_summary GLOBAL PROPERTY TK4CPP_BUILT_MODULES)
message(STATUS "")
message(STATUS "TK4CPP ${PROJECT_VERSION}")
message(STATUS "  Modules      : ${_summary}")
message(STATUS "  Shared / static: ${TK4CPP_BUILD_SHARED} / ${TK4CPP_BUILD_STATIC}")
message(STATUS "  C++ standard : ${TK4CPP_CXX_STANDARD}")
message(STATUS "  Tests / docs : ${TK4CPP_BUILD_TESTS} / ${TK4CPP_BUILD_DOCS}")
message(STATUS "  Install      : ${TK4CPP_INSTALL} (prefix ${CMAKE_INSTALL_PREFIX})")
message(STATUS "")
