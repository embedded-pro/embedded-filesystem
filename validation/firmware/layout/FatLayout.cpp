#include "validation/firmware/layout/FatLayout.hpp"
#include <array>

namespace validation
{
    namespace
    {
        constexpr std::size_t sectorBytes = 512;
        constexpr std::size_t firstPartition = 446;
        constexpr uint32_t fat12Clusters = 4085;
        constexpr uint32_t fat16Clusters = 65525;

        uint32_t Little(infra::ConstByteRange sector, std::size_t offset, std::size_t bytes)
        {
            uint32_t value = 0;
            for (auto byte = bytes; byte != 0; --byte)
                value = value << 8 | sector[offset + byte - 1];
            return value;
        }

        bool Signed(infra::ConstByteRange sector)
        {
            return sector.size() >= sectorBytes && sector[510] == 0x55 && sector[511] == 0xaa;
        }

        bool StartsWithJump(infra::ConstByteRange sector)
        {
            return (sector[0] == 0xeb && sector[2] == 0x90) || sector[0] == 0xe9;
        }

        bool IsExFat(infra::ConstByteRange sector)
        {
            static constexpr std::array<uint8_t, 8> name{ 'E', 'X', 'F', 'A', 'T', ' ', ' ', ' ' };
            for (std::size_t index = 0; index != name.size(); ++index)
                if (sector[3 + index] != name[index])
                    return false;

            return true;
        }

        LayoutType TypeOf(uint32_t clusters)
        {
            if (clusters < fat12Clusters)
                return LayoutType::fat12;
            if (clusters < fat16Clusters)
                return LayoutType::fat16;
            return LayoutType::fat32;
        }

        LayoutType TypeOf(fs::FatType type)
        {
            switch (type)
            {
                case fs::FatType::fat12:
                    return LayoutType::fat12;
                case fs::FatType::fat16:
                    return LayoutType::fat16;
                default:
                    return LayoutType::fat32;
            }
        }

        void Differ(bool differs, const char* field, std::size_t& count, infra::BoundedString& names)
        {
            if (!differs)
                return;

            if (count != 0)
                names.append(",");
            names.append(field);
            ++count;
        }
    }

    std::optional<VolumePlace> LocateVolume(infra::ConstByteRange firstSector)
    {
        if (!Signed(firstSector))
            return std::nullopt;

        if (StartsWithJump(firstSector) && Little(firstSector, 11, 2) == sectorBytes)
            return VolumePlace{ 0, 0 };

        auto type = firstSector[firstPartition + 4];
        auto start = Little(firstSector, firstPartition + 8, 4);
        auto sectors = Little(firstSector, firstPartition + 12, 4);
        if (type == 0 || sectors == 0)
            return std::nullopt;

        return VolumePlace{ start, sectors };
    }

    std::optional<FatLayout> ParseVolume(infra::ConstByteRange bootSector, const VolumePlace& place)
    {
        if (!Signed(bootSector))
            return std::nullopt;

        if (IsExFat(bootSector))
            return FatLayout{ place.start, place.sectors, LayoutType::exfat };

        auto sectorsPerCluster = bootSector[13];
        auto reserved = Little(bootSector, 14, 2);
        auto fats = bootSector[16];
        auto rootEntries = Little(bootSector, 17, 2);
        auto total = Little(bootSector, 19, 2) != 0 ? Little(bootSector, 19, 2) : Little(bootSector, 32, 4);
        auto fatSectors = Little(bootSector, 22, 2) != 0 ? Little(bootSector, 22, 2) : Little(bootSector, 36, 4);
        auto rootSectors = (rootEntries * 32 + sectorBytes - 1) / sectorBytes;
        auto systemSectors = uint64_t{ reserved } + uint64_t{ fats } * fatSectors + rootSectors;
        if (Little(bootSector, 11, 2) != sectorBytes || sectorsPerCluster == 0 || fats == 0 || systemSectors >= total)
            return std::nullopt;

        auto clusters = static_cast<uint32_t>((total - systemSectors) / sectorsPerCluster);
        auto sectors = place.sectors != 0 ? place.sectors : total;
        return FatLayout{ place.start, sectors, TypeOf(clusters), sectorsPerCluster, reserved, fats, fatSectors, rootEntries,
            static_cast<uint32_t>(place.start + systemSectors) };
    }

    FatLayout FormatterLayout(const fs::FatFormatLayout& layout)
    {
        const auto& parameters = layout.parameters;
        auto start = layout.partition ? layout.partition->firstSector : layout.volumeStart;
        auto sectors = layout.partition ? layout.partition->sectors : parameters.totalSectors;
        return { start, sectors, TypeOf(layout.type), parameters.sectorsPerCluster, parameters.reservedSectors, parameters.fatCopies,
            parameters.sectorsPerFat, parameters.rootEntries, layout.dataStart };
    }

    // An SDXC card holds exFAT, where the formatter makes FAT32, so only the start of its partition compares
    std::size_t Differences(const FatLayout& card, const FatLayout& formatter, infra::BoundedString& names)
    {
        names.clear();
        std::size_t count = 0;
        Differ(card.partitionStart != formatter.partitionStart, "partitionStart", count, names);
        if (card.type == LayoutType::exfat)
            return count;

        Differ(card.partitionSectors != formatter.partitionSectors, "partitionSectors", count, names);
        Differ(card.type != formatter.type, "type", count, names);
        Differ(card.sectorsPerCluster != formatter.sectorsPerCluster, "sectorsPerCluster", count, names);
        Differ(card.reservedSectors != formatter.reservedSectors, "reservedSectors", count, names);
        Differ(card.fats != formatter.fats, "fats", count, names);
        Differ(card.fatSectors != formatter.fatSectors, "fatSectors", count, names);
        Differ(card.rootEntries != formatter.rootEntries, "rootEntries", count, names);
        Differ(card.dataStart != formatter.dataStart, "dataStart", count, names);
        return count;
    }

    const char* TypeName(LayoutType type)
    {
        switch (type)
        {
            case LayoutType::fat12:
                return "fat12";
            case LayoutType::fat16:
                return "fat16";
            case LayoutType::fat32:
                return "fat32";
            default:
                return "exfat";
        }
    }

    void AddLayout(ResultRecord& record, const FatLayout& layout)
    {
        record.Add("partitionStart", layout.partitionStart)
            .Add("partitionSectors", layout.partitionSectors)
            .Add("type", TypeName(layout.type))
            .Add("sectorsPerCluster", layout.sectorsPerCluster)
            .Add("reservedSectors", layout.reservedSectors)
            .Add("fats", layout.fats)
            .Add("fatSectors", layout.fatSectors)
            .Add("rootEntries", layout.rootEntries)
            .Add("dataStart", layout.dataStart);
    }
}
