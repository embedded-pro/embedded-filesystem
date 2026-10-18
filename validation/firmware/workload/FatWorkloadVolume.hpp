#ifndef VALIDATION_FIRMWARE_WORKLOAD_FAT_WORKLOAD_VOLUME_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_FAT_WORKLOAD_VOLUME_HPP

#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"
#include <cstdint>

namespace validation
{
    // The FAT subject of the host on a block device, with the allocation unit of the device, and transfers aligned for a
    // driver that moves them by DMA
    class FatWorkloadVolume
        : public WorkloadVolume
    {
    public:
        FatWorkloadVolume(hal::BlockDevice& device, uint32_t unitBlocks)
            : unitBlocks(unitBlocks)
            , fileSystem(device, Config(unitBlocks))
            , formatter(device)
        {}

        infra::BoundedConstString Subject() const override
        {
            return "embedded-filesystem/fat";
        }

        // As on the host, which counts the RAM of a volume with one open file
        std::size_t RamBytes() const override
        {
            return sizeof(fs::FatFileSystem::WithStorage<1, 8>);
        }

        fs::FileSystem& FileSystem() override
        {
            return fileSystem;
        }

        void Format(const infra::Function<void(fs::Result result)>& onDone) override
        {
            fs::FatFormatConfig config;
            config.alignmentSectors = unitBlocks;
            formatter.Format(config, onDone);
        }

        std::optional<VolumeCounts> Counters() const override
        {
            return std::nullopt;
        }

    private:
        static fs::FatConfig Config(uint32_t unitBlocks)
        {
            fs::FatConfig config;
            config.blocksPerAllocationUnit = unitBlocks;
            config.transferAlignment = fs::bufferAlignment;
            return config;
        }

    private:
        uint32_t unitBlocks;
        fs::FatFileSystem::WithStorage<2, 8> fileSystem;
        fs::FatFormatter::WithStorage<8> formatter;
    };
}

#endif
