#ifndef VALIDATION_FIRMWARE_WORKLOAD_NOR_WORKLOAD_VOLUME_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_NOR_WORKLOAD_VOLUME_HPP

#include "fs/nor/NorFileSystem.hpp"
#include "hal/interfaces/Flash.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"

namespace validation
{
    template<fs::NorConfig Config>
    class NorWorkloadVolume
        : public WorkloadVolume
    {
    public:
        explicit NorWorkloadVolume(hal::Flash& flash)
            : fileSystem(flash)
        {}

        infra::BoundedConstString Subject() const override
        {
            return "embedded-filesystem/nor";
        }

        // As on the host, which counts the RAM of a volume with one open file
        std::size_t RamBytes() const override
        {
            return sizeof(fs::NorFileSystem::WithStorage<Config, 1>);
        }

        fs::FileSystem& FileSystem() override
        {
            return fileSystem;
        }

        void Format(const infra::Function<void(fs::Result result)>& onDone) override
        {
            fileSystem.Format(onDone);
        }

        std::optional<VolumeCounts> Counters() const override
        {
            auto counters = fileSystem.Counters();
            return VolumeCounts{ uint64_t{ counters.allocatedBlocks } * Config.sectorsPerBlock, counters.erasedSectors / Config.sectorsPerBlock, counters.checkpoints, counters.movedBlocks };
        }

    private:
        fs::NorFileSystem::WithStorage<Config, 2> fileSystem;
    };
}

#endif
