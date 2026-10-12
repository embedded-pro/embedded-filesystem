#include "fs/fat/test/FatFileSystemFixture.hpp"
#include <array>

namespace
{
    class FatOpenTest
        : public FatFileSystemFixture
    {
    public:
        void SetUp() override
        {
            ExpectMount();
            builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::MakeRange(contents));
            builder->AddDirectory(0, "Documents", "DOCUME~1   ");
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        std::array<uint8_t, 700> contents{ 1, 2, 3 };
        std::array<uint8_t, 16> buffer{};
    };
}

TEST_F(FatOpenTest, opens_a_file_for_reading_at_its_start)
{
    auto file = ExpectOpen("/NOTES.TXT");

    EXPECT_EQ(700u, file->Size());
    EXPECT_EQ(0u, file->Position());
}

TEST_F(FatOpenTest, opening_a_directory_is_a_directory)
{
    ExpectOpen("/documents", fs::OpenMode::read, fs::Result::isADirectory);
    ExpectOpen("/", fs::OpenMode::read, fs::Result::isADirectory);
}

TEST_F(FatOpenTest, opening_a_missing_file_is_not_found)
{
    ExpectOpen("/missing.txt", fs::OpenMode::read, fs::Result::notFound);
    ExpectOpen("/missing/notes.txt", fs::OpenMode::read, fs::Result::notFound);
    ExpectOpen("/notes.txt/inner", fs::OpenMode::read, fs::Result::notADirectory);
    ExpectOpen("notes.txt", fs::OpenMode::read, fs::Result::invalidPath);
}

TEST_F(FatOpenTest, modes_that_write_are_not_supported_yet)
{
    ExpectOpen("/notes.txt", fs::OpenMode::write, fs::Result::unsupported);
    ExpectOpen("/notes.txt", fs::OpenMode::append, fs::Result::unsupported);
    ExpectOpen("/missing.txt", fs::OpenMode::readWrite, fs::Result::unsupported);
    ExpectOpen("/documents", fs::OpenMode::write, fs::Result::isADirectory);
}

TEST_F(FatOpenTest, file_whose_chain_starts_outside_the_volume_is_corrupted)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'B', 'R', 'O', 'K', 'E', 'N', ' ', ' ', ' ', ' ', ' ' };
    entry.firstCluster = builder->Geometry().clusterCount + 2;
    entry.size = 10;
    builder->AddEntries(0, "", entry);

    ExpectOpen("/broken", fs::OpenMode::read, fs::Result::corrupted);
}

TEST_F(FatOpenTest, file_may_be_open_for_reading_more_than_once)
{
    auto first = ExpectOpen("/notes.txt");
    auto second = ExpectOpen("/notes.txt");

    EXPECT_NE(first, second);
}

TEST_F(FatOpenTest, opening_beyond_the_pool_is_too_many_open_files)
{
    auto first = ExpectOpen("/notes.txt");
    auto second = ExpectOpen("/notes.txt");

    ExpectOpen("/notes.txt", fs::OpenMode::read, fs::Result::tooManyOpenFiles);
}

TEST_F(FatOpenTest, released_file_returns_to_the_pool_without_close)
{
    auto first = ExpectOpen("/notes.txt");
    auto second = ExpectOpen("/notes.txt");
    first = nullptr;

    first = ExpectOpen("/notes.txt");
}

TEST_F(FatOpenTest, closed_file_returns_to_the_pool_when_it_is_released)
{
    auto first = ExpectOpen("/notes.txt");
    auto second = ExpectOpen("/notes.txt");
    first->Close(Done());
    ExpectDone(fs::Result::success);
    first = nullptr;

    first = ExpectOpen("/notes.txt");
}

TEST_F(FatOpenTest, file_released_during_a_read_returns_to_the_pool_after_it)
{
    builder->AddFile(0, "", "OTHER   TXT", infra::MakeRange(buffer));
    auto first = ExpectOpen("/notes.txt");
    auto second = ExpectOpen("/notes.txt");
    infra::SharedPtr<fs::File> other;
    volume.Open("/other.txt", fs::OpenMode::read, [this, &other](fs::Result result, infra::SharedPtr<fs::File> file)
        {
            other = file;
            openDone.callback(result);
        });

    StartRead(*first, infra::MakeRange(contents));
    first = nullptr;
    EXPECT_CALL(openDone, callback(fs::Result::tooManyOpenFiles));
    EXPECT_CALL(readDone, callback(fs::Result::success, 700));
    ExecuteAllActions();

    EXPECT_EQ(16u, ExpectOpen("/other.txt")->Size());
}

TEST_F(FatOpenTest, file_opened_for_reading_cannot_be_written)
{
    auto file = ExpectOpen("/notes.txt");

    file->Write(infra::MakeRange(buffer), Done());
    file->Truncate(Done());
    file->ExpectSize(1000, Done());
    EXPECT_CALL(done, callback(fs::Result::readOnly)).Times(3);
    ExecuteAllActions();

    EXPECT_EQ(700u, file->Size());
}

TEST_F(FatOpenTest, sync_and_close_of_a_file_opened_for_reading_succeed)
{
    auto file = ExpectOpen("/notes.txt");

    file->Sync(Done());
    ExpectDone(fs::Result::success);
    file->Close(Done());
    ExpectDone(fs::Result::success);
}

TEST_F(FatOpenTest, calls_while_a_read_is_pending_are_busy)
{
    auto file = ExpectOpen("/notes.txt");

    StartRead(*file, infra::MakeRange(buffer));
    StartRead(*file, infra::MakeRange(buffer));
    file->Write(infra::MakeRange(buffer), Done());
    file->Sync(Done());
    file->Close(Done());

    EXPECT_CALL(readDone, callback(fs::Result::busy, 0));
    EXPECT_CALL(done, callback(fs::Result::busy)).Times(3);
    EXPECT_CALL(readDone, callback(fs::Result::success, 16));
    ExecuteAllActions();
}

TEST_F(FatOpenTest, calls_on_a_file_after_unmount_are_not_mounted)
{
    auto file = ExpectOpen("/notes.txt");
    ExpectUnmount();
    ExpectMount();

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::notMounted, 0);
    file->Sync(Done());
    ExpectDone(fs::Result::notMounted);
    file->Close(Done());
    ExpectDone(fs::Result::notMounted);
}

TEST_F(FatOpenTest, removed_card_faults_the_volume_and_its_files)
{
    auto file = ExpectOpen("/notes.txt");
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::mediaRemoved, 0);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::mediaRemoved, 0);
    ExpectStat("/", fs::Result::mediaRemoved);
}

TEST_F(FatOpenTest, open_beyond_the_path_slots_is_busy)
{
    for (int call = 0; call != 3; ++call)
        volume.Open("/notes.txt", fs::OpenMode::read, [this](fs::Result result, infra::SharedPtr<fs::File>)
            {
                openDone.callback(result);
            });

    EXPECT_CALL(openDone, callback(fs::Result::busy));
    EXPECT_CALL(openDone, callback(fs::Result::success)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatOpenTest, open_waiting_behind_a_removal_reports_the_removal)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    StartStat("/first");
    volume.Open("/notes.txt", fs::OpenMode::read, [this](fs::Result result, infra::SharedPtr<fs::File> file)
        {
            EXPECT_EQ(nullptr, file);
            openDone.callback(result);
        });

    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{}));
    EXPECT_CALL(openDone, callback(fs::Result::mediaRemoved));
    ExecuteAllActions();
}

TEST_F(FatOpenTest, open_before_mount_is_not_mounted)
{
    ExpectUnmount();

    ExpectOpen("/notes.txt", fs::OpenMode::read, fs::Result::notMounted);
}
