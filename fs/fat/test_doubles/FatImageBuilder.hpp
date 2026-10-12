#ifndef FS_FAT_TEST_DOUBLES_FAT_IMAGE_BUILDER_HPP
#define FS_FAT_TEST_DOUBLES_FAT_IMAGE_BUILDER_HPP

#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/fat/test_doubles/BlockImage.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include <array>
#include <cstdint>

namespace fs
{
    class FatImageBuilder
    {
    public:
        using Sector = std::array<uint8_t, 512>;

        explicit FatImageBuilder(BlockImage& device);

        const FatGeometry& Geometry() const;
        uint32_t RootCluster() const;

        uint32_t AddFile(uint32_t parent, infra::BoundedConstString longName, const char (&shortName)[12], infra::ConstByteRange contents, bool fragmented = false);
        uint32_t AddDirectory(uint32_t parent, infra::BoundedConstString longName, const char (&shortName)[12]);
        FatEntryPosition AddEntries(uint32_t parent, infra::BoundedConstString longName, const ShortEntry& entry);

        uint32_t AllocateChain(uint32_t clusters, bool fragmented = false);
        uint32_t FatEntry(uint32_t cluster) const;
        void SetFatEntry(uint32_t cluster, uint32_t value);
        void WriteRawEntry(FatEntryPosition position, infra::ConstByteRange entry);
        Sector ReadSector(uint32_t sector) const;
        uint32_t SectorOf(FatEntryPosition position) const;
        uint32_t ClusterBytes() const;

    private:
        void WriteSector(uint32_t sector, const Sector& contents);
        void WriteData(uint32_t firstCluster, infra::ConstByteRange contents);
        void ZeroCluster(uint32_t cluster);
        uint32_t FreeCluster(uint32_t from) const;
        FatEntryPosition EndOfDirectory(uint32_t directory, uint32_t slots);
        uint32_t EntriesPerCluster(uint32_t directory) const;
        uint32_t LastCluster(uint32_t first) const;

    private:
        BlockImage& device;
        FatGeometry geometry;
    };
}

#endif
