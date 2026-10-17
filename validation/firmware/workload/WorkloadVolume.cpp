#include "validation/firmware/workload/WorkloadVolume.hpp"

namespace validation
{
    VolumeCounts VolumeCounts::operator-(const VolumeCounts& earlier) const
    {
        return { allocatedSectors - earlier.allocatedSectors, erasedBlocks - earlier.erasedBlocks, checkpoints - earlier.checkpoints, movedBlocks - earlier.movedBlocks };
    }

    VolumeCounts VolumeCounts::operator+(const VolumeCounts& other) const
    {
        return { allocatedSectors + other.allocatedSectors, erasedBlocks + other.erasedBlocks, checkpoints + other.checkpoints, movedBlocks + other.movedBlocks };
    }
}
