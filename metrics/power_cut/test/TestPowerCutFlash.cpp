#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "metrics/power_cut/PowerCutFlash.hpp"
#include "gmock/gmock.h"
#include <array>
#include <vector>

namespace
{
    class PowerCutFlashTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void Write(uint32_t address, std::vector<uint8_t> data)
        {
            cut.WriteBuffer(infra::MakeRange(data), address, [this]
                {
                    ++completions;
                });
            ExecuteAllActions();
        }

        void Erase(uint32_t beginIndex, uint32_t endIndex)
        {
            cut.EraseSectors(beginIndex, endIndex, [this]
                {
                    ++completions;
                });
            ExecuteAllActions();
        }

        hal::FlashStub flash{ 4, 8 };
        metrics::PowerCutFlash cut{ flash };
        int completions{ 0 };
    };
}

TEST_F(PowerCutFlashTest, forwards_requests_while_not_armed)
{
    std::array<uint8_t, 2> data{};

    Write(1, { 1, 2 });
    cut.ReadBuffer(infra::MakeRange(data), 1, [this]
        {
            ++completions;
        });
    ExecuteAllActions();

    EXPECT_EQ((std::array<uint8_t, 2>{ 1, 2 }), data);
    EXPECT_EQ(2, completions);
    EXPECT_EQ(4, cut.NumberOfSectors());
    EXPECT_EQ(8, cut.SizeOfSector(0));
    EXPECT_EQ(2, cut.SectorOfAddress(16));
    EXPECT_EQ(24, cut.AddressOfSector(3));
}

TEST_F(PowerCutFlashTest, counts_each_program_request_as_one_write_step)
{
    Write(0, { 1 });
    Write(1, { 2, 3, 4 });

    EXPECT_EQ(2, cut.WriteSteps());
}

TEST_F(PowerCutFlashTest, counts_each_erased_sector_as_one_write_step)
{
    Erase(0, 3);

    EXPECT_EQ(3, cut.WriteSteps());
}

TEST_F(PowerCutFlashTest, does_not_count_reads_as_write_steps)
{
    std::array<uint8_t, 2> data{};

    cut.ReadBuffer(infra::MakeRange(data), 0, [] {});
    ExecuteAllActions();

    EXPECT_EQ(0, cut.WriteSteps());
}

TEST_F(PowerCutFlashTest, completes_the_write_steps_before_the_cut)
{
    cut.Arm({ 2, metrics::NorCutMode::notStarted });

    Write(0, { 1 });
    Write(1, { 2 });

    EXPECT_EQ(2, completions);
    EXPECT_FALSE(cut.PoweredDown());
}

TEST_F(PowerCutFlashTest, interrupted_program_leaves_the_flash_unchanged_when_not_started)
{
    cut.Arm({ 0, metrics::NorCutMode::notStarted });

    Write(0, { 1, 2, 3, 4 });

    EXPECT_EQ((std::vector<uint8_t>(8, 0xff)), flash.sectors[0]);
    EXPECT_TRUE(cut.PoweredDown());
}

TEST_F(PowerCutFlashTest, interrupted_program_programs_only_the_first_half_when_partial)
{
    cut.Arm({ 0, metrics::NorCutMode::partial });

    Write(0, { 1, 2, 3, 4 });

    EXPECT_EQ((std::vector<uint8_t>{ 1, 2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }), flash.sectors[0]);
}

TEST_F(PowerCutFlashTest, interrupted_program_programs_the_given_number_of_bytes_when_partial)
{
    cut.Arm({ 0, metrics::NorCutMode::partial, 3 });

    Write(0, { 1, 2, 3, 4 });

    EXPECT_EQ((std::vector<uint8_t>{ 1, 2, 3, 0xff, 0xff, 0xff, 0xff, 0xff }), flash.sectors[0]);
}

TEST_F(PowerCutFlashTest, interrupted_program_is_applied_completely_when_completed)
{
    cut.Arm({ 0, metrics::NorCutMode::completed });

    Write(6, { 1, 2, 3, 4 });

    EXPECT_EQ((std::vector<uint8_t>{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1, 2 }), flash.sectors[0]);
    EXPECT_EQ((std::vector<uint8_t>{ 3, 4, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }), flash.sectors[1]);
}

TEST_F(PowerCutFlashTest, interrupted_program_keeps_the_and_semantics_of_nor_flash)
{
    Write(0, { 0x0f });
    cut.Arm({ 1, metrics::NorCutMode::completed });

    Write(0, { 0xf3 });

    EXPECT_EQ(0x03, flash.sectors[0][0]);
}

TEST_F(PowerCutFlashTest, interrupted_erase_leaves_the_sector_unchanged_when_not_started)
{
    flash.sectors[1] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    cut.Arm({ 0, metrics::NorCutMode::notStarted });

    Erase(1, 2);

    EXPECT_EQ((std::vector<uint8_t>{ 1, 2, 3, 4, 5, 6, 7, 8 }), flash.sectors[1]);
}

TEST_F(PowerCutFlashTest, interrupted_erase_leaves_the_sector_partially_erased_when_partial)
{
    flash.sectors[1] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    cut.Arm({ 0, metrics::NorCutMode::partial });

    Erase(1, 2);

    EXPECT_EQ((std::vector<uint8_t>{ 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0 }), flash.sectors[1]);
}

TEST_F(PowerCutFlashTest, interrupted_erase_erases_the_sector_when_completed)
{
    flash.sectors[1] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    cut.Arm({ 0, metrics::NorCutMode::completed });

    Erase(1, 2);

    EXPECT_EQ((std::vector<uint8_t>(8, 0xff)), flash.sectors[1]);
}

TEST_F(PowerCutFlashTest, erase_interrupted_at_its_second_sector_erases_the_first)
{
    flash.sectors[1] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    flash.sectors[2] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    cut.Arm({ 1, metrics::NorCutMode::notStarted });

    Erase(1, 3);

    EXPECT_EQ((std::vector<uint8_t>(8, 0xff)), flash.sectors[1]);
    EXPECT_EQ((std::vector<uint8_t>{ 1, 2, 3, 4, 5, 6, 7, 8 }), flash.sectors[2]);
}

TEST_F(PowerCutFlashTest, interrupted_request_is_never_completed)
{
    cut.Arm({ 0, metrics::NorCutMode::completed });

    Write(0, { 1 });
    Erase(1, 2);

    EXPECT_EQ(0, completions);
}

TEST_F(PowerCutFlashTest, requests_after_the_cut_have_no_effect_and_are_never_completed)
{
    std::array<uint8_t, 1> data{};
    cut.Arm({ 0, metrics::NorCutMode::notStarted });
    Write(0, { 1 });

    Write(8, { 2 });
    cut.ReadBuffer(infra::MakeRange(data), 0, [this]
        {
            ++completions;
        });
    ExecuteAllActions();

    EXPECT_EQ(0xff, flash.sectors[1][0]);
    EXPECT_EQ(0, completions);
}

TEST_F(PowerCutFlashTest, disarm_restores_normal_operation)
{
    cut.Arm({ 1, metrics::NorCutMode::notStarted });
    cut.Disarm();

    Write(0, { 1 });
    Write(1, { 2 });

    EXPECT_EQ(2, completions);
    EXPECT_FALSE(cut.PoweredDown());
}
