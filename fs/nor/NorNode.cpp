#include "fs/nor/NorNode.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorLayout.hpp"
#include "infra/util/Crc.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        uint32_t Crc(infra::ConstByteRange node)
        {
            infra::Crc32 crc;
            crc.Update(infra::Head(node, node.size() - norNodeCrcBytes));
            return crc.Result();
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

    void SealNorNode(const NorNodeHeader& header, infra::ByteRange node)
    {
        really_assert(node.size() > norNodeHeaderBytes + norNodeCrcBytes);

        node[0] = static_cast<uint8_t>(header.map);
        node[1] = header.level;
        StoreLittle16(node, 2, header.count);
        StoreLittle32(node, 4, header.owner);
        StoreLittle32(node, 8, header.lowestKey);
        StoreLittle32(node, node.size() - norNodeCrcBytes, Crc(node));
    }

    std::optional<NorNodeHeader> OpenNorNode(infra::ConstByteRange node)
    {
        really_assert(node.size() > norNodeHeaderBytes + norNodeCrcBytes);

        if (!IsKnownMap(node[0]) || node[1] >= norMaxLevels || LoadLittle32(node, node.size() - norNodeCrcBytes) != Crc(node))
            return std::nullopt;

        return NorNodeHeader{ static_cast<NorMap>(node[0]), node[1], LoadLittle16(node, 2), LoadLittle32(node, 4), LoadLittle32(node, 8) };
    }
}
