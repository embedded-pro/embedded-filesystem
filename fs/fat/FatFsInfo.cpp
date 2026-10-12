#include "fs/fat/FatFsInfo.hpp"
#include "fs/fat/FatBytes.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr std::size_t sectorBytes = 512;
        constexpr uint32_t leadSignature = 0x41615252;
        constexpr uint32_t structureSignature = 0x61417272;
        constexpr uint32_t trailSignature = 0xaa550000;
        constexpr uint32_t unknown = 0xffffffff;
        constexpr uint32_t firstCluster = 2;

        bool HasSignatures(infra::ConstByteRange sector)
        {
            return sector.size() >= sectorBytes && LoadLittle32(sector, 0) == leadSignature && LoadLittle32(sector, 484) == structureSignature && LoadLittle32(sector, 508) == trailSignature;
        }

        std::optional<uint32_t> Within(uint32_t value, uint32_t lowest, uint32_t highest)
        {
            if (value < lowest || value > highest)
                return std::nullopt;

            return value;
        }
    }

    std::optional<FsInfo> ParseFsInfo(infra::ConstByteRange sector, uint32_t clusterCount)
    {
        if (!HasSignatures(sector))
            return std::nullopt;

        return FsInfo{ Within(LoadLittle32(sector, 488), 0, clusterCount), Within(LoadLittle32(sector, 492), firstCluster, clusterCount + 1) };
    }

    void SerializeFsInfo(const FsInfo& info, infra::ByteRange sector)
    {
        really_assert(sector.size() >= sectorBytes);

        std::ranges::fill(sector, uint8_t{ 0 });
        StoreLittle32(sector, 0, leadSignature);
        StoreLittle32(sector, 484, structureSignature);
        StoreLittle32(sector, 488, info.freeClusters.value_or(unknown));
        StoreLittle32(sector, 492, info.nextFree.value_or(unknown));
        StoreLittle32(sector, 508, trailSignature);
    }
}
