// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file Zip.h
 * @brief Core layer: C++23 RAII wrapper around libzip.
 *
 * The classes in this header mirror libzip's object model with value
 * semantics and RAII:
 *
 * | Class         | Role                                              | libzip counterpart |
 * |---------------|---------------------------------------------------|--------------------|
 * | Zip::Archive  | owns an open archive                              | `zip_t`            |
 * | Zip::Entry    | non-owning handle to one member of an archive     | an index in `zip_t`|
 * | Zip::File     | one entry opened for (streaming) reading          | `zip_file_t`       |
 * | Zip::Source   | data waiting to be added to an archive            | `zip_source_t`     |
 *
 * Every libzip failure is reported as Zip::Exception; filesystem failures
 * raised while extracting propagate as `std::filesystem::filesystem_error`.
 *
 * For one-call operations (archive a directory, list files, transparent
 * passwords, verification) use the @ref zip_highlevel "high-level API" in
 * HighLevel.h, which is built on this layer.
 *
 * @par Where to read next
 *  - @ref guide_core for lifetimes, commit semantics and streaming
 *  - @ref pitfalls for the behaviours that surprise people
 *  - @ref backends for what depends on how libzip was built
 *  - @ref errors for the meaning of each Zip::Exception::zipCode()
 *
 * @par Requirements
 * C++23 and libzip >= 1.11; link the library (@ref library_build, @ref linking). The implementation uses only non-deprecated
 * libzip APIs.
 *
 * @par Thread safety
 * libzip allows independent `zip_t` objects to be used concurrently but not
 * one object from several threads. Zip::Archive adds no hidden locking:
 * create one Archive per concurrent operation or synchronise externally.
 *
 * @par License
 * Released under the MIT License; see the `LICENSE` file at the root of the repository.
 */

#pragma once

#include <zip.h>

#include <ZipWrapper/Export.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

ZIPWRAPPER_DISABLE_DLL_WARNINGS_BEGIN

namespace Zip {

/**
 * @brief Exception thrown for every libzip failure.
 * @ingroup zip_errors
 *
 * what() is a human-readable message that starts with the failing libzip
 * operation (for example `"zip_open: No such file"`). The numeric codes allow
 * programmatic handling; see @ref errors for the codes this library raises and
 * when.
 *
 * @code
 * try {
 *     auto archive = Zip::Archive::open("data.zip", Zip::OpenFlag::ReadOnly);
 * } catch (const Zip::Exception& e) {
 *     if (e.zipCode() == ZIP_ER_NOENT) { ... }   // file does not exist
 * }
 * @endcode
 */
class ZIPWRAPPER_API Exception : public std::runtime_error {
public:
    /**
     * @brief Construct an exception.
     * @param zipCode    libzip error code (ZIP_ER_*).
     * @param systemCode system error code (errno-like), 0 if none.
     * @param message    human readable description.
     */
    Exception(int zipCode, int systemCode, std::string message)
        : std::runtime_error(std::move(message)),
          zipCode_(zipCode),
          systemCode_(systemCode) {}

    /**
     * @brief The libzip error code (`ZIP_ER_*`).
     * @return One of the `ZIP_ER_*` constants from `<zip.h>`.
     */
    [[nodiscard]] int zipCode() const noexcept { return zipCode_; }
    /**
     * @brief The system error code (an `errno` value), if libzip recorded one.
     * @return The system error, or 0 when the failure was not caused by the OS.
     */
    [[nodiscard]] int systemCode() const noexcept { return systemCode_; }

private:
    int zipCode_{ZIP_ER_OK};
    int systemCode_{0};
};

/**
 * @brief Describe the last error recorded on an archive.
 * @ingroup zip_errors
 * @param[in] archive The libzip archive handle; may be null.
 * @return libzip's message, or a fixed text if @p archive is null.
 */
ZIPWRAPPER_API std::string errorMessage(zip_t* archive);

/**
 * @brief Throw an Exception built from the last error of an archive.
 * @ingroup zip_errors
 * @param[in] archive   The libzip archive handle the failed call used.
 * @param[in] operation Name of the failed libzip call, prefixed to the message.
 * @throws Exception Always.
 */
[[noreturn]] ZIPWRAPPER_API void throwArchiveError(zip_t* archive,
                                            std::string_view operation);

/**
 * @brief Throw an Exception built from the last error of an opened file.
 * @ingroup zip_errors
 * @param[in] archive   The archive the file belongs to (fallback for the message).
 * @param[in] file      The libzip file handle that failed; may be null.
 * @param[in] operation Name of the failed libzip call, prefixed to the message.
 * @throws Exception Always.
 */
[[noreturn]] ZIPWRAPPER_API void throwFileError(zip_t* archive,
                                        zip_file_t* file,
                                        std::string_view operation);

namespace detail {

/**
 * @brief Length value meaning "up to the end" for the libzip source
 * functions used by this wrapper.
 *
 * @note ZIP_LENGTH_TO_END is 0 in libzip, but zip_source_zip_file() treats
 * 0 as "zero bytes" (verified: it silently produces empty entries). -1 is
 * accepted by every source function used here.
 */
inline constexpr std::int64_t kLengthToEnd = -1;

struct CallbackState;  // defined inside the library (src/Detail.h)

} // namespace detail

/**
 * @brief Read a password from an environment variable.
 * @ingroup zip_types
 *
 * Prefer this (or a secret store) over command-line arguments, which other
 * users can see in the process list. On Windows the variable is read through
 * the wide-character API and converted to UTF-8, so a non-ASCII password is
 * the same byte sequence on every platform; `std::getenv` is not used there
 * because MSVC flags it as unsafe (C4996).
 *
 * @param[in] variable Name of the environment variable.
 * @return The value, or `std::nullopt` if the variable is unset or empty.
 */
[[nodiscard]] ZIPWRAPPER_API std::optional<std::string> passwordFromEnv(
    const char* variable = "ZIP_PASSWORD");

/**
 * @brief Flags for Archive::open().
 * @ingroup zip_types
 *
 * Combine flags with `operator|`. Passing OpenFlag::None opens an existing
 * archive for reading and writing.
 */
enum class OpenFlag : unsigned {
    None      = 0,  ///< Open an existing archive for reading and writing.
    ReadOnly  = 1u << 0,  ///< Open for reading only (maps to `ZIP_RDONLY`).
    Create    = 1u << 1,  ///< Create the archive if it does not exist (`ZIP_CREATE`).
    Exclusive = 1u << 2,  ///< With Create: fail with `ZIP_ER_EXISTS` if the file exists (`ZIP_EXCL`).
    Truncate  = 1u << 3,  ///< Ignore any existing content (`ZIP_TRUNCATE`).
    CheckCons = 1u << 4  ///< Run libzip's extra consistency checks on open (`ZIP_CHECKCONS`).
};

/**
 * @brief Combine two sets of open flags.
 * @ingroup zip_types
 * @param[in] a First set.
 * @param[in] b Second set.
 * @return The union of both sets.
 */
constexpr OpenFlag operator|(OpenFlag a, OpenFlag b) noexcept {
    return static_cast<OpenFlag>(std::to_underlying(a) | std::to_underlying(b));
}
/**
 * @brief Intersect two sets of open flags.
 * @ingroup zip_types
 * @param[in] a First set.
 * @param[in] b Second set.
 * @return The flags present in both sets.
 */
constexpr OpenFlag operator&(OpenFlag a, OpenFlag b) noexcept {
    return static_cast<OpenFlag>(std::to_underlying(a) & std::to_underlying(b));
}
/**
 * @brief Test whether any bit of @p f is set in @p v.
 * @ingroup zip_types
 * @param[in] v The flag set to inspect.
 * @param[in] f The flag (or flags) to look for.
 * @return `true` if at least one bit of @p f is set in @p v.
 */
constexpr bool hasFlag(OpenFlag v, OpenFlag f) noexcept {
    return (std::to_underlying(v) & std::to_underlying(f)) != 0;
}

/**
 * @brief Translate OpenFlag to libzip's `ZIP_*` open flags.
 * @ingroup zip_types
 * @param[in] f The wrapper flags.
 * @return The value to pass to `zip_open()`.
 */
inline int nativeOpenFlags(OpenFlag f) {
    int r = 0;
    if (hasFlag(f, OpenFlag::ReadOnly))  r |= ZIP_RDONLY;
    if (hasFlag(f, OpenFlag::Create))    r |= ZIP_CREATE;
    if (hasFlag(f, OpenFlag::Exclusive)) r |= ZIP_EXCL;
    if (hasFlag(f, OpenFlag::Truncate))  r |= ZIP_TRUNCATE;
    if (hasFlag(f, OpenFlag::CheckCons)) r |= ZIP_CHECKCONS;
    return r;
}

/**
 * @brief Compression methods.
 * @ingroup zip_types
 *
 * Only Store and Deflate are always available: BZip2, Lzma and Zstd depend on
 * how libzip was built. Check with Archive::compressionSupported() or list
 * what is available with Archive::supportedCompressions(). The underlying
 * type is signed because `ZIP_CM_DEFAULT` is -1.
 */
enum class Compression : std::int32_t {
    Store   = ZIP_CM_STORE,  ///< No compression.
    Deflate = ZIP_CM_DEFLATE,  ///< Deflate (zlib); the usual choice and always available.
    BZip2   = ZIP_CM_BZIP2,  ///< bzip2; needs libzip built with bzip2.
    Lzma    = ZIP_CM_LZMA,  ///< LZMA; needs libzip built with liblzma.
    Zstd    = ZIP_CM_ZSTD,  ///< Zstandard; needs libzip built with zstd.
    Default = ZIP_CM_DEFAULT  ///< Let libzip choose (Deflate, or Store if that does not shrink the data).
};

/**
 * @brief Encryption methods.
 * @ingroup zip_types
 *
 * The AES methods are WinZip AES and need a crypto backend in libzip
 * (OpenSSL, GnuTLS, mbed TLS, Common Crypto or Windows CNG). Traditional
 * PKWARE encryption ("ZipCrypto") is weak and is not offered by the
 * @ref zip_highlevel "high-level API". Encryption protects the contents only;
 * see @ref security.
 */
enum class Encryption : std::uint16_t {
    None        = ZIP_EM_NONE,  ///< No encryption (also used to remove it).
    AES128      = ZIP_EM_AES_128,  ///< WinZip AES with a 128-bit key.
    AES192      = ZIP_EM_AES_192,  ///< WinZip AES with a 192-bit key.
    AES256      = ZIP_EM_AES_256,  ///< WinZip AES with a 256-bit key (recommended).
    Traditional = ZIP_EM_TRAD_PKWARE  ///< Traditional PKWARE encryption; weak, avoid.
};

/**
 * @brief Metadata of one archive member, as returned by Entry::stat().
 * @ingroup zip_types
 */
struct Stat {
    /// Entry name: UTF-8, relative, '/' separated; directories end with '/'.
    std::string name;
    /// Index of the entry in the archive.
    std::uint64_t index{};
    /// Uncompressed size in bytes.
    std::uint64_t size{};
    /// Size in bytes as stored in the archive (includes the AES overhead for encrypted entries).
    std::uint64_t compressedSize{};
    /// Modification time, Unix seconds.
    std::int64_t modificationTime{};
    /// CRC-32 of the uncompressed data. It is 0 for WinZip AES (AE-2) entries,
    /// which rely on an authentication code instead of a CRC.
    std::uint32_t crc{};
    /// The `ZIP_CM_*` method the entry is stored with.
    std::uint16_t compressionMethod{ZIP_CM_STORE};
    /// The `ZIP_EM_*` method; `ZIP_EM_NONE` if the entry is not encrypted.
    std::uint16_t encryptionMethod{ZIP_EM_NONE};
    /// libzip's general-purpose flags for the entry.
    std::uint32_t flags{};
    /// `true` for a directory entry (name ends with '/').
    bool directory{};
};

/**
 * @brief Policy for Entry::extract() and Archive::extractAll().
 * @ingroup zip_types
 *
 * The defaults are the safe ones: unsafe paths are refused and existing files
 * are not overwritten.
 */
struct ExtractOptions {
    /// If true, create missing parent directories.
    bool createDirectories = true;
    /// If true, reject absolute paths, ".." components and any path that
    /// resolves (through symlinks) outside the destination directory.
    bool rejectUnsafePaths = true;
    /// If true, overwrite existing files.
    bool overwrite = false;
    /// Password used for encrypted entries. Ignored by unencrypted entries.
    std::optional<std::string> password;
};

/**
 * @brief RAII owner of a libzip source.
 * @ingroup zip_core
 *
 * A Source is independent of any Archive until it is handed over with
 * Archive::add() or Archive::replace(); at that point libzip takes ownership.
 * A Source that is never handed over is freed by its destructor.
 */
class ZIPWRAPPER_API Source {
public:
    /** @brief Construct an empty source (valid() is `false`). */
    Source() = default;
    /** @brief Free the source if it is still owned. */
    ~Source();

    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    /**
     * @brief Move constructor: take ownership of another source.
     * @param[in,out] o The source to take over; it is left empty.
     */
    Source(Source&& o) noexcept;
    /**
     * @brief Move assignment: free the current source, then take over another.
     * @param[in,out] o The source to take over; it is left empty.
     * @return This object.
     */
    Source& operator=(Source&& o) noexcept;

    /**
     * @brief Create a source backed by a filesystem file.
     *
     * libzip opens the file lazily, when the archive is closed, so a missing or
     * unreadable file is reported by Archive::close(), not here.
     *
     * @param[in] path   File to read.
     * @param[in] start  Offset of the first byte to use.
     * @param[in] length Number of bytes to use, or a negative value for "to the end".
     * @return The new source.
     * @throws Exception if libzip cannot create the source.
     */
    [[nodiscard]] static Source fromFile(
        const std::filesystem::path& path,
        std::uint64_t start = 0,
        std::int64_t length = detail::kLengthToEnd);

    /**
     * @brief Create a source backed by a memory buffer.
     *
     * The bytes are copied into storage owned by libzip, so the caller does not
     * have to keep @p data alive until Archive::close().
     *
     * @param[in] data The bytes to store; may be empty.
     * @return The new source.
     * @throws Exception on allocation or libzip failure.
     */
    [[nodiscard]] static Source fromBuffer(std::span<const std::byte> data);

    /**
     * @brief Give up ownership and return the raw handle (normally to libzip).
     * @return The `zip_source_t*`, or null if the source was empty. The caller
     *         becomes responsible for freeing it.
     */
    [[nodiscard]] zip_source_t* release() noexcept {
        return std::exchange(source_, nullptr);
    }

    /** @brief Whether this object currently owns a source. @return `true` if it does. */
    [[nodiscard]] bool valid() const noexcept { return source_ != nullptr; }
    /** @brief Access the raw handle without giving up ownership. @return The `zip_source_t*`, possibly null. */
    [[nodiscard]] zip_source_t* native() const noexcept { return source_; }

    /** @brief Free the source if still owned. */
    void reset() noexcept;

private:
    zip_source_t* source_{nullptr};
};

/**
 * @brief RAII owner of an archive entry opened for reading.
 * @ingroup zip_core
 *
 * Obtain one from Entry::open(), Entry::openEncrypted(), Archive::openEntry()
 * or Archive::openEntryEncrypted(). It must not outlive its Archive. Reading is
 * streaming: use read() for chunks or readAll() for the whole entry.
 *
 * Data errors (CRC mismatch, AES authentication failure) are reported by read()
 * and close() as Exception.
 */
class ZIPWRAPPER_API File {
public:
    /** @brief Construct a closed file. */
    File() = default;
    /**
     * @brief Take ownership of a libzip file handle.
     * @param[in] archive The archive the file belongs to (used for error messages).
     * @param[in] file    The handle returned by `zip_fopen*`; ownership is taken.
     */
    File(zip_t* archive, zip_file_t* file);

    /** @brief Close the file, ignoring errors (use close() to see them). */
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    /**
     * @brief Move constructor: take over another open file.
     * @param[in,out] o The file to take over; it is left closed.
     */
    File(File&& o) noexcept;

    /**
     * @brief Move assignment: close the current file, then take over another.
     * @param[in,out] o The file to take over; it is left closed.
     * @return This object.
     */
    File& operator=(File&& o) noexcept;

    /**
     * @brief Read up to `buffer.size()` bytes.
     * @param[out] buffer Where to store the bytes.
     * @return The number of bytes read; 0 at the end of the entry.
     * @throws Exception if the file is closed or on a read, CRC or authentication error.
     */
    std::size_t read(std::span<std::byte> buffer);

    /**
     * @brief Read the remaining bytes of the entry into memory.
     *
     * The file is not closed afterwards; call close() to also have CRC and
     * authentication failures reported.
     *
     * @return All remaining bytes.
     * @throws Exception if the file is closed or on a read error.
     */
    std::vector<std::byte> readAll();

    /**
     * @brief Whether seek() can be used on this stream.
     * @return `true` if libzip reports the stream as seekable (typically stored,
     *         unencrypted entries); compressed entries usually are not.
     * @throws Exception on a libzip error.
     */
    [[nodiscard]] bool seekable() const;

    /**
     * @brief Change the read position.
     * @param[in] offset Offset in bytes, relative to @p whence.
     * @param[in] whence `SEEK_SET`, `SEEK_CUR` or `SEEK_END`.
     * @throws Exception if the stream is not seekable (see seekable()), the file
     *         is closed, or the position is invalid.
     */
    void seek(std::int64_t offset, int whence = SEEK_SET);

    /**
     * @brief Report the current read position.
     * @return The offset from the start of the uncompressed data.
     * @throws Exception if the file is closed or on a libzip error.
     */
    [[nodiscard]] std::int64_t tell() const;

    /**
     * @brief Close the file, reporting errors (e.g. CRC mismatch).
     *
     * The handle is released even when this function throws.
     */
    void close();

    /** @brief Close the file, ignoring errors. */
    void reset() noexcept;

private:
    zip_t* archive_{nullptr};
    zip_file_t* file_{nullptr};
};

class Archive;

/**
 * @brief Non-owning handle to one member of an Archive.
 * @ingroup zip_core
 *
 * An Entry stores the address of its Archive and an index. It is invalidated
 * when the Archive is closed, discarded or **moved**; re-obtain it from the
 * Archive afterwards. See @ref lifetime.
 */
class ZIPWRAPPER_API Entry {
public:
    /** @brief Construct an invalid entry; using it throws Exception. */
    Entry() = default;
    /**
     * @brief Construct a handle (normally done by Archive::entry()).
     * @param[in] a The archive the entry belongs to; must outlive the handle.
     * @param[in] i Index of the entry.
     */
    Entry(Archive* a, std::size_t i) : archive_(a), index_(i) {}

    /** @brief Index of this entry in its archive. @return The index. */
    [[nodiscard]] std::size_t index() const noexcept { return index_; }

    /**
     * @brief Read this entry's metadata.
     * @return A snapshot of the metadata.
     * @throws Exception if the entry is invalid, the archive is closed, or the
     *         entry was deleted (`ZIP_ER_DELETED`).
     */
    [[nodiscard]] Stat stat() const;
    /** @brief Entry name (see Stat::name). @return The name. @throws Exception as stat(). */
    [[nodiscard]] std::string name() const { return stat().name; }
    /** @brief Uncompressed size. @return Size in bytes. @throws Exception as stat(). */
    [[nodiscard]] std::uint64_t size() const { return stat().size; }
    /** @brief Stored size. @return Size in bytes. @throws Exception as stat(). */
    [[nodiscard]] std::uint64_t compressedSize() const { return stat().compressedSize; }
    /** @brief Whether this is a directory entry. @return `true` for directories. @throws Exception as stat(). */
    [[nodiscard]] bool isDirectory() const { return stat().directory; }

    /**
     * @brief Open the entry for streaming reads.
     * @return The opened file.
     * @throws Exception `ZIP_ER_NOPASSWD` if the entry is encrypted (use
     *         openEncrypted() or Archive::setDefaultPassword()), or
     *         `ZIP_ER_COMPNOTSUPP` if its compression method is not available.
     */
    [[nodiscard]] File open() const;
    /**
     * @brief Open an encrypted entry for streaming reads.
     * @param[in] password The password; ignored by entries that are not encrypted.
     * @return The opened file.
     * @throws Exception `ZIP_ER_WRONGPASSWD` for a wrong password, or
     *         `ZIP_ER_ENCRNOTSUPP` if the encryption method is not available.
     */
    [[nodiscard]] File openEncrypted(std::string_view password) const;
    /**
     * @brief Read the whole (unencrypted) entry into memory.
     * @return The uncompressed bytes.
     * @throws Exception as open(), or on a read error.
     */
    [[nodiscard]] std::vector<std::byte> readAll() const;

    /**
     * @brief Extract this entry below a destination directory.
     *
     * With ExtractOptions::rejectUnsafePaths (the default) the entry is refused
     * before anything is created when its name is absolute, contains `..`, or
     * resolves, through existing symlinks, to a location outside @p destination.
     * Missing parent directories are created if ExtractOptions::createDirectories
     * is set. If writing fails the partial file is removed.
     *
     * @param[in] destination Directory to extract into.
     * @param[in] options     Extraction policy.
     * @throws Exception `ZIP_ER_INVAL` for an unsafe path, `ZIP_ER_EXISTS` if the
     *         target exists and ExtractOptions::overwrite is not set,
     *         `ZIP_ER_NOPASSWD` / `ZIP_ER_WRONGPASSWD` for encrypted entries,
     *         `ZIP_ER_WRITE` if writing fails.
     * @throws std::filesystem::filesystem_error for filesystem failures.
     */
    void extract(const std::filesystem::path& destination,
                 ExtractOptions options = {}) const;

private:
    Archive* archive_{nullptr};
    std::size_t index_{0};
};

/**
 * @brief RAII owner of an open archive.
 * @ingroup zip_core
 *
 * Archive is movable but not copyable. All changes made through it (add,
 * replace, rename, remove, setCompression, ...) are only recorded; they are
 * committed by close(). **Destroying an Archive that was not closed discards
 * every uncommitted change**, mirroring `zip_discard()`. See @ref guide_core.
 *
 * @code
 * auto archive = Zip::Archive::create("out.zip");
 * archive.addFile("readme.txt", "readme.txt");
 * archive.close();                       // commits; skip it and nothing is written
 * @endcode
 */
class ZIPWRAPPER_API Archive {
public:
    /** @brief Archives are created with open() or create(), never default-constructed. */
    Archive() = delete;

    /** @brief Discard any uncommitted changes and release the archive. Never throws. */
    ~Archive();

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    /**
     * @brief Move constructor: take over another archive.
     *
     * Entry handles and iterators obtained from @p o are invalidated.
     *
     * @param[in,out] o The archive to take over; it is left closed.
     */
    Archive(Archive&& o) noexcept;

    /**
     * @brief Move assignment: discard the current archive, then take over another.
     *
     * Uncommitted changes of the current archive are lost. Entry handles and
     * iterators obtained from @p o are invalidated.
     *
     * @param[in,out] o The archive to take over; it is left closed.
     * @return This object.
     */
    Archive& operator=(Archive&& o) noexcept;

    /**
     * @brief Open an existing archive.
     * @param[in] path  Archive file.
     * @param[in] flags OpenFlag::ReadOnly for read-only access; the default
     *                  OpenFlag::None opens for reading and writing.
     * @return The opened archive.
     * @throws Exception `ZIP_ER_NOENT` if the file does not exist, `ZIP_ER_OPEN`
     *         or `ZIP_ER_NOZIP` if it cannot be opened or is not a ZIP file.
     */
    [[nodiscard]] static Archive open(const std::filesystem::path& path,
                                      OpenFlag flags = OpenFlag::None);

    /**
     * @brief Create a new archive, replacing any existing file at @p path.
     *
     * Nothing is touched on disk until close(): an existing file stays intact if
     * the archive is discarded or if close() fails or is cancelled.
     *
     * @param[in] path Archive file to create.
     * @return The new, empty archive.
     * @throws Exception if the archive cannot be created.
     */
    [[nodiscard]] static Archive create(const std::filesystem::path& path);

    /**
     * @brief Number of entries, including newly added ones.
     *
     * Entries removed with remove() are still counted until close(); stat() on
     * them throws `ZIP_ER_DELETED`.
     *
     * @return The entry count.
     * @throws Exception if the archive is closed.
     */
    [[nodiscard]] std::size_t size() const;

    /** @brief Whether the archive has no entries. @return `true` if size() is 0. @throws Exception if the archive is closed. */
    [[nodiscard]] bool empty() const { return size() == 0; }

    /**
     * @brief Locate an entry by name.
     * @param[in] name Entry name, UTF-8, '/' separated; directories end with '/'.
     * @return The entry index, or `std::nullopt` if there is no such entry.
     * @throws Exception on a libzip error other than "not found".
     */
    [[nodiscard]] std::optional<std::size_t> find(std::string_view name) const;

    /** @brief Whether an entry with this name exists. @param[in] name Entry name. @return `true` if found. @throws Exception as find(). */
    [[nodiscard]] bool contains(std::string_view name) const {
        return find(name).has_value();
    }

    /**
     * @brief Get an entry handle by index.
     * @param[in] index Index in `[0, size())`.
     * @return The handle.
     * @throws Exception `ZIP_ER_INVAL` if @p index is out of range.
     */
    [[nodiscard]] Entry entry(std::size_t index);

    /**
     * @brief Get an entry handle by name.
     * @param[in] name Entry name.
     * @return The handle.
     * @throws Exception `ZIP_ER_NOENT` if there is no such entry.
     */
    [[nodiscard]] Entry entry(std::string_view name);

    /**
     * @brief Add a memory buffer as a new entry.
     *
     * The bytes are copied, so @p data need not outlive the call.
     *
     * @param[in] name      Entry name.
     * @param[in] data      Contents; may be empty.
     * @param[in] overwrite Replace an existing entry of that name instead of failing.
     * @return Index of the new entry.
     * @throws Exception `ZIP_ER_EXISTS` if the name exists and @p overwrite is `false`.
     */
    std::size_t add(std::string_view name, std::span<const std::byte> data,
                    bool overwrite = false);

    /**
     * @brief Add a filesystem file as a new entry.
     *
     * The file is read when the archive is closed, so a missing or unreadable
     * file is reported by close(), not here. Its modification time is kept.
     *
     * @param[in] name      Entry name.
     * @param[in] file      File to read.
     * @param[in] overwrite Replace an existing entry of that name instead of failing.
     * @return Index of the new entry.
     * @throws Exception `ZIP_ER_EXISTS` if the name exists and @p overwrite is `false`.
     */
    std::size_t addFile(std::string_view name,
                        const std::filesystem::path& file,
                        bool overwrite = false);

    /**
     * @brief Add a prepared Source as a new entry.
     *
     * @param[in] name      Entry name.
     * @param[in] source    The source; ownership passes to libzip on success.
     * @param[in] overwrite Replace an existing entry of that name instead of failing.
     * @return Index of the new entry.
     * @throws Exception `ZIP_ER_EXISTS` if the name exists and @p overwrite is `false`.
     */
    std::size_t add(std::string_view name, Source&& source,
                    bool overwrite = false);

    /**
     * @brief Replace the contents of an existing entry with a memory buffer.
     * @param[in] index Entry to replace.
     * @param[in] data  New contents (copied).
     * @throws Exception if @p index is invalid.
     */
    void replace(std::size_t index, std::span<const std::byte> data);

    /**
     * @brief Replace the contents of an existing entry with a filesystem file.
     * @param[in] index Entry to replace.
     * @param[in] file  File to read when the archive is closed.
     * @throws Exception if @p index is invalid.
     */
    void replaceFile(std::size_t index, const std::filesystem::path& file);

    /**
     * @brief Delete an entry.
     *
     * The entry stays counted by size() until close(); do not iterate the archive
     * afterwards, collect the indices first (see @ref pitfalls).
     *
     * @param[in] index Entry to delete.
     * @throws Exception if @p index is invalid.
     */
    void remove(std::size_t index);

    /**
     * @brief Rename an entry.
     * @param[in] index   Entry to rename.
     * @param[in] newName The new name.
     * @throws Exception if @p index is invalid or the name is already in use.
     */
    void rename(std::size_t index, std::string_view newName);

    /**
     * @brief Add a directory entry.
     * @param[in] name Directory name; a trailing '/' is added if missing.
     * @throws Exception if the name is already in use.
     */
    void mkdir(std::string_view name);

    /**
     * @brief Choose the compression method and level for an entry.
     * @param[in] index  Entry to configure.
     * @param[in] method The method.
     * @param[in] level  0 = the method's default, 1 (fastest) to 9 (smallest).
     * @throws Exception `ZIP_ER_INVAL` if @p level is above 9,
     *         `ZIP_ER_COMPNOTSUPP` if @p method is not available in this libzip.
     */
    void setCompression(std::size_t index, Compression method,
                        unsigned level = 0);

    /**
     * @brief Encrypt (or stop encrypting) an entry.
     *
     * The password is stored by libzip until close(). Use Encryption::None to
     * remove encryption; the password is ignored in that case.
     *
     * @param[in] index    Entry to protect.
     * @param[in] method   The encryption method.
     * @param[in] password The password.
     * @throws Exception `ZIP_ER_ENCRNOTSUPP` if @p method is not available in this libzip.
     */
    void setEncryption(std::size_t index, Encryption method,
                       std::string_view password);

    /**
     * @brief Test whether a compression method is available.
     * @param[in] method   The method to test.
     * @param[in] compress `true` to ask whether it can be written, `false` whether it can be read.
     * @return `true` if this libzip supports it.
     */
    [[nodiscard]] static bool compressionSupported(Compression method,
                                                   bool compress = true);

    /**
     * @brief Test whether an encryption method is available.
     * @param[in] method  The method to test.
     * @param[in] encrypt `true` to ask whether it can be written, `false` whether it can be read.
     * @return `true` if this libzip supports it.
     */
    [[nodiscard]] static bool encryptionSupported(Encryption method,
                                                  bool encrypt = true);

    /**
     * @brief List the compression methods this libzip build supports.
     *
     * Store and Deflate are always present; BZip2, Lzma and Zstd depend on how
     * libzip was configured.
     *
     * @param[in] compress `true` for methods it can write, `false` for methods it can read.
     * @return The supported methods.
     */
    [[nodiscard]] static std::vector<Compression> supportedCompressions(
        bool compress = true);

    /**
     * @brief List the encryption methods this libzip build supports.
     *
     * The AES methods need a crypto backend; traditional PKWARE encryption does not.
     *
     * @param[in] encrypt `true` for methods it can write, `false` for methods it can read.
     * @return The supported methods.
     */
    [[nodiscard]] static std::vector<Encryption> supportedEncryptions(
        bool encrypt = true);

    /**
     * @brief Version of the libzip loaded at run time.
     * @return The version string, for example `"1.11.4"`.
     */
    [[nodiscard]] static std::string_view libzipVersion() noexcept;

    /**
     * @brief Open an entry for streaming reads, by index.
     * @param[in] index Entry to open.
     * @return The opened file.
     * @throws Exception as Entry::open().
     */
    [[nodiscard]] File openEntry(std::size_t index);

    /**
     * @brief Open an entry for streaming reads, by name.
     * @param[in] name Entry to open.
     * @return The opened file.
     * @throws Exception `ZIP_ER_NOENT` if there is no such entry; otherwise as Entry::open().
     */
    [[nodiscard]] File openEntry(std::string_view name);

    /**
     * @brief Open an encrypted entry for streaming reads.
     * @param[in] index    Entry to open.
     * @param[in] password The password; ignored by entries that are not encrypted.
     * @return The opened file.
     * @throws Exception as Entry::openEncrypted().
     */
    [[nodiscard]] File openEntryEncrypted(std::size_t index,
                                          std::string_view password);

    /**
     * @brief Extract every entry below a destination directory.
     *
     * Stops at the first entry that fails and keeps what was already extracted.
     * To carry on after errors, loop over the entries yourself or use
     * Zip::extractArchive() with UnpackOptions::continueOnError.
     *
     * @param[in] destination Directory to extract into.
     * @param[in] options     Extraction policy.
     * @throws Exception as Entry::extract().
     */
    void extractAll(const std::filesystem::path& destination,
                    ExtractOptions options = {});

    /**
     * @brief Copy an entry from another archive.
     *
     * The data is streamed when this archive is closed, so @p source must stay
     * open until then. If the source entry is encrypted, call
     * `source.setDefaultPassword()` first.
     *
     * @param[in] source          Archive to copy from.
     * @param[in] sourceIndex     Index of the entry in @p source.
     * @param[in] destinationName Name of the new entry.
     * @param[in] overwrite       Replace an existing entry of that name instead of failing.
     * @return Index of the new entry in this archive.
     * @throws Exception `ZIP_ER_EXISTS` if the name exists and @p overwrite is `false`,
     *         or if @p sourceIndex is invalid.
     */
    std::size_t copyFrom(Archive& source, std::size_t sourceIndex,
                         std::string_view destinationName,
                         bool overwrite = false);

    /**
     * @brief Commit all changes and close the archive.
     *
     * libzip writes a temporary file and renames it over the target at the end, so
     * an existing archive is never left half-written. If this throws, the archive
     * stays open and is discarded by the destructor.
     *
     * @throws Exception on any write failure, for example `ZIP_ER_READ` when an
     *         addFile() source is missing, or `ZIP_ER_CANCELLED` when the callback
     *         set with setProgressCallback() returned `false`. An exception thrown
     *         by that callback is rethrown unchanged.
     */
    void close();

    /**
     * @brief Observe (and optionally cancel) the work done by close().
     *
     * libzip reads the sources and writes the archive during close(), so
     * that is where the time is spent and where the callback fires.
     *
     * @param callback  Receives the completed fraction in [0, 1]. Return
     *                  false to cancel: close() then throws Exception with
     *                  ZIP_ER_CANCELLED and the previous file, if any, is
     *                  left untouched. An exception thrown by the callback
     *                  also cancels, and is rethrown by close().
     * @param precision Minimum change of the fraction between two calls.
     *
     * Pass an empty function to remove the callback. The callback runs on
     * the thread that calls close().
     */
    void setProgressCallback(std::function<bool(double)> callback,
                             double precision = 0.01);

    /**
     * @brief Set the password libzip uses for encrypted entries when none is
     *        given explicitly.
     *
     * After this call open(), readAll(), copyFrom() and extraction read
     * encrypted entries transparently. It affects reading only; use
     * setEncryption() to encrypt entries being written.
     *
     * @param[in] password The password; an empty string clears it.
     * @throws Exception if the archive is closed.
     */
    void setDefaultPassword(std::string_view password);

    /**
     * @brief Throw away all uncommitted changes and close the archive.
     *
     * Never throws; safe to call on an already closed archive.
     */
    void discard() noexcept;

    /**
     * @brief Access the raw libzip handle for calls this wrapper does not cover.
     * @return The `zip_t*`, or null once the archive is closed. Ownership stays
     *         with this object.
     */
    [[nodiscard]] zip_t* native() noexcept { return archive_; }
    /** @brief Path the archive was opened or created with. @return The path. */
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    /**
     * @brief Input iterator over the entries of an Archive.
     *
     * Dereferencing yields an Entry **by value**, which makes this an input
     * iterator, not a forward iterator. Use it with range-for:
     *
     * @code
     * for (const auto& entry : archive)
     *     std::cout << entry.name() << '\n';
     * @endcode
     *
     * Do not iterate an archive after remove(): the deleted entry still counts
     * and Entry::stat() throws `ZIP_ER_DELETED`.
     */
    class Iterator {
    public:
        /// Iterator category: input iterator.
        using iterator_category = std::input_iterator_tag;
        /// C++20 iterator concept: input iterator.
        using iterator_concept  = std::input_iterator_tag;
        /// Type obtained by dereferencing: Entry.
        using value_type        = Entry;
        /// Type of the distance between iterators.
        using difference_type   = std::ptrdiff_t;
        /// Dereferencing returns by value.
        using reference         = Entry;
        /// No pointer type: Entry is returned by value.
        using pointer           = void;

        /** @brief Construct a singular iterator; only assignment is valid on it. */
        Iterator() = default;
        /**
         * @brief Construct an iterator positioned at an entry (use Archive::begin()).
         * @param[in] a Archive to iterate; must outlive the iterator.
         * @param[in] i Index to start at.
         */
        Iterator(Archive* a, std::size_t i) : archive_(a), index_(i) {}

        /** @brief Entry at the current position. @return A handle by value. @throws Exception if the position is out of range. */
        Entry operator*() const { return archive_->entry(index_); }
        /** @brief Advance to the next entry. @return This iterator. */
        Iterator& operator++() { ++index_; return *this; }
        /** @brief Post-increment (returns nothing, as for input iterators). */
        void operator++(int) { ++index_; }

        /** @brief Iterators are equal when they refer to the same archive and index. */
        friend bool operator==(const Iterator&, const Iterator&) = default;

    private:
        Archive* archive_{nullptr};
        std::size_t index_{0};
    };

    /** @brief Iterator to the first entry. @return The iterator. */
    [[nodiscard]] Iterator begin() { return Iterator(this, 0); }
    /** @brief Iterator past the last entry. @return The iterator. @throws Exception if the archive is closed. */
    [[nodiscard]] Iterator end() { return Iterator(this, size()); }

private:
    Archive(const std::filesystem::path& path, int flags);

    void ensureOpen() const;

    std::size_t addSource(std::string_view name, Source& source, bool overwrite);

    void replaceSource(std::size_t index, Source& source);

    void discardNoThrow() noexcept;

    zip_t* archive_{nullptr};
    std::filesystem::path path_;
    bool closed_{false};
    std::unique_ptr<detail::CallbackState> callbacks_;

    friend class Entry;
};

} // namespace Zip

ZIPWRAPPER_DISABLE_DLL_WARNINGS_END
