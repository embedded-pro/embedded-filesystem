#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "validation/firmware/baselines/LittleFsFlash.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <tuple>

class LittleFsFlashTest
    : public testing::Test
    , public infra::EventDispatcherFixture
{
public:
    LittleFsFlashTest()
    {
        EXPECT_CALL(flash, NumberOfSectors()).WillRepeatedly(testing::Return(4096));
        EXPECT_CALL(flash, SizeOfSector(0)).WillRepeatedly(testing::Return(4096));
        littleFsFlash.Configure(config);
    }

    auto Completes()
    {
        return [this](auto&&... arguments)
        {
            pending = std::get<sizeof...(arguments) - 1>(std::forward_as_tuple(arguments...));
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    completed = true;
                    pending();
                });
        };
    }

    testing::StrictMock<hal::CleanFlashMock> flash;
    validation::LittleFsFlash littleFsFlash{ flash };
    lfs_config config{};
    infra::Function<void()> pending;
    bool completed = false;
};

TEST_F(LittleFsFlashTest, the_blocks_are_the_sectors_of_the_flash)
{
    EXPECT_EQ(4096, config.block_size);
    EXPECT_EQ(4096, config.block_count);
}

TEST_F(LittleFsFlashTest, a_read_waits_for_the_flash)
{
    std::array<uint8_t, 16> buffer{};

    EXPECT_CALL(flash, ReadBuffer(infra::ByteRange(buffer.data(), buffer.data() + 16), 2 * 4096 + 32, testing::_)).WillOnce(Completes());
    EXPECT_EQ(LFS_ERR_OK, config.read(&config, 2, 32, buffer.data(), 16));
    EXPECT_TRUE(completed);
}

TEST_F(LittleFsFlashTest, a_program_waits_for_the_flash)
{
    const std::array<uint8_t, 16> buffer{};

    EXPECT_CALL(flash, WriteBuffer(infra::ConstByteRange(buffer.data(), buffer.data() + 16), 3 * 4096 + 16, testing::_)).WillOnce(Completes());
    EXPECT_EQ(LFS_ERR_OK, config.prog(&config, 3, 16, buffer.data(), 16));
    EXPECT_TRUE(completed);
}

TEST_F(LittleFsFlashTest, an_erase_waits_for_the_flash)
{
    EXPECT_CALL(flash, EraseSectors(5, 6, testing::_)).WillOnce(Completes());
    EXPECT_EQ(LFS_ERR_OK, config.erase(&config, 5));
    EXPECT_TRUE(completed);
}

TEST_F(LittleFsFlashTest, a_sync_does_nothing)
{
    EXPECT_EQ(LFS_ERR_OK, config.sync(&config));
}
