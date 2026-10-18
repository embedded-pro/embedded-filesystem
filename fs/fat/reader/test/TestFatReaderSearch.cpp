#include "fs/fat/reader/FatReaderSearch.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <optional>

namespace
{
    fs::FatFormatConfig SmallRoot()
    {
        fs::FatFormatConfig config;
        config.sectorsPerCluster = 1;
        config.rootEntries = 16;
        return config;
    }

    class FatReaderSearchTest
        : public FatVolumeFixture
    {
    public:
        explicit FatReaderSearchTest(const fs::FatFormatConfig& config = ClusterSize(1))
            : FatVolumeFixture(16384, config)
        {}

        void ExpectFind(infra::BoundedConstString path, fs::Result result = fs::Result::success)
        {
            EXPECT_CALL(done, callback(result));
            search.Find(path, [this](fs::Result result)
                {
                    done.callback(result);
                });
            ExecuteAllActions();
            testing::Mock::VerifyAndClearExpectations(&done);
        }

        void AddFiles(uint32_t directory, uint32_t count)
        {
            for (uint32_t index = 0; index != count; ++index)
            {
                char name[12] = "F0000000TXT";
                name[6] = static_cast<char>('0' + index / 10);
                name[7] = static_cast<char>('0' + index % 10);
                builder->AddFile(directory, "", name, infra::ConstByteRange());
            }
        }

        void Delete(fs::FatEntryPosition position)
        {
            std::array<uint8_t, fs::directoryEntryBytes> raw{};
            raw[0] = 0xe5;
            builder->WriteRawEntry(position, infra::MakeRange(raw));
        }

        std::array<uint8_t, 255> nameBuffer{};
        fs::FatEntryAssembler assembler{ infra::MakeRange(nameBuffer) };
        fs::FatDirectoryReader reader{ cache, geometry, assembler };
        fs::FatReaderSearch search{ reader, geometry };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    class FatReaderSearchSmallRootTest
        : public FatReaderSearchTest
    {
    public:
        FatReaderSearchSmallRootTest()
            : FatReaderSearchTest(SmallRoot())
        {}
    };
}

TEST_F(FatReaderSearchTest, finds_an_entry_by_its_long_name_ignoring_case)
{
    builder->AddFile(0, "Long File Name.txt", "LONGFI~1TXT", infra::ConstByteRange());

    ExpectFind("/LONG FILE NAME.TXT");

    ASSERT_FALSE(search.FoundRoot());
    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), search.Found().shortEntry);
    EXPECT_FALSE(search.FoundDirectory());
}

TEST_F(FatReaderSearchTest, finds_an_entry_by_its_short_name)
{
    builder->AddFile(0, "Long File Name.txt", "LONGFI~1TXT", infra::ConstByteRange());

    ExpectFind("/longfi~1.txt");

    EXPECT_FALSE(search.FoundRoot());
}

TEST_F(FatReaderSearchTest, the_root_is_a_directory_without_a_cluster)
{
    ExpectFind("/");

    EXPECT_TRUE(search.FoundRoot());
    EXPECT_TRUE(search.FoundDirectory());
    EXPECT_EQ(0u, search.FoundCluster());
}

TEST_F(FatReaderSearchTest, a_path_descends_into_directories)
{
    auto logs = builder->AddDirectory(0, "", "LOGS       ");
    auto old = builder->AddDirectory(logs, "", "OLD        ");
    builder->AddFile(old, "", "README  TXT", infra::ConstByteRange());

    ExpectFind("/logs/old");
    EXPECT_TRUE(search.FoundDirectory());
    EXPECT_EQ(old, search.FoundCluster());

    ExpectFind("/logs/old/readme.txt");
    EXPECT_EQ(old, search.Found().shortEntry.cluster);
}

TEST_F(FatReaderSearchTest, a_missing_last_name_is_not_found_and_the_end_marker_is_free)
{
    AddFiles(0, 3);

    ExpectFind("/other.txt", fs::Result::notFound);

    EXPECT_TRUE(search.LastMissing());
    EXPECT_EQ("other.txt", search.Component());
    EXPECT_EQ((fs::FatEntryPosition{ 0, 3 }), search.Free());
    EXPECT_FALSE(search.Extensible());
}

TEST_F(FatReaderSearchTest, the_first_deleted_entry_is_free)
{
    AddFiles(0, 4);
    Delete(fs::FatEntryPosition{ 0, 2 });
    Delete(fs::FatEntryPosition{ 0, 3 });

    ExpectFind("/new.txt", fs::Result::notFound);

    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), search.Free());
    EXPECT_FALSE(search.Extensible());
}

TEST_F(FatReaderSearchTest, a_full_directory_continues_after_its_last_cluster)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 30);
    auto second = builder->FatEntry(directory);

    ExpectFind("/dir/new.txt", fs::Result::notFound);

    EXPECT_TRUE(search.LastMissing());
    EXPECT_EQ((fs::FatEntryPosition{ second, 16 }), search.Free());
    EXPECT_TRUE(search.Extensible());
}

TEST_F(FatReaderSearchSmallRootTest, a_full_fixed_root_has_no_free_entry)
{
    AddFiles(0, 16);

    ExpectFind("/new.txt", fs::Result::notFound);

    EXPECT_TRUE(search.LastMissing());
    EXPECT_EQ(std::nullopt, search.Free());
    EXPECT_FALSE(search.Extensible());
}

TEST_F(FatReaderSearchTest, a_missing_directory_is_not_the_last_name)
{
    ExpectFind("/missing/new.txt", fs::Result::notFound);

    EXPECT_FALSE(search.LastMissing());
}

TEST_F(FatReaderSearchTest, a_file_on_the_path_is_not_a_directory)
{
    builder->AddFile(0, "", "README  TXT", infra::ConstByteRange());

    ExpectFind("/readme.txt/new.txt", fs::Result::notADirectory);
}

TEST_F(FatReaderSearchTest, a_name_that_fat_cannot_hold_is_invalid)
{
    ExpectFind("/a?.txt", fs::Result::invalidPath);
}

TEST_F(FatReaderSearchTest, a_directory_outside_the_volume_is_corrupted)
{
    fs::ShortEntry bad;
    bad.name = fs::ShortName{ { 'B', 'A', 'D', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' } };
    bad.attributes = fs::FatAttributes::directory;
    bad.firstCluster = geometry.clusterCount + 10;
    builder->AddEntries(0, "", bad);

    ExpectFind("/bad/new.txt", fs::Result::corrupted);
}

TEST_F(FatReaderSearchTest, a_new_search_forgets_the_previous_one)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 30);
    builder->AddFile(0, "", "README  TXT", infra::ConstByteRange());

    ExpectFind("/dir/new.txt", fs::Result::notFound);
    ExpectFind("/readme.txt");

    EXPECT_FALSE(search.LastMissing());
    EXPECT_EQ(std::nullopt, search.Free());
    EXPECT_FALSE(search.Extensible());

    ExpectFind("/other.txt", fs::Result::notFound);

    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), search.Free());
    EXPECT_FALSE(search.Extensible());
}

TEST_F(FatReaderSearchTest, a_device_error_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectFind("/new.txt", fs::Result::deviceError);
}
