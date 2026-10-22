#include <gtest/gtest.h>

#include "TK4CPP/Utility/Version.h"

using TK4CPP::Utility::Version;

TEST(Version, ExposesItsComponents)
{
    auto v = Version::create(1, 2, 3, 4);
    EXPECT_EQ(v->getMajor(), 1);
    EXPECT_EQ(v->getMinor(), 2);
    EXPECT_EQ(v->getPatch(), 3);
    EXPECT_EQ(v->getTweak(), 4);
}

TEST(Version, ComparesComponentByComponent)
{
    auto a = Version::create(1, 2, 3, 0);
    EXPECT_TRUE(*a < *Version::create(1, 2, 4, 0));
    EXPECT_TRUE(*a < *Version::create(1, 3, 0, 0));
    EXPECT_TRUE(*a < *Version::create(2, 0, 0, 0));
    EXPECT_TRUE(*a < *Version::create(1, 2, 3, 1));
    EXPECT_TRUE(*a > *Version::create(1, 2, 2, 9));
    EXPECT_TRUE(*a > *Version::create(0, 9, 9, 9));
    EXPECT_TRUE(*a == *Version::create(1, 2, 3, 0));
    EXPECT_TRUE(*a != *Version::create(1, 2, 3, 1));
    EXPECT_TRUE(*a <= *Version::create(1, 2, 3, 0));
}

TEST(Version, ThreeWayComparisonWithSharedPtr)
{
    auto a = Version::create(1, 0, 0, 0);
    auto b = Version::create(1, 0, 0, 1);
    EXPECT_EQ(*a <=> b, std::strong_ordering::less);
    EXPECT_EQ(*b <=> a, std::strong_ordering::greater);
    EXPECT_EQ(*a <=> a, std::strong_ordering::equal);
}
