#include "fs/fat/FatTable.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace
{
    class FatMakeDirectoryTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatMakeDirectoryTest(uint32_t blocks = 16384, const fs::FatFormatConfig& config = ClusterSize(1))
            : FatFileSystemFixture(blocks, config)
        {}

        void SetUp() override
        {
            builder->AddFile(0, "notes.txt", "NOTES   TXT", infra::ConstByteRange());
            ExpectMount();
        }

        void ExpectMakeDirectory(infra::BoundedConstString path, fs::Result result = fs::Result::success)
        {
            volume.MakeDirectory(path, [this](fs::Result result)
                {
                    done.callback(result);
                });
            ExpectDone(result);
        }

        fs::ShortEntry RawEntry(fs::FatEntryPosition position) const
        {
            auto sector = builder->ReadSector(builder->SectorOf(position));
            return fs::DecodeShortEntry(infra::Head(infra::DiscardHead(infra::MakeRange(sector), position.index % 16 * 32), 32));
        }

        uint32_t ClusterOf(fs::FatEntryPosition position) const
        {
            return RawEntry(position).firstCluster;
        }
    };

    class FatMakeDirectoryWithLargeClustersTest
        : public FatMakeDirectoryTest
    {
    public:
        FatMakeDirectoryWithLargeClustersTest()
            : FatMakeDirectoryTest(16384, ClusterSize(4))
        {}
    };

    class FatMakeDirectoryOnFat32Test
        : public FatMakeDirectoryTest
    {
    public:
        FatMakeDirectoryOnFat32Test()
            : FatMakeDirectoryTest(81920)
        {}
    };

    using Request = fs::SparseBlockDevice::Request;
}

TEST_F(FatMakeDirectoryTest, new_directory_is_empty)
{
    ExpectMakeDirectory("/logs");

    ExpectStat("/logs", fs::Result::success, fs::EntryInfo{ "logs", fs::EntryType::directory, 0 });
    auto directory = ExpectOpenDirectory("/LOGS");
    ExpectNext(*directory, fs::Result::success);
}

TEST_F(FatMakeDirectoryTest, new_directory_holds_its_dot_entries)
{
    ExpectMakeDirectory("/logs");
    ExpectUnmount();

    auto entry = RawEntry(fs::FatEntryPosition{ 0, 2 });
    EXPECT_EQ(fs::FatAttributes::directory, entry.attributes);
    EXPECT_EQ(fs::EndOfChain(builder->Geometry().type), builder->FatEntry(entry.firstCluster));
    auto dot = RawEntry(fs::FatEntryPosition{ entry.firstCluster, 0 });
    auto dotDot = RawEntry(fs::FatEntryPosition{ entry.firstCluster, 1 });
    EXPECT_EQ((fs::ShortNameBytes{ '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }), dot.name.bytes);
    EXPECT_EQ(fs::FatAttributes::directory, dot.attributes);
    EXPECT_EQ(entry.firstCluster, dot.firstCluster);
    EXPECT_EQ((fs::ShortNameBytes{ '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }), dotDot.name.bytes);
    EXPECT_EQ(0u, dotDot.firstCluster);
    EXPECT_EQ(fs::EntryKind::end, fs::KindOf(infra::Head(infra::DiscardHead(infra::MakeRange(builder->ReadSector(builder->Geometry().SectorOfCluster(entry.firstCluster))), 64), 32)));
}

TEST_F(FatMakeDirectoryTest, nested_directory_refers_to_its_parent)
{
    ExpectMakeDirectory("/a");
    ExpectMakeDirectory("/a/b");
    ExpectUnmount();

    auto parent = ClusterOf(fs::FatEntryPosition{ 0, 2 });
    auto child = ClusterOf(fs::FatEntryPosition{ parent, 2 });
    EXPECT_EQ(parent, ClusterOf(fs::FatEntryPosition{ child, 1 }));
}

TEST_F(FatMakeDirectoryTest, files_are_created_in_a_new_directory)
{
    ExpectMakeDirectory("/My Documents");
    ExpectOpen("/my documents/report.txt", fs::OpenMode::write);

    ExpectStat("/MYDOCU~1/REPORT.TXT", fs::Result::success, fs::EntryInfo{ "report.txt", fs::EntryType::file, 0 });
}

TEST_F(FatMakeDirectoryTest, existing_names_already_exist)
{
    ExpectMakeDirectory("/logs");

    ExpectMakeDirectory("/LOGS", fs::Result::alreadyExists);
    ExpectMakeDirectory("/notes.txt", fs::Result::alreadyExists);
    ExpectMakeDirectory("/", fs::Result::alreadyExists);
}

TEST_F(FatMakeDirectoryTest, parent_must_be_an_existing_directory)
{
    ExpectMakeDirectory("/missing/logs", fs::Result::notFound);
    ExpectMakeDirectory("/notes.txt/logs", fs::Result::notADirectory);
    ExpectMakeDirectory("/a|b", fs::Result::invalidPath);
}

TEST_F(FatMakeDirectoryTest, new_cluster_and_its_fat_entry_are_written_before_the_entry_in_the_parent_and_flushed)
{
    infra::BoundedVector<std::pair<Request, uint32_t>>::WithMaxSize<16> requests;
    device.Observe([&requests](Request request, uint32_t block)
        {
            if (request != Request::read)
                requests.emplace_back(request, block);
        });

    ExpectMakeDirectory("/logs");

    auto cluster = ClusterOf(fs::FatEntryPosition{ 0, 2 });
    const auto& geometry = builder->Geometry();
    ASSERT_EQ(7u, requests.size());
    EXPECT_EQ(std::make_pair(Request::write, geometry.SectorOfCluster(cluster)), requests[0]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[1]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector), requests[2]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.firstFatSector + geometry.sectorsPerFat), requests[3]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[4]);
    EXPECT_EQ(std::make_pair(Request::write, geometry.rootSector), requests[5]);
    EXPECT_EQ(std::make_pair(Request::flush, 0u), requests[6]);
}

TEST_F(FatMakeDirectoryWithLargeClustersTest, every_sector_of_the_new_cluster_is_zeroed)
{
    std::array<uint8_t, 512> stale;
    stale.fill(0x41);
    for (uint32_t sector = 0; sector != 4; ++sector)
        device.WriteBlock(builder->Geometry().SectorOfCluster(2) + sector, infra::MakeRange(stale));

    ExpectMakeDirectory("/logs");
    ExpectUnmount();

    ASSERT_EQ(2u, ClusterOf(fs::FatEntryPosition{ 0, 2 }));
    auto first = builder->ReadSector(builder->Geometry().SectorOfCluster(2));
    EXPECT_TRUE(std::all_of(first.begin() + 64, first.end(), [](uint8_t byte)
        {
            return byte == 0;
        }));
    for (uint32_t sector = 1; sector != 4; ++sector)
    {
        auto contents = builder->ReadSector(builder->Geometry().SectorOfCluster(2) + sector);
        EXPECT_TRUE(std::all_of(contents.begin(), contents.end(), [](uint8_t byte)
            {
                return byte == 0;
            }));
    }
}

TEST_F(FatMakeDirectoryTest, full_volume_has_no_space_for_a_directory)
{
    for (uint32_t cluster = 2; cluster != builder->Geometry().clusterCount + 2; ++cluster)
        builder->SetFatEntry(cluster, fs::EndOfChain(builder->Geometry().type));

    ExpectMakeDirectory("/logs", fs::Result::noSpace);

    ExpectStat("/logs", fs::Result::notFound);
}

TEST_F(FatMakeDirectoryTest, full_parent_on_a_volume_with_one_free_cluster_has_no_space)
{
    ExpectMakeDirectory("/logs");
    auto logs = ClusterOf(fs::FatEntryPosition{ 0, 2 });
    for (uint32_t index = 0; index != 14; ++index)
    {
        char name[12] = "F0000000TXT";
        name[6] = static_cast<char>('0' + index / 10);
        name[7] = static_cast<char>('0' + index % 10);
        builder->AddFile(logs, "", name, infra::ConstByteRange());
    }
    ExpectUnmount();
    for (uint32_t cluster = 2; cluster != builder->Geometry().clusterCount + 1; ++cluster)
        if (builder->FatEntry(cluster) == 0)
            builder->SetFatEntry(cluster, fs::EndOfChain(builder->Geometry().type));
    ExpectMount();

    ExpectMakeDirectory("/logs/inner", fs::Result::noSpace);
}

TEST_F(FatMakeDirectoryTest, full_root_takes_no_cluster)
{
    for (uint32_t index = 0; index != 510; ++index)
    {
        char name[12] = "F0000000TXT";
        name[5] = static_cast<char>('0' + index / 100);
        name[6] = static_cast<char>('0' + index / 10 % 10);
        name[7] = static_cast<char>('0' + index % 10);
        builder->AddFile(0, "", name, infra::ConstByteRange());
    }

    ExpectMakeDirectory("/logs", fs::Result::noSpace);
    ExpectUnmount();

    EXPECT_EQ(0u, builder->FatEntry(2));
}

namespace
{
    class ExposedFileSystem
        : public fs::FatFileSystem::WithStorage<2, 4>
    {
    public:
        using fs::FatFileSystem::WithStorage<2, 4>::WithStorage;
        using fs::FatFileSystem::Volume;
    };

    class FatMakeDirectoryWithFullCacheTest
        : public FatFileSystemFixture
    {
    public:
        void SetUp() override
        {
            exposed.Mount([this](fs::Result result)
                {
                    done.callback(result);
                });
            ExpectDone(fs::Result::success);
        }

        void Dirty(uint32_t sector, uint8_t rank)
        {
            exposed.Volume().Cache().Modify(sector, rank, [](fs::Result, infra::ByteRange) {});
            ExecuteAllActions();
        }

        ExposedFileSystem exposed{ device };
    };
}

TEST_F(FatMakeDirectoryWithFullCacheTest, failure_to_make_room_for_the_new_cluster_is_reported)
{
    const auto& geometry = builder->Geometry();
    Dirty(geometry.SectorOfCluster(100), 2);
    Dirty(geometry.SectorOfCluster(101), 2);
    Dirty(geometry.rootSector, 2);
    Dirty(geometry.firstFatSector, 1);
    device.FailNextOperationWith(hal::BlockDevice::Result::crcError);

    exposed.MakeDirectory("/logs", [this](fs::Result result)
        {
            done.callback(result);
        });
    ExpectDone(fs::Result::deviceError);
}

TEST_F(FatMakeDirectoryOnFat32Test, directory_in_the_root_refers_to_cluster_0_as_its_parent)
{
    ExpectMakeDirectory("/logs");
    ExpectUnmount();

    auto cluster = ClusterOf(fs::FatEntryPosition{ builder->RootCluster(), 2 });
    EXPECT_EQ(0u, ClusterOf(fs::FatEntryPosition{ cluster, 1 }));
}

TEST_F(FatMakeDirectoryTest, invalid_path_is_rejected_before_it_is_queued)
{
    ExpectMakeDirectory("relative", fs::Result::invalidPath);
}

TEST_F(FatMakeDirectoryTest, make_directory_beyond_the_path_slots_is_busy)
{
    for (int call = 0; call != 3; ++call)
        volume.MakeDirectory("/missing/logs", [this](fs::Result result)
            {
                done.callback(result);
            });

    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::notFound)).Times(2);
    ExecuteAllActions();
}

TEST_F(FatMakeDirectoryTest, make_directory_waiting_behind_a_removal_reports_the_removal)
{
    device.FailNextOperationWith(hal::BlockDevice::Result::notPresent);

    StartStat("/first");
    volume.MakeDirectory("/logs", [this](fs::Result result)
        {
            done.callback(result);
        });

    EXPECT_CALL(statDone, callback(fs::Result::mediaRemoved, fs::EntryInfo{}));
    EXPECT_CALL(done, callback(fs::Result::mediaRemoved));
    ExecuteAllActions();
}
