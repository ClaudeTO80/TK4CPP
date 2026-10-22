/**
 * @file Base64.h
 * @brief Base64 encoding and decoding (RFC 4648 and MIME line wrapping).
 */
#pragma once

#include <TK4CPP/Encode/Error.h>
#include <TK4CPP/Encode/Io.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TK4CPP::Encode {

/**
 * @brief Base64 codec.
 *
 * The behaviour is controlled by an immutable Options value captured at
 * construction time, so an instance is cheap, thread-safe and reusable.
 *
 * Supported sources/destinations:
 *  - strings and byte arrays: encode() / decode() / decodeToString();
 *  - incremental processing: Encoder and Decoder (feed data in arbitrary chunks);
 *  - streams, files and whole folders: inherited from StreamCodec (see Io.h).
 *
 * @code
 * using namespace TK4CPP::Encode;
 * std::string text = Base64::encodeDefault("Hello");    // "SGVsbG8="
 * auto bytes = Base64::decodeDefault(text);             // std::expected<std::vector<std::byte>, Error>
 * Base64 url{Base64::urlSafe()};                        // RFC 4648 section 5, no padding
 * std::string token = url.encode("\xfb\xff");           // "-_8"
 *
 * Base64{}.encodeFile("in.bin", "in.bin.b64");          // std::expected<void, Error>
 * @endcode
 */
class Base64 : public StreamCodec<Base64> {
public:
    /** @brief Alphabet used for symbols 62 and 63. */
    enum class Alphabet {
        Standard, ///< RFC 4648 section 4: `+` and `/`.
        UrlSafe   ///< RFC 4648 section 5: `-` and `_`.
    };

    /**
     * @brief Padding policy.
     *
     * When encoding, any value other than `None` emits `=` padding.
     * When decoding, `Required` demands correct padding, `Optional` accepts
     * both padded and unpadded input, `None` rejects any `=` character.
     */
    enum class Padding { Required, Optional, None };

    /** @brief Codec configuration. */
    struct Options {
        Alphabet    alphabet{Alphabet::Standard}; ///< Symbol alphabet.
        Padding     padding{Padding::Required};   ///< Padding policy (see Padding).
        std::size_t lineLength{0};                ///< Encode only: insert CRLF every N output characters (0 = never; 76 = MIME).
        bool        ignoreWhitespace{false};      ///< Decode only: silently skip space, TAB, CR and LF.
        bool        strictTrailingBits{false};    ///< Decode only: reject non-zero unused trailing bits (canonical encodings only).

        [[nodiscard]] friend constexpr bool operator==(const Options&, const Options&) noexcept = default;
    };

    /**
     * @brief Incremental encoder.
     *
     * Data can be supplied in chunks of any size; the result is identical to
     * encoding the concatenation in one call. After finish() the object is
     * reset and can be reused for a new message.
     */
    class Encoder {
    public:
        /** @brief Creates an encoder with the given options. */
        explicit Encoder(Options options) noexcept;

        /**
         * @brief Encodes a chunk.
         * @param data Bytes to encode.
         * @param out  Receives (appends) the characters that are complete so far.
         */
        void update(std::span<const std::byte> data, std::string& out);

        /**
         * @brief Flushes the last 1-2 buffered bytes, adding padding if required.
         * @param out Receives (appends) the final characters.
         */
        void finish(std::string& out);

    private:
        void put(char c, std::string& out);
        void emitGroup(std::byte b0, std::byte b1, std::byte b2, std::string& out);

        Options                  m_options;
        std::string_view         m_alphabet;
        std::array<std::byte, 3> m_pending{};
        std::size_t              m_pendingSize{0};
        std::size_t              m_column{0};
    };

    /**
     * @brief Incremental decoder.
     *
     * Text can be supplied in chunks of any size (even one character at a time).
     * Error offsets are always relative to the first character ever given to the
     * decoder since construction or the last finish(). After an error the decoder
     * keeps returning that error until finish() resets it.
     */
    class Decoder {
    public:
        /** @brief Creates a decoder with the given options. */
        explicit Decoder(Options options) noexcept;

        /**
         * @brief Decodes a chunk.
         * @param text Encoded characters.
         * @param out  Receives (appends) the bytes decoded so far (raw binary, not necessarily text).
         * @return Success or the first error found.
         */
        [[nodiscard]] std::expected<void, Error> update(std::string_view text, std::string& out);

        /**
         * @brief Signals the end of the input and performs the final checks (length, padding, trailing bits).
         * @param out Unused by Base64 (no bytes are produced at the end); kept for the common codec interface.
         * @return Success or the final-validation Error. The decoder is reset in both cases.
         */
        [[nodiscard]] std::expected<void, Error> finish(std::string& out);

    private:
        [[nodiscard]] std::unexpected<Error> fail(Error error);
        [[nodiscard]] std::expected<void, Error> checkEnd() const;
        void reset() noexcept;

        Options              m_options;
        const std::int8_t*   m_table;
        std::uint32_t        m_accumulator{0};
        unsigned             m_bits{0};
        std::size_t          m_symbolCount{0};
        std::size_t          m_padCount{0};
        std::size_t          m_firstPad{0};
        std::size_t          m_lastSymbol{0};
        std::size_t          m_offset{0};
        std::optional<Error> m_failed;
    };

    /** @brief Creates a codec with the default options (standard alphabet, required padding). */
    Base64() noexcept = default;

    /** @brief Creates a codec with the given options. */
    explicit Base64(Options options) noexcept : m_options(options) {}

    /** @brief Options for the URL and filename safe variant without padding (as used by JWT). */
    [[nodiscard]] static constexpr Options urlSafe() noexcept {
        return Options{.alphabet = Alphabet::UrlSafe, .padding = Padding::None};
    }

    /** @brief Options for MIME (RFC 2045): standard alphabet, 76 columns, whitespace tolerated on decode. */
    [[nodiscard]] static constexpr Options mime() noexcept {
        return Options{.lineLength = 76, .ignoreWhitespace = true};
    }

    /** @brief Returns the options of this codec. */
    [[nodiscard]] const Options& options() const noexcept { return m_options; }

    /** @brief Creates an incremental encoder configured like this codec. */
    [[nodiscard]] Encoder makeEncoder() const noexcept { return Encoder{m_options}; }

    /** @brief Creates an incremental decoder configured like this codec. */
    [[nodiscard]] Decoder makeDecoder() const noexcept { return Decoder{m_options}; }

    /** @brief Suffix used by encodeFolder()/decodeFolder() when none is given: `.b64`. */
    [[nodiscard]] static constexpr std::string_view defaultFileSuffix() noexcept { return ".b64"; }

    /**
     * @brief Computes the exact length of the encoded output.
     * @param inputSize Number of input bytes.
     * @return Number of characters produced by encode(), line breaks included.
     */
    [[nodiscard]] std::size_t encodedSize(std::size_t inputSize) const noexcept;

    /**
     * @brief Upper bound for the number of bytes produced by decoding.
     * @param encodedSize Number of input characters.
     * @return A value that is never smaller than the real decoded size.
     */
    [[nodiscard]] static constexpr std::size_t maxDecodedSize(std::size_t encodedSize) noexcept {
        return (encodedSize / 4) * 3 + 3;
    }

    /**
     * @brief Encodes a byte sequence.
     * @param data Bytes to encode.
     * @return The Base64 text.
     */
    [[nodiscard]] std::string encode(std::span<const std::byte> data) const;

    /**
     * @brief Encodes the bytes of a string (no charset conversion is performed).
     * @param text Input text.
     * @return The Base64 text.
     */
    [[nodiscard]] std::string encode(std::string_view text) const;

    /**
     * @brief Encodes any contiguous range of bytes (`std::vector<std::uint8_t>`, `std::array<char, N>`, ...).
     * @param range The data to encode.
     * @return The Base64 text.
     * @see ByteRange
     */
    template <ByteRange R>
    [[nodiscard]] std::string encode(const R& range) const {
        return encode(asBytes(range));
    }

    /**
     * @brief Decodes Base64 text into bytes.
     * @param text Encoded text.
     * @return The decoded bytes, or an Error describing the first problem found.
     */
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> decode(std::string_view text) const;

    /**
     * @brief Decodes Base64 text into a std::string holding the raw bytes.
     * @param text Encoded text.
     * @return The decoded bytes as a string, or an Error.
     * @note The result is not guaranteed to be valid text in any charset.
     */
    [[nodiscard]] std::expected<std::string, Error> decodeToString(std::string_view text) const;

    /** @brief Encodes with the default options. @see encode(std::span<const std::byte>) const */
    [[nodiscard]] static std::string encodeDefault(std::span<const std::byte> data);
    /** @brief Encodes with the default options. @see encode(std::string_view) const */
    [[nodiscard]] static std::string encodeDefault(std::string_view text);
    /** @brief Encodes with the default options. @see encode(const R&) const */
    template <ByteRange R>
    [[nodiscard]] static std::string encodeDefault(const R& range) {
        return encodeDefault(asBytes(range));
    }
    /** @brief Decodes with the default options. @see decode(std::string_view) const */
    [[nodiscard]] static std::expected<std::vector<std::byte>, Error> decodeDefault(std::string_view text);

private:
    Options m_options{};
};

} // namespace TK4CPP::Encode
