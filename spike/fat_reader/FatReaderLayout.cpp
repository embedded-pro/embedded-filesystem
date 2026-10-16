#include "spike/fat_reader/FatReaderLayout.hpp"
#include "fs/common/LittleEndian.hpp"
#include <bit>

namespace spike
{
    namespace
    {
        constexpr std::size_t signatureOffset = 510;
        constexpr std::size_t partitionTableOffset = 446;
        constexpr std::size_t partitionEntryBytes = 16;
        constexpr std::size_t partitions = 4;
        constexpr uint8_t protectiveGpt = 0xee;
        constexpr uint32_t fat12Clusters = 4085;
        constexpr uint32_t fat16Clusters = 65525;
        constexpr uint8_t mirroringDisabled = 0x80;
        constexpr uint8_t activeFatMask = 0x0f;

        struct BiosParameters
        {
            uint8_t sectorsPerCluster;
            uint16_t reserved;
            uint8_t fats;
            uint32_t rootEntries;
            uint32_t total;
            uint32_t fatSize;
        };

        bool HasSignature(infra::ConstByteRange sector)
        {
            return sector[signatureOffset] == 0x55 && sector[signatureOffset + 1] == 0xaa;
        }

        bool IsFatPartition(uint8_t type)
        {
            return type == 0x01 || type == 0x04 || type == 0x06 || type == 0x0b || type == 0x0c || type == 0x0e;
        }

        FatKind KindOf(uint32_t clusters)
        {
            if (clusters < fat12Clusters)
                return FatKind::fat12;
            if (clusters < fat16Clusters)
                return FatKind::fat16;
            return FatKind::fat32;
        }

        uint32_t EntryBits(FatKind kind)
        {
            if (kind == FatKind::fat12)
                return 12;
            return kind == FatKind::fat16 ? 16 : 32;
        }

        uint32_t Either16Or32(infra::ConstByteRange sector, std::size_t offset16, std::size_t offset32)
        {
            auto value = fs::LoadLittle16(sector, offset16);
            return value != 0 ? value : fs::LoadLittle32(sector, offset32);
        }

        BiosParameters ReadParameters(infra::ConstByteRange sector)
        {
            return BiosParameters{ sector[13], fs::LoadLittle16(sector, 14), sector[16], fs::LoadLittle16(sector, 17), Either16Or32(sector, 19, 32), Either16Or32(sector, 22, 36) };
        }

        bool Plausible(infra::ConstByteRange sector, const BiosParameters& parameters)
        {
            return HasSignature(sector) && (sector[0] == 0xeb || sector[0] == 0xe9) && std::has_single_bit(parameters.sectorsPerCluster) && parameters.reserved != 0 && parameters.fats != 0 && parameters.fatSize != 0;
        }

        uint32_t ActiveFat(infra::ConstByteRange sector, FatKind kind)
        {
            uint8_t flags = sector[40];
            return kind == FatKind::fat32 && (flags & mirroringDisabled) != 0 ? flags & activeFatMask : 0;
        }

        bool Consistent(const FatLayout& layout, const BiosParameters& parameters, uint32_t active)
        {
            bool fixedRoot = parameters.rootEntries != 0;
            uint64_t fatBits = uint64_t{ parameters.fatSize } * fatSectorBytes * 8;

            return (layout.kind == FatKind::fat32) != fixedRoot && active < parameters.fats && fatBits >= (uint64_t{ layout.clusters } + 2) * EntryBits(layout.kind) && (fixedRoot || IsCluster(layout, layout.rootCluster));
        }
    }

    fs::Result ParseBootSector(infra::ConstByteRange sector, uint32_t start, uint32_t deviceBlocks, FatLayout& layout)
    {
        auto parameters = ReadParameters(sector);

        if (!Plausible(sector, parameters))
            return fs::Result::corrupted;
        if (fs::LoadLittle16(sector, 11) != fatSectorBytes)
            return fs::Result::unsupported;

        uint32_t rootSectors = (parameters.rootEntries * 32 + fatSectorBytes - 1) / fatSectorBytes;
        uint64_t system = parameters.reserved + uint64_t{ parameters.fats } * parameters.fatSize + rootSectors;

        if (system >= parameters.total || uint64_t{ start } + parameters.total > deviceBlocks)
            return fs::Result::corrupted;

        layout.clusterShift = static_cast<uint8_t>(std::countr_zero(parameters.sectorsPerCluster));
        layout.clusters = static_cast<uint32_t>((parameters.total - system) >> layout.clusterShift);
        layout.kind = KindOf(layout.clusters);
        layout.rootCluster = fs::LoadLittle32(sector, 44);

        auto active = ActiveFat(sector, layout.kind);
        if (!Consistent(layout, parameters, active))
            return fs::Result::corrupted;

        layout.fatStart = start + parameters.reserved + active * parameters.fatSize;
        layout.rootStart = start + parameters.reserved + parameters.fats * parameters.fatSize;
        layout.rootSectors = rootSectors;
        layout.dataStart = layout.rootStart + rootSectors;
        return fs::Result::success;
    }

    fs::Result FindPartition(infra::ConstByteRange sector, uint32_t& start)
    {
        if (!HasSignature(sector))
            return fs::Result::corrupted;

        for (std::size_t index = 0; index != partitions; ++index)
        {
            auto entry = infra::DiscardHead(sector, partitionTableOffset + index * partitionEntryBytes);

            if (entry[4] == protectiveGpt)
                return fs::Result::unsupported;

            if (IsFatPartition(entry[4]))
            {
                start = fs::LoadLittle32(entry, 8);
                return fs::Result::success;
            }
        }

        return fs::Result::corrupted;
    }

    bool IsCluster(const FatLayout& layout, uint32_t cluster)
    {
        return cluster >= 2 && cluster - 2 < layout.clusters;
    }
}
