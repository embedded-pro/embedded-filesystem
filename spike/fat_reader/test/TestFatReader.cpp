#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "spike/fat_reader/FatReader.hpp"
#include "gmock/gmock.h"
#include <array>
#include <optional>
#include <ostream>
#include <string_view>

namespace fs
{
    inline void PrintTo(const EntryInfo& info, std::ostream* stream)
    {
        *stream << R"({ ")" << std::string_view(info.name.data(), info.name.size()) << R"(", )" << (info.type == EntryType::directory ? "directory" : "file") << ", " << info.size << " }";
    }
}

namespace
{
    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern()
    {
        std::array<uint8_t, Size> pattern;

        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 31 + index / 509);

        return pattern;
    }

    struct Volume
    {
        uint32_t blocks;
        uint8_t clusterSectors;
    };

    class FatReaderTest
        : public testing::TestWithParam<Volume>
        , public infra::ClockFixture
    {
    public:
        FatReaderTest()
            : device(GetParam().blocks)
        {
            fs::FatFormatConfig config;
            config.sectorsPerCluster = GetParam().clusterSectors;
            std::optional<fs::Result> formatted;
            formatter.Format(config, [&formatted](fs::Result result)
                {
                    formatted = result;
                });
            ExecuteAllActions();
            EXPECT_EQ(fs::Result::success, formatted);
            builder.emplace(device);
        }

        void ExpectMount(fs::Result result = fs::Result::success)
        {
            volume.Mount([this](fs::Result result)
                {
                    lifecycleDone.callback(result);
                });
            EXPECT_CALL(lifecycleDone, callback(result));
            ExecuteAllActions();
        }

        void ExpectUnmount(fs::Result result = fs::Result::success)
        {
            volume.Unmount([this](fs::Result result)
                {
                    lifecycleDone.callback(result);
                });
            EXPECT_CALL(lifecycleDone, callback(result));
            ExecuteAllActions();
        }

        void ExpectStat(infra::BoundedConstString path, fs::Result result, const fs::EntryInfo& info = {})
        {
            volume.Stat(path, [this](fs::Result result, const fs::EntryInfo& info)
                {
                    statDone.callback(result, info);
                });
            EXPECT_CALL(statDone, callback(result, info));
            ExecuteAllActions();
        }

        infra::SharedPtr<fs::File> ExpectOpen(infra::BoundedConstString path, fs::Result result = fs::Result::success, fs::OpenMode mode = fs::OpenMode::read)
        {
            infra::SharedPtr<fs::File> opened;
            volume.Open(path, mode, [this, &opened](fs::Result result, infra::SharedPtr<fs::File> file)
                {
                    opened = file;
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
            EXPECT_EQ(result == fs::Result::success, opened != nullptr);
            return opened;
        }

        infra::SharedPtr<fs::Directory> ExpectOpenDirectory(infra::BoundedConstString path, fs::Result result = fs::Result::success)
        {
            infra::SharedPtr<fs::Directory> opened;
            volume.OpenDirectory(path, [this, &opened](fs::Result result, infra::SharedPtr<fs::Directory> directory)
                {
                    opened = directory;
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
            EXPECT_EQ(result == fs::Result::success, opened != nullptr);
            return opened;
        }

        void ExpectRead(fs::File& file, infra::ByteRange buffer, fs::Result result, std::size_t bytesRead)
        {
            file.Read(buffer, [this](fs::Result result, std::size_t bytesRead)
                {
                    readDone.callback(result, bytesRead);
                });
            EXPECT_CALL(readDone, callback(result, bytesRead));
            ExecuteAllActions();
        }

        void ExpectNext(fs::Directory& directory, fs::Result result, const std::optional<fs::EntryInfo>& entry = std::nullopt)
        {
            directory.Next([this](fs::Result result, std::optional<fs::EntryInfo> entry)
                {
                    nextDone.callback(result, entry);
                });
            EXPECT_CALL(nextDone, callback(result, entry));
            ExecuteAllActions();
        }

        void ExpectDone(fs::Result result)
        {
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        fs::SparseBlockDevice::WithStorage<192> device;
        fs::FatFormatter::WithStorage<8> formatter{ device };
        std::optional<fs::FatImageBuilder> builder;
        spike::FatReader::WithStorage<2, 1> volume{ device };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> lifecycleDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result, const fs::EntryInfo&)>> statDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result, std::optional<fs::EntryInfo>)>> nextDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result, std::size_t)>> readDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
        std::array<uint8_t, 8192> buffer{};
    };
}

INSTANTIATE_TEST_SUITE_P(Fat12, FatReaderTest, testing::Values(Volume{ 16384, 16 }));
INSTANTIATE_TEST_SUITE_P(Fat16, FatReaderTest, testing::Values(Volume{ 16384, 1 }));
INSTANTIATE_TEST_SUITE_P(Fat32, FatReaderTest, testing::Values(Volume{ 81920, 1 }));

TEST_P(FatReaderTest, calls_before_mount_are_not_mounted)
{
    ExpectStat("/", fs::Result::notMounted);
    ExpectOpen("/notes.txt", fs::Result::notMounted);
    ExpectOpenDirectory("/", fs::Result::notMounted);
    ExpectUnmount(fs::Result::notMounted);
}

TEST_P(FatReaderTest, mount_completes_from_the_event_dispatcher)
{
    volume.Mount([this](fs::Result result)
        {
            lifecycleDone.callback(result);
        });

    EXPECT_CALL(lifecycleDone, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_P(FatReaderTest, stat_of_the_root_is_a_directory_with_an_empty_name)
{
    ExpectMount();

    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_P(FatReaderTest, stat_finds_an_entry_by_its_long_and_its_short_name)
{
    auto contents = Pattern<700>();
    builder->AddFile(0, "Meeting notes.txt", "MEETIN~1TXT", infra::MakeRange(contents));
    ExpectMount();

    ExpectStat("/Meeting notes.txt", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/meeting NOTES.TXT", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/MEETIN~1.TXT", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/missing.txt", fs::Result::notFound);
}

TEST_P(FatReaderTest, a_file_is_read_across_clusters)
{
    auto contents = Pattern<5000>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents), true);
    ExpectMount();
    auto file = ExpectOpen("/data.bin");

    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 3000), fs::Result::success, 3000);
    ExpectRead(*file, infra::DiscardHead(infra::MakeRange(buffer), 3000), fs::Result::success, 2000);
    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::success, 0);

    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), buffer.begin()));
    EXPECT_EQ(5000, file->Position());
}

TEST_P(FatReaderTest, seek_moves_the_position_of_the_next_read)
{
    auto contents = Pattern<5000>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents), true);
    ExpectMount();
    auto file = ExpectOpen("/data.bin");

    file->Seek(4321);
    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 10), fs::Result::success, 10);
    EXPECT_TRUE(std::equal(contents.begin() + 4321, contents.begin() + 4331, buffer.begin()));

    file->Seek(17);
    ExpectRead(*file, infra::Head(infra::MakeRange(buffer), 600), fs::Result::success, 600);
    EXPECT_TRUE(std::equal(contents.begin() + 17, contents.begin() + 617, buffer.begin()));

    file->Seek(6000);
    EXPECT_EQ(5000, file->Position());
}

TEST_P(FatReaderTest, nested_directories_are_walked)
{
    auto contents = Pattern<100>();
    auto documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
    auto reports = builder->AddDirectory(documents, "", "REPORTS    ");
    builder->AddFile(reports, "Quarterly report.pdf", "QUARTE~1PDF", infra::MakeRange(contents));
    ExpectMount();

    ExpectStat("/Documents/reports/Quarterly report.pdf", fs::Result::success, fs::EntryInfo{ "Quarterly report.pdf", fs::EntryType::file, 100 });
    ExpectStat("/DOCUME~1/REPORTS", fs::Result::success, fs::EntryInfo{ "REPORTS", fs::EntryType::directory, 0 });
    ExpectStat("/Documents/reports/Quarterly report.pdf/x", fs::Result::notADirectory);
}

TEST_P(FatReaderTest, a_directory_lists_its_entries)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "First file with a long name.txt", "FIRSTF~1TXT", infra::MakeRange(contents));
    builder->AddDirectory(0, "", "SUB        ");
    builder->AddFile(0, "", "SHORT   TXT", infra::Head(infra::MakeRange(contents), 10));
    ExpectMount();
    auto directory = ExpectOpenDirectory("/");

    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "First file with a long name.txt", fs::EntryType::file, 100 });
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "SUB", fs::EntryType::directory, 0 });
    ExpectNext(*directory, fs::Result::success, fs::EntryInfo{ "SHORT.TXT", fs::EntryType::file, 10 });
    ExpectNext(*directory, fs::Result::success);
    ExpectNext(*directory, fs::Result::success);
}

TEST_P(FatReaderTest, kinds_of_entries_are_checked_when_opened)
{
    builder->AddDirectory(0, "", "SUB        ");
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();

    ExpectOpen("/sub", fs::Result::isADirectory);
    ExpectOpenDirectory("/file.txt", fs::Result::notADirectory);
    ExpectOpen("/missing", fs::Result::notFound);
    ExpectOpen("/sub/missing", fs::Result::notFound);
}

TEST_P(FatReaderTest, invalid_paths_are_rejected)
{
    ExpectMount();

    ExpectStat("relative", fs::Result::invalidPath);
    ExpectStat("/a/", fs::Result::invalidPath);
    ExpectStat("/a*b", fs::Result::invalidPath);
}

TEST_P(FatReaderTest, writing_is_read_only)
{
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();

    ExpectOpen("/file.txt", fs::Result::readOnly, fs::OpenMode::write);
    volume.Remove("/file.txt", Done());
    ExpectDone(fs::Result::readOnly);
    volume.MakeDirectory("/new", Done());
    ExpectDone(fs::Result::readOnly);
    volume.Rename("/file.txt", "/other.txt", Done());
    ExpectDone(fs::Result::readOnly);

    auto file = ExpectOpen("/file.txt");
    file->Write(infra::Head(infra::MakeRange(buffer), 1), Done());
    ExpectDone(fs::Result::readOnly);
    file->Close(Done());
    ExpectDone(fs::Result::success);
}

TEST_P(FatReaderTest, files_beyond_the_pool_are_too_many)
{
    builder->AddFile(0, "", "FILE    TXT", infra::ConstByteRange());
    ExpectMount();
    auto first = ExpectOpen("/file.txt");
    auto second = ExpectOpen("/file.txt");

    ExpectOpen("/file.txt", fs::Result::tooManyOpenFiles);
}

TEST_P(FatReaderTest, handles_are_not_mounted_after_unmount_and_a_new_mount_works)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/file.txt");
    ExpectUnmount();

    ExpectRead(*file, infra::MakeRange(buffer), fs::Result::notMounted, 0);
    ExpectStat("/file.txt", fs::Result::notMounted);

    ExpectMount();
    ExpectStat("/file.txt", fs::Result::success, fs::EntryInfo{ "FILE.TXT", fs::EntryType::file, 100 });
}

TEST_P(FatReaderTest, a_second_read_on_a_handle_is_busy)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "", "FILE    TXT", infra::MakeRange(contents));
    ExpectMount();
    auto file = ExpectOpen("/file.txt");

    file->Read(infra::MakeRange(buffer), [this](fs::Result result, std::size_t bytesRead)
        {
            readDone.callback(result, bytesRead);
        });
    file->Read(infra::MakeRange(buffer), [this](fs::Result result, std::size_t bytesRead)
        {
            readDone.callback(result, bytesRead);
        });

    EXPECT_CALL(readDone, callback(fs::Result::busy, 0));
    EXPECT_CALL(readDone, callback(fs::Result::success, 100));
    ExecuteAllActions();
}

TEST_P(FatReaderTest, an_unformatted_device_is_corrupted)
{
    std::array<uint8_t, 512> zeros{};
    device.WriteBlock(0, infra::MakeRange(zeros));

    ExpectMount(fs::Result::corrupted);
    ExpectStat("/", fs::Result::notMounted);
}
