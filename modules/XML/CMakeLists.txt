# XML module: abstract interfaces (IXmlDocument, IXmlElement, ...) and their libxml2 implementation.
#
# libxml2 is a private implementation detail (no libxml2 header is part of the public API). It is taken from:
#   TK4CPP_LIBXML2_SOURCE=AUTO      the system package if found, otherwise the copy in 3rdParty/libxml2 (default)
#   TK4CPP_LIBXML2_SOURCE=SYSTEM    the system package only
#   TK4CPP_LIBXML2_SOURCE=VENDORED  the copy in 3rdParty/libxml2 only
set(TK4CPP_LIBXML2_SOURCE "AUTO" CACHE STRING "Where libxml2 comes from: AUTO, SYSTEM or VENDORED")
set_property(CACHE TK4CPP_LIBXML2_SOURCE PROPERTY STRINGS AUTO SYSTEM VENDORED)

# Pointing the build at a libxml2 that was installed earlier (for example with its own CMake build and
# `cmake --install --prefix <dir>`). Any of the following implies TK4CPP_LIBXML2_SOURCE=SYSTEM semantics,
# i.e. the library must be found there and the vendored copy is never used:
#   TK4CPP_LIBXML2_ROOT         installation prefix (contains include/libxml2 and lib/)
#   TK4CPP_LIBXML2_DIR          directory holding LibXml2Config.cmake (like -DLibXml2_DIR=...)
#   TK4CPP_LIBXML2_INCLUDE_DIR  directory holding libxml/parser.h (e.g. <prefix>/include/libxml2)
#   TK4CPP_LIBXML2_LIBRARY      full path of the library file (.so, .dylib, .a, .lib, import library)
# INCLUDE_DIR and LIBRARY go together and bypass CMake package files completely.
set(TK4CPP_LIBXML2_ROOT        "" CACHE PATH     "libxml2 installation prefix")
set(TK4CPP_LIBXML2_DIR         "" CACHE PATH     "Directory containing LibXml2Config.cmake")
set(TK4CPP_LIBXML2_INCLUDE_DIR "" CACHE PATH     "libxml2 include directory (the one containing libxml/)")
set(TK4CPP_LIBXML2_LIBRARY     "" CACHE FILEPATH "libxml2 library file")
set(TK4CPP_LIBXML2_EXTRA_LIBS  "" CACHE STRING   "Extra libraries needed by TK4CPP_LIBXML2_LIBRARY when it is static (e.g. z;iconv;lzma)")
option(TK4CPP_LIBXML2_STATIC_LIB "TK4CPP_LIBXML2_LIBRARY is a static library (defines LIBXML_STATIC)" OFF)

set(_custom_libxml2 FALSE)
if(TK4CPP_LIBXML2_ROOT OR TK4CPP_LIBXML2_DIR OR TK4CPP_LIBXML2_INCLUDE_DIR OR TK4CPP_LIBXML2_LIBRARY)
    set(_custom_libxml2 TRUE)
    if(TK4CPP_LIBXML2_SOURCE STREQUAL "VENDORED")
        message(FATAL_ERROR "TK4CPP XML: TK4CPP_LIBXML2_SOURCE=VENDORED conflicts with TK4CPP_LIBXML2_ROOT/DIR/INCLUDE_DIR/LIBRARY")
    endif()
    if(TK4CPP_LIBXML2_INCLUDE_DIR AND NOT TK4CPP_LIBXML2_LIBRARY OR TK4CPP_LIBXML2_LIBRARY AND NOT TK4CPP_LIBXML2_INCLUDE_DIR)
        message(FATAL_ERROR "TK4CPP XML: TK4CPP_LIBXML2_INCLUDE_DIR and TK4CPP_LIBXML2_LIBRARY must be given together")
    endif()
endif()

set(_use_vendored FALSE)
set(_manual_libxml2 FALSE)
set(LibXml2_FOUND FALSE)

if(_custom_libxml2 AND NOT TK4CPP_LIBXML2_INCLUDE_DIR AND NOT TK4CPP_LIBXML2_LIBRARY)
    # Package-file based: the CMake config installed by libxml2's own build.
    if(TK4CPP_LIBXML2_DIR)
        set(LibXml2_DIR "${TK4CPP_LIBXML2_DIR}" CACHE PATH "" FORCE)
        find_package(LibXml2 2.9 CONFIG NO_DEFAULT_PATH)
    else()
        find_package(LibXml2 2.9 CONFIG QUIET NO_DEFAULT_PATH PATHS "${TK4CPP_LIBXML2_ROOT}")
    endif()
    # An installation prefix without package files (autotools build, distro-style layout): look for the
    # files directly, only below that prefix.
    if(NOT LibXml2_FOUND AND TK4CPP_LIBXML2_ROOT AND NOT TK4CPP_LIBXML2_DIR)
        find_path(_tk4cpp_lx_inc libxml/xmlversion.h NO_DEFAULT_PATH
            PATHS "${TK4CPP_LIBXML2_ROOT}" PATH_SUFFIXES include/libxml2 include)
        find_library(_tk4cpp_lx_lib NAMES xml2 libxml2 NO_DEFAULT_PATH
            PATHS "${TK4CPP_LIBXML2_ROOT}" PATH_SUFFIXES lib lib64)
        if(_tk4cpp_lx_inc AND _tk4cpp_lx_lib)
            set(TK4CPP_LIBXML2_INCLUDE_DIR "${_tk4cpp_lx_inc}")
            set(TK4CPP_LIBXML2_LIBRARY     "${_tk4cpp_lx_lib}")
            if(_tk4cpp_lx_lib MATCHES "\\.a$")
                set(TK4CPP_LIBXML2_STATIC_LIB ON)
            endif()
        endif()
    endif()
    if(NOT LibXml2_FOUND AND NOT (TK4CPP_LIBXML2_INCLUDE_DIR AND TK4CPP_LIBXML2_LIBRARY))
        message(FATAL_ERROR "TK4CPP XML: no libxml2 >= 2.9 found with TK4CPP_LIBXML2_ROOT='${TK4CPP_LIBXML2_ROOT}' TK4CPP_LIBXML2_DIR='${TK4CPP_LIBXML2_DIR}'")
    endif()
endif()

if(TK4CPP_LIBXML2_INCLUDE_DIR AND TK4CPP_LIBXML2_LIBRARY)
    # Fully manual: build the imported target ourselves.
    if(NOT EXISTS "${TK4CPP_LIBXML2_INCLUDE_DIR}/libxml/xmlversion.h")
        message(FATAL_ERROR "TK4CPP XML: ${TK4CPP_LIBXML2_INCLUDE_DIR}/libxml/xmlversion.h not found")
    endif()
    if(NOT EXISTS "${TK4CPP_LIBXML2_LIBRARY}")
        message(FATAL_ERROR "TK4CPP XML: TK4CPP_LIBXML2_LIBRARY does not exist: ${TK4CPP_LIBXML2_LIBRARY}")
    endif()
    file(STRINGS "${TK4CPP_LIBXML2_INCLUDE_DIR}/libxml/xmlversion.h" _xv REGEX "#define +LIBXML_DOTTED_VERSION")
    string(REGEX REPLACE ".*\"([0-9.]+)\".*" "\\1" LibXml2_VERSION "${_xv}")
    if(LibXml2_VERSION VERSION_LESS 2.9)
        message(FATAL_ERROR "TK4CPP XML: libxml2 ${LibXml2_VERSION} is too old (need >= 2.9)")
    endif()
    add_library(LibXml2::LibXml2 UNKNOWN IMPORTED)
    set_target_properties(LibXml2::LibXml2 PROPERTIES
        IMPORTED_LOCATION             "${TK4CPP_LIBXML2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TK4CPP_LIBXML2_INCLUDE_DIR}")
    if(TK4CPP_LIBXML2_STATIC_LIB)
        set_property(TARGET LibXml2::LibXml2 APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS LIBXML_STATIC)
        if(UNIX)
            set_property(TARGET LibXml2::LibXml2 APPEND PROPERTY INTERFACE_LINK_LIBRARIES m)
        endif()
    endif()
    if(TK4CPP_LIBXML2_EXTRA_LIBS)
        set_property(TARGET LibXml2::LibXml2 APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${TK4CPP_LIBXML2_EXTRA_LIBS})
    endif()
    set(LibXml2_FOUND TRUE)
    set(_manual_libxml2 TRUE)
    message(STATUS "TK4CPP XML: using libxml2 ${LibXml2_VERSION}: ${TK4CPP_LIBXML2_LIBRARY}")
elseif(_custom_libxml2)
    message(STATUS "TK4CPP XML: using the libxml2 ${LibXml2_VERSION} selected by TK4CPP_LIBXML2_ROOT/DIR")
elseif(NOT TK4CPP_LIBXML2_SOURCE STREQUAL "VENDORED")
    find_package(LibXml2 2.9)
    if(NOT LibXml2_FOUND)
        if(TK4CPP_LIBXML2_SOURCE STREQUAL "SYSTEM")
            message(FATAL_ERROR "TK4CPP XML: TK4CPP_LIBXML2_SOURCE=SYSTEM but no system libxml2 was found")
        endif()
        set(_use_vendored TRUE)
    endif()
else()
    set(_use_vendored TRUE)
endif()

if(_use_vendored)
    set(_libxml2_dir "${TK4CPP_THIRD_PARTY_FOLDER}/libxml2")
    if(NOT EXISTS "${_libxml2_dir}/CMakeLists.txt")
        message(FATAL_ERROR "TK4CPP XML: vendored libxml2 not found in ${_libxml2_dir}")
    endif()
    # Scoped in a function so that the settings below do not leak into the rest of the project.
    function(_tk4cpp_add_vendored_libxml2)
        set(BUILD_SHARED_LIBS OFF)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        set(LIBXML2_WITH_PROGRAMS OFF)
        set(LIBXML2_WITH_TESTS    OFF)
        set(LIBXML2_WITH_PYTHON   OFF)
        set(LIBXML2_WITH_DOCS     OFF)
        set(LIBXML2_WITH_ICONV    OFF)
        set(LIBXML2_WITH_ICU      OFF)
        set(LIBXML2_WITH_ZLIB     OFF)
        set(LIBXML2_WITH_MODULES  OFF)
        set(LIBXML2_WITH_HTTP     OFF)
        add_subdirectory("${_libxml2_dir}" "${CMAKE_CURRENT_BINARY_DIR}/libxml2" EXCLUDE_FROM_ALL)
    endfunction()
    _tk4cpp_add_vendored_libxml2()
    message(STATUS "TK4CPP XML: using the vendored libxml2")
elseif(NOT _custom_libxml2)
    message(STATUS "TK4CPP XML: using the system libxml2 ${LibXml2_VERSION}")
endif()

set(_xml_extra_targets)
set(_xml_private_packages)
if(_use_vendored)
    set(_xml_extra_targets LibXml2)
    set(_xml_private_packages Threads)   # the static libxml2 links Threads::Threads
else()
    # Consumers of the static library must be able to find the same libxml2 again (CMAKE_PREFIX_PATH,
    # LibXml2_DIR, ...); a hand-made TK4CPP_LIBXML2_LIBRARY target has no package file of its own.
    set(_xml_private_packages "LibXml2 2.9")
endif()

tk4cpp_add_module(
    NAME            XML
    DESCRIPTION     "XML documents: abstract interfaces and a libxml2 implementation"
    SOURCES         src/XmlLibxml2.cpp
    MODULE_DEPENDS  Utility
    PRIVATE_LINK    LibXml2::LibXml2
    PRIVATE_PACKAGES ${_xml_private_packages}
    EXCLUDE_HEADERS doc.h
    STATIC_ONLY_INSTALL_TARGETS ${_xml_extra_targets}
)

# The vendored libxml2 is linked statically into the shared library: keep its symbols private so they can
# never clash with another libxml2 loaded into the same process.
if(_use_vendored AND TARGET TK4CPP_XML AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    get_target_property(_xml_type TK4CPP_XML TYPE)
    if(_xml_type STREQUAL "SHARED_LIBRARY")
        target_link_options(TK4CPP_XML PRIVATE "LINKER:--exclude-libs,ALL")
    endif()
endif()
