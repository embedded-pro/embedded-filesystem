#include "metrics/nor/NorSweep.hpp"
#include "fs/nor/NorFormatter.hpp"
#include <chrono>

namespace metrics
{
    using namespace std::chrono_literals;

    namespace
    {
        constexpr NorTiming qspiTypical{ 400us, 45ms, 50'000'000 };
        constexpr NorTiming qspiMaximum{ 3ms, 400ms, 50'000'000 };
        constexpr NorTiming internalTiming{ 8us, 20ms, 100'000'000 };

        void Wait(SweepContext<hal::Flash>& context, const bool& done)
        {
            context.time.RunUntil([&done]
                {
                    return done;
                });
        }
    }

    const NorModel norSweep128k{ "nor-128k", 32, 4096, 256, 1, qspiTypical, qspiMaximum };
    const NorModel internalSweep96k{ "internal-96k", 48, 2048, 16, 16, internalTiming, internalTiming };
    const NorModel norSweep128kTwoSectors{ "nor-128k-two-sector-blocks", 64, 2048, 256, 1, qspiTypical, qspiMaximum };

    fs::NorConfig SweepConfig(const NorModel& model)
    {
        fs::NorConfig config;
        config.maxBlocks = model.numberOfSectors;
        config.sectorsPerBlock = &model == &norSweep128kTwoSectors ? 2 : 1;
        config.programUnit = model.programUnit;
        config.pageSize = model.pageSize;
        config.cachedNodes = fs::norMinimumCachedNodes;
        return config;
    }

    NorImageSuperblock SuperblockOf(const NorModel& model, const fs::NorConfig& config)
    {
        return { config.sectorsPerBlock, model.sectorSize, model.numberOfSectors / config.sectorsPerBlock, config.programUnit, config.nodeSize, config.inlineBytes, config.maxNameBytes };
    }

    std::optional<fs::Result> Format(SweepContext<hal::Flash>& context, const fs::NorConfig& config)
    {
        std::vector<uint8_t> buffer(fs::NorFormatter::BufferBytes(config));
        fs::NorFormatter formatter{ context.device, config, infra::MakeRange(buffer) };
        std::optional<fs::Result> result;
        formatter.Format([&result](fs::Result formatted)
            {
                result = formatted;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });
        return result;
    }

    std::vector<uint8_t> ReadImage(SweepContext<hal::Flash>& context)
    {
        std::vector<uint8_t> image;

        for (uint32_t sector = 0; sector != context.device.NumberOfSectors(); ++sector)
        {
            std::vector<uint8_t> contents(context.device.SizeOfSector(sector));
            bool done = false;
            context.device.ReadBuffer(infra::MakeRange(contents), context.device.AddressOfSector(sector), [&done]
                {
                    done = true;
                });
            Wait(context, done);
            image.insert(image.end(), contents.begin(), contents.end());
        }

        return image;
    }

    NorCheck CheckImage(SweepContext<hal::Flash>& context, const NorModel& model)
    {
        return CheckNorImage(ReadImage(context), model.sectorSize, model.pageSize);
    }

    void Program(SweepContext<hal::Flash>& context, uint32_t address, const std::vector<uint8_t>& bytes)
    {
        bool done = false;
        context.device.WriteBuffer(infra::MakeRange(bytes), address, [&done]
            {
                done = true;
            });
        Wait(context, done);
    }

    void EraseSector(SweepContext<hal::Flash>& context, uint32_t sector)
    {
        bool done = false;
        context.device.EraseSectors(sector, sector + 1, [&done]
            {
                done = true;
            });
        Wait(context, done);
    }
}
