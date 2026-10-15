#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorSuperblock.hpp"
#include "hal/interfaces/test_doubles/FlashMock.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
    constexpr uint32_t sectorSize = 4096;
    constexpr fs::NorConfig spiNor{ .maxBlocks = 64 };
    constexpr fs::NorConfig internalFlash{ .maxBlocks = 64, .programUnit = 16, .pageSize = 16 };
    constexpr fs::NorConfig twoSectors{ .maxBlocks = 64, .sectorsPerBlock = 2 };

    class UnevenFlash
        : public hal::FlashStub
    {
    public:
        UnevenFlash()
            : hal::FlashStub(32, sectorSize)
        {}

        uint32_t SizeOfSector(uint32_t sectorIndex) const override
        {
            return sectorIndex == 31 ? 2 * sectorSize : sectorSize;
        }
    };

    class NorFormatterTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        template<fs::NorConfig Config>
        void Format(hal::Flash& device)
        {
            typename fs::NorFormatter::WithStorage<Config> formatter{ device };
            formatter.Format([this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(fs::Result::success));
            ExecuteAllActions();
        }

        infra::ConstByteRange Bytes(uint32_t address, uint32_t size) const
        {
            return infra::Head(infra::DiscardHead(infra::MakeRange(flash.sectors[address / sectorSize]), address % sectorSize), size);
        }

        fs::NorBlockHeader Header(uint32_t block, uint32_t programUnit = 1) const
        {
            return fs::ParseNorBlockHeader(Bytes(block * sectorSize, programUnit == 16 ? 48 : 36), programUnit);
        }

        void StoreEraseCount(uint32_t block, uint32_t eraseCount)
        {
            fs::SerializeNorHeaderA(eraseCount, infra::Head(infra::MakeRange(flash.sectors[block]), 12));
        }

        hal::FlashStub flash{ 32, sectorSize };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };
}

TEST_F(NorFormatterTest, writes_both_superblocks)
{
    Format<spiNor>(flash);

    fs::NorSuperblock expected{ 1, sectorSize, 32, 1, 512, 64, 64 };
    EXPECT_EQ(expected, fs::ParseNorSuperblock(Bytes(0, 28)));
    EXPECT_EQ(expected, fs::ParseNorSuperblock(Bytes(sectorSize, 28)));
}

TEST_F(NorFormatterTest, every_block_but_the_first_checkpoint_and_its_node_block_is_free_and_erased)
{
    Format<spiNor>(flash);

    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::node, 1 }), Header(2).allocation);
    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::checkpoint, 2 }), Header(3).allocation);
    for (uint32_t block = 4; block != 32; ++block)
    {
        EXPECT_TRUE(Header(block).IsFreeAndErased()) << block;
        EXPECT_TRUE(std::ranges::all_of(infra::DiscardHead(infra::MakeRange(flash.sectors[block]), 36), [](uint8_t byte)
            {
                return byte == 0xff;
            }))
            << block;
    }
}

TEST_F(NorFormatterTest, a_fresh_device_counts_one_erase_per_block)
{
    Format<spiNor>(flash);

    for (uint32_t block = 2; block != 32; ++block)
        EXPECT_EQ(std::optional<uint32_t>(1), Header(block).eraseCount) << block;
}

TEST_F(NorFormatterTest, keeps_the_erase_counts_and_gives_unknown_counts_the_average)
{
    StoreEraseCount(5, 7);
    StoreEraseCount(6, 3);

    Format<spiNor>(flash);

    EXPECT_EQ(std::optional<uint32_t>(8), Header(5).eraseCount);
    EXPECT_EQ(std::optional<uint32_t>(4), Header(6).eraseCount);
    EXPECT_EQ(std::optional<uint32_t>(6), Header(2).eraseCount);
    EXPECT_EQ(std::optional<uint32_t>(6), Header(31).eraseCount);
}

TEST_F(NorFormatterTest, erases_what_the_device_held)
{
    for (auto& sector : flash.sectors)
        std::ranges::fill(sector, uint8_t{ 0x00 });

    Format<spiNor>(flash);

    EXPECT_TRUE(Header(31).IsFreeAndErased());
    EXPECT_EQ(0xff, flash.sectors[0][sectorSize - 1]);
}

TEST_F(NorFormatterTest, the_inode_table_holds_the_root_directory)
{
    Format<spiNor>(flash);

    auto node = Bytes(2 * sectorSize + 256, 512);
    EXPECT_EQ((fs::NorNodeHeader{ fs::NorMap::inodeTable, 0, 1, 0, 1 }), fs::OpenNorNode(node));
    auto entries = fs::NorNodeEntries(node);
    EXPECT_EQ((std::array<uint8_t, 4>{ 1, 0, 0, 0 }), infra::Head(entries, 4));
    EXPECT_EQ((fs::NorInodeRecord{ 0, fs::NorInodeType::directory }), fs::ParseNorInodeRecord(infra::Head(infra::DiscardHead(entries, 4), 148), spiNor));
}

TEST_F(NorFormatterTest, the_checkpoint_points_to_the_inode_table_and_starts_the_epoch_after_it)
{
    Format<spiNor>(flash);

    auto checkpoint = Bytes(3 * sectorSize + 36, 36);
    EXPECT_EQ((fs::NorCheckpoint{ 2 * sectorSize + 256, 1, 2, 3, 72, 1, 32 }), fs::OpenNorCheckpoint(checkpoint));
    for (uint32_t block = 0; block != 32; ++block)
        EXPECT_EQ(block < 4, fs::IsNorBlockInUse(checkpoint, block)) << block;
    EXPECT_EQ(0xff, flash.sectors[3][72]);
}

TEST_F(NorFormatterTest, with_a_program_unit_of_16_bytes_every_part_starts_on_a_program_unit)
{
    Format<internalFlash>(flash);

    EXPECT_EQ((fs::NorSuperblock{ 1, sectorSize, 32, 16, 512, 64, 64 }), fs::ParseNorSuperblock(Bytes(0, 28)));
    EXPECT_EQ(std::optional<uint32_t>(1), Header(31, 16).eraseCount);
    EXPECT_EQ((fs::NorAllocation{ fs::NorBlockType::node, 1 }), Header(2, 16).allocation);
    EXPECT_EQ((fs::NorNodeHeader{ fs::NorMap::inodeTable, 0, 1, 0, 1 }), fs::OpenNorNode(Bytes(2 * sectorSize + 48, 512)));
    EXPECT_EQ((fs::NorCheckpoint{ 2 * sectorSize + 48, 1, 2, 3, 96, 1, 32 }), fs::OpenNorCheckpoint(Bytes(3 * sectorSize + 48, 36)));
}

TEST_F(NorFormatterTest, blocks_of_two_sectors_erase_both)
{
    hal::FlashStub large{ 64, sectorSize };
    for (auto& sector : large.sectors)
        std::ranges::fill(sector, uint8_t{ 0x00 });

    Format<twoSectors>(large);

    EXPECT_TRUE(std::ranges::all_of(large.sectors[63], [](uint8_t byte)
        {
            return byte == 0xff;
        }));
    EXPECT_EQ((fs::NorSuperblock{ 2, sectorSize, 32, 1, 512, 64, 64 }), fs::ParseNorSuperblock(infra::MakeRange(large.sectors[2])));
}

TEST_F(NorFormatterTest, a_device_that_does_not_fit_is_not_supported)
{
    hal::FlashStub small{ 17, sectorSize };
    fs::NorFormatter::WithStorage<spiNor> formatter{ small };

    formatter.Format([this](fs::Result result)
        {
            done.callback(result);
        });
    testing::Mock::VerifyAndClearExpectations(&done);

    EXPECT_CALL(done, callback(fs::Result::unsupported));
    ExecuteAllActions();
}

TEST_F(NorFormatterTest, sectors_of_different_sizes_are_not_supported)
{
    UnevenFlash uneven;
    fs::NorFormatter::WithStorage<spiNor> formatter{ uneven };

    formatter.Format([this](fs::Result result)
        {
            done.callback(result);
        });

    EXPECT_CALL(done, callback(fs::Result::unsupported));
    ExecuteAllActions();
}

TEST_F(NorFormatterTest, a_device_without_sectors_is_not_supported)
{
    hal::FlashStub empty{ 0, sectorSize };
    fs::NorFormatter::WithStorage<spiNor> formatter{ empty };

    formatter.Format([this](fs::Result result)
        {
            done.callback(result);
        });

    EXPECT_CALL(done, callback(fs::Result::unsupported));
    ExecuteAllActions();
}

TEST_F(NorFormatterTest, completes_through_the_event_dispatcher)
{
    fs::NorFormatter::WithStorage<spiNor> formatter{ flash };

    formatter.Format([this](fs::Result result)
        {
            done.callback(result);
        });
    testing::Mock::VerifyAndClearExpectations(&done);

    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

namespace
{
    class NorFormatterOnMockTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void ExpectReadHeader(uint32_t block)
        {
            EXPECT_CALL(flash, readBufferMock(block * sectorSize)).WillOnce(testing::Return(std::vector<uint8_t>(36, 0xff)));
        }

        void ExpectErase(uint32_t sector)
        {
            EXPECT_CALL(flash, eraseSectorsMock(sector, sector + 1));
        }

        void ExpectProgram(uint32_t size, uint32_t address)
        {
            EXPECT_CALL(flash, writeBufferMock(testing::SizeIs(size), address));
        }

        void CompleteRequests()
        {
            while (flash.done)
                flash.done();
        }

        testing::StrictMock<hal::FlashMock> flash{ 21, sectorSize };
        fs::NorFormatter::WithStorage<fs::NorConfig{ .maxBlocks = 21 }> formatter{ flash };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };
}

TEST_F(NorFormatterOnMockTest, reads_the_counts_erases_the_superblocks_first_and_programs_them_last)
{
    testing::InSequence sequence;
    for (uint32_t block = 2; block != 21; ++block)
        ExpectReadHeader(block);
    ExpectErase(0);
    ExpectErase(1);
    for (uint32_t block = 2; block != 21; ++block)
    {
        ExpectReadHeader(block);
        ExpectErase(block);
        ExpectProgram(12, block * sectorSize);
    }
    ExpectProgram(24, 2 * sectorSize + 12);
    ExpectProgram(512, 2 * sectorSize + 256);
    ExpectProgram(24, 3 * sectorSize + 12);
    ExpectProgram(35, 3 * sectorSize + 36);
    ExpectProgram(28, 0);
    ExpectProgram(28, sectorSize);

    formatter.Format([this](fs::Result result)
        {
            done.callback(result);
        });
    CompleteRequests();

    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}
