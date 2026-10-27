# Doxygen documentation: cmake -DTK4CPP_BUILD_DOCS=ON ... && cmake --build . --target docs
# The result is written to <build>/docs/html (what the GitHub Pages workflow publishes).
find_package(Doxygen REQUIRED OPTIONAL_COMPONENTS dot)

# Every module's public headers; the include path they are documented with is stripped
# so classes show up as "TK4CPP/Utility/Version.h" rather than a build-machine path.
set(TK4CPP_DOXYGEN_INPUT "${PROJECT_SOURCE_DIR}/docs/mainpage.md")
set(TK4CPP_DOXYGEN_STRIP_PATHS "")
foreach(_m IN LISTS TK4CPP_KNOWN_MODULES)
    if(IS_DIRECTORY "${PROJECT_SOURCE_DIR}/modules/${_m}/include")
        string(APPEND TK4CPP_DOXYGEN_INPUT " \\\n    \"${PROJECT_SOURCE_DIR}/modules/${_m}/include\"")
        string(APPEND TK4CPP_DOXYGEN_STRIP_PATHS " \\\n    \"${PROJECT_SOURCE_DIR}/modules/${_m}/include\"")
    endif()
endforeach()

if(DOXYGEN_DOT_FOUND)
    set(TK4CPP_DOXYGEN_HAVE_DOT YES)
else()
    set(TK4CPP_DOXYGEN_HAVE_DOT NO)
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)

add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/html"
    COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating the TK4CPP API documentation with Doxygen"
    VERBATIM)
