/**
 * @file Base64.cpp
 * @brief Implementation of TK4CPP::Encode::Base64.
 */
#include <TK4CPP/Encode/Base64.h>

#include <array>
#include <cstdint>

namespace TK4CPP::Encode {

namespace {

constexpr std::string_view kStandardAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view kUrlSafeAlphabet  = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/** @brief Returns the 64 symbols of the requested alphabet. */
constexpr std::string_view symbols(Base64::Alphabet alphabet) noexcept {
    return alphabet == Base64::Alphabet::Standard ? kStandardAlphabet : kUrlSafeAlphabet;
}

/** @brief Builds a 256-entry reverse lookup table; invalid characters map to -1. */
constexpr std::array<std::int8_t, 256> makeDecodeTable(std::string_view alphabet) noexcept {
    std::array<std::int8_t, 256> table{};
    for (auto& entry : table) {
        entry = -1;
    }
    for (std::size_t i = 0; i < alphabet.size(); ++i) {
        table[static_cast<unsigned char>(alphabet[i])] = static_cast<std::int8_t>(i);
    }
    return table;
}

constexpr auto kStandardTable = makeDecodeTable(kStandardAlphabet);
constexpr auto kUrlSafeTable  = makeDecodeTable(kUrlSafeAlphabet);

constexpr const std::int8_t* decodeTable(Base64::Alphabet alphabet) noexcept {
    return alphabet == Base64::Alphabet::Standard ? kStandardTable.data() : kUrlSafeTable.data();
}

constexpr bool isWhitespace(unsigned char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::vector<std::byte> toBytes(const std::string& raw) {
    const auto view = std::as_bytes(std::span{raw.data(), raw.size()});
    return {view.begin(), view.end()};
}

} // namespace

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

Base64::Encoder::Encoder(Options options) noexcept
    : m_options(options), m_alphabet(symbols(options.alphabet)) {}

void Base64::Encoder::put(char c, std::string& out) {
    if (m_options.lineLength != 0 && m_column == m_options.lineLength) {
        out += "\r\n";
        m_column = 0;
    }
    out += c;
    ++m_column;
}

void Base64::Encoder::emitGroup(std::byte b0, std::byte b1, std::byte b2, std::string& out) {
    const std::uint32_t v = (std::to_integer<std::uint32_t>(b0) << 16) |
                            (std::to_integer<std::uint32_t>(b1) << 8) |
                             std::to_integer<std::uint32_t>(b2);
    put(m_alphabet[(v >> 18) & 0x3F], out);
    put(m_alphabet[(v >> 12) & 0x3F], out);
    put(m_alphabet[(v >> 6) & 0x3F], out);
    put(m_alphabet[v & 0x3F], out);
}

void Base64::Encoder::update(std::span<const std::byte> data, std::string& out) {
    std::size_t i = 0;

    if (m_pendingSize != 0) { // complete the group left over by the previous chunk
        while (m_pendingSize < 3 && i < data.size()) {
            m_pending[m_pendingSize++] = data[i++];
        }
        if (m_pendingSize < 3) {
            return;
        }
        emitGroup(m_pending[0], m_pending[1], m_pending[2], out);
        m_pendingSize = 0;
    }

    for (; i + 3 <= data.size(); i += 3) {
        emitGroup(data[i], data[i + 1], data[i + 2], out);
    }
    while (i < data.size()) {
        m_pending[m_pendingSize++] = data[i++];
    }
}

void Base64::Encoder::finish(std::string& out) {
    const bool pad = m_options.padding != Padding::None;

    if (m_pendingSize == 1) {
        const std::uint32_t v = std::to_integer<std::uint32_t>(m_pending[0]) << 16;
        put(m_alphabet[(v >> 18) & 0x3F], out);
        put(m_alphabet[(v >> 12) & 0x3F], out);
        if (pad) {
            put('=', out);
            put('=', out);
        }
    } else if (m_pendingSize == 2) {
        const std::uint32_t v = (std::to_integer<std::uint32_t>(m_pending[0]) << 16) |
                                (std::to_integer<std::uint32_t>(m_pending[1]) << 8);
        put(m_alphabet[(v >> 18) & 0x3F], out);
        put(m_alphabet[(v >> 12) & 0x3F], out);
        put(m_alphabet[(v >> 6) & 0x3F], out);
        if (pad) {
            put('=', out);
        }
    }
    m_pendingSize = 0;
    m_column = 0;
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

Base64::Decoder::Decoder(Options options) noexcept
    : m_options(options), m_table(decodeTable(options.alphabet)) {}

std::unexpected<Error> Base64::Decoder::fail(Error error) {
    m_failed = error;
    return std::unexpected(std::move(error));
}

void Base64::Decoder::reset() noexcept {
    m_accumulator = 0;
    m_bits = 0;
    m_symbolCount = 0;
    m_padCount = 0;
    m_firstPad = 0;
    m_lastSymbol = 0;
    m_offset = 0;
    m_failed.reset();
}

std::expected<void, Error> Base64::Decoder::update(std::string_view text, std::string& out) {
    if (m_failed) {
        return std::unexpected(*m_failed);
    }

    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t position = m_offset + i;

        if (m_options.ignoreWhitespace && isWhitespace(c)) {
            continue;
        }
        if (c == '=') {
            if (m_padCount == 0) {
                m_firstPad = position;
            }
            ++m_padCount;
            continue;
        }
        if (m_padCount != 0) { // data after padding
            return fail(Error{ErrorCode::InvalidPadding, position, {}});
        }
        const int value = m_table[c];
        if (value < 0) {
            return fail(Error{ErrorCode::InvalidCharacter, position, {}});
        }

        ++m_symbolCount;
        m_lastSymbol = position;
        m_accumulator = (m_accumulator << 6) | static_cast<std::uint32_t>(value);
        m_bits += 6;
        if (m_bits >= 8) {
            m_bits -= 8;
            out += static_cast<char>((m_accumulator >> m_bits) & 0xFF);
            m_accumulator &= (1U << m_bits) - 1U;
        }
    }
    m_offset += text.size();
    return {};
}

std::expected<void, Error> Base64::Decoder::checkEnd() const {
    if (m_failed) {
        return std::unexpected(*m_failed);
    }

    const std::size_t remainder = m_symbolCount % 4;
    if (remainder == 1) { // 6 bits cannot make a byte
        return std::unexpected(Error{ErrorCode::InvalidLength, m_offset, {}});
    }

    const std::size_t neededPadding = (4 - remainder) % 4;
    if (m_padCount != 0) {
        if (m_options.padding == Padding::None || m_padCount != neededPadding) {
            return std::unexpected(Error{ErrorCode::InvalidPadding, m_firstPad, {}});
        }
    } else if (neededPadding != 0 && m_options.padding == Padding::Required) {
        return std::unexpected(Error{ErrorCode::InvalidPadding, m_offset, {}});
    }

    if (m_options.strictTrailingBits && m_bits != 0 && m_accumulator != 0) {
        return std::unexpected(Error{ErrorCode::NonZeroTrailingBits, m_lastSymbol, {}});
    }
    return {};
}

std::expected<void, Error> Base64::Decoder::finish(std::string& /*out*/) {
    auto result = checkEnd();
    reset();
    return result;
}

// ---------------------------------------------------------------------------
// Codec (one-shot API)
// ---------------------------------------------------------------------------

std::size_t Base64::encodedSize(std::size_t inputSize) const noexcept {
    const std::size_t chars = (m_options.padding != Padding::None)
                                  ? ((inputSize + 2) / 3) * 4
                                  : (inputSize * 4 + 2) / 3;
    if (m_options.lineLength == 0 || chars == 0) {
        return chars;
    }
    return chars + ((chars - 1) / m_options.lineLength) * 2; // one CRLF between full lines
}

std::string Base64::encode(std::span<const std::byte> data) const {
    std::string out;
    out.reserve(encodedSize(data.size()));
    Encoder encoder{m_options};
    encoder.update(data, out);
    encoder.finish(out);
    return out;
}

std::string Base64::encode(std::string_view text) const {
    return encode(std::as_bytes(std::span{text.data(), text.size()}));
}

std::expected<std::string, Error> Base64::decodeToString(std::string_view text) const {
    std::string out;
    out.reserve(maxDecodedSize(text.size()));
    Decoder decoder{m_options};
    if (auto r = decoder.update(text, out); !r) {
        return std::unexpected(std::move(r.error()));
    }
    if (auto r = decoder.finish(out); !r) {
        return std::unexpected(std::move(r.error()));
    }
    return out;
}

std::expected<std::vector<std::byte>, Error> Base64::decode(std::string_view text) const {
    auto raw = decodeToString(text);
    if (!raw) {
        return std::unexpected(std::move(raw.error()));
    }
    return toBytes(*raw);
}

std::string Base64::encodeDefault(std::span<const std::byte> data) {
    return Base64{}.encode(data);
}

std::string Base64::encodeDefault(std::string_view text) {
    return Base64{}.encode(text);
}

std::expected<std::vector<std::byte>, Error> Base64::decodeDefault(std::string_view text) {
    return Base64{}.decode(text);
}

} // namespace TK4CPP::Encode
