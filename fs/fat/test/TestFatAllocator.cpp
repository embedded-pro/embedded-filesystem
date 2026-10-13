#include "fs/fat/FatAllocator.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <utility>

namespace
{
    class FatAllocatorTest
        : public FatVolumeFixture
    {
    public:
        explicit FatAllocatorTest(uint32_t blocks = 16384)
            : FatVolumeFixture(blocks, ClusterSize(1))
        {
            state.geometry = geometry;
            cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ geometry.firstFatSector, geometry.sectorsPerFat, geometry.fatCopies });
            allocator.Reset();
        }

        void ExpectGrow(std::optional<uint32_t> last, uint32_t clusters, fs::Result result, uint32_t first, uint32_t allocated)
        {
            allocator.Grow(last, clusters, [this](fs::Result result, uint32_t first, uint32_t allocated)
                {
                    grown.callback(result, first, allocated);
                });
            EXPECT_CALL(grown, callback(result, first, allocated));
            ExecuteAllActions();
        }

        void Flush()
        {
            cache.Flush([this](fs::Result result)
                {
                    flushed.callback(result);
                });
            EXPECT_CALL(flushed, callback(fs::Result::success));
            ExecuteAllActions();
        }

        uint32_t EndOfChain() const
        {
            return fs::EndOfChain(geometry.type);
        }

        void StartAt(uint32_t cluster)
        {
            state.cleanAtMount = true;
            state.fsInfo = fs::FsInfo{ std::nullopt, cluster };
            allocator.Reset();
        }

        fs::FatVolumeState state;
        fs::FatTableWriter writer{ cache, state.geometry };
        fs::FatAllocator allocator{ cache, state, writer };
        testing::StrictMock<infra::MockCallback<void(fs::Result, uint32_t, uint32_t)>> grown;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> flushed;
    };

    class FatAllocatorOnFat12Test
        : public FatAllocatorTest
    {
    public:
        FatAllocatorOnFat12Test()
            : FatAllocatorTest(4096)
        {}
    };
}

TEST_F(FatAllocatorTest, new_chain_starts_at_the_cursor)
{
    EXPECT_EQ(2u, allocator.Cursor());

    ExpectGrow(std::nullopt, 1, fs::Result::success, 2, 1);
    Flush();

    EXPECT_EQ(EndOfChain(), builder->FatEntry(2));
    EXPECT_EQ(3u, allocator.Cursor());
}

TEST_F(FatAllocatorTest, chain_takes_contiguous_free_clusters)
{
    ExpectGrow(std::nullopt, 3, fs::Result::success, 2, 3);
    Flush();

    EXPECT_EQ(3u, builder->FatEntry(2));
    EXPECT_EQ(4u, builder->FatEntry(3));
    EXPECT_EQ(EndOfChain(), builder->FatEntry(4));
}

TEST_F(FatAllocatorTest, chain_skips_clusters_in_use)
{
    builder->SetFatEntry(3, EndOfChain());

    ExpectGrow(std::nullopt, 2, fs::Result::success, 2, 2);
    Flush();

    EXPECT_EQ(4u, builder->FatEntry(2));
    EXPECT_EQ(EndOfChain(), builder->FatEntry(4));
}

TEST_F(FatAllocatorTest, file_grows_into_the_cluster_after_its_last)
{
    auto last = builder->AllocateChain(1);
    StartAt(100);

    ExpectGrow(last, 2, fs::Result::success, last + 1, 2);
    Flush();

    EXPECT_EQ(last + 1, builder->FatEntry(last));
    EXPECT_EQ(last + 2, builder->FatEntry(last + 1));
    EXPECT_EQ(EndOfChain(), builder->FatEntry(last + 2));
}

TEST_F(FatAllocatorTest, file_whose_next_cluster_is_in_use_grows_at_the_cursor)
{
    auto last = builder->AllocateChain(1);
    builder->AllocateChain(1);
    StartAt(100);

    ExpectGrow(last, 1, fs::Result::success, 100, 1);
    Flush();

    EXPECT_EQ(100u, builder->FatEntry(last));
    EXPECT_EQ(101u, allocator.Cursor());
}

TEST_F(FatAllocatorTest, cursor_moves_past_each_allocation)
{
    ExpectGrow(std::nullopt, 1, fs::Result::success, 2, 1);

    ExpectGrow(std::nullopt, 1, fs::Result::success, 3, 1);
}

TEST_F(FatAllocatorTest, search_wraps_around_at_the_end_of_the_volume)
{
    auto lastCluster = geometry.clusterCount + 1;
    StartAt(lastCluster);

    ExpectGrow(std::nullopt, 2, fs::Result::success, lastCluster, 2);
    Flush();

    EXPECT_EQ(2u, builder->FatEntry(lastCluster));
    EXPECT_EQ(3u, allocator.Cursor());
}

TEST_F(FatAllocatorTest, cursor_starts_at_the_fsinfo_hint_only_on_a_clean_volume)
{
    StartAt(500);
    EXPECT_EQ(500u, allocator.Cursor());

    state.cleanAtMount = false;
    allocator.Reset();
    EXPECT_EQ(2u, allocator.Cursor());

    state.cleanAtMount = true;
    state.fsInfo = fs::FsInfo{ std::nullopt, std::nullopt };
    allocator.Reset();
    EXPECT_EQ(2u, allocator.Cursor());
}

TEST_F(FatAllocatorTest, known_free_count_is_reduced)
{
    state.freeClusters = 100;

    ExpectGrow(std::nullopt, 3, fs::Result::success, 2, 3);

    EXPECT_EQ(97u, state.freeClusters);
}

TEST_F(FatAllocatorTest, unknown_free_count_stays_unknown)
{
    ExpectGrow(std::nullopt, 3, fs::Result::success, 2, 3);

    EXPECT_EQ(std::nullopt, state.freeClusters);
}

TEST_F(FatAllocatorTest, link_in_the_sector_of_the_new_entries_needs_no_flush)
{
    auto last = builder->AllocateChain(1);
    auto writes = device.WriteRequests();
    auto flushes = device.FlushRequests();

    ExpectGrow(last, 2, fs::Result::success, last + 1, 2);

    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(flushes, device.FlushRequests());
}

TEST_F(FatAllocatorTest, link_in_another_fat_sector_flushes_the_new_entries_first)
{
    auto last = builder->AllocateChain(1);
    builder->AllocateChain(1);
    StartAt(300);
    infra::BoundedVector<std::pair<fs::SparseBlockDevice::Request, uint32_t>>::WithMaxSize<16> requests;
    device.Observe([&requests](fs::SparseBlockDevice::Request request, uint32_t block)
        {
            if (request != fs::SparseBlockDevice::Request::read)
                requests.emplace_back(request, block);
        });

    ExpectGrow(last, 1, fs::Result::success, 300, 1);
    Flush();

    auto firstCopy = geometry.firstFatSector;
    auto secondCopy = geometry.firstFatSector + geometry.sectorsPerFat;
    using Request = fs::SparseBlockDevice::Request;
    ASSERT_EQ(6u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, firstCopy + 1), requests[0]);
    EXPECT_EQ(std::make_pair(Request::write, secondCopy + 1), requests[1]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[2]);
    EXPECT_EQ(std::make_pair(Request::write, firstCopy), requests[3]);
    EXPECT_EQ(std::make_pair(Request::write, secondCopy), requests[4]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[5]);
    EXPECT_EQ(300u, builder->FatEntry(last));
}

TEST_F(FatAllocatorTest, failed_flush_before_the_link_is_reported)
{
    auto last = builder->AllocateChain(1);
    builder->AllocateChain(1);
    StartAt(300);
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 3);

    ExpectGrow(last, 1, fs::Result::deviceError, 300, 1);
}

TEST_F(FatAllocatorTest, failure_to_read_back_an_evicted_fat_sector_is_reported)
{
    for (uint32_t sector = 0; sector != 3; ++sector)
    {
        cache.Modify(geometry.firstDataSector + sector, 2, [](fs::Result, infra::ByteRange) {});
        ExecuteAllActions();
    }
    for (uint32_t cluster = 3; cluster != 256; ++cluster)
        builder->SetFatEntry(cluster, fs::EndOfChain(geometry.type));
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 6);

    ExpectGrow(std::nullopt, 2, fs::Result::deviceError, 2, 1);
}

TEST_F(FatAllocatorTest, device_error_while_searching_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectGrow(std::nullopt, 1, fs::Result::deviceError, 0, 0);
}

TEST_F(FatAllocatorOnFat12Test, clusters_whose_entry_straddles_two_sectors_are_never_taken)
{
    ASSERT_EQ(fs::FatType::fat12, geometry.type);
    StartAt(341);

    ExpectGrow(std::nullopt, 1, fs::Result::success, 342, 1);
}

TEST_F(FatAllocatorOnFat12Test, full_volume_allocates_what_is_free_and_reports_no_space)
{
    uint32_t free = 0;
    for (uint32_t cluster = 2; cluster != geometry.clusterCount + 2; ++cluster)
        if (!fs::LocateEntry(geometry.type, cluster, 512).straddles)
            ++free;
    builder->AllocateChain(geometry.clusterCount - 2);
    auto remaining = 0u;
    auto firstFree = 0u;
    for (uint32_t cluster = 2; cluster != geometry.clusterCount + 2; ++cluster)
        if (builder->FatEntry(cluster) == 0 && !fs::LocateEntry(geometry.type, cluster, 512).straddles)
        {
            firstFree = firstFree == 0 ? cluster : firstFree;
            ++remaining;
        }
    ASSERT_NE(0u, remaining);
    EXPECT_GT(free, remaining);

    ExpectGrow(std::nullopt, remaining + 3, fs::Result::noSpace, firstFree, remaining);
    ExpectGrow(std::nullopt, 1, fs::Result::noSpace, 0, 0);
}

TEST_F(FatAllocatorOnFat12Test, file_on_a_full_volume_keeps_its_chain)
{
    auto last = builder->AllocateChain(geometry.clusterCount);

    ExpectGrow(last, 1, fs::Result::noSpace, 0, 0);
}

TEST_F(FatAllocatorOnFat12Test, search_from_the_cursor_reaches_the_cluster_just_before_it)
{
    builder->AllocateChain(geometry.clusterCount);
    builder->SetFatEntry(10, 0);
    StartAt(11);

    ExpectGrow(100, 1, fs::Result::success, 10, 1);
}
