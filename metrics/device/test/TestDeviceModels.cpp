#include "metrics/device/DeviceModels.hpp"
#include "gmock/gmock.h"
#include <chrono>

using namespace std::chrono_literals;

namespace
{
    const metrics::NorModel testNor{ "test-nor", 4, 1024, 256, 1, { 400us, 45ms, 50'000'000 }, { 3ms, 400ms, 50'000'000 } };
    const metrics::NorModel testInternal{ "test-internal", 4, 2048, 16, 16, { 8us, 20ms, 100'000'000 }, { 8us, 20ms, 100'000'000 } };
    const metrics::BlockDeviceModel testBlock{ "test-block", 512, 64, 4, 2, { 100us, 25us, 50us, 0us, 10ms }, { 100us, 25us, 50us, 250ms, 10ms } };
}

TEST(DeviceModelsTest, nor_qspi_16m_has_4096_sectors_of_4_kib_and_256_byte_pages)
{
    EXPECT_EQ(4096, metrics::norQspi16m.numberOfSectors);
    EXPECT_EQ(4096, metrics::norQspi16m.sectorSize);
    EXPECT_EQ(256, metrics::norQspi16m.pageSize);
    EXPECT_EQ(1, metrics::norQspi16m.programUnit);
    EXPECT_EQ(400us, metrics::norQspi16m.typical.pageProgram);
    EXPECT_EQ(45ms, metrics::norQspi16m.typical.sectorErase);
    EXPECT_EQ(3ms, metrics::norQspi16m.maximum.pageProgram);
    EXPECT_EQ(400ms, metrics::norQspi16m.maximum.sectorErase);
}

TEST(DeviceModelsTest, nor_qspi_1m_has_256_sectors_with_the_timing_of_nor_qspi_16m)
{
    EXPECT_EQ(256, metrics::norQspi1m.numberOfSectors);
    EXPECT_EQ(4096, metrics::norQspi1m.sectorSize);
    EXPECT_EQ(metrics::norQspi16m.typical.pageProgram, metrics::norQspi1m.typical.pageProgram);
    EXPECT_EQ(metrics::norQspi16m.maximum.sectorErase, metrics::norQspi1m.maximum.sectorErase);
}

TEST(DeviceModelsTest, internal_flash_has_256_sectors_of_2_kib_and_a_16_byte_program_unit)
{
    EXPECT_EQ(256, metrics::internalFlash.numberOfSectors);
    EXPECT_EQ(2048, metrics::internalFlash.sectorSize);
    EXPECT_EQ(16, metrics::internalFlash.programUnit);
    EXPECT_EQ(16, metrics::internalFlash.pageSize);
    EXPECT_EQ(8us, metrics::internalFlash.typical.pageProgram);
    EXPECT_EQ(20ms, metrics::internalFlash.typical.sectorErase);
}

TEST(DeviceModelsTest, sd_card_256m_has_524288_blocks_and_8192_blocks_per_allocation_unit)
{
    EXPECT_EQ(512, metrics::sdCard256m.blockSize);
    EXPECT_EQ(524288, metrics::sdCard256m.numberOfBlocks);
    EXPECT_EQ(8192, metrics::sdCard256m.blocksPerAllocationUnit);
    EXPECT_EQ(2, metrics::sdCard256m.recentAllocationUnits);
    EXPECT_EQ(100us, metrics::sdCard256m.typical.command);
    EXPECT_EQ(250ms, metrics::sdCard256m.maximum.writeBusy);
}

TEST(DeviceModelsTest, program_time_counts_every_page_touched)
{
    EXPECT_EQ(0us, metrics::ProgramTime(testNor, metrics::TimingCase::typical, 0, 0));
    EXPECT_EQ(400us, metrics::ProgramTime(testNor, metrics::TimingCase::typical, 0, 256));
    EXPECT_EQ(800us, metrics::ProgramTime(testNor, metrics::TimingCase::typical, 255, 2));
    EXPECT_EQ(1200us, metrics::ProgramTime(testNor, metrics::TimingCase::typical, 100, 600));
    EXPECT_EQ(6ms, metrics::ProgramTime(testNor, metrics::TimingCase::maximum, 255, 2));
}

TEST(DeviceModelsTest, internal_flash_program_time_counts_every_program_unit)
{
    EXPECT_EQ(8us, metrics::ProgramTime(testInternal, metrics::TimingCase::typical, 0, 16));
    EXPECT_EQ(24us, metrics::ProgramTime(testInternal, metrics::TimingCase::typical, 8, 40));
}

TEST(DeviceModelsTest, read_time_follows_the_read_rate_rounded_up_to_whole_nanoseconds)
{
    EXPECT_EQ(0ns, metrics::ReadTime(testNor, metrics::TimingCase::typical, 0));
    EXPECT_EQ(20ns, metrics::ReadTime(testNor, metrics::TimingCase::typical, 1));
    EXPECT_EQ(81920ns, metrics::ReadTime(testNor, metrics::TimingCase::typical, 4096));
    EXPECT_EQ(10ns, metrics::ReadTime(testInternal, metrics::TimingCase::typical, 1));
}

TEST(DeviceModelsTest, erase_time_counts_every_sector)
{
    EXPECT_EQ(135ms, metrics::EraseTime(testNor, metrics::TimingCase::typical, 3));
    EXPECT_EQ(1200ms, metrics::EraseTime(testNor, metrics::TimingCase::maximum, 3));
}

TEST(DeviceModelsTest, block_read_time_is_command_plus_time_per_block)
{
    EXPECT_EQ(100us + 3 * 25us, metrics::ReadTime(testBlock, metrics::TimingCase::typical, 3));
}

TEST(DeviceModelsTest, block_write_time_is_command_plus_busy_plus_time_per_block_plus_openings)
{
    EXPECT_EQ(100us + 2 * 50us, metrics::WriteTime(testBlock, metrics::TimingCase::typical, 2, 0));
    EXPECT_EQ(100us + 2 * 50us + 20ms, metrics::WriteTime(testBlock, metrics::TimingCase::typical, 2, 2));
    EXPECT_EQ(100us + 250ms + 2 * 50us, metrics::WriteTime(testBlock, metrics::TimingCase::maximum, 2, 0));
}

TEST(DeviceModelsTest, command_time_is_one_command)
{
    EXPECT_EQ(100us, metrics::CommandTime(testBlock, metrics::TimingCase::typical));
}

TEST(DeviceModelsTest, models_are_found_by_name)
{
    EXPECT_EQ(&metrics::norQspi16m, metrics::FindNorModel("nor-qspi-16m"));
    EXPECT_EQ(&metrics::norQspi1m, metrics::FindNorModel("nor-qspi-1m"));
    EXPECT_EQ(&metrics::internalFlash, metrics::FindNorModel("internal-flash"));
    EXPECT_EQ(&metrics::sdCard256m, metrics::FindBlockDeviceModel("sd-card-256m"));
}

TEST(DeviceModelsTest, unknown_model_name_is_not_found)
{
    EXPECT_EQ(nullptr, metrics::FindNorModel("sd-card-256m"));
    EXPECT_EQ(nullptr, metrics::FindBlockDeviceModel("nor-qspi-16m"));
}

TEST(DeviceModelsTest, parameters_list_the_geometry_and_every_timing_of_a_nor_model)
{
    auto parameters = metrics::Parameters(testNor);

    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "sectors", "4" }));
    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "sectorSize", "1024" }));
    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "typical.pageProgramNs", "400000" }));
    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "maximum.sectorEraseNs", "400000000" }));
    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "typical.readBytesPerSecond", "50000000" }));
}

TEST(DeviceModelsTest, parameters_list_the_geometry_and_every_timing_of_a_block_device_model)
{
    auto parameters = metrics::Parameters(testBlock);

    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "blocksPerAllocationUnit", "4" }));
    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "recentAllocationUnits", "2" }));
    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "maximum.writeBusyNs", "250000000" }));
    EXPECT_THAT(parameters, testing::Contains(std::pair<std::string, std::string>{ "typical.allocationUnitOpeningNs", "10000000" }));
}
