@PACKAGE_INIT@

# TK4CPP_ThreadConfig.cmake
# Usage: find_package(TK4CPP_Thread REQUIRED)
#        target_link_libraries(app PRIVATE TK4CPP::Thread)   # or TK4CPP::Thread_static

include(CMakeFindDependencyMacro)

find_dependency(Threads)

include("${CMAKE_CURRENT_LIST_DIR}/TK4CPP_ThreadTargets.cmake")

check_required_components(TK4CPP_Thread)
