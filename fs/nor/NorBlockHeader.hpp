#ifndef FS_NOR_NOR_BLOCK_HEADER_HPP
#define FS_NOR_NOR_BLOCK_HEADER_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    enum class NorBlockType : uint8_t
    {
        checkpoint = 1,
        journal = 2,
        node = 3,
        data = 4
    };

    struct NorBlockOwner
    {
        uint32_t inode{ 0 };
        uint32_t index{ 0 };

        bool operator==(const NorBlockOwner& other) const = default;
    };

    struct NorAllocation
    {
        NorBlockType type;
        uint64_t sequence;
        NorBlockOwner owner{};

        bool operator==(const NorAllocation& other) const = default;
    };

    struct NorBlockHeader
    {
        std::optional<uint32_t> eraseCount;
        std::optional<NorAllocation> allocation;
        bool allocationErased{ false };

        bool IsFreeAndErased() const;
    };

    void SerializeNorHeaderA(uint32_t eraseCount, infra::ByteRange bytes);
    void SerializeNorHeaderB(const NorAllocation& allocation, infra::ByteRange bytes);
    NorBlockHeader ParseNorBlockHeader(infra::ConstByteRange header, uint32_t programUnit);
}

#endif
