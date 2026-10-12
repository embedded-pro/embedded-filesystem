#ifndef FS_FAT_TEST_FAT_VOLUME_FIXTURE_HPP
#define FS_FAT_TEST_FAT_VOLUME_FIXTURE_HPP

#include "fs/common/FlushBarrier.hpp"
#include "fs/common/SectorCache.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "gtest/gtest.h"
#include <optional>

class FatVolumeFixture
    : public testing::Test
    , public infra::EventDispatcherFixture
{
public:
    FatVolumeFixture(uint32_t blocks, const fs::FatFormatConfig& config)
        : device(blocks)
    {
        std::optional<fs::Result> result;
        formatter.Format(config, [&result](fs::Result formatted)
            {
                result = formatted;
            });
        ExecuteAllActions();
        EXPECT_EQ(fs::Result::success, result);
        builder.emplace(device);
        geometry = builder->Geometry();
    }

    static fs::FatFormatConfig ClusterSize(uint8_t sectors)
    {
        fs::FatFormatConfig config;
        config.sectorsPerCluster = sectors;
        return config;
    }

    fs::SparseBlockDevice::WithStorage<160> device;
    fs::FatFormatter::WithStorage<8> formatter{ device };
    std::optional<fs::FatImageBuilder> builder;
    fs::FatGeometry geometry;
    fs::FlushBarrier barrier{ device };
    fs::SectorCache::WithStorage<4> cache{ barrier };
};

#endif
