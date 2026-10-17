#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/nor/NorBlockEraser.hpp"
#include "fs/nor/NorFileSystem.hpp"
#include "fs/nor/test/NorVolumeFixture.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t blockSize = 4096;

    class NorBlockEraserTest
        : public fs::NorVolumeFixture<config, 32, blockSize>
    {
    public:
        NorBlockEraserTest()
        {
            Mount();
        }

        void Step()
        {
            bool stepped = false;
            Volume().Eraser().Step([&stepped]
                {
                    stepped = true;
                });
            EXPECT_FALSE(stepped);
            ExecuteAllActions();
            EXPECT_TRUE(stepped);
        }

        void Refill()
        {
            bool refilled = false;
            Volume().Eraser().Refill([&refilled]
                {
                    refilled = true;
                });
            ExecuteAllActions();
            EXPECT_TRUE(refilled);
        }

        void KeepErased(uint32_t erased)
        {
            for (uint32_t block = 2; block != 32; ++block)
                if (State().blocks.State(block) != fs::NorBlockState::freeErased)
                    continue;
                else if (erased == 0)
                    State().blocks.Set(block, fs::NorBlockState::inUse);
                else
                    --erased;
        }

        uint32_t Erases() const
        {
            uint32_t erases = 0;
            for (const auto& request : device.requests)
                erases += request.erase ? 1 : 0;
            return erases;
        }

        void FinishForegroundCall()
        {
            fs::OperationQueue::Claimer claimer{ Volume().Queue() };
            claimer.Claim([] {});
            ExecuteAllActions();
            claimer.Release();
        }
    };
}

TEST_F(NorBlockEraserTest, counts_the_sectors_it_erases)
{
    State().blocks.Set(10, fs::NorBlockState::freeNotErased);

    Step();

    EXPECT_EQ(1, Volume().Counters().erasedSectors);
}

TEST_F(NorBlockEraserTest, has_no_work_while_every_free_block_is_erased)
{
    EXPECT_FALSE(Volume().Eraser().HasWork());

    State().blocks.Set(9, fs::NorBlockState::freeNotErased);

    EXPECT_TRUE(Volume().Eraser().HasWork());
}

TEST_F(NorBlockEraserTest, erases_the_least_worn_free_block_that_is_not_erased_and_programs_part_a_with_its_next_count)
{
    State().eraseCounts.Record(9, 3);
    State().eraseCounts.Record(12, 2);
    State().blocks.Set(9, fs::NorBlockState::freeNotErased);
    State().blocks.Set(12, fs::NorBlockState::freeNotErased);
    image.Program(12 * blockSize + 100, infra::MakeRange(std::array<uint8_t, 2>{ 1, 2 }));

    Step();

    EXPECT_EQ(fs::NorBlockState::freeErased, State().blocks.State(12));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(9));
    EXPECT_EQ(3, State().eraseCounts.Count(12));
    EXPECT_EQ(3, HeaderOf(12).eraseCount);
    std::array<uint8_t, 2> payload{};
    image.Read(12 * blockSize + 100, infra::MakeRange(payload));
    EXPECT_EQ((std::array<uint8_t, 2>{ 0xff, 0xff }), payload);
}

TEST_F(NorBlockEraserTest, a_refill_erases_one_sector_when_the_pool_is_below_its_watermark)
{
    KeepErased(Volume().Geometry().WatermarkBlocks() - 1);
    State().blocks.Set(30, fs::NorBlockState::freeNotErased);
    device.recording = true;

    Refill();

    EXPECT_EQ(1, Erases());
    EXPECT_EQ(fs::NorBlockState::freeErased, State().blocks.State(30));
}

TEST_F(NorBlockEraserTest, a_refill_erases_nothing_when_the_pool_holds_its_watermark)
{
    KeepErased(Volume().Geometry().WatermarkBlocks());
    State().blocks.Set(30, fs::NorBlockState::freeNotErased);
    device.recording = true;

    Refill();

    EXPECT_EQ(0, Erases());
}

TEST_F(NorBlockEraserTest, a_refill_erases_nothing_without_a_free_block_that_is_not_erased)
{
    KeepErased(0);
    device.recording = true;

    Refill();

    EXPECT_EQ(0, Erases());
}

TEST_F(NorBlockEraserTest, refills_at_most_once_per_foreground_call)
{
    KeepErased(0);
    for (auto block : { 29, 30, 31 })
        State().blocks.Set(block, fs::NorBlockState::freeNotErased);
    device.recording = true;

    Refill();
    Refill();
    EXPECT_EQ(1, Erases());

    FinishForegroundCall();
    Refill();
    EXPECT_EQ(2, Erases());
}

TEST_F(NorBlockEraserTest, an_erase_that_counts_as_the_refill_of_the_call_leaves_no_refill)
{
    KeepErased(0);
    State().blocks.Set(30, fs::NorBlockState::freeNotErased);
    device.recording = true;

    Volume().Eraser().CountAsRefill();
    Refill();

    EXPECT_EQ(0, Erases());
}

namespace
{
    constexpr fs::NorConfig twoSectors{ .maxBlocks = 22, .sectorsPerBlock = 2, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };

    class NorBlockEraserOnMockTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorBlockEraserOnMockTest()
        {
            auto& state = volume.State();
            state.geometry = fs::MakeNorGeometry(twoSectors, flash);
            state.blocks.Reset(22);
            state.eraseCounts.Reset(22);
            for (uint32_t block = 0; block != 22; ++block)
            {
                state.eraseCounts.Record(block, 5);
                state.blocks.Set(block, fs::NorBlockState::inUse);
            }
            state.eraseCounts.Record(6, 4);
            state.blocks.Set(6, fs::NorBlockState::freeNotErased);
            state.blocks.Set(9, fs::NorBlockState::freeNotErased);
        }

        void Step()
        {
            volume.Eraser().Step([this]
                {
                    stepped.callback();
                });
            EXPECT_CALL(stepped, callback());
            while (flash.done)
                flash.done();
            testing::Mock::VerifyAndClearExpectations(&stepped);
        }

        std::vector<uint8_t> PartA(uint32_t count) const
        {
            std::array<uint8_t, 16> partA{};
            fs::SerializeNorHeaderA(count, infra::MakeRange(partA));
            return { partA.begin(), partA.end() };
        }

        testing::StrictMock<hal::FlashMock> flash{ 44, 2048 };
        fs::DeferredCompletions::WithMaxSize<2> deferred;
        std::array<uint8_t, fs::NorBlockStateBytes(22)> states{};
        std::array<uint8_t, 22> counts{};
        std::array<uint8_t, fs::NorNodeBlockBytes(22)> nodeBlocks{};
        std::array<uint8_t, 256> journal{};
        std::array<uint8_t, 15 * 512> nodes{};
        std::array<fs::NorNodeCache::Slot, 15> slots{};
        static constexpr fs::NorOperations operations = fs::NorOperationsFor<fs::NorFeatures{}>();
        fs::NorVolume volume{ fs::NorVolumeParts{ flash, deferred, infra::MakeRange(states), infra::MakeRange(counts), infra::MakeRange(nodeBlocks), infra::MakeRange(journal), infra::MakeRange(nodes), infra::MakeRange(slots), operations }, twoSectors };
        testing::StrictMock<infra::MockCallback<void()>> stepped;
    };
}

TEST_F(NorBlockEraserOnMockTest, erases_one_sector_per_step_and_programs_part_a_after_the_last_one)
{
    EXPECT_CALL(flash, eraseSectorsMock(12, 13));
    Step();
    EXPECT_EQ(fs::NorBlockState::freeNotErased, volume.State().blocks.State(6));

    testing::InSequence sequence;
    EXPECT_CALL(flash, eraseSectorsMock(13, 14));
    EXPECT_CALL(flash, writeBufferMock(PartA(5), 6 * 4096));
    Step();
    EXPECT_EQ(fs::NorBlockState::freeErased, volume.State().blocks.State(6));
}

TEST_F(NorBlockEraserOnMockTest, finishes_the_block_it_started_before_a_less_worn_one)
{
    EXPECT_CALL(flash, eraseSectorsMock(12, 13));
    Step();
    volume.State().eraseCounts.Record(9, 1);

    testing::InSequence sequence;
    EXPECT_CALL(flash, eraseSectorsMock(13, 14));
    EXPECT_CALL(flash, writeBufferMock(PartA(5), 6 * 4096));
    Step();
}

TEST_F(NorBlockEraserOnMockTest, after_a_reset_starts_again_with_the_least_worn_block)
{
    EXPECT_CALL(flash, eraseSectorsMock(12, 13));
    Step();
    volume.State().eraseCounts.Record(9, 1);

    volume.Eraser().Reset();

    EXPECT_CALL(flash, eraseSectorsMock(18, 19));
    Step();
}

TEST_F(NorBlockEraserTest, erasing_the_last_block_at_the_lowest_count_moves_the_base_up)
{
    for (uint32_t block = 2; block != 32; ++block)
        State().eraseCounts.Record(block, 3);
    State().eraseCounts.Record(12, 2);
    State().eraseCounts.Normalize(2);
    State().blocks.Set(12, fs::NorBlockState::freeNotErased);

    Step();

    EXPECT_EQ(3, State().eraseCounts.Base());
    EXPECT_EQ(3, State().eraseCounts.Count(12));
    EXPECT_EQ(3, State().eraseCounts.Count(20));
}

TEST_F(NorBlockEraserTest, a_step_for_the_most_worn_erases_the_most_worn_free_block_that_is_not_erased)
{
    State().eraseCounts.Record(9, 3);
    State().eraseCounts.Record(12, 2);
    State().blocks.Set(9, fs::NorBlockState::freeNotErased);
    State().blocks.Set(12, fs::NorBlockState::freeNotErased);

    bool stepped = false;
    Volume().Eraser().Step(fs::NorWear::most, [&stepped]
        {
            stepped = true;
        });
    EXPECT_FALSE(stepped);
    ExecuteAllActions();

    EXPECT_TRUE(stepped);
    EXPECT_EQ(fs::NorBlockState::freeErased, State().blocks.State(9));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(12));
}
