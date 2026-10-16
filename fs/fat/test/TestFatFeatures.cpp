#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <optional>

namespace
{
    constexpr fs::FatFeatures withoutChanges{ .makeDirectory = false, .remove = false, .rename = false, .expectSize = false, .usage = false };

    template<fs::FatFeatures Features, uint32_t Blocks = 4096>
    class FatFeaturesFixture
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        FatFeaturesFixture()
        {
            std::optional<fs::Result> formatted;
            formatter.Format(fs::FatFormatConfig{ .sectorsPerCluster = Blocks > 4096 ? std::optional<uint8_t>(1) : std::nullopt }, [&formatted](fs::Result result)
                {
                    formatted = result;
                });
            ExecuteAllActions();
            EXPECT_EQ(fs::Result::success, formatted);
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        void ExpectDone(fs::Result result, int times = 1)
        {
            EXPECT_CALL(done, callback(result)).Times(times);
            ExecuteAllActions();
        }

        infra::SharedPtr<fs::File> Open(infra::BoundedConstString path, fs::OpenMode mode)
        {
            infra::SharedPtr<fs::File> opened;
            volume.Open(path, mode, [this, &opened](fs::Result result, infra::SharedPtr<fs::File> file)
                {
                    opened = file;
                    done.callback(result);
                });
            ExpectDone(fs::Result::success);
            return opened;
        }

        void StartDisabledCalls()
        {
            volume.MakeDirectory("/a", Done());
            volume.Remove("/a", Done());
            volume.Rename("/a", "/b", Done());
            volume.Usage([this](fs::Result result, fs::VolumeUsage usage)
                {
                    EXPECT_EQ(fs::VolumeUsage{}, usage);
                    done.callback(result);
                });
        }

        fs::SparseBlockDevice::WithStorage<192> device{ Blocks };
        fs::FatFormatter::WithStorage<8> formatter{ device };
        fs::FatFileSystem::WithStorage<1, 4, fs::FatLimits{}, Features> volume{ device };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
        std::array<uint8_t, 100> data{};
    };

    class FatFeaturesTest
        : public FatFeaturesFixture<withoutChanges>
    {};

    constexpr fs::FatFeatures withoutCleanShutdown{ .cleanShutdown = false };

    class FatWithoutCleanShutdownOnFat16Test
        : public FatFeaturesFixture<withoutCleanShutdown, 16384>
    {
    public:
        bool CleanBit()
        {
            fs::FatImageBuilder builder{ device };
            return fs::CleanShutdown(builder.Geometry().type, builder.FatEntry(1)) == true;
        }
    };

    class FatWithoutCleanShutdownOnFat32Test
        : public FatFeaturesFixture<withoutCleanShutdown, 81920>
    {};

    class FatWithoutBackgroundScanTest
        : public FatFeaturesFixture<fs::FatFeatures{ .backgroundScan = false }>
    {
    public:
        void ExpectUsage(fs::Result result)
        {
            volume.Usage([this](fs::Result result, fs::VolumeUsage usage)
                {
                    this->usage = usage;
                    done.callback(result);
                });
            ExpectDone(result);
        }

        fs::VolumeUsage usage;
    };
}

TEST_F(FatFeaturesTest, disabled_calls_complete_with_unsupported)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);

    StartDisabledCalls();
    ExpectDone(fs::Result::unsupported, 4);
}

TEST_F(FatFeaturesTest, disabled_calls_are_not_mounted_before_mount)
{
    StartDisabledCalls();
    ExpectDone(fs::Result::notMounted, 4);
}

TEST_F(FatFeaturesTest, disabled_calls_are_unsupported_before_their_paths_are_validated)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);

    volume.MakeDirectory("relative", Done());
    volume.Rename("/a", "/b/", Done());
    ExpectDone(fs::Result::unsupported, 2);
}

TEST_F(FatFeaturesTest, expect_size_is_unsupported_on_a_writable_file_only)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);
    auto file = Open("/log.txt", fs::OpenMode::write);

    file->ExpectSize(4096, Done());
    ExpectDone(fs::Result::unsupported);
    file->Close(Done());
    ExpectDone(fs::Result::success);
    file = nullptr;

    auto reader = Open("/log.txt", fs::OpenMode::read);
    reader->ExpectSize(4096, Done());
    ExpectDone(fs::Result::readOnly);
    reader = nullptr;

    file = Open("/log.txt", fs::OpenMode::append);
    volume.Unmount(Done());
    ExpectDone(fs::Result::success);
    file->ExpectSize(4096, Done());
    ExpectDone(fs::Result::notMounted);
}

TEST_F(FatFeaturesTest, the_calls_that_stay_enabled_write_and_read_files)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);
    data.fill(0x5a);

    auto file = Open("/log.txt", fs::OpenMode::write);
    file->Write(infra::MakeRange(data), Done());
    ExpectDone(fs::Result::success);
    file->Close(Done());
    ExpectDone(fs::Result::success);
    file = nullptr;

    auto reader = Open("/log.txt", fs::OpenMode::read);
    std::array<uint8_t, 100> readBack{};
    reader->Read(infra::MakeRange(readBack), [this](fs::Result result, std::size_t bytesRead)
        {
            EXPECT_EQ(100u, bytesRead);
            done.callback(result);
        });
    ExpectDone(fs::Result::success);
    EXPECT_EQ(data, readBack);
}

TEST_F(FatWithoutBackgroundScanTest, nothing_is_read_while_the_volume_is_idle)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);
    auto reads = device.ReadRequests();

    ForwardTime(std::chrono::seconds(1));

    EXPECT_EQ(reads, device.ReadRequests());
}

TEST_F(FatWithoutBackgroundScanTest, usage_counts_the_free_clusters_on_its_first_call)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);
    auto reads = device.ReadRequests();

    ExpectUsage(fs::Result::success);
    EXPECT_LT(reads, device.ReadRequests());
    EXPECT_EQ(0u, usage.usedBytes);
    EXPECT_LT(0u, usage.totalBytes);

    reads = device.ReadRequests();
    ExpectUsage(fs::Result::success);
    EXPECT_EQ(reads, device.ReadRequests());
}

TEST_F(FatWithoutCleanShutdownOnFat16Test, writing_leaves_the_clean_shutdown_bit_alone)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);
    ASSERT_TRUE(CleanBit());
    data.fill(0x5a);

    auto file = Open("/log.txt", fs::OpenMode::write);
    file->Write(infra::MakeRange(data), Done());
    ExpectDone(fs::Result::success);
    file->Sync(Done());
    ExpectDone(fs::Result::success);
    EXPECT_TRUE(CleanBit());

    file = nullptr;
    volume.Unmount(Done());
    ExpectDone(fs::Result::success);
    EXPECT_TRUE(CleanBit());
}

TEST_F(FatWithoutCleanShutdownOnFat32Test, a_mount_reads_only_the_partition_table_and_the_boot_sector)
{
    auto reads = device.ReadRequests();

    volume.Mount(Done());
    ExpectDone(fs::Result::success);

    EXPECT_EQ(reads + 2, device.ReadRequests());
}
