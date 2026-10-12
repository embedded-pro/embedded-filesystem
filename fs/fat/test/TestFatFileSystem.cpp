#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <optional>

namespace
{
    using Sector = std::array<uint8_t, 512>;

    struct VolumeCase
    {
        const char* name;
        uint32_t blocks;
        fs::FatFormatConfig config;
        uint32_t mountReads;
    };

    fs::FatFormatConfig ClusterSize(uint8_t sectors, bool partitioned = true)
    {
        fs::FatFormatConfig config;
        config.sectorsPerCluster = sectors;
        config.partitioned = partitioned;
        return config;
    }

    void PrintTo(const VolumeCase& volumeCase, std::ostream* stream)
    {
        *stream << volumeCase.name;
    }

    class FatFileSystemTestBase
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        explicit FatFileSystemTestBase(uint32_t blocks)
            : device(blocks)
        {}

        void Format(const fs::FatFormatConfig& config = {})
        {
            std::optional<fs::Result> result;
            formatter.Format(config, [&result](fs::Result formatted)
                {
                    result = formatted;
                });
            ExecuteAllActions();
            ASSERT_EQ(fs::Result::success, result);
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
            testing::Mock::VerifyAndClearExpectations(&mountDone);
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

        void StartStat(infra::BoundedConstString path)
        {
            volume.Stat(path, [this](fs::Result result, const fs::EntryInfo& info)
                {
                    statName = info.name;
                    statDone.callback(result, info.type, info.size);
                });
        }

        void ExpectStat(infra::BoundedConstString path, fs::Result result, fs::EntryType type = fs::EntryType::file)
        {
            StartStat(path);
            EXPECT_CALL(statDone, callback(result, type, 0));
            ExecuteAllActions();
            EXPECT_TRUE(statName.empty());
        }

        void ExpectUnsupportedCallsCompleteWith(fs::Result result)
        {
            testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
            volume.Remove("/a", [&done](fs::Result result)
                {
                    done.callback(result);
                });
            volume.Rename("/a", "/b", [&done](fs::Result result)
                {
                    done.callback(result);
                });
            volume.MakeDirectory("/a", [&done](fs::Result result)
                {
                    done.callback(result);
                });
            volume.Usage([&done](fs::Result result, fs::VolumeUsage usage)
                {
                    EXPECT_EQ(fs::VolumeUsage{}, usage);
                    done.callback(result);
                });

            EXPECT_CALL(done, callback(result)).Times(4);
            ExecuteAllActions();
        }

        void ExpectEveryCallCompletesWith(fs::Result result)
        {
            ExpectUnsupportedCallsCompleteWith(result);

            testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
            volume.OpenDirectory("/", [&done](fs::Result result, infra::SharedPtr<fs::Directory> directory)
                {
                    EXPECT_EQ(nullptr, directory);
                    done.callback(result);
                });
            volume.Open("/a", fs::OpenMode::read, [&done](fs::Result result, infra::SharedPtr<fs::File> file)
                {
                    EXPECT_EQ(nullptr, file);
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result)).Times(2);
            ExecuteAllActions();
        }

        Sector Read(uint32_t block) const
        {
            Sector sector;
            device.ReadBlock(block, infra::MakeRange(sector));
            return sector;
        }

        void Write(uint32_t block, const Sector& sector)
        {
            device.WriteBlock(block, infra::MakeRange(sector));
        }

        fs::SparseBlockDevice::WithStorage<32> device;
        fs::FatFormatter::WithStorage<8> formatter{ device };
        fs::FatFileSystem::WithStorage<1, 4> volume{ device };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> mountDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> unmountDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result, fs::EntryType, uint32_t)>> statDone;
        infra::BoundedString::WithStorage<32> statName;
    };

    class FatFileSystemTest
        : public FatFileSystemTestBase
    {
    public:
        FatFileSystemTest()
            : FatFileSystemTestBase(4096)
        {}
    };

    class FatMountTest
        : public FatFileSystemTestBase
        , public testing::WithParamInterface<VolumeCase>
    {
    public:
        FatMountTest()
            : FatFileSystemTestBase(GetParam().blocks)
        {}
    };
}

TEST_P(FatMountTest, mounts_and_reads_only_the_metadata_it_needs)
{
    Format(GetParam().config);

    auto writes = device.WriteRequests();

    ExpectMount();

    EXPECT_EQ(GetParam().mountReads, device.ReadRequests());
    EXPECT_EQ(writes, device.WriteRequests());
    ExpectStat("/", fs::Result::success, fs::EntryType::directory);
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatMountTest,
    testing::Values(VolumeCase{ "fat12", 4096, {}, 2 },
        VolumeCase{ "fat16", 16384, ClusterSize(1), 2 },
        VolumeCase{ "fat16_without_partition_table", 16384, ClusterSize(1, false), 1 },
        VolumeCase{ "fat32", 81920, ClusterSize(1), 4 }));

TEST_F(FatFileSystemTest, calls_before_mount_are_not_mounted)
{
    ExpectEveryCallCompletesWith(fs::Result::notMounted);
    ExpectStat("/", fs::Result::notMounted);
    ExpectUnmount(fs::Result::notMounted);
}

TEST_F(FatFileSystemTest, mount_completes_from_the_event_dispatcher)
{
    Format();

    ExpectMount();
}

TEST_F(FatFileSystemTest, unformatted_device_is_corrupted)
{
    ExpectMount(fs::Result::corrupted);
    ExpectStat("/", fs::Result::notMounted);
}

TEST_F(FatFileSystemTest, protective_gpt_is_unsupported)
{
    Sector mbr{};
    mbr[446 + 4] = 0xee;
    mbr[446 + 12] = 0xff;
    mbr[510] = 0x55;
    mbr[511] = 0xaa;
    Write(0, mbr);

    ExpectMount(fs::Result::unsupported);
}

TEST_F(FatFileSystemTest, partition_beyond_the_device_is_corrupted)
{
    Format();
    auto mbr = Read(0);
    mbr[446 + 13] = 0x10;
    Write(0, mbr);

    ExpectMount(fs::Result::corrupted);
}

TEST_F(FatFileSystemTest, partition_starting_beyond_the_device_is_corrupted)
{
    Format();
    auto mbr = Read(0);
    mbr[446 + 9] = 0x10;
    Write(0, mbr);

    ExpectMount(fs::Result::corrupted);
}

TEST_F(FatFileSystemTest, invalid_boot_sector_in_a_partition_is_corrupted)
{
    Format();
    Write(13, Sector{});

    ExpectMount(fs::Result::corrupted);
}

TEST_F(FatFileSystemTest, inconsistent_boot_sector_reports_its_geometry_result)
{
    Format();
    auto boot = Read(13);
    boot[0x20] = 0xff;
    boot[0x21] = 0xff;
    boot[0x13] = 0;
    boot[0x14] = 0;
    Write(13, boot);

    ExpectMount(fs::Result::corrupted);
}

TEST_F(FatFileSystemTest, block_size_other_than_the_cache_sector_is_unsupported)
{
    fs::SparseBlockDevice::WithStorage<1, 4096> largeBlocks{ 1024 };
    fs::FatFileSystem::WithStorage<1, 4> largeVolume{ largeBlocks };

    largeVolume.Mount([this](fs::Result result)
        {
            mountDone.callback(result);
        });
    EXPECT_CALL(mountDone, callback(fs::Result::unsupported));
    ExecuteAllActions();

    EXPECT_EQ(0u, largeBlocks.ReadRequests());
}

TEST_F(FatFileSystemTest, device_error_faults_and_a_new_mount_succeeds)
{
    Format();
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);
    ExpectMount(fs::Result::deviceError);
    ExpectStat("/", fs::Result::notMounted);

    ExpectMount();
    ExpectStat("/", fs::Result::success, fs::EntryType::directory);
}

TEST_F(FatFileSystemTest, removed_card_is_reported_until_the_next_mount)
{
    Format();
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);
    ExpectMount(fs::Result::mediaRemoved);

    ExpectEveryCallCompletesWith(fs::Result::mediaRemoved);
    ExpectStat("/", fs::Result::mediaRemoved);
    ExpectUnmount(fs::Result::mediaRemoved);

    ExpectMount();
}

TEST_F(FatFileSystemTest, failure_reading_the_boot_sector_of_a_partition_is_reported)
{
    Format();
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 1);

    ExpectMount(fs::Result::deviceError);
}

TEST_F(FatFileSystemTest, mount_while_mounting_or_mounted_is_busy)
{
    Format();
    StartMount();
    StartMount();

    EXPECT_CALL(mountDone, callback(fs::Result::busy));
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();

    ExpectMount(fs::Result::busy);
}

TEST_F(FatFileSystemTest, unmount_while_mounting_or_unmounting_is_busy)
{
    Format();
    StartMount();
    StartUnmount();

    EXPECT_CALL(unmountDone, callback(fs::Result::busy));
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();

    StartUnmount();
    StartUnmount();
    EXPECT_CALL(unmountDone, callback(fs::Result::busy));
    EXPECT_CALL(unmountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatFileSystemTest, unmounted_volume_is_not_mounted_and_mounts_again)
{
    Format();
    ExpectMount();

    ExpectUnmount();
    ExpectStat("/", fs::Result::notMounted);
    ExpectUnmount(fs::Result::notMounted);

    ExpectMount();
    ExpectStat("/", fs::Result::success, fs::EntryType::directory);
}

TEST_F(FatFileSystemTest, unmount_without_writes_does_not_access_the_device)
{
    Format();
    ExpectMount();
    auto reads = device.ReadRequests();
    auto writes = device.WriteRequests();
    auto flushes = device.FlushRequests();

    ExpectUnmount();

    EXPECT_EQ(reads, device.ReadRequests());
    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(flushes, device.FlushRequests());
}

TEST_F(FatFileSystemTest, mount_after_unmount_reads_the_device_again)
{
    Format();
    ExpectMount();
    ExpectUnmount();
    auto reads = device.ReadRequests();

    ExpectMount();

    EXPECT_EQ(reads + 2, device.ReadRequests());
}

TEST_F(FatFileSystemTest, calls_that_are_not_yet_supported)
{
    Format();
    ExpectMount();

    ExpectUnsupportedCallsCompleteWith(fs::Result::unsupported);
}

TEST_F(FatFileSystemTest, stat_of_the_root_is_a_directory_with_an_empty_name)
{
    Format();
    ExpectMount();

    ExpectStat("/", fs::Result::success, fs::EntryType::directory);
}

TEST_F(FatFileSystemTest, stat_validates_the_path)
{
    Format();
    ExpectMount();

    ExpectStat("a", fs::Result::invalidPath);
    ExpectStat("/a/", fs::Result::invalidPath);
    ExpectStat("/..", fs::Result::invalidPath);
}

TEST_F(FatFileSystemTest, path_longer_than_the_configured_limit_is_too_long)
{
    fs::FatConfig config;
    config.maxPathBytes = 4;
    fs::FatFileSystem::WithStorage<1, 4> limited{ device, config };
    Format();
    limited.Mount([this](fs::Result result)
        {
            mountDone.callback(result);
        });
    EXPECT_CALL(mountDone, callback(fs::Result::success));
    ExecuteAllActions();

    limited.Stat("/abcd", [this](fs::Result result, const fs::EntryInfo&)
        {
            statDone.callback(result, fs::EntryType::file, 0);
        });
    EXPECT_CALL(statDone, callback(fs::Result::nameTooLong, fs::EntryType::file, 0));
    ExecuteAllActions();
}

TEST_F(FatFileSystemTest, path_calls_beyond_the_slots_are_busy)
{
    Format();
    ExpectMount();

    StartStat("/");
    StartStat("/");
    StartStat("/");

    EXPECT_CALL(statDone, callback(fs::Result::busy, fs::EntryType::file, 0));
    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryType::directory, 0)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatFileSystemTest, slot_is_free_again_when_its_call_completes)
{
    Format();
    ExpectMount();

    for (int round = 0; round != 3; ++round)
    {
        StartStat("/");
        StartStat("/");
        EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryType::directory, 0)).Times(2);
        ExecuteAllActions();
    }
}

TEST_F(FatFileSystemTest, call_made_before_unmount_completes_normally)
{
    Format();
    ExpectMount();

    StartStat("/");
    StartUnmount();

    testing::InSequence sequence;
    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryType::directory, 0));
    EXPECT_CALL(unmountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatFileSystemTest, call_made_while_unmounting_is_not_mounted)
{
    Format();
    ExpectMount();

    StartUnmount();
    StartStat("/");

    EXPECT_CALL(statDone, callback(fs::Result::notMounted, fs::EntryType::file, 0));
    EXPECT_CALL(unmountDone, callback(fs::Result::success));
    ExecuteAllActions();
}

class FatMountFailureTest
    : public testing::Test
    , public infra::ClockFixture
{
public:
    fs::SparseBlockDevice::WithStorage<32> device{ 81920 };
    fs::FatFormatter::WithStorage<8> formatter{ device };
    fs::FatFileSystem::WithStorage<1, 4> volume{ device };
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
};

TEST_F(FatMountFailureTest, failure_reading_fsinfo_or_the_fat_is_reported)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;
    formatter.Format(config, [](fs::Result) {});
    ExecuteAllActions();

    for (uint32_t succeeding : { 2u, 3u })
    {
        device.FailNextOperationWith(hal::BlockDevice::Result::failed, succeeding);
        volume.Mount([this](fs::Result result)
            {
                done.callback(result);
            });
        EXPECT_CALL(done, callback(fs::Result::deviceError));
        ExecuteAllActions();
    }
}

TEST_F(FatMountFailureTest, fat32_without_fsinfo_reads_the_fat_after_the_boot_sector)
{
    fs::FatFormatConfig config;
    config.sectorsPerCluster = 1;
    formatter.Format(config, [](fs::Result) {});
    ExecuteAllActions();
    Sector boot;
    device.ReadBlock(32, infra::MakeRange(boot));
    boot[48] = 0;
    device.WriteBlock(32, infra::MakeRange(boot));

    volume.Mount([this](fs::Result result)
        {
            done.callback(result);
        });
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();

    EXPECT_EQ(3u, device.ReadRequests());
}
