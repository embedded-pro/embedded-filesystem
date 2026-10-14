#ifndef FS_NOR_NOR_NODE_HPP
#define FS_NOR_NOR_NODE_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    enum class NorMap : uint8_t
    {
        inodeTable = 1,
        directory = 2,
        fileData = 3
    };

    struct NorNodeHeader
    {
        NorMap map{ NorMap::inodeTable };
        uint8_t level{ 0 };
        uint16_t count{ 0 };
        uint32_t owner{ 0 };
        uint32_t lowestKey{ 0 };

        bool operator==(const NorNodeHeader& other) const = default;
    };

    struct NorMapId
    {
        NorMap map{ NorMap::inodeTable };
        uint32_t owner{ 0 };
        uint32_t valueBytes{ 0 };
    };

    struct NorMapRoot
    {
        uint32_t address{ 0 };
        uint8_t height{ 0 };

        bool operator==(const NorMapRoot& other) const = default;
    };

    infra::ByteRange NorNodeEntries(infra::ByteRange node);
    infra::ConstByteRange NorNodeEntries(infra::ConstByteRange node);
    NorNodeHeader ReadNorNodeHeader(infra::ConstByteRange node);
    void WriteNorNodeHeader(const NorNodeHeader& header, infra::ByteRange node);
    void SealNorNode(const NorNodeHeader& header, infra::ByteRange node);
    std::optional<NorNodeHeader> OpenNorNode(infra::ConstByteRange node);
}

#endif
