#include "validation/firmware/workload/FatWorkloadVolume.hpp"
#include "fs/common/TransferAlignment.hpp"

namespace validation
{
    namespace
    {
        fs::FatConfig ConfigFor(uint32_t unitBlocks)
        {
            fs::FatConfig config;
            config.blocksPerAllocationUnit = unitBlocks;
            config.transferAlignment = fs::bufferAlignment;
            return config;
        }
    }

    FatWorkloadVolume::FatWorkloadVolume(hal::BlockDevice& device, uint32_t unitBlocks)
        : unitBlocks(unitBlocks)
        , fileSystem(device, ConfigFor(unitBlocks))
        , formatter(device)
    {}

    infra::BoundedConstString FatWorkloadVolume::Subject() const
    {
        return "embedded-filesystem/fat";
    }

    // As on the host, which counts the RAM of a volume with one open file
    std::size_t FatWorkloadVolume::RamBytes() const
    {
        return sizeof(fs::FatFileSystem::WithStorage<1, 8>);
    }

    fs::FileSystem& FatWorkloadVolume::FileSystem()
    {
        return fileSystem;
    }

    void FatWorkloadVolume::Format(const infra::Function<void(fs::Result result)>& onDone)
    {
        fs::FatFormatConfig config;
        config.alignmentSectors = unitBlocks;
        formatter.Format(config, onDone);
    }

    std::optional<VolumeCounts> FatWorkloadVolume::Counters() const
    {
        return std::nullopt;
    }
}
