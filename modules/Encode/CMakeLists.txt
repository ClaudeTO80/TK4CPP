
set(_sources
    src/Base64.cpp
    src/UrlEncoding.cpp
    src/Io.cpp
)

set(_module_deps)
set(_exclude_headers)

tk4cpp_add_module(
    NAME            Encode
    DESCRIPTION     "TK4CPP Base64 and URL percent-encoding codecs with incremental, stream, file and folder support"
    SOURCES         ${_sources}
    MODULE_DEPENDS  ${_module_deps}
    EXCLUDE_HEADERS ${_exclude_headers}
)

# The public headers carry no export macros: the whole public API is meant to be exported.
# Windows gets this from WINDOWS_EXPORT_ALL_SYMBOLS; GCC/Clang need the default visibility,
# otherwise the framework's -fvisibility=hidden leaves the shared library without its symbols.
foreach(_t TK4CPP_Encode TK4CPP_Encode_static)
    if(TARGET ${_t})
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "SHARED_LIBRARY")
            set_target_properties(${_t} PROPERTIES
                WINDOWS_EXPORT_ALL_SYMBOLS ON
                CXX_VISIBILITY_PRESET      default
                VISIBILITY_INLINES_HIDDEN  OFF)
        endif()
    endif()
endforeach()
