/**
 * @file FileStat.hpp
 * @brief Single-call metadata probe for one filesystem entry, including the creation time.
 * @since 0.1.0
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>

#include "TK4CPP/Utility/Export.h"

namespace TK4CPP::Utility
{
    /** @brief Kind of filesystem entry; symbolic links are never followed by probeFile(). */
    enum class FileType : std::uint8_t { Regular, Directory, Symlink, Other };

    /** @brief Marker stored in FileStat::modified / FileStat::created when the timestamp is unknown. */
    inline constexpr std::int64_t kNoTimestamp = std::numeric_limits<std::int64_t>::min();

    /** @brief Metadata of one entry (the entry itself, not the target of a symlink). */
    struct FileStat
    {
        FileType type{FileType::Other};       ///< Kind of entry.
        std::uintmax_t size{0};               ///< Logical size in bytes (regular files only).
        std::int64_t modified{kNoTimestamp};  ///< Modification time, seconds since the Unix epoch.
        std::int64_t created{kNoTimestamp};   ///< Creation (birth) time; only filled on request and where supported.
        std::uint64_t device{0};              ///< Filesystem id (POSIX only, 0 elsewhere).
        bool ok{false};                       ///< False if the entry could not be examined.
    };

    /**
     * @brief Read the metadata of @p path without following symbolic links.
     * @details On POSIX one lstat() yields type, size, mtime and device. The creation time costs one more
     *          statx(STATX_BTIME) call on Linux (the inode change time <tt>st_ctime</tt> is <em>not</em> the creation
     *          time there) and comes from <tt>st_birthtime</tt> on macOS; it stays kNoTimestamp where the platform
     *          or the filesystem does not record it, and on Windows (not implemented).
     * @param path         Entry to examine.
     * @param withCreation Also fetch FileStat::created.
     * @return The metadata; FileStat::ok is false if the entry cannot be examined. Never throws.
     */
    [[nodiscard]] TK4CPP_UTILITY_EXPORT FileStat probeFile(std::filesystem::path const& path,
                                                           bool withCreation = false) noexcept;
} // namespace TK4CPP::Utility
