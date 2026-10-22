/**
 * @file Io.cpp
 * @brief Stream, file and folder processing shared by all TK4CPP::Encode codecs.
 */
#include <TK4CPP/Encode/Io.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <system_error>

namespace TK4CPP::Encode::detail {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kChunkSize = 64 * 1024;

[[nodiscard]] std::unexpected<Error> ioError(std::string detail, std::size_t position = 0) {
    return std::unexpected(Error{ErrorCode::IoError, position, std::move(detail)});
}

[[nodiscard]] std::string describe(std::string_view what, const fs::path& path, const std::error_code& ec = {}) {
    std::string text{what};
    text += ": ";
    text += path.string();
    if (ec) {
        text += " (";
        text += ec.message();
        text += ')';
    }
    return text;
}

/** @brief Writes and clears the buffer; false if the stream went bad. */
bool flushTo(std::string& buffer, std::ostream& out) {
    if (!buffer.empty()) {
        out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        buffer.clear();
    }
    return static_cast<bool>(out);
}

/** @brief Calls `consume(chunk)` for every chunk of the stream; reports read failures. */
template <class Consume>
std::expected<void, Error> readChunks(std::istream& in, Consume&& consume) {
    std::vector<char> buffer(kChunkSize);
    std::size_t consumed = 0;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(in.gcount());
        if (count != 0) {
            if (auto r = consume(std::string_view{buffer.data(), count}, consumed); !r) {
                return r;
            }
            consumed += count;
        }
    }
    if (in.bad() || !in.eof()) {
        return ioError("read failed", consumed);
    }
    return {};
}

using RunFunction = std::function<std::expected<void, Error>(std::istream&, std::ostream&)>;

/**
 * @brief Runs a stream transformation from a file to a file, writing through a temporary file.
 */
std::expected<void, Error> transformFile(const RunFunction& run, const fs::path& input, const fs::path& output,
                                         const FileOptions& options) {
    std::error_code ec;
    if (!fs::is_regular_file(input, ec)) {
        return ioError(describe("input is not a readable regular file", input, ec));
    }
    if (fs::exists(output, ec)) {
        std::error_code same;
        if (fs::equivalent(input, output, same)) {
            return ioError(describe("input and output are the same file", output));
        }
        if (!options.overwrite) {
            return ioError(describe("output already exists", output));
        }
    }
    if (options.createDirectories && output.has_parent_path()) {
        fs::create_directories(output.parent_path(), ec);
        if (ec) {
            return ioError(describe("cannot create output folder", output.parent_path(), ec));
        }
    }

    std::ifstream in(input, std::ios::binary);
    if (!in) {
        return ioError(describe("cannot open input", input));
    }

    fs::path temp = output;
    temp += ".tk4tmp";

    std::expected<void, Error> result;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return ioError(describe("cannot create output", temp));
        }
        result = run(in, out);
        out.close();
        if (result && out.fail()) {
            result = ioError(describe("cannot write output", temp));
        }
    }

    if (result) {
        fs::rename(temp, output, ec);
        if (ec) {
            result = ioError(describe("cannot move output into place", output, ec));
        }
    }
    if (!result) {
        std::error_code ignored;
        fs::remove(temp, ignored);
    }
    return result;
}

/** @brief Runs a stream transformation from a file to a string. */
std::expected<std::string, Error> transformFileToString(const RunFunction& run, const fs::path& input) {
    std::error_code ec;
    if (!fs::is_regular_file(input, ec)) {
        return ioError(describe("input is not a readable regular file", input, ec));
    }
    std::ifstream in(input, std::ios::binary);
    if (!in) {
        return ioError(describe("cannot open input", input));
    }
    std::ostringstream out(std::ios::binary);
    if (auto r = run(in, out); !r) {
        return std::unexpected(std::move(r.error()));
    }
    return std::move(out).str();
}

[[nodiscard]] bool isStrictlyInside(const fs::path& child, const fs::path& parent) {
    const fs::path relative = child.lexically_relative(parent);
    return !relative.empty() && relative != "." && *relative.begin() != "..";
}

template <class Iterator>
bool collectRegularFiles(Iterator it, std::vector<fs::path>& files, std::error_code& ec) {
    const Iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc)) {
            files.push_back(it->path());
        }
    }
    return !ec;
}

using FileFunction = std::function<std::expected<void, Error>(const fs::path&, const fs::path&, const FileOptions&)>;

/**
 * @brief Shared folder walker.
 * @param encoding If true the suffix is appended to the target names, otherwise it is required and stripped.
 */
std::expected<FolderReport, Error> transformFolder(bool encoding, const FileFunction& processFile,
                                                   const fs::path& inputDir, const fs::path& outputDir,
                                                   const FolderOptions& options, std::string_view defaultSuffix) {
    const std::string suffix = options.suffix.value_or(std::string{defaultSuffix});

    std::error_code ec;
    if (!fs::is_directory(inputDir, ec)) {
        return ioError(describe("input is not a folder", inputDir, ec));
    }

    std::vector<fs::path> files;
    const bool listed = options.recursive
                            ? collectRegularFiles(fs::recursive_directory_iterator(inputDir, ec), files, ec)
                            : collectRegularFiles(fs::directory_iterator(inputDir, ec), files, ec);
    if (!listed) {
        return ioError(describe("cannot list folder", inputDir, ec));
    }
    std::ranges::sort(files);

    // An output folder nested in the input folder must not be processed again.
    std::error_code canonEc;
    const fs::path inputCanonical  = fs::weakly_canonical(inputDir, canonEc);
    const fs::path outputCanonical = fs::weakly_canonical(outputDir, canonEc);
    const bool outputInsideInput = !canonEc && outputCanonical != inputCanonical &&
                                   isStrictlyInside(outputCanonical, inputCanonical);

    const FileOptions fileOptions{.overwrite = options.overwrite, .createDirectories = true};

    FolderReport report;
    for (const fs::path& source : files) {
        if (outputInsideInput) {
            std::error_code e;
            if (isStrictlyInside(fs::weakly_canonical(source, e), outputCanonical)) {
                ++report.skipped;
                continue;
            }
        }

        const fs::path relative = source.lexically_relative(inputDir);
        fs::path target;
        if (encoding) {
            target = outputDir / relative;
            target += suffix;
        } else {
            const std::string name = relative.filename().string();
            if (suffix.empty() || name.size() <= suffix.size() || !name.ends_with(suffix)) {
                ++report.skipped;
                continue;
            }
            target = outputDir / relative.parent_path() / name.substr(0, name.size() - suffix.size());
        }

        FolderEntry entry{source, target, processFile(source, target, fileOptions)};
        if (entry.result) {
            ++report.succeeded;
        } else {
            ++report.failed;
        }
        const bool stop = !entry.result && options.stopOnError;
        report.entries.push_back(std::move(entry));
        if (stop) {
            break;
        }
    }
    return report;
}

} // namespace

// ---------------------------------------------------------------------------
// Streams
// ---------------------------------------------------------------------------

std::expected<void, Error> encodeStream(const EncodeFactory& factory, std::istream& in, std::ostream& out) {
    const EncodeStep step = factory();
    std::string encoded;

    auto read = readChunks(in, [&](std::string_view chunk, std::size_t consumed) -> std::expected<void, Error> {
        step.update(std::as_bytes(std::span{chunk.data(), chunk.size()}), encoded);
        if (!flushTo(encoded, out)) {
            return ioError("write failed", consumed + chunk.size());
        }
        return {};
    });
    if (!read) {
        return read;
    }

    step.finish(encoded);
    if (!flushTo(encoded, out) || !out.flush()) {
        return ioError("write failed");
    }
    return {};
}

std::expected<void, Error> decodeStream(const DecodeFactory& factory, std::istream& in, std::ostream& out) {
    const DecodeStep step = factory();
    std::string decoded;

    auto read = readChunks(in, [&](std::string_view chunk, std::size_t consumed) -> std::expected<void, Error> {
        if (auto r = step.update(chunk, decoded); !r) {
            return r;
        }
        if (!flushTo(decoded, out)) {
            return ioError("write failed", consumed + chunk.size());
        }
        return {};
    });
    if (!read) {
        return read;
    }

    if (auto r = step.finish(decoded); !r) {
        return r;
    }
    if (!flushTo(decoded, out) || !out.flush()) {
        return ioError("write failed");
    }
    return {};
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::expected<void, Error> encodeFile(const EncodeFactory& factory, const fs::path& input, const fs::path& output,
                                      const FileOptions& options) {
    return transformFile([&](std::istream& in, std::ostream& out) { return encodeStream(factory, in, out); },
                         input, output, options);
}

std::expected<void, Error> decodeFile(const DecodeFactory& factory, const fs::path& input, const fs::path& output,
                                      const FileOptions& options) {
    return transformFile([&](std::istream& in, std::ostream& out) { return decodeStream(factory, in, out); },
                         input, output, options);
}

std::expected<std::string, Error> encodeFileToString(const EncodeFactory& factory, const fs::path& input) {
    return transformFileToString([&](std::istream& in, std::ostream& out) { return encodeStream(factory, in, out); },
                                 input);
}

std::expected<std::string, Error> decodeFileToString(const DecodeFactory& factory, const fs::path& input) {
    return transformFileToString([&](std::istream& in, std::ostream& out) { return decodeStream(factory, in, out); },
                                 input);
}

// ---------------------------------------------------------------------------
// Folders
// ---------------------------------------------------------------------------

std::expected<FolderReport, Error> encodeFolder(const EncodeFactory& factory, const fs::path& inputDir,
                                                const fs::path& outputDir, const FolderOptions& options,
                                                std::string_view defaultSuffix) {
    return transformFolder(
        true,
        [&](const fs::path& in, const fs::path& out, const FileOptions& fileOptions) {
            return encodeFile(factory, in, out, fileOptions);
        },
        inputDir, outputDir, options, defaultSuffix);
}

std::expected<FolderReport, Error> decodeFolder(const DecodeFactory& factory, const fs::path& inputDir,
                                                const fs::path& outputDir, const FolderOptions& options,
                                                std::string_view defaultSuffix) {
    return transformFolder(
        false,
        [&](const fs::path& in, const fs::path& out, const FileOptions& fileOptions) {
            return decodeFile(factory, in, out, fileOptions);
        },
        inputDir, outputDir, options, defaultSuffix);
}

} // namespace TK4CPP::Encode::detail
