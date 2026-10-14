#include "fs/nor/test/NorMapsFixture.hpp"
#include "gtest/gtest.h"
#include <utility>
#include <vector>

namespace
{
    class NorNodeWriteBackTest
        : public testing::Test
        , public fs::NorMapsFixture
    {
    public:
        NorNodeWriteBackTest()
        {
            AddInodes(1, 5);
            AddEntries(1, 10, 80);
            MapBlocks(2, { 0, 20 });
        }

        uint32_t AddressOfModified(fs::NorMap map, uint8_t level) const
        {
            for (std::size_t slot = 0; slot != cache.Capacity(); ++slot)
            {
                auto header = fs::ReadNorNodeHeader(cache.Node(slot));
                if (cache.IsModified(slot) && header.map == map && header.level == level)
                    return cache.AddressOf(slot);
            }

            return 0;
        }
    };
}

TEST_F(NorNodeWriteBackTest, nothing_is_written_when_no_node_is_modified)
{
    WriteBack();

    EXPECT_EQ(std::nullopt, writeBack.Next());
    EXPECT_EQ(0u, cache.ModifiedNodes());
}

TEST_F(NorNodeWriteBackTest, writes_leaves_before_inner_nodes_and_every_other_map_before_the_inode_table)
{
    ASSERT_EQ(2, inodeTableRoot.height);
    ASSERT_EQ(2, MapRootOf(1).height);
    ASSERT_EQ(2, MapRootOf(2).height);

    auto written = WriteBack();

    ASSERT_EQ(10u, written.size());
    EXPECT_TRUE(std::ranges::is_sorted(written, {}, [](const fs::NorNodeHeader& header)
        {
            return std::make_pair(header.map == fs::NorMap::inodeTable, header.level);
        }));
    EXPECT_EQ(fs::NorMap::inodeTable, written.back().map);
    EXPECT_EQ(nextAddress - nodeSize, inodeTableRoot.address);
}

TEST_F(NorNodeWriteBackTest, patches_the_parent_among_nodes_of_several_maps_at_the_same_level)
{
    AddEntries(1, 90, 370);
    AddEntries(3, 10, 80);
    MapBlocks(2, { 150 });
    MapBlocks(4, { 0, 20 });
    ASSERT_EQ(3, MapRootOf(1).height);
    ASSERT_EQ(3, MapRootOf(2).height);

    WriteBack();

    fs::NorBTree entries{ cache, DirectoryMap(1) };
    for (uint32_t key = 10; key <= 370; key += 10)
        EXPECT_EQ(key + 1, fs::LoadLittle32(entries.Find(MapRootOf(1), key).value_or(infra::ConstByteRange()), 0)) << key;
    EXPECT_EQ(81u, fs::LoadLittle32(fs::NorBTree(cache, DirectoryMap(3)).Find(MapRootOf(3), 80).value_or(infra::ConstByteRange()), 0));
    fs::NorRadixTree blocks{ cache, 2 };
    for (uint32_t index : { 0, 20, 150 })
        EXPECT_EQ(index + 100, blocks.Find(MapRootOf(2), index)) << index;
    EXPECT_EQ(120u, fs::NorRadixTree(cache, 4).Find(MapRootOf(4), 20));
}

TEST_F(NorNodeWriteBackTest, a_sealed_node_opens)
{
    auto slot = writeBack.Next();
    ASSERT_TRUE(slot.has_value());

    auto header = fs::ReadNorNodeHeader(cache.Node(*slot));
    EXPECT_EQ(header, fs::OpenNorNode(writeBack.Seal(*slot)));
}

TEST_F(NorNodeWriteBackTest, the_parent_of_a_written_node_refers_to_its_new_address)
{
    auto slot = writeBack.Next();
    ASSERT_TRUE(slot.has_value());
    auto header = fs::ReadNorNodeHeader(cache.Node(*slot));
    auto parent = cache.Find(AddressOfModified(header.map, 1));
    ASSERT_TRUE(parent.has_value());

    writeBack.Seal(*slot);
    writeBack.Written(*slot, 3 * 4096, inodeTableRoot);

    auto node = cache.Node(*parent);
    auto child = header.map == fs::NorMap::fileData ? fs::NorRadixTree::ChildFor(node, header.lowestKey) : fs::NorBTree::ChildFor(node, header.lowestKey);
    EXPECT_EQ(3u * 4096, child);
    EXPECT_FALSE(cache.IsModified(*slot));
}

TEST_F(NorNodeWriteBackTest, the_inode_record_of_a_map_refers_to_the_new_address_of_its_root)
{
    WriteBack();

    auto directory = MapRootOf(1);
    auto file = MapRootOf(2);
    EXPECT_EQ(0u, directory.address & fs::norModifiedNodeTag);
    EXPECT_EQ(0u, file.address & fs::norModifiedNodeTag);
    EXPECT_EQ(71u, fs::LoadLittle32(fs::NorBTree(cache, DirectoryMap(1)).Find(directory, 70).value_or(infra::ConstByteRange()), 0));
    EXPECT_EQ(120u, fs::NorRadixTree(cache, 2).Find(file, 20));
}

TEST_F(NorNodeWriteBackTest, written_nodes_are_found_at_their_new_addresses)
{
    WriteBack();

    for (uint32_t inode = 1; inode <= 5; ++inode)
        EXPECT_EQ(inode, inodes.Find(inodeTableRoot, inode).value_or(infra::ConstByteRange())[0]);
    EXPECT_EQ(0u, inodeTableRoot.address & fs::norModifiedNodeTag);
}

TEST_F(NorNodeWriteBackTest, a_second_write_back_writes_only_the_nodes_modified_since)
{
    WriteBack();

    AddEntries(1, 15, 15);
    auto written = WriteBack();

    std::vector<fs::NorMap> maps;
    for (const auto& header : written)
        maps.push_back(header.map);
    EXPECT_EQ((std::vector<fs::NorMap>{ fs::NorMap::directory, fs::NorMap::directory, fs::NorMap::directory, fs::NorMap::inodeTable, fs::NorMap::inodeTable }), maps);
    EXPECT_EQ(16u, fs::LoadLittle32(fs::NorBTree(cache, DirectoryMap(1)).Find(MapRootOf(1), 15).value_or(infra::ConstByteRange()), 0));
}

TEST_F(NorNodeWriteBackTest, a_written_tree_counts_the_nodes_that_a_change_copies)
{
    WriteBack();
    fs::NorBTree entries{ cache, DirectoryMap(1) };
    fs::NorRadixTree blocks{ cache, 2 };

    EXPECT_EQ(2u, inodes.NodesModifiedByChange(inodeTableRoot, 3));
    EXPECT_EQ(std::optional<uint32_t>(3), entries.NodesModifiedByInsert(MapRootOf(1), 15));
    EXPECT_EQ(std::optional<uint32_t>(2), entries.NodesModifiedByInsert(MapRootOf(1), 85));
    EXPECT_EQ(std::optional<uint32_t>(2), blocks.NodesModifiedBySet(MapRootOf(2), 1));
    EXPECT_EQ(std::optional<uint32_t>(2), blocks.NodesModifiedBySet(MapRootOf(2), 30));
    EXPECT_EQ(2u, blocks.NodesModifiedByClear(MapRootOf(2), 20));
}

TEST_F(NorNodeWriteBackTest, changing_a_written_tree_copies_its_path_and_releases_the_old_nodes)
{
    WriteBack();
    cache.ClearReleasedNodes();
    auto before = inodeTableRoot;

    inodes.Modify(inodeTableRoot, 3)[0] = 33;

    EXPECT_NE(before.address, inodeTableRoot.address);
    EXPECT_EQ(fs::norModifiedNodeTag, inodeTableRoot.address & fs::norModifiedNodeTag);
    EXPECT_EQ(2u, cache.ModifiedNodes());
    EXPECT_EQ(2u, cache.ReleasedNodes());
    EXPECT_EQ(33, inodes.Find(inodeTableRoot, 3).value_or(infra::ConstByteRange())[0]);
    EXPECT_EQ(4, inodes.Find(inodeTableRoot, 4).value_or(infra::ConstByteRange())[0]);
}

TEST_F(NorNodeWriteBackTest, clearing_a_written_data_map_copies_its_path)
{
    WriteBack();
    cache.ClearReleasedNodes();
    auto map = MapRootOf(2);
    fs::NorRadixTree blocks{ cache, 2 };

    blocks.Clear(map, 0);

    EXPECT_EQ(2, map.height);
    EXPECT_EQ(0u, blocks.Find(map, 0));
    EXPECT_EQ(120u, blocks.Find(map, 20));
    EXPECT_EQ(1u, cache.ModifiedNodes());
    EXPECT_EQ(2u, cache.ReleasedNodes());
}
