#include "fs/common/DeferredCompletions.hpp"
#include "fs/nor/NorBlockAllocator.hpp"
#include "fs/nor/test/NorVolumeFixture.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t blockSize = 4096;

    class NorBlockAllocatorTest
        : public fs::NorVolumeFixture<config, 32, blockSize>
    {
    public:
        std::optional<uint32_t> Allocate(fs::NorBlockType type = fs::NorBlockType::journal, fs::NorBlockOwner owner = {})
        {
            std::optional<std::optional<uint32_t>> allocated;
            Volume().Allocator().Allocate(type, owner, [&allocated](std::optional<uint32_t> block)
                {
                    allocated = block;
                });
            ExecuteAllActions();
            EXPECT_TRUE(allocated.has_value());
            return allocated.value_or(std::nullopt);
        }

        void MarkEveryFreeBlockNotErased()
        {
            for (uint32_t block = 2; block != 32; ++block)
                if (State().blocks.State(block) == fs::NorBlockState::freeErased)
                    State().blocks.Set(block, fs::NorBlockState::freeNotErased);
        }
    };
}

TEST_F(NorBlockAllocatorTest, takes_the_free_and_erased_block_with_the_lowest_erase_count)
{
    Builder().SetEraseCount(9, 0);
    Mount();

    EXPECT_EQ(9, Allocate());
}

TEST_F(NorBlockAllocatorTest, takes_the_lowest_block_among_equally_worn_ones)
{
    Mount();

    EXPECT_EQ(4, Allocate());
    EXPECT_EQ(5, Allocate());
}

TEST_F(NorBlockAllocatorTest, programs_part_b_with_the_type_the_next_sequence_and_the_owner)
{
    Mount();
    auto sequence = State().nextSequence;

    auto block = Allocate(fs::NorBlockType::data, fs::NorBlockOwner{ 7, 3 });

    ASSERT_EQ(4, block);
    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::data, sequence, { 7, 3 } }), HeaderOf(4).allocation);
    EXPECT_EQ(1, HeaderOf(4).eraseCount);
    EXPECT_EQ(sequence + 1, State().nextSequence);
    EXPECT_EQ(fs::NorBlockState::pending, State().blocks.State(4));
}

TEST_F(NorBlockAllocatorTest, erases_the_least_worn_free_block_when_no_free_block_is_erased)
{
    Builder().SetEraseCount(12, 0);
    Builder().Program(12 * blockSize + 100, infra::MakeRange(std::array<uint8_t, 2>{ 1, 2 }));
    Mount();
    MarkEveryFreeBlockNotErased();

    ASSERT_EQ(12, Allocate());

    EXPECT_EQ(1, HeaderOf(12).eraseCount);
    EXPECT_EQ(1, State().eraseCounts.Count(12));
    std::array<uint8_t, 2> payload{};
    image.Read(12 * blockSize + 100, infra::MakeRange(payload));
    EXPECT_EQ((std::array<uint8_t, 2>{ 0xff, 0xff }), payload);
    EXPECT_EQ(fs::NorBlockState::pending, State().blocks.State(12));
}

TEST_F(NorBlockAllocatorTest, prefers_an_erased_block_to_a_less_worn_one_that_is_not_erased)
{
    Builder().SetEraseCount(12, 0);
    Mount();
    State().blocks.Set(12, fs::NorBlockState::freeNotErased);

    EXPECT_EQ(4, Allocate());
}

TEST_F(NorBlockAllocatorTest, completes_without_a_block_when_no_block_is_free)
{
    Mount();
    for (uint32_t block = 2; block != 32; ++block)
        State().blocks.Set(block, fs::NorBlockState::inUse);

    EXPECT_EQ(std::nullopt, Allocate());
}

TEST_F(NorBlockAllocatorTest, does_not_take_a_pending_block)
{
    Mount();
    for (uint32_t block = 2; block != 32; ++block)
        State().blocks.Set(block, fs::NorBlockState::pending);

    EXPECT_EQ(std::nullopt, Allocate());
}

namespace
{
    constexpr fs::NorConfig twoSectorWideUnit{ .maxBlocks = 20, .sectorsPerBlock = 2, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };

    class NorBlockAllocatorOnMockTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorBlockAllocatorOnMockTest()
        {
            auto& state = volume.State();
            state.geometry = fs::MakeNorGeometry(twoSectorWideUnit, flash);
            state.blocks.Reset(20);
            state.eraseCounts.Reset(20);
            for (uint32_t block = 2; block != 20; ++block)
            {
                state.eraseCounts.Record(block, block == 6 ? 4 : 5);
                state.blocks.Set(block, fs::NorBlockState::inUse);
            }
            state.blocks.Set(6, fs::NorBlockState::freeNotErased);
            state.nextSequence = 9;
        }

        void CompleteRequests()
        {
            while (flash.done)
                flash.done();
        }

        testing::StrictMock<hal::FlashMock> flash{ 40, 2048 };
        fs::DeferredCompletions::WithMaxSize<2> deferred;
        std::array<uint8_t, fs::NorBlockStateBytes(20)> states{};
        std::array<uint8_t, 20> counts{};
        std::array<uint8_t, 256> journal{};
        std::array<uint8_t, 15 * 512> nodes{};
        std::array<fs::NorNodeCache::Slot, 15> slots{};
        fs::NorVolume volume{ fs::NorVolumeParts{ flash, deferred, infra::MakeRange(states), infra::MakeRange(counts), infra::MakeRange(journal), infra::MakeRange(nodes), infra::MakeRange(slots) }, twoSectorWideUnit, std::chrono::milliseconds(10) };
        testing::StrictMock<infra::MockCallback<void(std::optional<uint32_t>)>> done;
    };
}

TEST_F(NorBlockAllocatorOnMockTest, erases_one_sector_per_request_then_programs_part_a_and_part_b)
{
    std::array<uint8_t, 16> partA{};
    fs::SerializeNorHeaderA(5, infra::MakeRange(partA));
    std::array<uint8_t, 32> partB{};
    fs::SerializeNorHeaderB(fs::NorAllocation{ fs::NorBlockType::node, 9, {} }, infra::MakeRange(partB));

    testing::InSequence sequence;
    EXPECT_CALL(flash, eraseSectorsMock(12, 13));
    EXPECT_CALL(flash, eraseSectorsMock(13, 14));
    EXPECT_CALL(flash, writeBufferMock(std::vector<uint8_t>(partA.begin(), partA.end()), 6 * 4096));
    EXPECT_CALL(flash, writeBufferMock(std::vector<uint8_t>(partB.begin(), partB.end()), 6 * 4096 + 16));
    EXPECT_CALL(done, callback(std::optional<uint32_t>(6)));

    volume.Allocator().Allocate(fs::NorBlockType::node, {}, [this](std::optional<uint32_t> block)
        {
            done.callback(block);
        });
    CompleteRequests();
}
