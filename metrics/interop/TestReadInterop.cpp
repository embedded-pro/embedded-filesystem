#include "fs/fat/FatFileSystem.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/fat/FatChecker.hpp"
#include "metrics/interop/InteropTools.hpp"
#include "gtest/gtest.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace
{
    struct ReadCase
    {
        std::string name;
        std::string options;
        uint32_t kibibytes;
        uint32_t clusterBytes;
    };

    void PrintTo(const ReadCase& readCase, std::ostream* stream)
    {
        *stream << readCase.name;
    }

    std::string CaseName(const testing::TestParamInfo<ReadCase>& info)
    {
        return info.param.name;
    }

    std::vector<uint8_t> Contents(const std::string& name, std::size_t size)
    {
        std::vector<uint8_t> contents(size);

        for (std::size_t index = 0; index != size; ++index)
            contents[index] = static_cast<uint8_t>(index * 13 + name.size() + index / 251);

        return contents;
    }
}

class ReadInterop
    : public testing::TestWithParam<ReadCase>
    , public infra::ClockFixture
{
public:
    void SetUp() override
    {
        if (!metrics::ToolAvailable("mkfs.fat") || !metrics::ToolAvailable("mcopy"))
            GTEST_SKIP() << "dosfstools or mtools are not installed";

        files = std::filesystem::path(testing::TempDir()) / ("emfs-read-" + GetParam().name);
        std::filesystem::remove_all(files);
        std::filesystem::create_directories(files);
        image = metrics::TemporaryImage("read-" + GetParam().name);
        std::filesystem::remove(image);
        Run("mkfs.fat -C " + GetParam().options + " " + image + " " + std::to_string(GetParam().kibibytes));
    }

    void TearDown() override
    {
        std::filesystem::remove_all(files);
        std::filesystem::remove(image);
    }

    void Run(const std::string& command)
    {
        auto result = metrics::RunTool("MTOOLS_SKIP_CHECK=1 LC_ALL=C.UTF-8 " + command);
        ASSERT_EQ(0, result.exitCode) << command << "\n"
                                      << result.output;
    }

    void Copy(const std::string& name, std::size_t size)
    {
        auto contents = Contents(name, size);
        std::ofstream{ files / name, std::ios::binary }.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
        Run("mcopy -i " + image + " '" + (files / name).string() + "' ::/");
        expected[name] = contents;
    }

    void Delete(const std::string& name)
    {
        Run("mdel -i " + image + " ::/" + name);
        expected.erase(name);
    }

    std::optional<std::vector<uint8_t>> ReadBack(fs::FileSystem& volume, const std::string& name, std::size_t chunk)
    {
        auto path = "/" + name;
        infra::SharedPtr<fs::File> file;
        volume.Open(infra::BoundedConstString(path.data(), path.size()), fs::OpenMode::read, [&file](fs::Result result, infra::SharedPtr<fs::File> opened)
            {
                EXPECT_EQ(fs::Result::success, result);
                file = opened;
            });
        ExecuteAllActions();

        if (file == nullptr)
            return std::nullopt;

        std::vector<uint8_t> contents;
        std::vector<uint8_t> buffer(chunk);

        for (std::size_t bytesRead = chunk; bytesRead != 0;)
        {
            bytesRead = 0;
            file->Read(infra::MakeRange(buffer), [&bytesRead](fs::Result result, std::size_t read)
                {
                    EXPECT_EQ(fs::Result::success, result);
                    bytesRead = read;
                });
            ExecuteAllActions();
            contents.insert(contents.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(bytesRead));
        }

        return contents;
    }

    std::filesystem::path files;
    std::string image;
    std::map<std::string, std::vector<uint8_t>> expected;
};

TEST_P(ReadInterop, files_written_by_mtools_read_back)
{
    auto cluster = GetParam().clusterBytes;

    for (std::size_t size : std::set<std::size_t>{ 0, 1, 511, 512, 513, cluster - 1, cluster, cluster + 1, 5 * cluster + 100 })
        Copy("size" + std::to_string(size) + ".bin", size);

    Copy("gap.bin", 3 * cluster);
    Copy("between.bin", cluster);
    Delete("gap.bin");
    Copy("fragmented.bin", 7 * cluster + 300);

    std::ifstream stream{ image, std::ios::binary };
    metrics::SectorReader reader = [&stream](uint32_t sector)
    {
        std::vector<uint8_t> contents(512);
        stream.seekg(std::streamoff{ sector } * 512);
        stream.read(reinterpret_cast<char*>(contents.data()), 512);
        return contents;
    };
    auto check = metrics::CheckFatImage(reader, metrics::FatCheckMode::strict);
    EXPECT_EQ(std::nullopt, check.problem);

    for (const auto& [name, contents] : expected)
        if (const auto* entry = check.Find("/" + name); entry != nullptr)
            EXPECT_EQ(contents, metrics::ReadContents(reader, check, *entry)) << name;
        else
            ADD_FAILURE() << "the checker does not find " << name;

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(image) };
    device.LoadFrom(image);
    fs::FatFileSystem::WithStorage<1, 4> volume{ device };
    std::optional<fs::Result> mounted;
    volume.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    ExecuteAllActions();
    ASSERT_EQ(fs::Result::success, mounted);

    for (const auto& [name, contents] : expected)
        for (std::size_t chunk : { std::size_t{ 1000 }, std::size_t{ 4 * cluster } })
            EXPECT_EQ(contents, ReadBack(volume, name, chunk)) << name << " in chunks of " << chunk;
}

INSTANTIATE_TEST_SUITE_P(Images, ReadInterop,
    testing::Values(ReadCase{ "fat12", "-F 12 -s 2", 4096, 1024 },
        ReadCase{ "fat16", "-F 16 -s 4", 16384, 2048 },
        ReadCase{ "fat32", "-F 32 -s 1", 40960, 512 }),
    CaseName);
