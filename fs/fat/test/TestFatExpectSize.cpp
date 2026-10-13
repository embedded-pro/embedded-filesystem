#include "fs/fat/FatAllocationUnits.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include <array>

namespace
{
    fs::FatConfig AllocationUnit(uint32_t blocks)
    {
        fs::FatConfig config;
        config.blocksPerAllocationUnit = blocks;
        return config;
    }

    class FatExpectSizeTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatExpectSizeTest(const fs::FatConfig& config = {})
            : FatFileSystemFixture(16384, ClusterSize(1), config)
        {}

        void SetUp() override
        {
            builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(notes));
            ExpectMount();
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        void ExpectExpectSize(fs::File& file, uint32_t size, fs::Result result = fs::Result::success)
        {
            file.ExpectSize(size, Done());
            ExpectDone(result);
        }

        void ExpectWrite(fs::File& file, infra::ConstByteRange data)
        {
            file.Write(data, Done());
            ExpectDone(fs::Result::success);
        }

        void ExpectClose(fs::File& file)
        {
            file.Close(Done());
            ExpectDone(fs::Result::success);
        }

        uint32_t FirstCluster(fs::File& file)
        {
            return static_cast<fs::FatFile&>(file).State().firstCluster;
        }

        bool Contiguous(uint32_t first, uint32_t clusters)
        {
            for (auto cluster = first; cluster != first + clusters - 1; ++cluster)
                if (builder->FatEntry(cluster) != cluster + 1)
                    return false;

            return fs::IsEndOfChain(builder->Geometry().type, builder->FatEntry(first + clusters - 1));
        }

        std::array<uint8_t, 700> notes{};
        std::array<uint8_t, 512> cluster{};
        std::array<uint8_t, 10 * 512> tenClusters{};
    };

    class FatExpectSizeWithAllocationUnitsTest
        : public FatExpectSizeTest
    {
    public:
        FatExpectSizeWithAllocationUnitsTest()
            : FatExpectSizeTest(AllocationUnit(64))
        {}
    };
}

TEST_F(FatExpectSizeTest, writes_of_a_new_file_take_the_reserved_run)
{
    auto log = ExpectOpen("/log.bin", fs::OpenMode::write);
    auto other = ExpectOpen("/other.bin", fs::OpenMode::write);
    ExpectExpectSize(*log, 10 * 512);

    ExpectWrite(*other, infra::MakeRange(cluster));
    ExpectWrite(*log, infra::MakeRange(tenClusters));
    ExpectClose(*log);
    ExpectClose(*other);

    EXPECT_EQ(4u, FirstCluster(*log));
    EXPECT_EQ(14u, FirstCluster(*other));
    EXPECT_TRUE(Contiguous(4, 10));
}

TEST_F(FatExpectSizeTest, growing_file_reserves_the_clusters_after_its_last)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto other = ExpectOpen("/other.bin", fs::OpenMode::write);
    ExpectExpectSize(*file, 700 + 3000);

    ExpectWrite(*other, infra::MakeRange(cluster));
    std::array<uint8_t, 3000> added{};
    ExpectWrite(*file, infra::MakeRange(added));
    ExpectClose(*file);
    ExpectClose(*other);

    EXPECT_TRUE(Contiguous(2, 8));
    EXPECT_EQ(10u, FirstCluster(*other));
}

TEST_F(FatExpectSizeTest, close_returns_the_unused_part_of_the_reservation)
{
    auto log = ExpectOpen("/log.bin", fs::OpenMode::write);
    ExpectExpectSize(*log, 10 * 512);
    ExpectWrite(*log, infra::MakeRange(cluster));
    ExpectClose(*log);

    auto other = ExpectOpen("/other.bin", fs::OpenMode::write);
    ExpectWrite(*other, infra::MakeRange(cluster));

    EXPECT_EQ(5u, FirstCluster(*other));
}

TEST_F(FatExpectSizeTest, reservation_is_never_written)
{
    auto log = ExpectOpen("/log.bin", fs::OpenMode::write);
    ExecuteAllActions();
    auto writes = device.WriteRequests();
    auto flushes = device.FlushRequests();

    ExpectExpectSize(*log, 100 * 512);

    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(flushes, device.FlushRequests());
}

TEST_F(FatExpectSizeTest, size_that_is_already_allocated_reserves_nothing)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    ExpectExpectSize(*file, 1000);

    EXPECT_EQ((fs::FatReservation{}), static_cast<fs::FatFile&>(*file).State().reservation);
}

TEST_F(FatExpectSizeTest, new_expected_size_replaces_the_reservation)
{
    auto log = ExpectOpen("/log.bin", fs::OpenMode::write);
    ExpectExpectSize(*log, 10 * 512);

    ExpectExpectSize(*log, 20 * 512);
    EXPECT_EQ((fs::FatReservation{ 4, 20 }), static_cast<fs::FatFile&>(*log).State().reservation);

    ExpectWrite(*log, infra::MakeRange(cluster));
    ExpectExpectSize(*log, 512);
    EXPECT_EQ((fs::FatReservation{}), static_cast<fs::FatFile&>(*log).State().reservation);
}

TEST_F(FatExpectSizeTest, call_while_another_is_pending_is_busy)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);

    file->Write(infra::MakeRange(cluster), Done());
    file->ExpectSize(5000, Done());
    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatExpectSizeTest, failure_to_find_the_last_cluster_is_reported)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectExpectSize(*file, 5000, fs::Result::deviceError);
}

TEST_F(FatExpectSizeTest, expect_size_after_unmount_is_not_mounted)
{
    auto file = ExpectOpen("/log.bin", fs::OpenMode::write);
    ExpectUnmount();

    ExpectExpectSize(*file, 5000, fs::Result::notMounted);
}

TEST_F(FatExpectSizeWithAllocationUnitsTest, run_that_would_cross_an_allocation_unit_starts_at_its_boundary)
{
    auto units = fs::AllocationUnitsOf(builder->Geometry(), 64);
    ASSERT_TRUE(units.has_value());
    auto boundary = units->NextBoundaryAfter(10);
    builder->AllocateChain(boundary - 5 - 4);

    auto log = ExpectOpen("/log.bin", fs::OpenMode::write);
    ExpectExpectSize(*log, 10 * 512);
    ExpectWrite(*log, infra::MakeRange(tenClusters));

    EXPECT_EQ(boundary, FirstCluster(*log));
}
