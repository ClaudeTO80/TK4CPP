# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Claudio

# The code shown in the manual: every snippet is a real program that is built
# and run by the test suite, so the documentation cannot show code that does
# not compile or does not work.

set(TK4CPP_ZIP_SNIPPETS
    core_basic core_stream core_modify core_protect core_errors
    hl_archive hl_read_extract hl_protect hl_patterns)

foreach(snippet IN LISTS TK4CPP_ZIP_SNIPPETS)
    add_executable(snippet_${snippet} ${snippet}.cpp)
    target_link_libraries(snippet_${snippet} PRIVATE TK4CPP::Zip)
    if(MSVC)
        target_compile_options(snippet_${snippet} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(snippet_${snippet} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME snippet_${snippet} COMMAND snippet_${snippet})
endforeach()
