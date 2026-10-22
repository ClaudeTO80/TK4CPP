/**
 * @file UrlEncoding.h
 * @brief Percent-encoding (RFC 3986) and `application/x-www-form-urlencoded` encoding.
 */
#pragma once

#include <TK4CPP/Encode/Error.h>
#include <TK4CPP/Encode/Io.h>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace TK4CPP::Encode {

/**
 * @brief URL percent-encoding codec.
 *
 * Operates on raw bytes: input is never reinterpreted or converted, so UTF-8
 * text is encoded byte by byte (e.g. `é` becomes `%C3%A9`). Encoding always
 * emits upper-case hexadecimal digits; decoding accepts both cases.
 *
 * The static functions cover strings and byte arrays. An instance (which only
 * carries a Mode) adds incremental processing (Encoder / Decoder) and, through
 * StreamCodec, streams, files and folders.
 *
 * @code
 * using namespace TK4CPP::Encode;
 * UrlEncoding::encode("a b&c");                                  // "a%20b%26c"
 * UrlEncoding::encode("a b", UrlEncoding::Mode::Form);           // "a+b"
 * auto r = UrlEncoding::decode("a%20b");                         // "a b"
 * UrlEncoding{UrlEncoding::Mode::Form}.encodeFile("in.txt", "out.txt");
 * @endcode
 */
class UrlEncoding : public StreamCodec<UrlEncoding> {
public:
    /** @brief Selects which characters are left untouched and how space is handled. */
    enum class Mode {
        /**
         * Query/fragment component: only the RFC 3986 unreserved set
         * (`A-Z a-z 0-9 - . _ ~`) is kept. Space is `%20`. `+` is a literal plus when decoding.
         */
        Component,
        /**
         * Path: like Component, but `/` is also kept so that path separators survive.
         */
        Path,
        /**
         * HTML forms (`application/x-www-form-urlencoded`): like Component,
         * but space is written as `+` and `+` is decoded as space.
         */
        Form
    };

    /**
     * @brief Incremental encoder (stateless apart from the mode; chunks may be split anywhere).
     */
    class Encoder {
    public:
        /** @brief Creates an encoder for the given mode. */
        explicit Encoder(Mode mode) noexcept : m_mode(mode) {}

        /**
         * @brief Encodes a chunk.
         * @param data Bytes to encode.
         * @param out  Receives (appends) the encoded characters.
         */
        void update(std::span<const std::byte> data, std::string& out) const;

        /** @brief Does nothing (kept for the common codec interface). */
        void finish(std::string& out) const noexcept { (void)out; }

    private:
        Mode m_mode;
    };

    /**
     * @brief Incremental decoder.
     *
     * A `%XX` sequence may be split across chunks; the decoder buffers the
     * incomplete part. Error offsets are relative to the first character given
     * since construction or the last finish().
     */
    class Decoder {
    public:
        /** @brief Creates a decoder for the given mode. */
        explicit Decoder(Mode mode) noexcept : m_mode(mode) {}

        /**
         * @brief Decodes a chunk.
         * @param text Encoded characters.
         * @param out  Receives (appends) the decoded bytes.
         * @return Success, or InvalidPercentEncoding with the offset of the offending `%`.
         */
        [[nodiscard]] std::expected<void, Error> update(std::string_view text, std::string& out);

        /**
         * @brief Signals the end of the input; fails if a `%` sequence is incomplete.
         * @param out Unused (kept for the common codec interface).
         * @return Success or Error. The decoder is reset in both cases.
         */
        [[nodiscard]] std::expected<void, Error> finish(std::string& out);

    private:
        Mode        m_mode;
        int         m_state{0};       ///< 0 = plain, 1 = after '%', 2 = after '%' and the first hex digit.
        int         m_high{0};        ///< Value of the first hex digit when m_state == 2.
        std::size_t m_percentPos{0};  ///< Offset of the pending '%'.
        std::size_t m_offset{0};      ///< Characters consumed so far.
    };

    /** @brief Creates a codec using Mode::Component. */
    constexpr UrlEncoding() noexcept = default;

    /** @brief Creates a codec using the given mode for the incremental/stream/file API. */
    explicit constexpr UrlEncoding(Mode mode) noexcept : m_mode(mode) {}

    /** @brief Returns the mode of this codec. */
    [[nodiscard]] constexpr Mode mode() const noexcept { return m_mode; }

    /** @brief Creates an incremental encoder configured like this codec. */
    [[nodiscard]] Encoder makeEncoder() const noexcept { return Encoder{m_mode}; }

    /** @brief Creates an incremental decoder configured like this codec. */
    [[nodiscard]] Decoder makeDecoder() const noexcept { return Decoder{m_mode}; }

    /** @brief Suffix used by encodeFolder()/decodeFolder() when none is given: `.urlenc`. */
    [[nodiscard]] static constexpr std::string_view defaultFileSuffix() noexcept { return ".urlenc"; }

    /**
     * @brief Percent-encodes a string.
     * @param input Raw bytes to encode.
     * @param mode  Encoding flavour, Mode::Component by default.
     * @return The encoded text. Encoding cannot fail.
     */
    [[nodiscard]] static std::string encode(std::string_view input, Mode mode = Mode::Component);

    /**
     * @brief Percent-encodes a byte array.
     * @param input Raw bytes to encode.
     * @param mode  Encoding flavour, Mode::Component by default.
     * @return The encoded text. Encoding cannot fail.
     */
    [[nodiscard]] static std::string encode(std::span<const std::byte> input, Mode mode = Mode::Component);

    /**
     * @brief Percent-encodes any contiguous range of bytes.
     * @see ByteRange
     */
    template <ByteRange R>
    [[nodiscard]] static std::string encode(const R& range, Mode mode = Mode::Component) {
        return encode(asBytes(range), mode);
    }

    /**
     * @brief Decodes percent-encoded text.
     *
     * A `%` must be followed by exactly two hexadecimal digits. In Mode::Form
     * `+` is translated to a space. Every other byte is copied unchanged, so
     * the function does not validate that the result is well-formed UTF-8.
     *
     * @param input Encoded text.
     * @param mode  Decoding flavour, Mode::Component by default.
     * @return The decoded bytes, or Error with ErrorCode::InvalidPercentEncoding
     *         and the offset of the offending `%`.
     */
    [[nodiscard]] static std::expected<std::string, Error> decode(std::string_view input, Mode mode = Mode::Component);

    /**
     * @brief Tells whether a byte is copied verbatim by encode().
     * @param c    Byte to test.
     * @param mode Encoding flavour.
     * @return `true` if the byte is not escaped.
     */
    [[nodiscard]] static constexpr bool isUnreserved(char c, Mode mode = Mode::Component) noexcept {
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alnum || c == '-' || c == '.' || c == '_' || c == '~') {
            return true;
        }
        return mode == Mode::Path && c == '/';
    }

private:
    Mode m_mode{Mode::Component};
};

} // namespace TK4CPP::Encode
