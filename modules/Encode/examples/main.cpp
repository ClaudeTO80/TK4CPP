/**
 * @file main.cpp
 * @brief Minimal usage example for TK4CPP::Encode.
 */
#include <TK4CPP/Encode/Base64.h>
#include <TK4CPP/Encode/UrlEncoding.h>

#include <iostream>

int main() {
    using namespace TK4CPP::Encode;

    // Base64, default options.
    const std::string encoded = Base64::encodeDefault("Hello, TK4CPP!");
    std::cout << "base64   : " << encoded << '\n';

    if (auto decoded = Base64{}.decodeToString(encoded)) {
        std::cout << "decoded  : " << *decoded << '\n';
    }

    // Base64 URL-safe without padding (JWT style).
    const Base64 jwt{Base64::urlSafe()};
    std::cout << "urlsafe  : " << jwt.encode("\xfb\xff") << '\n';

    // Error handling.
    if (auto bad = Base64{}.decode("Zm9v!")) {
        std::cout << "unexpected success\n";
    } else {
        std::cout << "error    : " << bad.error().message() << '\n';
    }

    // URL encoding.
    const std::string query = UrlEncoding::encode("name=Claudio & co/é", UrlEncoding::Mode::Component);
    std::cout << "url      : " << query << '\n';
    std::cout << "form     : " << UrlEncoding::encode("a b+c", UrlEncoding::Mode::Form) << '\n';
    if (auto back = UrlEncoding::decode(query)) {
        std::cout << "url back : " << *back << '\n';
    }
    return 0;
}
