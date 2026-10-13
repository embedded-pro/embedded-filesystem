#include "fs/fat/FatClosedFiles.hpp"
#include "gtest/gtest.h"
#include <array>

namespace
{
    fs::FatClosedFile Closed(uint32_t index, uint32_t firstCluster = 10, uint32_t size = 5000)
    {
        return fs::FatClosedFile{ fs::FatEntryPosition{ 0, index }, firstCluster, size, fs::FatExtent{ 3, firstCluster + 7, index + 1 } };
    }

    class FatClosedFilesTest
        : public testing::Test
    {
    public:
        std::array<fs::FatClosedFile, 2> storage{};
        fs::FatClosedFiles closed{ infra::MakeRange(storage) };
    };
}

TEST_F(FatClosedFilesTest, nothing_is_recalled_before_a_file_is_closed)
{
    EXPECT_EQ(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 1 }, 10, 5000));
}

TEST_F(FatClosedFilesTest, recalls_the_last_run_of_a_closed_file)
{
    closed.Remember(Closed(1));

    EXPECT_EQ((fs::FatExtent{ 3, 17, 2 }), closed.Recall(fs::FatEntryPosition{ 0, 1 }, 10, 5000));
}

TEST_F(FatClosedFilesTest, file_that_changed_since_it_was_closed_is_not_recalled)
{
    closed.Remember(Closed(1));

    EXPECT_EQ(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 1 }, 11, 5000));
    EXPECT_EQ(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 1 }, 10, 5001));
    EXPECT_EQ(std::nullopt, closed.Recall(fs::FatEntryPosition{ 1, 1 }, 10, 5000));
}

TEST_F(FatClosedFilesTest, closing_again_replaces_the_memo_of_a_file)
{
    closed.Remember(Closed(1));
    closed.Remember(Closed(2));

    closed.Remember(Closed(1, 10, 6000));

    EXPECT_EQ(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 1 }, 10, 5000));
    EXPECT_NE(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 1 }, 10, 6000));
    EXPECT_NE(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 2 }, 10, 5000));
}

TEST_F(FatClosedFilesTest, oldest_memo_is_forgotten_when_full)
{
    closed.Remember(Closed(1));
    closed.Remember(Closed(2));
    closed.Remember(Closed(1));

    closed.Remember(Closed(3));

    EXPECT_NE(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 1 }, 10, 5000));
    EXPECT_EQ(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 2 }, 10, 5000));
    EXPECT_NE(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 3 }, 10, 5000));
}

TEST_F(FatClosedFilesTest, clear_forgets_every_file)
{
    closed.Remember(Closed(1));

    closed.Clear();

    EXPECT_EQ(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 1 }, 10, 5000));
}

TEST(FatClosedFilesWithoutStorageTest, remembers_nothing)
{
    fs::FatClosedFiles closed{ infra::MemoryRange<fs::FatClosedFile>() };

    closed.Remember(Closed(1));

    EXPECT_EQ(std::nullopt, closed.Recall(fs::FatEntryPosition{ 0, 1 }, 10, 5000));
}
