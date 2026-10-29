/**
 * @file SearchSupportTest.cpp
 * @brief Tests for SizeFormat, TimeParse, FileStat and the extended FolderHandler::find().
 */
#include <gtest/gtest.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <stop_token>
#include <string>

#include "TK4CPP/Utility/FileStat.hpp"
#include "TK4CPP/Utility/FolderHandler.hpp"
#include "TK4CPP/Utility/SizeFormat.hpp"
#include "TK4CPP/Utility/TimeParse.hpp"

using namespace TK4CPP::Utility;
namespace fs = std::filesystem;

namespace
{
    std::int64_t local(int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
    {
        std::tm tm{};
        tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
        tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s; tm.tm_isdst = -1;
        return static_cast<std::int64_t>(std::mktime(&tm));
    }

    void write(fs::path const& p, std::size_t bytes)
    {
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << std::string(bytes, 'x');
    }

    std::set<std::string> names(std::vector<fs::path> const& v)
    {
        std::set<std::string> out;
        for (auto const& p : v) out.insert(p.filename().string());
        return out;
    }
}

TEST(SizeFormat, Formatting)
{
    EXPECT_EQ(formatSize(0), "0 B");
    EXPECT_EQ(formatSize(1023), "1023 B");
    EXPECT_EQ(formatSize(1024), "1.00 KB");
    EXPECT_EQ(formatSize(1536, SizeUnits::Binary, 1), "1.5 KB");
    EXPECT_EQ(formatSize(1000, SizeUnits::Decimal), "1.00 kB");
    EXPECT_EQ(formatSize(999, SizeUnits::Decimal), "999 B");
    EXPECT_EQ(formatSize(1024, SizeUnits::Iec), "1.00 KiB");
    EXPECT_EQ(formatSize(1ULL << 40, SizeUnits::Iec), "1.00 TiB");
    // FolderHandler keeps delegating with the historical 1024/KB convention.
    EXPECT_EQ(FolderHandler::formatSize(1536), formatSize(1536, SizeUnits::Binary));
}

TEST(SizeFormat, Parsing)
{
    EXPECT_EQ(*parseSize("0"), 0u);
    EXPECT_EQ(*parseSize("500B"), 500u);
    EXPECT_EQ(*parseSize("10k"), 10'000u);
    EXPECT_EQ(*parseSize("1.5MB"), 1'500'000u);
    EXPECT_EQ(*parseSize("3T"), 3'000'000'000'000ULL);
    EXPECT_EQ(*parseSize("1KiB"), 1024u);
    EXPECT_EQ(*parseSize("2mib"), 2u * 1024 * 1024);
    EXPECT_EQ(*parseSize(".5k"), 500u);
    for (auto bad : {"", "k", "-5", "1.2.3", "5 MB", "5XB", "99999999999999999999"})
        EXPECT_FALSE(parseSize(bad).has_value()) << bad;
}

TEST(TimeParse, AbsoluteAndRelative)
{
    EXPECT_EQ(*parseTimestamp("2026-03-15"), local(2026, 3, 15));
    EXPECT_EQ(*parseTimestamp("2026-03-15", true), local(2026, 3, 15, 23, 59, 59));
    EXPECT_EQ(*parseTimestamp("2026-03-15T10:30", true), local(2026, 3, 15, 10, 30));
    EXPECT_EQ(*parseTimestamp("2026-03-15 10:30:45"), local(2026, 3, 15, 10, 30, 45));
    for (auto bad : {"2026-02-31", "2026-13-01", "2026-03-15T25:00", "2026-03-15T10", "15/03/2026",
                     "2026-03-15x", "", "d", "-3d"})
        EXPECT_FALSE(parseTimestamp(bad).has_value()) << bad;

    auto const now = std::chrono::system_clock::time_point{std::chrono::seconds{1'000'000'000}};
    EXPECT_EQ(*parseTimestamp("30s", false, now), 1'000'000'000 - 30);
    EXPECT_EQ(*parseTimestamp("5m", false, now), 1'000'000'000 - 300);
    EXPECT_EQ(*parseTimestamp("2h", false, now), 1'000'000'000 - 7200);
    EXPECT_EQ(*parseTimestamp("7d", false, now), 1'000'000'000 - 7 * 86400);
    EXPECT_EQ(*parseTimestamp("2w", false, now), 1'000'000'000 - 14 * 86400);
}

class FindTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dir = fs::temp_directory_path() / "tk4cpp_FindTest";
        fs::remove_all(dir);
        write(dir / "a.txt", 10);
        write(dir / "B.TXT", 2000);
        write(dir / "notes.md", 500);
        write(dir / "sub" / "c.txt", 3000);
        write(dir / "sub" / "deep" / "d.log", 4000);
        write(dir / ".hidden", 1);
    }
    void TearDown() override { fs::remove_all(dir); }
    fs::path dir;
    FolderHandler fh;
};

TEST_F(FindTest, ProbeFile)
{
    auto const st = probeFile(dir / "a.txt", true);
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(st.type, FileType::Regular);
    EXPECT_EQ(st.size, 10u);
    EXPECT_NE(st.modified, kNoTimestamp);
    EXPECT_EQ(probeFile(dir / "sub").type, FileType::Directory);
    EXPECT_FALSE(probeFile(dir / "missing").ok);
}

TEST_F(FindTest, NamePatternsAndCase)
{
    FolderFilter f;
    f.namePatterns = {"*.txt", "*.md"};
    EXPECT_EQ(names(fh.find(dir, f)), (std::set<std::string>{"a.txt", "notes.md", "c.txt"}));
    f.caseSensitive = false;
    EXPECT_EQ(names(fh.find(dir, f)), (std::set<std::string>{"a.txt", "B.TXT", "notes.md", "c.txt"}));
    f.excludePatterns = {"c.*"};
    EXPECT_EQ(names(fh.find(dir, f)).count("c.txt"), 0u);
    FolderFilter g;
    g.nameContains = "NOTES";
    g.caseSensitive = false;
    EXPECT_EQ(names(fh.find(dir, g)), (std::set<std::string>{"notes.md"}));
}

TEST_F(FindTest, KindAndSize)
{
    FolderFilter f;
    f.kind = FolderFilter::Kind::Directories;
    EXPECT_EQ(names(fh.find(dir, f)), (std::set<std::string>{"sub", "deep"}));
    FolderFilter s;
    s.kind = FolderFilter::Kind::Files;
    s.minSize = 1000;
    s.maxSize = 3000;
    EXPECT_EQ(names(fh.find(dir, s)), (std::set<std::string>{"B.TXT", "c.txt"}));
    FolderFilter d;
    d.minSize = 0; // size bounds never select directories
    EXPECT_EQ(names(fh.find(dir, d)).count("sub"), 0u);
}

TEST_F(FindTest, DateRanges)
{
    auto const old = fs::file_time_type::clock::now() - std::chrono::hours(24 * 400);
    fs::last_write_time(dir / "a.txt", old);
    auto const cutoff = *parseTimestamp("30d");
    FolderFilter recent;
    recent.kind = FolderFilter::Kind::Files;
    recent.modifiedAfter = cutoff;
    auto r = names(fh.find(dir, recent));
    EXPECT_EQ(r.count("a.txt"), 0u);
    EXPECT_EQ(r.count("B.TXT"), 1u);
    FolderFilter older;
    older.kind = FolderFilter::Kind::Files;
    older.modifiedBefore = cutoff;
    EXPECT_EQ(names(fh.find(dir, older)), (std::set<std::string>{"a.txt"}));
    // Creation time is "now" for fresh files where the platform exposes it; unknown never passes.
    FolderFilter created;
    created.kind = FolderFilter::Kind::Files;
    created.createdAfter = cutoff;
    if (probeFile(dir / "B.TXT", true).created != kNoTimestamp)
        EXPECT_EQ(names(fh.find(dir, created)).count("B.TXT"), 1u);
    else
        EXPECT_TRUE(fh.find(dir, created).empty());
}

TEST_F(FindTest, DepthHiddenAndPredicate)
{
    FolderFilter f;
    f.kind = FolderFilter::Kind::Files;
    f.maxDepth = 1;
    EXPECT_EQ(names(fh.find(dir, f)).count("c.txt"), 0u);
    f.maxDepth = -1;
    f.excludeHidden = true;
    EXPECT_EQ(names(fh.find(dir, f)).count(".hidden"), 0u);
    f.predicate = [](fs::path const& p) { return p.extension() == ".log"; };
    EXPECT_EQ(names(fh.find(dir, f)), (std::set<std::string>{"d.log"}));
}

TEST_F(FindTest, CancellableAndCapped)
{
    FolderFilter f;
    f.kind = FolderFilter::Kind::Files;
    std::stop_source src;
    EXPECT_EQ(fh.find(dir, f, src.get_token(), 0).size(), 6u); // incl. .hidden
    EXPECT_EQ(fh.find(dir, f, src.get_token(), 2).size(), 2u);
    src.request_stop();
    EXPECT_TRUE(fh.find(dir, f, src.get_token(), 0).empty());
}

TEST_F(FindTest, DoesNotFollowSymlinks)
{
    std::error_code ec;
    fs::create_directory_symlink(dir / "sub", dir / "link", ec);
    if (ec) GTEST_SKIP() << "symlinks not available";
    FolderFilter f;
    f.kind = FolderFilter::Kind::Files;
    f.namePattern = "c.txt";
    EXPECT_EQ(fh.find(dir, f, std::stop_token{}, 0).size(), 1u);
}
