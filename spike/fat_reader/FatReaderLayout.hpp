#ifndef SPIKE_FAT_READER_FAT_READER_LAYOUT_HPP
#define SPIKE_FAT_READER_FAT_READER_LAYOUT_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>

namespace spike
{
    enum class FatKind : uint8_t
    {
        fat12,
        fat16,
        fat32
    };

    struct FatLayout
    {
        FatKind kind{ FatKind::fat12 };
        uint8_t clusterShift{ 0 };
        uint32_t fatStart{ 0 };
        uint32_t rootStart{ 0 };
        uint32_t rootSectors{ 0 };
        uint32_t rootCluster{ 0 };
        uint32_t dataStart{ 0 };
        uint32_t clusters{ 0 };
    };

    constexpr uint32_t fatSectorBytes = 512;

    fs::Result ParseBootSector(infra::ConstByteRange sector, uint32_t start, uint32_t deviceBlocks, FatLayout& layout);
    fs::Result FindPartition(infra::ConstByteRange sector, uint32_t& start);
    bool IsCluster(const FatLayout& layout, uint32_t cluster);
}

#endif
