#ifndef VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_VOLUME_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_WORKLOAD_VOLUME_HPP

#include "fs/interfaces/FileSystem.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    struct VolumeCounts
    {
        uint64_t allocatedSectors{};
        uint64_t erasedBlocks{};
        uint64_t checkpoints{};
        uint64_t movedBlocks{};

        VolumeCounts operator-(const VolumeCounts& earlier) const;
        VolumeCounts operator+(const VolumeCounts& other) const;
        bool operator==(const VolumeCounts& other) const = default;
    };

    class WorkloadVolume
    {
    protected:
        WorkloadVolume() = default;
        WorkloadVolume(const WorkloadVolume& other) = delete;
        WorkloadVolume& operator=(const WorkloadVolume& other) = delete;
        ~WorkloadVolume() = default;

    public:
        virtual infra::BoundedConstString Subject() const = 0;
        virtual std::size_t RamBytes() const = 0;
        virtual fs::FileSystem& FileSystem() = 0;
        virtual void Format(const infra::Function<void(fs::Result result)>& onDone) = 0;
        virtual std::optional<VolumeCounts> Counters() const = 0;
    };
}

#endif
