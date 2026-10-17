#include "fs/fat/FatGeometry.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t firstCluster = 2;
        constexpr uint32_t entryBytes = 32;
        constexpr uint32_t fat12Clusters = 4085;
        constexpr uint32_t fat16Clusters = 65525;
        constexpr uint16_t mirroringDisabled = 0x80;
        constexpr uint16_t activeFatMask = 0x0f;

        uint64_t FatBytesNeeded(FatType type, uint32_t clusters)
        {
            uint64_t entries = clusters + uint64_t{ firstCluster };

            switch (type)
            {
                case FatType::fat12:
                    return (entries * 3 + 1) / 2;
                case FatType::fat16:
                    return entries * 2;
                default:
                    return entries * 4;
            }
        }

        struct Layout
        {
            uint32_t rootSectors;
            uint64_t systemSectors;
        };

        Layout LayoutOf(const BiosParameterBlock& parameters)
        {
            uint32_t rootSectors = (parameters.rootEntries * entryBytes + parameters.bytesPerSector - 1) / parameters.bytesPerSector;

            return Layout{ rootSectors, parameters.reservedSectors + uint64_t{ parameters.fatCopies } * parameters.sectorsPerFat + rootSectors };
        }

        Result ValidateLayout(const BiosParameterBlock& parameters, const Layout& layout, uint32_t volumeSectors)
        {
            if (parameters.totalSectors == 0 || parameters.totalSectors > volumeSectors || parameters.sectorsPerFat == 0)
                return Result::corrupted;

            if (layout.systemSectors + parameters.sectorsPerCluster > parameters.totalSectors)
                return Result::corrupted;

            return Result::success;
        }

        Result ValidateFat32(const BiosParameterBlock& parameters, const FatGeometry& geometry)
        {
            if (!parameters.fat32Layout || parameters.rootEntries != 0)
                return Result::corrupted;

            if (parameters.fileSystemVersion != 0)
                return Result::unsupported;

            if (!geometry.IsCluster(parameters.rootCluster) || geometry.activeFat >= geometry.fatCopies)
                return Result::corrupted;

            return Result::success;
        }

        Result ValidateType(const BiosParameterBlock& parameters, const FatGeometry& geometry)
        {
            if (static_cast<uint64_t>(parameters.sectorsPerFat) * parameters.bytesPerSector < FatBytesNeeded(geometry.type, geometry.clusterCount))
                return Result::corrupted;

            if (geometry.type == FatType::fat32)
                return ValidateFat32(parameters, geometry);

            if (parameters.fat32Layout || parameters.rootEntries == 0)
                return Result::corrupted;

            return Result::success;
        }

        void SetFat32Fields(const BiosParameterBlock& parameters, uint32_t volumeStart, FatGeometry& geometry)
        {
            geometry.rootCluster = parameters.rootCluster;
            geometry.mirrored = (parameters.extendedFlags & mirroringDisabled) == 0;
            geometry.activeFat = geometry.mirrored ? 0 : static_cast<uint8_t>(parameters.extendedFlags & activeFatMask);

            if (parameters.fsInfoSector != 0 && parameters.fsInfoSector < parameters.reservedSectors)
                geometry.fsInfoSector = volumeStart + parameters.fsInfoSector;
        }

        FatGeometry GeometryFrom(const BiosParameterBlock& parameters, const Layout& layout, uint32_t volumeStart)
        {
            FatGeometry geometry;
            geometry.sectorsPerCluster = parameters.sectorsPerCluster;
            geometry.firstFatSector = volumeStart + parameters.reservedSectors;
            geometry.sectorsPerFat = parameters.sectorsPerFat;
            geometry.fatCopies = parameters.fatCopies;
            geometry.rootSector = geometry.firstFatSector + parameters.fatCopies * parameters.sectorsPerFat;
            geometry.rootSectors = layout.rootSectors;
            geometry.firstDataSector = geometry.rootSector + layout.rootSectors;
            geometry.clusterCount = static_cast<uint32_t>(parameters.totalSectors - layout.systemSectors) / parameters.sectorsPerCluster;
            geometry.type = TypeOfClusterCount(geometry.clusterCount);

            if (geometry.type == FatType::fat32)
                SetFat32Fields(parameters, volumeStart, geometry);

            return geometry;
        }
    }

    uint32_t FatGeometry::SectorOfCluster(uint32_t cluster) const
    {
        return firstDataSector + (cluster - firstCluster) * sectorsPerCluster;
    }

    uint32_t FatGeometry::ActiveFatSector() const
    {
        return firstFatSector + activeFat * sectorsPerFat;
    }

    bool FatGeometry::IsCluster(uint32_t cluster) const
    {
        return cluster >= firstCluster && cluster - firstCluster < clusterCount;
    }

    FatType TypeOfClusterCount(uint32_t clusters)
    {
        if (clusters < fat12Clusters)
            return FatType::fat12;

        if (clusters < fat16Clusters)
            return FatType::fat16;

        return FatType::fat32;
    }

    FatGeometryResult GeometryOf(const BiosParameterBlock& parameters, uint32_t volumeStart, uint32_t volumeSectors, uint32_t blockSize)
    {
        if (parameters.bytesPerSector != blockSize)
            return FatGeometryResult{ Result::unsupported, {} };

        auto layout = LayoutOf(parameters);
        auto result = ValidateLayout(parameters, layout, volumeSectors);

        if (result != Result::success)
            return FatGeometryResult{ result, {} };

        auto geometry = GeometryFrom(parameters, layout, volumeStart);
        result = ValidateType(parameters, geometry);

        if (result != Result::success)
            return FatGeometryResult{ result, {} };

        return FatGeometryResult{ Result::success, geometry };
    }
}
