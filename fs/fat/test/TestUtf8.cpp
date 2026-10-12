#include "fs/fat/Utf8.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    std::optional<fs::DecodedCodePoint> Decode(infra::BoundedConstString text)
    {
        return fs::DecodeUtf8(text, 0);
    }

    std::array<uint8_t, 4> Encode(char32_t codePoint)
    {
        std::array<uint8_t, 4> out{};
        fs::EncodeUtf8(codePoint, infra::MakeRange(out));
        return out;
    }
}

TEST(Utf8Test, decodes_one_to_four_byte_sequences)
{
    EXPECT_EQ((fs::DecodedCodePoint{ U'A', 1 }), Decode("A"));
    EXPECT_EQ((fs::DecodedCodePoint{ 0xdc, 2 }), Decode("\xc3\x9c"));
    EXPECT_EQ((fs::DecodedCodePoint{ 0x20ac, 3 }), Decode("\xe2\x82\xac"));
    EXPECT_EQ((fs::DecodedCodePoint{ 0x1f600, 4 }), Decode("\xf0\x9f\x98\x80"));
}

TEST(Utf8Test, decodes_at_an_offset)
{
    EXPECT_EQ((fs::DecodedCodePoint{ 0x20ac, 3 }), fs::DecodeUtf8("a\xe2\x82\xac", 1));
}

TEST(Utf8Test, decodes_the_bounds_of_every_length)
{
    EXPECT_EQ((fs::DecodedCodePoint{ 0x7f, 1 }), Decode("\x7f"));
    EXPECT_EQ((fs::DecodedCodePoint{ 0x80, 2 }), Decode("\xc2\x80"));
    EXPECT_EQ((fs::DecodedCodePoint{ 0x800, 3 }), Decode("\xe0\xa0\x80"));
    EXPECT_EQ((fs::DecodedCodePoint{ 0xffff, 3 }), Decode("\xef\xbf\xbf"));
    EXPECT_EQ((fs::DecodedCodePoint{ 0x10ffff, 4 }), Decode("\xf4\x8f\xbf\xbf"));
}

TEST(Utf8Test, rejects_overlong_sequences)
{
    EXPECT_EQ(std::nullopt, Decode("\xc0\xaf"));
    EXPECT_EQ(std::nullopt, Decode("\xc1\xbf"));
    EXPECT_EQ(std::nullopt, Decode("\xe0\x9f\xbf"));
    EXPECT_EQ(std::nullopt, Decode("\xf0\x8f\xbf\xbf"));
}

TEST(Utf8Test, rejects_surrogates)
{
    EXPECT_EQ(std::nullopt, Decode("\xed\xa0\x80"));
    EXPECT_EQ(std::nullopt, Decode("\xed\xbf\xbf"));
}

TEST(Utf8Test, rejects_code_points_above_10ffff)
{
    EXPECT_EQ(std::nullopt, Decode("\xf4\x90\x80\x80"));
}

TEST(Utf8Test, rejects_truncated_sequences)
{
    EXPECT_EQ(std::nullopt, Decode("\xc3"));
    EXPECT_EQ(std::nullopt, Decode("\xe2\x82"));
}

TEST(Utf8Test, rejects_bad_continuation_and_lead_bytes)
{
    EXPECT_EQ(std::nullopt, Decode("\xc3\x41"));
    EXPECT_EQ(std::nullopt, Decode("\x80"));
    EXPECT_EQ(std::nullopt, Decode("\xf8\x88\x80\x80"));
}

TEST(Utf8Test, encodes_code_points)
{
    EXPECT_EQ((std::array<uint8_t, 4>{ 'A', 0, 0, 0 }), Encode(U'A'));
    EXPECT_EQ((std::array<uint8_t, 4>{ 0xc3, 0x9c, 0, 0 }), Encode(0xdc));
    EXPECT_EQ((std::array<uint8_t, 4>{ 0xe2, 0x82, 0xac, 0 }), Encode(0x20ac));
    EXPECT_EQ((std::array<uint8_t, 4>{ 0xf0, 0x9f, 0x98, 0x80 }), Encode(0x1f600));
}

TEST(Utf8Test, bytes_of_code_points)
{
    EXPECT_EQ(1, fs::Utf8Bytes(0x7f));
    EXPECT_EQ(2, fs::Utf8Bytes(0x80));
    EXPECT_EQ(2, fs::Utf8Bytes(0x7ff));
    EXPECT_EQ(3, fs::Utf8Bytes(0x800));
    EXPECT_EQ(3, fs::Utf8Bytes(0xffff));
    EXPECT_EQ(4, fs::Utf8Bytes(0x10000));
}

TEST(Utf8Test, counts_utf16_units)
{
    EXPECT_EQ(0u, fs::Utf16Units(""));
    EXPECT_EQ(3u, fs::Utf16Units("a\xc3\x9c\xe2\x82\xac"));
    EXPECT_EQ(3u, fs::Utf16Units("a\xf0\x9f\x98\x80"));
    EXPECT_EQ(std::nullopt, fs::Utf16Units("a\xc3"));
}

TEST(Utf8Test, combines_surrogate_pairs)
{
    EXPECT_TRUE(fs::IsHighSurrogate(0xd83d));
    EXPECT_FALSE(fs::IsHighSurrogate(0xde00));
    EXPECT_TRUE(fs::IsLowSurrogate(0xde00));
    EXPECT_FALSE(fs::IsLowSurrogate(0xd83d));
    EXPECT_FALSE(fs::IsHighSurrogate(0xd7ff));
    EXPECT_FALSE(fs::IsLowSurrogate(0xe000));
    EXPECT_EQ(char32_t{ 0x1f600 }, fs::CombineSurrogates(0xd83d, 0xde00));
    EXPECT_EQ(char32_t{ 0x10000 }, fs::CombineSurrogates(0xd800, 0xdc00));
    EXPECT_EQ(char32_t{ 0x10ffff }, fs::CombineSurrogates(0xdbff, 0xdfff));
}

TEST(Utf8Test, encodes_utf16_units_with_surrogate_pairs)
{
    fs::Utf16Encoder encoder{ "a\xc3\x9c\xf0\x9f\x98\x80" };

    EXPECT_EQ(char16_t{ 'a' }, encoder.Next());
    EXPECT_EQ(char16_t{ 0xdc }, encoder.Next());
    EXPECT_EQ(char16_t{ 0xd83d }, encoder.Next());
    EXPECT_EQ(char16_t{ 0xde00 }, encoder.Next());
    EXPECT_EQ(std::nullopt, encoder.Next());
    EXPECT_EQ(std::nullopt, encoder.Next());
}
