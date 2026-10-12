#include "metrics/fat/FatSweep.hpp"
#include "fs/fat/FatFormatter.hpp"
#include <chrono>

namespace metrics
{
    using namespace std::chrono_literals;

    namespace
    {
        constexpr BlockDeviceTiming typicalTiming{ 100us, 25us, 50us, 0us, 10ms };
        constexpr BlockDeviceTiming maximumTiming{ 100us, 25us, 50us, 250ms, 10ms };
    }

    const BlockDeviceModel fatSweep12{ "fat12-2m", 512, 4096, 64, 2, typicalTiming, maximumTiming };
    const BlockDeviceModel fatSweep16{ "fat16-8m", 512, 16384, 64, 2, typicalTiming, maximumTiming };
    const BlockDeviceModel fatSweep32{ "fat32-40m", 512, 81920, 64, 2, typicalTiming, maximumTiming };

    fs::FatFormatConfig SweepFormat(const BlockDeviceModel& model)
    {
        fs::FatFormatConfig config;

        if (model.numberOfBlocks > fatSweep12.numberOfBlocks)
            config.sectorsPerCluster = 1;

        return config;
    }

    std::optional<fs::Result> Format(SweepContext<hal::BlockDevice>& context, const fs::FatFormatConfig& config)
    {
        fs::FatFormatter::WithStorage<8> formatter{ context.device };
        std::optional<fs::Result> result;
        formatter.Format(config, [&result](fs::Result formatted)
            {
                result = formatted;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });
        return result;
    }

    SectorReader ReaderOf(SweepContext<hal::BlockDevice>& context)
    {
        return [&context](uint32_t sector)
        {
            std::vector<uint8_t> contents(context.device.BlockSize());
            bool done = false;
            context.device.ReadBlocks(infra::MakeRange(contents), sector, [&done](hal::BlockDevice::Result)
                {
                    done = true;
                });
            context.time.RunUntil([&done]
                {
                    return done;
                });
            return contents;
        };
    }
}
