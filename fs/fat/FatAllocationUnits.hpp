#ifndef FS_FAT_FAT_ALLOCATION_UNITS_HPP
#define FS_FAT_FAT_ALLOCATION_UNITS_HPP

#include "fs/fat/FatGeometry.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatAllocationUnits
    {
        uint32_t clustersPerUnit;
        uint32_t firstBoundary;

        bool IsBoundary(uint32_t cluster) const;
        uint32_t NextBoundaryAfter(uint32_t cluster) const;
        uint32_t StartOf(uint32_t cluster) const;
        bool operator==(const FatAllocationUnits& other) const;
    };

    std::optional<FatAllocationUnits> AllocationUnitsOf(const FatGeometry& geometry, uint32_t blocksPerUnit);
}

#endif
