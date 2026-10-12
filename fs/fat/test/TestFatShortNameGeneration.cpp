#include "fs/fat/FatShortNameGeneration.hpp"
#include "infra/util/BoundedString.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace
{
    fs::ShortNameBytes Bytes(const char (&bytes)[12])
    {
        fs::ShortNameBytes name;

        for (std::size_t index = 0; index != name.size(); ++index)
            name[index] = static_cast<uint8_t>(bytes[index]);

        return name;
    }

    std::string_view View(const fs::ShortNameBytes& bytes)
    {
        return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
}

TEST(FatShortNameGenerationTest, upper_case_8_3_name_is_exact)
{
    auto name = fs::ExactShortName("README.TXT");

    ASSERT_TRUE(name.has_value());
    EXPECT_EQ("README  TXT", View(name->bytes));
    EXPECT_EQ(0, name->caseFlags);
}

TEST(FatShortNameGenerationTest, lower_case_parts_set_the_case_flags)
{
    auto name = fs::ExactShortName("readme.TXT");

    ASSERT_TRUE(name.has_value());
    EXPECT_EQ("README  TXT", View(name->bytes));
    EXPECT_EQ(fs::ShortName::lowerCaseBase, name->caseFlags);
    EXPECT_EQ(fs::ShortName::lowerCaseBase | fs::ShortName::lowerCaseExtension, fs::ExactShortName("a1.t_t")->caseFlags);
    EXPECT_EQ(fs::ShortName::lowerCaseExtension, fs::ExactShortName("A1.txt")->caseFlags);
}

TEST(FatShortNameGenerationTest, name_without_extension_and_with_symbols_is_exact)
{
    EXPECT_EQ("A~1!#$%    ", View(fs::ExactShortName("a~1!#$%")->bytes));
    EXPECT_EQ("12345678ABC", View(fs::ExactShortName("12345678.abc")->bytes));
}

TEST(FatShortNameGenerationTest, names_that_do_not_fit_8_3_are_not_exact)
{
    EXPECT_FALSE(fs::ExactShortName("Readme.txt").has_value());
    EXPECT_FALSE(fs::ExactShortName("readme.Txt").has_value());
    EXPECT_FALSE(fs::ExactShortName("123456789.txt").has_value());
    EXPECT_FALSE(fs::ExactShortName("a.text").has_value());
    EXPECT_FALSE(fs::ExactShortName("a.b.c").has_value());
    EXPECT_FALSE(fs::ExactShortName(".a").has_value());
    EXPECT_FALSE(fs::ExactShortName("a b").has_value());
    EXPECT_FALSE(fs::ExactShortName("a+b").has_value());
    EXPECT_FALSE(fs::ExactShortName("a,b").has_value());
    EXPECT_FALSE(fs::ExactShortName("a;b").has_value());
    EXPECT_FALSE(fs::ExactShortName("a=b").has_value());
    EXPECT_FALSE(fs::ExactShortName("a[b").has_value());
    EXPECT_FALSE(fs::ExactShortName("a]b").has_value());
    EXPECT_FALSE(fs::ExactShortName("\xc3\xa9").has_value());
}

TEST(FatShortNameGenerationTest, short_form_ignores_case)
{
    EXPECT_EQ(Bytes("README  TXT"), fs::ShortFormOf("Readme.Txt"));
    EXPECT_FALSE(fs::ShortFormOf("readme.text").has_value());
}

TEST(FatShortNameGenerationTest, basis_is_upper_case_without_spaces_and_leading_dots)
{
    EXPECT_EQ("LONGFILE   ", View(fs::BasisName("long file name")));
    EXPECT_EQ("LONGFILENAM", View(fs::BasisName("long file.name")));
    EXPECT_EQ("BASHRC     ", View(fs::BasisName(".bashrc")));
    EXPECT_EQ("A       B  ", View(fs::BasisName(" ..a.b")));
}

TEST(FatShortNameGenerationTest, basis_cuts_the_base_and_extension)
{
    EXPECT_EQ("ALONGFILJPE", View(fs::BasisName("a long file.jpeg")));
    EXPECT_EQ("THISISALTXT", View(fs::BasisName("This.Is.A.Long.Name.txt")));
}

TEST(FatShortNameGenerationTest, basis_replaces_characters_a_short_name_cannot_hold)
{
    EXPECT_EQ("A_B_C_D_   ", View(fs::BasisName("a+b,c;d=e[]")));
    EXPECT_EQ("_TXT_      ", View(fs::BasisName("\xc3\xa9txt\xf0\x9f\x98\x80")));
    EXPECT_EQ("_       _  ", View(fs::BasisName("\xc3\xa9.\xe2\x82\xac")));
}

TEST(FatShortNameGenerationTest, tail_replaces_the_end_of_the_base)
{
    EXPECT_EQ("LONGFI~1TXT", View(fs::WithNumericTail(Bytes("LONGFILETXT"), 1)));
    EXPECT_EQ("LONGF~32TXT", View(fs::WithNumericTail(Bytes("LONGFILETXT"), 32)));
    EXPECT_EQ("AB~1       ", View(fs::WithNumericTail(Bytes("AB         "), 1)));
}

TEST(FatShortNameGenerationTest, tail_of_a_name_with_the_same_basis)
{
    auto basis = Bytes("LONGFILETXT");

    EXPECT_EQ(1, fs::NumericTailOf(Bytes("LONGFI~1TXT"), basis));
    EXPECT_EQ(32, fs::NumericTailOf(Bytes("LONGF~32TXT"), basis));
    EXPECT_EQ(7, fs::NumericTailOf(Bytes("AB~7       "), Bytes("AB         ")));
}

TEST(FatShortNameGenerationTest, no_tail_for_names_with_another_basis)
{
    auto basis = Bytes("LONGFILETXT");

    EXPECT_EQ(std::nullopt, fs::NumericTailOf(Bytes("LONGFILETXT"), basis));
    EXPECT_EQ(std::nullopt, fs::NumericTailOf(Bytes("LONGFI~1DOC"), basis));
    EXPECT_EQ(std::nullopt, fs::NumericTailOf(Bytes("LONGFX~1TXT"), basis));
    EXPECT_EQ(std::nullopt, fs::NumericTailOf(Bytes("LONGFI~ TXT"), basis));
    EXPECT_EQ(std::nullopt, fs::NumericTailOf(Bytes("LONGF~01TXT"), basis));
    EXPECT_EQ(std::nullopt, fs::NumericTailOf(Bytes("LONGF~1ATXT"), basis));
    EXPECT_EQ(std::nullopt, fs::NumericTailOf(Bytes("LONGFI~0TXT"), basis));
    EXPECT_EQ(std::nullopt, fs::NumericTailOf(Bytes("LONGFIL~TXT"), basis));
}

TEST(FatShortNameGenerationTest, hashed_alias_keeps_two_characters_of_the_basis)
{
    auto alias = fs::HashedAlias(Bytes("LONGFILETXT"), "long file name.txt");

    EXPECT_EQ("LO", View(alias).substr(0, 2));
    EXPECT_EQ("~1TXT", View(alias).substr(6));
    EXPECT_EQ(1, fs::NumericTailOf(alias, alias));
    EXPECT_NE(alias, fs::HashedAlias(Bytes("LONGFILETXT"), "long file name 2.txt"));
}

TEST(FatShortNameGenerationTest, hashed_alias_of_a_one_character_basis)
{
    auto alias = fs::HashedAlias(Bytes("A       B  "), "a.b.");

    EXPECT_EQ("A", View(alias).substr(0, 1));
    EXPECT_EQ("~1 B  ", View(alias).substr(5));
}

TEST(FatShortNameGenerationTest, hashed_alias_uses_upper_case_hexadecimal_digits)
{
    auto alias = fs::HashedAlias(Bytes("LONGFILETXT"), "123456789");

    EXPECT_EQ("LO29B1~1TXT", View(alias));
}
