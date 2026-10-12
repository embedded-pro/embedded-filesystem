#include "fs/fat/FatFileSystem.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/fat/FatChecker.hpp"
#include "metrics/interop/InteropTools.hpp"
#include "gtest/gtest.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace
{
    struct WriteCase
    {
        std::string name;
        std::string options;
        uint32_t kibibytes;
    };

    void PrintTo(const WriteCase& writeCase, std::ostream* stream)
    {
        *stream << writeCase.name;
    }

    std::string CaseName(const testing::TestParamInfo<WriteCase>& info)
    {
        return info.param.name;
    }

    std::vector<uint8_t> Pattern(std::size_t size, uint8_t seed)
    {
        std::vector<uint8_t> pattern(size);

        for (std::size_t index = 0; index != size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 17 + seed + index / 263);

        return pattern;
    }
}

class WriteInterop
    : public testing::TestWithParam<WriteCase>
    , public infra::ClockFixture
{
public:
    void SetUp() override
    {
        if (!metrics::ToolAvailable("mkfs.fat") || !metrics::ToolAvailable("mcopy") || !metrics::ToolAvailable("fsck.fat"))
            GTEST_SKIP() << "dosfstools or mtools are not installed";

        directory = std::filesystem::path(testing::TempDir()) / ("emfs-write-" + GetParam().name);
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        image = metrics::TemporaryImage("write-" + GetParam().name);
        std::filesystem::remove(image);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(directory);
        std::filesystem::remove(image);
    }

    metrics::ToolResult Run(const std::string& command)
    {
        return metrics::RunTool("MTOOLS_SKIP_CHECK=1 LC_ALL=C.UTF-8 " + command);
    }

    template<class Start>
    fs::Result Wait(const Start& start)
    {
        std::optional<fs::Result> result;
        start([&result](fs::Result completed)
            {
                result = completed;
            });
        ExecuteAllActions();
        return result.value_or(fs::Result::deviceError);
    }

    std::filesystem::path directory;
    std::string image;
};

TEST_P(WriteInterop, appended_file_passes_fsck_and_reads_back_with_mtools)
{
    auto original = Pattern(3000, 1);
    std::ofstream{ directory / "log.bin", std::ios::binary }.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
    ASSERT_EQ(0, Run("mkfs.fat -C " + GetParam().options + " " + image + " " + std::to_string(GetParam().kibibytes)).exitCode);
    ASSERT_EQ(0, Run("mcopy -i " + image + " '" + (directory / "log.bin").string() + "' ::/").exitCode);

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(image) };
    device.LoadFrom(image);
    fs::FatFileSystem::WithStorage<1, 8> volume{ device };
    ASSERT_EQ(fs::Result::success, Wait([&volume](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           volume.Mount(onDone);
                                       }));

    infra::SharedPtr<fs::File> file;
    volume.Open("/log.bin", fs::OpenMode::append, [&file](fs::Result result, infra::SharedPtr<fs::File> opened)
        {
            EXPECT_EQ(fs::Result::success, result);
            file = opened;
        });
    ExecuteAllActions();
    ASSERT_NE(nullptr, file);

    auto expected = original;
    for (uint8_t round = 0; round != 5; ++round)
    {
        auto added = Pattern(round == 2 ? 40000 : 777, static_cast<uint8_t>(round + 10));
        expected.insert(expected.end(), added.begin(), added.end());
        EXPECT_EQ(fs::Result::success, Wait([&file, &added](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               file->Write(infra::MakeRange(added), onDone);
                                           }));
    }

    EXPECT_EQ(fs::Result::success, Wait([&file](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           file->Close(onDone);
                                       }));
    file = nullptr;
    EXPECT_EQ(fs::Result::success, Wait([&volume](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           volume.Unmount(onDone);
                                       }));
    device.Save(image);

    auto fsck = Run("fsck.fat -n " + image);
    EXPECT_EQ(0, fsck.exitCode) << fsck.output;

    std::ifstream stream{ image, std::ios::binary };
    auto check = metrics::CheckFatImage([&stream](uint32_t sector)
        {
            std::vector<uint8_t> contents(512);
            stream.seekg(std::streamoff{ sector } * 512);
            stream.read(reinterpret_cast<char*>(contents.data()), 512);
            return contents;
        },
        metrics::FatCheckMode::strict);
    EXPECT_EQ(std::nullopt, check.problem);

    ASSERT_EQ(0, Run("mcopy -n -i " + image + " ::/log.bin '" + (directory / "back.bin").string() + "'").exitCode);
    std::ifstream back{ directory / "back.bin", std::ios::binary };
    std::vector<uint8_t> read{ std::istreambuf_iterator<char>(back), std::istreambuf_iterator<char>() };
    EXPECT_EQ(expected, read);
}

INSTANTIATE_TEST_SUITE_P(Images, WriteInterop,
    testing::Values(WriteCase{ "fat12", "-F 12 -s 2", 4096 },
        WriteCase{ "fat16", "-F 16 -s 1", 16384 },
        WriteCase{ "fat32", "-F 32 -s 1", 40960 }),
    CaseName);
