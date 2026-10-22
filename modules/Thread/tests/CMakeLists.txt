add_executable(test_threadpool test_threadpool.cpp)
target_link_libraries(test_threadpool PRIVATE TK4CPP::ThreadPool)
set_target_properties(test_threadpool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "TK4CPP/ThreadPool/Tests")
add_test(NAME ThreadPool.Tests COMMAND test_threadpool)
set_tests_properties(ThreadPool.Tests PROPERTIES TIMEOUT 180)
