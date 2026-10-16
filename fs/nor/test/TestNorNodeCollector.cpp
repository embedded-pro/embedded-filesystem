#include "fs/nor/test/NorFilesFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace
{
    using fs::Pattern;

    constexpr fs::NorConfig config{ .maxBlocks = 48, .cachedNodes = 15 };
    constexpr uint32_t payload = 4096 - 36;
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    class NorNodeCollectorTest
        : public fs::NorFilesFixture<config, 48, 4096>
    {
    public:
        using Blocks = infra::BoundedVector<uint32_t>::WithMaxSize<48>;

        fs::Result WriteCheckpoint()
        {
            std::optional<fs::Result> written;
            Volume().Checkpoints().Write([&written](fs::Result result)
                {
                    written = result;
                });
            ExecuteAllActions();
            return written.value_or(fs::Result::busy);
        }

        Blocks NodeBlocks()
        {
            Blocks blocks;
            for (uint32_t block = 0; block != 48; ++block)
                if (State().nodeBlocks.Contains(block))
                    blocks.push_back(block);

            return blocks;
        }

        void ExpectFreed(uint32_t block)
        {
            EXPECT_FALSE(State().nodeBlocks.Contains(block)) << block;
            EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(block)) << block;
        }

        void ExpectFreed(const Blocks& blocks)
        {
            for (auto block : blocks)
                ExpectFreed(block);
        }

        void ExpectFreed(std::initializer_list<uint32_t> blocks)
        {
            for (auto block : blocks)
                ExpectFreed(block);
        }

        void RemountWithFreeBlocksErased()
        {
            Unmount();
            for (uint32_t block = 2; block != 48; ++block)
                if (State().blocks.State(block) == fs::NorBlockState::freeNotErased)
                    Builder().EraseBlock(block);
            Mount();
        }

        uint32_t ChildOf(uint32_t node, uint32_t key)
        {
            std::array<uint8_t, 512> bytes{};
            Builder().Read(node, infra::MakeRange(bytes));
            return fs::NorBTree::ChildFor(infra::MakeRange(bytes), key);
        }

        Blocks UsedNodesIn(uint32_t block)
        {
            const auto& geometry = Builder().Geometry();
            Blocks used;
            for (uint32_t slot = 0; slot != geometry.NodesPerBlock(); ++slot)
            {
                auto address = geometry.AddressOfBlock(block) + geometry.NodeSlotsOffset() + slot * geometry.nodeSize;
                std::array<uint8_t, 512> bytes{};
                Builder().Read(address, infra::MakeRange(bytes));
                auto header = fs::ReadNorNodeHeader(infra::MakeRange(bytes));
                if (!fs::IsNorNodeHeader(header))
                    continue;

                auto map = header.map == fs::NorMap::inodeTable ? Builder().InodeTable() : Builder().MapOf(header.owner);
                auto node = map.address;
                for (uint32_t level = map.height; level > header.level + 1u && node != 0; --level)
                    node = ChildOf(node, header.lowestKey);
                if (header.level < map.height && node == address)
                    used.push_back(address);
            }

            return used;
        }

        uint32_t LiveNodesIn(uint32_t block)
        {
            return static_cast<uint32_t>(UsedNodesIn(block).size());
        }

        void BuildBlocksOfZeroFiveThreeAndSixUsedNodes()
        {
            for (auto name : { "a", "b", "c", "d", "e", "f" })
                Builder().AddDirectory(Builder().AddDirectory(root, name), "x");
            for (auto name : { "g", "h", "i" })
                Builder().AddDirectory(root, name);
            Builder().Commit();
            Builder().AddDirectory(2, "y");
            Builder().AddDirectory(4, "z");
            Builder().Commit();
        }

        void AddDirectoriesWithAnEntry(uint32_t count)
        {
            for (uint32_t index = 0; index != count; ++index)
                Builder().AddDirectory(Builder().AddDirectory(root, NameOf(index)), "x");
        }

        void LeaveFreeBlocksInState(uint32_t free)
        {
            for (uint32_t block = 2; block != 48; ++block)
                if (State().blocks.State(block) == fs::NorBlockState::freeErased && free != 0)
                    --free;
                else if (State().blocks.State(block) == fs::NorBlockState::freeErased || State().blocks.State(block) == fs::NorBlockState::freeNotErased)
                    State().blocks.Set(block, fs::NorBlockState::inUse);
        }
    };
}

TEST_F(NorNodeCollectorTest, a_collection_is_due_when_two_node_blocks_or_more_could_hold_more_than_twice_the_live_nodes)
{
    Builder().AddDirectory(root, "a");
    Builder().Commit();
    Mount();

    EXPECT_EQ(2, State().liveNodes);
    EXPECT_EQ(2, Volume().Collector().CollectableBlocks());
    EXPECT_TRUE(Volume().Collector().Due());
}

TEST_F(NorNodeCollectorTest, no_collection_is_due_for_a_single_node_block_because_moving_its_nodes_frees_nothing)
{
    Mount();

    EXPECT_EQ(1, State().liveNodes);
    EXPECT_EQ(1, Volume().Collector().CollectableBlocks());
    EXPECT_FALSE(Volume().Collector().Due());
}

TEST_F(NorNodeCollectorTest, the_node_block_being_filled_does_not_count_for_a_collection)
{
    Builder().AddDirectory(root, "a");
    Builder().Commit();
    Mount();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/b"));
    ASSERT_EQ(fs::Result::success, WriteCheckpoint());

    EXPECT_EQ(1, State().nodeBlocks.Count());
    EXPECT_EQ(0, Volume().Collector().CollectableBlocks());
    EXPECT_FALSE(Volume().Collector().Due());
}

TEST_F(NorNodeCollectorTest, no_collection_is_due_while_the_node_blocks_could_hold_at_most_twice_the_live_nodes)
{
    for (auto name : { "a", "b", "c", "d", "e" })
        Builder().AddDirectory(root, name);
    Builder().AddDirectory(2, "x");
    Builder().AddDirectory(3, "x");
    Builder().Commit();
    Mount();

    EXPECT_EQ(7, State().liveNodes);
    EXPECT_EQ(2, State().nodeBlocks.Count());
    EXPECT_FALSE(Volume().Collector().Due());
}

TEST_F(NorNodeCollectorTest, a_collection_is_due_when_the_free_blocks_beyond_the_reserve_are_down_to_the_watermark_and_the_node_blocks_hold_a_block_of_unused_slots)
{
    for (auto name : { "a", "b", "c", "d", "e" })
        Builder().AddDirectory(root, name);
    Builder().AddDirectory(2, "x");
    Builder().AddDirectory(3, "x");
    Builder().Commit();
    Mount();
    ASSERT_EQ(7, State().liveNodes);
    ASSERT_EQ(2, Volume().Collector().CollectableBlocks());
    LeaveFreeBlocksInState(Volume().Geometry().ReserveBlocks() + Volume().Geometry().WatermarkBlocks());

    EXPECT_TRUE(Volume().Collector().DueForSpace());
    EXPECT_TRUE(Volume().Collector().Due());
}

TEST_F(NorNodeCollectorTest, no_collection_is_due_for_space_while_more_free_blocks_than_the_watermark_remain_beyond_the_reserve)
{
    for (auto name : { "a", "b", "c", "d", "e" })
        Builder().AddDirectory(root, name);
    Builder().AddDirectory(2, "x");
    Builder().AddDirectory(3, "x");
    Builder().Commit();
    Mount();
    LeaveFreeBlocksInState(Volume().Geometry().ReserveBlocks() + Volume().Geometry().WatermarkBlocks() + 1);

    EXPECT_FALSE(Volume().Collector().DueForSpace());
    EXPECT_FALSE(Volume().Collector().Due());
}

TEST_F(NorNodeCollectorTest, no_collection_is_due_for_space_while_the_node_blocks_hold_less_than_a_block_of_unused_slots)
{
    for (auto name : { "a", "b", "c", "d", "e", "f" })
        Builder().AddDirectory(Builder().AddDirectory(root, name), "x");
    for (auto name : { "g", "h", "i" })
        Builder().AddDirectory(root, name);
    Builder().Commit();
    Mount();
    ASSERT_EQ(14, State().liveNodes);
    ASSERT_EQ(3, Volume().Collector().CollectableBlocks());
    ASSERT_EQ(fs::Result::success, MakeDirectory("/g/y"));
    LeaveFreeBlocksInState(Volume().Geometry().ReserveBlocks() + Volume().Geometry().WatermarkBlocks());

    EXPECT_FALSE(Volume().Collector().DueForSpace());
    EXPECT_FALSE(Volume().Collector().Due());
}

TEST_F(NorNodeCollectorTest, a_checkpoint_frees_the_node_blocks_whose_nodes_are_no_longer_used)
{
    Builder().AddDirectory(root, "a");
    Builder().Commit();
    Mount();
    auto before = NodeBlocks();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/b"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(before);
    EXPECT_EQ(2, State().liveNodes);
    RemountWithFreeBlocksErased();
    EXPECT_THAT(List("/"), testing::UnorderedElementsAre("a", "b"));
}

TEST_F(NorNodeCollectorTest, a_checkpoint_writes_the_used_nodes_of_a_collected_block_elsewhere)
{
    auto a = Builder().AddDirectory(root, "a");
    Builder().AddDirectory(root, "b");
    Builder().AddDirectory(a, "x");
    Builder().Commit();
    Mount();
    auto before = NodeBlocks();
    ASSERT_EQ(2, before.size());
    ASSERT_EQ(fs::Result::success, MakeDirectory("/b/y"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(before);
    EXPECT_EQ(6, State().liveNodes);
    RemountWithFreeBlocksErased();
    EXPECT_EQ(6, State().liveNodes);
    EXPECT_THAT(List("/"), testing::UnorderedElementsAre("a", "b"));
    EXPECT_THAT(List("/a"), testing::ElementsAre("x"));
    EXPECT_THAT(List("/b"), testing::ElementsAre("y"));
}

TEST_F(NorNodeCollectorTest, a_used_node_of_a_data_map_moves_with_the_contents_of_its_file)
{
    static auto contents = Pattern<2 * payload + 100>();
    Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().AddDirectory(root, "d");
    Builder().Commit();
    Mount();
    auto before = NodeBlocks();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d/x"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(before);
    RemountWithFreeBlocksErased();
    ExpectContents("/f", contents);
    EXPECT_EQ(fs::EntryType::directory, Stat("/d/x").second.type);
}

TEST_F(NorNodeCollectorTest, a_used_node_of_a_data_map_of_two_levels_moves_with_its_path)
{
    static auto contents = Pattern<payload + 100>();
    auto file = Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().MapBlock(file, 200, 47);
    Builder().AddDirectory(root, "d");
    Builder().AddEmptyNodeBlock(30);
    Builder().AddEmptyNodeBlock(31);
    Builder().Commit();
    ASSERT_EQ(2, Builder().MapOf(file).height);
    Mount();
    auto first = BlockInVolume(file, 0);
    auto before = NodeBlocks();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/d/x"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(before);
    RemountWithFreeBlocksErased();
    EXPECT_EQ(first, BlockInVolume(file, 0));
    EXPECT_EQ(47, BlockInVolume(file, 200));
}

TEST_F(NorNodeCollectorTest, the_nodes_of_a_removed_file_are_no_longer_used)
{
    static auto contents = Pattern<2 * payload + 100>();
    Builder().AddFile(root, "f", infra::MakeRange(contents));
    Builder().AddDirectory(root, "d");
    Builder().Commit();
    Mount();
    auto before = NodeBlocks();
    ASSERT_EQ(fs::Result::success, Remove("/f"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(before);
    EXPECT_EQ(2, State().liveNodes);
    RemountWithFreeBlocksErased();
    EXPECT_THAT(List("/"), testing::ElementsAre("d"));
}

TEST_F(NorNodeCollectorTest, a_used_leaf_is_found_through_its_inner_node)
{
    for (auto name : { "a", "b", "c", "d", "e", "f", "g", "h" })
        Builder().AddDirectory(root, name);
    Builder().Commit();
    Mount();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/h/y"));
    ASSERT_EQ(fs::Result::success, WriteCheckpoint());
    RemountWithFreeBlocksErased();
    auto before = NodeBlocks();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/h/z"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(before);
    EXPECT_EQ(7, State().liveNodes);
    RemountWithFreeBlocksErased();
    EXPECT_THAT(List("/"), testing::UnorderedElementsAre("a", "b", "c", "d", "e", "f", "g", "h"));
    EXPECT_THAT(List("/h"), testing::UnorderedElementsAre("y", "z"));
}

TEST_F(NorNodeCollectorTest, a_step_passes_at_most_twice_the_node_blocks_of_a_checkpoint)
{
    for (uint32_t block = 10; block != 20; ++block)
        Builder().AddEmptyNodeBlock(block);
    Builder().Commit();
    Mount();
    ASSERT_EQ(11, State().nodeBlocks.Count());

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    EXPECT_EQ(6, Volume().Collector().LeftBlocks());
    ExpectFreed({ 2, 10, 11, 12, 13, 14 });
    for (uint32_t block = 15; block != 20; ++block)
        EXPECT_TRUE(State().nodeBlocks.Contains(block)) << block;
    RemountWithFreeBlocksErased();
    EXPECT_EQ(1, State().liveNodes);
}

TEST_F(NorNodeCollectorTest, a_step_stops_when_the_cache_has_no_room_and_the_next_checkpoint_continues_where_it_stopped)
{
    AddDirectoriesWithAnEntry(12);
    for (uint32_t block = 40; block != 44; ++block)
        Builder().AddEmptyNodeBlock(block);
    Builder().Commit();
    Mount();
    auto first = NodeBlocks()[1];
    ASSERT_EQ(23, State().liveNodes);

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(2);
    EXPECT_TRUE(State().nodeBlocks.Contains(first));
    EXPECT_EQ(1, Volume().Collector().LeftBlocks());

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(first);
    RemountWithFreeBlocksErased();
    EXPECT_EQ(23, State().liveNodes);
    EXPECT_EQ(12, List("/").size());
    EXPECT_THAT(List("/d011"), testing::ElementsAre("x"));
}

TEST_F(NorNodeCollectorTest, a_step_moves_no_more_nodes_than_the_free_blocks_hold_next_to_the_checkpoint_block)
{
    AddDirectoriesWithAnEntry(12);
    for (uint32_t block = 40; block != 44; ++block)
        Builder().AddEmptyNodeBlock(block);
    Builder().Commit();
    Mount();
    auto first = NodeBlocks()[1];
    LeaveFreeBlocksInState(2);

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(2);
    EXPECT_TRUE(State().nodeBlocks.Contains(first));
    EXPECT_EQ(1, Volume().Collector().LeftBlocks());
    RemountWithFreeBlocksErased();
    EXPECT_EQ(23, State().liveNodes);
    EXPECT_THAT(List("/d000"), testing::ElementsAre("x"));
}

TEST_F(NorNodeCollectorTest, a_rename_that_finds_the_journal_full_writes_the_checkpoint_before_it_reads_the_record)
{
    AddDirectoriesWithAnEntry(12);
    for (uint32_t block = 40; block != 44; ++block)
        Builder().AddEmptyNodeBlock(block);
    Builder().Commit();
    Mount();
    for (uint32_t index : { 0, 1, 3, 4, 6, 7, 9, 10 })
    {
        auto record = Serialized(fs::NorInodeRecord{ root, fs::NorInodeType::directory, 0, 0, 0, {}, infra::StringAsByteRange(NameOf(index)) });
        ASSERT_EQ(fs::Result::success, Commit({ fs::NorInodePut{ 2 + 2 * index, infra::MakeRange(record) } }));
    }
    auto record = Serialized(fs::NorInodeRecord{ root, fs::NorInodeType::directory, 0, 0, 0, {}, infra::StringAsByteRange(NameOf(0)) });
    while (!Volume().Journal().NeedsCheckpointFor(fs::norDirectoryEntryRemoveBytes + fs::norDirectoryEntryPutBytes + fs::norInodePutFixedBytes + recordBytes))
        ASSERT_EQ(fs::Result::success, Commit({ fs::NorInodePut{ 2, infra::MakeRange(record) } }));
    ASSERT_EQ(9, Volume().Cache().ModifiedNodes());
    ASSERT_TRUE(Volume().Collector().Due());

    EXPECT_EQ(fs::Result::success, Rename("/d011/x", "/d011/y"));

    RemountWithFreeBlocksErased();
    EXPECT_THAT(List("/d011"), testing::ElementsAre("y"));
}

TEST_F(NorNodeCollectorTest, a_walk_through_a_corrupted_node_to_the_record_of_the_owner_leaves_the_block_in_use)
{
    for (auto name : { "a", "b", "c", "d", "e", "f", "g", "h" })
        Builder().AddDirectory(root, name);
    Builder().AddDirectory(5, "x");
    Builder().AddEmptyNodeBlock(30);
    Builder().AddEmptyNodeBlock(31);
    Builder().Commit();
    Builder().Corrupt(ChildOf(Builder().InodeTable().address, 4) + 12);
    Mount();
    auto before = NodeBlocks();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/h/y"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed({ 2, 30, 31 });
    EXPECT_TRUE(State().nodeBlocks.Contains(before[1]));
    EXPECT_EQ(fs::EntryType::directory, Stat("/h/y").second.type);
    EXPECT_EQ(fs::EntryType::directory, Stat("/a").second.type);
}

TEST_F(NorNodeCollectorTest, a_walk_through_a_corrupted_inner_node_leaves_the_block_in_use)
{
    auto big = Builder().AddDirectory(root, "big");
    FillDirectory(big, 70);
    Builder().AddDirectory(root, "e");
    Builder().Commit();
    ASSERT_EQ(2, Builder().MapOf(big).height);
    Builder().Corrupt(Builder().MapOf(big).address + 2);
    Mount();
    auto before = NodeBlocks();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/e/y"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(2);
    EXPECT_TRUE(State().nodeBlocks.Contains(before[1]));
    EXPECT_EQ(fs::EntryType::directory, Stat("/e/y").second.type);
}

TEST_F(NorNodeCollectorTest, a_corrupted_node_that_is_still_used_leaves_its_block_in_use_as_often_as_the_step_finds_it)
{
    auto d = Builder().AddDirectory(root, "d");
    Builder().AddDirectory(d, "x");
    Builder().AddDirectory(root, "e");
    Builder().AddEmptyNodeBlock(30);
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(d).address + 2);
    Mount();
    auto before = NodeBlocks();
    ASSERT_EQ(fs::Result::success, MakeDirectory("/e/y"));

    EXPECT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed({ 2, 30 });
    EXPECT_TRUE(State().nodeBlocks.Contains(before[1]));
    EXPECT_EQ(6, Volume().Collector().LeftBlocks());
    EXPECT_EQ(fs::EntryType::directory, Stat("/e/y").second.type);
}

TEST_F(NorNodeCollectorTest, a_collection_for_space_compacts_the_blocks_where_fewer_than_half_the_slots_hold_used_nodes_and_leaves_the_others)
{
    BuildBlocksOfZeroFiveThreeAndSixUsedNodes();
    Mount();
    ASSERT_EQ(0, LiveNodesIn(2));
    ASSERT_EQ(5, LiveNodesIn(4));
    ASSERT_EQ(3, LiveNodesIn(5));
    ASSERT_EQ(6, LiveNodesIn(7));
    LeaveFreeBlocksInState(Volume().Geometry().ReserveBlocks() + Volume().Geometry().WatermarkBlocks());
    ASSERT_GE(2 * State().liveNodes, Volume().Collector().CollectableBlocks() * Volume().Geometry().NodesPerBlock());

    ASSERT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed({ 2, 5 });
    EXPECT_TRUE(State().nodeBlocks.Contains(4));
    EXPECT_TRUE(State().nodeBlocks.Contains(7));
}

TEST_F(NorNodeCollectorTest, a_collection_for_space_leaves_a_block_where_half_the_slots_or_more_hold_used_nodes)
{
    for (auto name : { "a", "b", "c", "d", "e", "f" })
        Builder().AddDirectory(Builder().AddDirectory(root, name), "x");
    for (auto name : { "g", "h", "i" })
        Builder().AddDirectory(root, name);
    Builder().Commit();
    Builder().AddDirectory(2, "y");
    Builder().Commit();
    Mount();
    ASSERT_EQ(6, LiveNodesIn(4));
    ASSERT_EQ(4, LiveNodesIn(5));
    ASSERT_EQ(4, LiveNodesIn(7));
    LeaveFreeBlocksInState(Volume().Geometry().ReserveBlocks() + Volume().Geometry().WatermarkBlocks());

    ASSERT_EQ(fs::Result::success, WriteCheckpoint());

    ExpectFreed(2);
    for (auto block : { 4, 5, 7 })
        EXPECT_TRUE(State().nodeBlocks.Contains(block)) << block;
}

TEST_F(NorNodeCollectorTest, after_a_lap_that_frees_less_than_a_block_of_slots_no_collection_is_due_for_space_until_another_block_of_slots_is_unused)
{
    for (auto name : { "a", "b", "c", "d", "e", "f" })
        Builder().AddDirectory(Builder().AddDirectory(root, name), "x");
    for (auto name : { "g", "h", "i" })
        Builder().AddDirectory(root, name);
    Builder().Commit();
    Builder().AddDirectory(2, "y");
    Builder().Commit();
    Mount();
    auto lowSpace = Volume().Geometry().ReserveBlocks() + Volume().Geometry().WatermarkBlocks();
    LeaveFreeBlocksInState(lowSpace);
    ASSERT_EQ(fs::Result::success, WriteCheckpoint());
    LeaveFreeBlocksInState(lowSpace);
    ASSERT_TRUE(Volume().Collector().DueForSpace());

    ASSERT_EQ(fs::Result::success, WriteCheckpoint());
    LeaveFreeBlocksInState(lowSpace);

    EXPECT_FALSE(Volume().Collector().DueForSpace());

    State().liveNodes -= Volume().Geometry().NodesPerBlock() - 1;
    EXPECT_FALSE(Volume().Collector().DueForSpace());

    --State().liveNodes;
    EXPECT_TRUE(Volume().Collector().DueForSpace());
}

TEST_F(NorNodeCollectorTest, a_lap_that_skips_every_block_for_a_corrupted_path_ends_the_collection_for_space)
{
    BuildBlocksOfZeroFiveThreeAndSixUsedNodes();
    Builder().Corrupt(Builder().InodeTable().address + 2);
    Mount();
    auto lowSpace = Volume().Geometry().ReserveBlocks() + Volume().Geometry().WatermarkBlocks();
    LeaveFreeBlocksInState(lowSpace);
    ASSERT_TRUE(Volume().Collector().DueForSpace());
    auto nodeBlocks = NodeBlocks();

    ASSERT_EQ(fs::Result::success, WriteCheckpoint());

    LeaveFreeBlocksInState(lowSpace);
    EXPECT_EQ(nodeBlocks, NodeBlocks());
    EXPECT_FALSE(Volume().Collector().DueForSpace());
}

TEST_F(NorNodeCollectorTest, a_collection_for_space_without_room_for_a_path_in_the_free_blocks_makes_no_collection_due_for_space)
{
    BuildBlocksOfZeroFiveThreeAndSixUsedNodes();
    Mount();
    LeaveFreeBlocksInState(1);
    ASSERT_TRUE(Volume().Collector().DueForSpace());

    ASSERT_EQ(fs::Result::success, WriteCheckpoint());

    EXPECT_TRUE(State().nodeBlocks.Contains(5));
    LeaveFreeBlocksInState(Volume().Geometry().ReserveBlocks() + Volume().Geometry().WatermarkBlocks());
    EXPECT_FALSE(Volume().Collector().DueForSpace());
}
