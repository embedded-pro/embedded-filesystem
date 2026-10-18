#include "fs/fat/lite/FatLiteScan.hpp"
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

    class FatLiteScanTest
        : public FatVolumeFixture
    {
    public:
        explicit FatLiteScanTest(const fs::FatFormatConfig& config = ClusterSize(1))
            : FatVolumeFixture(16384, config)
        {}

        void ExpectScan(uint32_t directory, infra::BoundedConstString name, fs::Result result = fs::Result::success)
        {
            scan.Scan(directory, name, [this](fs::Result result)
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
        fs::FatLiteScan scan{ reader };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    class FatLiteScanSmallRootTest
        : public FatLiteScanTest
    {
    public:
        FatLiteScanSmallRootTest()
            : FatLiteScanTest(SmallRoot())
        {}
    };
}

TEST_F(FatLiteScanTest, finds_an_entry_by_its_long_name_ignoring_case)
{
    builder->AddFile(0, "Long File Name.txt", "LONGFI~1TXT", infra::ConstByteRange());

    ExpectScan(0, "LONG FILE NAME.TXT");

    ASSERT_TRUE(scan.Found().has_value());
    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), scan.Found()->shortEntry);
}

TEST_F(FatLiteScanTest, finds_an_entry_by_its_short_name)
{
    builder->AddFile(0, "Long File Name.txt", "LONGFI~1TXT", infra::ConstByteRange());

    ExpectScan(0, "longfi~1.txt");

    EXPECT_TRUE(scan.Found().has_value());
}

TEST_F(FatLiteScanTest, a_missing_name_is_not_found_and_the_end_marker_is_free)
{
    AddFiles(0, 3);

    ExpectScan(0, "other.txt");

    EXPECT_FALSE(scan.Found().has_value());
    EXPECT_EQ((fs::FatEntryPosition{ 0, 3 }), scan.Free());
    EXPECT_FALSE(scan.Extensible());
}

TEST_F(FatLiteScanTest, the_first_deleted_entry_is_free)
{
    AddFiles(0, 4);
    Delete(fs::FatEntryPosition{ 0, 2 });
    Delete(fs::FatEntryPosition{ 0, 3 });

    ExpectScan(0, "new.txt");

    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), scan.Free());
    EXPECT_FALSE(scan.Extensible());
}

TEST_F(FatLiteScanTest, a_full_directory_continues_after_its_last_cluster)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 30);
    auto second = builder->FatEntry(directory);

    ExpectScan(directory, "new.txt");

    EXPECT_EQ((fs::FatEntryPosition{ second, 16 }), scan.Free());
    EXPECT_TRUE(scan.Extensible());
}

TEST_F(FatLiteScanSmallRootTest, a_full_fixed_root_has_no_free_entry)
{
    AddFiles(0, 16);

    ExpectScan(0, "new.txt");

    EXPECT_EQ(std::nullopt, scan.Free());
    EXPECT_FALSE(scan.Extensible());
}

TEST_F(FatLiteScanTest, a_new_scan_forgets_the_previous_one)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 30);
    builder->AddFile(0, "", "README  TXT", infra::ConstByteRange());

    ExpectScan(directory, "new.txt");
    ExpectScan(0, "readme.txt");

    EXPECT_TRUE(scan.Found().has_value());
    EXPECT_EQ(std::nullopt, scan.Free());
    EXPECT_FALSE(scan.Extensible());

    ExpectScan(0, "other.txt");

    EXPECT_FALSE(scan.Found().has_value());
    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), scan.Free());
}

TEST_F(FatLiteScanTest, a_device_error_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectScan(0, "new.txt", fs::Result::deviceError);
}
