#include "fs/fat/FatNameScan.hpp"
#include "fs/fat/FatShortNameGeneration.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>
#include <optional>

namespace
{
    void CopyName(const fs::ShortNameBytes& bytes, char (&name)[12])
    {
        std::copy(bytes.begin(), bytes.end(), name);
        name[11] = 0;
    }

    fs::FatFormatConfig SmallRoot()
    {
        fs::FatFormatConfig config;
        config.sectorsPerCluster = 1;
        config.rootEntries = 16;
        return config;
    }

    class FatNameScanTest
        : public FatVolumeFixture
    {
    public:
        explicit FatNameScanTest(const fs::FatFormatConfig& config = ClusterSize(1))
            : FatVolumeFixture(16384, config)
        {}

        void ExpectScan(uint32_t directory, infra::BoundedConstString name, uint8_t slots, const std::optional<fs::ShortNameBytes>& basis = std::nullopt, fs::Result result = fs::Result::success, const std::optional<fs::FatEntryPosition>& except = std::nullopt)
        {
            scan.Scan(directory, name, slots, basis, except, [this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        void ExpectAliasScan(const fs::ShortNameBytes& alias, fs::Result result = fs::Result::success)
        {
            scan.ScanForAlias(alias, [this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
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
        fs::FatNameScan scan{ reader };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    class FatNameScanSmallRootTest
        : public FatNameScanTest
    {
    public:
        FatNameScanSmallRootTest()
            : FatNameScanTest(SmallRoot())
        {}
    };
}

TEST_F(FatNameScanTest, finds_an_entry_by_its_long_name_ignoring_case)
{
    builder->AddFile(0, "Long File Name.txt", "LONGFI~1TXT", infra::ConstByteRange());

    ExpectScan(0, "LONG FILE NAME.TXT", 3);

    ASSERT_TRUE(scan.Found().has_value());
    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), scan.Found()->shortEntry);
}

TEST_F(FatNameScanTest, excepted_entry_does_not_match_and_the_scan_goes_on_to_free_slots)
{
    builder->AddFile(0, "Long File Name.txt", "LONGFI~1TXT", infra::ConstByteRange());

    ExpectScan(0, "long file name.txt", 3, fs::ShortNameBytes{ 'L', 'O', 'N', 'G', 'F', 'I', ' ', ' ', 'T', 'X', 'T' }, fs::Result::success, fs::FatEntryPosition{ 0, 2 });

    EXPECT_FALSE(scan.Found().has_value());
    ASSERT_TRUE(scan.Slots().has_value());
    EXPECT_EQ((fs::FatEntryPosition{ 0, 3 }), scan.Slots()->first);
    EXPECT_EQ(2u, scan.FreeTail());
}

TEST_F(FatNameScanTest, finds_an_entry_by_its_alias)
{
    builder->AddFile(0, "Long File Name.txt", "LONGFI~1TXT", infra::ConstByteRange());

    ExpectScan(0, "longfi~1.txt", 1);

    EXPECT_TRUE(scan.Found().has_value());
}

TEST_F(FatNameScanTest, missing_name_is_not_found)
{
    builder->AddFile(0, "", "README  TXT", infra::ConstByteRange());

    ExpectScan(0, "other.txt", 1);

    EXPECT_FALSE(scan.Found().has_value());
}

TEST_F(FatNameScanTest, takes_the_lowest_tail_that_no_short_or_long_name_uses)
{
    builder->AddFile(0, "long file 1.txt", "LONGFI~1TXT", infra::ConstByteRange());
    builder->AddFile(0, "LONGFI~2.TXT", "LONGFI~3TXT", infra::ConstByteRange());
    builder->AddFile(0, "", "LONGFI~5TXT", infra::ConstByteRange());

    ExpectScan(0, "long file 4.txt", 3, fs::BasisName("long file 4.txt"));

    EXPECT_EQ(4, scan.FreeTail());
}

TEST_F(FatNameScanTest, tails_beyond_32_are_ignored)
{
    builder->AddFile(0, "", "LONGF~33TXT", infra::ConstByteRange());

    ExpectScan(0, "long file.txt", 2, fs::BasisName("long file.txt"));

    EXPECT_EQ(1, scan.FreeTail());
}

TEST_F(FatNameScanTest, last_tail_is_32)
{
    for (uint32_t tail = 1; tail != 32; ++tail)
    {
        char name[12];
        CopyName(fs::WithNumericTail(fs::BasisName("long file.txt"), tail), name);
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }

    ExpectScan(0, "long file.txt", 2, fs::BasisName("long file.txt"));

    EXPECT_EQ(32, scan.FreeTail());
}

TEST_F(FatNameScanTest, no_tail_is_free_when_all_32_are_used)
{
    for (uint32_t tail = 1; tail <= 32; ++tail)
    {
        char name[12];
        CopyName(fs::WithNumericTail(fs::BasisName("long file.txt"), tail), name);
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }

    ExpectScan(0, "long file.txt", 2, fs::BasisName("long file.txt"));

    EXPECT_EQ(std::nullopt, scan.FreeTail());
}

TEST_F(FatNameScanTest, deleted_entries_are_reused)
{
    AddFiles(0, 4);
    Delete(fs::FatEntryPosition{ 0, 1 });
    Delete(fs::FatEntryPosition{ 0, 2 });

    ExpectScan(0, "new file.txt", 2);

    EXPECT_EQ((fs::FatFreeSlots{ fs::FatEntryPosition{ 0, 1 }, 1, false }), scan.Slots());
}

TEST_F(FatNameScanTest, too_short_run_of_deleted_entries_is_skipped)
{
    AddFiles(0, 3);
    Delete(fs::FatEntryPosition{ 0, 1 });

    ExpectScan(0, "new file.txt", 2);

    EXPECT_EQ((fs::FatFreeSlots{ fs::FatEntryPosition{ 0, 3 }, 3, false }), scan.Slots());
}

TEST_F(FatNameScanTest, deleted_entries_before_the_end_join_the_free_end)
{
    AddFiles(0, 3);
    Delete(fs::FatEntryPosition{ 0, 2 });

    ExpectScan(0, "new file.txt", 2);

    EXPECT_EQ((fs::FatFreeSlots{ fs::FatEntryPosition{ 0, 2 }, 2, false }), scan.Slots());
}

TEST_F(FatNameScanTest, run_does_not_cross_a_cluster_boundary)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 16);
    auto second = builder->FatEntry(directory);
    Delete(fs::FatEntryPosition{ directory, 15 });
    Delete(fs::FatEntryPosition{ second, 0 });

    ExpectScan(directory, "new file.txt", 2);

    EXPECT_EQ((fs::FatFreeSlots{ fs::FatEntryPosition{ second, 2 }, 18, false }), scan.Slots());
}

TEST_F(FatNameScanTest, short_end_of_a_cluster_starts_the_set_at_the_end_marker)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 11);
    Delete(fs::FatEntryPosition{ directory, 12 });

    ExpectScan(directory, "new file.txt", 5);

    EXPECT_EQ((fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 13 }, 13, true }), scan.Slots());
}

TEST_F(FatNameScanTest, deleted_entries_and_the_end_of_a_cluster_hold_a_set)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 11);
    Delete(fs::FatEntryPosition{ directory, 12 });

    ExpectScan(directory, "new file.txt", 4);

    EXPECT_EQ((fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 12 }, 12, false }), scan.Slots());
}

TEST_F(FatNameScanTest, full_directory_continues_after_its_last_cluster)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 30);
    auto second = builder->FatEntry(directory);

    ExpectScan(directory, "new file.txt", 2);

    EXPECT_EQ((fs::FatFreeSlots{ fs::FatEntryPosition{ second, 16 }, 32, true }), scan.Slots());
}

TEST_F(FatNameScanSmallRootTest, full_fixed_root_has_no_free_slots)
{
    AddFiles(0, 16);

    ExpectScan(0, "new file.txt", 2);

    EXPECT_EQ(std::nullopt, scan.Slots());
}

TEST_F(FatNameScanSmallRootTest, end_of_the_fixed_root_that_is_too_short_has_no_free_slots)
{
    AddFiles(0, 15);

    ExpectScan(0, "new file.txt", 2);

    EXPECT_EQ(std::nullopt, scan.Slots());
}

TEST_F(FatNameScanTest, alias_used_by_a_short_name)
{
    builder->AddFile(0, "", "LOAB12~1TXT", infra::ConstByteRange());
    ExpectScan(0, "long file.txt", 2, fs::BasisName("long file.txt"));

    ExpectAliasScan(fs::ShortNameBytes{ 'L', 'O', 'A', 'B', '1', '2', '~', '1', 'T', 'X', 'T' });

    EXPECT_TRUE(scan.AliasInUse());
}

TEST_F(FatNameScanTest, alias_used_by_a_long_name)
{
    builder->AddFile(0, "loab12~1.txt", "OTHER   TXT", infra::ConstByteRange());
    builder->AddFile(0, "", "LAST    TXT", infra::ConstByteRange());
    ExpectScan(0, "long file.txt", 2, fs::BasisName("long file.txt"));

    ExpectAliasScan(fs::ShortNameBytes{ 'L', 'O', 'A', 'B', '1', '2', '~', '1', 'T', 'X', 'T' });

    EXPECT_TRUE(scan.AliasInUse());
}

TEST_F(FatNameScanTest, unused_alias)
{
    builder->AddFile(0, "long file name.txt", "LONGFI~1TXT", infra::ConstByteRange());
    Delete(fs::FatEntryPosition{ 0, 0 });
    ExpectScan(0, "long file.txt", 2, fs::BasisName("long file.txt"));
    auto slots = scan.Slots();

    ExpectAliasScan(fs::ShortNameBytes{ 'L', 'O', 'A', 'B', '1', '2', '~', '1', 'T', 'X', 'T' });

    EXPECT_FALSE(scan.AliasInUse());
    EXPECT_FALSE(scan.Found().has_value());
    EXPECT_EQ(slots, scan.Slots());
}

TEST_F(FatNameScanTest, device_error_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectScan(0, "new file.txt", 2, std::nullopt, fs::Result::deviceError);
}
