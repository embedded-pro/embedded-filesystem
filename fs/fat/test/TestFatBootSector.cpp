#include "fs/fat/FatBootSector.hpp"
#include "infra/util/BoundedString.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace
{
    using Sector = std::array<uint8_t, 512>;

    Sector BootSector(std::initializer_list<uint8_t> head)
    {
        Sector sector{};
        std::size_t index = 0;

        for (auto byte : head)
            sector[index++] = byte;

        sector[510] = 0x55;
        sector[511] = 0xaa;
        return sector;
    }

    Sector Fat12BootSector()
    {
        return BootSector({ 0xeb, 0x3c, 0x90, 0x6d, 0x6b, 0x66, 0x73, 0x2e, 0x66, 0x61, 0x74, 0x00, 0x02, 0x04, 0x01, 0x00,
            0x02, 0x00, 0x02, 0x00, 0x10, 0xf8, 0x03, 0x00, 0x10, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x29, 0x0d, 0xf0, 0xad, 0x0b, 0x4e, 0x4f, 0x20, 0x4e, 0x41,
            0x4d, 0x45, 0x20, 0x20, 0x20, 0x20, 0x46, 0x41, 0x54, 0x31, 0x32, 0x20, 0x20, 0x20 });
    }

    Sector Fat16BootSector()
    {
        return BootSector({ 0xeb, 0x3c, 0x90, 0x6d, 0x6b, 0x66, 0x73, 0x2e, 0x66, 0x61, 0x74, 0x00, 0x02, 0x01, 0x01, 0x00,
            0x02, 0x00, 0x02, 0x00, 0x40, 0xf8, 0x40, 0x00, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x29, 0x78, 0x56, 0x34, 0x12, 0x4e, 0x4f, 0x20, 0x4e, 0x41,
            0x4d, 0x45, 0x20, 0x20, 0x20, 0x20, 0x46, 0x41, 0x54, 0x31, 0x36, 0x20, 0x20, 0x20 });
    }

    Sector Fat32BootSector()
    {
        return BootSector({ 0xeb, 0x58, 0x90, 0x6d, 0x6b, 0x66, 0x73, 0x2e, 0x66, 0x61, 0x74, 0x00, 0x02, 0x01, 0x20, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x20, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x40, 0x01, 0x00, 0x76, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x80, 0x00, 0x29, 0x21, 0x43, 0x65, 0x87, 0x4e, 0x4f, 0x20, 0x4e, 0x41, 0x4d, 0x45, 0x20, 0x20,
            0x20, 0x20, 0x46, 0x41, 0x54, 0x33, 0x32, 0x20, 0x20, 0x20 });
    }

    const fs::BiosParameterBlock fat12Parameters{ 512, 4, 1, 2, 512, 4096, 0xf8, 3, 0, false, 0, 0, 0, 0, 0, 0x0badf00d };
    const fs::BiosParameterBlock fat16Parameters{ 512, 1, 1, 2, 512, 16384, 0xf8, 64, 0, false, 0, 0, 0, 0, 0, 0x12345678 };
    const fs::BiosParameterBlock fat32Parameters{ 512, 1, 32, 2, 0, 81920, 0xf8, 630, 0, true, 0, 0, 2, 1, 6, 0x87654321 };

    std::optional<fs::BiosParameterBlock> Parse(const Sector& sector)
    {
        return fs::ParseBootSector(infra::MakeRange(sector));
    }

    Sector Serialize(const fs::BiosParameterBlock& parameters, fs::FatType type)
    {
        Sector sector;
        sector.fill(0xcc);
        fs::SerializeBootSector(parameters, type, infra::MakeRange(sector));
        return sector;
    }

    infra::BoundedConstString Text(const Sector& sector, std::size_t offset, std::size_t size)
    {
        return infra::BoundedConstString(reinterpret_cast<const char*>(sector.data() + offset), size);
    }
}

TEST(FatBootSectorTest, parses_fat12_bios_parameter_block)
{
    EXPECT_EQ(fat12Parameters, Parse(Fat12BootSector()));
}

TEST(FatBootSectorTest, parses_fat16_bios_parameter_block)
{
    EXPECT_EQ(fat16Parameters, Parse(Fat16BootSector()));
}

TEST(FatBootSectorTest, parses_fat32_extended_fields)
{
    EXPECT_EQ(fat32Parameters, Parse(Fat32BootSector()));
}

TEST(FatBootSectorTest, uses_32_bit_total_when_16_bit_total_is_zero)
{
    auto sector = Fat16BootSector();
    sector[19] = 0;
    sector[20] = 0;
    sector[32] = 0x00;
    sector[33] = 0x00;
    sector[34] = 0x02;

    EXPECT_EQ(0x20000u, Parse(sector)->totalSectors);
}

TEST(FatBootSectorTest, rejects_sector_without_signature)
{
    auto sector = Fat16BootSector();
    sector[511] = 0;

    EXPECT_EQ(std::nullopt, Parse(sector));
}

TEST(FatBootSectorTest, rejects_sector_without_jump)
{
    auto sector = Fat16BootSector();
    sector[0] = 0xfa;

    EXPECT_EQ(std::nullopt, Parse(sector));
}

TEST(FatBootSectorTest, accepts_every_jump_instruction)
{
    for (uint8_t jump : std::initializer_list<uint8_t>{ 0xeb, 0xe9, 0xe8 })
    {
        auto sector = Fat16BootSector();
        sector[0] = jump;

        EXPECT_EQ(fat16Parameters, Parse(sector));
    }
}

TEST(FatBootSectorTest, rejects_bytes_per_sector_that_is_not_a_sector_size)
{
    for (uint16_t bytes : std::initializer_list<uint16_t>{ 0, 256, 768, 8192 })
    {
        auto sector = Fat16BootSector();
        sector[11] = static_cast<uint8_t>(bytes);
        sector[12] = static_cast<uint8_t>(bytes >> 8);

        EXPECT_EQ(std::nullopt, Parse(sector)) << bytes;
    }
}

TEST(FatBootSectorTest, accepts_every_sector_size)
{
    for (uint16_t bytes : std::initializer_list<uint16_t>{ 512, 1024, 2048, 4096 })
    {
        auto sector = Fat16BootSector();
        sector[11] = static_cast<uint8_t>(bytes);
        sector[12] = static_cast<uint8_t>(bytes >> 8);

        EXPECT_EQ(bytes, Parse(sector)->bytesPerSector);
    }
}

TEST(FatBootSectorTest, rejects_sectors_per_cluster_that_is_not_a_power_of_two)
{
    for (uint8_t sectors : std::initializer_list<uint8_t>{ 0, 3, 6, 255 })
    {
        auto sector = Fat16BootSector();
        sector[13] = sectors;

        EXPECT_EQ(std::nullopt, Parse(sector)) << int{ sectors };
    }
}

TEST(FatBootSectorTest, rejects_zero_reserved_sectors)
{
    auto sector = Fat16BootSector();
    sector[14] = 0;

    EXPECT_EQ(std::nullopt, Parse(sector));
}

TEST(FatBootSectorTest, rejects_zero_fat_copies)
{
    auto sector = Fat16BootSector();
    sector[16] = 0;

    EXPECT_EQ(std::nullopt, Parse(sector));
}

TEST(FatBootSectorTest, serializes_fat16_name_label_and_signature)
{
    auto sector = Serialize(fat16Parameters, fs::FatType::fat16);

    EXPECT_EQ(0xeb, sector[0]);
    EXPECT_EQ(0x3c, sector[1]);
    EXPECT_EQ(0x90, sector[2]);
    EXPECT_EQ(0x29, sector[38]);
    EXPECT_EQ("NO NAME    ", Text(sector, 43, 11));
    EXPECT_EQ("FAT16   ", Text(sector, 54, 8));
    EXPECT_EQ(0x55, sector[510]);
    EXPECT_EQ(0xaa, sector[511]);
}

TEST(FatBootSectorTest, serializes_fat12_name)
{
    EXPECT_EQ("FAT12   ", Text(Serialize(fat12Parameters, fs::FatType::fat12), 54, 8));
}

TEST(FatBootSectorTest, serializes_fat32_name_label_and_jump)
{
    auto sector = Serialize(fat32Parameters, fs::FatType::fat32);

    EXPECT_EQ(0x58, sector[1]);
    EXPECT_EQ(0x29, sector[66]);
    EXPECT_EQ("NO NAME    ", Text(sector, 71, 11));
    EXPECT_EQ("FAT32   ", Text(sector, 82, 8));
}

TEST(FatBootSectorTest, serializing_clears_the_rest_of_the_sector)
{
    auto sector = Serialize(fat16Parameters, fs::FatType::fat16);

    EXPECT_EQ(0, sector[100]);
    EXPECT_EQ(0, sector[509]);
}

TEST(FatBootSectorTest, serializes_large_fat16_total_in_32_bits)
{
    auto parameters = fat16Parameters;
    parameters.totalSectors = 0x20000;

    auto sector = Serialize(parameters, fs::FatType::fat16);

    EXPECT_EQ(0, sector[19]);
    EXPECT_EQ(0, sector[20]);
    EXPECT_EQ(parameters, Parse(sector));
}

class FatBootSectorRoundTrip
    : public testing::TestWithParam<std::pair<fs::FatType, fs::BiosParameterBlock>>
{};

TEST_P(FatBootSectorRoundTrip, serializes_and_parses_back)
{
    auto [type, parameters] = GetParam();
    parameters.hiddenSectors = 8192;

    EXPECT_EQ(parameters, Parse(Serialize(parameters, type)));
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatBootSectorRoundTrip,
    testing::Values(std::make_pair(fs::FatType::fat12, fat12Parameters), std::make_pair(fs::FatType::fat16, fat16Parameters), std::make_pair(fs::FatType::fat32, fat32Parameters)));
