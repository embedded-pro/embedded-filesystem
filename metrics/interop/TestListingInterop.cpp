#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/reader/FatReader.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/interop/InteropTools.hpp"
#include "gtest/gtest.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>

namespace
{
    struct ListingCase
    {
        std::string name;
        std::string options;
        uint32_t kibibytes;
    };

    struct Listed
    {
        fs::EntryType type;
        uint32_t size;

        bool operator==(const Listed& other) const = default;
    };

    using Listing = std::map<std::string, Listed>;

    void PrintTo(const ListingCase& listingCase, std::ostream* stream)
    {
        *stream << listingCase.name;
    }

    std::string CaseName(const testing::TestParamInfo<ListingCase>& info)
    {
        return info.param.name;
    }

    void PrintTo(const Listed& listed, std::ostream* stream)
    {
        *stream << (listed.type == fs::EntryType::directory ? "directory" : "file") << " of " << listed.size;
    }

    std::string Mtools(const std::string& command)
    {
        return "MTOOLS_SKIP_CHECK=1 LC_ALL=C.UTF-8 " + command;
    }
}

class ListingInterop
    : public testing::TestWithParam<ListingCase>
    , public infra::ClockFixture
{
public:
    void SetUp() override
    {
        if (!metrics::ToolAvailable("mkfs.fat") || !metrics::ToolAvailable("mcopy"))
            GTEST_SKIP() << "dosfstools or mtools are not installed";

        files = std::filesystem::path(testing::TempDir()) / ("emfs-listing-" + GetParam().name);
        std::filesystem::remove_all(files);
        std::filesystem::create_directories(files / "Documents");
        image = metrics::TemporaryImage("listing-" + GetParam().name);
        std::filesystem::remove(image);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(files);
        std::filesystem::remove(image);
    }

    void WriteFile(const std::filesystem::path& path, std::size_t size)
    {
        std::ofstream stream{ path, std::ios::binary };
        stream << std::string(size, 'x');
        expected[path.parent_path() == files ? "/" : "/Documents"][path.filename().string()] = Listed{ fs::EntryType::file, static_cast<uint32_t>(size) };
    }

    void Run(const std::string& command)
    {
        auto result = metrics::RunTool(command);
        ASSERT_EQ(0, result.exitCode) << command << "\n"
                                      << result.output;
    }

    std::optional<fs::Result> Lifecycle(fs::FileSystem& volume, void (fs::FileSystem::*call)(const infra::Function<void(fs::Result result)>& onDone))
    {
        std::optional<fs::Result> completed;
        (volume.*call)([&completed](fs::Result result)
            {
                completed = result;
            });
        ExecuteAllActions();
        return completed;
    }

    Listing List(fs::FileSystem& volume, const std::string& path)
    {
        Listing listing;
        infra::SharedPtr<fs::Directory> directory;
        volume.OpenDirectory(infra::BoundedConstString(path.data(), path.size()), [&directory](fs::Result result, infra::SharedPtr<fs::Directory> opened)
            {
                EXPECT_EQ(fs::Result::success, result);
                directory = opened;
            });
        ExecuteAllActions();

        for (bool more = directory != nullptr; more;)
        {
            more = false;
            directory->Next([&listing, &more](fs::Result result, std::optional<fs::EntryInfo> entry)
                {
                    EXPECT_EQ(fs::Result::success, result);

                    if (entry)
                    {
                        listing[std::string(entry->name.data(), entry->name.size())] = Listed{ entry->type, entry->size };
                        more = true;
                    }
                });
            ExecuteAllActions();
        }

        return listing;
    }

    std::filesystem::path files;
    std::string image;
    std::map<std::string, Listing> expected;
};

TEST_P(ListingInterop, tree_written_by_mtools_is_listed)
{
    WriteFile(files / (std::string("\xc3\x84rger \xc3\xbc") + "ber.txt"), 6);
    WriteFile(files / "readme.txt", 0);
    WriteFile(files / "UPPER.TXT", 511);
    WriteFile(files / "Documents" / "A long file name with spaces.text", 4097);

    for (int index = 0; index != 30; ++index)
        WriteFile(files / "Documents" / ("file number " + std::to_string(index) + ".txt"), index);

    expected["/"]["Documents"] = Listed{ fs::EntryType::directory, 0 };

    Run("mkfs.fat -C " + GetParam().options + " " + image + " " + std::to_string(GetParam().kibibytes));
    Run(Mtools("mcopy -s -i " + image + " '" + files.string() + "'/* ::/"));

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(image) };
    device.LoadFrom(image);
    fs::FatFileSystem::WithStorage<1, 4> volume{ device };
    fs::FatReader::WithStorage<1> fatReader{ device };

    for (auto [engine, fileSystem] : { std::pair<const char*, fs::FileSystem*>{ "FatFileSystem", &volume }, std::pair<const char*, fs::FileSystem*>{ "FatReader", &fatReader } })
    {
        SCOPED_TRACE(engine);
        ASSERT_EQ(fs::Result::success, Lifecycle(*fileSystem, &fs::FileSystem::Mount));

        EXPECT_EQ(expected["/"], List(*fileSystem, "/"));
        EXPECT_EQ(expected["/Documents"], List(*fileSystem, "/Documents"));

        EXPECT_EQ(fs::Result::success, Lifecycle(*fileSystem, &fs::FileSystem::Unmount));
    }
}

INSTANTIATE_TEST_SUITE_P(Images, ListingInterop,
    testing::Values(ListingCase{ "fat12", "-F 12", 4096 },
        ListingCase{ "fat16", "-F 16 -s 1", 8192 },
        ListingCase{ "fat32", "-F 32 -s 1", 40960 }),
    CaseName);
