# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(tk4cpp_xml_impl)
# Trova tutte le sottocartelle della cartella corrente
file(GLOB SUBFOLDERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_CURRENT_SOURCE_DIR}/*
)

# Filtra solo le directory
set(DIRS "")
foreach(item ${SUBFOLDERS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${item})
        list(APPEND DIRS ${item})
    endif()
endforeach()

add_subdirectory("tinyxml2")

