add_executable(example_basic example_basic.cpp)
target_link_libraries(example_basic PRIVATE TK4CPP::Scheduler)

# The HTTP example is optional: it needs cpp-httplib.
#  - upstream CMake package (httplib::httplib), or
#  - the Debian/Ubuntu split package (header + libcpp-httplib.so), or
#  - the upstream single header: pass -DTK4_HTTPLIB_INCLUDE_DIR=<dir containing httplib.h>.
find_package(httplib CONFIG QUIET)
if(TARGET httplib::httplib)
    set(_tk4_httplib httplib::httplib)
else()
    find_path(TK4_HTTPLIB_INCLUDE_DIR httplib.h)
    if(TK4_HTTPLIB_INCLUDE_DIR)
        add_library(tk4_httplib INTERFACE)
        target_include_directories(tk4_httplib INTERFACE ${TK4_HTTPLIB_INCLUDE_DIR})
        # Split distributions (Debian) ship the implementation as a library.
        find_library(TK4_HTTPLIB_LIBRARY NAMES cpp-httplib)
        if(TK4_HTTPLIB_LIBRARY)
            target_link_libraries(tk4_httplib INTERFACE ${TK4_HTTPLIB_LIBRARY})
        endif()
        set(_tk4_httplib tk4_httplib)
    endif()
endif()
if(_tk4_httplib)
    add_executable(example_http_server example_http_server.cpp)
    target_link_libraries(example_http_server PRIVATE TK4CPP::Scheduler ${_tk4_httplib})
else()
    message(STATUS "cpp-httplib not found: example_http_server is not built")
endif()
