/**
 * @file Error.h
 * @brief Error type shared by all TK4CPP::Encode codecs.
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace TK4CPP::Encode {

/**
 * @brief Reason why an encode/decode operation failed.
 */
enum class ErrorCode {
    InvalidCharacter,       ///< A character outside the expected alphabet was found.
    InvalidLength,          ///< The number of significant symbols is impossible (e.g. Base64 length % 4 == 1).
    InvalidPadding,         ///< Padding is missing, superfluous, misplaced or not allowed by the policy.
    NonZeroTrailingBits,    ///< Unused trailing bits are not zero (only reported in strict mode).
    InvalidPercentEncoding, ///< A '%' is not followed by two hexadecimal digits.
    IoError                 ///< A stream, file or folder operation failed (see Error::detail).
};

/**
 * @brief Returns a short, stable, human readable name for an error code.
 * @param code The error code.
 * @return A string view with static storage duration.
 */
[[nodiscard]] constexpr std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidCharacter:       return "invalid character";
        case ErrorCode::InvalidLength:          return "invalid length";
        case ErrorCode::InvalidPadding:         return "invalid padding";
        case ErrorCode::NonZeroTrailingBits:    return "non-zero trailing bits";
        case ErrorCode::InvalidPercentEncoding: return "invalid percent-encoding";
        case ErrorCode::IoError:                return "I/O error";
    }
    return "unknown error";
}

/**
 * @brief Describes a failure.
 *
 * Returned as the error alternative of `std::expected` by every fallible
 * function of the module.
 */
struct Error {
    ErrorCode   code{ErrorCode::InvalidCharacter}; ///< What went wrong.
    std::size_t position{0};                       ///< Zero-based offset in the input where the problem was detected
                                                   ///< (for ErrorCode::IoError: number of input bytes consumed so far).
    std::string detail{};                          ///< Optional free text, e.g. the offending path for I/O errors.

    /**
     * @brief Builds a printable description such as `"invalid character at offset 7"`.
     *
     * If detail is not empty it is appended as `": <detail>"`.
     */
    [[nodiscard]] std::string message() const {
        std::string out{toString(code)};
        out += " at offset ";
        out += std::to_string(position);
        if (!detail.empty()) {
            out += ": ";
            out += detail;
        }
        return out;
    }

    [[nodiscard]] friend bool operator==(const Error&, const Error&) = default;
};

} // namespace TK4CPP::Encode
