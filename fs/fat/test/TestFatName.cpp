#include "fs/fat/FatName.hpp"
#include "gtest/gtest.h"
#include <array>

class FatReservedCharacterTest
    : public testing::TestWithParam<char>
{};

TEST_P(FatReservedCharacterTest, rejects_reserved_characters)
{
    std::array<char, 3> name{ 'a', GetParam(), 'b' };

    EXPECT_EQ(fs::Result::invalidPath, fs::ValidateFatName(infra::BoundedConstString(name.data(), name.size())));
}

INSTANTIATE_TEST_SUITE_P(EveryReservedCharacter, FatReservedCharacterTest,
    testing::Values('"', '*', '/', ':', '<', '>', '?', '\\', '|', '\x01', '\x1f', '\x7f', '\0'));

TEST(FatNameTest, accepts_ordinary_names)
{
    EXPECT_EQ(fs::Result::success, fs::ValidateFatName("readme.txt"));
    EXPECT_EQ(fs::Result::success, fs::ValidateFatName("A long file name.txt"));
    EXPECT_EQ(fs::Result::success, fs::ValidateFatName("a+b,c;d=e[f]"));
    EXPECT_EQ(fs::Result::success, fs::ValidateFatName("\xc3\x9cn\xc3\xaf"
                                                       "c\xc3\xb8"
                                                       "d\xc3\xa9 \xf0\x9f\x98\x80.dat"));
}

TEST(FatNameTest, rejects_empty_name)
{
    EXPECT_EQ(fs::Result::invalidPath, fs::ValidateFatName(""));
}

TEST(FatNameTest, rejects_trailing_dot_and_space)
{
    EXPECT_EQ(fs::Result::invalidPath, fs::ValidateFatName("name."));
    EXPECT_EQ(fs::Result::invalidPath, fs::ValidateFatName("name "));
    EXPECT_EQ(fs::Result::invalidPath, fs::ValidateFatName("."));
    EXPECT_EQ(fs::Result::invalidPath, fs::ValidateFatName(".."));
}

TEST(FatNameTest, accepts_leading_space_and_inner_dots)
{
    EXPECT_EQ(fs::Result::success, fs::ValidateFatName(" name"));
    EXPECT_EQ(fs::Result::success, fs::ValidateFatName(".profile"));
    EXPECT_EQ(fs::Result::success, fs::ValidateFatName("a.b.c"));
}

TEST(FatNameTest, rejects_invalid_utf8)
{
    EXPECT_EQ(fs::Result::invalidPath, fs::ValidateFatName("a\xc3"));
    EXPECT_EQ(fs::Result::invalidPath, fs::ValidateFatName("\xed\xa0\x80"));
}

TEST(FatNameTest, accepts_255_utf16_units)
{
    std::array<char, 255> name;
    name.fill('a');

    EXPECT_EQ(fs::Result::success, fs::ValidateFatName(infra::BoundedConstString(name.data(), name.size())));
}

TEST(FatNameTest, rejects_256_utf16_units)
{
    std::array<char, 256> name;
    name.fill('a');

    EXPECT_EQ(fs::Result::nameTooLong, fs::ValidateFatName(infra::BoundedConstString(name.data(), name.size())));
}

TEST(FatNameTest, counts_supplementary_characters_as_two_units)
{
    std::array<char, 4 * 128> name;

    for (std::size_t index = 0; index != name.size(); index += 4)
    {
        name[index] = '\xf0';
        name[index + 1] = '\x9f';
        name[index + 2] = '\x98';
        name[index + 3] = '\x80';
    }

    EXPECT_EQ(fs::Result::nameTooLong, fs::ValidateFatName(infra::BoundedConstString(name.data(), name.size())));
    EXPECT_EQ(fs::Result::success, fs::ValidateFatName(infra::BoundedConstString(name.data(), name.size() - 4)));
}

TEST(FatNameTest, compares_ascii_letters_ignoring_case)
{
    EXPECT_TRUE(fs::EqualIgnoringAsciiCase("ReadMe.TXT", "readme.txt"));
    EXPECT_FALSE(fs::EqualIgnoringAsciiCase("readme.txt", "readme.txv"));
    EXPECT_FALSE(fs::EqualIgnoringAsciiCase("readme", "readme.txt"));
}

TEST(FatNameTest, compares_other_characters_exactly)
{
    EXPECT_FALSE(fs::EqualIgnoringAsciiCase("\xc3\x9c", "\xc3\xbc"));
    EXPECT_TRUE(fs::EqualIgnoringAsciiCase("\xc3\x9c", "\xc3\x9c"));
    EXPECT_FALSE(fs::EqualIgnoringAsciiCase("@", "`"));
    EXPECT_FALSE(fs::EqualIgnoringAsciiCase("[", "{"));
}

TEST(FatNameTest, folds_only_lower_case_ascii_letters)
{
    EXPECT_EQ('A', fs::FoldAscii('a'));
    EXPECT_EQ('Z', fs::FoldAscii('z'));
    EXPECT_EQ('A', fs::FoldAscii('A'));
    EXPECT_EQ('{', fs::FoldAscii('{'));
    EXPECT_EQ('`', fs::FoldAscii('`'));
}
