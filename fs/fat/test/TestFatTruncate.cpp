#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace
{
    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern(uint8_t seed)
    {
        std::array<uint8_t, Size> pattern;

        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 7 + seed);

        return pattern;
    }

    class FatTruncateTest
        : public FatFileSystemFixture
    {
    public:
        void SetUp() override
        {
            first = builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(original));
            ExpectMount();
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        void ExpectTruncate(fs::File& file, uint32_t position, fs::Result result = fs::Result::success)
        {
            file.Seek(position);
            file.Truncate(Done());
            ExpectDone(result);
        }

        void RecordRequests()
        {
            device.Observe([this](fs::SparseBlockDevice::Request request, uint32_t block)
                {
                    if (request != fs::SparseBlockDevice::Request::read)
                        requests.emplace_back(request, block);
                });
        }

        void Remount()
        {
            ExpectUnmount();
            ExpectMount();
        }

        void ExpectHead(infra::BoundedConstString path, std::size_t size)
        {
            auto file = ExpectOpen(path);
            ASSERT_NE(nullptr, file);
            EXPECT_EQ(size, file->Size());
            std::array<uint8_t, 1301> read{};
            ExpectRead(*file, infra::MakeRange(read), fs::Result::success, size);
            EXPECT_TRUE(std::equal(original.begin(), original.begin() + size, read.begin()));
        }

        std::array<uint8_t, 1300> original{ Pattern<1300>(3) };
        uint32_t first{ 0 };
        infra::BoundedVector<std::pair<fs::SparseBlockDevice::Request, uint32_t>>::WithMaxSize<32> requests;
    };

    using Request = fs::SparseBlockDevice::Request;
}

TEST_F(FatTruncateTest, truncate_in_the_middle_keeps_the_head_and_frees_the_rest)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);

    ExpectTruncate(*file, 100);

    EXPECT_EQ(100u, file->Size());
    EXPECT_EQ(100u, file->Position());
    file = nullptr;
    Remount();
    ExpectHead("/notes.txt", 100);
    EXPECT_TRUE(fs::IsEndOfChain(builder->Geometry().type, builder->FatEntry(first)));
    EXPECT_EQ(0u, builder->FatEntry(first + 1));
    EXPECT_EQ(0u, builder->FatEntry(first + 2));
}

TEST_F(FatTruncateTest, entry_then_end_of_chain_then_freed_clusters_each_reach_the_device_before_the_next)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    RecordRequests();

    ExpectTruncate(*file, 100);

    const auto& geometry = builder->Geometry();
    auto secondFat = geometry.firstFatSector + geometry.sectorsPerFat;
    ASSERT_EQ(9u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, geometry.rootSector), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector), requests[2]);
    EXPECT_EQ(std::make_pair(Request::write, secondFat), requests[3]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[4]);
    EXPECT_EQ(std::make_pair(Request::erase, geometry.SectorOfCluster(first + 1)), requests[5]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector), requests[6]);
    EXPECT_EQ(std::make_pair(Request::write, secondFat), requests[7]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[8]);
}

TEST_F(FatTruncateTest, truncate_to_zero_frees_the_whole_chain_without_an_end_of_chain)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    RecordRequests();

    ExpectTruncate(*file, 0);

    ASSERT_EQ(6u, requests.size());
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_EQ(std::make_pair(Request::erase, builder->Geometry().SectorOfCluster(first)), requests[2]);
    file = nullptr;
    Remount();
    ExpectHead("/notes.txt", 0);
    EXPECT_EQ(0u, builder->FatEntry(first));
    EXPECT_EQ(0u, builder->FatEntry(first + 2));
}

TEST_F(FatTruncateTest, truncate_at_the_end_writes_nothing)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    RecordRequests();

    ExpectTruncate(*file, 1300);

    EXPECT_TRUE(requests.empty());
    EXPECT_EQ(1300u, file->Size());
}

TEST_F(FatTruncateTest, truncate_within_the_last_cluster_only_updates_the_entry)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    RecordRequests();

    ExpectTruncate(*file, 1100);

    ASSERT_EQ(2u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, builder->Geometry().rootSector), requests[0]);
    file = nullptr;
    Remount();
    ExpectHead("/notes.txt", 1100);
    EXPECT_EQ(first + 2, builder->FatEntry(first + 1));
}

TEST_F(FatTruncateTest, truncate_of_a_fragmented_file_frees_each_cut_run)
{
    std::array<uint8_t, 2000> contents{};
    auto spread = builder->AddFile(0, "", "SPREAD  BIN", infra::MakeRange(contents), true);
    auto file = ExpectOpen("/spread.bin", fs::OpenMode::readWrite);
    auto erases = device.EraseRequests();

    ExpectTruncate(*file, 600);

    EXPECT_EQ(erases + 2, device.EraseRequests());
    file = nullptr;
    Remount();
    auto reopened = ExpectOpen("/spread.bin");
    EXPECT_EQ(600u, reopened->Size());
    auto second = builder->FatEntry(spread);
    EXPECT_TRUE(builder->Geometry().IsCluster(second));
    EXPECT_TRUE(fs::IsEndOfChain(builder->Geometry().type, builder->FatEntry(second)));
}

TEST_F(FatTruncateTest, write_after_truncate_grows_the_chain_again)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    ExpectTruncate(*file, 100);
    auto added = Pattern<600>(9);

    file->Write(infra::MakeRange(added), Done());
    ExpectDone(fs::Result::success);
    file->Close(Done());
    ExpectDone(fs::Result::success);

    file = nullptr;
    Remount();
    auto reopened = ExpectOpen("/notes.txt");
    ASSERT_NE(nullptr, reopened);
    EXPECT_EQ(700u, reopened->Size());
    std::array<uint8_t, 701> read{};
    ExpectRead(*reopened, infra::MakeRange(read), fs::Result::success, 700);
    EXPECT_TRUE(std::equal(original.begin(), original.begin() + 100, read.begin()));
    EXPECT_TRUE(std::equal(added.begin(), added.end(), read.begin() + 100));
}

TEST_F(FatTruncateTest, truncate_of_a_chain_shorter_than_its_size_writes_nothing)
{
    builder->SetFatEntry(first, fs::EndOfChain(builder->Geometry().type));
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    RecordRequests();

    ExpectTruncate(*file, 600, fs::Result::corrupted);

    EXPECT_TRUE(requests.empty());
}

TEST_F(FatTruncateTest, truncate_where_the_chain_already_ends_frees_nothing)
{
    builder->SetFatEntry(first, fs::EndOfChain(builder->Geometry().type));
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    RecordRequests();

    ExpectTruncate(*file, 100);

    ASSERT_EQ(2u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, builder->Geometry().rootSector), requests[0]);
}

TEST_F(FatTruncateTest, truncate_where_the_chain_runs_into_a_free_cluster_is_corrupted)
{
    builder->SetFatEntry(first, 0);
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    RecordRequests();

    ExpectTruncate(*file, 100, fs::Result::corrupted);

    EXPECT_TRUE(requests.empty());
}

TEST_F(FatTruncateTest, failed_entry_update_frees_nothing)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 1);

    ExpectTruncate(*file, 100, fs::Result::deviceError);

    EXPECT_EQ(first + 1, builder->FatEntry(first));
}

TEST_F(FatTruncateTest, failed_end_of_chain_frees_nothing)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 3);

    ExpectTruncate(*file, 100, fs::Result::deviceError);

    EXPECT_EQ(first + 2, builder->FatEntry(first + 1));
}

TEST_F(FatTruncateTest, failed_discard_of_the_cut_clusters_reports_the_error)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 6);

    ExpectTruncate(*file, 100, fs::Result::deviceError);
}

TEST_F(FatTruncateTest, failed_final_flush_reports_the_error)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError, 9);

    ExpectTruncate(*file, 100, fs::Result::deviceError);
}

TEST_F(FatTruncateTest, truncate_through_a_read_handle_is_read_only)
{
    auto file = ExpectOpen("/notes.txt");

    ExpectTruncate(*file, 100, fs::Result::readOnly);
}

TEST_F(FatTruncateTest, truncate_while_another_call_is_pending_is_busy)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    file->Sync(Done());
    file->Truncate(Done());

    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatTruncateTest, truncate_completes_after_the_event_loop_runs)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    file->Truncate(Done());

    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatTruncateTest, truncate_after_unmount_is_not_mounted)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    ExpectUnmount();

    ExpectTruncate(*file, 100, fs::Result::notMounted);
}

TEST_F(FatTruncateTest, open_for_writing_truncates_an_existing_file)
{
    RecordRequests();

    auto file = ExpectOpen("/notes.txt", fs::OpenMode::write);

    ASSERT_NE(nullptr, file);
    EXPECT_EQ(0u, file->Size());
    ASSERT_EQ(6u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, builder->Geometry().rootSector), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    file = nullptr;
    Remount();
    ExpectHead("/notes.txt", 0);
    EXPECT_EQ(0u, builder->FatEntry(first));
}

TEST_F(FatTruncateTest, open_for_writing_an_empty_file_writes_nothing)
{
    builder->AddFile(0, "", "EMPTY   TXT", infra::ConstByteRange());
    RecordRequests();

    auto file = ExpectOpen("/empty.txt", fs::OpenMode::write);

    EXPECT_NE(nullptr, file);
    EXPECT_TRUE(requests.empty());
}

TEST_F(FatTruncateTest, failed_truncation_on_open_releases_the_handle)
{
    ExpectStat("/notes.txt", fs::Result::success, fs::EntryInfo{ "notes.txt", fs::EntryType::file, 1300 });
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectOpen("/notes.txt", fs::OpenMode::write, fs::Result::deviceError);

    auto first = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto second = ExpectOpen("/other.txt", fs::OpenMode::write);
    EXPECT_NE(nullptr, first);
    EXPECT_NE(nullptr, second);
}

TEST_F(FatTruncateTest, written_file_opened_for_writing_again_starts_empty)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::write);
    auto written = Pattern<300>(5);
    file->Write(infra::MakeRange(written), Done());
    ExpectDone(fs::Result::success);
    file->Close(Done());
    ExpectDone(fs::Result::success);
    file = nullptr;

    auto reopened = ExpectOpen("/notes.txt", fs::OpenMode::write);

    EXPECT_EQ(0u, reopened->Size());
    reopened = nullptr;
    Remount();
    ExpectHead("/notes.txt", 0);
}
