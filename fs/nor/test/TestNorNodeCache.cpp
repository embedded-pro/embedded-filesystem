#include "fs/nor/NorNodeCache.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>

namespace
{
    constexpr uint32_t nodeSize = 512;
    constexpr uint32_t a = 2 * 4096 + 512;
    constexpr uint32_t b = 2 * 4096 + 1024;
    constexpr uint32_t c = 2 * 4096 + 1536;
    constexpr uint32_t d = 3 * 4096 + 512;

    class NorNodeCacheTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorNodeCacheTest()
        {
            for (auto address : { a, b, c, d })
                Store(address, fs::NorNodeHeader{ fs::NorMap::directory, 0, 1, 7, address });
        }

        void Store(uint32_t address, const fs::NorNodeHeader& header)
        {
            std::array<uint8_t, nodeSize> node{};
            fs::SealNorNode(header, infra::MakeRange(node));
            std::ranges::copy(node, flash.sectors[address / 4096].begin() + address % 4096);
        }

        std::size_t Load(uint32_t address)
        {
            std::optional<std::size_t> loaded;
            cache.Load(address, [&loaded](std::optional<std::size_t> slot)
                {
                    loaded = slot;
                });
            ExecuteAllActions();
            EXPECT_TRUE(loaded.has_value());
            return loaded.value_or(0);
        }

        hal::FlashStub flash{ 4, 4096 };
        std::array<fs::NorNodeCache::Slot, 3> slots{};
        std::array<uint8_t, 3 * nodeSize> buffers{};
        fs::NorNodeCache cache{ flash, infra::MakeRange(slots), infra::MakeRange(buffers) };
    };
}

TEST_F(NorNodeCacheTest, starts_empty)
{
    EXPECT_EQ(3u, cache.Capacity());
    EXPECT_EQ(nodeSize, cache.NodeSize());
    EXPECT_EQ(0u, cache.ModifiedNodes());
    EXPECT_EQ(0u, cache.ReleasedNodes());
    EXPECT_TRUE(cache.HasRoom());
    EXPECT_EQ(std::nullopt, cache.Find(a));
    EXPECT_EQ(std::nullopt, cache.Find(0));
}

TEST_F(NorNodeCacheTest, loads_a_node_through_the_event_dispatcher)
{
    testing::StrictMock<infra::MockCallback<void(std::optional<std::size_t>)>> loaded;
    cache.Load(a, [&loaded](std::optional<std::size_t> slot)
        {
            loaded.callback(slot);
        });

    EXPECT_CALL(loaded, callback(std::optional<std::size_t>(0)));
    ExecuteAllActions();

    EXPECT_EQ(std::optional<std::size_t>(0), cache.Find(a));
    EXPECT_EQ(a, cache.AddressOf(0));
    EXPECT_FALSE(cache.IsModified(0));
    EXPECT_EQ((fs::NorNodeHeader{ fs::NorMap::directory, 0, 1, 7, a }), fs::OpenNorNode(cache.Node(0)));
}

TEST_F(NorNodeCacheTest, a_node_with_a_bad_crc_does_not_load)
{
    flash.sectors[a / 4096][a % 4096 + 20] ^= 1;

    testing::StrictMock<infra::MockCallback<void(std::optional<std::size_t>)>> loaded;
    cache.Load(a, [&loaded](std::optional<std::size_t> slot)
        {
            loaded.callback(slot);
        });

    EXPECT_CALL(loaded, callback(std::optional<std::size_t>()));
    ExecuteAllActions();

    EXPECT_EQ(std::nullopt, cache.Find(a));
    EXPECT_EQ(std::nullopt, cache.Find(0));
}

TEST_F(NorNodeCacheTest, a_full_cache_replaces_the_least_recently_used_node)
{
    auto slotOfA = Load(a);
    Load(b);
    Load(c);
    cache.Pin(slotOfA);
    cache.UnpinAll();

    Load(d);

    EXPECT_TRUE(cache.Find(a).has_value());
    EXPECT_EQ(std::nullopt, cache.Find(b));
    EXPECT_TRUE(cache.Find(c).has_value());
    EXPECT_TRUE(cache.Find(d).has_value());
}

TEST_F(NorNodeCacheTest, pinned_and_modified_nodes_stay_in_the_cache)
{
    cache.Pin(Load(a));
    cache.Modify(Load(b));
    Load(c);

    auto slotOfD = Load(d);

    EXPECT_EQ(std::nullopt, cache.Find(c));
    EXPECT_TRUE(cache.HasRoom());
    cache.Pin(slotOfD);
    EXPECT_FALSE(cache.HasRoom());
    cache.UnpinAll();
    EXPECT_TRUE(cache.HasRoom());
}

TEST_F(NorNodeCacheTest, a_modified_node_gets_an_address_that_names_its_slot)
{
    Load(c);
    auto slot = Load(a);

    auto node = cache.Modify(slot);
    node[100] = 0x5a;

    EXPECT_EQ(fs::norModifiedNodeTag | 1, cache.AddressOf(slot));
    EXPECT_EQ(std::optional<std::size_t>(1), cache.Find(fs::norModifiedNodeTag | 1));
    EXPECT_EQ(std::nullopt, cache.Find(a));
    EXPECT_EQ(0x5a, cache.Node(slot)[100]);
    EXPECT_TRUE(cache.IsModified(slot));
    EXPECT_EQ(1u, cache.ModifiedNodes());
}

TEST_F(NorNodeCacheTest, modifying_a_node_releases_its_copy_on_flash_once)
{
    auto slot = Load(a);

    cache.Modify(slot);
    cache.Modify(slot);

    EXPECT_EQ(1u, cache.ReleasedNodes());
    cache.ClearReleasedNodes();
    EXPECT_EQ(0u, cache.ReleasedNodes());
}

TEST_F(NorNodeCacheTest, a_tagged_address_of_a_slot_that_is_not_modified_is_not_found)
{
    Load(a);

    EXPECT_EQ(std::nullopt, cache.Find(fs::norModifiedNodeTag | 0));
    EXPECT_EQ(std::nullopt, cache.Find(fs::norModifiedNodeTag | 3));
}

TEST_F(NorNodeCacheTest, a_created_node_is_modified_and_releases_nothing)
{
    auto slot = cache.Create(fs::NorNodeHeader{ fs::NorMap::fileData, 1, 0, 9, 124 });

    EXPECT_TRUE(cache.IsModified(slot));
    EXPECT_EQ(fs::norModifiedNodeTag | slot, cache.AddressOf(slot));
    EXPECT_EQ((fs::NorNodeHeader{ fs::NorMap::fileData, 1, 0, 9, 124 }), fs::ReadNorNodeHeader(cache.Node(slot)));
    EXPECT_TRUE(std::ranges::all_of(fs::NorNodeEntries(cache.Node(slot)), [](uint8_t byte)
        {
            return byte == 0;
        }));
    EXPECT_EQ(0u, cache.ReleasedNodes());
}

TEST_F(NorNodeCacheTest, a_created_node_replaces_the_least_recently_used_node)
{
    Load(a);
    Load(b);
    Load(c);

    cache.Create(fs::NorNodeHeader{});

    EXPECT_EQ(std::nullopt, cache.Find(a));
}

TEST_F(NorNodeCacheTest, dropping_a_modified_node_frees_its_slot)
{
    auto slot = cache.Create(fs::NorNodeHeader{});
    auto address = cache.AddressOf(slot);

    cache.Drop(slot);

    EXPECT_EQ(std::nullopt, cache.Find(address));
    EXPECT_EQ(0u, cache.ModifiedNodes());
    EXPECT_EQ(0u, cache.ReleasedNodes());
}

TEST_F(NorNodeCacheTest, dropping_a_node_from_flash_releases_it)
{
    auto slot = Load(a);
    cache.Pin(slot);

    cache.Drop(slot);

    EXPECT_EQ(std::nullopt, cache.Find(a));
    EXPECT_EQ(1u, cache.ReleasedNodes());
    Load(b);
    Load(c);
    EXPECT_TRUE(cache.HasRoom());
}

TEST_F(NorNodeCacheTest, a_written_node_keeps_its_contents_at_its_new_address)
{
    auto slot = cache.Create(fs::NorNodeHeader{ fs::NorMap::directory, 0, 1, 7, 0 });
    cache.Modify(slot)[40] = 0x33;

    cache.Written(slot, d);

    EXPECT_EQ(std::optional<std::size_t>(slot), cache.Find(d));
    EXPECT_FALSE(cache.IsModified(slot));
    EXPECT_EQ(0u, cache.ModifiedNodes());
    EXPECT_EQ(0x33, cache.Node(slot)[40]);
}

TEST_F(NorNodeCacheTest, clear_empties_the_cache_and_its_count_of_released_nodes)
{
    cache.Modify(Load(a));
    cache.Pin(Load(b));
    cache.Create(fs::NorNodeHeader{});

    cache.Clear();

    EXPECT_EQ(std::nullopt, cache.Find(a));
    EXPECT_EQ(std::nullopt, cache.Find(b));
    EXPECT_EQ(0u, cache.ModifiedNodes());
    EXPECT_EQ(0u, cache.ReleasedNodes());
    Load(c);
    Load(d);
    Load(a);
    EXPECT_TRUE(cache.HasRoom());
}
