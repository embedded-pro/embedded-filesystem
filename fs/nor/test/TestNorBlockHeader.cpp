#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "infra/util/Crc.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
    using SpiHeader = std::array<uint8_t, 36>;
    using InternalHeader = std::array<uint8_t, 48>;

    constexpr fs::NorAllocation dataBlock{ fs::NorBlockType::data, 0x0102030405060708, { 7, 9 } };

    uint32_t Crc(infra::ConstByteRange bytes)
    {
        infra::Crc32 crc;
        crc.Update(bytes);
        return crc.Result();
    }

    template<std::size_t Size>
    std::array<uint8_t, Size> Erased()
    {
        std::array<uint8_t, Size> header;
        header.fill(0xff);
        return header;
    }

    SpiHeader FreeAndErased(uint32_t eraseCount)
    {
        auto header = Erased<36>();
        fs::SerializeNorHeaderA(eraseCount, infra::Head(infra::MakeRange(header), 12));
        return header;
    }

    SpiHeader Allocated(const fs::NorAllocation& allocation)
    {
        auto header = FreeAndErased(5);
        fs::SerializeNorHeaderB(allocation, infra::DiscardHead(infra::MakeRange(header), 12));
        return header;
    }

    fs::NorBlockHeader Parse(const SpiHeader& header)
    {
        return fs::ParseNorBlockHeader(infra::MakeRange(header), 1);
    }
}

TEST(NorBlockHeaderTest, part_a_holds_the_magic_value_the_erase_count_and_a_crc)
{
    std::array<uint8_t, 12> bytes{};

    fs::SerializeNorHeaderA(0x01020304, infra::MakeRange(bytes));

    EXPECT_EQ((std::array<uint8_t, 8>{ 'E', 'F', 'S', 'B', 0x04, 0x03, 0x02, 0x01 }), infra::Head(infra::MakeRange(bytes), 8));
    EXPECT_EQ(Crc(infra::Head(infra::MakeRange(bytes), 8)), fs::LoadLittle32(infra::MakeRange(bytes), 8));
}

TEST(NorBlockHeaderTest, part_b_holds_the_type_the_sequence_the_owner_and_a_crc)
{
    std::array<uint8_t, 24> bytes{};

    fs::SerializeNorHeaderB(dataBlock, infra::MakeRange(bytes));

    EXPECT_EQ((std::array<uint8_t, 20>{ 0x04, 0x00, 0x00, 0x00, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x07, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00 }), infra::Head(infra::MakeRange(bytes), 20));
    EXPECT_EQ(Crc(infra::Head(infra::MakeRange(bytes), 20)), fs::LoadLittle32(infra::MakeRange(bytes), 20));
}

TEST(NorBlockHeaderTest, parts_are_padded_to_the_program_unit_with_erased_bytes)
{
    std::array<uint8_t, 16> partA{};
    std::array<uint8_t, 32> partB{};

    fs::SerializeNorHeaderA(1, infra::MakeRange(partA));
    fs::SerializeNorHeaderB(dataBlock, infra::MakeRange(partB));

    EXPECT_EQ((std::array<uint8_t, 4>{ 0xff, 0xff, 0xff, 0xff }), infra::DiscardHead(infra::MakeRange(partA), 12));
    EXPECT_EQ((std::array<uint8_t, 8>{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }), infra::DiscardHead(infra::MakeRange(partB), 24));
}

TEST(NorBlockHeaderTest, a_block_with_part_a_and_an_erased_part_b_is_free_and_erased)
{
    auto header = Parse(FreeAndErased(42));

    EXPECT_EQ(std::optional<uint32_t>(42), header.eraseCount);
    EXPECT_TRUE(header.allocationErased);
    EXPECT_EQ(std::nullopt, header.allocation);
    EXPECT_TRUE(header.IsFreeAndErased());
}

TEST(NorBlockHeaderTest, an_allocated_block_has_its_allocation)
{
    auto header = Parse(Allocated(dataBlock));

    EXPECT_EQ(std::optional<uint32_t>(5), header.eraseCount);
    EXPECT_EQ(dataBlock, header.allocation);
    EXPECT_FALSE(header.allocationErased);
    EXPECT_FALSE(header.IsFreeAndErased());
}

TEST(NorBlockHeaderTest, every_type_is_parsed)
{
    for (auto type : { fs::NorBlockType::checkpoint, fs::NorBlockType::journal, fs::NorBlockType::node, fs::NorBlockType::data })
    {
        fs::NorAllocation allocation{ type, 1 };

        EXPECT_EQ(allocation, Parse(Allocated(allocation)).allocation);
    }
}

TEST(NorBlockHeaderTest, an_erased_block_has_no_erase_count_and_is_not_free_and_erased)
{
    auto header = Parse(Erased<36>());

    EXPECT_EQ(std::nullopt, header.eraseCount);
    EXPECT_FALSE(header.IsFreeAndErased());
}

TEST(NorBlockHeaderTest, a_changed_byte_of_part_a_loses_the_erase_count)
{
    for (std::size_t offset = 0; offset != 12; ++offset)
    {
        auto bytes = FreeAndErased(42);
        bytes[offset] ^= 0x01;

        EXPECT_EQ(std::nullopt, Parse(bytes).eraseCount) << offset;
    }
}

TEST(NorBlockHeaderTest, a_changed_byte_of_part_b_is_neither_an_allocation_nor_erased)
{
    for (std::size_t offset = 12; offset != 36; ++offset)
    {
        auto bytes = Allocated(dataBlock);
        bytes[offset] ^= 0x01;
        auto header = Parse(bytes);

        EXPECT_EQ(std::nullopt, header.allocation) << offset;
        EXPECT_FALSE(header.allocationErased) << offset;
    }
}

TEST(NorBlockHeaderTest, a_programmed_byte_in_an_erased_part_b_is_not_free_and_erased)
{
    auto bytes = FreeAndErased(42);
    bytes[35] = 0xfe;

    EXPECT_FALSE(Parse(bytes).IsFreeAndErased());
}

TEST(NorBlockHeaderTest, an_unknown_type_is_not_an_allocation)
{
    for (uint8_t type : { 0, 5, 0xff })
    {
        auto bytes = Allocated(dataBlock);
        bytes[12] = type;
        fs::StoreLittle32(infra::MakeRange(bytes), 32, Crc(infra::Head(infra::DiscardHead(infra::MakeRange(bytes), 12), 20)));

        EXPECT_EQ(std::nullopt, Parse(bytes).allocation) << int(type);
    }
}

TEST(NorBlockHeaderTest, with_a_program_unit_of_16_bytes_part_b_starts_at_16)
{
    auto bytes = Erased<48>();
    fs::SerializeNorHeaderA(3, infra::Head(infra::MakeRange(bytes), 16));
    fs::SerializeNorHeaderB(dataBlock, infra::DiscardHead(infra::MakeRange(bytes), 16));

    auto header = fs::ParseNorBlockHeader(infra::MakeRange(bytes), 16);

    EXPECT_EQ(std::optional<uint32_t>(3), header.eraseCount);
    EXPECT_EQ(dataBlock, header.allocation);
}

TEST(NorBlockHeaderTest, with_a_program_unit_of_16_bytes_the_padding_of_part_b_must_be_erased_too)
{
    auto bytes = Erased<48>();
    fs::SerializeNorHeaderA(3, infra::Head(infra::MakeRange(bytes), 16));
    bytes[47] = 0x00;

    EXPECT_FALSE(fs::ParseNorBlockHeader(infra::MakeRange(bytes), 16).allocationErased);
}
