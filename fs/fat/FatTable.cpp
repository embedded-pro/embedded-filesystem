#include "fs/fat/FatTable.hpp"
#include "fs/fat/FatBytes.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t fat32Mask = 0x0fffffff;
        constexpr uint32_t fat16CleanBit = 0x8000;
        constexpr uint32_t fat32CleanBit = 0x08000000;

        uint32_t ByteOffset(FatType type, uint32_t cluster)
        {
            switch (type)
            {
                case FatType::fat12:
                    return cluster + cluster / 2;
                case FatType::fat16:
                    return cluster * 2;
                default:
                    return cluster * 4;
            }
        }

        uint32_t Maximum(FatType type)
        {
            switch (type)
            {
                case FatType::fat12:
                    return 0xfff;
                case FatType::fat16:
                    return 0xffff;
                default:
                    return fat32Mask;
            }
        }

        uint16_t LoadFat12Pair(uint32_t offset, infra::ConstByteRange sector, infra::ConstByteRange nextSector)
        {
            if (offset + 1 < sector.size())
                return LoadLittle16(sector, offset);

            really_assert(!nextSector.empty());
            return static_cast<uint16_t>(sector[offset] | (nextSector[0] << 8));
        }

        void StoreFat12Pair(uint32_t offset, uint16_t pair, infra::ByteRange sector, infra::ByteRange nextSector)
        {
            if (offset + 1 < sector.size())
                StoreLittle16(sector, offset, pair);
            else
            {
                really_assert(!nextSector.empty());
                sector[offset] = static_cast<uint8_t>(pair);
                nextSector[0] = static_cast<uint8_t>(pair >> 8);
            }
        }

        uint32_t ReadFat12(uint32_t cluster, uint32_t offset, infra::ConstByteRange sector, infra::ConstByteRange nextSector)
        {
            auto pair = LoadFat12Pair(offset, sector, nextSector);

            if (cluster % 2 != 0)
                return pair >> 4;

            return pair & 0xfffu;
        }

        void WriteFat12(uint32_t cluster, uint32_t value, uint32_t offset, infra::ByteRange sector, infra::ByteRange nextSector)
        {
            auto pair = LoadFat12Pair(offset, sector, nextSector);

            if (cluster % 2 != 0)
                pair = static_cast<uint16_t>((pair & 0x000fu) | ((value & 0xfffu) << 4));
            else
                pair = static_cast<uint16_t>((pair & 0xf000u) | (value & 0xfffu));

            StoreFat12Pair(offset, pair, sector, nextSector);
        }
    }

    FatEntryLocation LocateEntry(FatType type, uint32_t cluster, uint32_t sectorSize)
    {
        auto offset = ByteOffset(type, cluster);
        auto inSector = offset % sectorSize;

        return FatEntryLocation{ offset / sectorSize, inSector, type == FatType::fat12 && inSector == sectorSize - 1 };
    }

    uint32_t ReadEntry(FatType type, uint32_t cluster, infra::ConstByteRange sector, infra::ConstByteRange nextSector)
    {
        auto offset = ByteOffset(type, cluster) % sector.size();

        switch (type)
        {
            case FatType::fat12:
                return ReadFat12(cluster, offset, sector, nextSector);
            case FatType::fat16:
                return LoadLittle16(sector, offset);
            default:
                return LoadLittle32(sector, offset) & fat32Mask;
        }
    }

    void WriteEntry(FatType type, uint32_t cluster, uint32_t value, infra::ByteRange sector, infra::ByteRange nextSector)
    {
        auto offset = ByteOffset(type, cluster) % sector.size();

        switch (type)
        {
            case FatType::fat12:
                WriteFat12(cluster, value, offset, sector, nextSector);
                break;
            case FatType::fat16:
                StoreLittle16(sector, offset, static_cast<uint16_t>(value));
                break;
            default:
                StoreLittle32(sector, offset, (LoadLittle32(sector, offset) & ~fat32Mask) | (value & fat32Mask));
                break;
        }
    }

    bool IsEndOfChain(FatType type, uint32_t value)
    {
        return value >= Maximum(type) - 7;
    }

    bool IsBadCluster(FatType type, uint32_t value)
    {
        return value == Maximum(type) - 8;
    }

    uint32_t EndOfChain(FatType type)
    {
        return Maximum(type);
    }

    std::optional<bool> CleanShutdown(FatType type, uint32_t secondEntry)
    {
        switch (type)
        {
            case FatType::fat12:
                return std::nullopt;
            case FatType::fat16:
                return (secondEntry & fat16CleanBit) != 0;
            default:
                return (secondEntry & fat32CleanBit) != 0;
        }
    }

    uint32_t WithCleanShutdown(FatType type, uint32_t secondEntry, bool clean)
    {
        really_assert(type != FatType::fat12);

        auto bit = type == FatType::fat16 ? fat16CleanBit : fat32CleanBit;

        if (clean)
            return secondEntry | bit;

        return secondEntry & ~bit;
    }
}
