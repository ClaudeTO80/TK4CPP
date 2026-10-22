# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(tk4cpp_tinyxml2)

# Aggiungi tinyxml2 come shared library
add_library(tk4cpp_tinyxml2 SHARED "src/tinyxml2.cpp")
add_library(tk4cpp_tinyxml2_impl SHARED "src/XmlTinyXml2.cpp")
target_link_libraries(tk4cpp_tinyxml2_impl PRIVATE tk4cpp_tinyxml2)
target_link_libraries(tk4cpp_tinyxml2_impl PRIVATE Utility)
include_directories("include")

foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    set_target_properties(tk4cpp_tinyxml2
		PROPERTIES
		VERSION 11.0.0
	    SOVERSION 11
	    OUTPUT_NAME "tinyxml2"
		FOLDER "TK4CPP/XML/Tinyxml2"
        RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}/${CMAKE_VS_PLATFORM_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG}/${CMAKE_VS_PLATFORM_NAME}"
        ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG}/${CMAKE_VS_PLATFORM_NAME}"
	)

#set_target_properties(tk4cpp_tinyxml2_impl
#		PROPERTIES
#		FOLDER "TK4CPP/XML/Tinyxml2"
#		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
#		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
#		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
#	)

    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set_target_properties(tk4cpp_tinyxml2_impl 
        PROPERTIES
		OUTPUT_NAME TK4CPP_XML_Tinyxml2 
        RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}/${CMAKE_VS_PLATFORM_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG}/${CMAKE_VS_PLATFORM_NAME}"
        ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG}/${CMAKE_VS_PLATFORM_NAME}"
		FOLDER "TK4CPP/XML/Tinyxml2"
    )
endforeach()

	if(WIN32)
    # Export all symbols automatically on Windows
    set_target_properties(tk4cpp_tinyxml2 PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set_target_properties(tk4cpp_tinyxml2_impl PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
