#include "fs/fat/FatFsInfo.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace
{
    using Sector = std::array<uint8_t, 512>;

    constexpr uint32_t clusterCount = 80628;

    void Store(Sector& sector, std::size_t offset, uint32_t value)
    {
        for (std::size_t byte = 0; byte != 4; ++byte)
            sector[offset + byte] = static_cast<uint8_t>(value >> (8 * byte));
    }

    Sector FsInfoSector(uint32_t freeClusters, uint32_t nextFree)
    {
        Sector sector{};
        sector[0] = 0x52;
        sector[1] = 0x52;
        sector[2] = 0x61;
        sector[3] = 0x41;
        sector[484] = 0x72;
        sector[485] = 0x72;
        sector[486] = 0x41;
        sector[487] = 0x61;
        Store(sector, 488, freeClusters);
        Store(sector, 492, nextFree);
        sector[510] = 0x55;
        sector[511] = 0xaa;
        return sector;
    }

    std::optional<fs::FsInfo> Parse(const Sector& sector)
    {
        return fs::ParseFsInfo(infra::MakeRange(sector), clusterCount);
    }
}

TEST(FatFsInfoTest, parses_valid_hints)
{
    EXPECT_EQ((fs::FsInfo{ 80627, 2 }), Parse(FsInfoSector(80627, 2)));
}

TEST(FatFsInfoTest, unknown_values_are_absent)
{
    EXPECT_EQ((fs::FsInfo{ std::nullopt, std::nullopt }), Parse(FsInfoSector(0xffffffff, 0xffffffff)));
}

TEST(FatFsInfoTest, free_count_up_to_cluster_count_is_valid)
{
    EXPECT_EQ(clusterCount, Parse(FsInfoSector(clusterCount, 2))->freeClusters);
    EXPECT_EQ(0u, Parse(FsInfoSector(0, 2))->freeClusters);
}

TEST(FatFsInfoTest, free_count_above_cluster_count_is_absent)
{
    EXPECT_EQ(std::nullopt, Parse(FsInfoSector(clusterCount + 1, 2))->freeClusters);
}

TEST(FatFsInfoTest, next_free_outside_the_clusters_is_absent)
{
    EXPECT_EQ(std::nullopt, Parse(FsInfoSector(0, 0))->nextFree);
    EXPECT_EQ(std::nullopt, Parse(FsInfoSector(0, 1))->nextFree);
    EXPECT_EQ(std::nullopt, Parse(FsInfoSector(0, clusterCount + 2))->nextFree);
}

TEST(FatFsInfoTest, next_free_at_last_cluster_is_valid)
{
    EXPECT_EQ(clusterCount + 1, Parse(FsInfoSector(0, clusterCount + 1))->nextFree);
}

TEST(FatFsInfoTest, rejects_wrong_signatures)
{
    for (std::size_t offset : std::initializer_list<std::size_t>{ 0, 487, 510 })
    {
        auto sector = FsInfoSector(80627, 2);
        sector[offset] = 0;

        EXPECT_EQ(std::nullopt, Parse(sector)) << offset;
    }
}

TEST(FatFsInfoTest, serializes_and_parses_back)
{
    Sector sector;
    sector.fill(0xcc);

    fs::SerializeFsInfo(fs::FsInfo{ 1234, 77 }, infra::MakeRange(sector));

    EXPECT_EQ(FsInfoSector(1234, 77), sector);
}

TEST(FatFsInfoTest, serializes_unknown_values)
{
    Sector sector;

    fs::SerializeFsInfo(fs::FsInfo{}, infra::MakeRange(sector));

    EXPECT_EQ(FsInfoSector(0xffffffff, 0xffffffff), sector);
}
