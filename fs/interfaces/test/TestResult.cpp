#include "fs/interfaces/Result.hpp"
#include "gtest/gtest.h"

TEST(ResultTest, value_initialized_result_is_success)
{
    fs::Result result{};

    EXPECT_EQ(fs::Result::success, result);
}
