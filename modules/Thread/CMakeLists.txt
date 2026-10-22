
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

# Ensure the Windows DLL exports every public and out-of-line symbol. Explicit
# TK4CPP_TP_API annotations remain useful for non-MSVC visibility control.
if(TARGET Thread)
    set_property(TARGET Thread PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
if(TARGET TK4CPP_Thread)
    set_property(TARGET TK4CPP_Thread PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
