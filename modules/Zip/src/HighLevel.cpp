// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file HighLevel.cpp
 * @brief Implementation of the high-level layer declared in HighLevel.h.
 */

#include <ZipWrapper/HighLevel.h>

#include "HighLevelDetail.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <set>

namespace Zip {

bool PathFilter::excludes(std::string_view relPath) const {
    return std::ranges::any_of(exclude, [&](const std::string& p) {
        return detail::matchesTree(p, relPath);
    });
}

bool PathFilter::includes(std::string_view relPath, bool isDirectory) const {
    if (include.empty()) return true;
    if (isDirectory) return false;
    return std::ranges::any_of(include, [&](const std::string& p) {
        return detail::matchesName(p, relPath);
    });
}

bool PathFilter::accepts(std::string_view relPath, bool isDirectory) const {
    return !excludes(relPath) && includes(relPath, isDirectory);
}

std::vector<EntryInfo> listEntries(Archive& archive, const ListOptions& options) {
    std::vector<EntryInfo> out;
    for (const auto& entry : archive) {
        const Stat st = entry.stat();
        if (options.filesOnly && st.directory) continue;
        if (!options.filter.accepts(st.name, st.directory)) continue;
        out.push_back(EntryInfo{
            st.name, st.size, st.compressedSize, st.modificationTime, st.crc,
            st.compressionMethod, st.encryptionMethod, st.directory,
            st.encryptionMethod != ZIP_EM_NONE});
    }
    return out;
}

std::vector<EntryInfo> listEntries(const std::filesystem::path& path, const ListOptions& options) {
    auto archive = Archive::open(path, OpenFlag::ReadOnly);
    return listEntries(archive, options);
}

std::vector<EntryInfo> listFiles(const std::filesystem::path& path, std::string_view includePattern) {
    ListOptions o;
    o.filesOnly = true;
    if (!includePattern.empty()) o.filter.include.emplace_back(includePattern);
    return listEntries(path, o);
}

ArchiveSummary summarize(std::span<const EntryInfo> entries) {
    ArchiveSummary s;
    for (const auto& e : entries) {
        ++s.entries;
        (e.directory ? s.directories : s.files) += 1;
        if (e.encrypted) ++s.encryptedEntries;
        s.totalSize += e.size;
        s.packedSize += e.packedSize;
    }
    return s;
}

ArchiveSummary summarize(const std::filesystem::path& path) {
    const auto entries = listEntries(path);
    return summarize(entries);
}

std::string formatListing(std::span<const EntryInfo> entries) {
    const auto method = [](std::uint16_t m) -> std::string_view {
        switch (m) {
            case ZIP_CM_STORE:   return "store";
            case ZIP_CM_DEFLATE: return "deflate";
            case ZIP_CM_BZIP2:   return "bzip2";
            case ZIP_CM_LZMA:    return "lzma";
            case ZIP_CM_ZSTD:    return "zstd";
            default:             return "other";
        }
    };
    std::string out = std::format("{:>10} {:>10} {:>6} {:<8} {:<16} {}\n",
                                  "size", "packed", "saved", "method",
                                  "modified", "name");
    for (const auto& e : entries) {
        const double saved =
            e.size ? 100.0 * (1.0 - static_cast<double>(e.packedSize) /
                                        static_cast<double>(e.size))
                   : 0.0;
        const std::chrono::sys_seconds mtime{std::chrono::seconds{e.modified}};
        out += std::format("{:>10} {:>10} {:>5.0f}% {:<8} {:%Y-%m-%d %H:%M} {}{}\n",
                           e.size, e.packedSize, saved, method(e.method), mtime,
                           e.name, e.encrypted ? " *" : "");
    }
    const auto sum = summarize(entries);
    out += std::format("{:>10} {:>10}  {} entries", sum.totalSize, sum.packedSize,
                       sum.entries);
    if (sum.encryptedEntries)
        out += std::format(", {} encrypted (*)", sum.encryptedEntries);
    out += '\n';
    return out;
}

std::vector<std::byte> readBytes(Archive& archive, std::string_view name, const std::optional<std::string>& password) {
    File file = detail::openTransparent(archive.entry(name), password);
    auto data = file.readAll();
    file.close();  // reports CRC / authentication failures
    return data;
}

std::vector<std::byte> readBytes(const std::filesystem::path& path, std::string_view name, const std::optional<std::string>& password) {
    auto archive = Archive::open(path, OpenFlag::ReadOnly);
    return readBytes(archive, name, password);
}

std::string readText(const std::filesystem::path& path, std::string_view name, const std::optional<std::string>& password) {
    const auto bytes = readBytes(path, name, password);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

VerifyReport verifyArchive(Archive& archive, const std::optional<std::string>& password) {
    VerifyReport report;
    for (const auto& entry : archive) {
        const Stat st = entry.stat();
        if (st.directory) continue;
        const bool encrypted = st.encryptionMethod != ZIP_EM_NONE;
        if (encrypted && !password) {
            ++report.skipped;
            continue;
        }
        try {
            File file = encrypted ? entry.openEncrypted(*password) : entry.open();
            detail::Crc32 crc;
            std::uint64_t total = 0;
            std::vector<std::byte> buffer(64 * 1024);
            for (;;) {
                const auto n = file.read(buffer);
                if (!n) break;
                crc.update(std::span<const std::byte>(buffer).first(n));
                total += n;
            }
            file.close();
            if (total != st.size)
                report.problems.push_back(st.name + ": size mismatch");
            else if (!detail::isAes(st.encryptionMethod) && crc.value() != st.crc)
                report.problems.push_back(st.name + ": CRC mismatch");
            else
                ++report.checked;
        } catch (const Exception& e) {
            report.problems.push_back(st.name + ": " + e.what());
        }
    }
    return report;
}

VerifyReport verifyArchive(const std::filesystem::path& path, const std::optional<std::string>& password) {
    auto archive = Archive::open(path, OpenFlag::ReadOnly);
    return verifyArchive(archive, password);
}

ArchiveReport archiveDirectory(const std::filesystem::path& directory, const std::filesystem::path& output, const ArchiveOptions& options) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(directory, ec))
        throw Exception(ZIP_ER_NOENT, 0, "directory not found: " + directory.string());
    if (!fs::is_directory(directory, ec))
        throw Exception(ZIP_ER_INVAL, 0, "not a directory: " + directory.string());
    detail::validateProtection(options.password, options.encryption);

    ArchiveReport report;
    std::vector<detail::Item> items;
    std::set<std::string> seen;
    detail::collectTree(directory, directory, options, fs::weakly_canonical(output),
                        output.filename(), items, seen, report);
    return detail::writeArchive(items, output, options, std::move(report));
}

ArchiveReport archiveFiles(std::span<const std::filesystem::path> paths, const std::filesystem::path& base, const std::filesystem::path& output, const ArchiveOptions& options) {
    namespace fs = std::filesystem;
    detail::validateProtection(options.password, options.encryption);

    ArchiveReport report;
    std::vector<detail::Item> items;
    std::set<std::string> seen;
    const fs::path outputResolved = fs::weakly_canonical(output);
    for (const auto& p : paths) {
        std::error_code ec;
        if (!fs::exists(fs::symlink_status(p, ec)))
            throw Exception(ZIP_ER_NOENT, 0, "path not found: " + p.string());
        detail::collectTree(p, base, options, outputResolved, output.filename(),
                            items, seen, report);
    }
    return detail::writeArchive(items, output, options, std::move(report));
}

UnpackReport extractArchive(const std::filesystem::path& archivePath, const std::filesystem::path& destination, const UnpackOptions& options) {
    auto archive = Archive::open(archivePath, OpenFlag::ReadOnly);

    ExtractOptions extract;
    extract.overwrite = options.overwrite;
    extract.password = options.password;

    // Decide first, so progress can report a meaningful total.
    struct Selected { std::size_t index; std::string name; };
    std::vector<Selected> selected;
    UnpackReport report;
    for (std::size_t i = 0; i < archive.size(); ++i) {
        const Stat st = archive.entry(i).stat();
        if (options.filter.accepts(st.name, st.directory))
            selected.push_back({i, st.name});
        else
            ++report.filtered;
    }

    std::size_t done = 0;
    for (const auto& s : selected) {
        if (options.progress && !options.progress(done, selected.size(), s.name))
            throw Exception(ZIP_ER_CANCELLED, 0, "extraction cancelled");
        try {
            archive.entry(s.index).extract(destination, extract);
            ++report.extracted;
        } catch (const Exception& e) {
            if (!options.continueOnError) throw;
            report.failures.push_back({s.name, e.zipCode(), e.what()});
        } catch (const std::filesystem::filesystem_error& e) {
            if (!options.continueOnError) throw;
            report.failures.push_back({s.name, -1, e.what()});
        }
        ++done;
    }
    if (options.progress) options.progress(done, selected.size(), {});
    return report;
}

void changeProtection(const std::filesystem::path& input, const std::filesystem::path& output, const ProtectOptions& options) {
    namespace fs = std::filesystem;
    detail::validateProtection(options.newPassword, options.encryption);

    std::error_code ec;
    if (fs::exists(output, ec) && fs::equivalent(input, output, ec))
        throw Exception(ZIP_ER_INVAL, 0,
                        "output must differ from input (use the in-place overload)");
    if (!options.overwriteOutput && fs::exists(fs::symlink_status(output)))
        throw Exception(ZIP_ER_EXISTS, 0, "output already exists: " + output.string());

    auto src = Archive::open(input, OpenFlag::ReadOnly);
    if (options.oldPassword) src.setDefaultPassword(*options.oldPassword);

    auto dst = Archive::create(output);
    const PathFilter encryptFilter{options.encryptOnly, {}};

    for (const auto& entry : src) {
        const Stat st = entry.stat();
        if (st.directory) {
            dst.mkdir(st.name);
            continue;
        }
        if (st.encryptionMethod != ZIP_EM_NONE && !options.oldPassword)
            throw Exception(ZIP_ER_NOPASSWD, 0,
                            "entry is encrypted and no old password was given: " + st.name);

        const auto index = dst.copyFrom(src, entry.index(), st.name);
        dst.setCompression(index, static_cast<Compression>(st.compressionMethod));
        if (options.newPassword && encryptFilter.includes(st.name, false))
            dst.setEncryption(index, options.encryption, *options.newPassword);
    }
    dst.close();  // reads from src, which is still open here
}

void changeProtection(const std::filesystem::path& archivePath, const ProtectOptions& options) {
    namespace fs = std::filesystem;
    const fs::path temp = archivePath.parent_path() /
                          (archivePath.filename().string() + ".protect-tmp");
    std::error_code ec;
    fs::remove(temp, ec);

    ProtectOptions o = options;
    o.overwriteOutput = true;
    try {
        changeProtection(archivePath, temp, o);
        fs::rename(temp, archivePath);
    } catch (...) {
        fs::remove(temp, ec);
        throw;
    }
}

} // namespace Zip
