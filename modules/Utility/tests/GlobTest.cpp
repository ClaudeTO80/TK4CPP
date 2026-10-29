/**
 * @file GlobTest.cpp
 * @brief Tests for Glob.hpp (globMatch, containsText) and Utf8.hpp.
 */
#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "TK4CPP/Utility/Glob.hpp"
#include "TK4CPP/Utility/Utf8.hpp"

using namespace TK4CPP::Utility;

TEST(Glob, StarAndQuestion)
{
    EXPECT_TRUE(globMatch("*", ""));
    EXPECT_TRUE(globMatch("*", "anything"));
    EXPECT_TRUE(globMatch("*.cpp", "main.cpp"));
    EXPECT_FALSE(globMatch("*.cpp", "main.cpp.bak"));
    EXPECT_TRUE(globMatch("*.c*", "main.cpp"));
    EXPECT_TRUE(globMatch("a*b*c", "aXXbYYc"));
    EXPECT_FALSE(globMatch("a*b*c", "aXXbYY"));
    EXPECT_TRUE(globMatch("a?c", "abc"));
    EXPECT_FALSE(globMatch("a?c", "ac"));
    EXPECT_FALSE(globMatch("a?c", "abbc"));
    EXPECT_TRUE(globMatch("", ""));
    EXPECT_FALSE(globMatch("", "x"));
    EXPECT_FALSE(globMatch("x", ""));
    EXPECT_TRUE(globMatch("**a**", "bab"));
}

TEST(Glob, Classes)
{
    EXPECT_TRUE(globMatch("file[0-9].txt", "file7.txt"));
    EXPECT_FALSE(globMatch("file[0-9].txt", "fileX.txt"));
    EXPECT_TRUE(globMatch("[abc]x", "bx"));
    EXPECT_FALSE(globMatch("[!abc]x", "bx"));
    EXPECT_TRUE(globMatch("[!abc]x", "dx"));
    EXPECT_TRUE(globMatch("[^abc]x", "dx"));
    EXPECT_TRUE(globMatch("[]]x", "]x"));
    EXPECT_TRUE(globMatch("[a-]x", "-x"));
    EXPECT_TRUE(globMatch("a[", "a["));
    EXPECT_TRUE(globMatch("a[b", "a[b"));
    EXPECT_FALSE(globMatch("[a-c]", "d"));
}

TEST(Glob, Escapes)
{
    EXPECT_TRUE(globMatch("a\\*b", "a*b"));
    EXPECT_FALSE(globMatch("a\\*b", "aXb"));
    EXPECT_TRUE(globMatch("a\\?", "a?"));
    EXPECT_FALSE(globMatch("a\\?", "ab"));
    EXPECT_TRUE(globMatch("a\\", "a\\"));
}

TEST(Glob, CaseFolding)
{
    EXPECT_FALSE(globMatch("*.CPP", "main.cpp", true));
    EXPECT_TRUE(globMatch("*.CPP", "main.cpp", false));
    EXPECT_TRUE(globMatch("[A-C]x", "bx", false));
    EXPECT_TRUE(globMatch("[a-c]x", "BX", false));
    EXPECT_FALSE(globMatch("[!a-c]x", "BX", false));
    EXPECT_FALSE(globMatch("\xC3\x89", "\xC3\xA9", false)); // only ASCII is folded
}

TEST(Glob, Utf8QuestionMarkIsOneCodePoint)
{
    EXPECT_TRUE(globMatch("caf?", "caf\xC3\xA9"));
    EXPECT_FALSE(globMatch("caf??", "caf\xC3\xA9"));
    EXPECT_TRUE(globMatch("?", "\xE2\x82\xAC"));
    EXPECT_TRUE(globMatch("?", "\xF0\x9F\x98\x80"));
    EXPECT_TRUE(globMatch("*\xC3\xA9", "caf\xC3\xA9"));
    EXPECT_TRUE(globMatch("a?b", "a\xFF" "b"));
    EXPECT_TRUE(globMatch("*", "\xC3"));
}

TEST(Glob, NoExponentialBacktracking)
{
    const std::string text(5000, 'a');
    const auto begin = std::chrono::steady_clock::now();
    EXPECT_FALSE(globMatch("a*a*a*a*a*a*a*a*a*a*b", text));
    EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::seconds(2));
    EXPECT_TRUE(globMatch("*a*a*a*a*a*a*a*a*a*a", text));
}

TEST(Glob, ContainsText)
{
    EXPECT_TRUE(containsText("Report-2025.PDF", "port", true));
    EXPECT_FALSE(containsText("Report-2025.PDF", "pdf", true));
    EXPECT_TRUE(containsText("Report-2025.PDF", "pdf", false));
    EXPECT_TRUE(containsText("abc", "", true));
    EXPECT_FALSE(containsText("ab", "abc", false));
    EXPECT_TRUE(containsText("xxabc", "ABC", false));
}

TEST(Utf8, ValidationAndRepair)
{
    EXPECT_TRUE(isValidUtf8("caf\xC3\xA9"));
    EXPECT_TRUE(isValidUtf8(""));
    EXPECT_FALSE(isValidUtf8("a\xFF"));
    EXPECT_FALSE(isValidUtf8("\xC3"));              // truncated sequence
    EXPECT_FALSE(isValidUtf8("\xC0\x80"));          // overlong
    EXPECT_FALSE(isValidUtf8("\xED\xA0\x80"));      // surrogate
    EXPECT_TRUE(isValidUtf8(toValidUtf8("a\xFF" "b\xC3")));
    EXPECT_EQ(toValidUtf8("plain"), "plain");
    EXPECT_EQ(asciiLower('A'), 'a');
    EXPECT_EQ(asciiLower('z'), 'z');
    EXPECT_EQ(asciiLower('\xC3'), '\xC3');
}

TEST(Utf8, PathRoundTrip)
{
    const std::string name = "caf\xC3\xA9.txt";
    EXPECT_EQ(pathToUtf8(pathFromUtf8(name)), name);
}
