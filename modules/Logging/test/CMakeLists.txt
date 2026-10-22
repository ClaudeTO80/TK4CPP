# GoogleTest: already provided by the TK4CPP root, otherwise system package, otherwise downloaded.
if(NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()
include(GoogleTest)

add_executable(Logging_tests LoggingTests.cpp)
target_link_libraries(Logging_tests PRIVATE TK4CPP::Logging GTest::gtest_main)
set_target_properties(Logging_tests PROPERTIES FOLDER "TK4CPP/Logging")
if(NOT MSVC)
    target_compile_options(Logging_tests PRIVATE -Wall -Wextra)
endif()

gtest_discover_tests(Logging_tests DISCOVERY_TIMEOUT 60)
