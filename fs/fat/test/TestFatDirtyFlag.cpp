#include "fs/fat/FatDirtyFlag.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    class FatDirtyFlagTest
        : public FatVolumeFixture
    {
    public:
        explicit FatDirtyFlagTest(uint32_t blocks = 16384)
            : FatVolumeFixture(blocks, ClusterSize(1))
        {
            state.geometry = geometry;
            cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ geometry.firstFatSector, geometry.sectorsPerFat, geometry.fatCopies });
        }

        void ExpectMarkDirty(fs::Result result = fs::Result::success)
        {
            flag.MarkDirty([this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        void ExpectMarkClean(fs::Result result = fs::Result::success)
        {
            flag.MarkClean([this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        void Flush()
        {
            cache.Flush([this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(fs::Result::success));
            ExecuteAllActions();
        }

        std::optional<bool> CleanInCopy(uint8_t copy) const
        {
            std::array<uint8_t, 512> sector{};
            device.ReadBlock(geometry.firstFatSector + copy * geometry.sectorsPerFat, infra::MakeRange(sector));
            return fs::CleanShutdown(geometry.type, fs::ReadEntry(geometry.type, 1, infra::MakeRange(sector), infra::ConstByteRange()));
        }

        void MarkDirtyOnDisk()
        {
            builder->SetFatEntry(1, fs::WithCleanShutdown(geometry.type, builder->FatEntry(1), false));
        }

        uint32_t Requests() const
        {
            return device.ReadRequests() + device.WriteRequests() + device.FlushRequests();
        }

        fs::FatVolumeState state;
        fs::FatDirtyFlag flag{ cache, state };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    class FatDirtyFlagOnFat12Test
        : public FatDirtyFlagTest
    {
    public:
        FatDirtyFlagOnFat12Test()
            : FatDirtyFlagTest(4096)
        {}
    };

    class FatDirtyFlagOnFat32Test
        : public FatDirtyFlagTest
    {
    public:
        FatDirtyFlagOnFat32Test()
            : FatDirtyFlagTest(81920)
        {}
    };
}

TEST_F(FatDirtyFlagTest, marking_dirty_clears_the_clean_bit_of_every_copy_and_flushes)
{
    ASSERT_EQ(fs::FatType::fat16, geometry.type);
    ASSERT_EQ(true, CleanInCopy(0));
    auto flushes = device.FlushRequests();

    ExpectMarkDirty();

    EXPECT_EQ(false, CleanInCopy(0));
    EXPECT_EQ(false, CleanInCopy(1));
    EXPECT_EQ(flushes + 1, device.FlushRequests());
}

TEST_F(FatDirtyFlagTest, marking_dirty_keeps_the_rest_of_the_second_entry)
{
    auto before = builder->FatEntry(1);

    ExpectMarkDirty();

    EXPECT_EQ(fs::WithCleanShutdown(geometry.type, before, false), builder->FatEntry(1));
}

TEST_F(FatDirtyFlagTest, marking_dirty_again_needs_no_device_request)
{
    ExpectMarkDirty();
    auto requests = Requests();

    ExpectMarkDirty();

    EXPECT_EQ(requests, Requests());
}

TEST_F(FatDirtyFlagTest, marking_clean_sets_the_clean_bit_that_this_mount_cleared)
{
    ExpectMarkDirty();

    ExpectMarkClean();
    Flush();

    EXPECT_EQ(true, CleanInCopy(0));
    EXPECT_EQ(true, CleanInCopy(1));
}

TEST_F(FatDirtyFlagTest, marking_clean_without_a_change_writes_nothing)
{
    auto requests = Requests();

    ExpectMarkClean();
    Flush();

    EXPECT_EQ(requests, Requests());
}

TEST_F(FatDirtyFlagTest, volume_that_was_not_clean_is_left_as_it_is)
{
    MarkDirtyOnDisk();
    auto writes = device.WriteRequests();

    ExpectMarkDirty();
    ExpectMarkClean();
    Flush();

    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(false, CleanInCopy(0));
}

TEST_F(FatDirtyFlagTest, reset_forgets_what_this_mount_marked)
{
    ExpectMarkDirty();

    flag.Reset();
    ExpectMarkClean();
    Flush();

    EXPECT_EQ(false, CleanInCopy(0));
}

TEST_F(FatDirtyFlagTest, failed_read_is_reported_and_marking_is_tried_again)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectMarkDirty(fs::Result::deviceError);
    ExpectMarkDirty();

    EXPECT_EQ(false, CleanInCopy(0));
}

TEST_F(FatDirtyFlagTest, write_protected_card_is_reported_and_its_sector_is_read_again)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::writeProtected, 1);

    ExpectMarkDirty(fs::Result::readOnly);

    auto reads = device.ReadRequests();
    cache.Read(state.geometry.ActiveFatSector(), [this](fs::Result result, infra::ConstByteRange contents)
        {
            EXPECT_EQ(true, fs::CleanShutdown(geometry.type, fs::ReadEntry(geometry.type, 1, contents, infra::ConstByteRange())));
            done.callback(result);
        });
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
    EXPECT_EQ(reads + 1, device.ReadRequests());
}

TEST_F(FatDirtyFlagTest, marking_completes_through_the_event_dispatcher)
{
    flag.MarkDirty([this](fs::Result result)
        {
            done.callback(result);
        });

    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatDirtyFlagOnFat12Test, fat12_has_no_clean_bit)
{
    ASSERT_EQ(fs::FatType::fat12, geometry.type);
    auto requests = Requests();

    ExpectMarkDirty();
    ExpectMarkClean();

    EXPECT_EQ(requests, Requests());
}

TEST_F(FatDirtyFlagOnFat32Test, marking_dirty_clears_the_clean_bit_of_fat32)
{
    ASSERT_EQ(fs::FatType::fat32, geometry.type);

    ExpectMarkDirty();

    EXPECT_EQ(false, CleanInCopy(0));
}

TEST_F(FatDirtyFlagTest, failure_to_modify_the_first_fat_sector_is_reported)
{
    cache.Modify(state.geometry.ActiveFatSector(), 2, [](fs::Result, infra::ByteRange) {});
    ExecuteAllActions();
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    ExpectMarkDirty(fs::Result::deviceError);
}
