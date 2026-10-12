#include "fs/fat/FatFormatLayout.hpp"
#include "fs/fat/FatGeometry.hpp"
#include <algorithm>
#include <array>
#include <bit>

namespace fs
{
    namespace
    {
        constexpr uint32_t sectorBytes = 512;
        constexpr uint32_t entryBytes = 32;
        constexpr uint16_t fat32ReservedSectors = 32;
        constexpr uint16_t fat32FsInfoSector = 1;
        constexpr uint16_t fat32BackupBootSector = 6;
        constexpr uint32_t fat32RootCluster = 2;
        constexpr uint8_t fixedDiskMedia = 0xf8;
        constexpr uint32_t largestFat16PartitionType04 = 65536;

        struct CapacityRow
        {
            uint32_t sectors;
            SdFormatParameters parameters;
        };

        constexpr std::array<CapacityRow, 6> sdTable{ {
            { 16384, { FatType::fat12, 16, 16 } },
            { 131072, { FatType::fat12, 32, 32 } },
            { 524288, { FatType::fat16, 32, 32 } },
            { 2097152, { FatType::fat16, 32, 64 } },
            { 4194304, { FatType::fat16, 64, 64 } },
            { 67108864, { FatType::fat32, 64, 8192 } },
        } };

        constexpr SdFormatParameters largerThanSdhc{ FatType::fat32, 64, 32768 };

        uint32_t FatEntryBits(FatType type)
        {
            switch (type)
            {
                case FatType::fat12:
                    return 12;
                case FatType::fat16:
                    return 16;
                default:
                    return 32;
            }
        }

        uint32_t SectorsPerFat(FatType type, uint64_t clusters)
        {
            auto bytes = ((clusters + 2) * FatEntryBits(type) + 7) / 8;
            return static_cast<uint32_t>((bytes + sectorBytes - 1) / sectorBytes);
        }

        uint32_t Padding(uint64_t sector, uint32_t alignment)
        {
            return static_cast<uint32_t>((alignment - sector % alignment) % alignment);
        }

        uint8_t PartitionType(FatType type, uint32_t sectors)
        {
            if (type == FatType::fat12)
                return 0x01;
            if (type == FatType::fat16)
                return sectors < largestFat16PartitionType04 ? 0x04 : 0x06;

            return 0x0c;
        }

        struct Shape
        {
            FatType type;
            uint32_t sectorsPerCluster;
            uint32_t alignment;
        };

        BiosParameterBlock BaseParameters(const Shape& shape, const FatFormatConfig& config, uint32_t sectorsPerFat)
        {
            BiosParameterBlock parameters;
            parameters.bytesPerSector = sectorBytes;
            parameters.sectorsPerCluster = static_cast<uint8_t>(shape.sectorsPerCluster);
            parameters.fatCopies = config.fatCopies;
            parameters.media = fixedDiskMedia;
            parameters.sectorsPerFat = sectorsPerFat;
            parameters.volumeId = config.volumeId;

            if (shape.type == FatType::fat32)
            {
                parameters.fat32Layout = true;
                parameters.rootCluster = fat32RootCluster;
                parameters.fsInfoSector = fat32FsInfoSector;
                parameters.backupBootSector = fat32BackupBootSector;
            }
            else
                parameters.rootEntries = config.rootEntries;

            return parameters;
        }

        uint32_t SystemSectorsWithoutReserved(const BiosParameterBlock& parameters)
        {
            return parameters.fatCopies * parameters.sectorsPerFat + (parameters.rootEntries * entryBytes + sectorBytes - 1) / sectorBytes;
        }

        uint32_t VolumeStart(const Shape& shape, const FatFormatConfig& config, uint16_t reserved, uint32_t systemSectors)
        {
            if (!config.partitioned)
                return 0;

            if (shape.type == FatType::fat32)
                return shape.alignment;

            auto start = Padding(reserved + systemSectors, shape.alignment);

            if (start == 0)
                return shape.alignment;

            return start;
        }

        uint16_t ReservedSectors(const Shape& shape, const FatFormatConfig& config, uint32_t volumeStart, uint32_t systemSectors)
        {
            uint16_t minimum = shape.type == FatType::fat32 ? fat32ReservedSectors : 1;

            if (config.partitioned && shape.type != FatType::fat32)
                return minimum;

            return static_cast<uint16_t>(minimum + Padding(uint64_t{ volumeStart } + minimum + systemSectors, shape.alignment));
        }

        std::optional<FatFormatLayout> LayoutFor(const Shape& shape, uint32_t blocks, const FatFormatConfig& config)
        {
            auto estimate = SectorsPerFat(shape.type, blocks / shape.sectorsPerCluster);
            auto parameters = BaseParameters(shape, config, estimate);
            auto systemSectors = SystemSectorsWithoutReserved(parameters);
            uint16_t minimumReserved = shape.type == FatType::fat32 ? fat32ReservedSectors : 1;
            auto volumeStart = VolumeStart(shape, config, minimumReserved, systemSectors);
            parameters.reservedSectors = ReservedSectors(shape, config, volumeStart, systemSectors);

            uint64_t dataStart = uint64_t{ volumeStart } + parameters.reservedSectors + systemSectors;

            if (dataStart + shape.sectorsPerCluster > blocks)
                return std::nullopt;

            parameters.totalSectors = blocks - volumeStart;
            parameters.hiddenSectors = volumeStart;
            auto clusterCount = static_cast<uint32_t>((blocks - dataStart) / shape.sectorsPerCluster);

            FatFormatLayout layout{ shape.type, std::nullopt, parameters, volumeStart, static_cast<uint32_t>(dataStart), clusterCount };

            if (config.partitioned)
                layout.partition = PartitionEntry{ PartitionType(shape.type, parameters.totalSectors), volumeStart, parameters.totalSectors };

            return layout;
        }

        Shape ShapeFor(uint32_t blocks, const FatFormatConfig& config)
        {
            auto parameters = SdParametersFor(blocks);
            auto sectorsPerCluster = config.sectorsPerCluster.value_or(parameters.sectorsPerCluster);

            return Shape{ parameters.type, sectorsPerCluster, std::max(config.alignmentSectors.value_or(parameters.boundaryUnitSectors), uint32_t{ sectorsPerCluster }) };
        }

        bool IsValid(const FatFormatConfig& config, uint32_t blockSize)
        {
            return blockSize == sectorBytes && config.fatCopies != 0 && (!config.sectorsPerCluster.has_value() || std::has_single_bit(*config.sectorsPerCluster)) && (!config.alignmentSectors.has_value() || *config.alignmentSectors != 0);
        }
    }

    SdFormatParameters SdParametersFor(uint32_t sectors)
    {
        for (const auto& row : sdTable)
            if (sectors <= row.sectors)
                return row.parameters;

        return largerThanSdhc;
    }

    std::optional<FatFormatLayout> ComputeFormatLayout(uint32_t blocks, uint32_t blockSize, const FatFormatConfig& config)
    {
        if (!IsValid(config, blockSize))
            return std::nullopt;

        auto shape = ShapeFor(blocks, config);

        for (int attempt = 0; attempt != 2; ++attempt)
        {
            auto layout = LayoutFor(shape, blocks, config);

            if (!layout)
                return std::nullopt;

            auto type = TypeOfClusterCount(layout->clusterCount);

            if (type == shape.type)
                return layout;

            shape.type = type;
        }

        return std::nullopt;
    }
}
