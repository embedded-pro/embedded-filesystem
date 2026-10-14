#include "fs/common/LittleEndian.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

TEST(LittleEndianTest, loads_the_lowest_byte_first)
{
    const std::array<uint8_t, 9> bytes{ 0xaa, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };

    EXPECT_EQ(0x0201u, fs::LoadLittle16(infra::MakeRange(bytes), 1));
    EXPECT_EQ(0x04030201u, fs::LoadLittle32(infra::MakeRange(bytes), 1));
    EXPECT_EQ(0x0807060504030201u, fs::LoadLittle64(infra::MakeRange(bytes), 1));
}

TEST(LittleEndianTest, stores_the_lowest_byte_first)
{
    std::array<uint8_t, 15> bytes{};

    fs::StoreLittle16(infra::MakeRange(bytes), 0, 0x0102);
    fs::StoreLittle32(infra::MakeRange(bytes), 2, 0x03040506);
    fs::StoreLittle64(infra::MakeRange(bytes), 6, 0x0708090a0b0c0d0e);

    EXPECT_EQ((std::array<uint8_t, 15>{ 0x02, 0x01, 0x06, 0x05, 0x04, 0x03, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x00 }), bytes);
}
