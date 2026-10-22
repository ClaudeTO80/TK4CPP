# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Claudio

# One executable per example. Each links only against TK4CPP::Zip,
# exactly like an application using the installed package would.

set(TK4CPP_ZIP_EXAMPLES
    backup_directory:01_backup_directory.cpp
    list_archive:02_list_archive.cpp
    extract:03_extract.cpp
    plugin_package:04_plugin_package.cpp
    update_archive:05_update_archive.cpp
    encrypted_archive:06_encrypted_archive.cpp
    verify_and_tail:07_verify_and_tail.cpp
    secure_backup:08_secure_backup.cpp)

foreach(entry IN LISTS TK4CPP_ZIP_EXAMPLES)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 target)
    list(GET parts 1 source)

    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE TK4CPP::Zip)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
