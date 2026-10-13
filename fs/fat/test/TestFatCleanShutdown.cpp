#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include <array>
#include <utility>

namespace
{
    using Request = fs::SparseBlockDevice::Request;

    class FatCleanShutdownTest
        : public FatFileSystemFixture
    {
    public:
        void SetUp() override
        {
            builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(notes));
            builder->AddDirectory(0, "", "DOCS       ");
            ExpectMount();
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        std::optional<bool> Clean() const
        {
            return fs::CleanShutdown(builder->Geometry().type, builder->FatEntry(1));
        }

        void RecordRequests()
        {
            device.Observe([this](Request request, uint32_t block)
                {
                    if (request != Request::read && !requests.full())
                        requests.emplace_back(request, block);
                });
        }

        void WriteProtect()
        {
            device.Observe([this](Request request, uint32_t)
                {
                    if (request == Request::write)
                        device.FailNextOperationWith(hal::BlockDevice::Result::writeProtected);
                });
        }

        void ExpectCleanBitClearedFirst() const
        {
            const auto& geometry = builder->Geometry();
            ASSERT_LE(3u, requests.size());
            EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector), requests[0]);
            EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector + geometry.sectorsPerFat), requests[1]);
            EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[2]);
            EXPECT_EQ(false, Clean());
        }

        std::array<uint8_t, 700> notes{};
        infra::BoundedVector<std::pair<Request, uint32_t>>::WithMaxSize<32> requests;
    };
}

TEST_F(FatCleanShutdownTest, volume_that_is_only_read_stays_clean_and_is_not_written)
{
    auto writes = device.WriteRequests();
    auto file = ExpectOpen("/notes.txt");
    std::array<uint8_t, 700> read{};
    ExpectRead(*file, infra::MakeRange(read), fs::Result::success, 700);
    file = nullptr;

    ExpectUnmount();

    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(true, Clean());
}

TEST_F(FatCleanShutdownTest, write_clears_the_clean_bit_before_anything_else_is_written)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    RecordRequests();
    std::array<uint8_t, 600> data{};

    file->Write(infra::MakeRange(data), Done());
    ExpectDone(fs::Result::success);
    file->Sync(Done());
    ExpectDone(fs::Result::success);

    ExpectCleanBitClearedFirst();
}

TEST_F(FatCleanShutdownTest, created_file_clears_the_clean_bit_before_its_entry_is_written)
{
    RecordRequests();

    auto file = ExpectOpen("/new.txt", fs::OpenMode::write);
    file->Sync(Done());
    ExpectDone(fs::Result::success);

    ExpectCleanBitClearedFirst();
}

TEST_F(FatCleanShutdownTest, new_directory_clears_the_clean_bit_first)
{
    RecordRequests();

    volume.MakeDirectory("/new", Done());
    ExpectDone(fs::Result::success);

    ExpectCleanBitClearedFirst();
}

TEST_F(FatCleanShutdownTest, removal_clears_the_clean_bit_first)
{
    RecordRequests();

    volume.Remove("/notes.txt", Done());
    ExpectDone(fs::Result::success);

    ExpectCleanBitClearedFirst();
}

TEST_F(FatCleanShutdownTest, rename_clears_the_clean_bit_first)
{
    RecordRequests();

    volume.Rename("/notes.txt", "/docs/notes.txt", Done());
    ExpectDone(fs::Result::success);

    ExpectCleanBitClearedFirst();
}

TEST_F(FatCleanShutdownTest, truncation_clears_the_clean_bit_first)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    RecordRequests();

    file->Seek(100);
    file->Truncate(Done());
    ExpectDone(fs::Result::success);

    ExpectCleanBitClearedFirst();
}

TEST_F(FatCleanShutdownTest, sync_without_a_change_leaves_the_volume_clean)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    auto writes = device.WriteRequests();

    file->Sync(Done());
    ExpectDone(fs::Result::success);

    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(true, Clean());
}

TEST_F(FatCleanShutdownTest, clean_unmount_marks_the_volume_clean_again)
{
    volume.MakeDirectory("/new", Done());
    ExpectDone(fs::Result::success);
    ASSERT_EQ(false, Clean());

    ExpectUnmount();

    EXPECT_EQ(true, Clean());
}

TEST_F(FatCleanShutdownTest, clean_bit_is_written_after_everything_else_is_flushed)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    std::array<uint8_t, 600> data{};
    file->Write(infra::MakeRange(data), Done());
    ExpectDone(fs::Result::success);
    RecordRequests();

    ExpectUnmount();

    const auto& geometry = builder->Geometry();
    ASSERT_LE(4u, requests.size());
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[requests.size() - 4]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector), requests[requests.size() - 3]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector + geometry.sectorsPerFat), requests[requests.size() - 2]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests.back());
}

TEST_F(FatCleanShutdownTest, volume_that_was_not_clean_at_mount_stays_not_clean)
{
    ExpectUnmount();
    MarkVolumeDirty();
    ExpectMount();
    volume.MakeDirectory("/new", Done());
    ExpectDone(fs::Result::success);

    ExpectUnmount();

    EXPECT_EQ(false, Clean());
}

TEST_F(FatCleanShutdownTest, removed_card_leaves_the_volume_not_clean)
{
    volume.MakeDirectory("/new", Done());
    ExpectDone(fs::Result::success);
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);
    ExpectStat("/docs/missing.txt", fs::Result::mediaRemoved);

    ExpectMount();
    ExpectUnmount();

    EXPECT_EQ(false, Clean());
}

TEST_F(FatCleanShutdownTest, empty_write_leaves_the_volume_clean)
{
    auto file = ExpectOpen("/notes.txt", fs::OpenMode::append);
    auto writes = device.WriteRequests();

    file->Write(infra::ConstByteRange(), Done());
    ExpectDone(fs::Result::success);

    EXPECT_EQ(writes, device.WriteRequests());
    EXPECT_EQ(true, Clean());
}

TEST_F(FatCleanShutdownTest, write_protected_card_can_be_read_while_changes_complete_with_read_only)
{
    WriteProtect();

    ExpectOpen("/new.txt", fs::OpenMode::write, fs::Result::readOnly);
    volume.MakeDirectory("/new", Done());
    ExpectDone(fs::Result::readOnly);
    volume.Remove("/notes.txt", Done());
    ExpectDone(fs::Result::readOnly);
    auto changed = ExpectOpen("/notes.txt", fs::OpenMode::readWrite);
    std::array<uint8_t, 10> data{};
    changed->Write(infra::MakeRange(data), Done());
    ExpectDone(fs::Result::readOnly);
    changed = nullptr;
    ExecuteAllActions();

    auto file = ExpectOpen("/notes.txt");
    std::array<uint8_t, 700> read{};
    ExpectRead(*file, infra::MakeRange(read), fs::Result::success, 700);
    EXPECT_EQ(true, Clean());
}
