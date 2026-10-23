# TK4CPPModule.cmake
#
# Helpers shared by every TK4CPP module. A module's CMakeLists.txt only has to describe
# what is specific to it (sources, dependencies); building static/shared variants, export
# headers, install rules and the find_package() plumbing are handled here.

include_guard(GLOBAL)

include(GenerateExportHeader)
include(GNUInstallDirs)

set_property(GLOBAL PROPERTY TK4CPP_BUILT_MODULES "")

# ---------------------------------------------------------------------------------------
# tk4cpp_target_defaults(<target>)
#   Warning flags and (optionally) -Werror, applied to the library and test targets.
# ---------------------------------------------------------------------------------------
function(tk4cpp_target_defaults target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
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

# ---------------------------------------------------------------------------------------
# tk4cpp_add_module(
#     NAME             <Name>                 # e.g. Utility -> targets TK4CPP_Utility, alias TK4CPP::Utility
#     DESCRIPTION      <text>
#     SOURCES          <files...>             # relative to the module directory
#     MODULE_DEPENDS   <modules...>           # other TK4CPP modules (public dependencies)
#     PUBLIC_LINK      <libs...>              # linked PUBLIC  (non-TK4CPP libraries/targets)
#     PRIVATE_LINK     <libs...>              # linked PRIVATE
#     PUBLIC_PACKAGES  <"Pkg [args]"...>      # find_dependency() lines needed by consumers
#     PRIVATE_PACKAGES <"Pkg [args]"...>      # same, but only needed by consumers of the static library
#     PRIVATE_SYSTEM_INCLUDES <dirs...>       # header-only third parties (vendored), warnings silenced
#     EXCLUDE_HEADERS  <file names...>        # headers that must not be installed
#     STATIC_ONLY_INSTALL_TARGETS <targets...> # privately linked static third-party targets that must be
#                                              # exported with the static library (e.g. a vendored libxml2)
# )
#
# Build variants are controlled by TK4CPP_BUILD_SHARED / TK4CPP_BUILD_STATIC. When both are
# enabled the shared library is TK4CPP::<Name> and the static one TK4CPP::<Name>_static.
# ---------------------------------------------------------------------------------------
function(tk4cpp_add_module)
    cmake_parse_arguments(ARG ""
        "NAME;DESCRIPTION"
        "SOURCES;MODULE_DEPENDS;PUBLIC_LINK;PRIVATE_LINK;PUBLIC_PACKAGES;PRIVATE_PACKAGES;PRIVATE_SYSTEM_INCLUDES;EXCLUDE_HEADERS;STATIC_ONLY_INSTALL_TARGETS"
        ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "tk4cpp_add_module: NAME is required")
    endif()
    if(NOT TK4CPP_BUILD_SHARED AND NOT TK4CPP_BUILD_STATIC)
        message(FATAL_ERROR "At least one of TK4CPP_BUILD_SHARED / TK4CPP_BUILD_STATIC must be ON")
    endif()

    set(_name   "${ARG_NAME}")
    string(TOUPPER "${_name}" _upper)
    set(_base   "TK4CPP_${_upper}")
    set(_target "TK4CPP_${_name}")
    set(_incdir "${CMAKE_CURRENT_BINARY_DIR}/include")
    set(_export_header "${_incdir}/TK4CPP/${_name}/Export.h")

    set(_both FALSE)
    if(TK4CPP_BUILD_SHARED AND TK4CPP_BUILD_STATIC)
        set(_both TRUE)
    endif()

    set(_variants)
    if(TK4CPP_BUILD_SHARED)
        list(APPEND _variants SHARED)
    endif()
    if(TK4CPP_BUILD_STATIC)
        list(APPEND _variants STATIC)
    endif()

    set(_installed_targets)
    set(_export_header_done FALSE)

    foreach(_type IN LISTS _variants)
        # Naming: the shared library (or the only library) owns the plain name.
        if(_type STREQUAL "STATIC" AND _both)
            set(_suffix "_static")
        else()
            set(_suffix "")
        endif()
        set(_t     "${_target}${_suffix}")
        set(_alias "TK4CPP::${_name}${_suffix}")

        add_library(${_t} ${_type} ${ARG_SOURCES})
        add_library(${_alias} ALIAS ${_t})

        target_compile_features(${_t} PUBLIC cxx_std_${TK4CPP_CXX_STANDARD})
        target_include_directories(${_t} PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
            "$<BUILD_INTERFACE:${_incdir}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
        if(ARG_PRIVATE_SYSTEM_INCLUDES)
            target_include_directories(${_t} SYSTEM PRIVATE ${ARG_PRIVATE_SYSTEM_INCLUDES})
        endif()

        foreach(_dep IN LISTS ARG_MODULE_DEPENDS)
            target_link_libraries(${_t} PUBLIC "TK4CPP::${_dep}${_suffix}")
        endforeach()
        if(ARG_PUBLIC_LINK)
            target_link_libraries(${_t} PUBLIC ${ARG_PUBLIC_LINK})
        endif()
        if(ARG_PRIVATE_LINK)
            target_link_libraries(${_t} PRIVATE ${ARG_PRIVATE_LINK})
        endif()

        set_target_properties(${_t} PROPERTIES
            EXPORT_NAME                 "${_name}${_suffix}"
            DEFINE_SYMBOL               "${_base}_EXPORTS"
            CXX_VISIBILITY_PRESET       hidden
            VISIBILITY_INLINES_HIDDEN   ON
            POSITION_INDEPENDENT_CODE   ON
            FOLDER                      "TK4CPP/${_name}")
        if(_type STREQUAL "SHARED")
            set_target_properties(${_t} PROPERTIES
                VERSION   "${PROJECT_VERSION}"
                SOVERSION "${PROJECT_VERSION_MAJOR}")
        else()
            # Tells <Module>/Export.h that symbols need no import/export decoration.
            target_compile_definitions(${_t} PUBLIC ${_base}_STATIC_DEFINE)
        endif()

        tk4cpp_target_defaults(${_t})

        if(NOT _export_header_done)
            generate_export_header(${_t}
                BASE_NAME        ${_base}
                EXPORT_FILE_NAME "${_export_header}")
            set(_export_header_done TRUE)
        endif()

        list(APPEND _installed_targets ${_t})
    endforeach()

    # ------------------------------------------------------------------ install rules
    if(TK4CPP_INSTALL)
        set(_rt  "TK4CPP_${_name}_Runtime")
        set(_dev "TK4CPP_${_name}_Development")

        install(TARGETS ${_installed_targets}
            EXPORT   TK4CPP_${_name}Targets
            RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"     COMPONENT ${_rt}
            LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"     COMPONENT ${_rt}
                     NAMELINK_COMPONENT ${_dev}
            ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}"     COMPONENT ${_dev}
            INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

        # A static library re-exports the static libraries it links privately: they must be part of the
        # same export set, otherwise the generated targets file references targets nobody defines.
        if(TK4CPP_BUILD_STATIC AND ARG_STATIC_ONLY_INSTALL_TARGETS)
            install(TARGETS ${ARG_STATIC_ONLY_INSTALL_TARGETS}
                EXPORT  TK4CPP_${_name}Targets
                ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT ${_dev})
        endif()

        set(_exclude_args)
        foreach(_h IN LISTS ARG_EXCLUDE_HEADERS)
            list(APPEND _exclude_args PATTERN "${_h}" EXCLUDE)
        endforeach()
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/TK4CPP/"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/TK4CPP"
            COMPONENT   ${_dev}
            FILES_MATCHING ${_exclude_args} PATTERN "*.h" PATTERN "*.hpp")
        install(FILES "${_export_header}"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/TK4CPP/${_name}"
            COMPONENT   ${_dev})

        install(EXPORT TK4CPP_${_name}Targets
            FILE        TK4CPP_${_name}Targets.cmake
            NAMESPACE   TK4CPP::
            DESTINATION "${TK4CPP_INSTALL_CMAKEDIR}"
            COMPONENT   ${_dev})

        # Dependencies that a consumer must resolve before loading the targets file.
        set(_deps_file "${CMAKE_CURRENT_BINARY_DIR}/TK4CPP_${_name}Deps.cmake")
        set(_deps_content "# Generated by tk4cpp_add_module(${_name}). Do not edit.\ninclude(CMakeFindDependencyMacro)\n")
        foreach(_p IN LISTS ARG_PUBLIC_PACKAGES)
            string(APPEND _deps_content "find_dependency(${_p})\n")
        endforeach()
        if(TK4CPP_BUILD_STATIC)
            foreach(_p IN LISTS ARG_PRIVATE_PACKAGES)
                string(APPEND _deps_content "find_dependency(${_p})\n")
            endforeach()
        endif()
        foreach(_dep IN LISTS ARG_MODULE_DEPENDS)
            string(APPEND _deps_content "_tk4cpp_load_component(${_dep})\n")
        endforeach()
        file(GENERATE OUTPUT "${_deps_file}" CONTENT "${_deps_content}")
        install(FILES "${_deps_file}"
            DESTINATION "${TK4CPP_INSTALL_CMAKEDIR}"
            COMPONENT   ${_dev})
    endif()

    set_property(GLOBAL APPEND PROPERTY TK4CPP_BUILT_MODULES "${_name}")

    # ------------------------------------------------------------------------ tests
    if(TK4CPP_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()

    message(STATUS "TK4CPP module ${_name}: ${_variants}")
endfunction()

# ---------------------------------------------------------------------------------------
# tk4cpp_add_tests(
#     MODULE  <Name>
#     SOURCES <files...>
#     LINK    <extra libs...>
# )
#   Builds one GoogleTest executable per module and registers its cases with CTest.
#   Tests link against TK4CPP::<Name> (the shared library if both variants are built).
# ---------------------------------------------------------------------------------------
function(tk4cpp_add_tests)
    cmake_parse_arguments(ARG "" "MODULE" "SOURCES;LINK" ${ARGN})
    set(_t "TK4CPP_${ARG_MODULE}_tests")
    add_executable(${_t} ${ARG_SOURCES})
    target_link_libraries(${_t} PRIVATE TK4CPP::${ARG_MODULE} GTest::gtest_main ${ARG_LINK})
    tk4cpp_target_defaults(${_t})
    set_target_properties(${_t} PROPERTIES FOLDER "TK4CPP/${ARG_MODULE}")
    gtest_discover_tests(${_t} DISCOVERY_TIMEOUT 60)
endfunction()
