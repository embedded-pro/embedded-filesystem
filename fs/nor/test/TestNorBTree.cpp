#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <tuple>
#include <vector>

namespace
{
    constexpr uint32_t nodeSize = 48;
    constexpr fs::NorMapId directory{ fs::NorMap::directory, 7, 4 };

    class NorBTreeTest
        : public testing::Test
    {
    public:
        void Insert(uint32_t key)
        {
            fs::StoreLittle32(tree.Insert(root, key), 0, key * 10);
        }

        void InsertRange(uint32_t first, uint32_t last)
        {
            for (auto key = first; key <= last; key += 10)
                Insert(key);
        }

        uint32_t ValueOf(uint32_t key) const
        {
            auto value = tree.Find(root, key);
            return value.has_value() ? fs::LoadLittle32(*value, 0) : 0;
        }

        infra::ConstByteRange NodeAt(uint32_t address) const
        {
            auto slot = cache.Find(address);
            EXPECT_TRUE(slot.has_value());
            return cache.Node(slot.value_or(0));
        }

        uint32_t KeyAt(uint32_t address, std::size_t index) const
        {
            return fs::LoadLittle32(fs::NorNodeEntries(NodeAt(address)), index * 8);
        }

        uint32_t ChildAt(uint32_t address, std::size_t index) const
        {
            return fs::LoadLittle32(fs::NorNodeEntries(NodeAt(address)), index * 8 + 4);
        }

        uint16_t CountAt(uint32_t address) const
        {
            return fs::ReadNorNodeHeader(NodeAt(address)).count;
        }

        std::vector<uint32_t> Keys() const
        {
            std::vector<uint32_t> keys;
            if (root.address != 0)
                Collect(root.address, static_cast<uint8_t>(root.height - 1), 0, uint64_t{ 1 } << 32, keys);

            return keys;
        }

        void Collect(uint32_t address, uint8_t level, uint64_t low, uint64_t high, std::vector<uint32_t>& keys) const
        {
            auto node = NodeAt(address);
            auto header = fs::ReadNorNodeHeader(node);
            EXPECT_EQ(directory.map, header.map);
            EXPECT_EQ(directory.owner, header.owner);
            EXPECT_EQ(level, header.level);
            EXPECT_GE(header.count, 1);
            EXPECT_LE(header.count, 4);
            EXPECT_EQ(KeyAt(address, 0), header.lowestKey);

            for (std::size_t index = 0; index != header.count; ++index)
            {
                uint64_t key = KeyAt(address, index);
                uint64_t next = index + 1 != header.count ? KeyAt(address, index + 1) : high;
                EXPECT_GE(key, low);
                EXPECT_LT(key, next);

                if (level == 0)
                    keys.push_back(static_cast<uint32_t>(key));
                else
                    Collect(ChildAt(address, index), static_cast<uint8_t>(level - 1), key, next, keys);
            }
        }

        hal::FlashStub flash{ 4, 4096 };
        std::array<fs::NorNodeCache::Slot, 64> slots{};
        std::array<uint8_t, 64 * nodeSize> buffers{};
        fs::NorNodeCache cache{ flash, infra::MakeRange(slots), infra::MakeRange(buffers) };
        fs::NorBTree tree{ cache, directory };
        fs::NorMapRoot root;
    };
}

TEST_F(NorBTreeTest, an_empty_tree_holds_nothing)
{
    EXPECT_EQ(std::nullopt, tree.Find(root, 5));
    EXPECT_EQ(std::optional<uint32_t>(1), tree.NodesModifiedByInsert(root, 5));
    EXPECT_EQ(0u, tree.NodesModifiedByChange(root, 5));
}

TEST_F(NorBTreeTest, the_first_key_creates_a_leaf_as_root)
{
    Insert(50);

    EXPECT_EQ(1, root.height);
    EXPECT_EQ(fs::norModifiedNodeTag, root.address & fs::norModifiedNodeTag);
    EXPECT_EQ(500u, ValueOf(50));
    EXPECT_EQ(std::nullopt, tree.Find(root, 40));
    EXPECT_EQ(std::nullopt, tree.Find(root, 60));
    EXPECT_EQ((std::vector<uint32_t>{ 50 }), Keys());
}

TEST_F(NorBTreeTest, keys_inserted_in_any_order_are_found_in_order)
{
    std::vector<uint32_t> inserted;
    for (uint32_t step = 0; step != 30; ++step)
    {
        auto key = step * 37 % 64 * 3;
        ASSERT_TRUE(tree.NodesModifiedByInsert(root, key).has_value());
        Insert(key);
        inserted.push_back(key);
    }

    std::ranges::sort(inserted);
    EXPECT_EQ(inserted, Keys());
    for (auto key : inserted)
        EXPECT_EQ(key * 10, ValueOf(key));
    EXPECT_EQ(std::nullopt, tree.Find(root, 1));
}

TEST_F(NorBTreeTest, a_full_leaf_splits_in_half)
{
    InsertRange(10, 40);
    Insert(15);

    EXPECT_EQ(2, root.height);
    EXPECT_EQ(2, CountAt(root.address));
    EXPECT_EQ(10u, KeyAt(root.address, 0));
    EXPECT_EQ(30u, KeyAt(root.address, 1));
    EXPECT_EQ(3, CountAt(ChildAt(root.address, 0)));
    EXPECT_EQ(2, CountAt(ChildAt(root.address, 1)));
    EXPECT_EQ((std::vector<uint32_t>{ 10, 15, 20, 30, 40 }), Keys());
}

TEST_F(NorBTreeTest, a_key_after_the_last_of_a_full_leaf_goes_alone_into_a_new_leaf)
{
    InsertRange(10, 50);

    EXPECT_EQ(50u, KeyAt(root.address, 1));
    EXPECT_EQ(4, CountAt(ChildAt(root.address, 0)));
    EXPECT_EQ(1, CountAt(ChildAt(root.address, 1)));
}

TEST_F(NorBTreeTest, a_key_that_lands_in_the_upper_half_of_a_split_goes_to_the_new_leaf)
{
    InsertRange(10, 40);
    Insert(35);

    EXPECT_EQ(30u, KeyAt(root.address, 1));
    EXPECT_EQ(2, CountAt(ChildAt(root.address, 0)));
    EXPECT_EQ(3, CountAt(ChildAt(root.address, 1)));
    EXPECT_EQ((std::vector<uint32_t>{ 10, 20, 30, 35, 40 }), Keys());
}

TEST_F(NorBTreeTest, a_key_below_every_key_lowers_the_first_keys_of_its_path)
{
    InsertRange(10, 50);
    Insert(5);

    EXPECT_EQ(5u, KeyAt(root.address, 0));
    EXPECT_EQ(5u, fs::ReadNorNodeHeader(NodeAt(root.address)).lowestKey);
    EXPECT_EQ((std::vector<uint32_t>{ 5, 10, 20, 30, 40, 50 }), Keys());
}

TEST_F(NorBTreeTest, inner_nodes_split_up_to_three_levels)
{
    InsertRange(10, 640);

    EXPECT_EQ(3, root.height);
    EXPECT_EQ(64u, Keys().size());
    EXPECT_EQ(4, CountAt(root.address));
}

TEST_F(NorBTreeTest, an_insert_that_needs_a_fourth_level_does_not_fit)
{
    InsertRange(10, 640);

    EXPECT_EQ(std::nullopt, tree.NodesModifiedByInsert(root, 650));
    EXPECT_EQ(std::nullopt, tree.NodesModifiedByInsert(root, 5));
}

TEST_F(NorBTreeTest, counts_the_nodes_that_splits_add)
{
    InsertRange(10, 30);
    EXPECT_EQ(std::optional<uint32_t>(0), tree.NodesModifiedByInsert(root, 15));

    Insert(40);
    EXPECT_EQ(std::optional<uint32_t>(2), tree.NodesModifiedByInsert(root, 15));

    Insert(50);
    EXPECT_EQ(std::optional<uint32_t>(1), tree.NodesModifiedByInsert(root, 15));
    EXPECT_EQ(std::optional<uint32_t>(0), tree.NodesModifiedByInsert(root, 55));

    InsertRange(60, 160);
    EXPECT_EQ(std::optional<uint32_t>(3), tree.NodesModifiedByInsert(root, 170));
}

TEST_F(NorBTreeTest, modify_changes_the_value_of_a_key)
{
    InsertRange(10, 50);
    auto rootBefore = root;

    fs::StoreLittle32(tree.Modify(root, 20), 0, 99);

    EXPECT_EQ(99u, ValueOf(20));
    EXPECT_EQ(rootBefore, root);
    EXPECT_EQ(0u, tree.NodesModifiedByChange(root, 20));
}

TEST_F(NorBTreeTest, removing_a_key_keeps_the_others)
{
    InsertRange(10, 50);

    tree.Remove(root, 30);

    EXPECT_EQ(std::nullopt, tree.Find(root, 30));
    EXPECT_EQ((std::vector<uint32_t>{ 10, 20, 40, 50 }), Keys());
    EXPECT_EQ(400u, ValueOf(40));
}

TEST_F(NorBTreeTest, removing_the_first_key_of_a_node_raises_its_lowest_key)
{
    InsertRange(10, 50);

    tree.Remove(root, 10);

    EXPECT_EQ(20u, fs::ReadNorNodeHeader(NodeAt(ChildAt(root.address, 0))).lowestKey);
    EXPECT_EQ((std::vector<uint32_t>{ 20, 30, 40, 50 }), Keys());
}

TEST_F(NorBTreeTest, an_empty_leaf_leaves_its_parent_and_a_root_with_one_child_is_replaced_by_it)
{
    InsertRange(10, 50);
    auto leaf = ChildAt(root.address, 0);

    tree.Remove(root, 50);

    EXPECT_EQ((fs::NorMapRoot{ leaf, 1 }), root);
    EXPECT_EQ((std::vector<uint32_t>{ 10, 20, 30, 40 }), Keys());
    EXPECT_EQ(1u, cache.ModifiedNodes());
}

TEST_F(NorBTreeTest, an_empty_first_leaf_leaves_its_parent)
{
    InsertRange(10, 90);

    tree.Remove(root, 50);
    for (auto key : { 10, 20, 30, 40 })
        tree.Remove(root, key);

    EXPECT_EQ(2, root.height);
    EXPECT_EQ(50u, fs::ReadNorNodeHeader(NodeAt(root.address)).lowestKey);
    EXPECT_EQ((std::vector<uint32_t>{ 60, 70, 80, 90 }), Keys());
}

TEST_F(NorBTreeTest, removing_the_last_key_empties_the_tree)
{
    Insert(10);

    tree.Remove(root, 10);

    EXPECT_EQ(fs::NorMapRoot{}, root);
    EXPECT_EQ(0u, cache.ModifiedNodes());
}

TEST_F(NorBTreeTest, the_root_shrinks_along_the_path_of_a_removal)
{
    InsertRange(10, 180);

    for (uint32_t key = 10; key <= 160; key += 10)
        tree.Remove(root, key);

    EXPECT_EQ(2, root.height);
    EXPECT_EQ(1, CountAt(root.address));

    tree.Remove(root, 170);

    EXPECT_EQ(1, root.height);
    EXPECT_EQ((std::vector<uint32_t>{ 180 }), Keys());
    EXPECT_EQ(1u, cache.ModifiedNodes());
}

TEST_F(NorBTreeTest, child_for_follows_the_last_key_not_above_the_searched_one)
{
    InsertRange(10, 90);
    auto node = NodeAt(root.address);

    EXPECT_EQ(ChildAt(root.address, 0), fs::NorBTree::ChildFor(node, 5));
    EXPECT_EQ(ChildAt(root.address, 0), fs::NorBTree::ChildFor(node, 49));
    EXPECT_EQ(ChildAt(root.address, 1), fs::NorBTree::ChildFor(node, 50));
    EXPECT_EQ(ChildAt(root.address, 2), fs::NorBTree::ChildFor(node, 1000));
}

TEST_F(NorBTreeTest, a_valid_count_is_between_one_and_the_capacity_of_the_level)
{
    std::array<uint8_t, nodeSize> node{};
    auto range = infra::MakeRange(node);

    for (auto [level, count, valid] : std::initializer_list<std::tuple<uint8_t, uint16_t, bool>>{ { 0, 0, false }, { 0, 1, true }, { 0, 4, true }, { 0, 5, false }, { 1, 4, true }, { 1, 5, false } })
    {
        fs::WriteNorNodeHeader(fs::NorNodeHeader{ fs::NorMap::directory, level, count, 7, 0 }, range);
        EXPECT_EQ(valid, fs::NorBTree::HasValidCount(range, 4)) << int(level) << " " << count;
    }

    fs::WriteNorNodeHeader(fs::NorNodeHeader{ fs::NorMap::directory, 0, 2, 7, 0 }, range);
    EXPECT_TRUE(fs::NorBTree::HasValidCount(range, 12));
    fs::WriteNorNodeHeader(fs::NorNodeHeader{ fs::NorMap::directory, 0, 3, 7, 0 }, range);
    EXPECT_FALSE(fs::NorBTree::HasValidCount(range, 12));
}

TEST_F(NorBTreeTest, replaces_the_address_of_a_child)
{
    InsertRange(10, 90);
    auto second = ChildAt(root.address, 1);
    auto node = cache.Modify(cache.Find(root.address).value_or(0));

    EXPECT_TRUE(fs::NorBTree::ReplaceChild(node, second, 0x1234));
    EXPECT_EQ(0x1234u, ChildAt(root.address, 1));
    EXPECT_FALSE(fs::NorBTree::ReplaceChild(node, second, 0x1234));
}
