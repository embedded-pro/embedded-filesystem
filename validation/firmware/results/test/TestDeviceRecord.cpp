#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/results/DeviceRecord.hpp"
#include "gmock/gmock.h"

class DeviceRecordTest
    : public services::HilFixture
{
public:
    validation::ResultLines results{ response };
};

TEST_F(DeviceRecordTest, a_ram_flash_is_described_by_its_sectors_and_its_memory)
{
    validation::PrintDevice(results, { "ramflash", validation::DeviceKind::nor, 1024, 4096, 0, "", 0, "sdram" });

    EXPECT_EQ("\r\nEVT device id=1 device=ramflash kind=nor sectors=1024 sectorSize=4096 memory=sdram\r\n", Output());
}

TEST_F(DeviceRecordTest, a_ram_block_device_is_described_by_its_blocks)
{
    validation::PrintDevice(results, { "ramblock", validation::DeviceKind::block, 4096, 512, 0, "", 0, "sdram" });

    EXPECT_EQ("\r\nEVT device id=1 device=ramblock kind=block blocks=4096 blockSize=512 memory=sdram\r\n", Output());
}

TEST_F(DeviceRecordTest, a_nor_flash_chip_has_its_page_its_identifier_and_its_clock)
{
    validation::PrintDevice(results, { "qspi", validation::DeviceKind::nor, 4096, 4096, 256, "20ba20", 50'000'000, "" });

    EXPECT_EQ("\r\nEVT device id=1 device=qspi kind=nor sectors=4096 sectorSize=4096 pageSize=256 chip=20ba20 clockHz=50000000\r\n", Output());
}

TEST_F(DeviceRecordTest, a_card_has_its_allocation_unit_its_identifier_its_clock_and_its_cache)
{
    validation::PrintDevice(results, { "emmc", validation::DeviceKind::block, 524288, 512, 0, "150100344642363452", 25'000'000, "", 1024, "on" });

    EXPECT_EQ("\r\nEVT device id=1 device=emmc kind=block blocks=524288 blockSize=512 unitBlocks=1024 chip=150100344642363452 clockHz=25000000 cache=on\r\n", Output());
}
