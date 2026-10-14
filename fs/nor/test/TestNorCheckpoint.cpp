#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "infra/util/Crc.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    constexpr fs::NorCheckpoint checkpoint{ 0x2100, 1, 2, 3, 0x48, 1, 20 };
    using Bytes = std::array<uint8_t, 40>;

    Bytes Sealed()
    {
        Bytes bytes;
        bytes.fill(0xff);
        fs::SerializeNorCheckpoint(checkpoint, infra::MakeRange(bytes));
        fs::MarkNorBlockInUse(infra::MakeRange(bytes), 0);
        fs::MarkNorBlockInUse(infra::MakeRange(bytes), 3);
        fs::MarkNorBlockInUse(infra::MakeRange(bytes), 19);
        fs::SealNorCheckpoint(infra::MakeRange(bytes));
        return bytes;
    }
}

TEST(NorCheckpointTest, a_checkpoint_holds_its_fields_a_bit_per_block_and_a_crc)
{
    static_assert(fs::NorCheckpointBytes(20) == 35);
    static_assert(fs::NorCheckpointBytes(4096) == 544);
}

TEST(NorCheckpointTest, serializes_the_fields_and_the_bits_of_the_blocks_in_use)
{
    auto bytes = Sealed();

    EXPECT_EQ((std::array<uint8_t, 31>{ 0x00, 0x21, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 0x48, 0, 0, 0, 1, 0, 0, 0, 20, 0, 0, 0, 0x09, 0x00, 0x08 }), infra::Head(infra::MakeRange(bytes), 31));
    infra::Crc32 crc;
    crc.Update(infra::Head(infra::MakeRange(bytes), 31));
    EXPECT_EQ(crc.Result(), fs::LoadLittle32(infra::MakeRange(bytes), 31));
}

TEST(NorCheckpointTest, serializes_the_fixed_part_alone)
{
    auto sealed = Sealed();
    std::array<uint8_t, 28> fixedPart;
    fixedPart.fill(0xff);

    fs::SerializeNorCheckpointFixedPart(checkpoint, infra::MakeRange(fixedPart));

    EXPECT_TRUE(infra::ContentsEqual(infra::Head(infra::MakeRange(sealed), 28), infra::MakeRange(fixedPart)));
}

TEST(NorCheckpointTest, opens_a_sealed_checkpoint)
{
    auto bytes = Sealed();

    EXPECT_EQ(checkpoint, fs::OpenNorCheckpoint(infra::MakeRange(bytes)));
}

TEST(NorCheckpointTest, parses_the_fields_before_the_bitmap)
{
    auto bytes = Sealed();

    EXPECT_EQ(checkpoint, fs::ParseNorCheckpointFixedPart(infra::Head(infra::MakeRange(bytes), 28)));
}

TEST(NorCheckpointTest, tells_which_blocks_are_in_use)
{
    auto bytes = Sealed();

    EXPECT_TRUE(fs::IsNorBlockInUse(infra::MakeRange(bytes), 0));
    EXPECT_FALSE(fs::IsNorBlockInUse(infra::MakeRange(bytes), 1));
    EXPECT_TRUE(fs::IsNorBlockInUse(infra::MakeRange(bytes), 3));
    EXPECT_FALSE(fs::IsNorBlockInUse(infra::MakeRange(bytes), 18));
    EXPECT_TRUE(fs::IsNorBlockInUse(infra::MakeRange(bytes), 19));
}

TEST(NorCheckpointTest, a_changed_byte_does_not_open)
{
    for (std::size_t offset = 0; offset != 35; ++offset)
    {
        auto bytes = Sealed();
        bytes[offset] ^= 0x01;

        EXPECT_EQ(std::nullopt, fs::OpenNorCheckpoint(infra::MakeRange(bytes))) << offset;
    }
}

TEST(NorCheckpointTest, a_checkpoint_cut_short_does_not_open)
{
    auto bytes = Sealed();

    EXPECT_EQ(std::nullopt, fs::OpenNorCheckpoint(infra::Head(infra::MakeRange(bytes), 34)));
}

TEST(NorCheckpointTest, erased_flash_does_not_open)
{
    Bytes bytes;
    bytes.fill(0xff);

    EXPECT_EQ(std::nullopt, fs::OpenNorCheckpoint(infra::MakeRange(bytes)));
}
