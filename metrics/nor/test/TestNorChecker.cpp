#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/NorSuperblock.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/Crc.hpp"
#include "metrics/nor/NorChecker.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32_t sectorSize = 4096;
    constexpr uint32_t pageSize = 256;
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t headerSize = 36;
    constexpr uint32_t payload = blockSize - headerSize;
    constexpr uint32_t firstRecord = headerSize + 36;
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15, .inlineBytes = 40, .maxNameBytes = 24 };
    constexpr fs::NorConfig twoSectorConfig{ .maxBlocks = 32, .sectorsPerBlock = 2, .cachedNodes = 15 };
    constexpr fs::NorConfig wideUnitConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };

    using Use = metrics::NorBlockUse;

    std::vector<uint8_t> Bytes(std::string_view text)
    {
        return { text.begin(), text.end() };
    }

    std::vector<uint8_t> Pattern(std::size_t size, uint8_t seed)
    {
        std::vector<uint8_t> pattern(size);
        for (std::size_t index = 0; index != size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 13 + seed + index / 251);

        return pattern;
    }

    uint32_t NodeAddress(uint32_t block, uint32_t slot)
    {
        return block * blockSize + pageSize + slot * config.nodeSize;
    }

    uint32_t Key(std::string_view name, uint32_t index = 0)
    {
        auto bytes = Bytes(name);
        infra::Crc32 crc;
        crc.Update(infra::MakeRange(bytes));
        return (crc.Result() & 0xffffff) << 8 | index;
    }

    void Put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value)
    {
        for (std::size_t byte = 0; byte != 4; ++byte)
            bytes[offset + byte] = static_cast<uint8_t>(value >> (8 * byte));
    }

    class NorCheckerTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        template<fs::NorConfig Config>
        std::vector<uint8_t> FormattedImage(uint32_t sectors, uint32_t sizeOfSector)
        {
            hal::FlashStub flash{ sectors, sizeOfSector };
            fs::NorFormatter::WithStorage<Config> formatter{ flash };
            formatter.Format([](fs::Result) {});
            ExecuteAllActions();

            std::vector<uint8_t> formatted;
            for (const auto& sector : flash.sectors)
                formatted.insert(formatted.end(), sector.begin(), sector.end());

            return formatted;
        }

        metrics::NorCheck Check() const
        {
            return metrics::CheckNorImage(image, sectorSize, pageSize);
        }

        void ExpectProblem(const std::string& part) const
        {
            auto check = Check();
            ASSERT_TRUE(check.problem.has_value()) << "no problem found";
            EXPECT_NE(std::string::npos, check.problem->find(part)) << *check.problem;
        }

        void ExpectConsistent() const
        {
            EXPECT_EQ(std::nullopt, Check().problem);
        }

        std::vector<uint8_t> Contents(const std::string& path) const
        {
            auto check = Check();
            const auto* entry = check.Find(path);
            if (entry == nullptr)
                return {};

            return metrics::ReadContents(image, check, *entry);
        }

        void Write(uint32_t address, const std::vector<uint8_t>& bytes)
        {
            std::ranges::copy(bytes, image.begin() + address);
        }

        void Fill(uint32_t address, uint32_t size, uint8_t value)
        {
            std::fill_n(image.begin() + address, size, value);
        }

        void Allocate(uint32_t block, fs::NorBlockType type, uint64_t sequence, fs::NorBlockOwner owner = {})
        {
            std::vector<uint8_t> partB(24);
            fs::SerializeNorHeaderB(fs::NorAllocation{ type, sequence, owner }, infra::MakeRange(partB));
            Write(block * blockSize + 12, partB);
        }

        void WriteSuperblock(uint32_t block, const fs::NorSuperblock& superblock)
        {
            std::vector<uint8_t> bytes(28);
            fs::SerializeNorSuperblock(superblock, infra::MakeRange(bytes));
            Write(block * blockSize, bytes);
        }

        void WriteNode(uint32_t address, const fs::NorNodeHeader& header, const std::vector<uint8_t>& entries)
        {
            std::vector<uint8_t> node(config.nodeSize, 0);
            std::ranges::copy(entries, node.begin() + 12);
            fs::SealNorNode(header, infra::MakeRange(node));
            Write(address, node);
        }

        std::vector<uint8_t> Inode(uint32_t parent, fs::NorInodeType type, std::string_view name, uint32_t size = 0, uint32_t root = 0, const std::vector<uint8_t>& inlineBytes = {}, uint8_t height = 1) const
        {
            auto nameBytes = Bytes(name);
            std::vector<uint8_t> record(fs::NorInodeRecordBytes(config));
            fs::SerializeNorInodeRecord(fs::NorInodeRecord{ parent, type, static_cast<uint8_t>(root != 0 ? height : 0), size, root, infra::MakeRange(inlineBytes), infra::MakeRange(nameBytes) }, infra::MakeRange(record), config);
            return record;
        }

        void WriteInodeLeaf(uint32_t address, const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& inodes)
        {
            std::vector<uint8_t> entries;
            for (const auto& [inode, record] : inodes)
            {
                entries.resize(entries.size() + 4);
                Put32(entries, entries.size() - 4, inode);
                entries.insert(entries.end(), record.begin(), record.end());
            }

            WriteNode(address, { fs::NorMap::inodeTable, 0, static_cast<uint16_t>(inodes.size()), 0, inodes.front().first }, entries);
        }

        void WritePairs(uint32_t address, fs::NorMap map, uint8_t level, uint32_t owner, const std::vector<std::pair<uint32_t, uint32_t>>& pairs)
        {
            std::vector<uint8_t> entries(pairs.size() * 8);
            for (std::size_t index = 0; index != pairs.size(); ++index)
            {
                Put32(entries, 8 * index, pairs[index].first);
                Put32(entries, 8 * index + 4, pairs[index].second);
            }

            WriteNode(address, { map, level, static_cast<uint16_t>(pairs.size()), owner, pairs.front().first }, entries);
        }

        void WriteDirectory(uint32_t address, uint32_t directory, std::vector<std::pair<uint32_t, uint32_t>> children)
        {
            std::ranges::sort(children);
            WritePairs(address, fs::NorMap::directory, 0, directory, children);
        }

        void WriteRadix(uint32_t address, uint8_t level, uint32_t inode, uint32_t lowestKey, const std::vector<uint32_t>& slots, std::optional<uint16_t> count = std::nullopt)
        {
            std::vector<uint8_t> entries(slots.size() * 4);
            for (std::size_t index = 0; index != slots.size(); ++index)
                Put32(entries, 4 * index, slots[index]);

            auto set = static_cast<uint16_t>(std::ranges::count_if(slots, [](uint32_t slot)
                {
                    return slot != 0;
                }));
            WriteNode(address, { fs::NorMap::fileData, level, count.value_or(set), inode, lowestKey }, entries);
        }

        void WriteDataBlock(uint32_t block, uint32_t inode, uint32_t index, uint64_t sequence, const std::vector<uint8_t>& contents)
        {
            Allocate(block, fs::NorBlockType::data, sequence, { inode, index });
            Write(block * blockSize + headerSize, contents);
        }

        void WriteCheckpoint(uint32_t block, const fs::NorCheckpoint& checkpoint, const std::vector<uint32_t>& used)
        {
            std::vector<uint8_t> bytes(fs::NorCheckpointBytes(checkpoint.blocks));
            fs::SerializeNorCheckpoint(checkpoint, infra::MakeRange(bytes));
            for (auto block : used)
                fs::MarkNorBlockInUse(infra::MakeRange(bytes), block);
            fs::SealNorCheckpoint(infra::MakeRange(bytes));
            Write(block * blockSize + headerSize, bytes);
        }

        uint32_t AppendRecord(uint32_t address, const std::vector<fs::NorEntry>& entries)
        {
            std::vector<uint8_t> buffer(1024);
            fs::CrcRecord record{ infra::MakeRange(buffer) };
            for (const auto& entry : entries)
                fs::AddNorEntry(record, entry);

            auto sealed = record.Seal();
            Write(address, std::vector<uint8_t>(sealed.begin(), sealed.end()));
            return address + static_cast<uint32_t>(sealed.size());
        }

        void WriteTree(uint32_t liveNodes = 4)
        {
            WriteInodeLeaf(NodeAddress(2, 0), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                                  { 2, Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2)) },
                                                  { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 3)) },
                                                  { 4, Inode(2, fs::NorInodeType::file, "c", fileC.size(), 0, fileC) } });
            WriteDirectory(NodeAddress(2, 1), 1, { { Key("a"), 2 }, { Key("b"), 3 } });
            WriteDirectory(NodeAddress(2, 2), 2, { { Key("c"), 4 } });
            WriteRadix(NodeAddress(2, 3), 0, 3, 0, { 6, 7 });
            WriteDataBlock(6, 3, 0, 3, { fileB.begin(), fileB.begin() + payload });
            WriteDataBlock(7, 3, 1, 4, { fileB.begin() + payload, fileB.end() });
            WriteTreeCheckpoint(liveNodes, { 0, 1, 2, 3, 6, 7 });
        }

        void WriteTreeCheckpoint(uint32_t liveNodes, const std::vector<uint32_t>& used, uint32_t root = NodeAddress(2, 0), uint8_t height = 1)
        {
            WriteCheckpoint(3, { root, height, 5, 3, firstRecord, liveNodes, 32 }, used);
        }

        std::vector<uint8_t> fileB{ Pattern(payload + 10, 3) };
        std::vector<uint8_t> fileC{ Pattern(20, 4) };
        std::vector<uint8_t> image{ FormattedImage<config>(32, sectorSize) };
    };
}

TEST_F(NorCheckerTest, accepts_a_formatted_volume)
{
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ((metrics::NorImageSuperblock{ 1, 4096, 32, 1, 512, 40, 24 }), check.superblock);
    EXPECT_EQ(2, check.validSuperblocks);
    EXPECT_EQ(headerSize, check.layout.headerSize);
    EXPECT_EQ(payload, check.layout.payloadSize);
    EXPECT_TRUE(check.entries.empty());
    EXPECT_EQ(0, check.records);
}

TEST_F(NorCheckerTest, classifies_the_blocks_of_a_formatted_volume)
{
    std::vector<Use> expected(32, Use::freeErased);
    expected[0] = Use::superblock;
    expected[1] = Use::superblock;
    expected[2] = Use::node;
    expected[3] = Use::checkpoint;

    EXPECT_EQ(expected, Check().blocks);
}

TEST_F(NorCheckerTest, reports_the_erase_count_of_every_block_but_the_superblocks)
{
    Fill(5 * blockSize, 12, 0xff);
    auto check = Check();

    ASSERT_EQ(32, check.eraseCounts.size());
    EXPECT_EQ(std::nullopt, check.eraseCounts[0]);
    EXPECT_EQ(std::nullopt, check.eraseCounts[1]);
    EXPECT_EQ(1, check.eraseCounts[2]);
    EXPECT_EQ(std::nullopt, check.eraseCounts[5]);
    EXPECT_EQ(1, check.eraseCounts[31]);
    EXPECT_EQ(Use::free, check.blocks[5]);
}

TEST_F(NorCheckerTest, reports_an_image_without_a_valid_superblock)
{
    Fill(0, 28, 0xff);
    Fill(blockSize, 28, 0xff);

    ExpectProblem("no valid superblock");
    EXPECT_EQ(std::nullopt, Check().superblock);
}

TEST_F(NorCheckerTest, reads_the_second_superblock_when_the_first_is_not_valid)
{
    image[5] ^= 1;
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(1, check.validSuperblocks);
    EXPECT_EQ(Use::superblock, check.blocks[0]);
}

TEST_F(NorCheckerTest, reports_superblocks_that_differ)
{
    WriteSuperblock(1, { 1, 4096, 32, 1, 512, 40, 23 });

    ExpectProblem("the superblocks differ");
}

TEST_F(NorCheckerTest, reports_a_superblock_that_names_another_number_of_blocks)
{
    WriteSuperblock(0, { 1, 4096, 31, 1, 512, 40, 24 });
    WriteSuperblock(1, { 1, 4096, 31, 1, 512, 40, 24 });

    ExpectProblem("the superblock names 31 blocks, the device holds 32");
}

TEST_F(NorCheckerTest, reports_a_superblock_that_names_another_sector_size)
{
    auto check = metrics::CheckNorImage(image, 2048, pageSize);

    ASSERT_TRUE(check.problem.has_value());
    EXPECT_NE(std::string::npos, check.problem->find("sectors of 4096 bytes")) << *check.problem;
}

TEST_F(NorCheckerTest, reports_format_parameters_that_are_not_valid)
{
    WriteSuperblock(0, { 1, 4096, 32, 3, 512, 40, 24 });
    WriteSuperblock(1, { 1, 4096, 32, 3, 512, 40, 24 });

    ExpectProblem("the format parameters are not valid");
}

TEST_F(NorCheckerTest, finds_the_second_superblock_of_blocks_of_two_sectors)
{
    image = FormattedImage<twoSectorConfig>(64, 2048);
    std::fill_n(image.begin(), 28, 0xff);
    auto check = metrics::CheckNorImage(image, 2048, pageSize);

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(1, check.validSuperblocks);
    EXPECT_EQ(2, check.superblock->sectorsPerBlock);
}

TEST_F(NorCheckerTest, accepts_a_volume_with_a_program_unit_of_16_bytes)
{
    image = FormattedImage<wideUnitConfig>(48, 2048);
    auto check = metrics::CheckNorImage(image, 2048, 16);

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(48, check.layout.headerSize);
    EXPECT_EQ(48, check.layout.nodeSlotsOffset);
}

TEST_F(NorCheckerTest, reports_two_blocks_with_the_same_sequence)
{
    Allocate(5, fs::NorBlockType::data, 1, { 9, 0 });

    ExpectProblem("blocks 2 and 5 have the same sequence 1");
}

TEST_F(NorCheckerTest, reports_a_volume_without_a_valid_checkpoint)
{
    image[3 * blockSize + headerSize + 5] ^= 1;

    ExpectProblem("no valid checkpoint");
}

TEST_F(NorCheckerTest, uses_the_checkpoint_with_the_highest_sequence)
{
    Allocate(4, fs::NorBlockType::checkpoint, 3);
    WriteCheckpoint(4, { NodeAddress(2, 0), 1, 2, 4, firstRecord, 1, 32 }, { 0, 1, 2, 4 });
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(Use::free, check.blocks[3]);
    EXPECT_EQ(Use::checkpoint, check.blocks[4]);
}

TEST_F(NorCheckerTest, skips_a_checkpoint_whose_crc_is_not_valid)
{
    Allocate(4, fs::NorBlockType::checkpoint, 3);
    WriteCheckpoint(4, { NodeAddress(2, 0), 1, 2, 4, firstRecord, 1, 32 }, { 0, 1, 2, 4 });
    image[4 * blockSize + headerSize + 34] ^= 1;
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(Use::checkpoint, check.blocks[3]);
    EXPECT_EQ(Use::free, check.blocks[4]);
}

TEST_F(NorCheckerTest, never_skips_a_valid_checkpoint_that_is_not_consistent)
{
    Allocate(4, fs::NorBlockType::checkpoint, 3);
    WriteCheckpoint(4, { NodeAddress(2, 0), 1, 2, 4, firstRecord, 2, 32 }, { 0, 1, 2, 4 });

    ExpectProblem("the checkpoint counts 2 live nodes, the maps hold 1");
}

TEST_F(NorCheckerTest, reports_a_checkpoint_for_another_number_of_blocks)
{
    WriteCheckpoint(3, { NodeAddress(2, 0), 1, 2, 3, firstRecord, 1, 30 }, { 0, 1, 2, 3 });

    ExpectProblem("the checkpoint names 30 blocks");
}

TEST_F(NorCheckerTest, reports_a_used_block_that_the_checkpoint_does_not_mark)
{
    WriteCheckpoint(3, { NodeAddress(2, 0), 1, 2, 3, firstRecord, 1, 32 }, { 0, 1, 3 });

    ExpectProblem("block 2 is used but not marked in use");
}

TEST_F(NorCheckerTest, reports_a_block_that_the_checkpoint_marks_but_nothing_uses)
{
    WriteCheckpoint(3, { NodeAddress(2, 0), 1, 2, 3, firstRecord, 1, 32 }, { 0, 1, 2, 3, 9 });

    ExpectProblem("block 9 is marked in use, but nothing uses it");
}

TEST_F(NorCheckerTest, accepts_a_node_block_whose_nodes_were_all_replaced)
{
    Allocate(9, fs::NorBlockType::node, 3);
    Write(NodeAddress(9, 0), std::vector<uint8_t>(image.begin() + NodeAddress(2, 0), image.begin() + NodeAddress(2, 0) + config.nodeSize));
    Allocate(10, fs::NorBlockType::checkpoint, 4);
    WriteCheckpoint(10, { NodeAddress(9, 0), 1, 2, 10, firstRecord, 1, 32 }, { 0, 1, 2, 9, 10 });

    ExpectConsistent();
    EXPECT_EQ(Use::node, Check().blocks[2]);
}

TEST_F(NorCheckerTest, lists_files_and_directories_with_their_paths)
{
    WriteTree();
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    ASSERT_EQ(3, check.entries.size());
    ASSERT_NE(nullptr, check.Find("/a"));
    EXPECT_TRUE(check.Find("/a")->directory);
    ASSERT_NE(nullptr, check.Find("/b"));
    EXPECT_FALSE(check.Find("/b")->directory);
    EXPECT_EQ(fileB.size(), check.Find("/b")->size);
    ASSERT_NE(nullptr, check.Find("/a/c"));
    EXPECT_EQ(4, check.Find("/a/c")->inode);
    EXPECT_EQ(nullptr, check.Find("/c"));
}

TEST_F(NorCheckerTest, reads_files_from_their_data_blocks_and_inline_areas)
{
    WriteTree();

    EXPECT_EQ(fileB, Contents("/b"));
    EXPECT_EQ(fileC, Contents("/a/c"));
}

TEST_F(NorCheckerTest, classifies_the_data_blocks_of_files)
{
    WriteTree();
    auto check = Check();

    EXPECT_EQ(Use::data, check.blocks[6]);
    EXPECT_EQ(Use::data, check.blocks[7]);
    EXPECT_EQ(Use::freeErased, check.blocks[8]);
}

TEST_F(NorCheckerTest, reads_an_inode_table_of_two_levels)
{
    WriteTree(6);
    WriteInodeLeaf(NodeAddress(2, 4), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                          { 2, Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2)) } });
    WriteInodeLeaf(NodeAddress(2, 5), { { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 3)) },
                                          { 4, Inode(2, fs::NorInodeType::file, "c", fileC.size(), 0, fileC) } });
    WritePairs(NodeAddress(2, 6), fs::NorMap::inodeTable, 1, 0, { { 1, NodeAddress(2, 4) }, { 3, NodeAddress(2, 5) } });
    WriteTreeCheckpoint(6, { 0, 1, 2, 3, 6, 7 }, NodeAddress(2, 6), 2);

    ExpectConsistent();
    EXPECT_EQ(fileB, Contents("/b"));
}

TEST_F(NorCheckerTest, reports_an_inner_key_above_the_keys_of_its_child)
{
    WriteTree(6);
    WriteInodeLeaf(NodeAddress(2, 4), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                          { 2, Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2)) } });
    WriteInodeLeaf(NodeAddress(2, 5), { { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 3)) },
                                          { 4, Inode(2, fs::NorInodeType::file, "c", fileC.size(), 0, fileC) } });
    WritePairs(NodeAddress(2, 6), fs::NorMap::inodeTable, 1, 0, { { 1, NodeAddress(2, 4) }, { 4, NodeAddress(2, 5) } });
    WriteTreeCheckpoint(6, { 0, 1, 2, 3, 6, 7 }, NodeAddress(2, 6), 2);

    ExpectProblem("the keys of node at");
}

TEST_F(NorCheckerTest, reports_keys_that_are_not_in_order)
{
    WriteTree();
    WritePairs(NodeAddress(2, 1), fs::NorMap::directory, 0, 1, { { std::max(Key("a"), Key("b")), Key("a") > Key("b") ? 2u : 3u }, { std::min(Key("a"), Key("b")), Key("a") > Key("b") ? 3u : 2u } });

    ExpectProblem("are not in order");
}

TEST_F(NorCheckerTest, reports_a_node_with_a_bad_crc)
{
    image[NodeAddress(2, 0) + 20] ^= 1;

    ExpectProblem("has a bad CRC");
}

TEST_F(NorCheckerTest, reports_a_node_outside_a_node_block)
{
    std::copy_n(image.begin() + NodeAddress(2, 0), config.nodeSize, image.begin() + NodeAddress(5, 0));
    WriteCheckpoint(3, { NodeAddress(5, 0), 1, 2, 3, firstRecord, 1, 32 }, { 0, 1, 3, 5 });

    ExpectProblem("is not in a node block");
}

TEST_F(NorCheckerTest, reports_a_node_address_that_is_not_a_node_slot)
{
    WriteCheckpoint(3, { NodeAddress(2, 0) + 4, 1, 2, 3, firstRecord, 1, 32 }, { 0, 1, 2, 3 });

    ExpectProblem("is not a node slot");
}

TEST_F(NorCheckerTest, reports_a_node_of_another_map)
{
    WriteTree();
    WriteRadix(NodeAddress(2, 3), 0, 4, 0, { 6, 7 });

    ExpectProblem("belongs to another map");
}

TEST_F(NorCheckerTest, reports_a_node_that_is_reached_twice)
{
    WriteTree(5);
    WriteRadix(NodeAddress(2, 4), 1, 3, 0, { NodeAddress(2, 3), NodeAddress(2, 3) });
    WriteInodeLeaf(NodeAddress(2, 0), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                          { 2, Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2)) },
                                          { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 4), {}, 2) },
                                          { 4, Inode(2, fs::NorInodeType::file, "c", fileC.size(), 0, fileC) } });

    ExpectProblem("is reached twice");
}

TEST_F(NorCheckerTest, reads_a_data_map_of_two_levels)
{
    WriteTree(5);
    WriteRadix(NodeAddress(2, 4), 1, 3, 0, { NodeAddress(2, 3) });
    WriteInodeLeaf(NodeAddress(2, 0), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                          { 2, Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2)) },
                                          { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 4), {}, 2) },
                                          { 4, Inode(2, fs::NorInodeType::file, "c", fileC.size(), 0, fileC) } });

    ExpectConsistent();
    EXPECT_EQ(fileB, Contents("/b"));
}

TEST_F(NorCheckerTest, reports_a_radix_node_that_covers_other_indices)
{
    WriteTree();
    WriteRadix(NodeAddress(2, 3), 0, 3, 124, { 6, 7 });

    ExpectProblem("covers index 124 instead of 0");
}

TEST_F(NorCheckerTest, reports_a_radix_node_whose_count_is_not_its_number_of_set_slots)
{
    WriteTree();
    WriteRadix(NodeAddress(2, 3), 0, 3, 0, { 6, 7 }, 3);

    ExpectProblem("counts 3 entries, 2 are set");
}

TEST_F(NorCheckerTest, reports_an_inode_record_that_is_not_valid)
{
    WriteTree();
    auto record = Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2));
    record[4] = 7;
    WriteInodeLeaf(NodeAddress(2, 0), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                          { 2, record },
                                          { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 3)) },
                                          { 4, Inode(2, fs::NorInodeType::file, "c", fileC.size(), 0, fileC) } });

    ExpectProblem("the record of inode 2 is not valid");
}

TEST_F(NorCheckerTest, reports_an_entry_whose_key_does_not_match_its_name)
{
    WriteTree();
    WriteDirectory(NodeAddress(2, 1), 1, { { Key("x"), 2 }, { Key("b"), 3 } });

    ExpectProblem("does not match its name");
}

TEST_F(NorCheckerTest, reports_two_entries_with_the_same_name)
{
    WriteTree();
    WriteDirectory(NodeAddress(2, 2), 2, { { Key("c"), 4 }, { Key("c", 1), 5 } });
    WriteInodeLeaf(NodeAddress(2, 0), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                          { 2, Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2)) },
                                          { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 3)) },
                                          { 4, Inode(2, fs::NorInodeType::file, "c", fileC.size(), 0, fileC) },
                                          { 5, Inode(2, fs::NorInodeType::file, "c") } });
    WriteCheckpoint(3, { NodeAddress(2, 0), 1, 6, 3, firstRecord, 4, 32 }, { 0, 1, 2, 3, 6, 7 });

    ExpectProblem("holds the name c twice");
}

TEST_F(NorCheckerTest, reports_an_inode_in_no_directory)
{
    WriteTree();
    WriteDirectory(NodeAddress(2, 1), 1, { { Key("a"), 2 } });

    ExpectProblem("inode 3 is in no directory");
}

TEST_F(NorCheckerTest, reports_an_entry_of_an_inode_that_does_not_exist)
{
    WriteTree();
    WriteDirectory(NodeAddress(2, 2), 2, { { Key("c"), 4 }, { Key("d"), 9 } });

    ExpectProblem("directory 2 names inode 9, which does not exist");
}

TEST_F(NorCheckerTest, reports_an_inode_whose_parent_is_another_directory)
{
    WriteTree();
    WriteInodeLeaf(NodeAddress(2, 0), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                          { 2, Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2)) },
                                          { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 3)) },
                                          { 4, Inode(1, fs::NorInodeType::file, "c", fileC.size(), 0, fileC) } });

    ExpectProblem("inode 4 is in directory 2, but names directory 1 as its parent");
}

TEST_F(NorCheckerTest, reports_an_inode_number_that_is_not_below_the_next_one)
{
    WriteTree();
    WriteCheckpoint(3, { NodeAddress(2, 0), 1, 4, 3, firstRecord, 4, 32 }, { 0, 1, 2, 3, 6, 7 });

    ExpectProblem("inode 4 is not below the next inode number 4");
}

TEST_F(NorCheckerTest, reports_a_data_block_owned_by_another_position)
{
    WriteTree();
    Allocate(7, fs::NorBlockType::data, 4, { 3, 5 });

    ExpectProblem("data block 7 does not name inode 3 and index 1 as its owner");
}

TEST_F(NorCheckerTest, reports_a_block_used_twice)
{
    WriteTree();
    WriteRadix(NodeAddress(2, 3), 0, 3, 0, { 6, 6 });
    WriteTreeCheckpoint(4, { 0, 1, 2, 3, 6 });

    ExpectProblem("block 6 is used twice");
}

TEST_F(NorCheckerTest, reports_a_file_without_a_block_for_an_index_before_its_last)
{
    WriteTree();
    WriteRadix(NodeAddress(2, 3), 0, 3, 0, { 0, 7 });
    WriteTreeCheckpoint(4, { 0, 1, 2, 3, 7 });

    ExpectProblem("file 3 has no block for index 0");
}

TEST_F(NorCheckerTest, reports_a_block_mapped_beyond_the_size_of_its_file)
{
    WriteTree();
    WriteRadix(NodeAddress(2, 3), 0, 3, 0, { 6, 7, 8 });
    WriteDataBlock(8, 3, 2, 5, Pattern(10, 8));
    WriteTreeCheckpoint(4, { 0, 1, 2, 3, 6, 7, 8 });

    ExpectProblem("file 3 maps index 2 beyond its size");
}

TEST_F(NorCheckerTest, reports_inline_bytes_that_do_not_match_the_size)
{
    WriteTree();
    WriteInodeLeaf(NodeAddress(2, 0), { { 1, Inode(0, fs::NorInodeType::directory, "", 0, NodeAddress(2, 1)) },
                                          { 2, Inode(1, fs::NorInodeType::directory, "a", 0, NodeAddress(2, 2)) },
                                          { 3, Inode(1, fs::NorInodeType::file, "b", fileB.size(), NodeAddress(2, 3)) },
                                          { 4, Inode(2, fs::NorInodeType::file, "c", 21, 0, fileC) } });

    ExpectProblem("file 4 holds 20 inline bytes, its size needs 21");
}

TEST_F(NorCheckerTest, reports_a_used_block_without_a_valid_part_a)
{
    WriteTree();
    image[6 * blockSize + 1] ^= 1;

    ExpectProblem("block 6 is in use without a valid part A");
}

TEST_F(NorCheckerTest, reports_a_free_erased_block_that_holds_programmed_bytes)
{
    image[10 * blockSize + 100] = 0;

    ExpectProblem("block 10 looks free and erased, but holds programmed bytes");
}

TEST_F(NorCheckerTest, replays_the_records_after_the_checkpoint)
{
    WriteTree();
    AppendRecord(3 * blockSize + firstRecord, { fs::NorDirectoryEntryPut{ 1, Key("d"), 5 }, fs::NorInodePut{ 5, infra::MakeRange(Inode(1, fs::NorInodeType::file, "d", 3, 0, Bytes("xyz"))) } });
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(1, check.records);
    EXPECT_EQ(Bytes("xyz"), Contents("/d"));
}

TEST_F(NorCheckerTest, stops_at_a_record_whose_crc_is_not_valid)
{
    WriteTree();
    auto end = AppendRecord(3 * blockSize + firstRecord, { fs::NorDirectoryEntryPut{ 1, Key("d"), 5 }, fs::NorInodePut{ 5, infra::MakeRange(Inode(1, fs::NorInodeType::file, "d", 3, 0, Bytes("xyz"))) } });
    auto next = AppendRecord(end, { fs::NorDirectoryEntryRemove{ 1, Key("d") }, fs::NorInodeRemove{ 5 } });
    image[next - 1] ^= 1;
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(1, check.records);
    EXPECT_NE(nullptr, check.Find("/d"));
}

TEST_F(NorCheckerTest, follows_the_journal_into_a_linked_journal_block)
{
    WriteTree();
    auto end = AppendRecord(3 * blockSize + firstRecord, { fs::NorDirectoryEntryPut{ 1, Key("d"), 5 }, fs::NorInodePut{ 5, infra::MakeRange(Inode(1, fs::NorInodeType::file, "d", 3, 0, Bytes("xyz"))) } });
    Allocate(8, fs::NorBlockType::journal, 5);
    AppendRecord(8 * blockSize + headerSize, { fs::NorLink{ 3, end - 3 * blockSize }, fs::NorDirectoryEntryRemove{ 1, Key("d") }, fs::NorInodeRemove{ 5 } });
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(2, check.records);
    EXPECT_EQ(nullptr, check.Find("/d"));
    EXPECT_EQ(Use::journal, check.blocks[8]);
}

TEST_F(NorCheckerTest, skips_a_journal_block_whose_link_names_another_end)
{
    WriteTree();
    auto end = AppendRecord(3 * blockSize + firstRecord, { fs::NorDirectoryEntryPut{ 1, Key("d"), 5 }, fs::NorInodePut{ 5, infra::MakeRange(Inode(1, fs::NorInodeType::file, "d", 3, 0, Bytes("xyz"))) } });
    Allocate(8, fs::NorBlockType::journal, 5);
    AppendRecord(8 * blockSize + headerSize, { fs::NorLink{ 3, end - 3 * blockSize + 1 }, fs::NorDirectoryEntryRemove{ 1, Key("d") }, fs::NorInodeRemove{ 5 } });
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(1, check.records);
    EXPECT_NE(nullptr, check.Find("/d"));
    EXPECT_EQ(Use::free, check.blocks[8]);
}

TEST_F(NorCheckerTest, follows_the_linked_journal_block_with_the_lowest_sequence)
{
    WriteTree();
    Allocate(8, fs::NorBlockType::journal, 6);
    AppendRecord(8 * blockSize + headerSize, { fs::NorLink{ 3, firstRecord }, fs::NorDirectoryEntryPut{ 1, Key("d"), 5 }, fs::NorInodePut{ 5, infra::MakeRange(Inode(1, fs::NorInodeType::file, "d", 3, 0, Bytes("xyz"))) } });
    Allocate(9, fs::NorBlockType::journal, 5);
    AppendRecord(9 * blockSize + headerSize, { fs::NorLink{ 3, firstRecord }, fs::NorDirectoryEntryPut{ 1, Key("e"), 5 }, fs::NorInodePut{ 5, infra::MakeRange(Inode(1, fs::NorInodeType::file, "e", 3, 0, Bytes("abc"))) } });
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(Bytes("abc"), Contents("/e"));
    EXPECT_EQ(nullptr, check.Find("/d"));
    EXPECT_EQ(Use::free, check.blocks[8]);
    EXPECT_EQ(Use::journal, check.blocks[9]);
}

TEST_F(NorCheckerTest, replays_block_sets_and_sizes)
{
    WriteTree();
    auto grown = Pattern(payload + 20, 3);
    std::copy_n(fileB.begin(), payload, grown.begin());
    WriteDataBlock(8, 3, 1, 5, { grown.begin() + payload, grown.end() });
    AppendRecord(3 * blockSize + firstRecord, { fs::NorBlockSet{ 3, 1, 7, 8 }, fs::NorSize{ 3, payload + 20, {} } });
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(grown, Contents("/b"));
    EXPECT_EQ(Use::data, check.blocks[8]);
    EXPECT_EQ(Use::free, check.blocks[7]);
}

TEST_F(NorCheckerTest, a_size_releases_the_blocks_beyond_it)
{
    WriteTree();
    AppendRecord(3 * blockSize + firstRecord, { fs::NorSize{ 3, 5, {} } });
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(std::vector<uint8_t>(fileB.begin(), fileB.begin() + 5), Contents("/b"));
    EXPECT_EQ(Use::free, check.blocks[7]);
}

TEST_F(NorCheckerTest, replays_a_rename)
{
    WriteTree();
    AppendRecord(3 * blockSize + firstRecord, { fs::NorDirectoryEntryRemove{ 1, Key("b") }, fs::NorDirectoryEntryPut{ 2, Key("e"), 3 }, fs::NorInodePut{ 3, infra::MakeRange(Inode(2, fs::NorInodeType::file, "e", fileB.size(), NodeAddress(2, 3))) } });
    auto check = Check();

    EXPECT_EQ(std::nullopt, check.problem);
    EXPECT_EQ(nullptr, check.Find("/b"));
    EXPECT_EQ(fileB, Contents("/a/e"));
}

TEST_F(NorCheckerTest, reports_a_block_set_whose_previous_block_is_not_the_mapped_one)
{
    WriteTree();
    WriteDataBlock(8, 3, 1, 5, Pattern(10, 8));
    AppendRecord(3 * blockSize + firstRecord, { fs::NorBlockSet{ 3, 1, 9, 8 } });

    ExpectProblem("a block set of inode 3 at index 1 expects block 9, the map holds block 7");
}

TEST_F(NorCheckerTest, reports_a_link_that_does_not_start_a_journal_block)
{
    WriteTree();
    AppendRecord(3 * blockSize + firstRecord, { fs::NorLink{ 1, 2 } });

    ExpectProblem("a link that does not start a journal block");
}

TEST_F(NorCheckerTest, reports_a_record_with_an_entry_of_unknown_kind)
{
    WriteTree();
    std::vector<uint8_t> buffer(64);
    fs::CrcRecord record{ infra::MakeRange(buffer) };
    record.Add(5)[0] = 9;
    auto sealed = record.Seal();
    Write(3 * blockSize + firstRecord, std::vector<uint8_t>(sealed.begin(), sealed.end()));

    ExpectProblem("is malformed");
}
