#include "fs/fat/FatAllocationUnits.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t firstCluster = 2;
    }

    bool FatAllocationUnits::IsBoundary(uint32_t cluster) const
    {
        return cluster >= firstBoundary && (cluster - firstBoundary) % clustersPerUnit == 0;
    }

    uint32_t FatAllocationUnits::NextBoundaryAfter(uint32_t cluster) const
    {
        if (cluster < firstBoundary)
            return firstBoundary;

        return firstBoundary + ((cluster - firstBoundary) / clustersPerUnit + 1) * clustersPerUnit;
    }

    uint32_t FatAllocationUnits::StartOf(uint32_t cluster) const
    {
        if (cluster < firstBoundary)
            return firstCluster;

        return firstBoundary + (cluster - firstBoundary) / clustersPerUnit * clustersPerUnit;
    }

    bool FatAllocationUnits::operator==(const FatAllocationUnits& other) const = default;

    std::optional<FatAllocationUnits> AllocationUnitsOf(const FatGeometry& geometry, uint32_t blocksPerUnit)
    {
        if (blocksPerUnit == 0 || blocksPerUnit % geometry.sectorsPerCluster != 0)
            return std::nullopt;

        auto toBoundary = (blocksPerUnit - geometry.firstDataSector % blocksPerUnit) % blocksPerUnit;

        if (toBoundary % geometry.sectorsPerCluster != 0)
            return std::nullopt;

        return FatAllocationUnits{ blocksPerUnit / geometry.sectorsPerCluster, firstCluster + toBoundary / geometry.sectorsPerCluster };
    }
}
