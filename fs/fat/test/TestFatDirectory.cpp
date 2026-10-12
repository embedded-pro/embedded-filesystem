#include "fs/fat/test/FatFileSystemFixture.hpp"
#include <array>
#include <cstdio>

namespace
{
    class FatDirectoryTest
        : public FatFileSystemFixture
    {
    public:
        void SetUp() override
        {
            ExpectMount();
        }

        void AddNumberedFiles(uint32_t directory, uint32_t count)
        {
            for (uint32_t index = 0; index != count; ++index)
            {
                std::array<char, 12> name;
                std::snprintf(name.data(), name.size(), "F%07uTXT", static_cast<unsigned>(index));
                char shortName[12];
                std::copy_n(name.begin(), 12, shortName);
                builder->AddFile(directory, "", shortName, infra::ConstByteRange());
            }
        }

        std::array<uint8_t, 3> data{ 1, 2, 3 };
        testing::StrictMock<infra::MockCallback<void(fs::Result, std::optional<fs::EntryInfo>)>> secondNext;
    };
}

TEST_F(FatDirectoryTest, lists_every_entry_and_then_the_end)
{
    builder->AddFile(0, "A long file name.txt", "ALONGF~1TXT", infra::MakeRange(data));
    builder->AddDirectory(0, "", "SUBDIR     ");
    builder->AddFile(0, "", "README  TXT", infra::ConstByteRange());
    auto directory = ExpectOpenDirectory("/");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "A long file name.txt", fs::EntryType::file, 3 });
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "SUBDIR", fs::EntryType::directory, 0 });
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "README.TXT", fs::EntryType::file, 0 });
    ExpectNext(*directory, fs::Result::success);
    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatDirectoryTest, never_lists_the_dot_entries)
{
    auto subdirectory = builder->AddDirectory(0, "Sub directory", "SUBDIR~1   ");
    builder->AddFile(subdirectory, "", "INNER   TXT", infra::MakeRange(data));
    auto directory = ExpectOpenDirectory("/sub directory");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "INNER.TXT", fs::EntryType::file, 3 });
    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatDirectoryTest, empty_directory_ends_at_once)
{
    builder->AddDirectory(0, "", "EMPTY      ");
    auto directory = ExpectOpenDirectory("/empty");

    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatDirectoryTest, lists_a_directory_across_its_clusters)
{
    auto subdirectory = builder->AddDirectory(0, "", "SUBDIR     ");
    builder->AddDirectory(0, "", "OTHER      ");
    AddNumberedFiles(subdirectory, 40);
    auto directory = ExpectOpenDirectory("/subdir");

    for (uint32_t index = 0; index != 40; ++index)
    {
        std::array<char, 13> name;
        std::snprintf(name.data(), name.size(), "F%07u.TXT", static_cast<unsigned>(index));
        ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ infra::BoundedConstString(name.data(), 12), fs::EntryType::file, 0 });
    }

    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatDirectoryTest, opening_a_file_is_not_a_directory)
{
    builder->AddFile(0, "", "README  TXT", infra::ConstByteRange());

    ExpectOpenDirectory("/readme.txt", fs::Result::notADirectory);
}

TEST_F(FatDirectoryTest, opening_a_missing_directory_is_not_found)
{
    ExpectOpenDirectory("/missing", fs::Result::notFound);
    ExpectOpenDirectory("missing", fs::Result::invalidPath);
}

TEST_F(FatDirectoryTest, directory_pointing_outside_the_volume_is_corrupted)
{
    fs::ShortEntry entry;
    entry.name.bytes = { 'B', 'R', 'O', 'K', 'E', 'N', ' ', ' ', ' ', ' ', ' ' };
    entry.attributes = fs::FatAttributes::directory;
    entry.firstCluster = 1;
    builder->AddEntries(0, "", entry);

    ExpectOpenDirectory("/broken", fs::Result::corrupted);
}

TEST_F(FatDirectoryTest, opening_beyond_the_pool_is_too_many_open_files)
{
    auto directory = ExpectOpenDirectory("/");

    ExpectOpenDirectory("/", fs::Result::tooManyOpenFiles);
}

TEST_F(FatDirectoryTest, open_beyond_the_path_slots_is_busy)
{
    volume.OpenDirectory("/", [this](fs::Result result, infra::SharedPtr<fs::Directory>)
        {
            openDone.callback(result);
        });
    volume.OpenDirectory("/", [this](fs::Result result, infra::SharedPtr<fs::Directory>)
        {
            openDone.callback(result);
        });
    volume.OpenDirectory("/", [this](fs::Result result, infra::SharedPtr<fs::Directory>)
        {
            openDone.callback(result);
        });

    EXPECT_CALL(openDone, callback(fs::Result::busy));
    EXPECT_CALL(openDone, callback(fs::Result::success)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatDirectoryTest, released_directory_returns_to_the_pool)
{
    auto directory = ExpectOpenDirectory("/");
    directory = nullptr;

    directory = ExpectOpenDirectory("/");
}

TEST_F(FatDirectoryTest, reopened_directory_starts_from_its_first_entry)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    auto directory = ExpectOpenDirectory("/");
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "FIRST", fs::EntryType::file, 0 });
    directory = nullptr;

    directory = ExpectOpenDirectory("/");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "FIRST", fs::EntryType::file, 0 });
}

TEST_F(FatDirectoryTest, next_while_a_next_is_pending_is_busy)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    auto directory = ExpectOpenDirectory("/");

    StartNext(*directory);
    StartNext(*directory);

    testing::InSequence sequence;
    EXPECT_CALL(nextDone, callback(fs::Result::busy, std::optional<fs::EntryInfo>()));
    EXPECT_CALL(nextDone, callback(fs::Result::success, std::optional<fs::EntryInfo>(fs::EntryInfo{ "FIRST", fs::EntryType::file, 0 })));
    ExecuteAllActions();
}

TEST_F(FatDirectoryTest, next_may_be_called_from_its_own_completion)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    builder->AddFile(0, "", "SECOND     ", infra::ConstByteRange());
    auto directory = ExpectOpenDirectory("/");

    directory->Next([this, &directory](fs::Result result, std::optional<fs::EntryInfo> entry)
        {
            nextDone.callback(result, entry);
            directory->Next([this](fs::Result result, std::optional<fs::EntryInfo> entry)
                {
                    secondNext.callback(result, entry);
                });
        });

    EXPECT_CALL(nextDone, callback(fs::Result::success, std::optional<fs::EntryInfo>(fs::EntryInfo{ "FIRST", fs::EntryType::file, 0 })));
    EXPECT_CALL(secondNext, callback(fs::Result::success, std::optional<fs::EntryInfo>(fs::EntryInfo{ "SECOND", fs::EntryType::file, 0 })));
    ExecuteAllActions();
}

TEST_F(FatDirectoryTest, directory_released_during_next_returns_to_the_pool_after_it)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    builder->AddFile(builder->AddDirectory(0, "", "SUBDIR     "), "", "INNER      ", infra::ConstByteRange());
    auto directory = ExpectOpenDirectory("/");
    infra::SharedPtr<fs::Directory> other;
    volume.OpenDirectory("/subdir", [this, &other](fs::Result result, infra::SharedPtr<fs::Directory> opened)
        {
            other = opened;
            openDone.callback(result);
        });

    StartNext(*directory);
    directory = nullptr;
    EXPECT_CALL(openDone, callback(fs::Result::tooManyOpenFiles));
    EXPECT_CALL(nextDone, callback(fs::Result::success, std::optional<fs::EntryInfo>(fs::EntryInfo{ "FIRST", fs::EntryType::file, 0 })));
    ExecuteAllActions();

    directory = ExpectOpenDirectory("/subdir");
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "INNER", fs::EntryType::file, 0 });
}

TEST_F(FatDirectoryTest, device_error_is_reported_and_the_listing_continues)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    auto directory = ExpectOpenDirectory("/");
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectNext(*directory, fs::Result::deviceError);
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "FIRST", fs::EntryType::file, 0 });
}

TEST_F(FatDirectoryTest, next_after_unmount_is_not_mounted_even_after_a_new_mount)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    auto directory = ExpectOpenDirectory("/");
    ExpectUnmount();

    ExpectNext(*directory, fs::Result::notMounted);

    ExpectMount();
    ExpectNext(*directory, fs::Result::notMounted);

    directory = nullptr;
    directory = ExpectOpenDirectory("/");
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "FIRST", fs::EntryType::file, 0 });
}

TEST_F(FatDirectoryTest, next_queued_before_unmount_completes_normally)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    auto directory = ExpectOpenDirectory("/");

    StartNext(*directory);
    volume.Unmount([this](fs::Result result)
        {
            lifecycleDone.callback(result);
        });

    testing::InSequence sequence;
    EXPECT_CALL(nextDone, callback(fs::Result::success, std::optional<fs::EntryInfo>(fs::EntryInfo{ "FIRST", fs::EntryType::file, 0 })));
    EXPECT_CALL(lifecycleDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(FatDirectoryTest, next_queued_after_unmount_is_not_mounted)
{
    auto directory = ExpectOpenDirectory("/");

    volume.Unmount([this](fs::Result result)
        {
            lifecycleDone.callback(result);
        });
    StartNext(*directory);

    EXPECT_CALL(lifecycleDone, callback(fs::Result::success));
    EXPECT_CALL(nextDone, callback(fs::Result::notMounted, std::optional<fs::EntryInfo>()));
    ExecuteAllActions();
}

TEST_F(FatDirectoryTest, removed_card_faults_the_volume_and_its_directories)
{
    auto directory = ExpectOpenDirectory("/");
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    ExpectNext(*directory, fs::Result::mediaRemoved);
    ExpectNext(*directory, fs::Result::mediaRemoved);
    ExpectStat("/", fs::Result::mediaRemoved);

    ExpectMount();
    ExpectNext(*directory, fs::Result::mediaRemoved);
}

TEST_F(FatDirectoryTest, next_waiting_behind_a_removal_reports_the_removal)
{
    auto directory = ExpectOpenDirectory("/");
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    StartStat("/first");
    StartNext(*directory);

    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{}));
    EXPECT_CALL(nextDone, callback(fs::Result::mediaRemoved, std::optional<fs::EntryInfo>()));
    ExecuteAllActions();
}

TEST_F(FatDirectoryTest, open_waiting_behind_a_removal_reports_the_removal)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    StartStat("/first");
    volume.OpenDirectory("/", [this](fs::Result result, infra::SharedPtr<fs::Directory> directory)
        {
            EXPECT_EQ(nullptr, directory);
            openDone.callback(result);
        });

    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{}));
    EXPECT_CALL(openDone, callback(fs::Result::mediaRemoved));
    ExecuteAllActions();
}

TEST_F(FatDirectoryTest, open_directory_before_mount_is_not_mounted)
{
    ExpectUnmount();

    ExpectOpenDirectory("/", fs::Result::notMounted);
}

TEST_F(FatDirectoryTest, directory_with_a_pending_next_is_destroyed_with_its_volume)
{
    auto directory = ExpectOpenDirectory("/");

    directory->Next([](fs::Result, std::optional<fs::EntryInfo>) {});
    directory = nullptr;
}
