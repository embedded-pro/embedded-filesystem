#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorNode.hpp"
#include "infra/util/Crc.hpp"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>

namespace
{
    using Node = std::array<uint8_t, 512>;

    constexpr fs::NorNodeHeader dataLeaf{ fs::NorMap::fileData, 0, 124, 7, 248 };

    uint32_t Crc(infra::ConstByteRange bytes)
    {
        infra::Crc32 crc;
        crc.Update(bytes);
        return crc.Result();
    }

    Node Sealed(const fs::NorNodeHeader& header)
    {
        Node node{};
        auto entries = fs::NorNodeEntries(infra::MakeRange(node));
        for (std::size_t index = 0; index != entries.size(); ++index)
            entries[index] = static_cast<uint8_t>(index);
        fs::SealNorNode(header, infra::MakeRange(node));
        return node;
    }

    void Reseal(Node& node)
    {
        fs::StoreLittle32(infra::MakeRange(node), 508, Crc(infra::Head(infra::MakeRange(node), 508)));
    }
}

TEST(NorNodeTest, entries_lie_between_the_header_and_the_crc)
{
    Node node{};

    auto entries = fs::NorNodeEntries(infra::MakeRange(node));

    EXPECT_EQ(node.data() + 12, entries.begin());
    EXPECT_EQ(496u, entries.size());
    EXPECT_EQ(entries, fs::NorNodeEntries(infra::ConstByteRange(infra::MakeRange(node))));
}

TEST(NorNodeTest, sealing_writes_the_header_and_a_crc_of_the_rest)
{
    auto node = Sealed(dataLeaf);

    EXPECT_EQ((std::array<uint8_t, 12>{ 0x03, 0x00, 0x7c, 0x00, 0x07, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00 }), infra::Head(infra::MakeRange(node), 12));
    EXPECT_EQ(Crc(infra::Head(infra::MakeRange(node), 508)), fs::LoadLittle32(infra::MakeRange(node), 508));
}

TEST(NorNodeTest, opens_a_sealed_node)
{
    EXPECT_EQ(dataLeaf, fs::OpenNorNode(infra::MakeRange(Sealed(dataLeaf))));
}

TEST(NorNodeTest, every_map_opens)
{
    for (auto map : { fs::NorMap::inodeTable, fs::NorMap::directory, fs::NorMap::fileData })
    {
        fs::NorNodeHeader header{ map, 2, 1, 0, 0 };

        EXPECT_EQ(header, fs::OpenNorNode(infra::MakeRange(Sealed(header))));
    }
}

TEST(NorNodeTest, a_changed_byte_does_not_open)
{
    for (std::size_t offset : { 0, 3, 11, 12, 300, 507, 508, 511 })
    {
        auto node = Sealed(dataLeaf);
        node[offset] ^= 0x01;

        EXPECT_EQ(std::nullopt, fs::OpenNorNode(infra::MakeRange(node))) << offset;
    }
}

TEST(NorNodeTest, an_unknown_map_does_not_open)
{
    for (uint8_t map : { 0, 4 })
    {
        auto node = Sealed(dataLeaf);
        node[0] = map;
        Reseal(node);

        EXPECT_EQ(std::nullopt, fs::OpenNorNode(infra::MakeRange(node))) << int(map);
    }
}

TEST(NorNodeTest, a_level_beyond_the_deepest_does_not_open)
{
    auto node = Sealed(dataLeaf);
    node[1] = 3;
    Reseal(node);

    EXPECT_EQ(std::nullopt, fs::OpenNorNode(infra::MakeRange(node)));
}

TEST(NorNodeTest, erased_flash_does_not_open)
{
    Node node;
    node.fill(0xff);

    EXPECT_EQ(std::nullopt, fs::OpenNorNode(infra::MakeRange(node)));
}
