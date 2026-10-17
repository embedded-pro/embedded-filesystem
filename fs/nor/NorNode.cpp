#include "fs/nor/NorNode.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/common/Crc32.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        uint32_t Crc(infra::ConstByteRange node)
        {
            return Crc32Of(infra::Head(node, node.size() - norNodeCrcBytes));
        }

        bool IsKnownMap(uint8_t map)
        {
            return map >= static_cast<uint8_t>(NorMap::inodeTable) && map <= static_cast<uint8_t>(NorMap::fileData);
        }

        template<class Range>
        Range Entries(Range node)
        {
            really_assert(node.size() > norNodeHeaderBytes + norNodeCrcBytes);

            return infra::Head(infra::DiscardHead(node, norNodeHeaderBytes), NorNodeEntryBytes(static_cast<uint32_t>(node.size())));
        }
    }

    infra::ByteRange NorNodeEntries(infra::ByteRange node)
    {
        return Entries(node);
    }

    infra::ConstByteRange NorNodeEntries(infra::ConstByteRange node)
    {
        return Entries(node);
    }

    NorNodeHeader ReadNorNodeHeader(infra::ConstByteRange node)
    {
        return NorNodeHeader{ static_cast<NorMap>(node[0]), node[1], LoadLittle16(node, 2), LoadLittle32(node, 4), LoadLittle32(node, 8) };
    }

    bool IsNorNodeHeader(const NorNodeHeader& header)
    {
        return IsKnownMap(static_cast<uint8_t>(header.map)) && header.level < norMaxLevels;
    }

    void WriteNorNodeHeader(const NorNodeHeader& header, infra::ByteRange node)
    {
        node[0] = static_cast<uint8_t>(header.map);
        node[1] = header.level;
        StoreLittle16(node, 2, header.count);
        StoreLittle32(node, 4, header.owner);
        StoreLittle32(node, 8, header.lowestKey);
    }

    void SealNorNode(const NorNodeHeader& header, infra::ByteRange node)
    {
        really_assert(node.size() > norNodeHeaderBytes + norNodeCrcBytes);

        WriteNorNodeHeader(header, node);
        StoreLittle32(node, node.size() - norNodeCrcBytes, Crc(node));
    }

    std::optional<NorNodeHeader> OpenNorNode(infra::ConstByteRange node)
    {
        really_assert(node.size() > norNodeHeaderBytes + norNodeCrcBytes);

        if (!IsKnownMap(node[0]) || node[1] >= norMaxLevels || LoadLittle32(node, node.size() - norNodeCrcBytes) != Crc(node))
            return std::nullopt;

        return ReadNorNodeHeader(node);
    }
}
