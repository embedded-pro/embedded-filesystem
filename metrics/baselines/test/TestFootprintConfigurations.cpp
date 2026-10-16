#include "fs/nor/NorFileSystem.hpp"
#include "metrics/baselines/littlefs/LittleFsSubject.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/nor/NorConfigurations.hpp"
#include "validation/footprint/Configurations.hpp"
#include "gtest/gtest.h"
#include <type_traits>

namespace
{
    void ExpectEqual(const metrics::LittleFsConfiguration& host, const validation::footprint::LittleFsConfiguration& footprint)
    {
        EXPECT_EQ(host.readSize, footprint.readSize);
        EXPECT_EQ(host.programSize, footprint.programSize);
        EXPECT_EQ(host.cacheSize, footprint.cacheSize);
        EXPECT_EQ(host.lookaheadSize, footprint.lookaheadSize);
        EXPECT_EQ(host.blockCycles, footprint.blockCycles);
        EXPECT_EQ(host.collectGarbageWhenIdle, footprint.collectGarbageWhenIdle);
    }
}

TEST(FootprintConfigurationsTest, the_nor_device_is_that_of_nor_qspi_16m)
{
    EXPECT_EQ(metrics::norQspi16m.numberOfSectors, validation::footprint::norSectors);
    EXPECT_EQ(metrics::norQspi16m.sectorSize, validation::footprint::norSectorSize);
    EXPECT_TRUE((std::is_same_v<fs::NorFileSystem::WithStorage<metrics::norQspi16mConfig, 1>, fs::NorFileSystem::WithStorage<validation::footprint::norConfig, 1>>));
}

TEST(FootprintConfigurationsTest, the_block_device_is_that_of_sd_card_256m)
{
    EXPECT_EQ(512, metrics::sdCard256m.blockSize);
    EXPECT_EQ(metrics::sdCard256m.numberOfBlocks, validation::footprint::blockDeviceBlocks);
    EXPECT_EQ(metrics::sdCard256m.blocksPerAllocationUnit, validation::footprint::allocationUnitBlocks);
}

TEST(FootprintConfigurationsTest, the_littlefs_configurations_are_those_of_the_host)
{
    ExpectEqual(metrics::littleFsDefault, validation::footprint::littleFsDefault);
    ExpectEqual(metrics::LittleFsRamMatched(metrics::norQspi16m), validation::footprint::littleFsRamMatched);
}
