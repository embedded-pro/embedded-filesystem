#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/test_doubles/NorFlashStubImage.hpp"
#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "metrics/nor/NorChecker.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <vector>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr fs::NorConfig wideUnitConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };

    std::vector<uint8_t> Pattern(std::size_t size)
    {
        std::vector<uint8_t> pattern(size);
        for (std::size_t index = 0; index != size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 13 + index / 251);

        return pattern;
    }

    std::vector<uint8_t> ImageOf(const hal::FlashStub& flash)
    {
        std::vector<uint8_t> bytes;
        for (const auto& sector : flash.sectors)
            bytes.insert(bytes.end(), sector.begin(), sector.end());

        return bytes;
    }

    class NorBuiltImageTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        NorBuiltImageTest()
        {
            fs::NorFormatter::WithStorage<config> formatter{ flash };
            formatter.Format([](fs::Result) {});
            ExecuteAllActions();
        }

        metrics::NorCheck Check() const
        {
            return metrics::CheckNorImage(ImageOf(flash), 4096, 256);
        }

        hal::FlashStub flash{ 32, 4096 };
        fs::NorFlashStubImage image{ flash };
    };
}

TEST_F(NorBuiltImageTest, directories_and_inline_files_pass_the_checker)
{
    fs::NorImageBuilder builder{ image, config };
    auto docs = builder.AddDirectory(fs::NorImageBuilder::rootInode, "docs");
    builder.AddDirectory(docs, "sub");
    builder.AddFile(docs, "a.txt", infra::MakeRange(std::array<uint8_t, 5>{ 'h', 'e', 'l', 'l', 'o' }));
    builder.AddFile(fs::NorImageBuilder::rootInode, "t15311", {});
    builder.AddFile(fs::NorImageBuilder::rootInode, "t498781", {});

    builder.Commit();

    auto check = Check();
    ASSERT_EQ(std::nullopt, check.problem);
    const auto* file = check.Find("/docs/a.txt");
    ASSERT_NE(nullptr, file);
    EXPECT_EQ(5u, file->size);
    EXPECT_EQ((std::vector<uint8_t>{ 'h', 'e', 'l', 'l', 'o' }), file->inlineBytes);
    EXPECT_NE(nullptr, check.Find("/docs/sub"));
    EXPECT_NE(nullptr, check.Find("/t498781"));
}

TEST_F(NorBuiltImageTest, maps_of_several_levels_over_several_commits_pass_the_checker)
{
    fs::NorImageBuilder builder{ image, config };
    for (uint32_t index = 0; index != 40; ++index)
    {
        std::array<char, 3> name{ 'f', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
        builder.AddDirectory(fs::NorImageBuilder::rootInode, infra::BoundedConstString(name.data(), name.size()));

        if (index % 10 == 9)
            builder.Commit();
    }

    auto check = Check();
    ASSERT_EQ(std::nullopt, check.problem);
    EXPECT_NE(nullptr, check.Find("/fd9"));
}

TEST_F(NorBuiltImageTest, files_with_data_blocks_pass_the_checker_with_their_contents)
{
    fs::NorImageBuilder builder{ image, config };
    auto big = Pattern(9000);
    auto exact = Pattern(4096);
    builder.AddFile(fs::NorImageBuilder::rootInode, "big", infra::MakeRange(big));
    builder.AddFile(fs::NorImageBuilder::rootInode, "exact", infra::MakeRange(exact));

    builder.Commit();

    auto check = Check();
    ASSERT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(big, metrics::ReadContents(ImageOf(flash), check, *check.Find("/big")));
    EXPECT_EQ(exact, metrics::ReadContents(ImageOf(flash), check, *check.Find("/exact")));
    EXPECT_EQ(36u, check.Find("/exact")->inlineBytes.size());
}

TEST_F(NorBuiltImageTest, files_with_a_program_unit_of_16_bytes_keep_less_than_a_unit_inline)
{
    hal::FlashStub internal{ 48, 2048 };
    fs::NorFormatter::WithStorage<wideUnitConfig> formatter{ internal };
    formatter.Format([](fs::Result) {});
    ExecuteAllActions();
    fs::NorFlashStubImage internalImage{ internal };
    fs::NorImageBuilder builder{ internalImage, wideUnitConfig };
    auto contents = Pattern(2000 + 100 + 7);
    builder.AddFile(fs::NorImageBuilder::rootInode, "f", infra::MakeRange(contents));

    builder.Commit();

    auto check = metrics::CheckNorImage(ImageOf(internal), 2048, 16);
    ASSERT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(contents, metrics::ReadContents(ImageOf(internal), check, *check.Find("/f")));
    EXPECT_EQ(11u, check.Find("/f")->inlineBytes.size());
}
