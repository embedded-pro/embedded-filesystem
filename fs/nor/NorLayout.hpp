#ifndef FS_NOR_NOR_LAYOUT_HPP
#define FS_NOR_NOR_LAYOUT_HPP

#include <cstdint>

namespace fs
{
    constexpr uint32_t norSuperblockBytes = 28;
    constexpr uint32_t norHeaderPartABytes = 12;
    constexpr uint32_t norHeaderPartBBytes = 24;
    constexpr uint32_t norNodeHeaderBytes = 12;
    constexpr uint32_t norNodeCrcBytes = 4;
    constexpr uint32_t norInodeRecordFixedBytes = 20;
    constexpr uint32_t norMaxLevels = 3;
    constexpr uint32_t norUncommittedBlocks = 8;

    constexpr uint32_t RoundUpTo(uint32_t size, uint32_t unit)
    {
        return (size + unit - 1) / unit * unit;
    }

    constexpr bool IsPowerOfTwo(uint32_t value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    constexpr uint32_t NorHeaderPartBOffset(uint32_t programUnit)
    {
        return RoundUpTo(norHeaderPartABytes, programUnit);
    }

    constexpr uint32_t NorHeaderSize(uint32_t programUnit)
    {
        return NorHeaderPartBOffset(programUnit) + RoundUpTo(norHeaderPartBBytes, programUnit);
    }

    constexpr uint32_t NorNodeEntryBytes(uint32_t nodeSize)
    {
        return nodeSize - norNodeHeaderBytes - norNodeCrcBytes;
    }

    constexpr uint32_t NorRadixFanout(uint32_t nodeSize)
    {
        return NorNodeEntryBytes(nodeSize) / 4;
    }

    constexpr uint32_t NorKeyedEntries(uint32_t nodeSize, uint32_t valueBytes)
    {
        return NorNodeEntryBytes(nodeSize) / (4 + valueBytes);
    }

    constexpr uint32_t NorInodeRecordBytes(uint32_t inlineBytes, uint32_t maxNameBytes)
    {
        return norInodeRecordFixedBytes + inlineBytes + maxNameBytes;
    }
}

#endif
