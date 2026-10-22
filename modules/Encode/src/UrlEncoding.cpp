/**
 * @file UrlEncoding.cpp
 * @brief Implementation of TK4CPP::Encode::UrlEncoding.
 */
#include <TK4CPP/Encode/UrlEncoding.h>

namespace TK4CPP::Encode {

namespace {

constexpr char kHexDigits[] = "0123456789ABCDEF";

/** @brief Converts a hexadecimal digit (either case) to its value, or -1 if it is not one. */
constexpr int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------
// Encoder / Decoder
// ---------------------------------------------------------------------------

void UrlEncoding::Encoder::update(std::span<const std::byte> data, std::string& out) const {
    for (const std::byte b : data) {
        const auto byte = std::to_integer<unsigned char>(b);
        const char c = static_cast<char>(byte);
        if (isUnreserved(c, m_mode)) {
            out += c;
        } else if (m_mode == Mode::Form && c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += kHexDigits[byte >> 4];
            out += kHexDigits[byte & 0x0F];
        }
    }
}

std::expected<void, Error> UrlEncoding::Decoder::update(std::string_view text, std::string& out) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const std::size_t position = m_offset + i;

        switch (m_state) {
            case 0:
                if (c == '%') {
                    m_state = 1;
                    m_percentPos = position;
                } else if (m_mode == Mode::Form && c == '+') {
                    out += ' ';
                } else {
                    out += c;
                }
                break;
            case 1: {
                const int high = hexValue(c);
                if (high < 0) {
                    const Error error{ErrorCode::InvalidPercentEncoding, m_percentPos, {}};
                    m_state = 0;
                    return std::unexpected(error);
                }
                m_high = high;
                m_state = 2;
                break;
            }
            default: {
                const int low = hexValue(c);
                if (low < 0) {
                    const Error error{ErrorCode::InvalidPercentEncoding, m_percentPos, {}};
                    m_state = 0;
                    return std::unexpected(error);
                }
                out += static_cast<char>((m_high << 4) | low);
                m_state = 0;
                break;
            }
        }
    }
    m_offset += text.size();
    return {};
}

std::expected<void, Error> UrlEncoding::Decoder::finish(std::string& /*out*/) {
    std::expected<void, Error> result;
    if (m_state != 0) {
        result = std::unexpected(Error{ErrorCode::InvalidPercentEncoding, m_percentPos, {}});
    }
    m_state = 0;
    m_offset = 0;
    return result;
}

// ---------------------------------------------------------------------------
// One-shot API
// ---------------------------------------------------------------------------

std::string UrlEncoding::encode(std::span<const std::byte> input, Mode mode) {
    std::string out;
    out.reserve(input.size()); // lower bound; escapes will grow it
    Encoder{mode}.update(input, out);
    return out;
}

std::string UrlEncoding::encode(std::string_view input, Mode mode) {
    return encode(std::as_bytes(std::span{input.data(), input.size()}), mode);
}

std::expected<std::string, Error> UrlEncoding::decode(std::string_view input, Mode mode) {
    std::string out;
    out.reserve(input.size());
    Decoder decoder{mode};
    if (auto r = decoder.update(input, out); !r) {
        return std::unexpected(std::move(r.error()));
    }
    if (auto r = decoder.finish(out); !r) {
        return std::unexpected(std::move(r.error()));
    }
    return out;
}

} // namespace TK4CPP::Encode
