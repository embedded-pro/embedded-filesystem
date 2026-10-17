#include "fs/nor/lite/test/NorLiteFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>

namespace
{
    constexpr fs::NorConfig writerConfig{ .maxBlocks = 32, .cachedNodes = 24, .journalBlocks = 4 };
    constexpr fs::NorConfig readerConfig{ .maxBlocks = 32, .cachedNodes = 15, .journalBlocks = 4 };

    class Writer
        : public fs::NorFileSystem::WithStorage<writerConfig, 1>
    {
    public:
        using fs::NorFileSystem::WithStorage<writerConfig, 1>::WithStorage;
        using fs::NorFileSystem::Volume;
    };

    class Reader
        : public fs::NorLite::WithStorage<readerConfig, 1>
    {
    public:
        using fs::NorLite::WithStorage<readerConfig, 1>::WithStorage;
        using fs::NorLite::Volume;
    };

    infra::BoundedString::WithStorage<8> DirectoryPath(std::size_t index)
    {
        infra::BoundedString::WithStorage<8> path{ "/d" };
        path.push_back(static_cast<char>('a' + index / 26));
        path.push_back(static_cast<char>('a' + index % 26));
        return path;
    }

    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern(uint8_t seed = 0)
    {
        std::array<uint8_t, Size> pattern{};
        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 7 + seed);
        return pattern;
    }

    template<class Range>
    fs::NorLiteFixture::Contents ContentsOf(const Range& range)
    {
        return fs::NorLiteFixture::Contents(range.begin(), range.end());
    }

    class NorLiteTest
        : public fs::NorLiteFixture
    {};
}

TEST_F(NorLiteTest, mounts_a_volume_that_the_full_volume_formatted)
{
    EXPECT_EQ(fs::Result::success, Mount(lite));
    EXPECT_EQ(fs::Result::success, Unmount(lite));
}

TEST_F(NorLiteTest, is_idle_unless_a_call_waits_or_runs)
{
    EXPECT_TRUE(lite.Idle());

    std::optional<fs::Result> mounted;
    lite.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    EXPECT_FALSE(lite.Idle());
    ExecuteFirstAction();
    EXPECT_FALSE(lite.Idle());

    ExecuteAllActions();
    EXPECT_EQ(fs::Result::success, mounted);
    EXPECT_TRUE(lite.Idle());
}

TEST_F(NorLiteTest, a_flash_that_was_never_formatted_does_not_mount)
{
    hal::FlashStub blank{ 32, 4096 };
    fs::ExposedNorLite unformatted{ blank };

    EXPECT_EQ(fs::Result::corrupted, Mount(unformatted));
    EXPECT_EQ(fs::Result::notMounted, Unmount(unformatted));
    EXPECT_EQ(fs::Result::corrupted, Mount(unformatted));
}

TEST_F(NorLiteTest, a_second_mount_or_unmount_while_mounting_completes_with_busy)
{
    std::optional<fs::Result> mounted;
    std::optional<fs::Result> mountedAgain;
    std::optional<fs::Result> unmounted;

    lite.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    lite.Mount([&mountedAgain](fs::Result result)
        {
            mountedAgain = result;
        });
    lite.Unmount([&unmounted](fs::Result result)
        {
            unmounted = result;
        });
    ExecuteAllActions();

    EXPECT_EQ(fs::Result::success, mounted);
    EXPECT_EQ(fs::Result::busy, mountedAgain);
    EXPECT_EQ(fs::Result::busy, unmounted);
}

TEST_F(NorLiteTest, a_mount_with_a_smaller_cache_than_the_writer_checkpoints_during_replay)
{
    hal::FlashStub device{ 32, 4096 };
    Writer writer{ device };
    Reader reader{ device };
    ASSERT_EQ(fs::Result::success, Complete([&writer](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           writer.Format(onDone);
                                       }));
    ASSERT_EQ(fs::Result::success, Mount(writer));
    std::size_t made = 0;
    while (writer.Volume().Cache().ModifiedNodes() <= readerConfig.cachedNodes)
        ASSERT_EQ(fs::Result::success, MakeDirectory(writer, DirectoryPath(made++)));

    EXPECT_EQ(fs::Result::success, Mount(reader));

    const auto& state = reader.Volume().State();
    EXPECT_NE(state.checkpoint.recordsBlock, state.checkpointBlock);
    for (std::size_t index = 0; index != made; ++index)
        EXPECT_EQ(fs::EntryType::directory, Stat(reader, DirectoryPath(index)).second.type);
}

TEST_F(NorLiteTest, the_full_volume_reads_a_file_that_the_lite_volume_created)
{
    auto data = Pattern<5000>();
    ASSERT_EQ(fs::Result::success, Mount(lite));

    EXPECT_EQ(fs::Result::success, WriteFile(lite, "/log.bin", fs::OpenMode::write, infra::MakeRange(data)));
    ASSERT_EQ(fs::Result::success, Unmount(lite));

    ASSERT_EQ(fs::Result::success, Mount(full));
    EXPECT_EQ(ContentsOf(data), ReadFile(full, "/log.bin"));
}

TEST_F(NorLiteTest, reads_a_file_that_the_full_volume_wrote_into_a_directory)
{
    auto data = Pattern<300>(1);
    ASSERT_EQ(fs::Result::success, Mount(full));
    ASSERT_EQ(fs::Result::success, MakeDirectory(full, "/data"));
    ASSERT_EQ(fs::Result::success, WriteFile(full, "/data/settings", fs::OpenMode::write, infra::MakeRange(data)));
    ASSERT_EQ(fs::Result::success, Unmount(full));

    ASSERT_EQ(fs::Result::success, Mount(lite));
    EXPECT_EQ(ContentsOf(data), ReadFile(lite, "/data/settings"));
}

TEST_F(NorLiteTest, appends_to_the_end_of_a_file)
{
    auto first = Pattern<100>(2);
    auto second = Pattern<4500>(3);
    ASSERT_EQ(fs::Result::success, Mount(lite));
    ASSERT_EQ(fs::Result::success, WriteFile(lite, "/log", fs::OpenMode::write, infra::MakeRange(first)));

    EXPECT_EQ(fs::Result::success, WriteFile(lite, "/log", fs::OpenMode::append, infra::MakeRange(second)));

    auto expected = ContentsOf(first);
    expected.insert(expected.end(), second.begin(), second.end());
    EXPECT_EQ(expected, ReadFile(lite, "/log"));
}

TEST_F(NorLiteTest, opening_a_file_for_writing_truncates_it)
{
    auto first = Pattern<5000>(4);
    auto second = Pattern<10>(5);
    ASSERT_EQ(fs::Result::success, Mount(lite));
    ASSERT_EQ(fs::Result::success, WriteFile(lite, "/config", fs::OpenMode::write, infra::MakeRange(first)));

    EXPECT_EQ(fs::Result::success, WriteFile(lite, "/config", fs::OpenMode::write, infra::MakeRange(second)));

    EXPECT_EQ(ContentsOf(second), ReadFile(lite, "/config"));
}

TEST_F(NorLiteTest, a_write_before_the_end_of_a_file_and_a_truncation_complete_with_unsupported)
{
    auto data = Pattern<20>(6);
    ASSERT_EQ(fs::Result::success, Mount(lite));
    ASSERT_EQ(fs::Result::success, WriteFile(lite, "/file", fs::OpenMode::write, infra::MakeRange(data)));
    auto [opened, file] = Open(lite, "/file", fs::OpenMode::readWrite);
    ASSERT_EQ(fs::Result::success, opened);

    EXPECT_EQ(fs::Result::unsupported, Write(*file, infra::MakeRange(data)));
    EXPECT_EQ(fs::Result::unsupported, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               file->Truncate(onDone);
                                           }));
    EXPECT_EQ(fs::Result::success, Close(*file));
}

TEST_F(NorLiteTest, stats_a_file)
{
    auto data = Pattern<123>(7);
    ASSERT_EQ(fs::Result::success, Mount(lite));
    ASSERT_EQ(fs::Result::success, WriteFile(lite, "/file", fs::OpenMode::write, infra::MakeRange(data)));

    auto [result, info] = Stat(lite, "/file");

    EXPECT_EQ(fs::Result::success, result);
    EXPECT_EQ(fs::EntryType::file, info.type);
    EXPECT_EQ(123, info.size);
    EXPECT_EQ(fs::Result::notFound, Stat(lite, "/missing").first);
}

TEST_F(NorLiteTest, a_path_call_while_another_one_runs_completes_with_busy)
{
    ASSERT_EQ(fs::Result::success, Mount(lite));
    std::optional<fs::Result> first;
    lite.Stat("/", [&first](fs::Result result, const fs::EntryInfo&)
        {
            first = result;
        });

    EXPECT_EQ(fs::Result::busy, Stat(lite, "/").first);
    EXPECT_EQ(fs::Result::success, first);
    EXPECT_EQ(fs::Result::success, Stat(lite, "/").first);
}

TEST_F(NorLiteTest, path_calls_complete_with_not_mounted_before_a_mount_and_validate_their_path_after_it)
{
    EXPECT_EQ(fs::Result::notMounted, Stat(lite, "/file").first);
    EXPECT_EQ(fs::Result::notMounted, Open(lite, "/file", fs::OpenMode::write).first);

    ASSERT_EQ(fs::Result::success, Mount(lite));

    EXPECT_EQ(fs::Result::invalidPath, Stat(lite, "file").first);
    EXPECT_EQ(fs::Result::invalidPath, Open(lite, "/a/../b", fs::OpenMode::write).first);
}

TEST_F(NorLiteTest, directories_removal_renaming_and_usage_complete_with_unsupported_once_mounted)
{
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    auto callAll = [this, &done]
    {
        lite.OpenDirectory("/", [&done](fs::Result result, infra::SharedPtr<fs::Directory>)
            {
                done.callback(result);
            });
        lite.Remove("/file", [&done](fs::Result result)
            {
                done.callback(result);
            });
        lite.Rename("/file", "/other", [&done](fs::Result result)
            {
                done.callback(result);
            });
        lite.MakeDirectory("/data", [&done](fs::Result result)
            {
                done.callback(result);
            });
        lite.Usage([&done](fs::Result result, fs::VolumeUsage)
            {
                done.callback(result);
            });
    };

    EXPECT_CALL(done, callback(fs::Result::notMounted)).Times(5);
    callAll();
    ExecuteAllActions();

    ASSERT_EQ(fs::Result::success, Mount(lite));
    EXPECT_CALL(done, callback(fs::Result::unsupported)).Times(5);
    callAll();
    ExecuteAllActions();
}

TEST_F(NorLiteTest, levels_wear_after_the_first_call_of_a_mount)
{
    auto data = Pattern<300>(8);
    ASSERT_EQ(fs::Result::success, Mount(lite));
    ASSERT_EQ(fs::Result::success, WriteFile(lite, "/cold", fs::OpenMode::write, infra::MakeRange(Pattern<4100>(9))));
    ASSERT_EQ(fs::Result::success, Unmount(lite));
    ASSERT_EQ(fs::Result::success, Mount(lite));
    auto& state = lite.Volume().State();
    for (uint32_t block = 2; block != 32; ++block)
        state.eraseCounts.Record(block, state.blocks.State(block) == fs::NorBlockState::inUse ? 1 : 100);

    EXPECT_EQ(fs::Result::success, Stat(lite, "/cold").first);

    EXPECT_EQ(1, lite.Counters().movedBlocks);
    EXPECT_EQ(fs::Result::success, WriteFile(lite, "/hot", fs::OpenMode::write, infra::MakeRange(data)));
}
