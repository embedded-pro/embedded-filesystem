#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorSuperblock.hpp"
#include "infra/util/Crc.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    constexpr fs::NorSuperblock spiNor{ 1, 4096, 4096, 1, 512, 64, 64 };

    constexpr std::array<uint8_t, 28> spiNorBytes{
        'E', 'F', 'S', 'N', 0x01, 0x00, 0x01, 0x00,
        0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x02, 0x40, 0x00, 0x40, 0x00,
        0xc5, 0xb7, 0x77, 0x45
    };

    void Reseal(std::array<uint8_t, 28>& bytes)
    {
        infra::Crc32 crc;
        crc.Update(infra::Head(infra::MakeRange(bytes), 24));
        fs::StoreLittle32(infra::MakeRange(bytes), 24, crc.Result());
    }
}

TEST(NorSuperblockTest, serializes_every_field_and_the_crc)
{
    std::array<uint8_t, 28> bytes{};

    fs::SerializeNorSuperblock(spiNor, infra::MakeRange(bytes));

    EXPECT_EQ(spiNorBytes, bytes);
}

TEST(NorSuperblockTest, pads_to_the_program_unit_with_erased_bytes)
{
    std::array<uint8_t, 32> bytes{};

    fs::SerializeNorSuperblock(spiNor, infra::MakeRange(bytes));

    EXPECT_EQ((std::array<uint8_t, 4>{ 0xff, 0xff, 0xff, 0xff }), infra::DiscardHead(infra::MakeRange(bytes), 28));
}

TEST(NorSuperblockTest, parses_what_it_serializes)
{
    std::array<uint8_t, 28> bytes{};
    fs::NorSuperblock internalFlash{ 1, 2048, 256, 16, 512, 64, 32 };
    fs::SerializeNorSuperblock(internalFlash, infra::MakeRange(bytes));

    EXPECT_EQ(internalFlash, fs::ParseNorSuperblock(infra::MakeRange(bytes)));
}

TEST(NorSuperblockTest, parses_a_valid_superblock)
{
    EXPECT_EQ(spiNor, fs::ParseNorSuperblock(infra::MakeRange(spiNorBytes)));
}

TEST(NorSuperblockTest, a_changed_byte_is_not_a_superblock)
{
    for (std::size_t offset = 0; offset != spiNorBytes.size(); ++offset)
    {
        auto bytes = spiNorBytes;
        bytes[offset] ^= 0x01;

        EXPECT_EQ(std::nullopt, fs::ParseNorSuperblock(infra::MakeRange(bytes))) << offset;
    }
}

TEST(NorSuperblockTest, another_format_version_is_not_a_superblock)
{
    auto bytes = spiNorBytes;
    bytes[4] = 0x02;
    Reseal(bytes);

    EXPECT_EQ(std::nullopt, fs::ParseNorSuperblock(infra::MakeRange(bytes)));
}

TEST(NorSuperblockTest, resealing_the_same_bytes_keeps_a_superblock)
{
    auto bytes = spiNorBytes;
    Reseal(bytes);

    EXPECT_EQ(spiNor, fs::ParseNorSuperblock(infra::MakeRange(bytes)));
}

TEST(NorSuperblockTest, erased_flash_is_not_a_superblock)
{
    std::array<uint8_t, 28> bytes;
    bytes.fill(0xff);

    EXPECT_EQ(std::nullopt, fs::ParseNorSuperblock(infra::MakeRange(bytes)));
}
