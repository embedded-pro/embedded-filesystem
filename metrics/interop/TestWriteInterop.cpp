#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/lite/FatLite.hpp"
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
#include <set>
#include <sstream>
#include <string>
#include <utility>
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

    void ExpectFsckAndStrictCheck()
    {
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
    }

    template<class Volume>
    void ExpectAppendedFileReadsBack();

    std::filesystem::path directory;
    std::string image;
};

template<class Volume>
void WriteInterop::ExpectAppendedFileReadsBack()
{
    auto original = Pattern(3000, 1);
    std::ofstream{ directory / "log.bin", std::ios::binary }.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
    ASSERT_EQ(0, Run("mkfs.fat -C " + GetParam().options + " " + image + " " + std::to_string(GetParam().kibibytes)).exitCode);
    ASSERT_EQ(0, Run("mcopy -i " + image + " '" + (directory / "log.bin").string() + "' ::/").exitCode);

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(image) };
    device.LoadFrom(image);
    Volume volume{ device };
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
    ExpectFsckAndStrictCheck();

    ASSERT_EQ(0, Run("mcopy -n -i " + image + " ::/log.bin '" + (directory / "back.bin").string() + "'").exitCode);
    std::ifstream back{ directory / "back.bin", std::ios::binary };
    std::vector<uint8_t> read{ std::istreambuf_iterator<char>(back), std::istreambuf_iterator<char>() };
    EXPECT_EQ(expected, read);
}

TEST_P(WriteInterop, appended_file_passes_fsck_and_reads_back_with_mtools)
{
    ExpectAppendedFileReadsBack<fs::FatFileSystem::WithStorage<1, 8>>();
}

TEST_P(WriteInterop, a_file_appended_by_the_lite_volume_passes_fsck_and_reads_back_with_mtools)
{
    ExpectAppendedFileReadsBack<fs::FatLite::WithStorage<1>>();
}

TEST_P(WriteInterop, files_the_lite_volume_creates_truncates_and_writes_list_in_mtools_and_pass_fsck)
{
    ASSERT_EQ(0, Run("mkfs.fat -C " + GetParam().options + " " + image + " " + std::to_string(GetParam().kibibytes)).exitCode);
    ASSERT_EQ(0, Run("mmd -i " + image + " ::/dir").exitCode);

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(image) };
    device.LoadFrom(image);
    fs::FatLite::WithStorage<1> volume{ device };
    ASSERT_EQ(fs::Result::success, Wait([&volume](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           volume.Mount(onDone);
                                       }));

    std::vector<std::string> names{ "/readme.txt", "/DATA.BIN", "/log" };
    for (uint32_t index = 0; index != 40; ++index)
        names.push_back("/dir/file" + std::to_string(index) + ".dat");

    auto contents = Pattern(5000, 3);
    for (const auto& name : names)
    {
        infra::SharedPtr<fs::File> file;
        volume.Open(infra::BoundedConstString(name.data(), name.size()), fs::OpenMode::readWrite, [&file](fs::Result result, infra::SharedPtr<fs::File> opened)
            {
                EXPECT_EQ(fs::Result::success, result);
                file = opened;
            });
        ExecuteAllActions();
        ASSERT_NE(nullptr, file) << name;

        EXPECT_EQ(fs::Result::success, Wait([&file, &contents](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               file->Write(infra::MakeRange(contents), onDone);
                                           }));
        file->Seek(1234);
        EXPECT_EQ(fs::Result::success, Wait([&file](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               file->Truncate(onDone);
                                           }));
        EXPECT_EQ(fs::Result::success, Wait([&file](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               file->Close(onDone);
                                           }));
    }

    EXPECT_EQ(fs::Result::success, Wait([&volume](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           volume.Unmount(onDone);
                                       }));
    device.Save(image);
    ExpectFsckAndStrictCheck();

    std::set<std::string> listed;
    for (const auto* folder : { "::/", "::/dir/" })
    {
        auto listing = Run(std::string("mdir -b -/ -i ") + image + " " + folder);
        ASSERT_EQ(0, listing.exitCode) << listing.output;
        std::istringstream lines{ listing.output };
        for (std::string line; std::getline(lines, line);)
            if (line.starts_with("::") && !line.ends_with("/"))
                listed.insert(line.substr(2));
    }
    EXPECT_EQ(std::set<std::string>(names.begin(), names.end()), listed);

    ASSERT_EQ(0, Run("mcopy -n -i " + image + " ::/dir/file39.dat '" + (directory / "back.bin").string() + "'").exitCode);
    std::ifstream back{ directory / "back.bin", std::ios::binary };
    std::vector<uint8_t> read{ std::istreambuf_iterator<char>(back), std::istreambuf_iterator<char>() };
    EXPECT_EQ(std::vector<uint8_t>(contents.begin(), contents.begin() + 1234), read);
}

TEST_P(WriteInterop, created_files_list_in_mtools_and_pass_fsck)
{
    ASSERT_EQ(0, Run("mkfs.fat -C " + GetParam().options + " " + image + " " + std::to_string(GetParam().kibibytes)).exitCode);
    ASSERT_EQ(0, Run("mmd -i " + image + " ::/dir").exitCode);

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(image) };
    device.LoadFrom(image);
    fs::FatFileSystem::WithStorage<1, 8> volume{ device };
    ASSERT_EQ(fs::Result::success, Wait([&volume](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           volume.Mount(onDone);
                                       }));

    std::vector<std::string> names{ "/readme.txt", "/Long File Name.txt", "/Long File Name 2.txt", "/\xc3\x9c"
                                                                                                   "n"
                                                                                                   "\xc3\xaf"
                                                                                                   "c"
                                                                                                   "\xc3\xb6"
                                                                                                   "d"
                                                                                                   "\xc3\xa9"
                                                                                                   " name.txt",
        "/a+b,c;d.txt", "/MixedCase.Txt", "/.hidden name", "/" + std::string(200, 'x') + ".txt" };
    for (uint32_t index = 0; index != 40; ++index)
        names.push_back("/dir/file " + std::to_string(index) + ".dat");

    auto contents = Pattern(2000, 3);
    for (const auto& name : names)
    {
        infra::SharedPtr<fs::File> file;
        volume.Open(infra::BoundedConstString(name.data(), name.size()), fs::OpenMode::write, [&file](fs::Result result, infra::SharedPtr<fs::File> opened)
            {
                EXPECT_EQ(fs::Result::success, result);
                file = opened;
            });
        ExecuteAllActions();
        ASSERT_NE(nullptr, file) << name;

        if (name == "/Long File Name.txt")
            EXPECT_EQ(fs::Result::success, Wait([&file, &contents](const infra::Function<void(fs::Result)>& onDone)
                                               {
                                                   file->Write(infra::MakeRange(contents), onDone);
                                               }));

        EXPECT_EQ(fs::Result::success, Wait([&file](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               file->Close(onDone);
                                           }));
    }

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

    auto listing = Run("mdir -b -/ -i " + image + " ::/");
    ASSERT_EQ(0, listing.exitCode) << listing.output;
    std::set<std::string> listed;
    std::istringstream lines{ listing.output };
    for (std::string line; std::getline(lines, line);)
        if (line.starts_with("::") && !line.ends_with("/"))
            listed.insert(line.substr(2));
    EXPECT_EQ(std::set<std::string>(names.begin(), names.end()), listed);

    auto aliases = Run("mdir -i " + image + " ::/");
    EXPECT_NE(std::string::npos, aliases.output.find("LONGFI~1 TXT")) << aliases.output;
    EXPECT_NE(std::string::npos, aliases.output.find("LONGFI~2 TXT")) << aliases.output;
    EXPECT_NE(std::string::npos, aliases.output.find("readme   txt")) << aliases.output;

    ASSERT_EQ(0, Run("mcopy -n -i " + image + " '::/Long File Name.txt' '" + (directory / "back.bin").string() + "'").exitCode);
    std::ifstream back{ directory / "back.bin", std::ios::binary };
    std::vector<uint8_t> read{ std::istreambuf_iterator<char>(back), std::istreambuf_iterator<char>() };
    EXPECT_EQ(contents, read);
}

TEST_P(WriteInterop, made_directories_and_removed_entries_pass_fsck)
{
    auto original = Pattern(5000, 2);
    std::ofstream{ directory / "an old long name.txt", std::ios::binary }.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
    std::ofstream{ directory / "kept.txt", std::ios::binary }.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
    ASSERT_EQ(0, Run("mkfs.fat -C " + GetParam().options + " " + image + " " + std::to_string(GetParam().kibibytes)).exitCode);
    ASSERT_EQ(0, Run("mmd -i " + image + " ::/old").exitCode);
    ASSERT_EQ(0, Run("mcopy -i " + image + " '" + (directory / "an old long name.txt").string() + "' '" + (directory / "kept.txt").string() + "' ::/").exitCode);
    ASSERT_EQ(0, Run("mcopy -i " + image + " '" + (directory / "kept.txt").string() + "' ::/old/").exitCode);

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(image) };
    device.LoadFrom(image);
    fs::FatFileSystem::WithStorage<1, 8> volume{ device };
    auto call = [this](const auto& start)
    {
        return Wait(start);
    };
    ASSERT_EQ(fs::Result::success, call([&volume](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           volume.Mount(onDone);
                                       }));

    for (const char* path : { "/New Folder", "/New Folder/Sub", "/another" })
        EXPECT_EQ(fs::Result::success, call([&volume, path](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               volume.MakeDirectory(path, onDone);
                                           }))
            << path;

    infra::SharedPtr<fs::File> file;
    volume.Open("/New Folder/Sub/inside.bin", fs::OpenMode::write, [&file](fs::Result result, infra::SharedPtr<fs::File> opened)
        {
            EXPECT_EQ(fs::Result::success, result);
            file = opened;
        });
    ExecuteAllActions();
    ASSERT_NE(nullptr, file);
    EXPECT_EQ(fs::Result::success, call([&file, &original](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           file->Write(infra::MakeRange(original), onDone);
                                       }));
    EXPECT_EQ(fs::Result::success, call([&file](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           file->Close(onDone);
                                       }));
    file = nullptr;

    for (const char* path : { "/an old long name.txt", "/old/kept.txt", "/old", "/another" })
        EXPECT_EQ(fs::Result::success, call([&volume, path](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               volume.Remove(path, onDone);
                                           }))
            << path;

    EXPECT_EQ(fs::Result::success, call([&volume](const infra::Function<void(fs::Result)>& onDone)
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
    EXPECT_EQ(0u, check.orphanedLongNames);

    auto listing = Run("mdir -b -/ -i " + image + " ::/");
    ASSERT_EQ(0, listing.exitCode) << listing.output;
    std::set<std::string> listed;
    std::istringstream lines{ listing.output };
    for (std::string line; std::getline(lines, line);)
        if (line.starts_with("::"))
            listed.insert(line.substr(2));
    EXPECT_EQ((std::set<std::string>{ "/kept.txt", "/New Folder/", "/New Folder/Sub/", "/New Folder/Sub/inside.bin" }), listed);

    ASSERT_EQ(0, Run("mcopy -n -i " + image + " '::/New Folder/Sub/inside.bin' '" + (directory / "back.bin").string() + "'").exitCode);
    std::ifstream back{ directory / "back.bin", std::ios::binary };
    std::vector<uint8_t> read{ std::istreambuf_iterator<char>(back), std::istreambuf_iterator<char>() };
    EXPECT_EQ(original, read);
}

TEST_P(WriteInterop, renamed_and_truncated_entries_pass_fsck)
{
    auto original = Pattern(20000, 4);
    std::ofstream{ directory / "an old long name.txt", std::ios::binary }.write(reinterpret_cast<const char*>(original.data()), 5000);
    std::ofstream{ directory / "kept.txt", std::ios::binary }.write(reinterpret_cast<const char*>(original.data()), 5000);
    std::ofstream{ directory / "big.bin", std::ios::binary }.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
    ASSERT_EQ(0, Run("mkfs.fat -C " + GetParam().options + " " + image + " " + std::to_string(GetParam().kibibytes)).exitCode);
    ASSERT_EQ(0, Run("mmd -i " + image + " ::/old ::/old/inner").exitCode);
    ASSERT_EQ(0, Run("mcopy -i " + image + " '" + (directory / "an old long name.txt").string() + "' '" + (directory / "kept.txt").string() + "' '" + (directory / "big.bin").string() + "' ::/").exitCode);
    ASSERT_EQ(0, Run("mcopy -i " + image + " '" + (directory / "kept.txt").string() + "' ::/old/inner/").exitCode);

    metrics::ImageBlockDevice device{ metrics::BlocksInImage(image) };
    device.LoadFrom(image);
    fs::FatFileSystem::WithStorage<1, 8> volume{ device };
    auto call = [this](const auto& start)
    {
        return Wait(start);
    };
    ASSERT_EQ(fs::Result::success, call([&volume](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           volume.Mount(onDone);
                                       }));

    for (const auto& [from, to] : std::vector<std::pair<const char*, const char*>>{ { "/an old long name.txt", "/a new long name.txt" }, { "/old/inner", "/inner moved" }, { "/kept.txt", "/old/KEPT.TXT" } })
        EXPECT_EQ(fs::Result::success, call([&volume, from, to](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               volume.Rename(from, to, onDone);
                                           }))
            << from;

    auto open = [this, &volume](const char* path, fs::OpenMode mode)
    {
        infra::SharedPtr<fs::File> file;
        volume.Open(path, mode, [&file](fs::Result result, infra::SharedPtr<fs::File> opened)
            {
                EXPECT_EQ(fs::Result::success, result);
                file = opened;
            });
        ExecuteAllActions();
        return file;
    };

    auto big = open("/big.bin", fs::OpenMode::readWrite);
    ASSERT_NE(nullptr, big);
    big->Seek(7000);
    EXPECT_EQ(fs::Result::success, call([&big](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           big->Truncate(onDone);
                                       }));
    EXPECT_EQ(fs::Result::success, call([&big](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           big->Close(onDone);
                                       }));
    big = nullptr;

    auto rewritten = Pattern(300, 9);
    auto kept = open("/old/KEPT.TXT", fs::OpenMode::write);
    ASSERT_NE(nullptr, kept);
    EXPECT_EQ(fs::Result::success, call([&kept, &rewritten](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           kept->Write(infra::MakeRange(rewritten), onDone);
                                       }));
    EXPECT_EQ(fs::Result::success, call([&kept](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           kept->Close(onDone);
                                       }));
    kept = nullptr;

    EXPECT_EQ(fs::Result::success, call([&volume](const infra::Function<void(fs::Result)>& onDone)
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
    EXPECT_EQ(0u, check.orphanedLongNames);

    auto listing = Run("mdir -b -/ -i " + image + " ::/");
    ASSERT_EQ(0, listing.exitCode) << listing.output;
    std::set<std::string> listed;
    std::istringstream lines{ listing.output };
    for (std::string line; std::getline(lines, line);)
        if (line.starts_with("::"))
            listed.insert(line.substr(2));
    EXPECT_EQ((std::set<std::string>{ "/a new long name.txt", "/big.bin", "/inner moved/", "/inner moved/kept.txt", "/old/", "/old/KEPT.TXT" }), listed);

    auto readBack = [this](const std::string& path)
    {
        std::filesystem::remove(directory / "back.bin");
        EXPECT_EQ(0, Run("mcopy -n -i " + image + " '::" + path + "' '" + (directory / "back.bin").string() + "'").exitCode) << path;
        std::ifstream back{ directory / "back.bin", std::ios::binary };
        return std::vector<uint8_t>{ std::istreambuf_iterator<char>(back), std::istreambuf_iterator<char>() };
    };
    EXPECT_EQ(std::vector<uint8_t>(original.begin(), original.begin() + 7000), readBack("/big.bin"));
    EXPECT_EQ(std::vector<uint8_t>(original.begin(), original.begin() + 5000), readBack("/a new long name.txt"));
    EXPECT_EQ(std::vector<uint8_t>(original.begin(), original.begin() + 5000), readBack("/inner moved/kept.txt"));
    EXPECT_EQ(rewritten, readBack("/old/KEPT.TXT"));
}

INSTANTIATE_TEST_SUITE_P(Images, WriteInterop,
    testing::Values(WriteCase{ "fat12", "-F 12 -s 2", 4096 },
        WriteCase{ "fat16", "-F 16 -s 1", 16384 },
        WriteCase{ "fat32", "-F 32 -s 1", 40960 }),
    CaseName);
