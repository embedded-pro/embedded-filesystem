#include "fs/fat/CodePage437.hpp"
#include "fs/fat/FatShortName.hpp"
#include "infra/util/BoundedString.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    fs::ShortName Name(const char (&bytes)[12], uint8_t caseFlags = 0)
    {
        fs::ShortName name;

        for (std::size_t index = 0; index != name.bytes.size(); ++index)
            name.bytes[index] = static_cast<uint8_t>(bytes[index]);

        name.caseFlags = caseFlags;
        return name;
    }

    struct Text
    {
        std::array<uint8_t, fs::ShortName::maxUtf8Bytes> bytes{};
        std::size_t size{ 0 };

        infra::BoundedConstString View() const
        {
            return infra::BoundedConstString(reinterpret_cast<const char*>(bytes.data()), size);
        }
    };

    Text Display(const fs::ShortName& name)
    {
        Text text;
        text.size = fs::ShortNameToUtf8(name, infra::MakeRange(text.bytes));
        return text;
    }
}

TEST(FatShortNameTest, checksum_of_an_alias_written_by_mtools)
{
    EXPECT_EQ(0x02, fs::LongNameChecksum(Name("ALONGF~1TXT").bytes));
}

TEST(FatShortNameTest, checksum_rotates_before_adding)
{
    EXPECT_EQ(0x82, fs::LongNameChecksum(Name("\x9aN\xd8"
                                              "C\x9d"
                                              "D~1DAT")
                                             .bytes));
}

TEST(FatShortNameTest, upper_case_name_with_extension)
{
    EXPECT_EQ("ALONGF~1.TXT", Display(Name("ALONGF~1TXT")).View());
}

TEST(FatShortNameTest, lower_case_base_and_extension_flags)
{
    EXPECT_EQ("readme.txt", Display(Name("README  TXT", 0x18)).View());
    EXPECT_EQ("readme.TXT", Display(Name("README  TXT", fs::ShortName::lowerCaseBase)).View());
    EXPECT_EQ("README.txt", Display(Name("README  TXT", fs::ShortName::lowerCaseExtension)).View());
}

TEST(FatShortNameTest, lower_case_flags_affect_only_letters)
{
    EXPECT_EQ("a_1~2.b-3", Display(Name("A_1~2   B-3", 0x18)).View());
}

TEST(FatShortNameTest, name_without_extension_has_no_dot)
{
    EXPECT_EQ("MAKEFILE", Display(Name("MAKEFILE   ")).View());
    EXPECT_EQ("A", Display(Name("A          ")).View());
}

TEST(FatShortNameTest, first_byte_0x05_stands_for_0xe5)
{
    EXPECT_EQ("\xcf\x83"
              "ABC",
        Display(Name("\x05"
                     "ABC       "))
            .View());
}

TEST(FatShortNameTest, bytes_from_0x80_decode_as_code_page_437)
{
    EXPECT_EQ("\xc3\x9cN\xe2\x95\xaa.DAT", Display(Name("\x9aN\xd8     DAT")).View());
}

TEST(FatShortNameTest, code_page_437)
{
    EXPECT_EQ(char32_t{ 'A' }, fs::FromCodePage437('A'));
    EXPECT_EQ(char32_t{ 0x7f }, fs::FromCodePage437(0x7f));
    EXPECT_EQ(char32_t{ 0xc7 }, fs::FromCodePage437(0x80));
    EXPECT_EQ(char32_t{ 0xdc }, fs::FromCodePage437(0x9a));
    EXPECT_EQ(char32_t{ 0x03c3 }, fs::FromCodePage437(0xe5));
    EXPECT_EQ(char32_t{ 0x00a0 }, fs::FromCodePage437(0xff));
}

TEST(FatShortNameTest, longest_display_fits_the_buffer)
{
    EXPECT_EQ(fs::ShortName::maxUtf8Bytes, Display(Name("\xb0\xb0\xb0\xb0\xb0\xb0\xb0\xb0\xb0\xb0\xb0")).size);
}
