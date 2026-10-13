#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include <array>
#include <chrono>

namespace
{
    class FatBackgroundScanTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatBackgroundScanTest(uint32_t blocks = 16384)
            : FatFileSystemFixture(blocks)
        {}

        void SetUp() override
        {
            ExpectMount();
        }

        void ExpectUsageFromMemory()
        {
            auto before = device.ReadRequests();
            volume.Usage([this](fs::Result result, fs::VolumeUsage usage)
                {
                    usageDone.callback(result, usage.usedBytes);
                });
            EXPECT_CALL(usageDone, callback(fs::Result::success, testing::_));
            ExecuteAllActions();
            EXPECT_EQ(before, device.ReadRequests());
        }

        uint32_t StartScan()
        {
            auto before = device.ReadRequests();
            systemTimerService.TimeProgressed(idleDelay);

            while (device.ReadRequests() == before && !IsIdle())
                ExecuteFirstAction();

            EXPECT_EQ(before + 1, device.ReadRequests());
            return before;
        }

        uint32_t FatSectors() const
        {
            return ((builder->Geometry().clusterCount + 2) * 2 + 511) / 512;
        }

        const infra::Duration idleDelay{ std::chrono::milliseconds(10) };
        testing::StrictMock<infra::MockCallback<void(fs::Result, uint64_t)>> usageDone;
    };

    class FatBackgroundScanOnFat32Test
        : public FatBackgroundScanTest
    {
    public:
        FatBackgroundScanOnFat32Test()
            : FatBackgroundScanTest(81920)
        {}
    };
}

TEST_F(FatBackgroundScanTest, nothing_is_read_before_the_idle_delay)
{
    auto before = device.ReadRequests();

    ForwardTime(idleDelay - std::chrono::milliseconds(1));

    EXPECT_EQ(before, device.ReadRequests());
}

TEST_F(FatBackgroundScanTest, idle_volume_counts_its_free_clusters)
{
    auto before = device.ReadRequests();

    ForwardTime(idleDelay);

    EXPECT_EQ(before + FatSectors(), device.ReadRequests());
    ExpectUsageFromMemory();
}

TEST_F(FatBackgroundScanTest, count_is_read_once_per_mount)
{
    ForwardTime(idleDelay);
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
    auto before = device.ReadRequests();

    ForwardTime(idleDelay);

    EXPECT_EQ(before, device.ReadRequests());
}

TEST_F(FatBackgroundScanTest, foreground_call_waits_for_at_most_one_scan_request)
{
    builder->AddFile(0, "", "NOTES   TXT", infra::ConstByteRange());
    auto before = StartScan();

    StartStat("/notes.txt");
    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryInfo{ "NOTES.TXT", fs::EntryType::file, 0 })).WillOnce([this, before]
        {
            EXPECT_EQ(before + 2, device.ReadRequests());
        });
    ExecuteAllActions();
}

TEST_F(FatBackgroundScanTest, scan_granted_while_a_call_waits_yields_before_reading)
{
    builder->AddFile(0, "", "NOTES   TXT", infra::ConstByteRange());
    auto before = device.ReadRequests();
    systemTimerService.TimeProgressed(idleDelay);

    StartStat("/notes.txt");
    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryInfo{ "NOTES.TXT", fs::EntryType::file, 0 })).WillOnce([this, before]
        {
            EXPECT_EQ(before + 1, device.ReadRequests());
        });
    ExecuteAllActions();
}

TEST_F(FatBackgroundScanTest, yielded_scan_continues_after_the_next_idle_delay)
{
    auto before = StartScan();
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });

    ForwardTime(idleDelay);

    EXPECT_EQ(before + FatSectors(), device.ReadRequests());
    ExpectUsageFromMemory();
}

TEST_F(FatBackgroundScanTest, usage_finishes_a_scan_that_yielded)
{
    auto before = StartScan();

    volume.Usage([this](fs::Result result, fs::VolumeUsage usage)
        {
            usageDone.callback(result, usage.usedBytes);
        });
    EXPECT_CALL(usageDone, callback(fs::Result::success, 0));
    ExecuteAllActions();

    EXPECT_EQ(before + FatSectors(), device.ReadRequests());
    ForwardTime(idleDelay);
    EXPECT_EQ(before + FatSectors(), device.ReadRequests());
}

TEST_F(FatBackgroundScanTest, scan_that_fails_continues_after_the_next_foreground_call)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 1);
    auto before = device.ReadRequests();
    ForwardTime(idleDelay);
    EXPECT_EQ(before + 2, device.ReadRequests());

    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
    ForwardTime(idleDelay);

    EXPECT_EQ(before + 2 + FatSectors() - 1, device.ReadRequests());
    ExpectUsageFromMemory();
}

TEST_F(FatBackgroundScanTest, removed_card_during_the_scan_faults_the_volume)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    ForwardTime(idleDelay);

    ExpectStat("/", fs::Result::mediaRemoved);
}

TEST_F(FatBackgroundScanTest, unmounted_volume_is_not_scanned)
{
    ExpectUnmount();
    auto before = device.ReadRequests();

    ForwardTime(idleDelay);

    EXPECT_EQ(before, device.ReadRequests());
}

TEST_F(FatBackgroundScanTest, unmount_requested_while_the_scan_waits_for_the_queue_is_not_delayed)
{
    auto before = device.ReadRequests();
    systemTimerService.TimeProgressed(idleDelay);
    volume.Unmount([this](fs::Result result)
        {
            lifecycleDone.callback(result);
        });
    EXPECT_CALL(lifecycleDone, callback(fs::Result::success));
    ExecuteAllActions();

    ForwardTime(idleDelay);

    EXPECT_EQ(before, device.ReadRequests());
}

TEST_F(FatBackgroundScanOnFat32Test, clean_volume_needs_no_scan)
{
    auto before = device.ReadRequests();

    ForwardTime(idleDelay);

    EXPECT_EQ(before, device.ReadRequests());
    ExpectUsageFromMemory();
}

TEST_F(FatBackgroundScanTest, clusters_taken_in_a_counted_sector_during_the_scan_are_used)
{
    builder->AddFile(0, "", "NOTES   TXT", infra::ConstByteRange());
    StartScan();
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::write);
    std::array<uint8_t, 1024> data{};

    file->Write(infra::MakeRange(data), [this](fs::Result result)
        {
            done.callback(result);
        });
    ExpectDone(fs::Result::success);

    volume.Usage([this](fs::Result result, fs::VolumeUsage usage)
        {
            usageDone.callback(result, usage.usedBytes);
        });
    EXPECT_CALL(usageDone, callback(fs::Result::success, 2 * 512));
    ExecuteAllActions();
}
