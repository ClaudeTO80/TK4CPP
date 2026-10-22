
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

# Ensure the Windows DLL exports every public and out-of-line symbol.
if(TARGET Encode)
    set_property(TARGET Encode PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
if(TARGET TK4CPP_Encode)
    set_property(TARGET TK4CPP_Encode PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
