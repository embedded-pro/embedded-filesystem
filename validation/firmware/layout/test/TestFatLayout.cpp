#include "fs/fat/FatFormatLayout.hpp"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/layout/FatLayout.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>

namespace
{
    using Sector = std::array<uint8_t, 512>;

    void Put(Sector& sector, std::size_t offset, uint32_t value, std::size_t bytes)
    {
        for (std::size_t byte = 0; byte != bytes; ++byte)
            sector[offset + byte] = static_cast<uint8_t>(value >> (8 * byte));
    }

    Sector Signed()
    {
        Sector sector{};
        sector[510] = 0x55;
        sector[511] = 0xaa;
        return sector;
    }

    Sector Mbr(uint8_t type, uint32_t start, uint32_t sectors)
    {
        auto sector = Signed();
        sector[446 + 4] = type;
        Put(sector, 446 + 8, start, 4);
        Put(sector, 446 + 12, sectors, 4);
        return sector;
    }

    struct Bpb
    {
        uint32_t sectorsPerCluster;
        uint32_t reservedSectors;
        uint32_t fats;
        uint32_t rootEntries;
        uint32_t totalSectors;
        uint32_t fatSectors;
        bool fat32;
    };

    Sector BootSector(const Bpb& bpb)
    {
        auto sector = Signed();
        sector[0] = 0xeb;
        sector[1] = 0x3c;
        sector[2] = 0x90;
        Put(sector, 11, 512, 2);
        sector[13] = static_cast<uint8_t>(bpb.sectorsPerCluster);
        Put(sector, 14, bpb.reservedSectors, 2);
        sector[16] = static_cast<uint8_t>(bpb.fats);
        Put(sector, 17, bpb.rootEntries, 2);
        if (bpb.totalSectors < 0x10000)
            Put(sector, 19, bpb.totalSectors, 2);
        else
            Put(sector, 32, bpb.totalSectors, 4);
        if (bpb.fat32)
            Put(sector, 36, bpb.fatSectors, 4);
        else
            Put(sector, 22, bpb.fatSectors, 2);
        return sector;
    }

    const Bpb fat16{ 32, 1, 2, 512, 516259, 65, false };
    const Bpb fat32{ 64, 3542, 2, 0, 31108096, 3797, true };

    class FatLayoutRecordTest
        : public services::HilFixture
    {
    public:
        validation::ResultLines results{ response };
    };
}

TEST(FatLayoutTest, the_volume_of_a_card_with_a_master_boot_record_is_its_first_partition)
{
    auto place = validation::LocateVolume(infra::MakeRange(Mbr(0x06, 8029, 516259)));

    ASSERT_TRUE(place.has_value());
    EXPECT_EQ(8029, place->start);
    EXPECT_EQ(516259, place->sectors);
}

TEST(FatLayoutTest, the_volume_of_a_card_that_starts_with_a_boot_sector_starts_at_sector_zero)
{
    auto place = validation::LocateVolume(infra::MakeRange(BootSector(fat16)));

    ASSERT_TRUE(place.has_value());
    EXPECT_EQ(0, place->start);
    EXPECT_EQ(0, place->sectors);
}

TEST(FatLayoutTest, a_first_sector_without_a_signature_or_a_partition_holds_no_volume)
{
    auto unsigned_ = Mbr(0x06, 8029, 516259);
    unsigned_[511] = 0;

    EXPECT_EQ(std::nullopt, validation::LocateVolume(infra::MakeRange(unsigned_)));
    EXPECT_EQ(std::nullopt, validation::LocateVolume(infra::MakeRange(Mbr(0, 8029, 516259))));
    EXPECT_EQ(std::nullopt, validation::LocateVolume(infra::MakeRange(Mbr(0x06, 8029, 0))));
}

TEST(FatLayoutTest, a_fat16_volume_gives_its_layout_and_the_start_of_its_data_area_on_the_card)
{
    auto layout = validation::ParseVolume(infra::MakeRange(BootSector(fat16)), { 8029, 516259 });

    validation::FatLayout expected{ 8029, 516259, validation::LayoutType::fat16, 32, 1, 2, 65, 512, 8029 + 1 + 2 * 65 + 32 };
    EXPECT_EQ(expected, layout);
}

TEST(FatLayoutTest, a_fat32_volume_takes_the_size_of_its_fat_from_the_fat32_fields)
{
    auto layout = validation::ParseVolume(infra::MakeRange(BootSector(fat32)), { 8192, 31108096 });

    validation::FatLayout expected{ 8192, 31108096, validation::LayoutType::fat32, 64, 3542, 2, 3797, 0, 8192 + 3542 + 2 * 3797 };
    EXPECT_EQ(expected, layout);
}

TEST(FatLayoutTest, a_volume_with_few_clusters_is_fat12)
{
    auto layout = validation::ParseVolume(infra::MakeRange(BootSector({ 16, 1, 2, 512, 16000, 3, false })), { 0, 0 });

    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(validation::LayoutType::fat12, layout->type);
    EXPECT_EQ(16000, layout->partitionSectors);
    EXPECT_EQ(1 + 2 * 3 + 32, layout->dataStart);
}

TEST(FatLayoutTest, an_exfat_volume_gives_only_its_place)
{
    auto sector = Signed();
    for (std::size_t index = 0; index != 8; ++index)
        sector[3 + index] = static_cast<uint8_t>("EXFAT   "[index]);

    auto layout = validation::ParseVolume(infra::MakeRange(sector), { 32768, 62300160 });

    validation::FatLayout expected{ 32768, 62300160, validation::LayoutType::exfat };
    EXPECT_EQ(expected, layout);
}

TEST(FatLayoutTest, a_boot_sector_that_is_not_of_a_fat_volume_gives_no_layout)
{
    auto unsigned_ = BootSector(fat16);
    unsigned_[510] = 0;
    auto otherSectorSize = BootSector(fat16);
    Put(otherSectorSize, 11, 4096, 2);

    EXPECT_EQ(std::nullopt, validation::ParseVolume(infra::MakeRange(unsigned_), { 0, 0 }));
    EXPECT_EQ(std::nullopt, validation::ParseVolume(infra::MakeRange(otherSectorSize), { 0, 0 }));
    EXPECT_EQ(std::nullopt, validation::ParseVolume(infra::MakeRange(BootSector({ 0, 1, 2, 512, 516259, 65, false })), { 0, 0 }));
    EXPECT_EQ(std::nullopt, validation::ParseVolume(infra::MakeRange(BootSector({ 32, 1, 0, 512, 516259, 65, false })), { 0, 0 }));
    EXPECT_EQ(std::nullopt, validation::ParseVolume(infra::MakeRange(BootSector({ 32, 1, 2, 512, 100, 65, false })), { 0, 0 }));
}

TEST(FatLayoutTest, the_layout_of_the_formatter_is_that_of_its_partition_and_its_boot_sector)
{
    fs::FatFormatConfig config;
    config.alignmentSectors = 8192;
    auto formatted = fs::ComputeFormatLayout(524288, 512, config);
    ASSERT_TRUE(formatted.has_value());

    validation::FatLayout expected{ 8029, 516259, validation::LayoutType::fat16, 32, 1, 2, 65, 512, 8192 };
    EXPECT_EQ(expected, validation::FormatterLayout(*formatted));
}

TEST(FatLayoutTest, the_layout_of_the_formatter_without_a_partition_starts_at_sector_zero)
{
    fs::FatFormatConfig config;
    config.partitioned = false;
    auto formatted = fs::ComputeFormatLayout(65536, 512, config);
    ASSERT_TRUE(formatted.has_value());

    auto layout = validation::FormatterLayout(*formatted);
    EXPECT_EQ(0, layout.partitionStart);
    EXPECT_EQ(65536, layout.partitionSectors);
    EXPECT_EQ(validation::LayoutType::fat12, layout.type);
}

TEST(FatLayoutTest, differences_name_the_fields_that_differ)
{
    validation::FatLayout card{ 8192, 516096, validation::LayoutType::fat16, 64, 1, 2, 65, 512, 8360 };
    validation::FatLayout formatter{ 8029, 516259, validation::LayoutType::fat16, 32, 1, 2, 65, 512, 8192 };
    infra::BoundedString::WithStorage<128> names;

    EXPECT_EQ(0, validation::Differences(formatter, formatter, names));
    EXPECT_EQ("", names);
    EXPECT_EQ(4, validation::Differences(card, formatter, names));
    EXPECT_EQ("partitionStart,partitionSectors,sectorsPerCluster,dataStart", names);
}

TEST(FatLayoutTest, of_an_exfat_card_only_the_start_of_the_partition_is_compared)
{
    validation::FatLayout card{ 32768, 62300160, validation::LayoutType::exfat };
    validation::FatLayout formatter{ 32768, 62300160, validation::LayoutType::fat32, 64, 3542, 2, 3797, 0, 43648 };
    infra::BoundedString::WithStorage<128> names;

    EXPECT_EQ(0, validation::Differences(card, formatter, names));
    card.partitionStart = 8192;
    EXPECT_EQ(1, validation::Differences(card, formatter, names));
    EXPECT_EQ("partitionStart", names);
}

TEST_F(FatLayoutRecordTest, a_layout_is_added_to_a_record_with_its_type_by_name)
{
    validation::FatLayout layout{ 8029, 516259, validation::LayoutType::fat16, 32, 1, 2, 65, 512, 8192 };

    {
        auto record = results.Record("layout");
        validation::AddLayout(record, layout);
    }

    EXPECT_THAT(Output(), testing::HasSubstr("EVT layout id=1 partitionStart=8029 partitionSectors=516259 type=fat16 sectorsPerCluster=32 reservedSectors=1 fats=2 fatSectors=65 rootEntries=512 dataStart=8192\r\n"));
}

TEST(FatLayoutTest, every_type_has_a_name)
{
    EXPECT_STREQ("fat12", validation::TypeName(validation::LayoutType::fat12));
    EXPECT_STREQ("fat32", validation::TypeName(validation::LayoutType::fat32));
    EXPECT_STREQ("exfat", validation::TypeName(validation::LayoutType::exfat));
}

TEST(FatLayoutTest, a_boot_sector_may_start_with_a_near_jump)
{
    auto sector = BootSector(fat16);
    sector[0] = 0xe9;

    EXPECT_EQ(0, validation::LocateVolume(infra::MakeRange(sector))->start);
}

TEST(FatLayoutTest, the_formatter_makes_fat32_on_a_card_above_2_gb)
{
    auto formatted = fs::ComputeFormatLayout(31116288, 512, fs::FatFormatConfig{});
    ASSERT_TRUE(formatted.has_value());

    auto layout = validation::FormatterLayout(*formatted);
    EXPECT_EQ(validation::LayoutType::fat32, layout.type);
    EXPECT_EQ(0, layout.partitionStart % 8192);
    EXPECT_EQ(0, layout.dataStart % 8192);
}
