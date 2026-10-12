#ifndef FS_FAT_FAT_GEOMETRY_HPP
#define FS_FAT_FAT_GEOMETRY_HPP

#include "fs/fat/FatBootSector.hpp"
#include "fs/interfaces/Result.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct FatGeometry
    {
        FatType type{ FatType::fat12 };
        uint32_t sectorsPerCluster{ 0 };
        uint32_t firstFatSector{ 0 };
        uint32_t sectorsPerFat{ 0 };
        uint8_t fatCopies{ 0 };
        uint8_t activeFat{ 0 };
        bool mirrored{ true };
        uint32_t rootSector{ 0 };
        uint32_t rootSectors{ 0 };
        uint32_t rootCluster{ 0 };
        uint32_t firstDataSector{ 0 };
        uint32_t clusterCount{ 0 };
        std::optional<uint32_t> fsInfoSector;

        uint32_t SectorOfCluster(uint32_t cluster) const;
        uint32_t ActiveFatSector() const;
        bool IsCluster(uint32_t cluster) const;

        bool operator==(const FatGeometry& other) const = default;
    };

    struct FatGeometryResult
    {
        Result result{ Result::success };
        FatGeometry geometry;
    };

    FatType TypeOfClusterCount(uint32_t clusters);
    FatGeometryResult GeometryOf(const BiosParameterBlock& parameters, uint32_t volumeStart, uint32_t volumeSectors, uint32_t blockSize);
}

#endif
