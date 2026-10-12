#include "fs/fat/FatPartitionTable.hpp"
#include "fs/fat/FatBytes.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <array>

namespace fs
{
    namespace
    {
        constexpr std::size_t sectorBytes = 512;
        constexpr std::size_t tableOffset = 446;
        constexpr std::size_t entryBytes = 16;
        constexpr std::size_t entries = 4;
        constexpr uint8_t protectiveType = 0xee;
        constexpr std::array<uint8_t, 6> fatTypes{ 0x01, 0x04, 0x06, 0x0b, 0x0c, 0x0e };
        constexpr std::array<uint8_t, 3> addressedByBlock{ 0xfe, 0xff, 0xff };

        PartitionEntry EntryAt(infra::ConstByteRange sector, std::size_t index)
        {
            auto offset = tableOffset + index * entryBytes;

            return PartitionEntry{ sector[offset + 4], LoadLittle32(sector, offset + 8), LoadLittle32(sector, offset + 12) };
        }

        bool IsFatType(uint8_t type)
        {
            return std::ranges::find(fatTypes, type) != fatTypes.end();
        }
    }

    std::optional<PartitionEntry> FirstFatPartition(infra::ConstByteRange sector)
    {
        if (!HasBootSignature(sector))
            return std::nullopt;

        for (std::size_t index = 0; index != entries; ++index)
        {
            auto entry = EntryAt(sector, index);

            if (IsFatType(entry.type) && entry.sectors != 0)
                return entry;
        }

        return std::nullopt;
    }

    bool IsProtectiveMbr(infra::ConstByteRange sector)
    {
        if (!HasBootSignature(sector))
            return false;

        for (std::size_t index = 0; index != entries; ++index)
            if (EntryAt(sector, index).type == protectiveType)
                return true;

        return false;
    }

    void SerializeMbr(const PartitionEntry& partition, infra::ByteRange sector)
    {
        really_assert(sector.size() >= sectorBytes);

        std::ranges::fill(sector, uint8_t{ 0 });
        infra::Copy(infra::MakeRange(addressedByBlock), infra::Head(infra::DiscardHead(sector, tableOffset + 1), addressedByBlock.size()));
        sector[tableOffset + 4] = partition.type;
        infra::Copy(infra::MakeRange(addressedByBlock), infra::Head(infra::DiscardHead(sector, tableOffset + 5), addressedByBlock.size()));
        StoreLittle32(sector, tableOffset + 8, partition.firstSector);
        StoreLittle32(sector, tableOffset + 12, partition.sectors);
        StoreBootSignature(sector);
    }
}
