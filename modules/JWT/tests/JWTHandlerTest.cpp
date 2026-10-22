#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "TK4CPP/JWT/JWTHandler.h"

using TK4CPP::JWT::JWTHandler;

TEST(JWTHandler, CreatedTokensValidateAndCarryTheUser)
{
    const auto token = JWTHandler::create("alice", {"admin", "dev"});
    EXPECT_TRUE(JWTHandler::validate(token));
    EXPECT_EQ(JWTHandler::getUser(token), "alice");
}

TEST(JWTHandler, ChecksTheIssuer)
{
    const auto token = JWTHandler::create("alice", {});
    EXPECT_TRUE(JWTHandler::validate(token, "auth0"));
    EXPECT_FALSE(JWTHandler::validate(token, "somebody-else"));
}

TEST(JWTHandler, ChecksClaims)
{
    const auto token = JWTHandler::create("alice", {});
    EXPECT_TRUE(JWTHandler::validate(token, {{"user", "alice"}}));
    EXPECT_FALSE(JWTHandler::validate(token, {{"user", "mallory"}}));
}

TEST(JWTHandler, RejectsTamperedAndMalformedTokens)
{
    auto token = JWTHandler::create("alice", {});
    // Corrupt a character in the middle of the signature. (The last base64url character of a 32-byte HMAC
    // carries two padding bits that decoders ignore, so changing only that character may not change the signature.)
    char& c = token[token.size() - 10];
    c = (c == 'A') ? 'B' : 'A';
    EXPECT_FALSE(JWTHandler::validate(token));
    EXPECT_FALSE(JWTHandler::validate("not-a-token"));
    EXPECT_FALSE(JWTHandler::validate(""));
    EXPECT_TRUE(JWTHandler::getUser("not-a-token").empty());
}

TEST(JWTHandler, RejectsExpiredTokens)
{
    const auto token = JWTHandler::create("alice", {}, std::chrono::seconds(-60));
    EXPECT_FALSE(JWTHandler::validate(token));
}
