#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "validation/firmware/devices/RamBlockDevice.hpp"
#include "validation/firmware/devices/RamFlash.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>
#include <cstdint>

class RamFlashTest
    : public testing::Test
    , public infra::EventDispatcherFixture
{
public:
    void Write(const std::array<uint8_t, 4>& data, uint32_t address)
    {
        flash.WriteBuffer(infra::MakeRange(data), address, [this]
            {
                done.callback();
            });
        EXPECT_CALL(done, callback());
        ExecuteAllActions();
    }

    std::array<uint8_t, 4> Read(uint32_t address)
    {
        std::array<uint8_t, 4> buffer{};
        flash.ReadBuffer(infra::MakeRange(buffer), address, [this]
            {
                done.callback();
            });
        EXPECT_CALL(done, callback());
        ExecuteAllActions();
        return buffer;
    }

    std::array<uint8_t, 64> storage{};
    validation::RamFlash flash{ infra::MakeRange(storage), 16 };
    testing::StrictMock<infra::MockCallback<void()>> done;
};

TEST_F(RamFlashTest, has_sectors_of_the_given_size_over_its_storage)
{
    EXPECT_EQ(4, flash.NumberOfSectors());
    EXPECT_EQ(16, flash.SizeOfSector(0));
}

TEST_F(RamFlashTest, starts_erased)
{
    EXPECT_TRUE(std::all_of(storage.begin(), storage.end(), [](uint8_t byte)
        {
            return byte == 0xff;
        }));
}

TEST_F(RamFlashTest, a_write_clears_bits_and_never_sets_them)
{
    Write({ { 0x0f, 0xf0, 0x00, 0xff } }, 14);
    Write({ { 0xf3, 0x3f, 0xff, 0x00 } }, 14);

    EXPECT_EQ((std::array<uint8_t, 4>{ { 0x03, 0x30, 0x00, 0x00 } }), Read(14));
}

TEST_F(RamFlashTest, an_erase_sets_the_bytes_of_its_sectors_only)
{
    Write({ { 0, 0, 0, 0 } }, 14);

    flash.EraseSectors(1, 2, [this]
        {
            done.callback();
        });
    EXPECT_CALL(done, callback());
    ExecuteAllActions();

    EXPECT_EQ((std::array<uint8_t, 4>{ { 0, 0, 0xff, 0xff } }), Read(14));
}

TEST_F(RamFlashTest, erases_up_to_its_last_sector)
{
    Write({ { 0, 0, 0, 0 } }, 60);

    flash.EraseSectors(3, 4, [this]
        {
            done.callback();
        });
    EXPECT_CALL(done, callback());
    ExecuteAllActions();

    EXPECT_EQ((std::array<uint8_t, 4>{ { 0xff, 0xff, 0xff, 0xff } }), Read(60));
}

TEST_F(RamFlashTest, completes_within_the_call_when_its_completion_is_immediate)
{
    validation::RamFlash immediate{ infra::MakeRange(storage), 16, validation::RamCompletion::immediate };
    std::array<uint8_t, 4> buffer{};

    EXPECT_CALL(done, callback()).Times(3);
    immediate.WriteBuffer(infra::MakeRange(buffer), 0, [this]
        {
            done.callback();
        });
    immediate.ReadBuffer(infra::MakeRange(buffer), 0, [this]
        {
            done.callback();
        });
    immediate.EraseSectors(0, 1, [this]
        {
            done.callback();
        });
}

TEST_F(RamFlashTest, completes_only_from_the_event_dispatcher)
{
    std::array<uint8_t, 4> buffer{};
    flash.WriteBuffer(infra::MakeRange(buffer), 0, [this]
        {
            done.callback();
        });
    flash.ReadBuffer(infra::MakeRange(buffer), 0, [this]
        {
            done.callback();
        });
    flash.EraseSectors(0, 1, [this]
        {
            done.callback();
        });

    EXPECT_CALL(done, callback()).Times(3);
    ExecuteAllActions();
}

class RamBlockDeviceTest
    : public testing::Test
    , public infra::EventDispatcherFixture
{
public:
    infra::Function<void(hal::BlockDevice::Result)> Done()
    {
        return [this](hal::BlockDevice::Result result)
        {
            done.callback(result);
        };
    }

    void Expect(hal::BlockDevice::Result result)
    {
        EXPECT_CALL(done, callback(result));
        ExecuteAllActions();
    }

    std::array<uint8_t, 4 * 512> storage{};
    validation::RamBlockDevice device{ infra::MakeRange(storage) };
    std::array<uint8_t, 512> block{};
    testing::StrictMock<infra::MockCallback<void(hal::BlockDevice::Result)>> done;
};

TEST_F(RamBlockDeviceTest, has_blocks_of_512_bytes_over_its_storage)
{
    EXPECT_EQ(512, device.BlockSize());
    EXPECT_EQ(4, device.NumberOfBlocks());
}

TEST_F(RamBlockDeviceTest, starts_zeroed)
{
    std::fill(storage.begin(), storage.end(), 0x5a);
    validation::RamBlockDevice zeroed{ infra::MakeRange(storage) };

    EXPECT_TRUE(std::all_of(storage.begin(), storage.end(), [](uint8_t byte)
        {
            return byte == 0;
        }));
}

TEST_F(RamBlockDeviceTest, reads_back_what_it_wrote)
{
    block.fill(0xa5);
    device.WriteBlocks(infra::MakeRange(block), 3, Done());
    Expect(hal::BlockDevice::Result::success);

    std::array<uint8_t, 512> readBack{};
    device.ReadBlocks(infra::MakeRange(readBack), 3, Done());
    Expect(hal::BlockDevice::Result::success);

    EXPECT_EQ(block, readBack);
}

TEST_F(RamBlockDeviceTest, an_erase_zeroes_its_blocks_only)
{
    std::fill(storage.begin(), storage.end(), 0x5a);

    device.EraseBlocks(1, 3, Done());
    Expect(hal::BlockDevice::Result::success);

    EXPECT_EQ(0x5a, storage[511]);
    EXPECT_EQ(0, storage[512]);
    EXPECT_EQ(0, storage[3 * 512 - 1]);
    EXPECT_EQ(0x5a, storage[3 * 512]);
}

TEST_F(RamBlockDeviceTest, requests_beyond_the_device_complete_with_out_of_range)
{
    device.ReadBlocks(infra::MakeRange(block), 4, Done());
    Expect(hal::BlockDevice::Result::outOfRange);

    device.WriteBlocks(infra::MakeRange(block), 5, Done());
    Expect(hal::BlockDevice::Result::outOfRange);

    device.EraseBlocks(3, 5, Done());
    Expect(hal::BlockDevice::Result::outOfRange);

    device.EraseBlocks(2, 1, Done());
    Expect(hal::BlockDevice::Result::outOfRange);
}

TEST_F(RamBlockDeviceTest, completes_within_the_call_when_its_completion_is_immediate)
{
    validation::RamBlockDevice immediate{ infra::MakeRange(storage), validation::RamCompletion::immediate };

    EXPECT_CALL(done, callback(hal::BlockDevice::Result::success));
    immediate.WriteBlocks(infra::MakeRange(block), 0, Done());
    EXPECT_CALL(done, callback(hal::BlockDevice::Result::outOfRange));
    immediate.ReadBlocks(infra::MakeRange(block), 4, Done());
}

TEST_F(RamBlockDeviceTest, a_flush_succeeds_from_the_event_dispatcher)
{
    device.Flush(Done());

    Expect(hal::BlockDevice::Result::success);
}
