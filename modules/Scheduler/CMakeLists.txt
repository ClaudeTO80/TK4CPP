# TK4CPP::Scheduler - Quartz-style job scheduler (UTC, SQLite persistence).
# Needs the Thread module (ThreadPoolAdapter) and SQLite3.

option(TK4CPP_SCHEDULER_BUILD_EXAMPLES "Build the Scheduler examples" OFF)

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

tk4cpp_add_module(
    NAME             Scheduler
    DESCRIPTION      "TK4CPP Quartz-style job scheduler: cron/interval/one-shot triggers, UTC, SQLite persistence"
    SOURCES          src/CronExpression.cpp
                     src/Trigger.cpp
                     src/Executor.cpp
                     src/RamJobStore.cpp
                     src/SqliteJobStore.cpp
                     src/JobScheduler.cpp
                     src/ThreadPoolAdapter.cpp
    MODULE_DEPENDS   Thread
    PRIVATE_LINK     Threads::Threads SQLite::SQLite3
    PRIVATE_PACKAGES Threads SQLite3
)

if(TK4CPP_SCHEDULER_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()
