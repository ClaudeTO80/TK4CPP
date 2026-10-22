# jwt-cpp (header-only, MIT) is vendored in ${TK4CPP_THIRD_PARTY_FOLDER}/jwt-cpp and only used by the implementation,
# so it never shows up in the public interface of this module. It needs OpenSSL.
set(_jwtcpp_include "${TK4CPP_THIRD_PARTY_FOLDER}/jwt-cpp/include")
if(NOT EXISTS "${_jwtcpp_include}/jwt-cpp/jwt.h")
    message(FATAL_ERROR "TK4CPP JWT: vendored jwt-cpp not found in ${_jwtcpp_include}")
endif()

find_package(OpenSSL REQUIRED)

tk4cpp_add_module(
    NAME            JWT
    DESCRIPTION     "JSON Web Token creation and validation"
    SOURCES         src/JWTHandler.cpp
    PRIVATE_LINK    OpenSSL::Crypto
    PRIVATE_PACKAGES OpenSSL
    PRIVATE_SYSTEM_INCLUDES "${_jwtcpp_include}"
)
