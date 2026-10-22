#include <gtest/gtest.h>

#include <algorithm>
#include <ctime>
#include <functional>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "TK4CPP/Utility/UUIDHandler.h"

using TK4CPP::Utility::UUIDHandler;

namespace
{
    // xxxxxxxx-xxxx-Vxxx-Nxxx-xxxxxxxxxxxx with V = version and N in [89ab] (RFC 9562 variant).
    bool hasShapeOf(const std::string& uuid, char version)
    {
        const std::regex re(std::string("^[0-9a-f]{8}-[0-9a-f]{4}-") + version +
                            "[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
        return std::regex_match(uuid, re);
    }
}

TEST(UUIDHandler, V4HasCorrectShape)
{
    for (int i = 0; i < 100; ++i)
        EXPECT_TRUE(hasShapeOf(UUIDHandler::Generate_V4(), '4'));
}

TEST(UUIDHandler, V6HasCorrectShape)
{
    for (int i = 0; i < 100; ++i)
        EXPECT_TRUE(hasShapeOf(UUIDHandler::Generate_V6(), '6'));
}

TEST(UUIDHandler, V7HasCorrectShape)
{
    for (int i = 0; i < 100; ++i)
        EXPECT_TRUE(hasShapeOf(UUIDHandler::Generate_V7(), '7'));
}

TEST(UUIDHandler, V4IsUnique)
{
    std::set<std::string> seen;
    for (int i = 0; i < 10000; ++i)
        seen.insert(UUIDHandler::Generate_V4());
    EXPECT_EQ(seen.size(), 10000u);
}

TEST(UUIDHandler, V6SortsInGenerationOrder)
{
    std::vector<std::string> ids;
    for (int i = 0; i < 5000; ++i)
        ids.push_back(UUIDHandler::Generate_V6());
    EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end(), std::less_equal<>{}));  // strictly increasing
}

TEST(UUIDHandler, V7SortsInGenerationOrder)
{
    std::vector<std::string> ids;
    for (int i = 0; i < 20000; ++i)  // far more than 4096 per millisecond is not expected, but must still hold
        ids.push_back(UUIDHandler::Generate_V7());
    EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end(), std::less_equal<>{}));  // strictly increasing
}

TEST(UUIDHandler, V6TimestampMatchesWallClock)
{
    // The first 8 hex digits + next 4 are the high 48 bits of the 60-bit timestamp. Decode it back.
    const auto id = UUIDHandler::Generate_V6();
    const std::string hex = id.substr(0, 8) + id.substr(9, 4) + id.substr(15, 3);   // time_high|time_mid|time_low
    const unsigned long long ticks = std::stoull(hex, nullptr, 16);
    const unsigned long long unixTicks = ticks - 0x01B21DD213814000ULL;
    const auto seconds = static_cast<long long>(unixTicks / 10'000'000ULL);
    const auto now = static_cast<long long>(std::time(nullptr));
    EXPECT_NEAR(static_cast<double>(seconds), static_cast<double>(now), 5.0);
}

TEST(UUIDHandler, V7TimestampMatchesWallClock)
{
    const auto id = UUIDHandler::Generate_V7();
    const unsigned long long millis = std::stoull(id.substr(0, 8) + id.substr(9, 4), nullptr, 16);
    const auto now = static_cast<long long>(std::time(nullptr));
    EXPECT_NEAR(static_cast<double>(millis / 1000), static_cast<double>(now), 5.0);
}

TEST(UUIDHandler, IsThreadSafeAndStillUnique)
{
    constexpr int threads = 8, perThread = 2000;
    std::vector<std::vector<std::string>> out(threads);
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (int i = 0; i < perThread; ++i)
                out[t].push_back(UUIDHandler::Generate_V7());
        });
    for (auto& th : pool) th.join();

    std::set<std::string> all;
    for (auto& v : out) all.insert(v.begin(), v.end());
    EXPECT_EQ(all.size(), static_cast<std::size_t>(threads * perThread));
}
