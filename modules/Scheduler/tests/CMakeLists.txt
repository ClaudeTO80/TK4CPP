# One GoogleTest executable for the whole module (core + ThreadPool adapter).
tk4cpp_add_tests(
    MODULE  Scheduler
    SOURCES test_cron.cpp
            test_store.cpp
            test_scheduler.cpp
            test_persistence.cpp
            test_threadpool_adapter.cpp
    LINK    Threads::Threads
)

# Helper process the persistence tests start and SIGKILL (crash recovery).
add_executable(TK4CPP_Scheduler_crash_child crash_child.cpp)
target_link_libraries(TK4CPP_Scheduler_crash_child PRIVATE TK4CPP::Scheduler Threads::Threads)
tk4cpp_target_defaults(TK4CPP_Scheduler_crash_child)
set_target_properties(TK4CPP_Scheduler_crash_child PROPERTIES FOLDER "TK4CPP/Scheduler")
target_compile_definitions(TK4CPP_Scheduler_tests PRIVATE
    TK4CPP_SCHEDULER_CRASH_CHILD="$<TARGET_FILE:TK4CPP_Scheduler_crash_child>")
add_dependencies(TK4CPP_Scheduler_tests TK4CPP_Scheduler_crash_child)

# Dispatch-latency benchmark (not a test; run by hand).
add_executable(TK4CPP_Scheduler_bench_latency bench_latency.cpp)
target_link_libraries(TK4CPP_Scheduler_bench_latency PRIVATE TK4CPP::Scheduler)
tk4cpp_target_defaults(TK4CPP_Scheduler_bench_latency)
set_target_properties(TK4CPP_Scheduler_bench_latency PROPERTIES FOLDER "TK4CPP/Scheduler")
