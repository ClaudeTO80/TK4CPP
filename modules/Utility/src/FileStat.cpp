#include <TK4CPP/Utility/FileStat.hpp>

#include <chrono>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#define TK4CPP_UTILITY_POSIX_STAT 1
#include <sys/stat.h>
#if defined(__linux__) && defined(STATX_BTIME)
#define TK4CPP_UTILITY_HAS_STATX 1
#include <fcntl.h>
#endif
#endif

namespace TK4CPP::Utility {

namespace {

[[maybe_unused]] std::int64_t toUnixSeconds(std::filesystem::file_time_type time)
{
    using namespace std::chrono;
    if constexpr (requires { clock_cast<system_clock>(time); }) {
        return floor<seconds>(clock_cast<system_clock>(time).time_since_epoch()).count();
    } else {
        auto const sys = time_point_cast<system_clock::duration>(time - file_clock::now() + system_clock::now());
        return floor<seconds>(sys.time_since_epoch()).count();
    }
}

} // namespace

#if defined(TK4CPP_UTILITY_POSIX_STAT)
FileStat probeFile(std::filesystem::path const& path, bool withCreation) noexcept
{
    FileStat result;
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) return result;
    result.ok = true;
    result.device = static_cast<std::uint64_t>(st.st_dev);
    if (S_ISLNK(st.st_mode)) {
        result.type = FileType::Symlink;
    } else if (S_ISDIR(st.st_mode)) {
        result.type = FileType::Directory;
        result.modified = static_cast<std::int64_t>(st.st_mtime);
    } else if (S_ISREG(st.st_mode)) {
        result.type = FileType::Regular;
        result.size = static_cast<std::uintmax_t>(st.st_size);
        result.modified = static_cast<std::int64_t>(st.st_mtime);
    }
    if (withCreation && result.type != FileType::Symlink && result.type != FileType::Other) {
#if defined(__APPLE__)
        result.created = static_cast<std::int64_t>(st.st_birthtimespec.tv_sec);
#elif defined(TK4CPP_UTILITY_HAS_STATX)
        struct statx sx{};
        if (::statx(AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BTIME, &sx) == 0 &&
            (sx.stx_mask & STATX_BTIME) != 0)
            result.created = static_cast<std::int64_t>(sx.stx_btime.tv_sec);
#endif
    }
    return result;
}
#else
FileStat probeFile(std::filesystem::path const& path, bool) noexcept
{
    FileStat result;
    std::error_code ec;
    auto const status = std::filesystem::symlink_status(path, ec);
    if (ec) return result;
    result.ok = true;
    if (std::filesystem::is_symlink(status)) result.type = FileType::Symlink;
    else if (std::filesystem::is_directory(status)) result.type = FileType::Directory;
    else if (std::filesystem::is_regular_file(status)) result.type = FileType::Regular;
    if (result.type == FileType::Regular) {
        auto const size = std::filesystem::file_size(path, ec);
        if (ec) result.ok = false; else result.size = size;
    }
    if (result.type == FileType::Regular || result.type == FileType::Directory) {
        ec.clear();
        auto const time = std::filesystem::last_write_time(path, ec);
        if (ec) result.ok = false; else result.modified = toUnixSeconds(time);
    }
    return result;
}
#endif

} // namespace TK4CPP::Utility
