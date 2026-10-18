#ifndef VALIDATION_FIRMWARE_WORKLOAD_FAT_WORKLOAD_VOLUME_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_FAT_WORKLOAD_VOLUME_HPP

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
        FatWorkloadVolume(hal::BlockDevice& device, uint32_t unitBlocks);

        infra::BoundedConstString Subject() const override;
        std::size_t RamBytes() const override;
        fs::FileSystem& FileSystem() override;
        void Format(const infra::Function<void(fs::Result result)>& onDone) override;
        std::optional<VolumeCounts> Counters() const override;

    private:
        uint32_t unitBlocks;
        fs::FatFileSystem::WithStorage<2, 8> fileSystem;
        fs::FatFormatter::WithStorage<8> formatter;
    };
}

#endif
