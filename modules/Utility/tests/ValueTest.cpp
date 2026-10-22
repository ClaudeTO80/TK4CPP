#include <gtest/gtest.h>

#include <string>

#include "TK4CPP/Utility/Properties.hpp"
#include "TK4CPP/Utility/Value.hpp"

using namespace TK4CPP::Utility;

TEST(Value, HoldsTheTypeItWasBuiltFrom)
{
    Value i(42), d(2.5), s(std::string("txt")), c("literal");
    EXPECT_TRUE(i.holds<int>());
    EXPECT_TRUE(d.holds<double>());
    EXPECT_TRUE(s.holds<std::string>());
    EXPECT_TRUE(c.holds<std::string>());
    EXPECT_EQ(i.as<int>(), 42);
    EXPECT_DOUBLE_EQ(d.as<double>(), 2.5);
    EXPECT_EQ(c.as<std::string>(), "literal");
}

TEST(Value, WrongTypeAccessThrowsTypeError)
{
    Value i(1);
    EXPECT_THROW((void)i.as<std::string>(), TypeError);
    EXPECT_FALSE(i.get<std::string>().has_value());
    EXPECT_TRUE(i.get<int>().has_value());
}

TEST(Value, Conversions)
{
    EXPECT_EQ(Value(3).to_string(), "3");
    EXPECT_DOUBLE_EQ(Value(3).to_double(), 3.0);
    EXPECT_EQ(Value(7.0).to_int(), 7);
    EXPECT_TRUE(Value(3).is_numeric());
    EXPECT_FALSE(Value("x").is_numeric());
}

TEST(Value, ComparesAndHashes)
{
    EXPECT_EQ(Value(5), Value(5));
    EXPECT_NE(Value(5), Value(6));
    EXPECT_EQ(std::hash<Value>{}(Value("abc")), std::hash<Value>{}(Value("abc")));
}

TEST(FileArtifact, KeepsTextInMemory)
{
    FileArtifact fa("hello");
    EXPECT_TRUE(fa.has_buffer());
    EXPECT_FALSE(fa.has_source());
    EXPECT_EQ(fa.text(), "hello");
    EXPECT_EQ(fa.size_bytes(), 5u);
}

TEST(FileArtifact, ThrowsFileErrorWhenSourceIsMissing)
{
    FileArtifact fa(std::filesystem::path("/definitely/not/here.bin"));
    EXPECT_THROW(fa.load(), FileError);
}

TEST(Properties, StoresAndRetrievesValues)
{
    auto p = Property::create("answer", 42);
    EXPECT_EQ(p->key(), "answer");
    EXPECT_EQ(p->value_ref<int>(), 42);
    EXPECT_THROW((void)Property::create("", 1), PropertyError);
}
