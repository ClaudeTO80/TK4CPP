set(TK4CPP_THREADPOOL_EXAMPLES
    001_BasicUsage
    002_Observers
    003_Cancellation
    004_LongTimeExecution
    005_InputOutputFiles
)

foreach(example IN LISTS TK4CPP_THREADPOOL_EXAMPLES)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE TK4CPP::ThreadPool)
    set_target_properties(${example} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        FOLDER "TK4CPP/ThreadPool/Examples")
endforeach()
