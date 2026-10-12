#include "fs/fat/FatFormatter.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "metrics/interop/InteropTools.hpp"
#include "gtest/gtest.h"
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>

namespace
{
    struct FormatCase
    {
        std::string name;
        uint32_t blocks;
        fs::FatFormatConfig config;
    };

    fs::FatFormatConfig WithAllocationUnit(uint32_t sectors)
    {
        fs::FatFormatConfig config;
        config.alignmentSectors = sectors;
        return config;
    }

    fs::FatFormatConfig WithClusterSize(uint8_t sectors)
    {
        fs::FatFormatConfig config;
        config.sectorsPerCluster = sectors;
        return config;
    }

    fs::FatFormatConfig WithOneCopy()
    {
        fs::FatFormatConfig config;
        config.fatCopies = 1;
        return config;
    }

    fs::FatFormatConfig WithoutPartitionTable()
    {
        fs::FatFormatConfig config;
        config.partitioned = false;
        return config;
    }

    void PrintTo(const FormatCase& formatCase, std::ostream* stream)
    {
        *stream << formatCase.name;
    }

    std::string CaseName(const testing::TestParamInfo<FormatCase>& info)
    {
        return info.param.name;
    }
}

class FormatterInterop
    : public testing::TestWithParam<FormatCase>
    , public infra::EventDispatcherFixture
{
public:
    void SetUp() override
    {
        if (!metrics::ToolAvailable("fsck.fat"))
            GTEST_SKIP() << "fsck.fat is not installed";
    }
};

TEST_P(FormatterInterop, image_passes_fsck)
{
    const auto& formatCase = GetParam();
    metrics::ImageBlockDevice device{ formatCase.blocks };
    fs::FatFormatter::WithStorage<64> formatter{ device };
    auto layout = fs::ComputeFormatLayout(formatCase.blocks, 512, formatCase.config);
    ASSERT_TRUE(layout.has_value());

    device.Fill(layout->volumeStart, layout->dataStart + layout->parameters.sectorsPerCluster, 0xa5);

    std::optional<fs::Result> result;
    formatter.Format(formatCase.config, [&result](fs::Result formatted)
        {
            result = formatted;
        });
    ExecuteAllActions();
    ASSERT_EQ(fs::Result::success, result);

    auto path = metrics::TemporaryImage(formatCase.name);
    device.Save(path, layout->volumeStart);
    auto check = metrics::RunTool("fsck.fat -n -v " + path);
    std::filesystem::remove(path);

    EXPECT_EQ(0, check.exitCode) << check.output;
    EXPECT_EQ(std::string::npos, check.output.find("Dirty bit")) << check.output;
}

INSTANTIATE_TEST_SUITE_P(Formats, FormatterInterop,
    testing::Values(FormatCase{ "fat12_4m", 8192, {} },
        FormatCase{ "fat16_256m", 524288, {} },
        FormatCase{ "fat16_256m_allocation_unit", 524288, WithAllocationUnit(8192) },
        FormatCase{ "fat16_one_copy", 524288, WithOneCopy() },
        FormatCase{ "fat16_without_partition_table", 131072, WithoutPartitionTable() },
        FormatCase{ "fat32_64m", 131072, WithClusterSize(1) },
        FormatCase{ "fat32_4g", 8388608, {} }),
    CaseName);
