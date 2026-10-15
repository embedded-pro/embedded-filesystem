#include "fs/nor/test/NorRecordsFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>

namespace
{
    constexpr fs::NorConfig writerConfig{ .maxBlocks = 32, .cachedNodes = 24 };
    constexpr fs::NorConfig readerConfig{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    class NorReplayTest
        : public fs::NorRecordsFixture<writerConfig, 32, 4096>
    {
    public:
        template<class FileSystem>
        fs::Result MountWith(FileSystem& fileSystem)
        {
            std::optional<fs::Result> mounted;
            fileSystem.Mount([&mounted](fs::Result result)
                {
                    mounted = result;
                });
            ExecuteAllActions();
            return mounted.value_or(fs::Result::busy);
        }

        template<class FileSystem>
        std::optional<fs::EntryType> TypeIn(FileSystem& fileSystem, infra::BoundedConstString path)
        {
            std::optional<fs::EntryType> type;
            fileSystem.Stat(path, [&type](fs::Result result, const fs::EntryInfo& info)
                {
                    if (result == fs::Result::success)
                        type = info.type;
                });
            ExecuteAllActions();
            return type;
        }

        infra::BoundedConstString NameOf(uint32_t index)
        {
            names[index] = { '/', 'd', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
            return infra::BoundedConstString(names[index].data(), names[index].size());
        }

        uint32_t MakeDirectoriesUntilTheJournalNeedsMoreThanTheReaderCache()
        {
            Mount();
            uint32_t made = 0;
            while (Volume().Cache().ModifiedNodes() <= readerConfig.cachedNodes)
                EXPECT_EQ(fs::Result::success, MakeDirectory(NameOf(made++)));

            return made;
        }

        template<class FileSystem>
        void ExpectDirectories(FileSystem& fileSystem, uint32_t made)
        {
            for (uint32_t index = 0; index != made; ++index)
                EXPECT_EQ(fs::EntryType::directory, TypeIn(fileSystem, NameOf(index))) << index;
        }

        std::array<std::array<char, 4>, 60> names{};
        fs::ExposedNorFileSystem<readerConfig> reader{ device };
    };
}

TEST_F(NorReplayTest, a_mount_with_the_cache_of_the_writer_writes_nothing)
{
    auto made = MakeDirectoriesUntilTheJournalNeedsMoreThanTheReaderCache();
    fs::ExposedNorFileSystem<writerConfig> sameCache{ device };
    device.recording = true;

    ASSERT_EQ(fs::Result::success, MountWith(sameCache));

    EXPECT_TRUE(device.requests.empty());
    ExpectDirectories(sameCache, made);
}

TEST_F(NorReplayTest, a_mount_whose_cache_cannot_hold_the_journal_writes_a_checkpoint_whose_records_start_in_the_journal)
{
    auto made = MakeDirectoriesUntilTheJournalNeedsMoreThanTheReaderCache();
    auto writerCheckpoint = State().checkpointBlock;

    ASSERT_EQ(fs::Result::success, MountWith(reader));

    const auto& state = reader.Volume().State();
    EXPECT_NE(writerCheckpoint, state.checkpointBlock);
    EXPECT_NE(state.checkpointBlock, state.checkpoint.recordsBlock);
    ExpectDirectories(reader, made);
}

TEST_F(NorReplayTest, after_the_checkpoint_of_a_replay_both_caches_mount_the_volume)
{
    auto made = MakeDirectoriesUntilTheJournalNeedsMoreThanTheReaderCache();
    ASSERT_EQ(fs::Result::success, MountWith(reader));

    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    ASSERT_EQ(fs::Result::success, MountWith(smallAgain));
    ExpectDirectories(smallAgain, made);

    fs::ExposedNorFileSystem<writerConfig> large{ device };
    ASSERT_EQ(fs::Result::success, MountWith(large));
    ExpectDirectories(large, made);
}

TEST_F(NorReplayTest, the_volume_takes_changes_after_a_mount_that_wrote_the_checkpoint_of_a_replay)
{
    auto made = MakeDirectoriesUntilTheJournalNeedsMoreThanTheReaderCache();
    ASSERT_EQ(fs::Result::success, MountWith(reader));

    std::optional<fs::Result> result;
    reader.MakeDirectory("/new", [&result](fs::Result made)
        {
            result = made;
        });
    ExecuteAllActions();
    ASSERT_EQ(fs::Result::success, result);

    fs::ExposedNorFileSystem<readerConfig> smallAgain{ device };
    ASSERT_EQ(fs::Result::success, MountWith(smallAgain));
    ExpectDirectories(smallAgain, made);
    EXPECT_EQ(fs::EntryType::directory, TypeIn(smallAgain, "/new"));
}

TEST_F(NorReplayTest, a_record_that_does_not_fit_an_empty_cache_makes_the_mount_unsupported)
{
    Mount();
    for (uint32_t index = 0; index != 15; ++index)
        ASSERT_EQ(fs::Result::success, MakeDirectory(NameOf(index)));
    std::optional<fs::Result> written;
    Volume().Checkpoints().Write([&written](fs::Result result)
        {
            written = result;
        });
    ExecuteAllActions();
    ASSERT_EQ(fs::Result::success, written);

    ASSERT_EQ(fs::Result::success, Commit({ fs::NorDirectoryEntryPut{ 2, 0, 3 }, fs::NorDirectoryEntryPut{ 3, 0, 3 }, fs::NorDirectoryEntryPut{ 4, 0, 3 }, fs::NorDirectoryEntryPut{ 5, 0, 3 }, fs::NorDirectoryEntryPut{ 6, 0, 3 }, fs::NorDirectoryEntryPut{ 7, 0, 3 }, fs::NorDirectoryEntryPut{ 8, 0, 3 }, fs::NorDirectoryEntryPut{ 9, 0, 3 }, fs::NorDirectoryEntryPut{ 10, 0, 3 }, fs::NorDirectoryEntryPut{ 11, 0, 3 }, fs::NorDirectoryEntryPut{ 12, 0, 3 }, fs::NorDirectoryEntryPut{ 13, 0, 3 }, fs::NorDirectoryEntryPut{ 14, 0, 3 }, fs::NorDirectoryEntryPut{ 15, 0, 3 }, fs::NorDirectoryEntryPut{ 16, 0, 3 } }));
    ASSERT_LT(readerConfig.cachedNodes, Volume().Cache().ModifiedNodes());

    EXPECT_EQ(fs::Result::unsupported, MountWith(reader));
}
