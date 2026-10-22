set(_sources
    src/FileHandler.cpp
    src/FolderHandler.cpp
    src/System.cpp
    src/UUIDHandler.cpp
    src/Value.cpp
    src/Version.cpp
)

set(_module_deps)
set(_exclude_headers)

#find_package(Thread REQUIRED)

tk4cpp_add_module(
    NAME            Utility
    DESCRIPTION     "General purpose utilities: values, properties, files, versions, UUIDs, system info"
    SOURCES         ${_sources}
    MODULE_DEPENDS  Thread
    #PRIVATE_LINK    Thread
    #PRIVATE_PACKAGES Thread
    EXCLUDE_HEADERS ${_exclude_headers}
)
