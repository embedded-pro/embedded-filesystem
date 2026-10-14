#include "fs/nor/NorDirectoryKey.hpp"
#include "gtest/gtest.h"

TEST(NorDirectoryKeyTest, the_key_of_a_name_is_the_low_24_bits_of_its_crc_shifted_by_8)
{
    EXPECT_EQ(0xb7be4300u, fs::NorDirectoryKey("a"));
    EXPECT_EQ(0x572bb700u, fs::NorDirectoryKey("docs"));
}

TEST(NorDirectoryKeyTest, names_with_the_same_hash_share_their_key)
{
    EXPECT_EQ(0x6b2b9000u, fs::NorDirectoryKey("t15311"));
    EXPECT_EQ(0x6b2b9000u, fs::NorDirectoryKey("t498781"));
    EXPECT_EQ(0x6b2b9000u, fs::NorDirectoryKey("t636020"));
}

TEST(NorDirectoryKeyTest, the_last_key_of_a_hash_holds_the_highest_collision_index)
{
    EXPECT_EQ(0x6b2b90ffu, fs::NorLastDirectoryKey(fs::NorDirectoryKey("t15311")));
}
