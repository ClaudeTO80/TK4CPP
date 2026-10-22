#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "TK4CPP/Utility/FileHandler.h"

using TK4CPP::Utility::FileHandler;
namespace fs = std::filesystem;

namespace
{
    /// Creates a unique scratch directory removed again at the end of each test.
    class FileHandlerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            dir = fs::temp_directory_path() / (std::string("tk4cpp_") + info->test_suite_name() + "_" + info->name());
            fs::remove_all(dir);
            fs::create_directories(dir);
        }
        void TearDown() override { fs::remove_all(dir); }

        fs::path dir;
    };
}

TEST_F(FileHandlerTest, DistinguishesFilesFromFolders)
{
    const auto file = (dir / "a.txt").string();
    FileHandler::writeStringToFile(file, "x");

    EXPECT_TRUE(FileHandler::isFile(file));
    EXPECT_FALSE(FileHandler::isFolder(file));
    EXPECT_TRUE(FileHandler::isFolder(dir.string()));
    EXPECT_FALSE(FileHandler::isFile(dir.string()));
    EXPECT_FALSE(FileHandler::isFile((dir / "missing").string()));
    EXPECT_FALSE(FileHandler::isFolder((dir / "missing").string()));
}

TEST_F(FileHandlerTest, WritesAndReadsBack)
{
    const auto file = (dir / "data.txt").string();
    FileHandler::writeStringToFile(file, "hello\nworld");
    EXPECT_EQ(FileHandler::readStringFromFile(file), "hello\nworld");
    EXPECT_EQ(FileHandler::getFileSize(file), 11ull);
}

TEST_F(FileHandlerTest, OverwriteReplacesContent)
{
    const auto file = (dir / "data.txt").string();
    FileHandler::writeStringToFile(file, "first");
    FileHandler::writeStringToFile(file, "second", FileHandler::WriteBehavior::Overwrite);
    EXPECT_EQ(FileHandler::readStringFromFile(file), "second");
}

TEST_F(FileHandlerTest, SkipLeavesExistingFileAlone)
{
    const auto file = (dir / "data.txt").string();
    FileHandler::writeStringToFile(file, "first");
    const auto result = FileHandler::writeStringToFile(file, "second", FileHandler::WriteBehavior::Skip);
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(FileHandler::readStringFromFile(file), "first");
}

TEST_F(FileHandlerTest, CopyFailsIfDestinationExistsByDefault)
{
    const auto src = (dir / "src.txt").string();
    const auto dst = (dir / "dst.txt").string();
    FileHandler::writeStringToFile(src, "src");
    FileHandler::writeStringToFile(dst, "dst");

    EXPECT_FALSE(FileHandler::copyFile(src, dst).first);
    EXPECT_EQ(FileHandler::readStringFromFile(dst), "dst");

    EXPECT_TRUE(FileHandler::copyFile(src, dst, FileHandler::CopyBehavior::OverwriteIfExists).first);
    EXPECT_EQ(FileHandler::readStringFromFile(dst), "src");
}

TEST_F(FileHandlerTest, CreatesAndRemovesFolders)
{
    const auto nested = (dir / "a" / "b" / "c").string();
    EXPECT_TRUE(FileHandler::createFolders(nested).first);
    EXPECT_TRUE(FileHandler::isFolder(nested));

    EXPECT_FALSE(FileHandler::removeFolder((dir / "a").string(), false).first);  // not empty
    EXPECT_TRUE(FileHandler::removeFolder((dir / "a").string(), true).first);
    EXPECT_FALSE(FileHandler::isFolder((dir / "a").string()));
}

TEST(FileHandlerSizeToString, ScalesThroughTheUnits)
{
    EXPECT_EQ(FileHandler::sizeToString(0), "0 Byte");
    EXPECT_EQ(FileHandler::sizeToString(1023), "1023 Byte");
    EXPECT_EQ(FileHandler::sizeToString(1024), "1 KB");
    EXPECT_EQ(FileHandler::sizeToString(1536), "1.5 KB");
    EXPECT_EQ(FileHandler::sizeToString(1024ull * 1024), "1 MB");
    EXPECT_EQ(FileHandler::sizeToString(1024ull * 1024 * 1024), "1 GB");
    EXPECT_EQ(FileHandler::sizeToString(1024ull * 1024 * 1024 * 1024), "1 TB");
}

// Regression: sizes >= 2^60 used to divide by zero because the ZB/YB/RB constants overflow 64 bits.
TEST(FileHandlerSizeToString, HandlesTheLargestRepresentableSizes)
{
    EXPECT_EQ(FileHandler::sizeToString(1ull << 60), "1 EB");
    EXPECT_EQ(FileHandler::sizeToString(~0ull), "16 EB");
}
