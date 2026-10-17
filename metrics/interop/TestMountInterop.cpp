#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/reader/FatReader.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/interop/InteropTools.hpp"
#include "gtest/gtest.h"
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <utility>

namespace
{
    struct MkfsCase
    {
        std::string name;
        std::string options;
        uint32_t kibibytes;
    };

    void PrintTo(const MkfsCase& mkfsCase, std::ostream* stream)
    {
        *stream << mkfsCase.name;
    }

    std::string CaseName(const testing::TestParamInfo<MkfsCase>& info)
    {
        return info.param.name;
    }
}

class MountInterop
    : public testing::TestWithParam<MkfsCase>
    , public infra::ClockFixture
{
public:
    void SetUp() override
    {
        if (!metrics::ToolAvailable("mkfs.fat"))
            GTEST_SKIP() << "mkfs.fat is not installed";
    }
};

TEST_P(MountInterop, image_made_by_mkfs_mounts)
{
    const auto& mkfsCase = GetParam();
    auto path = metrics::TemporaryImage("mkfs-" + mkfsCase.name);
    std::filesystem::remove(path);
    auto made = metrics::RunTool("mkfs.fat -C " + mkfsCase.options + " " + path + " " + std::to_string(mkfsCase.kibibytes));
    ASSERT_EQ(0, made.exitCode) << made.output;

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(path) };
    device.LoadFrom(path);
    std::filesystem::remove(path);
    fs::FatFileSystem::WithStorage<1, 4> volume{ device };
    fs::FatReader::WithStorage<1> fatReader{ device };

    for (auto [engine, fileSystem] : { std::pair<const char*, fs::FileSystem*>{ "FatFileSystem", &volume }, std::pair<const char*, fs::FileSystem*>{ "FatReader", &fatReader } })
    {
        SCOPED_TRACE(engine);
        std::optional<fs::Result> mounted;
        fileSystem->Mount([&mounted](fs::Result result)
            {
                mounted = result;
            });
        ExecuteAllActions();
        EXPECT_EQ(fs::Result::success, mounted);

        std::optional<fs::Result> stat;
        fileSystem->Stat("/", [&stat](fs::Result result, const fs::EntryInfo& info)
            {
                EXPECT_EQ(fs::EntryType::directory, info.type);
                stat = result;
            });
        ExecuteAllActions();
        EXPECT_EQ(fs::Result::success, stat);

        std::optional<fs::Result> unmounted;
        fileSystem->Unmount([&unmounted](fs::Result result)
            {
                unmounted = result;
            });
        ExecuteAllActions();
        EXPECT_EQ(fs::Result::success, unmounted);
    }
}

INSTANTIATE_TEST_SUITE_P(Images, MountInterop,
    testing::Values(MkfsCase{ "fat12", "-F 12", 4096 },
        MkfsCase{ "fat16", "-F 16 -s 1", 8192 },
        MkfsCase{ "fat16_one_fat", "-F 16 -s 1 -f 1", 8192 },
        MkfsCase{ "fat16_large_clusters", "-F 16 -s 64", 262144 },
        MkfsCase{ "fat32", "-F 32 -s 1", 40960 },
        MkfsCase{ "fat32_eight_sectors_per_cluster", "-F 32 -s 8", 524288 },
        MkfsCase{ "fat32_with_mbr", "-F 32 -s 1 --mbr=y", 40960 }),
    CaseName);
