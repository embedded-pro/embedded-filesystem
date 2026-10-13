#include "fs/fat/FatEntrySetClear.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

namespace
{
    class FatEntrySetClearTest
        : public FatVolumeFixture
    {
    public:
        FatEntrySetClearTest()
            : FatVolumeFixture(16384, ClusterSize(1))
        {}

        void ExpectClear(const fs::FatScannedEntry& entry, fs::Result result)
        {
            clear.Clear(entry, [this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        std::array<uint8_t, 255> nameBuffer{};
        fs::FatEntryAssembler assembler{ infra::MakeRange(nameBuffer) };
        fs::FatDirectoryReader reader{ cache, geometry, assembler };
        fs::FatEntrySetClear clear{ cache, reader, geometry };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    fs::FatScannedEntry SetOf(fs::FatEntryPosition first, fs::FatEntryPosition shortEntry, uint8_t slots)
    {
        fs::FatScannedEntry entry;
        entry.first = first;
        entry.shortEntry = shortEntry;
        entry.slots = slots;
        return entry;
    }
}

TEST_F(FatEntrySetClearTest, set_across_three_clusters_without_a_middle_cluster_is_corrupted)
{
    auto directory = builder->AllocateChain(1);

    ExpectClear(SetOf(fs::FatEntryPosition{ directory, 15 }, fs::FatEntryPosition{ 300, 3 }, 21), fs::Result::corrupted);
}

TEST_F(FatEntrySetClearTest, device_error_while_finding_the_middle_cluster_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectClear(SetOf(fs::FatEntryPosition{ 2, 15 }, fs::FatEntryPosition{ 300, 3 }, 21), fs::Result::deviceError);
}

TEST_F(FatEntrySetClearTest, device_error_while_reading_the_sector_of_the_short_entry_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectClear(SetOf(fs::FatEntryPosition{ 0, 0 }, fs::FatEntryPosition{ 0, 2 }, 3), fs::Result::deviceError);
}
