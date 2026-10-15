#include "fs/nor/NorFileSystem.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };

    class NorFileSystemTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        void StartFormat()
        {
            volume.Format([this](fs::Result result)
                {
                    formatDone.callback(result);
                });
        }

        void ExpectFormat(fs::Result result = fs::Result::success)
        {
            StartFormat();
            EXPECT_CALL(formatDone, callback(result));
            ExecuteAllActions();
        }

        void StartMount()
        {
            volume.Mount([this](fs::Result result)
                {
                    mountDone.callback(result);
                });
        }

        void ExpectMount(fs::Result result = fs::Result::success)
        {
            StartMount();
            EXPECT_CALL(mountDone, callback(result));
            ExecuteAllActions();
        }

        void StartUnmount()
        {
            volume.Unmount([this](fs::Result result)
                {
                    unmountDone.callback(result);
                });
        }

        void ExpectUnmount(fs::Result result = fs::Result::success)
        {
            StartUnmount();
            EXPECT_CALL(unmountDone, callback(result));
            ExecuteAllActions();
        }

        void ExpectLookupsCompleteWith(fs::Result result)
        {
            testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
            EXPECT_CALL(done, callback(result)).Times(4);
            volume.OpenDirectory("/a", [&done](fs::Result opened, infra::SharedPtr<fs::Directory> directory)
                {
                    EXPECT_EQ(nullptr, directory);
                    done.callback(opened);
                });
            volume.Stat("/a", [&done](fs::Result found, const fs::EntryInfo&)
                {
                    done.callback(found);
                });
            ExecuteAllActions();
            volume.Remove("/a", [&done](fs::Result removed)
                {
                    done.callback(removed);
                });
            ExecuteAllActions();
            volume.Rename("/a", "/b", [&done](fs::Result renamed)
                {
                    done.callback(renamed);
                });
            ExecuteAllActions();
        }

        void ExpectUsage(fs::Result result, fs::VolumeUsage expected)
        {
            testing::StrictMock<infra::MockCallback<void(fs::Result, fs::VolumeUsage)>> done;
            volume.Usage([&done](fs::Result measured, fs::VolumeUsage usage)
                {
                    done.callback(measured, usage);
                });
            testing::Mock::VerifyAndClearExpectations(&done);

            EXPECT_CALL(done, callback(result, expected));
            ExecuteAllActions();
        }

        void ExpectMakeDirectory(infra::BoundedConstString path, fs::Result result)
        {
            testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
            volume.MakeDirectory(path, [&done](fs::Result made)
                {
                    done.callback(made);
                });

            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        hal::FlashStub flash{ 32, 4096 };
        fs::NorFileSystem::WithStorage<config, 1> volume{ flash };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> formatDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> mountDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> unmountDone;
    };
}

TEST_F(NorFileSystemTest, mounts_a_volume_that_it_formatted)
{
    ExpectFormat();
    ExpectMount();
}

TEST_F(NorFileSystemTest, completes_format_and_mount_only_through_the_event_dispatcher)
{
    StartFormat();
    testing::Mock::VerifyAndClearExpectations(&formatDone);
    EXPECT_CALL(formatDone, callback(fs::Result::success));
    ExecuteAllActions();

    StartMount();
    testing::Mock::VerifyAndClearExpectations(&mountDone);
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorFileSystemTest, mounting_a_device_without_a_volume_completes_with_corrupted)
{
    ExpectMount(fs::Result::corrupted);
}

TEST_F(NorFileSystemTest, a_failed_mount_can_be_retried)
{
    ExpectMount(fs::Result::corrupted);
    ExpectMount(fs::Result::corrupted);
}

TEST_F(NorFileSystemTest, format_after_a_failed_mount_lets_the_next_mount_succeed)
{
    ExpectMount(fs::Result::corrupted);
    ExpectFormat();
    ExpectMount();
}

TEST_F(NorFileSystemTest, unmount_of_a_mounted_volume_succeeds_and_it_mounts_again)
{
    ExpectFormat();
    ExpectMount();
    ExpectUnmount();
    ExpectMount();
}

TEST_F(NorFileSystemTest, mount_while_mounted_completes_with_busy)
{
    ExpectFormat();
    ExpectMount();

    ExpectMount(fs::Result::busy);
}

TEST_F(NorFileSystemTest, mount_while_mounting_completes_with_busy_and_the_first_mount_succeeds)
{
    ExpectFormat();
    StartMount();
    volume.Mount([this](fs::Result result)
        {
            unmountDone.callback(result);
        });

    EXPECT_CALL(unmountDone, callback(fs::Result::busy));
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorFileSystemTest, unmount_while_unmounted_completes_with_not_mounted)
{
    ExpectUnmount(fs::Result::notMounted);
}

TEST_F(NorFileSystemTest, unmount_after_a_failed_mount_completes_with_not_mounted)
{
    ExpectMount(fs::Result::corrupted);

    ExpectUnmount(fs::Result::notMounted);
}

TEST_F(NorFileSystemTest, unmount_while_mounting_completes_with_busy)
{
    ExpectFormat();
    StartMount();
    StartUnmount();

    EXPECT_CALL(unmountDone, callback(fs::Result::busy));
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorFileSystemTest, format_while_mounted_completes_with_busy)
{
    ExpectFormat();
    ExpectMount();

    ExpectFormat(fs::Result::busy);
}

TEST_F(NorFileSystemTest, mount_and_format_while_formatting_complete_with_busy)
{
    StartFormat();
    StartMount();
    volume.Format([this](fs::Result result)
        {
            unmountDone.callback(result);
        });

    EXPECT_CALL(mountDone, callback(fs::Result::busy));
    EXPECT_CALL(unmountDone, callback(fs::Result::busy));
    EXPECT_CALL(formatDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorFileSystemTest, format_of_a_device_that_does_not_fit_completes_with_unsupported)
{
    flash.sectors.resize(8);

    ExpectFormat(fs::Result::unsupported);
    ExpectMount(fs::Result::unsupported);
}

TEST_F(NorFileSystemTest, calls_on_an_unmounted_volume_complete_with_not_mounted)
{
    ExpectLookupsCompleteWith(fs::Result::notMounted);
    ExpectUsage(fs::Result::notMounted, fs::VolumeUsage{});
    ExpectMakeDirectory("/a", fs::Result::notMounted);

    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    volume.Open("/a", fs::OpenMode::write, [&done](fs::Result opened, infra::SharedPtr<fs::File> file)
        {
            EXPECT_EQ(nullptr, file);
            done.callback(opened);
        });
    EXPECT_CALL(done, callback(fs::Result::notMounted));
    ExecuteAllActions();
}

TEST_F(NorFileSystemTest, lookups_after_unmount_complete_with_not_mounted)
{
    ExpectFormat();
    ExpectMount();
    ExpectUnmount();

    ExpectLookupsCompleteWith(fs::Result::notMounted);
}

TEST_F(NorFileSystemTest, lookups_on_a_mounted_volume_find_nothing_on_a_new_volume)
{
    ExpectFormat();
    ExpectMount();

    ExpectLookupsCompleteWith(fs::Result::notFound);
}

TEST_F(NorFileSystemTest, usage_counts_the_payload_of_every_block_but_the_superblocks_and_uses_all_but_the_free_blocks)
{
    constexpr uint64_t payload = 4096 - fs::NorHeaderSize(1);
    ExpectFormat();
    ExpectMount();

    ExpectUsage(fs::Result::success, fs::VolumeUsage{ 2 * payload, 30 * payload });
    ExpectMakeDirectory("/a", fs::Result::success);
    ExpectUsage(fs::Result::success, fs::VolumeUsage{ 3 * payload, 30 * payload });
}

TEST_F(NorFileSystemTest, makes_a_directory_that_a_lookup_then_finds)
{
    ExpectFormat();
    ExpectMount();

    ExpectMakeDirectory("/a", fs::Result::success);

    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    volume.Stat("/a", [&done](fs::Result found, const fs::EntryInfo& info)
        {
            EXPECT_EQ(fs::EntryType::directory, info.type);
            done.callback(found);
        });
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorFileSystemTest, a_directory_made_before_unmount_is_there_after_the_next_mount)
{
    ExpectFormat();
    ExpectMount();
    ExpectMakeDirectory("/a", fs::Result::success);
    ExpectUnmount();
    ExpectMount();

    ExpectMakeDirectory("/a", fs::Result::alreadyExists);
}
