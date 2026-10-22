/**
 * @file Io.h
 * @brief Stream, file and folder support shared by all TK4CPP::Encode codecs.
 *
 * A codec (Base64, UrlEncoding) gains the whole I/O surface by deriving from
 * StreamCodec<Derived>. The derived class only has to provide:
 *
 *  - `Derived::Encoder` with `void update(std::span<const std::byte>, std::string&)` and `void finish(std::string&)`;
 *  - `Derived::Decoder` with `std::expected<void, Error> update(std::string_view, std::string&)` and
 *    `std::expected<void, Error> finish(std::string&)`;
 *  - `makeEncoder() const`, `makeDecoder() const` and `static defaultFileSuffix()`.
 *
 * Both `update()` functions *append* to the output string, which is only a byte
 * container here (it may hold arbitrary binary data).
 *
 * @note All file and folder operations are implemented with `std::filesystem`.
 */
#pragma once

#include <TK4CPP/Encode/Error.h>

#include <concepts>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace TK4CPP::Encode {

/**
 * @brief Contiguous range of single-byte elements, usable as binary input.
 *
 * Accepts `std::vector<std::uint8_t>`, `std::array<char, N>`, `std::span<const std::byte>`, ...
 * It deliberately rejects raw arrays (a string literal would include its terminating NUL)
 * and anything convertible to `std::string_view` (those use the text overloads).
 */
template <class R>
concept ByteRange =
    std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
    !std::is_array_v<std::remove_cvref_t<R>> &&
    !std::convertible_to<const std::remove_cvref_t<R>&, std::string_view> &&
    (std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, char> ||
     std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, signed char> ||
     std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, unsigned char> ||
     std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, std::byte>);

/**
 * @brief Views a ByteRange as a span of const bytes (no copy).
 * @param range Any ByteRange.
 * @return A non-owning view of the range's storage.
 */
template <ByteRange R>
[[nodiscard]] std::span<const std::byte> asBytes(const R& range) noexcept {
    return std::as_bytes(std::span{std::ranges::data(range), std::ranges::size(range)});
}

/** @brief Options for single-file operations. */
struct FileOptions {
    bool overwrite{true};          ///< If false, fail when the output file already exists.
    bool createDirectories{true};  ///< Create missing parent directories of the output file.
};

/** @brief Options for folder operations. */
struct FolderOptions {
    bool recursive{true};                  ///< Descend into sub-folders (the tree is mirrored in the output folder).
    bool overwrite{true};                  ///< If false, existing output files are reported as failures.
    bool stopOnError{false};               ///< Stop at the first failing file instead of processing all of them.
    std::optional<std::string> suffix{};   ///< Suffix appended on encode and required/stripped on decode.
                                           ///< Defaults to the codec's defaultFileSuffix().
};

/** @brief Outcome for one file processed by a folder operation. */
struct FolderEntry {
    std::filesystem::path       source; ///< Input file.
    std::filesystem::path       target; ///< Output file.
    std::expected<void, Error>  result; ///< Success or the reason of the failure.
};

/** @brief Summary of a folder operation. */
struct FolderReport {
    std::vector<FolderEntry> entries;   ///< One element per processed file, sorted by source path.
    std::size_t succeeded{0};           ///< Number of files processed successfully.
    std::size_t failed{0};              ///< Number of files that failed.
    std::size_t skipped{0};             ///< Files ignored (decode: name without the suffix; files inside the output folder).

    /** @brief True when no file failed. */
    [[nodiscard]] bool ok() const noexcept { return failed == 0; }
};

namespace detail {

/** @brief Type-erased incremental encoder. */
struct EncodeStep {
    std::function<void(std::span<const std::byte>, std::string&)> update;
    std::function<void(std::string&)>                              finish;
};

/** @brief Type-erased incremental decoder. */
struct DecodeStep {
    std::function<std::expected<void, Error>(std::string_view, std::string&)> update;
    std::function<std::expected<void, Error>(std::string&)>                    finish;
};

using EncodeFactory = std::function<EncodeStep()>;
using DecodeFactory = std::function<DecodeStep()>;

std::expected<void, Error> encodeStream(const EncodeFactory&, std::istream&, std::ostream&);
std::expected<void, Error> decodeStream(const DecodeFactory&, std::istream&, std::ostream&);

std::expected<void, Error> encodeFile(const EncodeFactory&, const std::filesystem::path&,
                                      const std::filesystem::path&, const FileOptions&);
std::expected<void, Error> decodeFile(const DecodeFactory&, const std::filesystem::path&,
                                      const std::filesystem::path&, const FileOptions&);

std::expected<std::string, Error> encodeFileToString(const EncodeFactory&, const std::filesystem::path&);
std::expected<std::string, Error> decodeFileToString(const DecodeFactory&, const std::filesystem::path&);

std::expected<FolderReport, Error> encodeFolder(const EncodeFactory&, const std::filesystem::path&,
                                                const std::filesystem::path&, const FolderOptions&,
                                                std::string_view defaultSuffix);
std::expected<FolderReport, Error> decodeFolder(const DecodeFactory&, const std::filesystem::path&,
                                                const std::filesystem::path&, const FolderOptions&,
                                                std::string_view defaultSuffix);

} // namespace detail

/**
 * @brief CRTP mixin that adds stream, file and folder operations to a codec.
 * @tparam Derived The codec class (see the file documentation for its requirements).
 */
template <class Derived>
class StreamCodec {
public:
    /**
     * @brief Encodes everything readable from a binary stream.
     *
     * Memory use is bounded (64 KiB chunks), so arbitrarily large inputs are fine.
     * The streams are not closed; the output is flushed.
     *
     * @param in  Source stream (read until EOF).
     * @param out Destination stream.
     * @return Success, or an Error with ErrorCode::IoError if reading or writing failed.
     */
    [[nodiscard]] std::expected<void, Error> encodeStream(std::istream& in, std::ostream& out) const {
        return detail::encodeStream(encoderFactory(), in, out);
    }

    /**
     * @brief Decodes everything readable from a text stream.
     * @param in  Source stream with encoded text (read until EOF).
     * @param out Destination stream receiving the raw decoded bytes.
     * @return Success, or the decoding Error (offset relative to the start of the stream) or an I/O Error.
     */
    [[nodiscard]] std::expected<void, Error> decodeStream(std::istream& in, std::ostream& out) const {
        return detail::decodeStream(decoderFactory(), in, out);
    }

    /**
     * @brief Encodes a file into another file.
     *
     * The output is written to `<output>.tk4tmp` and renamed on success, so a failure
     * never leaves a truncated output nor destroys an existing one.
     * Input and output must not be the same file.
     *
     * @param input   File to read.
     * @param output  File to create.
     * @param options Overwrite / directory creation policy.
     */
    [[nodiscard]] std::expected<void, Error> encodeFile(const std::filesystem::path& input,
                                                        const std::filesystem::path& output,
                                                        const FileOptions& options = {}) const {
        return detail::encodeFile(encoderFactory(), input, output, options);
    }

    /** @brief Decodes a file into another file. @copydetails encodeFile */
    [[nodiscard]] std::expected<void, Error> decodeFile(const std::filesystem::path& input,
                                                        const std::filesystem::path& output,
                                                        const FileOptions& options = {}) const {
        return detail::decodeFile(decoderFactory(), input, output, options);
    }

    /**
     * @brief Encodes a file and returns the text.
     * @warning The whole result is held in memory; use encodeFile() for large files.
     */
    [[nodiscard]] std::expected<std::string, Error> encodeFileToString(const std::filesystem::path& input) const {
        return detail::encodeFileToString(encoderFactory(), input);
    }

    /**
     * @brief Decodes a file and returns the raw bytes as a string.
     * @warning The whole result is held in memory; use decodeFile() for large files.
     */
    [[nodiscard]] std::expected<std::string, Error> decodeFileToString(const std::filesystem::path& input) const {
        return detail::decodeFileToString(decoderFactory(), input);
    }

    /** @brief Decodes a file and returns the bytes. @copydetails decodeFileToString */
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> decodeFileToBytes(const std::filesystem::path& input) const {
        auto text = decodeFileToString(input);
        if (!text) {
            return std::unexpected(std::move(text.error()));
        }
        const auto bytes = std::as_bytes(std::span{text->data(), text->size()});
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    /**
     * @brief Encodes every regular file of a folder into a mirrored folder.
     *
     * Each `dir/a/b.txt` becomes `output/a/b.txt<suffix>`. The list of files is
     * collected before processing starts, so an output folder located inside the
     * input folder is never re-read. Per-file failures do not abort the run
     * (unless FolderOptions::stopOnError); they are listed in the report.
     *
     * @param inputDir  Existing folder to read.
     * @param outputDir Folder to write (created if missing).
     * @param options   Recursion, overwrite and suffix policy.
     * @return The per-file report, or an Error if the input folder cannot be read at all.
     */
    [[nodiscard]] std::expected<FolderReport, Error> encodeFolder(const std::filesystem::path& inputDir,
                                                                  const std::filesystem::path& outputDir,
                                                                  const FolderOptions& options = {}) const {
        return detail::encodeFolder(encoderFactory(), inputDir, outputDir, options, Derived::defaultFileSuffix());
    }

    /**
     * @brief Decodes every file ending with the suffix of a folder into a mirrored folder.
     *
     * `dir/a/b.txt<suffix>` becomes `output/a/b.txt`. Files that do not end with the
     * suffix are counted as skipped.
     *
     * @copydetails encodeFolder
     */
    [[nodiscard]] std::expected<FolderReport, Error> decodeFolder(const std::filesystem::path& inputDir,
                                                                  const std::filesystem::path& outputDir,
                                                                  const FolderOptions& options = {}) const {
        return detail::decodeFolder(decoderFactory(), inputDir, outputDir, options, Derived::defaultFileSuffix());
    }

private:
    [[nodiscard]] detail::EncodeFactory encoderFactory() const {
        return [self = static_cast<const Derived&>(*this)]() {
            auto encoder = std::make_shared<typename Derived::Encoder>(self.makeEncoder());
            return detail::EncodeStep{
                [encoder](std::span<const std::byte> data, std::string& out) { encoder->update(data, out); },
                [encoder](std::string& out) { encoder->finish(out); }};
        };
    }

    [[nodiscard]] detail::DecodeFactory decoderFactory() const {
        return [self = static_cast<const Derived&>(*this)]() {
            auto decoder = std::make_shared<typename Derived::Decoder>(self.makeDecoder());
            return detail::DecodeStep{
                [decoder](std::string_view text, std::string& out) { return decoder->update(text, out); },
                [decoder](std::string& out) { return decoder->finish(out); }};
        };
    }
};

} // namespace TK4CPP::Encode
