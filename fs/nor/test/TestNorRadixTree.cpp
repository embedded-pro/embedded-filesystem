#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/util/BoundedVector.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <utility>

namespace
{
    constexpr uint32_t nodeSize = 48;
    constexpr uint32_t fanout = 8;
    constexpr uint32_t owner = 9;

    using Blocks = infra::BoundedVector<std::pair<uint32_t, uint32_t>>::WithMaxSize<16>;

    class NorRadixTreeTest
        : public testing::Test
    {
    public:
        infra::ConstByteRange NodeAt(uint32_t address) const
        {
            auto slot = cache.Find(address);
            EXPECT_TRUE(slot.has_value());
            return cache.Node(slot.value_or(0));
        }

        uint32_t SlotAt(uint32_t address, std::size_t position) const
        {
            return fs::LoadLittle32(fs::NorNodeEntries(NodeAt(address)), position * 4);
        }

        Blocks Mapped() const
        {
            Blocks mapped;
            if (root.address != 0)
                Collect(root.address, static_cast<uint8_t>(root.height - 1), 0, mapped);

            return mapped;
        }

        void Collect(uint32_t address, uint8_t level, uint32_t first, Blocks& mapped) const
        {
            auto header = fs::ReadNorNodeHeader(NodeAt(address));
            EXPECT_EQ(fs::NorMap::fileData, header.map);
            EXPECT_EQ(owner, header.owner);
            EXPECT_EQ(level, header.level);
            EXPECT_EQ(first, header.lowestKey);
            EXPECT_TRUE(fs::NorRadixTree::HasValidCount(NodeAt(address)));

            uint32_t span = 1;
            for (uint8_t below = 0; below != level; ++below)
                span *= fanout;

            for (uint32_t position = 0; position != fanout; ++position)
            {
                auto value = SlotAt(address, position);
                if (value == 0)
                    continue;

                if (level == 0)
                    mapped.emplace_back(first + position, value);
                else
                    Collect(value, static_cast<uint8_t>(level - 1), first + position * span, mapped);
            }
        }

        hal::FlashStub flash{ 4, 4096 };
        std::array<fs::NorNodeCache::Slot, 32> slots{};
        std::array<uint8_t, 32 * nodeSize> buffers{};
        fs::NorNodeCache cache{ flash, infra::MakeRange(slots), infra::MakeRange(buffers) };
        fs::NorRadixTree tree{ cache, owner };
        fs::NorMapRoot root;
    };
}

TEST_F(NorRadixTreeTest, an_empty_tree_maps_nothing)
{
    EXPECT_EQ(0u, tree.Find(root, 0));
    EXPECT_EQ(std::optional<uint32_t>(1), tree.NodesModifiedBySet(root, 7));
    EXPECT_EQ(std::optional<uint32_t>(2), tree.NodesModifiedBySet(root, 8));
    EXPECT_EQ(std::optional<uint32_t>(3), tree.NodesModifiedBySet(root, 511));
    EXPECT_EQ(std::nullopt, tree.NodesModifiedBySet(root, 512));
    EXPECT_EQ(0u, tree.NodesModifiedByClear(root, 0));
}

TEST_F(NorRadixTreeTest, set_creates_the_nodes_down_to_the_leaf)
{
    tree.Set(root, 70, 33);

    EXPECT_EQ(3, root.height);
    EXPECT_EQ(33u, tree.Find(root, 70));
    EXPECT_EQ(0u, tree.Find(root, 71));
    EXPECT_EQ(0u, tree.Find(root, 6));
    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(70, 33)));
    EXPECT_EQ(3u, cache.ModifiedNodes());
}

TEST_F(NorRadixTreeTest, set_replaces_a_mapped_block)
{
    tree.Set(root, 3, 7);
    tree.Set(root, 3, 8);

    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(3, 8)));
}

TEST_F(NorRadixTreeTest, the_tree_grows_when_an_index_is_beyond_its_reach)
{
    tree.Set(root, 3, 7);
    auto leaf = root.address;
    EXPECT_EQ(std::optional<uint32_t>(2), tree.NodesModifiedBySet(root, 9));
    EXPECT_EQ(std::optional<uint32_t>(4), tree.NodesModifiedBySet(root, 100));

    tree.Set(root, 9, 8);

    EXPECT_EQ(2, root.height);
    EXPECT_EQ(leaf, SlotAt(root.address, 0));
    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(3, 7), testing::Pair(9, 8)));
}

TEST_F(NorRadixTreeTest, the_tree_grows_by_two_levels_at_once)
{
    tree.Set(root, 3, 7);

    tree.Set(root, 100, 8);

    EXPECT_EQ(3, root.height);
    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(3, 7), testing::Pair(100, 8)));
    EXPECT_EQ(5u, cache.ModifiedNodes());
}

TEST_F(NorRadixTreeTest, counts_the_nodes_below_a_hole)
{
    tree.Set(root, 3, 7);
    tree.Set(root, 9, 8);

    EXPECT_EQ(std::optional<uint32_t>(0), tree.NodesModifiedBySet(root, 4));
    EXPECT_EQ(std::optional<uint32_t>(1), tree.NodesModifiedBySet(root, 20));
    EXPECT_EQ(0u, tree.NodesModifiedByClear(root, 3));
}

TEST_F(NorRadixTreeTest, counts_the_nodes_that_a_set_creates)
{
    EXPECT_EQ(std::optional<uint32_t>(3), tree.NodesCreatedBySet(root, 70));
    EXPECT_EQ(std::nullopt, tree.NodesCreatedBySet(root, 512));

    tree.Set(root, 3, 7);
    EXPECT_EQ(std::optional<uint32_t>(0), tree.NodesCreatedBySet(root, 4));
    EXPECT_EQ(std::optional<uint32_t>(4), tree.NodesCreatedBySet(root, 100));

    tree.Set(root, 9, 8);
    EXPECT_EQ(std::optional<uint32_t>(1), tree.NodesCreatedBySet(root, 20));
}

TEST_F(NorRadixTreeTest, a_node_that_is_written_back_is_modified_by_a_set_but_not_created)
{
    tree.Set(root, 3, 7);
    cache.Written(cache.Find(root.address).value_or(0), 4096);
    root.address = 4096;

    EXPECT_EQ(std::optional<uint32_t>(1), tree.NodesModifiedBySet(root, 4));
    EXPECT_EQ(std::optional<uint32_t>(0), tree.NodesCreatedBySet(root, 4));
}

TEST_F(NorRadixTreeTest, maps_the_indexes_that_three_levels_reach)
{
    EXPECT_TRUE(fs::NorRadixTree::CanMap(511, nodeSize));
    EXPECT_FALSE(fs::NorRadixTree::CanMap(512, nodeSize));
}

TEST_F(NorRadixTreeTest, an_empty_leaf_leaves_its_parent_and_a_root_with_only_its_first_child_is_replaced_by_it)
{
    tree.Set(root, 3, 7);
    tree.Set(root, 9, 8);
    auto first = SlotAt(root.address, 0);

    tree.Clear(root, 9);

    EXPECT_EQ((fs::NorMapRoot{ first, 1 }), root);
    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(3, 7)));
    EXPECT_EQ(1u, cache.ModifiedNodes());
}

TEST_F(NorRadixTreeTest, a_root_whose_only_child_is_not_its_first_stays)
{
    tree.Set(root, 3, 7);
    tree.Set(root, 9, 8);

    tree.Clear(root, 3);

    EXPECT_EQ(2, root.height);
    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(9, 8)));
}

TEST_F(NorRadixTreeTest, clearing_one_of_several_blocks_keeps_the_leaf)
{
    tree.Set(root, 3, 7);
    tree.Set(root, 4, 9);

    tree.Clear(root, 3);

    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(4, 9)));
}

TEST_F(NorRadixTreeTest, a_root_with_several_children_stays)
{
    tree.Set(root, 3, 7);
    tree.Set(root, 9, 8);
    tree.Set(root, 20, 6);

    tree.Clear(root, 9);

    EXPECT_EQ(2, root.height);
    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(3, 7), testing::Pair(20, 6)));
}

TEST_F(NorRadixTreeTest, clearing_an_index_that_is_not_mapped_changes_nothing)
{
    tree.Set(root, 3, 7);
    tree.Set(root, 9, 8);
    auto before = root;

    tree.Clear(root, 4);
    tree.Clear(root, 20);
    tree.Clear(root, 64);

    EXPECT_EQ(before, root);
    EXPECT_EQ(0u, tree.NodesModifiedByClear(root, 4));
    EXPECT_EQ(0u, tree.NodesModifiedByClear(root, 20));
    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(3, 7), testing::Pair(9, 8)));
}

TEST_F(NorRadixTreeTest, clearing_the_last_block_empties_the_tree)
{
    tree.Set(root, 70, 33);

    tree.Clear(root, 70);

    EXPECT_EQ(fs::NorMapRoot{}, root);
    EXPECT_EQ(0u, cache.ModifiedNodes());
}

TEST_F(NorRadixTreeTest, the_root_shrinks_along_the_path_of_a_clear)
{
    tree.Set(root, 3, 7);
    tree.Set(root, 4, 9);
    tree.Set(root, 100, 8);

    tree.Clear(root, 100);

    EXPECT_EQ(2, root.height);

    tree.Clear(root, 3);

    EXPECT_EQ(1, root.height);
    EXPECT_THAT(Mapped(), testing::ElementsAre(testing::Pair(4, 9)));
    EXPECT_EQ(1u, cache.ModifiedNodes());
}

TEST_F(NorRadixTreeTest, covers_the_indexes_below_the_fanout_to_the_power_of_the_height)
{
    EXPECT_FALSE(fs::NorRadixTree::Covers(fs::NorMapRoot{}, 0, nodeSize));
    EXPECT_TRUE(fs::NorRadixTree::Covers(fs::NorMapRoot{ 4096, 1 }, 7, nodeSize));
    EXPECT_FALSE(fs::NorRadixTree::Covers(fs::NorMapRoot{ 4096, 1 }, 8, nodeSize));
    EXPECT_TRUE(fs::NorRadixTree::Covers(fs::NorMapRoot{ 4096, 3 }, 511, nodeSize));
    EXPECT_FALSE(fs::NorRadixTree::Covers(fs::NorMapRoot{ 4096, 3 }, 512, nodeSize));
}

TEST_F(NorRadixTreeTest, child_for_picks_the_slot_of_the_index_below_the_first_index_of_the_node)
{
    tree.Set(root, 70, 33);
    auto inner = SlotAt(root.address, 1);

    EXPECT_EQ(inner, fs::NorRadixTree::ChildFor(NodeAt(root.address), 70));
    EXPECT_EQ(SlotAt(inner, 0), fs::NorRadixTree::ChildFor(NodeAt(inner), 70));
    EXPECT_EQ(0u, fs::NorRadixTree::ChildFor(NodeAt(inner), 72));
}

TEST_F(NorRadixTreeTest, a_valid_count_is_the_number_of_slots_that_are_set)
{
    std::array<uint8_t, nodeSize> node{};
    auto range = infra::MakeRange(node);

    EXPECT_FALSE(fs::NorRadixTree::HasValidCount(range));
    fs::StoreLittle32(fs::NorNodeEntries(range), 8, 5);
    EXPECT_FALSE(fs::NorRadixTree::HasValidCount(range));
    fs::WriteNorNodeHeader(fs::NorNodeHeader{ fs::NorMap::fileData, 0, 1, owner, 0 }, range);
    EXPECT_TRUE(fs::NorRadixTree::HasValidCount(range));
}

TEST_F(NorRadixTreeTest, replaces_the_address_of_a_child)
{
    tree.Set(root, 70, 33);
    auto inner = SlotAt(root.address, 1);
    auto node = cache.Modify(cache.Find(root.address).value_or(0));

    EXPECT_TRUE(fs::NorRadixTree::ReplaceChild(node, inner, 0x1234));
    EXPECT_EQ(0x1234u, SlotAt(root.address, 1));
    EXPECT_EQ(1, fs::ReadNorNodeHeader(node).count);
    EXPECT_FALSE(fs::NorRadixTree::ReplaceChild(node, inner, 0x1234));
}
