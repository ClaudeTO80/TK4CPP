// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file HighLevel.h
 * @brief High-level, task-oriented API on top of Zip.h.
 *
 * Where Zip.h mirrors libzip (archives, entries, sources), this header offers
 * the operations applications actually need, each as a single call:
 *
 *  - archiveDirectory() / archiveFiles(): recursive archiving with
 *    include/exclude patterns, deterministic order, compression policy,
 *    optional password protection, progress/cancel and post-write check.
 *  - listEntries() / listFiles() / summarize() / formatListing(): file lists.
 *  - readBytes() / readText(): read one member, transparently decrypting it.
 *  - extractArchive(): filtered, safe extraction with a per-entry report.
 *  - verifyArchive(): streaming integrity check.
 *  - changeProtection(): add, change or remove the password of an archive.
 *
 * @section hl_password Transparent password handling
 *
 * Pass the password once, in the options. Write functions encrypt with it;
 * read functions use it only for entries that are actually encrypted, so the
 * same code path works for plain and protected archives. Reading an
 * encrypted entry without a password fails with ZIP_ER_NOPASSWD instead of
 * returning garbage.
 *
 * @section hl_security Security notes (read these)
 *
 *  - Only WinZip AES (128/192/256) is offered. Traditional PKWARE ("ZipCrypto")
 *    is deliberately not exposed: it is known to be weak.
 *  - If the requested AES method is not available in the libzip build, the
 *    call fails with ZIP_ER_ENCRNOTSUPP **before** anything is written. It
 *    never silently falls back to a weaker method or to no encryption.
 *  - ZIP encryption protects the *contents* only. File names, sizes,
 *    timestamps and the number of entries stay visible to anyone holding the
 *    archive, and the archive structure is not authenticated. If names are
 *    sensitive, put everything in one inner archive or use a different
 *    container format.
 *  - Passwords live in ordinary heap memory (yours, ours and libzip's) and
 *    are not wiped after use. Prefer passwordFromEnv() or a secret store over
 *    command-line arguments, which other users can see.
 *  - Extraction never disables the path-traversal / symlink protection of
 *    Entry::extract(); there is no option to do so at this level.
 *
 * @section hl_patterns Patterns
 *
 * Patterns are used by PathFilter and use '/' separated, archive-style paths
 * relative to the archived (or extracted) root:
 *  - `*` matches any run of characters inside one path component, `?` one
 *    character, `**` any number of whole components (including none).
 *  - A pattern without '/' matches a single component: for `exclude` any
 *    component of the path (so `node_modules` excludes the whole subtree, like
 *    .gitignore); for `include` the file name (so `*.cpp` finds sources at any
 *    depth).
 *  - A pattern with '/' is matched against the whole relative path
 *    (for example `docs/&#42;&#42;` or `src/&#42;&#42;/&#42;.cpp`, where each
 *    `&#42;&#42;` stands for any number of directories); for `exclude` it also
 *    matches every directory prefix, so `build/cache` excludes everything
 *    below it.
 *  - A leading "./" or "/" and a trailing "/" are ignored. Matching is
 *    case-sensitive.
 *  - When `include` is non-empty only matching *files* are selected, and
 *    directory entries are not written (parent directories are implied by the
 *    file names and re-created on extraction).
 *
 * @par License
 * Released under the MIT License; see the `LICENSE` file at the root of the repository.
 */

#pragma once

#include <ZipWrapper/Zip.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

ZIPWRAPPER_DISABLE_DLL_WARNINGS_BEGIN

namespace Zip {

// ---------------------------------------------------------------------------
// Pattern matching
// ---------------------------------------------------------------------------

/**
 * @brief Include/exclude selection shared by archiving, listing and extraction.
 * @ingroup zip_hl_filters
 *
 * Patterns are matched against '/'-separated paths relative to the archived
 * (or extracted) root. See @ref patterns for the exact rules with examples.
 *
 * @code
 * Zip::PathFilter f;
 * f.exclude = {".git", "node_modules", "*.tmp"};
 * f.include = {"src/&#42;&#42;/&#42;.cpp"};   // each pair of stars = any number of directories
 * bool take = f.accepts("src/util/helper.cpp", false);   // true
 * @endcode
 */
struct ZIPWRAPPER_API PathFilter {
    /// Only files matching one of these are selected (empty = all files).
    std::vector<std::string> include;
    /// Paths matching one of these are dropped, with their whole subtree.
    std::vector<std::string> exclude;

    /**
     * @brief Test whether a path (or one of its parent directories) is excluded.
     * @param[in] relPath Path relative to the root, '/' separated.
     * @return `true` if any `exclude` pattern matches.
     */
    [[nodiscard]] bool excludes(std::string_view relPath) const;

    /**
     * @brief Test whether an entry passes the `include` list.
     * @param[in] relPath     Path relative to the root, '/' separated.
     * @param[in] isDirectory Whether the path is a directory entry.
     * @return `true` if `include` is empty, or if the path is a file matching one
     *         of the patterns. Directories never match a non-empty `include` list.
     */
    [[nodiscard]] bool includes(std::string_view relPath, bool isDirectory) const;

    /**
     * @brief Combine excludes() and includes().
     * @param[in] relPath     Path relative to the root, '/' separated.
     * @param[in] isDirectory Whether the path is a directory entry.
     * @return `true` if the entry is not excluded and passes the include list.
     */
    [[nodiscard]] bool accepts(std::string_view relPath, bool isDirectory) const;
};

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

/**
 * @brief Metadata of one archive member, detached from the Archive.
 * @ingroup zip_hl_list
 */
struct EntryInfo {
    std::string name;  ///< Entry name: UTF-8, '/' separated; directories end with '/'.
    std::uint64_t size{};  ///< Uncompressed size in bytes.
    std::uint64_t packedSize{};  ///< Stored size in bytes (includes the AES overhead when encrypted).
    std::int64_t modified{};  ///< Modification time, Unix seconds.
    std::uint32_t crc{};  ///< CRC-32 of the data; 0 for AES (AE-2) entries.
    std::uint16_t method{};  ///< The `ZIP_CM_*` compression method.
    std::uint16_t encryption{};  ///< The `ZIP_EM_*` encryption method.
    bool directory{};  ///< `true` for a directory entry.
    bool encrypted{};  ///< `true` if the entry needs a password to be read.
};

/**
 * @brief Selection for listEntries().
 * @ingroup zip_hl_list
 */
struct ListOptions {
    PathFilter filter;  ///< Which entries to list.
    bool filesOnly = false;  ///< Skip directory entries.
};

/**
 * @brief Totals for a list of entries, from summarize().
 * @ingroup zip_hl_list
 */
struct ArchiveSummary {
    std::size_t entries = 0;  ///< Number of entries, files and directories.
    std::size_t files = 0;  ///< Number of file entries.
    std::size_t directories = 0;  ///< Number of directory entries.
    std::size_t encryptedEntries = 0;  ///< Number of entries that need a password.
    std::uint64_t totalSize = 0;  ///< Sum of the uncompressed sizes in bytes.
    std::uint64_t packedSize = 0;  ///< Sum of the stored sizes in bytes.

    /**
     * @brief Whether at least one entry needs a password to be read.
     * @return `true` if encryptedEntries is greater than zero.
     */
    [[nodiscard]] bool needsPassword() const noexcept { return encryptedEntries > 0; }
};

/**
 * @brief List the members of an open archive.
 * @ingroup zip_hl_list
 *
 * Never needs a password: names, sizes and the encryption flag are readable
 * without it.
 *
 * @param[in,out] archive The archive to list.
 * @param[in]     options Which entries to include.
 * @return The matching entries, in archive order.
 * @throws Exception if the archive is closed.
 */
[[nodiscard]] ZIPWRAPPER_API std::vector<EntryInfo> listEntries(
    Archive& archive, const ListOptions& options = {});

/**
 * @brief List the members of the archive at a path.
 * @ingroup zip_hl_list
 * @param[in] path    Archive file.
 * @param[in] options Which entries to include.
 * @return The matching entries, in archive order.
 * @throws Exception if the archive cannot be opened (`ZIP_ER_NOENT`, `ZIP_ER_NOZIP`, ...).
 */
[[nodiscard]] ZIPWRAPPER_API std::vector<EntryInfo> listEntries(
    const std::filesystem::path& path, const ListOptions& options = {});

/**
 * @brief Shortcut for listing files only, optionally matching one pattern.
 * @ingroup zip_hl_list
 * @param[in] path           Archive file.
 * @param[in] includePattern Include pattern (see @ref patterns); empty for all files.
 * @return The matching file entries, in archive order.
 * @throws Exception if the archive cannot be opened.
 */
[[nodiscard]] ZIPWRAPPER_API std::vector<EntryInfo> listFiles(
    const std::filesystem::path& path, std::string_view includePattern = {});

/**
 * @brief Compute totals for a list of entries.
 * @ingroup zip_hl_list
 * @param[in] entries Entries from listEntries().
 * @return The counts and size totals.
 */
[[nodiscard]] ZIPWRAPPER_API ArchiveSummary summarize(std::span<const EntryInfo> entries);

/**
 * @brief Compute totals for the archive at a path.
 * @ingroup zip_hl_list
 * @param[in] path Archive file.
 * @return The counts and size totals.
 * @throws Exception if the archive cannot be opened.
 */
[[nodiscard]] ZIPWRAPPER_API ArchiveSummary summarize(const std::filesystem::path& path);

/**
 * @brief Render entries as an `unzip -l` style table.
 * @ingroup zip_hl_list
 *
 * Columns: size, packed size, space saved, method, modification time, name.
 * Encrypted entries are marked with `*`. A totals line follows the table.
 *
 * @param[in] entries Entries from listEntries().
 * @return The formatted table, ending with a newline.
 */
[[nodiscard]] ZIPWRAPPER_API std::string formatListing(std::span<const EntryInfo> entries);

// ---------------------------------------------------------------------------
// Reading one member
// ---------------------------------------------------------------------------

/**
 * @brief Read one member into memory, decrypting it transparently.
 * @ingroup zip_hl_read
 *
 * The password is used only if the entry is encrypted, so the same call works
 * on plain and protected archives.
 *
 * @param[in,out] archive  An open archive.
 * @param[in]     name     Entry name.
 * @param[in]     password Password for encrypted entries; may be left unset.
 * @return The uncompressed bytes.
 * @throws Exception `ZIP_ER_NOENT` if there is no such entry,
 *         `ZIP_ER_NOPASSWD` if it is encrypted and no password was given,
 *         `ZIP_ER_WRONGPASSWD` for a wrong password, or a read / CRC /
 *         authentication error.
 */
[[nodiscard]] ZIPWRAPPER_API std::vector<std::byte> readBytes(
    Archive& archive, std::string_view name,
    const std::optional<std::string>& password = std::nullopt);

/**
 * @brief Read one member of the archive at a path into memory.
 * @ingroup zip_hl_read
 * @param[in] path     Archive file.
 * @param[in] name     Entry name.
 * @param[in] password Password for encrypted entries; may be left unset.
 * @return The uncompressed bytes.
 * @throws Exception as the overload taking an open archive, or if the archive cannot be opened.
 */
[[nodiscard]] ZIPWRAPPER_API std::vector<std::byte> readBytes(
    const std::filesystem::path& path, std::string_view name,
    const std::optional<std::string>& password = std::nullopt);

/**
 * @brief Read one member as text.
 * @ingroup zip_hl_read
 *
 * The bytes are returned as they are; no character-set conversion is done.
 *
 * @param[in] path     Archive file.
 * @param[in] name     Entry name.
 * @param[in] password Password for encrypted entries; may be left unset.
 * @return The member contents.
 * @throws Exception as readBytes().
 */
[[nodiscard]] ZIPWRAPPER_API std::string readText(
    const std::filesystem::path& path, std::string_view name,
    const std::optional<std::string>& password = std::nullopt);

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

/**
 * @brief Result of verifyArchive().
 * @ingroup zip_hl_verify
 */
struct VerifyReport {
    std::size_t checked = 0;  ///< Entries verified successfully.
    std::size_t skipped = 0;  ///< Encrypted entries that were not checked because no password was given.
    std::vector<std::string> problems;  ///< "name: reason" for every entry that failed.

    /**
     * @brief Whether nothing is wrong.
     * @return `true` if problems is empty. Check skipped to know what was not covered.
     */
    [[nodiscard]] bool ok() const noexcept { return problems.empty(); }
};

/**
 * @brief Stream every member and compare size and CRC-32 with the archive.
 * @ingroup zip_hl_verify
 *
 * WinZip AES (AE-2) entries store no CRC; their integrity is guaranteed by an
 * authentication code that libzip checks while reading, so for those only the
 * size is compared. Encrypted entries are skipped, and counted in
 * VerifyReport::skipped, when no password is given; they are never reported as
 * verified.
 *
 * @param[in,out] archive  An open archive.
 * @param[in]     password Password for encrypted entries; may be left unset.
 * @return The report; per-entry failures are collected, not thrown.
 */
[[nodiscard]] ZIPWRAPPER_API VerifyReport verifyArchive(
    Archive& archive, const std::optional<std::string>& password = std::nullopt);

/**
 * @brief Verify the archive at a path.
 * @ingroup zip_hl_verify
 * @param[in] path     Archive file.
 * @param[in] password Password for encrypted entries; may be left unset.
 * @return The report.
 * @throws Exception if the archive cannot be opened.
 */
[[nodiscard]] ZIPWRAPPER_API VerifyReport verifyArchive(
    const std::filesystem::path& path,
    const std::optional<std::string>& password = std::nullopt);

// ---------------------------------------------------------------------------
// Archiving
// ---------------------------------------------------------------------------

/**
 * @brief Settings for archiveDirectory() and archiveFiles().
 * @ingroup zip_hl_archive
 *
 * The defaults archive everything below the directory, compress with Deflate
 * level 6, store already-compressed formats, add no password and refuse to
 * overwrite an existing output.
 */
struct ArchiveOptions {
    /// Which files and directories to archive.
    PathFilter filter;
    /// Prepended to every entry name, e.g. "project/" (a '/' is added if missing).
    std::string prefix;

    /// If set, files are encrypted with this password (see @ref hl_security).
    std::optional<std::string> password;
    /// AES128, AES192 or AES256. Anything else is rejected.
    Encryption encryption = Encryption::AES256;
    /// Encrypt only files matching these patterns (empty = every file).
    std::vector<std::string> encryptOnly;

    /// Preferred method. Falls back to Deflate, with a warning, if this
    /// libzip build lacks it. (Unlike encryption, this fallback is harmless.)
    Compression compression = Compression::Deflate;
    /// 0 = method default, 1 (fastest) .. 9 (best).
    unsigned level = 6;
    /// Extensions stored without compression (already-compressed formats).
    std::vector<std::string> storeExtensions{
        ".zip", ".jpg", ".jpeg", ".png", ".gif", ".webp", ".mp3", ".mp4",
        ".mkv", ".gz", ".xz", ".zst", ".7z", ".rar"};

    /// Replace an existing output file. Default: refuse, never destroy silently.
    bool overwriteOutput = false;
    /// Re-read the finished archive and check every entry (this costs one more
    /// read of the archive). The archive is written to "<output>.partial" and
    /// moved into place only if the check passes; otherwise ZIP_ER_CRC is
    /// thrown and an existing output is left untouched.
    bool verifyAfterWrite = false;
    /// Called during the final write with the completed fraction in [0, 1];
    /// return false to cancel (ZIP_ER_CANCELLED, output left untouched).
    std::function<bool(double)> progress;
};

/**
 * @brief What archiveDirectory() or archiveFiles() did.
 * @ingroup zip_hl_archive
 */
struct ArchiveReport {
    std::size_t files = 0;  ///< Number of files added.
    std::size_t directories = 0;  ///< Number of directory entries added.
    std::size_t encrypted = 0;  ///< Number of files that were encrypted.
    std::uint64_t inputBytes = 0;  ///< Total size of the files added, in bytes.
    std::uint64_t archiveBytes = 0;  ///< Size of the finished archive, in bytes.
    /// "name: reason" for things left out that the caller might not expect
    /// (symlinks, special files). Entries removed by the filter are not listed.
    std::vector<std::string> skipped;
    std::vector<std::string> warnings;  ///< Notices, for example that a requested compression method was unavailable and Deflate was used.
};

/**
 * @brief Archive a directory tree recursively.
 * @ingroup zip_hl_archive
 *
 * Entry names are relative to @p directory. Symbolic links and special files
 * are skipped and listed in ArchiveReport::skipped. The output file is never
 * archived into itself, even when it lives inside @p directory. If the
 * operation fails or is cancelled, an existing @p output is left untouched
 * (libzip writes to a temporary file and renames it at the end).
 *
 * @param[in] directory Directory to archive.
 * @param[in] output    Archive file to write.
 * @param[in] options   Filters, compression, protection, progress.
 * @return A report of what was archived.
 * @throws Exception `ZIP_ER_NOENT` / `ZIP_ER_INVAL` for a missing or
 *         non-directory @p directory; `ZIP_ER_EXISTS` if @p output exists and
 *         ArchiveOptions::overwriteOutput is `false`; `ZIP_ER_ENCRNOTSUPP` or
 *         `ZIP_ER_INVAL` for unusable password settings (checked before
 *         anything is written); `ZIP_ER_CANCELLED` if the progress callback
 *         returned `false`; `ZIP_ER_CRC` if ArchiveOptions::verifyAfterWrite
 *         found a problem; or any error from closing the archive.
 */
ZIPWRAPPER_API ArchiveReport archiveDirectory(const std::filesystem::path& directory,
                                      const std::filesystem::path& output,
                                      const ArchiveOptions& options = {});

/**
 * @brief Archive an explicit list of files and/or directories.
 * @ingroup zip_hl_archive
 *
 * Directories are archived recursively. Entry names are relative to
 * @p base, so every path must lie below it. Duplicates are added once.
 *
 * @param[in] paths   Files and directories to archive.
 * @param[in] base    Directory the entry names are relative to.
 * @param[in] output  Archive file to write.
 * @param[in] options Filters, compression, protection, progress.
 * @return A report of what was archived.
 * @throws Exception `ZIP_ER_NOENT` if a path does not exist, `ZIP_ER_INVAL`
 *         if a path is outside @p base; otherwise as archiveDirectory().
 */
ZIPWRAPPER_API ArchiveReport archiveFiles(std::span<const std::filesystem::path> paths,
                                  const std::filesystem::path& base,
                                  const std::filesystem::path& output,
                                  const ArchiveOptions& options = {});

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------

/**
 * @brief Settings for extractArchive().
 * @ingroup zip_hl_extract
 *
 * Extraction always runs with path-traversal, absolute-path and symlink-escape
 * protection; there is deliberately no option to switch it off.
 */
struct UnpackOptions {
    PathFilter filter;  ///< Which entries to extract.
    bool overwrite = false;  ///< Replace existing files instead of failing with `ZIP_ER_EXISTS`.
    /// Used only for entries that are encrypted.
    std::optional<std::string> password;
    /// false: rethrow the first error. true: record it and go on.
    bool continueOnError = false;
    /// (done, total, current entry); return false to stop (ZIP_ER_CANCELLED).
    std::function<bool(std::size_t, std::size_t, std::string_view)> progress;
};

/**
 * @brief One entry that could not be extracted.
 * @ingroup zip_hl_extract
 */
struct EntryFailure {
    std::string name;  ///< Entry name.
    int zipCode = -1;  ///< libzip code (`ZIP_ER_*`), or -1 for filesystem errors.
    std::string message;  ///< Human-readable reason.
};

/**
 * @brief What extractArchive() did.
 * @ingroup zip_hl_extract
 */
struct UnpackReport {
    std::size_t extracted = 0;  ///< Entries extracted successfully.
    std::size_t filtered = 0;  ///< Entries left out by the filter.
    std::vector<EntryFailure> failures;  ///< Entries that failed (only filled when `continueOnError` is set).

    /**
     * @brief Whether every selected entry was extracted.
     * @return `true` if failures is empty.
     */
    [[nodiscard]] bool ok() const noexcept { return failures.empty(); }
};

/**
 * @brief Extract an archive below a destination directory.
 * @ingroup zip_hl_extract
 *
 * Encrypted entries are decrypted transparently with UnpackOptions::password.
 * Entry::extract()'s protections always apply: absolute paths, `..`
 * components and symlinks pointing outside the destination are refused.
 *
 * @param[in] archivePath Archive file.
 * @param[in] destination Directory to extract into (created if needed).
 * @param[in] options     Filter, overwrite policy, password, error policy, progress.
 * @return A report; with UnpackOptions::continueOnError it lists every failure.
 * @throws Exception the first failure when `continueOnError` is `false`
 *         (for example `ZIP_ER_NOPASSWD`, `ZIP_ER_EXISTS`, `ZIP_ER_INVAL`);
 *         `ZIP_ER_CANCELLED` if the progress callback returned `false`; any
 *         error from opening the archive.
 * @throws std::filesystem::filesystem_error for filesystem failures when
 *         `continueOnError` is `false`.
 */
ZIPWRAPPER_API UnpackReport extractArchive(const std::filesystem::path& archivePath,
                                   const std::filesystem::path& destination,
                                   const UnpackOptions& options = {});

// ---------------------------------------------------------------------------
// Changing the protection of an existing archive
// ---------------------------------------------------------------------------

/**
 * @brief Settings for changeProtection().
 * @ingroup zip_hl_protect
 */
struct ProtectOptions {
    /// Needed to read entries that are currently encrypted.
    std::optional<std::string> oldPassword;
    /// New password; leave unset to write the archive without encryption.
    std::optional<std::string> newPassword;
    Encryption encryption = Encryption::AES256;  ///< AES128, AES192 or AES256 (default); anything else is rejected.
    /// Encrypt only files matching these patterns (empty = every file).
    std::vector<std::string> encryptOnly;
    /// Replace an existing output file (ignored by the in-place overload).
    bool overwriteOutput = false;
};

/**
 * @brief Write a copy of an archive with a different password, or none.
 * @ingroup zip_hl_protect
 *
 * Every entry is decrypted with ProtectOptions::oldPassword and re-encrypted
 * with ProtectOptions::newPassword, keeping names, contents, modification
 * times and the compression method. Entries are recompressed in the process.
 * The input is never modified.
 *
 * @param[in] input   Archive to read.
 * @param[in] output  Archive to write; must differ from @p input.
 * @param[in] options Old and new password, method, selective encryption.
 * @throws Exception `ZIP_ER_NOPASSWD` if an entry is encrypted and no old
 *         password was given; `ZIP_ER_WRONGPASSWD` for a wrong one;
 *         `ZIP_ER_INVAL` if @p output is the same file as @p input or the new
 *         password settings are unusable; `ZIP_ER_EXISTS` if @p output exists
 *         and ProtectOptions::overwriteOutput is `false`; `ZIP_ER_ENCRNOTSUPP`
 *         if the requested AES method is not available (checked before
 *         anything is written).
 */
ZIPWRAPPER_API void changeProtection(const std::filesystem::path& input,
                             const std::filesystem::path& output,
                             const ProtectOptions& options);

/**
 * @brief Change the protection of an archive in place.
 * @ingroup zip_hl_protect
 *
 * The new archive is written next to the original as `<name>.protect-tmp` and
 * swapped in only after it was completely written; on any failure the original
 * is left byte for byte as it was.
 *
 * @param[in] archivePath Archive to change.
 * @param[in] options     Old and new password, method, selective encryption
 *                        (ProtectOptions::overwriteOutput is ignored).
 * @throws Exception as the overload that writes a new file.
 */
ZIPWRAPPER_API void changeProtection(const std::filesystem::path& archivePath,
                             const ProtectOptions& options);

} // namespace Zip

ZIPWRAPPER_DISABLE_DLL_WARNINGS_END
