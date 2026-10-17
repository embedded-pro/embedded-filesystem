#include "fs/common/Crc32.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Crc.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

TEST(Crc32Test, computes_the_crc_32_check_value)
{
    constexpr std::array<uint8_t, 9> checkInput{ '1', '2', '3', '4', '5', '6', '7', '8', '9' };

    EXPECT_EQ(0xcbf43926, fs::Crc32Of(infra::MakeConstByteRange(checkInput)));
}

TEST(Crc32Test, equals_the_table_driven_crc_32_of_every_byte)
{
    std::array<uint8_t, 256> bytes{};
    for (std::size_t index = 0; index != bytes.size(); ++index)
        bytes[index] = static_cast<uint8_t>(index);
    infra::Crc32 table;
    table.Update(infra::MakeConstByteRange(bytes));

    EXPECT_EQ(table.Result(), fs::Crc32Of(infra::MakeConstByteRange(bytes)));
}
