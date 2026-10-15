#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/test/NorMapsFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"

namespace
{
    class NorMapFetchTest
        : public testing::Test
        , public fs::NorMapsFixture
    {
    public:
        NorMapFetchTest()
        {
            AddInodes(1, 5);
            AddEntries(1, 10, 80);
            MapBlocks(2, { 0, 20 });
            WriteBack();
            directory = MapRootOf(1);
            file = MapRootOf(2);
            cache.Clear();
        }

        void StartFetch(fs::NorMapFetch& with, const fs::NorMapId& map, const fs::NorMapRoot& root, uint32_t key)
        {
            with.Fetch(map, root, key, [this](fs::NorFetchResult result)
                {
                    done.callback(result);
                });
        }

        void ExpectFetch(const fs::NorMapId& map, const fs::NorMapRoot& root, uint32_t key, fs::NorFetchResult result)
        {
            EXPECT_CALL(done, callback(result));
            StartFetch(fetch, map, root, key);
            ExecuteAllActions();
            testing::Mock::VerifyAndClearExpectations(&done);
        }

        void ExpectFetchAtOnce(const fs::NorMapId& map, const fs::NorMapRoot& root, uint32_t key, fs::NorFetchResult result)
        {
            EXPECT_CALL(done, callback(result));
            StartFetch(fetch, map, root, key);
            testing::Mock::VerifyAndClearExpectations(&done);
        }

        uint32_t StoreNode(const fs::NorNodeHeader& header, uint32_t slot, uint32_t value)
        {
            std::array<uint8_t, nodeSize> node{};
            fs::StoreLittle32(fs::NorNodeEntries(infra::MakeRange(node)), slot * 4, value);
            fs::SealNorNode(header, infra::MakeRange(node));
            auto address = nextAddress;
            Program(address, infra::MakeRange(node));
            nextAddress += nodeSize;
            return address;
        }

        fs::NorMapFetch fetch{ cache };
        fs::NorMapRoot directory;
        fs::NorMapRoot file;
        testing::StrictMock<infra::MockCallback<void(fs::NorFetchResult)>> done;
    };
}

TEST_F(NorMapFetchTest, brings_the_path_of_a_key_into_the_cache)
{
    ExpectFetch(inodeTable, inodeTableRoot, 4, fs::NorFetchResult::fetched);
    ExpectFetch(DirectoryMap(1), directory, 70, fs::NorFetchResult::fetched);
    ExpectFetch(DataMap(2), file, 20, fs::NorFetchResult::fetched);

    EXPECT_EQ(4, inodes.Find(inodeTableRoot, 4).value_or(infra::ConstByteRange())[0]);
    EXPECT_EQ(71u, fs::LoadLittle32(fs::NorBTree(cache, DirectoryMap(1)).Find(directory, 70).value_or(infra::ConstByteRange()), 0));
    EXPECT_EQ(120u, fs::NorRadixTree(cache, 2).Find(file, 20));
}

TEST_F(NorMapFetchTest, brings_the_path_of_a_key_down_to_a_level_into_the_cache)
{
    ASSERT_EQ(2, directory.height);

    EXPECT_CALL(done, callback(fs::NorFetchResult::fetched));
    fetch.Fetch(DirectoryMap(1), directory, 70, 1, [this](fs::NorFetchResult result)
        {
            done.callback(result);
        });
    ExecuteAllActions();

    auto root = cache.Find(directory.address);
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(std::nullopt, cache.Find(fs::NorBTree::ChildFor(cache.Node(root.value_or(0)), 70)));
}

TEST_F(NorMapFetchTest, completes_through_the_event_dispatcher_when_it_reads)
{
    StartFetch(fetch, inodeTable, inodeTableRoot, 4);

    EXPECT_CALL(done, callback(fs::NorFetchResult::fetched));
    ExecuteAllActions();
}

TEST_F(NorMapFetchTest, completes_at_once_without_reading_when_the_path_is_cached)
{
    ExpectFetch(DirectoryMap(1), directory, 10, fs::NorFetchResult::fetched);
    cache.UnpinAll();
    for (uint32_t address = 2 * 4096; address != nextAddress; address += nodeSize)
        Corrupt(address);

    ExpectFetchAtOnce(DirectoryMap(1), directory, 10, fs::NorFetchResult::fetched);
}

TEST_F(NorMapFetchTest, a_data_map_completes_at_once_when_its_path_is_cached)
{
    ExpectFetch(DataMap(2), file, 20, fs::NorFetchResult::fetched);

    ExpectFetchAtOnce(DataMap(2), file, 20, fs::NorFetchResult::fetched);
}

TEST_F(NorMapFetchTest, an_empty_map_or_an_index_beyond_a_data_map_fetches_nothing)
{
    ExpectFetchAtOnce(DirectoryMap(3), fs::NorMapRoot{}, 10, fs::NorFetchResult::fetched);
    ExpectFetchAtOnce(DataMap(2), file, 144, fs::NorFetchResult::fetched);
}

TEST_F(NorMapFetchTest, a_root_without_a_height_a_height_without_a_root_or_more_than_3_levels_are_corrupted)
{
    ExpectFetchAtOnce(DirectoryMap(1), fs::NorMapRoot{ directory.address, 0 }, 10, fs::NorFetchResult::corrupted);
    ExpectFetchAtOnce(DirectoryMap(1), fs::NorMapRoot{ 0, 1 }, 10, fs::NorFetchResult::corrupted);
    ExpectFetchAtOnce(DirectoryMap(1), fs::NorMapRoot{ directory.address, 4 }, 10, fs::NorFetchResult::corrupted);
    ExpectFetchAtOnce(DataMap(2), fs::NorMapRoot{ 0, 2 }, 0, fs::NorFetchResult::corrupted);
}

TEST_F(NorMapFetchTest, a_hole_in_a_data_map_ends_the_path)
{
    ExpectFetch(DataMap(2), file, 30, fs::NorFetchResult::fetched);

    EXPECT_EQ(0u, fs::NorRadixTree(cache, 2).Find(file, 30));
}

TEST_F(NorMapFetchTest, pinned_nodes_stay_until_the_cache_is_full)
{
    std::array<fs::NorNodeCache::Slot, 3> fewSlots{};
    std::array<uint8_t, 3 * nodeSize> fewBuffers{};
    fs::NorNodeCache small{ flash, infra::MakeRange(fewSlots), infra::MakeRange(fewBuffers) };
    fs::NorMapFetch smallFetch{ small };

    StartFetch(smallFetch, inodeTable, inodeTableRoot, 1);
    EXPECT_CALL(done, callback(fs::NorFetchResult::fetched)).Times(2);
    ExecuteAllActions();
    StartFetch(smallFetch, inodeTable, inodeTableRoot, 3);
    ExecuteAllActions();
    EXPECT_FALSE(small.HasRoom());

    EXPECT_CALL(done, callback(fs::NorFetchResult::cacheFull));
    StartFetch(smallFetch, inodeTable, inodeTableRoot, 5);
}

TEST_F(NorMapFetchTest, a_node_with_a_bad_crc_is_corrupted)
{
    Corrupt(directory.address);

    ExpectFetch(DirectoryMap(1), directory, 10, fs::NorFetchResult::corrupted);
}

TEST_F(NorMapFetchTest, a_node_of_another_map_is_corrupted)
{
    ExpectFetch(DirectoryMap(2), directory, 10, fs::NorFetchResult::corrupted);
    ExpectFetchAtOnce(DataMap(1), directory, 10, fs::NorFetchResult::corrupted);
    ExpectFetchAtOnce(DirectoryMap(1), fs::NorMapRoot{ directory.address, 1 }, 10, fs::NorFetchResult::corrupted);
}

TEST_F(NorMapFetchTest, a_cached_node_of_another_map_is_corrupted)
{
    ExpectFetch(DirectoryMap(1), directory, 10, fs::NorFetchResult::fetched);

    ExpectFetchAtOnce(DirectoryMap(2), directory, 10, fs::NorFetchResult::corrupted);
}

TEST_F(NorMapFetchTest, a_b_tree_node_with_more_entries_than_fit_is_corrupted)
{
    auto leaf = StoreNode(fs::NorNodeHeader{ fs::NorMap::directory, 0, 7, 3, 0 }, 0, 1);

    ExpectFetch(DirectoryMap(3), fs::NorMapRoot{ leaf, 1 }, 0, fs::NorFetchResult::corrupted);
}

TEST_F(NorMapFetchTest, a_radix_node_that_covers_other_indexes_is_corrupted)
{
    auto leaf = StoreNode(fs::NorNodeHeader{ fs::NorMap::fileData, 0, 1, 3, 12 }, 0, 100);

    ExpectFetch(DataMap(3), fs::NorMapRoot{ leaf, 1 }, 0, fs::NorFetchResult::corrupted);
}

TEST_F(NorMapFetchTest, a_radix_node_that_miscounts_its_slots_is_corrupted)
{
    auto leaf = StoreNode(fs::NorNodeHeader{ fs::NorMap::fileData, 0, 2, 3, 0 }, 0, 100);

    ExpectFetch(DataMap(3), fs::NorMapRoot{ leaf, 1 }, 0, fs::NorFetchResult::corrupted);
}

TEST_F(NorMapFetchTest, a_tree_written_back_and_fetched_again_holds_its_contents)
{
    ExpectFetch(DirectoryMap(1), directory, 15, fs::NorFetchResult::fetched);
    ExpectFetch(inodeTable, inodeTableRoot, 1, fs::NorFetchResult::fetched);
    AddEntries(1, 15, 15);
    WriteBack();
    cache.Clear();

    ExpectFetch(inodeTable, inodeTableRoot, 1, fs::NorFetchResult::fetched);
    auto map = MapRootOf(1);
    for (uint32_t key : { 10, 15, 20, 80 })
    {
        ExpectFetch(DirectoryMap(1), map, key, fs::NorFetchResult::fetched);
        EXPECT_EQ(key + 1, fs::LoadLittle32(fs::NorBTree(cache, DirectoryMap(1)).Find(map, key).value_or(infra::ConstByteRange()), 0)) << key;
    }
}
