#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdio>

namespace
{
    class CollectingVisitor
        : public fs::FatDirectoryVisitor
    {
    public:
        bool Visit(const fs::FatScannedEntry& entry) override
        {
            ++visits;

            if (!names.full())
            {
                names.emplace_back(entry.name);
                positions.push_back(entry.shortEntry);
            }

            return visits != stopAfter;
        }

        void FreeSlot(fs::FatEntryPosition position, bool end) override
        {
            if (end)
                endAt = position;
            else
                freeSlots.push_back(position);
        }

        infra::BoundedVector<infra::BoundedString::WithStorage<64>>::WithMaxSize<600> names;
        infra::BoundedVector<fs::FatEntryPosition>::WithMaxSize<600> positions;
        infra::BoundedVector<fs::FatEntryPosition>::WithMaxSize<8> freeSlots;
        std::optional<fs::FatEntryPosition> endAt;
        std::size_t stopAfter{ 0 };
        std::size_t visits{ 0 };
    };

    class FatDirectoryReaderTest
        : public FatVolumeFixture
    {
    public:
        explicit FatDirectoryReaderTest(uint32_t blocks = 16384, const fs::FatFormatConfig& config = ClusterSize(1))
            : FatVolumeFixture(blocks, config)
        {}

        void ExpectRead(fs::FatDirectoryCursor& cursor, fs::Result result = fs::Result::success)
        {
            reader.Read(cursor, visitor, [this](fs::Result result)
                {
                    done.callback(result);
                });
            testing::Mock::VerifyAndClearExpectations(&done);
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        fs::FatDirectoryCursor ReadAll(uint32_t directory, fs::Result result = fs::Result::success)
        {
            auto cursor = reader.Start(directory);
            ExpectRead(cursor, result);
            return cursor;
        }

        void AddNumberedFiles(uint32_t directory, uint32_t count)
        {
            for (uint32_t index = 0; index != count; ++index)
            {
                std::array<char, 12> shortName;
                std::snprintf(shortName.data(), shortName.size(), "F%07uTXT", static_cast<unsigned>(index));
                char name[12];
                std::copy_n(shortName.begin(), 12, name);
                builder->AddFile(directory, "", name, infra::ConstByteRange());
            }
        }

        std::array<uint8_t, 255> nameBuffer{};
        fs::FatEntryAssembler assembler{ infra::MakeRange(nameBuffer) };
        fs::FatDirectoryReader reader{ cache, geometry, assembler };
        CollectingVisitor visitor;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };
}

TEST_F(FatDirectoryReaderTest, reads_the_fixed_root_up_to_its_end_marker)
{
    std::array<uint8_t, 3> data{ 1, 2, 3 };
    builder->AddFile(0, "A long file name.txt", "ALONGF~1TXT", infra::MakeRange(data));
    builder->AddFile(0, "", "README  TXT", infra::ConstByteRange());

    auto cursor = ReadAll(0);

    ASSERT_EQ(2, visitor.names.size());
    EXPECT_EQ("A long file name.txt", visitor.names[0]);
    EXPECT_EQ("README.TXT", visitor.names[1]);
    EXPECT_EQ((fs::FatEntryPosition{ 0, 4 }), visitor.endAt);
    EXPECT_TRUE(cursor.done);
}

TEST_F(FatDirectoryReaderTest, follows_the_cluster_chain_of_a_directory)
{
    auto directory = builder->AddDirectory(0, "", "SUBDIR     ");
    AddNumberedFiles(directory, 20);

    ReadAll(directory);

    ASSERT_EQ(20, visitor.names.size());
    EXPECT_EQ("F0000000.TXT", visitor.names[0]);
    EXPECT_EQ("F0000019.TXT", visitor.names[19]);
    EXPECT_NE(directory, visitor.positions[19].cluster);
}

TEST_F(FatDirectoryReaderTest, directory_that_ends_with_its_last_cluster_is_done)
{
    auto directory = builder->AddDirectory(0, "", "SUBDIR     ");
    AddNumberedFiles(directory, 14);

    auto cursor = ReadAll(directory);

    EXPECT_EQ(14, visitor.names.size());
    EXPECT_EQ(std::nullopt, visitor.endAt);
    EXPECT_TRUE(cursor.done);
}

TEST_F(FatDirectoryReaderTest, stops_when_the_visitor_stops_and_resumes_after_it)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    builder->AddFile(0, "second file", "SECOND~1   ", infra::ConstByteRange());
    builder->AddFile(0, "", "THIRD      ", infra::ConstByteRange());
    visitor.stopAfter = 2;
    auto cursor = reader.Start(0);

    ExpectRead(cursor);
    EXPECT_EQ(2, visitor.names.size());
    EXPECT_FALSE(cursor.done);

    ExpectRead(cursor);
    ASSERT_EQ(3, visitor.names.size());
    EXPECT_EQ("THIRD", visitor.names[2]);
    EXPECT_TRUE(cursor.done);

    ExpectRead(cursor);
    EXPECT_EQ(3, visitor.names.size());
}

TEST_F(FatDirectoryReaderTest, entries_after_the_end_marker_are_not_read)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    std::array<uint8_t, 32> entry{};
    entry[0] = 'X';
    builder->WriteRawEntry(fs::FatEntryPosition{ 0, 2 }, infra::MakeRange(entry));

    ReadAll(0);

    EXPECT_EQ(1, visitor.names.size());
}

TEST_F(FatDirectoryReaderTest, reports_free_slots)
{
    builder->AddFile(0, "", "FIRST      ", infra::ConstByteRange());
    builder->AddFile(0, "", "SECOND     ", infra::ConstByteRange());
    auto sector = builder->ReadSector(geometry.rootSector);
    std::array<uint8_t, 32> deleted;
    std::copy_n(sector.begin(), 32, deleted.begin());
    deleted[0] = 0xe5;
    builder->WriteRawEntry(fs::FatEntryPosition{ 0, 0 }, infra::MakeRange(deleted));

    ReadAll(0);

    ASSERT_EQ(1, visitor.freeSlots.size());
    EXPECT_EQ((fs::FatEntryPosition{ 0, 0 }), visitor.freeSlots[0]);
    EXPECT_EQ(1, visitor.names.size());
}

TEST_F(FatDirectoryReaderTest, directory_with_more_entries_than_the_limit_is_corrupted)
{
    auto directory = builder->AddDirectory(0, "", "SUBDIR     ");
    AddNumberedFiles(directory, 14);
    builder->SetFatEntry(directory, directory);

    ReadAll(directory, fs::Result::corrupted);

    EXPECT_EQ(fs::FatDirectoryReader::maxEntries / 16 * 14, visitor.visits);
}

TEST_F(FatDirectoryReaderTest, cluster_link_outside_the_volume_is_corrupted)
{
    auto directory = builder->AddDirectory(0, "", "SUBDIR     ");
    AddNumberedFiles(directory, 16);
    builder->SetFatEntry(directory, 0);

    ReadAll(directory, fs::Result::corrupted);
}

TEST_F(FatDirectoryReaderTest, device_error_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout);

    ReadAll(0, fs::Result::deviceError);
}

TEST_F(FatDirectoryReaderTest, sector_of_an_entry_position)
{
    EXPECT_EQ(geometry.rootSector + 1, reader.SectorOf(fs::FatEntryPosition{ 0, 16 }));
    EXPECT_EQ(geometry.SectorOfCluster(5), reader.SectorOf(fs::FatEntryPosition{ 5, 15 }));
}

namespace
{
    class FatDirectoryReaderOnFat12Test
        : public FatDirectoryReaderTest
    {
    public:
        FatDirectoryReaderOnFat12Test()
            : FatDirectoryReaderTest(4096, ClusterSize(1))
        {}
    };

    class FatDirectoryReaderOnSmallFat12Test
        : public FatDirectoryReaderTest
    {
    public:
        FatDirectoryReaderOnSmallFat12Test()
            : FatDirectoryReaderTest(4096, {})
        {}
    };

    class FatDirectoryReaderOnFat32Test
        : public FatDirectoryReaderTest
    {
    public:
        FatDirectoryReaderOnFat32Test()
            : FatDirectoryReaderTest(81920, ClusterSize(1))
        {}
    };
}

TEST_F(FatDirectoryReaderOnFat12Test, reads_a_directory_whose_link_straddles_two_fat_sectors)
{
    ASSERT_EQ(fs::FatType::fat12, geometry.type);
    builder->AllocateChain(339);
    auto directory = builder->AddDirectory(0, "", "SUBDIR     ");
    ASSERT_EQ(341u, directory);
    AddNumberedFiles(directory, 20);

    ReadAll(directory);

    EXPECT_EQ(20, visitor.names.size());
}

TEST_F(FatDirectoryReaderOnSmallFat12Test, chain_that_loops_is_corrupted)
{
    auto directory = builder->AddDirectory(0, "", "SUBDIR     ");
    AddNumberedFiles(directory, 254);
    builder->SetFatEntry(directory, directory);

    ReadAll(directory, fs::Result::corrupted);

    EXPECT_EQ(geometry.clusterCount * 254, visitor.visits);
}

TEST_F(FatDirectoryReaderOnSmallFat12Test, full_fixed_root_ends_at_its_last_sector)
{
    AddNumberedFiles(0, 512);

    auto cursor = ReadAll(0);

    EXPECT_EQ(512, visitor.names.size());
    EXPECT_EQ(std::nullopt, visitor.endAt);
    EXPECT_TRUE(cursor.done);
}

TEST_F(FatDirectoryReaderOnFat32Test, root_of_fat32_is_a_cluster_chain)
{
    builder->AddFile(0, "in the fat32 root", "INTHEF~1   ", infra::ConstByteRange());

    auto cursor = reader.Start(0);
    EXPECT_EQ(geometry.rootCluster, cursor.cluster);
    ExpectRead(cursor);

    ASSERT_EQ(1, visitor.names.size());
    EXPECT_EQ("in the fat32 root", visitor.names[0]);
}
