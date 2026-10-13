#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "metrics/fat/FatChecker.hpp"
#include "gtest/gtest.h"
#include <array>
#include <optional>

namespace
{
    std::array<uint8_t, 2000> Pattern()
    {
        std::array<uint8_t, 2000> pattern;

        for (std::size_t index = 0; index != pattern.size(); ++index)
            pattern[index] = static_cast<uint8_t>(index * 7 + 3);

        return pattern;
    }

    class FatCheckerTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        explicit FatCheckerTest(uint32_t blocks = 16384, bool partitioned = true)
            : device(blocks)
        {
            fs::FatFormatConfig config;
            config.sectorsPerCluster = 1;
            config.partitioned = partitioned;
            fs::FatFormatter::WithStorage<8> formatter{ device };
            formatter.Format(config, [](fs::Result) {});
            ExecuteAllActions();
            builder.emplace(device);
        }

        metrics::FatCheck Check(metrics::FatCheckMode mode)
        {
            return metrics::CheckFatImage(reader, mode);
        }

        void ExpectProblem(metrics::FatCheckMode mode, const std::string& part)
        {
            auto check = Check(mode);
            ASSERT_TRUE(check.problem.has_value());
            EXPECT_NE(std::string::npos, check.problem->find(part)) << *check.problem;
        }

        void ExpectOnlyAfterCut(const std::string& part)
        {
            EXPECT_EQ(std::nullopt, Check(metrics::FatCheckMode::afterCut).problem);
            ExpectProblem(metrics::FatCheckMode::strict, part);
        }

        fs::ShortEntry Entry(const char (&name)[12], uint32_t firstCluster, uint32_t size)
        {
            fs::ShortEntry entry;
            std::copy_n(name, 11, entry.name.bytes.begin());
            entry.attributes = fs::FatAttributes::archive;
            entry.firstCluster = firstCluster;
            entry.size = size;
            return entry;
        }

        fs::SparseBlockDevice::WithStorage<400> device;
        std::optional<fs::FatImageBuilder> builder;
        metrics::SectorReader reader{ [this](uint32_t sector)
            {
                std::vector<uint8_t> contents(512);
                device.ReadBlock(sector, infra::MakeRange(contents));
                return contents;
            } };
        std::array<uint8_t, 2000> contents{ Pattern() };
    };

    class FatCheckerOnFat12Test
        : public FatCheckerTest
    {
    public:
        FatCheckerOnFat12Test()
            : FatCheckerTest(4096)
        {}
    };

    class FatCheckerOnFat32Test
        : public FatCheckerTest
    {
    public:
        FatCheckerOnFat32Test()
            : FatCheckerTest(81920)
        {}
    };

    class FatCheckerWithoutPartitionTableTest
        : public FatCheckerTest
    {
    public:
        FatCheckerWithoutPartitionTableTest()
            : FatCheckerTest(16384, false)
        {}
    };
}

TEST_F(FatCheckerTest, formatted_volume_is_consistent)
{
    auto check = Check(metrics::FatCheckMode::strict);

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(16u, check.layout.bits);
    EXPECT_TRUE(check.entries.empty());
    EXPECT_EQ(0u, check.lostClusters);
}

TEST_F(FatCheckerTest, lists_files_and_directories_with_their_chains)
{
    builder->AddFile(0, "A long file name.txt", "ALONGF~1TXT", infra::MakeRange(contents), true);
    auto directory = builder->AddDirectory(0, "", "SUBDIR     ");
    builder->AddFile(directory, "", "INNER   TXT", infra::Head(infra::MakeRange(contents), 10));

    auto check = Check(metrics::FatCheckMode::strict);

    EXPECT_EQ(std::nullopt, check.problem);
    ASSERT_NE(nullptr, check.Find("/A long file name.txt"));
    EXPECT_EQ(2000u, check.Find("/A long file name.txt")->size);
    EXPECT_EQ(4u, check.Find("/A long file name.txt")->clusters.size());
    ASSERT_NE(nullptr, check.Find("/SUBDIR"));
    EXPECT_TRUE(check.Find("/SUBDIR")->directory);
    ASSERT_NE(nullptr, check.Find("/SUBDIR/INNER.TXT"));
    EXPECT_EQ(10u, check.Find("/SUBDIR/INNER.TXT")->size);
}

TEST_F(FatCheckerTest, reads_the_contents_of_a_file)
{
    builder->AddFile(0, "", "FILE    BIN", infra::MakeRange(contents), true);

    auto check = Check(metrics::FatCheckMode::strict);
    ASSERT_NE(nullptr, check.Find("/FILE.BIN"));

    auto read = metrics::ReadContents(reader, check, *check.Find("/FILE.BIN"));
    EXPECT_TRUE(std::equal(read.begin(), read.end(), contents.begin(), contents.end()));
}

TEST_F(FatCheckerTest, cluster_in_two_chains_is_a_problem)
{
    auto first = builder->AddFile(0, "", "FIRST   BIN", infra::Head(infra::MakeRange(contents), 600));
    auto second = builder->AddFile(0, "", "SECOND  BIN", infra::Head(infra::MakeRange(contents), 600));
    builder->SetFatEntry(first + 1, second);

    ExpectProblem(metrics::FatCheckMode::afterCut, "is also in the chain of");
}

TEST_F(FatCheckerTest, chain_that_links_to_a_free_cluster_is_a_problem)
{
    auto first = builder->AddFile(0, "", "FILE    BIN", infra::Head(infra::MakeRange(contents), 600));
    builder->SetFatEntry(first + 1, 0);

    ExpectProblem(metrics::FatCheckMode::afterCut, "free or bad cluster");
}

TEST_F(FatCheckerTest, chain_that_links_out_of_range_is_a_problem)
{
    auto first = builder->AddFile(0, "", "FILE    BIN", infra::Head(infra::MakeRange(contents), 600));
    builder->SetFatEntry(first, builder->Geometry().clusterCount + 10);

    ExpectProblem(metrics::FatCheckMode::afterCut, "out of range");
}

TEST_F(FatCheckerTest, chain_that_loops_is_a_problem)
{
    auto first = builder->AddFile(0, "", "FILE    BIN", infra::Head(infra::MakeRange(contents), 600));
    builder->SetFatEntry(first + 1, first);

    ExpectProblem(metrics::FatCheckMode::afterCut, "loops");
}

TEST_F(FatCheckerTest, chain_shorter_than_its_file_is_a_problem)
{
    builder->AddEntries(0, "", Entry("SHORT   BIN", builder->AllocateChain(1), 600));

    ExpectProblem(metrics::FatCheckMode::afterCut, "is shorter than its size");
}

TEST_F(FatCheckerTest, file_with_data_and_no_cluster_is_a_problem)
{
    builder->AddEntries(0, "", Entry("EMPTY   BIN", 0, 600));

    ExpectProblem(metrics::FatCheckMode::afterCut, "has no clusters");
}

TEST_F(FatCheckerTest, directory_without_a_cluster_is_a_problem)
{
    auto entry = Entry("SUBDIR     ", 0, 0);
    entry.attributes = fs::FatAttributes::directory;
    builder->AddEntries(0, "", entry);

    ExpectProblem(metrics::FatCheckMode::afterCut, "directory without a valid first cluster");
}

TEST_F(FatCheckerTest, chain_longer_than_its_file_is_allowed_only_after_a_cut)
{
    builder->AddEntries(0, "", Entry("LONGER  BIN", builder->AllocateChain(3), 600));

    ExpectOnlyAfterCut("is longer than its size");
}

TEST_F(FatCheckerTest, lost_clusters_are_allowed_only_after_a_cut)
{
    builder->AllocateChain(2);

    EXPECT_EQ(2u, Check(metrics::FatCheckMode::afterCut).lostClusters);
    ExpectOnlyAfterCut("2 lost clusters");
}

TEST_F(FatCheckerTest, orphaned_long_name_entries_are_allowed_only_after_a_cut)
{
    std::array<uint8_t, 32> longEntry{};
    fs::EncodeLongEntry("orphan", 1, true, 0x12, infra::MakeRange(longEntry));
    builder->WriteRawEntry(fs::FatEntryPosition{ 0, 0 }, infra::MakeRange(longEntry));

    EXPECT_EQ(1u, Check(metrics::FatCheckMode::afterCut).orphanedLongNames);
    ExpectOnlyAfterCut("1 orphaned long name entries");
}

TEST_F(FatCheckerTest, long_name_with_a_wrong_checksum_is_an_orphan)
{
    std::array<uint8_t, 32> longEntry{};
    fs::EncodeLongEntry("wrong", 1, true, 0x12, infra::MakeRange(longEntry));
    builder->WriteRawEntry(fs::FatEntryPosition{ 0, 0 }, infra::MakeRange(longEntry));
    std::array<uint8_t, 32> shortEntry{};
    fs::EncodeShortEntry(Entry("WRONG      ", 0, 0), infra::MakeRange(shortEntry));
    builder->WriteRawEntry(fs::FatEntryPosition{ 0, 1 }, infra::MakeRange(shortEntry));

    auto check = Check(metrics::FatCheckMode::afterCut);

    EXPECT_EQ(1u, check.orphanedLongNames);
    EXPECT_NE(nullptr, check.Find("/WRONG"));
}

TEST_F(FatCheckerTest, two_names_of_one_chain_are_allowed_only_after_a_cut)
{
    auto first = builder->AddFile(0, "", "OLD     BIN", infra::Head(infra::MakeRange(contents), 600));
    builder->AddEntries(0, "", Entry("NEW     BIN", first, 600));

    ExpectOnlyAfterCut("is also in the chain of /OLD.BIN");
}

TEST_F(FatCheckerTest, fat_copy_that_lags_behind_is_allowed_only_after_a_cut)
{
    builder->AllocateChain(1);
    auto check = Check(metrics::FatCheckMode::afterCut);
    auto secondCopy = check.layout.firstFatSector + check.layout.sectorsPerFat;
    std::array<uint8_t, 512> sector;
    device.ReadBlock(secondCopy, infra::MakeRange(sector));
    sector[4] = 0;
    sector[5] = 0;
    device.WriteBlock(secondCopy, infra::MakeRange(sector));
    builder->AddEntries(0, "", Entry("FILE    BIN", 2, 100));

    ExpectOnlyAfterCut("FAT copy 1 differs from the first");
}

TEST_F(FatCheckerTest, device_without_a_boot_sector_is_a_problem)
{
    std::array<uint8_t, 512> empty{};
    device.WriteBlock(0, infra::MakeRange(empty));

    ExpectProblem(metrics::FatCheckMode::afterCut, "no boot sector");
}

TEST_F(FatCheckerOnFat12Test, files_whose_fat_entries_straddle_sectors_are_followed)
{
    builder->AllocateChain(339);
    builder->AddFile(0, "", "FILE    BIN", infra::MakeRange(contents));

    auto check = Check(metrics::FatCheckMode::strict);

    EXPECT_EQ(12u, check.layout.bits);
    EXPECT_EQ(std::nullopt, Check(metrics::FatCheckMode::afterCut).problem);
    ASSERT_NE(nullptr, check.Find("/FILE.BIN"));
    EXPECT_EQ(4u, check.Find("/FILE.BIN")->clusters.size());
    EXPECT_EQ(339u, check.lostClusters);
}

TEST_F(FatCheckerOnFat32Test, root_chain_of_fat32_is_owned)
{
    builder->AddFile(0, "in the root.bin", "INTHER~1BIN", infra::MakeRange(contents), true);

    auto check = Check(metrics::FatCheckMode::strict);

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(32u, check.layout.bits);
    EXPECT_NE(nullptr, check.Find("/in the root.bin"));
}

TEST_F(FatCheckerWithoutPartitionTableTest, volume_without_a_partition_table_is_read)
{
    builder->AddFile(0, "", "FILE    BIN", infra::MakeRange(contents));

    auto check = Check(metrics::FatCheckMode::strict);

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(0u, check.layout.volumeStart);
    EXPECT_NE(nullptr, check.Find("/FILE.BIN"));
}

TEST_F(FatCheckerTest, reports_a_volume_that_is_marked_clean)
{
    EXPECT_EQ(true, Check(metrics::FatCheckMode::strict).clean);
}

TEST_F(FatCheckerTest, reports_a_volume_that_is_not_marked_clean)
{
    builder->SetFatEntry(1, fs::WithCleanShutdown(fs::FatType::fat16, builder->FatEntry(1), false));

    auto check = Check(metrics::FatCheckMode::strict);

    EXPECT_EQ(false, check.clean);
    EXPECT_EQ(std::nullopt, check.problem);
}

TEST_F(FatCheckerOnFat32Test, reports_the_clean_bit_of_fat32)
{
    builder->SetFatEntry(1, fs::WithCleanShutdown(fs::FatType::fat32, builder->FatEntry(1), false));

    EXPECT_EQ(false, Check(metrics::FatCheckMode::strict).clean);
}

TEST_F(FatCheckerOnFat12Test, fat12_has_no_clean_bit)
{
    EXPECT_EQ(std::nullopt, Check(metrics::FatCheckMode::strict).clean);
}
