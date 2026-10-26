
set(_sources
    src/Signal.cpp
    src/Task.cpp
    src/TaskQueue.cpp
    src/WorkerThread.cpp
    src/ThreadPool.cpp
    src/PoolLogger.cpp
    src/Observers.cpp
)

set(_module_deps)
set(_exclude_headers)

find_package(Threads REQUIRED)

tk4cpp_add_module(
    NAME            Thread
    DESCRIPTION     "TK4CPP prioritised, dynamically scaling thread pool with typed event notifications"
    SOURCES         ${_sources}
    MODULE_DEPENDS  ${_module_deps}
    PRIVATE_LINK    Threads::Threads
    PRIVATE_PACKAGES Threads
    EXCLUDE_HEADERS ${_exclude_headers}
)

# Export.h selects TK4CPP_TP_API from TK4CPP_TP_SHARED / TK4CPP_TP_BUILDING, which the
# framework helper does not define: without them a shared build hides every symbol.
foreach(_t TK4CPP_Thread TK4CPP_Thread_static)
    if(TARGET ${_t})
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "SHARED_LIBRARY")
            target_compile_definitions(${_t} PUBLIC TK4CPP_TP_SHARED PRIVATE TK4CPP_TP_BUILDING)
            # Ensure the Windows DLL exports every public and out-of-line symbol.
            set_property(TARGET ${_t} PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
        endif()
    endif()
endforeach()
