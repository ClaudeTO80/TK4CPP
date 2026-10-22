// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file HighLevelDetail.h
 * @brief Private helpers of the high-level layer (pattern matching, CRC-32,
 *        tree collection, archive writing).
 *
 * This header is **not installed**: it is an implementation detail and may
 * change without notice.
 */

#pragma once

#include <ZipWrapper/HighLevel.h>

#include "Detail.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Zip {

namespace detail {

inline std::vector<std::string_view> splitPath(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const auto end = s.find('/', start);
        const auto stop = end == std::string_view::npos ? s.size() : end;
        if (stop > start) out.push_back(s.substr(start, stop - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

/** @brief Glob within one path component: '*' any run, '?' one character. */
inline bool globComponent(std::string_view pat, std::string_view text) {
    constexpr auto npos = std::string_view::npos;
    std::size_t p = 0, t = 0, starP = npos, starT = 0;
    while (t < text.size()) {
        if (p < pat.size() && pat[p] == '*') {
            starP = p++;
            starT = t;
        } else if (p < pat.size() && (pat[p] == '?' || pat[p] == text[t])) {
            ++p;
            ++t;
        } else if (starP != npos) {
            p = starP + 1;
            t = ++starT;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

/** @brief Match pattern components against the first @p n path components. */
inline bool globPath(const std::vector<std::string_view>& pat, std::size_t pi,
                     const std::vector<std::string_view>& path,
                     std::size_t ti, std::size_t n) {
    if (pi == pat.size()) return ti == n;
    if (pat[pi] == "**") {
        for (std::size_t k = ti; k <= n; ++k)
            if (globPath(pat, pi + 1, path, k, n)) return true;
        return false;
    }
    if (ti == n) return false;
    return globComponent(pat[pi], path[ti]) &&
           globPath(pat, pi + 1, path, ti + 1, n);
}

inline std::string_view normalizePattern(std::string_view p) {
    while (p.starts_with("./")) p.remove_prefix(2);
    while (p.starts_with('/')) p.remove_prefix(1);
    while (p.ends_with('/')) p.remove_suffix(1);
    return p;
}

/** @brief include semantics: file-name match, or whole-path match with '/'. */
inline bool matchesName(std::string_view pattern, std::string_view relPath) {
    pattern = normalizePattern(pattern);
    const auto path = splitPath(relPath);
    if (path.empty() || pattern.empty()) return false;
    const auto pat = splitPath(pattern);
    if (pattern.find('/') == std::string_view::npos && pattern != "**")
        return globComponent(pattern, path.back());
    return globPath(pat, 0, path, 0, path.size());
}

/** @brief exclude semantics: any component, or any directory prefix. */
inline bool matchesTree(std::string_view pattern, std::string_view relPath) {
    pattern = normalizePattern(pattern);
    const auto path = splitPath(relPath);
    if (path.empty() || pattern.empty()) return false;
    if (pattern.find('/') == std::string_view::npos && pattern != "**") {
        return std::ranges::any_of(path, [&](std::string_view c) {
            return globComponent(pattern, c);
        });
    }
    const auto pat = splitPath(pattern);
    for (std::size_t n = 1; n <= path.size(); ++n)
        if (globPath(pat, 0, path, 0, n)) return true;
    return false;
}

} // namespace detail

namespace detail {

/** @brief Open an entry, using the password only if the entry is encrypted. */
inline File openTransparent(const Entry& entry,
                            const std::optional<std::string>& password) {
    const Stat st = entry.stat();
    if (st.encryptionMethod == ZIP_EM_NONE) return entry.open();
    if (!password)
        throw Exception(ZIP_ER_NOPASSWD, 0,
                        "entry is encrypted and no password was given: " + st.name);
    return entry.openEncrypted(*password);
}

} // namespace detail

namespace detail {

/** @brief Table-driven CRC-32 (IEEE 802.3), the checksum ZIP stores. */
class Crc32 {
public:
    void update(std::span<const std::byte> data) {
        for (const std::byte b : data)
            crc_ = kTable[(crc_ ^ std::to_integer<std::uint32_t>(b)) & 0xffu] ^
                   (crc_ >> 8);
    }
    [[nodiscard]] std::uint32_t value() const { return ~crc_; }

private:
    static constexpr std::array<std::uint32_t, 256> kTable = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc_ = 0xffffffffu;
};

inline bool isAes(std::uint16_t m) {
    return m == ZIP_EM_AES_128 || m == ZIP_EM_AES_192 || m == ZIP_EM_AES_256;
}

} // namespace detail

namespace detail {

struct Item {
    std::filesystem::path source;
    std::string name;   // relative, '/' separated, no trailing slash
    bool directory{};
};

inline std::string utf8Name(const std::filesystem::path& rel) {
    const auto u = rel.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

inline std::string lowerExtension(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

/** @brief Reject unusable protection settings before touching any file. */
inline void validateProtection(const std::optional<std::string>& password,
                               Encryption encryption) {
    if (!password) return;
    if (password->empty())
        throw Exception(ZIP_ER_INVAL, 0, "the password must not be empty");
    if (encryption != Encryption::AES128 && encryption != Encryption::AES192 &&
        encryption != Encryption::AES256)
        throw Exception(ZIP_ER_INVAL, 0,
                        "only AES encryption is offered (traditional PKWARE is weak)");
    if (!Archive::encryptionSupported(encryption))
        throw Exception(ZIP_ER_ENCRNOTSUPP, 0,
                        "this libzip build cannot write the requested AES "
                        "encryption; refusing to fall back to a weaker method");
}

/** @brief Walk @p start and append the entries that pass the options. */
inline void collectTree(const std::filesystem::path& start,
                        const std::filesystem::path& base,
                        const ArchiveOptions& options,
                        const std::filesystem::path& outputResolved,
                        const std::filesystem::path& outputName,
                        std::vector<Item>& items, std::set<std::string>& seen,
                        ArchiveReport& report) {
    namespace fs = std::filesystem;

    // Returns true if the walk should descend into @p entry.
    const auto consider = [&](const fs::directory_entry& entry) -> bool {
        const fs::path& path = entry.path();
        const std::string name = utf8Name(path.lexically_relative(base));
        if (name.empty() || name == ".") return true;  // the base itself
        if (name == ".." || name.starts_with("../"))
            throw Exception(ZIP_ER_INVAL, 0,
                            "path is outside the base directory: " + path.string());
        if (options.filter.excludes(name)) return false;
        if (entry.is_symlink()) {
            report.skipped.push_back(name + ": symbolic link");
            return false;
        }
        std::error_code ec;
        if (entry.is_directory(ec)) {
            if (options.filter.include.empty() && seen.insert(name).second)
                items.push_back({path, name, true});
            return true;
        }
        if (!entry.is_regular_file(ec)) {
            report.skipped.push_back(name + ": not a regular file");
            return false;
        }
        // Never archive the output file into itself.
        if (path.filename() == outputName && fs::weakly_canonical(path) == outputResolved)
            return false;
        if (options.filter.includes(name, false) && seen.insert(name).second)
            items.push_back({path, name, false});
        return true;
    };

    std::error_code ec;
    if (!fs::is_directory(start, ec)) {
        consider(fs::directory_entry(start));
        return;
    }
    consider(fs::directory_entry(start));
    for (auto it = fs::recursive_directory_iterator(
             start, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        if (!consider(*it) && it->is_directory()) it.disable_recursion_pending();
    }
}

inline std::string normalizePrefix(std::string prefix) {
    while (prefix.starts_with('/')) prefix.erase(prefix.begin());
    if (!prefix.empty() && !prefix.ends_with('/')) prefix.push_back('/');
    return prefix;
}

/** @brief Turn the collected items into the output archive. */
inline ArchiveReport writeArchive(std::vector<Item>& items,
                                  const std::filesystem::path& output,
                                  const ArchiveOptions& options,
                                  ArchiveReport report) {
    namespace fs = std::filesystem;

    validateProtection(options.password, options.encryption);
    if (options.level > 9)
        throw Exception(ZIP_ER_INVAL, 0, "compression level must be 0..9");
    if (!options.overwriteOutput && fs::exists(fs::symlink_status(output)))
        throw Exception(ZIP_ER_EXISTS, 0, "output already exists: " + output.string());

    Compression method = options.compression;
    if (!Archive::compressionSupported(method)) {
        report.warnings.push_back(
            "requested compression method is not available in this libzip; using Deflate");
        method = Compression::Deflate;
    }

    // Deterministic archives: directory order is unspecified, names are not.
    std::ranges::sort(items, {}, &Item::name);

    if (output.has_parent_path()) fs::create_directories(output.parent_path());

    const std::string prefix = normalizePrefix(options.prefix);
    const PathFilter encryptFilter{options.encryptOnly, {}};

    // With verification the archive is first written next to the output and
    // only moved into place once it checked out, so a failed check can never
    // replace a good existing archive with a bad one.
    const fs::path target =
        options.verifyAfterWrite ? fs::path(output.string() + ".partial") : output;
    try {
    {
        auto archive = Archive::create(target);
        for (const auto& item : items) {
            const std::string name = prefix + item.name;
            if (item.directory) {
                archive.mkdir(name);
                ++report.directories;
                continue;
            }
            const auto index = archive.addFile(name, item.source);
            const bool store =
                std::ranges::find(options.storeExtensions, lowerExtension(item.source)) !=
                options.storeExtensions.end();
            archive.setCompression(index, store ? Compression::Store : method,
                                   options.level);
            if (options.password &&
                encryptFilter.includes(item.name, false)) {
                archive.setEncryption(index, options.encryption, *options.password);
                ++report.encrypted;
            }
            std::error_code ec;
            report.inputBytes += fs::file_size(item.source, ec);
            ++report.files;
        }
        if (options.progress) archive.setProgressCallback(options.progress);
        archive.close();
    }
    report.archiveBytes = fs::file_size(target);

    if (options.verifyAfterWrite) {
        const auto check = verifyArchive(target, options.password);
        if (!check.ok())
            throw Exception(ZIP_ER_CRC, 0,
                            "post-write verification failed: " + check.problems.front());
        fs::rename(target, output);
    }
    } catch (...) {
        if (options.verifyAfterWrite) {
            std::error_code ec;
            fs::remove(target, ec);
        }
        throw;
    }
    return report;
}

} // namespace detail

} // namespace Zip
