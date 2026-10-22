#include <gtest/gtest.h>

#include "TK4CPP/Utility/System.h"

using TK4CPP::Utility::System;

TEST(System, ReportsAtLeastOneCore)
{
    EXPECT_GE(System::logicalCores(), 1u);
    EXPECT_GE(System::physicalCores(), 1u);
}

TEST(System, PhysicalCoresDoNotExceedLogicalCores)
{
    EXPECT_LE(System::physicalCores(), System::logicalCores());
}

TEST(System, ReportsSomeFreeMemory)
{
    EXPECT_GT(System::freeMemory(), 0ull);
}
