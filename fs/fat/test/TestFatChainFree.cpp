#include "fs/common/DirectTransfers.hpp"
#include "fs/fat/FatChainFree.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"

namespace
{
    class FatChainFreeOnFat12Test
        : public FatVolumeFixture
    {
    public:
        FatChainFreeOnFat12Test()
            : FatVolumeFixture(4096, ClusterSize(1))
        {
            state.geometry = geometry;
            tinyCache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ geometry.firstFatSector, geometry.sectorsPerFat, geometry.fatCopies });
        }

        void ExpectFree(uint32_t first, fs::Result result)
        {
            chain.Free(first, [this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        fs::SectorCache::WithStorage<1> tinyCache{ barrier };
        fs::DirectTransfers transfers{ barrier, tinyCache };
        fs::FatVolumeState state;
        fs::FatTableWriter writer{ tinyCache, state.geometry };
        fs::FatChainFree chain{ tinyCache, transfers, state, writer, true };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };
}

TEST_F(FatChainFreeOnFat12Test, freed_clusters_add_to_a_known_free_count)
{
    builder->SetFatEntry(10, fs::EndOfChain(geometry.type));
    state.freeClusters = 5;

    ExpectFree(10, fs::Result::success);

    EXPECT_EQ(6u, state.freeClusters);
    EXPECT_TRUE(state.tableChanged);
}

TEST_F(FatChainFreeOnFat12Test, failure_to_write_a_straddling_entry_is_reported)
{
    ASSERT_TRUE(fs::LocateEntry(geometry.type, 341, 512).straddles);
    builder->SetFatEntry(341, fs::EndOfChain(geometry.type));
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 2);

    ExpectFree(341, fs::Result::deviceError);
}
