#include "infra/stream/StringOutputStream.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "services/hil/HilResponse.hpp"
#include "services/tracer/Tracer.hpp"
#include "validation/firmware/devices/CountingBlockDevice.hpp"
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/devices/RamBlockDevice.hpp"
#include "validation/firmware/devices/RamFlash.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include "validation/firmware/workload/DataLogger.hpp"
#include "validation/firmware/workload/FatWorkloadVolume.hpp"
#include "validation/firmware/workload/MountScaling.hpp"
#include "validation/firmware/workload/NorWorkloadVolume.hpp"
#include "validation/firmware/workload/RandomOverwrite.hpp"
#include "validation/firmware/workload/ReadPatterns.hpp"
#include "validation/firmware/workload/SequentialWrite.hpp"
#include "validation/firmware/workload/SmallFiles.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>

using namespace std::chrono_literals;

namespace
{
    constexpr uint32_t sectors = 512;
    constexpr uint32_t sectorSize = 4096;
    constexpr fs::NorConfig config{ .maxBlocks = sectors, .programUnit = 1, .pageSize = 256 };
    constexpr validation::FlashGuard guard{ 100ms, 20ms, 2s };

    std::array<uint8_t, sectors * sectorSize>& Memory()
    {
        static std::array<uint8_t, sectors * sectorSize> memory;
        return memory;
    }

    constexpr uint32_t fatBlocks = 16384;
    constexpr uint32_t fatUnitBlocks = 2048;

    std::array<uint8_t, fatBlocks * validation::RamBlockDevice::blockSize>& FatMemory()
    {
        static std::array<uint8_t, fatBlocks * validation::RamBlockDevice::blockSize> memory;
        return memory;
    }

    uint32_t LittleEndian(infra::ConstByteRange bytes)
    {
        uint32_t value = 0;
        for (auto byte = bytes.end(); byte != bytes.begin();)
            value = value << 8 | *--byte;
        return value;
    }

    // The keys of the record of a kind whose key has a value, joined from its lines
    std::string Record(const std::string& output, const std::string& kind, const std::string& key, const std::string& value)
    {
        std::istringstream lines{ output };
        std::string line;
        std::string found;
        std::string id;
        while (std::getline(lines, line))
        {
            std::istringstream tokens{ line };
            std::string head;
            std::string lineKind;
            std::string lineId;
            tokens >> head >> lineKind >> lineId;
            if (head != "EVT" || lineKind != kind)
                continue;

            std::string rest;
            std::getline(tokens, rest);
            if (lineId == id)
                found += rest;
            else if (id.empty() && (rest + " ").find(" " + key + "=" + value + " ") != std::string::npos)
            {
                id = lineId;
                found = rest;
            }
        }

        std::erase(found, '\r');
        return found + " ";
    }

    class WorkloadsTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        std::string Run(validation::Workload& workload, uint32_t scaleDivisor, uint32_t fill = 0)
        {
            validation::WorkloadParameters parameters;
            parameters.scaleDivisor = scaleDivisor;
            parameters.fillPercent = fill;
            runner.Begin(volume, counting, { "ramflash", volume.Subject(), workload.Name(), fill, scaleDivisor, volume.RamBytes() }, [] {});
            finished = false;
            workload.Run(parameters, [this]
                {
                    runner.End();
                    finished = true;
                });

            for (int minute = 0; minute != 600 && !finished; ++minute)
                ForwardTime(1min);

            EXPECT_TRUE(finished);
            EXPECT_FALSE(runner.Failed());
            return { stream.Storage().begin(), stream.Storage().end() };
        }

        std::string Phase(const std::string& output, const std::string& phase)
        {
            return Record(output, "phase", "phase", phase);
        }

        infra::StringOutputStream::WithStorage<128 * 1024> stream;
        services::TracerToStream tracer{ stream };
        services::HilResponse response{ tracer };
        validation::RamFlash ram{ infra::MakeRange(Memory()), sectorSize };
        validation::FakeCycleCounter cycles;
        validation::CountingFlash counting{ ram, 256, cycles, validation::FakeCycleCounter::clockHz, guard };
        validation::NorWorkloadVolume<config> volume{ counting };
        validation::ResultLines results{ response };
        validation::WorkloadRunner runner{ results, cycles, validation::FakeCycleCounter::clockHz };
        validation::WorkloadTools tools{ runner };
        bool finished{ false };
    };
}

TEST_F(WorkloadsTest, the_volume_is_the_nor_subject_with_the_ram_of_the_host)
{
    EXPECT_EQ("embedded-filesystem/nor", volume.Subject());
    EXPECT_EQ(sizeof(fs::NorFileSystem::WithStorage<config, 1>), volume.RamBytes());
}

TEST_F(WorkloadsTest, l1_writes_each_file_in_its_chunks_after_announcing_its_size_and_removes_it)
{
    validation::SequentialWrite workload{ tools };
    EXPECT_FALSE(workload.Fills());

    auto output = Run(workload, 256, 50);

    EXPECT_THAT(output, testing::HasSubstr("EVT workload id=1 device=ramflash subject=embedded-filesystem/nor workload=L1 fill=50 scale=256 "));
    EXPECT_THAT(Phase(output, "L1,prepare"), testing::HasSubstr(" format.calls=1 "));
    EXPECT_THAT(Phase(output, "L1,prepare"), testing::Not(testing::HasSubstr(" usage.calls=")));
    EXPECT_THAT(Phase(output, "L1,4096,64"), testing::HasSubstr(" write.calls=64 write.userBytes=4096 "));
    EXPECT_THAT(Phase(output, "L1,4096,64"), testing::HasSubstr(" expectSize.calls=1 "));
    EXPECT_THAT(Phase(output, "L1,4096,64"), testing::HasSubstr(" idle.calls=1 "));
    EXPECT_THAT(Phase(output, "L1,cleanup,4096,64"), testing::HasSubstr(" remove.calls=1 "));
    EXPECT_THAT(Phase(output, "L1,16384,4096"), testing::HasSubstr(" write.calls=4 write.userBytes=16384 "));
    EXPECT_THAT(Phase(output, "L1,cleanup,16384,4096"), testing::HasSubstr(" idle.calls=1 "));
}

TEST_F(WorkloadsTest, l2_appends_to_two_logs_with_an_idle_period_after_each_append)
{
    validation::DataLogger workload{ tools };
    EXPECT_TRUE(workload.Fills());

    auto output = Run(workload, 16, 10);

    EXPECT_THAT(Phase(output, "fill"), testing::HasSubstr(" makeDirectory.calls=1 "));
    EXPECT_THAT(Phase(output, "L2"), testing::HasSubstr(" write.calls=4096 write.userBytes=262144 "));
    EXPECT_THAT(Phase(output, "L2"), testing::HasSubstr(" sync.calls=4096 "));
    EXPECT_THAT(Phase(output, "L2"), testing::HasSubstr(" idle.calls=4096 "));
    EXPECT_THAT(Phase(output, "L2,second,log"), testing::HasSubstr(" write.calls=2048 write.userBytes=131072 "));
    EXPECT_THAT(Record(output, "window", "log", "first"), testing::HasSubstr(" fill=10 log=first appends="));
    EXPECT_THAT(Record(output, "window", "log", "second"), testing::HasSubstr(" fill=10 log=second appends="));
}

TEST_F(WorkloadsTest, l3_overwrites_at_random_offsets_in_three_files)
{
    validation::RandomOverwrite workload{ tools };
    EXPECT_TRUE(workload.Fills());

    auto output = Run(workload, 256, 50);

    EXPECT_THAT(Phase(output, "L3,create,512"), testing::HasSubstr(" write.calls=1 write.userBytes=512 "));
    EXPECT_THAT(Phase(output, "L3,512"), testing::HasSubstr(" open.calls=1 "));
    EXPECT_THAT(Phase(output, "L3,512"), testing::HasSubstr(" write.calls=64 write.userBytes=32768 "));
    EXPECT_THAT(Phase(output, "L3,512"), testing::HasSubstr(" seek.calls=64 "));
    EXPECT_THAT(Phase(output, "L3,512"), testing::HasSubstr(" sync.calls=64 "));
    EXPECT_THAT(Phase(output, "L3,cleanup,512"), testing::HasSubstr(" remove.calls=1 "));
    EXPECT_THAT(Phase(output, "L3,16384"), testing::HasSubstr(" write.calls=64 "));
}

TEST_F(WorkloadsTest, l4_writes_200_small_files_and_keeps_the_latest_16)
{
    validation::SmallFiles workload{ tools };
    EXPECT_TRUE(workload.Fills());

    auto output = Run(workload, 16, 10);

    EXPECT_THAT(Phase(output, "L4"), testing::HasSubstr(" open.calls=200 "));
    EXPECT_THAT(Phase(output, "L4"), testing::HasSubstr(" remove.calls=184 "));
    EXPECT_THAT(Phase(output, "L4"), testing::HasSubstr(" makeDirectory.calls=1 "));
}

TEST_F(WorkloadsTest, l6_reads_sequentially_at_random_offsets_and_by_path)
{
    validation::ReadPatterns workload{ tools };
    EXPECT_FALSE(workload.Fills());

    auto output = Run(workload, 256);

    EXPECT_THAT(Phase(output, "L6,create"), testing::HasSubstr(" write.calls=1 write.userBytes=4096 "));
    EXPECT_THAT(Phase(output, "L6,sequential"), testing::HasSubstr(" read.calls=2 read.userBytes=4096 "));
    EXPECT_THAT(Phase(output, "L6,random,create,512"), testing::HasSubstr(" open.calls=2 "));
    EXPECT_THAT(Phase(output, "L6,random,4096"), testing::HasSubstr(" read.calls=64 read.userBytes=32768 "));
    EXPECT_THAT(Phase(output, "L6,random,4096"), testing::HasSubstr(" seek.calls=64 "));
    EXPECT_THAT(Phase(output, "L6,seek,end,16384"), testing::HasSubstr(" read.calls=1 read.userBytes=512 "));
    EXPECT_THAT(Phase(output, "L6,seek,end,16384"), testing::HasSubstr(" remove.calls=1 "));
    EXPECT_THAT(Phase(output, "L6,directory,create,1000"), testing::HasSubstr(" open.calls=1000 "));
    EXPECT_THAT(Phase(output, "L6,list,100"), testing::HasSubstr(" list.calls=1 "));
    EXPECT_THAT(Phase(output, "L6,lookup,10"), testing::HasSubstr(" open.calls=32 "));
    EXPECT_THAT(Phase(output, "L6,lookup,10"), testing::HasSubstr(" close.calls=32 "));
}

TEST_F(WorkloadsTest, l7_mounts_the_volume_at_three_fill_levels)
{
    validation::MountScaling workload{ tools };
    EXPECT_FALSE(workload.Fills());

    auto output = Run(workload, 16);

    EXPECT_THAT(Phase(output, "L7,prepare,10"), testing::HasSubstr(" format.calls=1 "));
    EXPECT_THAT(Phase(output, "L7,50"), testing::HasSubstr(" mount.calls=1 "));
    EXPECT_THAT(Phase(output, "L7,cleanup,90"), testing::HasSubstr(" unmount.calls=1 "));
}

namespace
{
    class FatWorkloadsTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        std::string Run(validation::Workload& workload, uint32_t scaleDivisor, uint32_t fill = 0)
        {
            validation::WorkloadParameters parameters;
            parameters.scaleDivisor = scaleDivisor;
            parameters.fillPercent = fill;
            runner.Begin(volume, counting, { "ramblock", volume.Subject(), workload.Name(), fill, scaleDivisor, volume.RamBytes(), validation::DeviceKind::block }, [] {});
            finished = false;
            workload.Run(parameters, [this]
                {
                    runner.End();
                    finished = true;
                });

            for (int minute = 0; minute != 600 && !finished; ++minute)
                ForwardTime(1min);

            EXPECT_TRUE(finished);
            EXPECT_FALSE(runner.Failed());
            return { stream.Storage().begin(), stream.Storage().end() };
        }

        infra::StringOutputStream::WithStorage<128 * 1024> stream;
        services::TracerToStream tracer{ stream };
        services::HilResponse response{ tracer };
        validation::RamBlockDevice ram{ infra::MakeRange(FatMemory()) };
        validation::FakeCycleCounter cycles;
        validation::CountingBlockDevice counting{ ram, fatUnitBlocks, cycles, validation::FakeCycleCounter::clockHz, 1s };
        validation::FatWorkloadVolume volume{ counting, fatUnitBlocks };
        validation::ResultLines results{ response };
        validation::WorkloadRunner runner{ results, cycles, validation::FakeCycleCounter::clockHz };
        validation::WorkloadTools tools{ runner };
        bool finished{ false };
    };
}

TEST_F(FatWorkloadsTest, the_volume_is_the_fat_subject_with_the_ram_of_the_host_and_without_counters)
{
    EXPECT_EQ("embedded-filesystem/fat", volume.Subject());
    EXPECT_EQ(sizeof(fs::FatFileSystem::WithStorage<1, 8>), volume.RamBytes());
    EXPECT_EQ(std::nullopt, volume.Counters());
}

TEST_F(FatWorkloadsTest, the_volume_formats_with_its_data_area_at_an_allocation_unit)
{
    std::optional<fs::Result> formatted;
    volume.Format([&formatted](fs::Result result)
        {
            formatted = result;
        });
    ExecuteAllActions();

    ASSERT_EQ(fs::Result::success, formatted);
    const auto& memory = FatMemory();
    auto partition = LittleEndian(infra::MakeRange(memory.data() + 454, memory.data() + 458));
    const auto* boot = memory.data() + partition * validation::RamBlockDevice::blockSize;
    auto reserved = LittleEndian(infra::MakeRange(boot + 14, boot + 16));
    auto fats = boot[16];
    auto rootSectors = LittleEndian(infra::MakeRange(boot + 17, boot + 19)) * 32 / validation::RamBlockDevice::blockSize;
    auto fatSectors = LittleEndian(infra::MakeRange(boot + 22, boot + 24));
    EXPECT_EQ(0, (partition + reserved + fats * fatSectors + rootSectors) % fatUnitBlocks);
}

TEST_F(FatWorkloadsTest, l2_takes_the_windows_of_a_volume_without_checkpoints_by_appends_and_prints_the_block_counts)
{
    validation::DataLogger workload{ tools };

    auto output = Run(workload, 256, 10);

    EXPECT_THAT(Record(output, "phase", "phase", "L2"), testing::HasSubstr(" write.calls=256 write.userBytes=16384 "));
    EXPECT_THAT(Record(output, "phase", "phase", "L2"), testing::HasSubstr(" blocksWritten="));
    EXPECT_THAT(Record(output, "phase", "phase", "L2"), testing::Not(testing::HasSubstr(" allocatedSectors=")));
    EXPECT_THAT(Record(output, "window", "log", "first"), testing::HasSubstr(" fill=10 log=first appends=64 readRequests="));
    EXPECT_THAT(Record(output, "window", "log", "second"), testing::HasSubstr(" fill=10 log=second appends=64 readRequests="));
}
