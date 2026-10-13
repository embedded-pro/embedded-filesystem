#include "fs/fat/FatEntrySetWriter.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatVolumeFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <algorithm>
#include <array>
#include <utility>

namespace
{
    class NameCollector
        : public fs::FatDirectoryVisitor
    {
    public:
        bool Visit(const fs::FatScannedEntry& entry) override
        {
            names.emplace_back(entry.name);
            return true;
        }

        void FreeSlot(fs::FatEntryPosition, bool) override
        {}

        infra::BoundedVector<infra::BoundedString::WithStorage<255>>::WithMaxSize<64> names;
    };

    class NoReservations
        : public fs::FatReservations
    {
    public:
        bool Reserved(uint32_t) const override
        {
            return false;
        }
    };

    fs::ShortEntry Entry(const char (&name)[12])
    {
        fs::ShortEntry entry;
        std::copy_n(name, entry.name.bytes.size(), entry.name.bytes.begin());
        entry.attributes = fs::FatAttributes::archive;
        return entry;
    }

    class FatEntrySetWriterTest
        : public FatVolumeFixture
    {
    public:
        explicit FatEntrySetWriterTest(uint32_t blocks = 16384, uint8_t sectorsPerCluster = 1)
            : FatVolumeFixture(blocks, ClusterSize(sectorsPerCluster))
        {
            state.geometry = geometry;
            cache.SetMirroredRegion(fs::SectorCache::MirroredRegion{ geometry.firstFatSector, geometry.sectorsPerFat, geometry.fatCopies });
            allocator.Reset();
        }

        void ExpectWrite(infra::BoundedConstString longName, const fs::ShortEntry& entry, const fs::FatFreeSlots& slots, fs::Result result = fs::Result::success)
        {
            setWriter.Write(fs::FatEntrySet{ longName, entry }, slots, [this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
        }

        void Flush()
        {
            cache.Flush([this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(fs::Result::success));
            ExecuteAllActions();
        }

        NameCollector& List(uint32_t directory)
        {
            auto cursor = reader.Start(directory);
            collector.names.clear();
            reader.Read(cursor, collector, [this](fs::Result result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(fs::Result::success));
            ExecuteAllActions();
            return collector;
        }

        uint32_t FullDirectory()
        {
            auto directory = builder->AddDirectory(0, "", "DIR        ");
            AddFiles(directory, 14);
            return directory;
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

        void RecordWrites()
        {
            device.Observe([this](fs::SparseBlockDevice::Request request, uint32_t block)
                {
                    if (request != fs::SparseBlockDevice::Request::read)
                        requests.emplace_back(request, block);
                });
        }

        fs::FatVolumeState state;
        fs::FatTableWriter tableWriter{ cache, state.geometry };
        NoReservations reservations;
        fs::FatFreeSpaceScan freeSpace{ cache, state };
        fs::FatAllocator allocator{ cache, state, tableWriter, reservations, freeSpace };
        std::array<uint8_t, 255> nameBuffer{};
        fs::FatEntryAssembler assembler{ infra::MakeRange(nameBuffer) };
        fs::FatDirectoryReader reader{ cache, state.geometry, assembler };
        fs::FatEntrySetWriter setWriter{ cache, reader, allocator, state.geometry };
        NameCollector collector;
        infra::BoundedVector<std::pair<fs::SparseBlockDevice::Request, uint32_t>>::WithMaxSize<32> requests;
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    };

    class FatEntrySetWriterWithLargeClustersTest
        : public FatEntrySetWriterTest
    {
    public:
        FatEntrySetWriterWithLargeClustersTest()
            : FatEntrySetWriterTest(16384, 4)
        {}
    };

    class FatEntrySetWriterOnFat12Test
        : public FatEntrySetWriterTest
    {
    public:
        FatEntrySetWriterOnFat12Test()
            : FatEntrySetWriterTest(4096)
        {}
    };

    using Request = fs::SparseBlockDevice::Request;
}

TEST_F(FatEntrySetWriterTest, short_entry_is_written_in_its_slot)
{
    auto entry = Entry("README  TXT");

    ExpectWrite("", entry, fs::FatFreeSlots{ fs::FatEntryPosition{ 0, 2 }, 2, false });
    Flush();

    auto sector = builder->ReadSector(geometry.rootSector);
    EXPECT_EQ(entry, fs::DecodeShortEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), 64), 32)));
    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), setWriter.Written().first);
    EXPECT_EQ((fs::FatEntryPosition{ 0, 2 }), setWriter.Written().shortEntry);
    EXPECT_EQ(1, setWriter.Written().slots);
    EXPECT_FALSE(setWriter.Written().longName);
}

TEST_F(FatEntrySetWriterTest, long_name_entries_precede_the_short_entry)
{
    auto entry = Entry("LONGFI~1TXT");
    AddFiles(0, 3);

    ExpectWrite("Long File Name.txt", entry, fs::FatFreeSlots{ fs::FatEntryPosition{ 0, 3 }, 3, false });
    Flush();

    auto sector = builder->ReadSector(geometry.rootSector);
    auto first = fs::DecodeLongEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), 3 * 32), 32));
    auto second = fs::DecodeLongEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), 4 * 32), 32));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(2, first->ordinal);
    EXPECT_TRUE(first->last);
    EXPECT_EQ(1, second->ordinal);
    EXPECT_FALSE(second->last);
    EXPECT_EQ(fs::LongNameChecksum(entry.name.bytes), first->checksum);
    EXPECT_EQ(entry, fs::DecodeShortEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), 5 * 32), 32)));
    EXPECT_EQ((fs::FatEntryPosition{ 0, 3 }), setWriter.Written().first);
    EXPECT_EQ((fs::FatEntryPosition{ 0, 5 }), setWriter.Written().shortEntry);
    EXPECT_EQ(3, setWriter.Written().slots);
    EXPECT_TRUE(setWriter.Written().longName);
    EXPECT_EQ("Long File Name.txt", setWriter.Written().name);
    EXPECT_EQ("Long File Name.txt", List(0).names.back());
}

TEST_F(FatEntrySetWriterTest, set_in_one_sector_is_written_with_other_entries)
{
    cache.Modify(geometry.rootSector + 1, 2, [](fs::Result, infra::ByteRange) {});
    ExecuteAllActions();
    RecordWrites();

    ExpectWrite("Long File Name.txt", Entry("LONGFI~1TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ 0, 0 }, 0, false });
    Flush();

    ASSERT_EQ(3u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, geometry.rootSector), requests[0]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.rootSector + 1), requests[1]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[2]);
}

TEST_F(FatEntrySetWriterTest, set_spanning_two_sectors_writes_its_short_entry_last)
{
    AddFiles(0, 15);
    RecordWrites();

    ExpectWrite("Long File Name.txt", Entry("LONGFI~1TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ 0, 15 }, 15, false });
    Flush();

    ASSERT_EQ(4u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, geometry.rootSector), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.rootSector + 1), requests[2]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[3]);
    EXPECT_EQ("Long File Name.txt", List(0).names.back());
}

TEST_F(FatEntrySetWriterTest, full_directory_grows_by_a_cluster_that_is_written_before_it_is_linked)
{
    auto directory = FullDirectory();
    state.freeClusters = 100;
    RecordWrites();

    ExpectWrite("Long File Name.txt", Entry("LONGFI~1TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 16 }, 16, true });
    Flush();

    auto added = builder->FatEntry(directory);
    ASSERT_TRUE(geometry.IsCluster(added));
    EXPECT_EQ(fs::EndOfChain(geometry.type), builder->FatEntry(added));
    EXPECT_EQ(99u, state.freeClusters);
    EXPECT_EQ((fs::FatEntryPosition{ added, 0 }), setWriter.Written().first);
    EXPECT_EQ((fs::FatEntryPosition{ added, 2 }), setWriter.Written().shortEntry);
    ASSERT_FALSE(requests.empty());
    EXPECT_EQ(std::make_pair(Request::write, geometry.SectorOfCluster(added)), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_EQ("Long File Name.txt", List(directory).names.back());
}

TEST_F(FatEntrySetWriterTest, set_continues_from_the_end_of_a_cluster_into_a_new_one)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 12);

    ExpectWrite("Long File Name.txt", Entry("LONGFI~1TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 14 }, 14, true });
    Flush();

    auto added = builder->FatEntry(directory);
    EXPECT_EQ((fs::FatEntryPosition{ directory, 14 }), setWriter.Written().first);
    EXPECT_EQ((fs::FatEntryPosition{ added, 0 }), setWriter.Written().shortEntry);
    EXPECT_EQ(13u, List(directory).names.size());
    EXPECT_EQ("Long File Name.txt", List(directory).names.back());
}

TEST_F(FatEntrySetWriterTest, existing_cluster_after_the_end_holds_the_rest_of_the_set)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 12);
    auto spare = builder->AllocateChain(1);
    builder->SetFatEntry(directory, spare);
    state.freeClusters = 100;

    ExpectWrite("Long File Name.txt", Entry("LONGFI~1TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 14 }, 14, true });
    Flush();

    EXPECT_EQ((fs::FatEntryPosition{ spare, 0 }), setWriter.Written().shortEntry);
    EXPECT_EQ(fs::EndOfChain(geometry.type), builder->FatEntry(spare));
    EXPECT_EQ(100u, state.freeClusters);
    EXPECT_EQ("Long File Name.txt", List(directory).names.back());
}

TEST_F(FatEntrySetWriterTest, longest_name_spans_three_clusters)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 13);
    std::array<char, 255> name;
    name.fill('n');

    ExpectWrite(infra::BoundedConstString(name.data(), name.size()), Entry("NNNNNN~1   "), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 15 }, 15, true });
    Flush();

    auto second = builder->FatEntry(directory);
    auto third = builder->FatEntry(second);
    EXPECT_EQ(21, setWriter.Written().slots);
    EXPECT_EQ((fs::FatEntryPosition{ third, 3 }), setWriter.Written().shortEntry);
    EXPECT_EQ(fs::EndOfChain(geometry.type), builder->FatEntry(third));
    EXPECT_EQ(infra::BoundedConstString(name.data(), name.size()), List(directory).names.back());
}

TEST_F(FatEntrySetWriterWithLargeClustersTest, every_sector_of_a_new_cluster_is_zeroed)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 62);
    auto next = directory + 1;
    std::array<uint8_t, 512> stale;
    stale.fill(0x41);
    for (uint32_t sector = 0; sector != 4; ++sector)
        device.WriteBlock(geometry.SectorOfCluster(next) + sector, infra::MakeRange(stale));

    ExpectWrite("", Entry("README  TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 64 }, 64, true });
    Flush();

    ASSERT_EQ(next, builder->FatEntry(directory));
    for (uint32_t sector = 1; sector != 4; ++sector)
    {
        auto contents = builder->ReadSector(geometry.SectorOfCluster(next) + sector);
        EXPECT_TRUE(std::all_of(contents.begin(), contents.end(), [](uint8_t byte)
            {
                return byte == 0;
            }));
    }
    EXPECT_EQ(63u, List(directory).names.size());
}

TEST_F(FatEntrySetWriterTest, directory_cannot_hold_more_than_65536_entries)
{
    auto directory = FullDirectory();
    auto reads = device.ReadRequests();

    ExpectWrite("Long File Name.txt", Entry("LONGFI~1TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 16 }, 65534, true }, fs::Result::noSpace);

    EXPECT_EQ(reads, device.ReadRequests());
}

TEST_F(FatEntrySetWriterOnFat12Test, full_volume_cannot_grow_a_directory)
{
    auto directory = FullDirectory();
    for (uint32_t cluster = 2; cluster != geometry.clusterCount + 2; ++cluster)
        if (builder->FatEntry(cluster) == 0)
            builder->SetFatEntry(cluster, fs::EndOfChain(geometry.type));

    ExpectWrite("", Entry("README  TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 16 }, 16, true }, fs::Result::noSpace);

    EXPECT_EQ(fs::EndOfChain(geometry.type), builder->FatEntry(directory));
}

TEST_F(FatEntrySetWriterTest, device_error_while_writing_an_entry_is_reported)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectWrite("", Entry("README  TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ 0, 0 }, 0, false }, fs::Result::deviceError);
}

TEST_F(FatEntrySetWriterTest, device_error_while_reading_the_chain_is_reported)
{
    auto directory = builder->AddDirectory(0, "", "DIR        ");
    AddFiles(directory, 12);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    ExpectWrite("Long File Name.txt", Entry("LONGFI~1TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 14 }, 14, true }, fs::Result::deviceError);
}

TEST_F(FatEntrySetWriterTest, failed_write_while_zeroing_a_new_cluster_is_reported)
{
    auto directory = FullDirectory();
    for (uint32_t sector = 0; sector != 4; ++sector)
    {
        cache.Modify(geometry.firstDataSector + 100 + 2 * sector, 2, [](fs::Result, infra::ByteRange) {});
        ExecuteAllActions();
    }
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 2);

    ExpectWrite("", Entry("README  TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 16 }, 16, true }, fs::Result::deviceError);
}

TEST_F(FatEntrySetWriterTest, failed_link_of_a_new_cluster_is_reported)
{
    auto directory = FullDirectory();
    for (uint32_t cluster = directory + 1; cluster != 301; ++cluster)
        builder->SetFatEntry(cluster, fs::EndOfChain(geometry.type));
    device.FailNextOperationWith(hal::BlockDevice::Result::timeout, 3);

    ExpectWrite("", Entry("README  TXT"), fs::FatFreeSlots{ fs::FatEntryPosition{ directory, 16 }, 16, true }, fs::Result::deviceError);

    EXPECT_EQ(fs::EndOfChain(geometry.type), builder->FatEntry(directory));
}
