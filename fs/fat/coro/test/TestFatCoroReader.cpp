#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/coro/FatCoroReader.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <iostream>
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
    struct VolumeCase
    {
        const char* name;
        uint32_t blocks;
        uint8_t sectorsPerCluster;
        bool partitioned;
    };

    void PrintTo(const VolumeCase& volumeCase, std::ostream* stream)
    {
        *stream << volumeCase.name;
    }

    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern()
    {
        std::array<uint8_t, Size> pattern;

        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 31 + index / 509);

        return pattern;
    }

    class FatCoroReaderTest
        : public testing::TestWithParam<VolumeCase>
        , public infra::ClockFixture
    {
    public:
        FatCoroReaderTest()
            : device(GetParam().blocks)
        {
            fs::FatFormatConfig config;
            config.sectorsPerCluster = GetParam().sectorsPerCluster;
            config.partitioned = GetParam().partitioned;
            std::optional<fs::Result> formatted;
            formatter.Format(config, [&formatted](fs::Result result)
                {
                    formatted = result;
                });
            ExecuteAllActions();
            EXPECT_EQ(fs::Result::success, formatted);
            builder.emplace(device);
        }

        ~FatCoroReaderTest() override
        {
            std::cout << "frame peaks: volume " << volume.FramePeak() << std::endl;
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        void ExpectDone(fs::Result result)
        {
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
            testing::Mock::VerifyAndClearExpectations(&done);
        }

        void ExpectMount(fs::Result result = fs::Result::success)
        {
            volume.Mount(Done());
            ExpectDone(result);
        }

        void ExpectStat(infra::BoundedConstString path, fs::Result result, const fs::EntryInfo& info = {})
        {
            volume.Stat(path, [this](fs::Result result, const fs::EntryInfo& info)
                {
                    statDone.callback(result, info);
                });
            EXPECT_CALL(statDone, callback(result, info));
            ExecuteAllActions();
            testing::Mock::VerifyAndClearExpectations(&statDone);
        }

        infra::SharedPtr<fs::File> ExpectOpen(infra::BoundedConstString path, fs::Result result = fs::Result::success)
        {
            infra::SharedPtr<fs::File> opened;
            volume.Open(path, fs::OpenMode::read, [this, &opened](fs::Result result, infra::SharedPtr<fs::File> file)
                {
                    opened = file;
                    done.callback(result);
                });
            ExpectDone(result);
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
            ExpectDone(result);
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
            testing::Mock::VerifyAndClearExpectations(&readDone);
        }

        void ExpectNext(fs::Directory& directory, fs::Result result, const std::optional<fs::EntryInfo>& entry = std::nullopt)
        {
            directory.Next([this](fs::Result result, std::optional<fs::EntryInfo> entry)
                {
                    nextDone.callback(result, entry);
                });
            EXPECT_CALL(nextDone, callback(result, entry));
            ExecuteAllActions();
            testing::Mock::VerifyAndClearExpectations(&nextDone);
        }

        fs::SparseBlockDevice::WithStorage<192> device;
        fs::FatFormatter::WithStorage<8> formatter{ device };
        std::optional<fs::FatImageBuilder> builder;
        fs::FatCoroReader::WithStorage<2, 2> volume{ device };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
        testing::StrictMock<infra::MockCallback<void(fs::Result, const fs::EntryInfo&)>> statDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result, std::size_t)>> readDone;
        testing::StrictMock<infra::MockCallback<void(fs::Result, std::optional<fs::EntryInfo>)>> nextDone;
    };
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatCoroReaderTest,
    testing::Values(VolumeCase{ "fat12", 16384, 16, true }, VolumeCase{ "fat16", 16384, 1, true }, VolumeCase{ "fat16_without_partition_table", 16384, 1, false }, VolumeCase{ "fat32", 81920, 1, true }),
    [](const testing::TestParamInfo<VolumeCase>& info)
    {
        return std::string(info.param.name);
    });

TEST_P(FatCoroReaderTest, calls_before_mount_are_not_mounted)
{
    ExpectStat("/file.txt", fs::Result::notMounted);
    ExpectOpen("/file.txt", fs::Result::notMounted);
}

TEST_P(FatCoroReaderTest, stat_finds_an_entry_by_its_long_and_its_short_name)
{
    auto contents = Pattern<700>();
    builder->AddFile(0, "Meeting notes.txt", "MEETIN~1TXT", infra::MakeRange(contents));
    ExpectMount();

    ExpectStat("/Meeting notes.txt", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/meeting NOTES.TXT", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/MEETIN~1.TXT", fs::Result::success, fs::EntryInfo{ "Meeting notes.txt", fs::EntryType::file, 700 });
    ExpectStat("/missing.txt", fs::Result::notFound);
    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_P(FatCoroReaderTest, nested_directories_are_walked)
{
    auto contents = Pattern<100>();
    auto documents = builder->AddDirectory(0, "Documents", "DOCUME~1   ");
    auto reports = builder->AddDirectory(documents, "", "REPORTS    ");
    builder->AddFile(reports, "Quarterly report.pdf", "QUARTE~1PDF", infra::MakeRange(contents));
    ExpectMount();

    ExpectStat("/Documents/reports/Quarterly report.pdf", fs::Result::success, fs::EntryInfo{ "Quarterly report.pdf", fs::EntryType::file, 100 });
    ExpectStat("/DOCUME~1/REPORTS", fs::Result::success, fs::EntryInfo{ "REPORTS", fs::EntryType::directory, 0 });
    ExpectStat("/Documents/missing/Quarterly report.pdf", fs::Result::notFound);
    ExpectStat("/Documents/reports/Quarterly report.pdf/x", fs::Result::notADirectory);
}

TEST_P(FatCoroReaderTest, a_file_is_read_across_clusters_in_parts_and_whole_sectors)
{
    auto contents = Pattern<9000>();
    builder->AddFile(0, "", "DATA    BIN", infra::MakeRange(contents), true);
    ExpectMount();
    auto file = ExpectOpen("/DATA.BIN");
    ASSERT_NE(nullptr, file);
    EXPECT_EQ(9000u, file->Size());

    std::array<uint8_t, 9100> readBack{};
    ExpectRead(*file, infra::Head(infra::MakeRange(readBack), 100), fs::Result::success, 100);
    ExpectRead(*file, infra::DiscardHead(infra::MakeRange(readBack), 100), fs::Result::success, 8900);
    EXPECT_TRUE(std::equal(contents.begin(), contents.end(), readBack.begin()));

    file->Seek(4000);
    std::array<uint8_t, 600> middle{};
    ExpectRead(*file, infra::MakeRange(middle), fs::Result::success, 600);
    EXPECT_TRUE(std::equal(middle.begin(), middle.end(), contents.begin() + 4000));

    std::cout << "frame peaks: file " << static_cast<fs::FatCoroFile&>(*file).FramePeak() << std::endl;
}

TEST_P(FatCoroReaderTest, the_root_and_a_subdirectory_are_listed)
{
    auto contents = Pattern<100>();
    builder->AddFile(0, "First file with a long name.txt", "FIRSTF~1TXT", infra::MakeRange(contents));
    auto sub = builder->AddDirectory(0, "", "SUB        ");
    builder->AddFile(sub, "", "INNER   TXT", infra::Head(infra::MakeRange(contents), 10));
    ExpectMount();

    auto root = ExpectOpenDirectory("/");
    ASSERT_NE(nullptr, root);
    ExpectNext(*root, fs::Result::success, fs::EntryInfo{ "First file with a long name.txt", fs::EntryType::file, 100 });
    ExpectNext(*root, fs::Result::success, fs::EntryInfo{ "SUB", fs::EntryType::directory, 0 });
    ExpectNext(*root, fs::Result::success);

    auto inner = ExpectOpenDirectory("/SUB");
    ASSERT_NE(nullptr, inner);
    ExpectNext(*inner, fs::Result::success, fs::EntryInfo{ "INNER.TXT", fs::EntryType::file, 10 });
    ExpectNext(*inner, fs::Result::success);

    std::cout << "frame peaks: directory " << static_cast<fs::FatCoroDirectory&>(*root).FramePeak() << std::endl;
}

TEST_P(FatCoroReaderTest, a_second_path_call_while_one_runs_is_busy)
{
    builder->AddFile(0, "", "A       TXT", infra::ConstByteRange());
    ExpectMount();

    volume.Stat("/A.TXT", [this](fs::Result result, const fs::EntryInfo& info)
        {
            statDone.callback(result, info);
        });
    volume.Stat("/A.TXT", [this](fs::Result result, const fs::EntryInfo& info)
        {
            statDone.callback(result, info);
        });

    EXPECT_CALL(statDone, callback(fs::Result::busy, fs::EntryInfo{}));
    EXPECT_CALL(statDone, callback(fs::Result::success, fs::EntryInfo{ "A.TXT", fs::EntryType::file, 0 }));
    ExecuteAllActions();
}

TEST_P(FatCoroReaderTest, unmount_and_mount_again)
{
    builder->AddFile(0, "", "A       TXT", infra::ConstByteRange());
    ExpectMount();
    volume.Unmount(Done());
    ExpectDone(fs::Result::success);
    ExpectStat("/A.TXT", fs::Result::notMounted);
    ExpectMount();
    ExpectStat("/A.TXT", fs::Result::success, fs::EntryInfo{ "A.TXT", fs::EntryType::file, 0 });
}
