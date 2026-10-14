#include "fs/nor/NorBlockHeader.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorLayout.hpp"
#include "infra/util/Crc.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t magic = 0x42534645;
        constexpr std::size_t partACrcOffset = 8;
        constexpr std::size_t partBCrcOffset = 20;

        uint32_t Crc(infra::ConstByteRange bytes)
        {
            infra::Crc32 crc;
            crc.Update(bytes);
            return crc.Result();
        }

        bool IsSealed(infra::ConstByteRange bytes, std::size_t crcOffset)
        {
            return LoadLittle32(bytes, crcOffset) == Crc(infra::Head(bytes, crcOffset));
        }

        void Seal(infra::ByteRange bytes, std::size_t crcOffset)
        {
            StoreLittle32(bytes, crcOffset, Crc(infra::Head(bytes, crcOffset)));
        }

        bool IsKnownType(uint8_t type)
        {
            return type >= static_cast<uint8_t>(NorBlockType::checkpoint) && type <= static_cast<uint8_t>(NorBlockType::data);
        }

        bool IsErased(infra::ConstByteRange bytes)
        {
            return std::ranges::all_of(bytes, [](uint8_t byte)
                {
                    return byte == 0xff;
                });
        }

        std::optional<uint32_t> ParsePartA(infra::ConstByteRange partA)
        {
            if (LoadLittle32(partA, 0) != magic || !IsSealed(partA, partACrcOffset))
                return std::nullopt;

            return LoadLittle32(partA, 4);
        }

        std::optional<NorAllocation> ParsePartB(infra::ConstByteRange partB)
        {
            if (!IsKnownType(partB[0]) || !IsSealed(partB, partBCrcOffset))
                return std::nullopt;

            return NorAllocation{ static_cast<NorBlockType>(partB[0]), LoadLittle64(partB, 4), { LoadLittle32(partB, 12), LoadLittle32(partB, 16) } };
        }
    }

    bool NorBlockHeader::IsFreeAndErased() const
    {
        return eraseCount.has_value() && allocationErased;
    }

    void SerializeNorHeaderA(uint32_t eraseCount, infra::ByteRange bytes)
    {
        really_assert(bytes.size() >= norHeaderPartABytes);

        std::ranges::fill(bytes, uint8_t{ 0xff });
        StoreLittle32(bytes, 0, magic);
        StoreLittle32(bytes, 4, eraseCount);
        Seal(bytes, partACrcOffset);
    }

    void SerializeNorHeaderB(const NorAllocation& allocation, infra::ByteRange bytes)
    {
        really_assert(bytes.size() >= norHeaderPartBBytes);

        std::ranges::fill(bytes, uint8_t{ 0xff });
        bytes[0] = static_cast<uint8_t>(allocation.type);
        std::ranges::fill(infra::Head(infra::DiscardHead(bytes, 1), 3), uint8_t{ 0 });
        StoreLittle64(bytes, 4, allocation.sequence);
        StoreLittle32(bytes, 12, allocation.owner.inode);
        StoreLittle32(bytes, 16, allocation.owner.index);
        Seal(bytes, partBCrcOffset);
    }

    NorBlockHeader ParseNorBlockHeader(infra::ConstByteRange header, uint32_t programUnit)
    {
        really_assert(header.size() >= NorHeaderSize(programUnit));

        auto partB = infra::Head(infra::DiscardHead(header, NorHeaderPartBOffset(programUnit)), RoundUpTo(norHeaderPartBBytes, programUnit));
        return NorBlockHeader{ ParsePartA(header), ParsePartB(partB), IsErased(partB) };
    }
}
