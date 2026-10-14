#include "metrics/nor/NorChecker.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Crc.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <set>
#include <utility>

namespace metrics
{
    namespace
    {
        constexpr uint32_t superblockMagic = 0x4e534645;
        constexpr uint32_t blockMagic = 0x42534645;
        constexpr uint32_t superblockBytes = 28;
        constexpr uint32_t partABytes = 12;
        constexpr uint32_t partBBytes = 24;
        constexpr uint32_t nodeOverhead = 16;
        constexpr uint32_t inodeRecordFixedBytes = 20;
        constexpr uint32_t checkpointFixedBytes = 28;
        constexpr uint32_t crcBytes = 4;
        constexpr uint32_t recordOverhead = 8;
        constexpr uint32_t erasedLength = 0xffff;
        constexpr uint32_t maxLevels = 3;
        constexpr uint32_t rootInode = 1;
        constexpr uint32_t superblocks = 2;
        constexpr uint64_t largestFileBlocks = uint64_t{ 1 } << 32;
        constexpr uint8_t fileType = 1;
        constexpr uint8_t directoryType = 2;

        enum class BlockType : uint8_t
        {
            checkpoint = 1,
            journal = 2,
            node = 3,
            data = 4
        };

        enum class Map : uint8_t
        {
            inodeTable = 1,
            directory = 2,
            fileData = 3
        };

        enum class Kind : uint8_t
        {
            link = 1,
            inodePut = 2,
            inodeRemove = 3,
            directoryPut = 4,
            directoryRemove = 5,
            blockSet = 6,
            size = 7
        };

        struct Allocation
        {
            BlockType type{ BlockType::data };
            uint64_t sequence{ 0 };
            uint32_t inode{ 0 };
            uint32_t index{ 0 };
        };

        struct Header
        {
            std::optional<uint32_t> eraseCount;
            std::optional<Allocation> allocation;
            bool partBErased{ false };
        };

        struct Inode
        {
            uint32_t parent{ 0 };
            uint8_t type{ 0 };
            uint32_t size{ 0 };
            uint32_t root{ 0 };
            uint8_t height{ 0 };
            std::vector<uint8_t> inlineBytes;
            std::vector<uint8_t> name;
            std::map<uint32_t, uint32_t> blocks;
            std::map<uint32_t, uint32_t> children;
        };

        struct Checkpoint
        {
            uint32_t block{ 0 };
            uint32_t root{ 0 };
            uint8_t height{ 0 };
            uint32_t nextInode{ 0 };
            uint32_t recordsBlock{ 0 };
            uint32_t recordsOffset{ 0 };
            uint32_t liveNodes{ 0 };
            uint32_t blocks{ 0 };
        };

        struct Node
        {
            uint16_t count{ 0 };
            uint32_t lowestKey{ 0 };
            uint32_t entries{ 0 };
        };

        struct Record
        {
            uint32_t address{ 0 };
            uint32_t length{ 0 };
            uint8_t entries{ 0 };
        };

        struct Entry
        {
            Kind kind{ Kind::link };
            std::array<uint32_t, 4> fields{};
            uint32_t bytes{ 0 };
            uint32_t byteCount{ 0 };
        };

        struct TreeWalk
        {
            Map map{ Map::inodeTable };
            uint32_t owner{ 0 };
            uint32_t valueBytes{ 0 };
            std::function<bool(uint32_t key, uint32_t value)> onLeafEntry;
        };

        using Roles = std::vector<std::optional<NorBlockUse>>;

        uint32_t RoundUp(uint32_t size, uint32_t unit)
        {
            return (size + unit - 1) / unit * unit;
        }

        std::optional<uint32_t> FieldsOf(uint8_t kind)
        {
            switch (static_cast<Kind>(kind))
            {
                case Kind::link:
                case Kind::directoryRemove:
                case Kind::size:
                    return 2;
                case Kind::inodePut:
                case Kind::inodeRemove:
                    return 1;
                case Kind::directoryPut:
                    return 3;
                case Kind::blockSet:
                    return 4;
                default:
                    return std::nullopt;
            }
        }

        std::string Text(const std::vector<uint8_t>& bytes)
        {
            return { bytes.begin(), bytes.end() };
        }

        class Checker
        {
        public:
            Checker(const std::vector<uint8_t>& image, uint32_t sectorSize, uint32_t pageSize)
                : image(image)
                , sectorSize(sectorSize)
                , pageSize(pageSize)
                , sectors(static_cast<uint32_t>(image.size() / sectorSize))
            {}

            NorCheck Run()
            {
                if (ReadSuperblocks() && CheckParameters())
                {
                    ReadHeaders();
                    if (CheckSequences() && LoadCheckpoint() && LoadMaps() && CheckBitmap() && Replay() && CheckTree() && CheckFiles() && ClassifyBlocks())
                        ListEntries();
                }

                return std::move(check);
            }

        private:
            bool Fail(const std::string& reason)
            {
                if (!check.problem)
                    check.problem = reason;

                return false;
            }

            uint32_t Load16(std::size_t address) const
            {
                return image[address] | static_cast<uint32_t>(image[address + 1]) << 8;
            }

            uint32_t Load32(std::size_t address) const
            {
                return Load16(address) | Load16(address + 2) << 16;
            }

            uint64_t Load64(std::size_t address) const
            {
                return Load32(address) | static_cast<uint64_t>(Load32(address + 4)) << 32;
            }

            uint32_t Crc(std::size_t address, std::size_t size) const
            {
                infra::Crc32 crc;
                crc.Update(infra::Head(infra::DiscardHead(infra::MakeRange(image), address), size));
                return crc.Result();
            }

            bool IsSealed(std::size_t address, std::size_t size) const
            {
                return Load32(address + size) == Crc(address, size);
            }

            bool IsErased(std::size_t address, std::size_t size) const
            {
                return std::all_of(image.begin() + static_cast<std::ptrdiff_t>(address), image.begin() + static_cast<std::ptrdiff_t>(address + size), [](uint8_t byte)
                    {
                        return byte == 0xff;
                    });
            }

            uint32_t AddressOfBlock(uint32_t block) const
            {
                return block * blockSize;
            }

            bool IsOfType(uint32_t block, BlockType type) const
            {
                const auto& allocation = headers[block].allocation;
                return allocation.has_value() && allocation->type == type;
            }

            uint64_t SequenceOf(uint32_t block) const
            {
                return headers[block].allocation->sequence;
            }

            std::optional<NorImageSuperblock> SuperblockAt(uint32_t sector) const
            {
                std::size_t address = static_cast<std::size_t>(sector) * sectorSize;
                if (sector >= sectors || Load32(address) != superblockMagic || Load16(address + 4) != 1 || !IsSealed(address, superblockBytes - crcBytes))
                    return std::nullopt;

                return NorImageSuperblock{ Load16(address + 6), Load32(address + 8), Load32(address + 12), Load16(address + 16), Load16(address + 18), Load16(address + 20), image[address + 22] };
            }

            std::optional<NorImageSuperblock> SecondSuperblock(const std::optional<NorImageSuperblock>& first) const
            {
                if (first.has_value())
                    return first->sectorsPerBlock != 0 ? SuperblockAt(first->sectorsPerBlock) : std::nullopt;

                for (uint32_t sector = 1; sector < sectors; ++sector)
                    if (auto second = SuperblockAt(sector); second.has_value() && second->sectorsPerBlock == sector)
                        return second;

                return std::nullopt;
            }

            bool ReadSuperblocks()
            {
                auto first = SuperblockAt(0);
                auto second = SecondSuperblock(first);
                check.validSuperblocks = static_cast<uint32_t>(first.has_value()) + static_cast<uint32_t>(second.has_value());
                check.superblock = first.has_value() ? first : second;

                if (!check.superblock.has_value())
                    return Fail("no valid superblock");

                if (first.has_value() && second.has_value() && *first != *second)
                    return Fail("the superblocks differ");

                superblock = *check.superblock;
                return true;
            }

            bool CheckParameters()
            {
                if (superblock.sectorSize != sectorSize)
                    return Fail("the superblock names sectors of " + std::to_string(superblock.sectorSize) + " bytes, the device has sectors of " + std::to_string(sectorSize) + " bytes");

                if (superblock.sectorsPerBlock == 0 || superblock.sectorsPerBlock > sectors)
                    return Fail("the format parameters are not valid");

                if (superblock.blocks != sectors / superblock.sectorsPerBlock)
                    return Fail("the superblock names " + std::to_string(superblock.blocks) + " blocks, the device holds " + std::to_string(sectors / superblock.sectorsPerBlock));

                blockSize = superblock.sectorsPerBlock * sectorSize;
                programUnit = superblock.programUnit;
                if (!std::has_single_bit(programUnit) || programUnit > blockSize || superblock.nodeSize <= nodeOverhead || superblock.maxNameBytes == 0)
                    return Fail("the format parameters are not valid");

                partBOffset = RoundUp(partABytes, programUnit);
                headerSize = partBOffset + RoundUp(partBBytes, programUnit);
                slotsOffset = RoundUp(headerSize, pageSize);
                nodeSize = superblock.nodeSize;
                recordBytes = inodeRecordFixedBytes + superblock.inlineBytes + superblock.maxNameBytes;
                checkpointBytes = checkpointFixedBytes + (superblock.blocks + 7) / 8 + crcBytes;

                if (slotsOffset + nodeSize > blockSize || superblock.inlineBytes < headerSize || checkpointBytes > blockSize - headerSize || superblock.blocks < 4 || (nodeSize - nodeOverhead) / (4 + recordBytes) < 2)
                    return Fail("the format parameters are not valid");

                check.layout = NorImageLayout{ blockSize, headerSize, blockSize - headerSize, slotsOffset };
                return true;
            }

            void ReadHeaders()
            {
                headers.resize(superblock.blocks);
                check.eraseCounts.assign(superblock.blocks, std::nullopt);

                for (uint32_t block = superblocks; block != superblock.blocks; ++block)
                {
                    auto address = AddressOfBlock(block);
                    auto& header = headers[block];

                    if (Load32(address) == blockMagic && IsSealed(address, partABytes - crcBytes))
                        header.eraseCount = Load32(address + 4);

                    auto partB = address + partBOffset;
                    auto type = image[partB];
                    if (type >= static_cast<uint8_t>(BlockType::checkpoint) && type <= static_cast<uint8_t>(BlockType::data) && IsSealed(partB, partBBytes - crcBytes))
                        header.allocation = Allocation{ static_cast<BlockType>(type), Load64(partB + 4), Load32(partB + 12), Load32(partB + 16) };

                    header.partBErased = IsErased(partB, RoundUp(partBBytes, programUnit));
                    check.eraseCounts[block] = header.eraseCount;
                }
            }

            bool CheckSequences()
            {
                std::map<uint64_t, uint32_t> owners;

                for (uint32_t block = superblocks; block != superblock.blocks; ++block)
                    if (headers[block].allocation.has_value())
                        if (auto [owner, added] = owners.emplace(SequenceOf(block), block); !added)
                            return Fail("blocks " + std::to_string(owner->second) + " and " + std::to_string(block) + " have the same sequence " + std::to_string(owner->first));

                return true;
            }

            bool HasValidCheckpoint(uint32_t block) const
            {
                auto address = AddressOfBlock(block) + headerSize;
                uint64_t bitmapBytes = (uint64_t{ Load32(address + 24) } + 7) / 8;

                return checkpointFixedBytes + bitmapBytes + crcBytes <= blockSize - headerSize && IsSealed(address, checkpointFixedBytes + static_cast<std::size_t>(bitmapBytes));
            }

            bool LoadCheckpoint()
            {
                std::optional<uint32_t> chosen;
                for (uint32_t block = superblocks; block != superblock.blocks; ++block)
                    if (IsOfType(block, BlockType::checkpoint) && HasValidCheckpoint(block) && (!chosen.has_value() || SequenceOf(block) > SequenceOf(*chosen)))
                        chosen = block;

                if (!chosen.has_value())
                    return Fail("no valid checkpoint");

                checkpointAddress = AddressOfBlock(*chosen) + headerSize;
                checkpoint = Checkpoint{ *chosen, Load32(checkpointAddress), image[checkpointAddress + 4], Load32(checkpointAddress + 8), Load32(checkpointAddress + 12), Load32(checkpointAddress + 16), Load32(checkpointAddress + 20), Load32(checkpointAddress + 24) };
                nextInode = checkpoint.nextInode;

                if (checkpoint.blocks != superblock.blocks)
                    return Fail("the checkpoint names " + std::to_string(checkpoint.blocks) + " blocks, the volume has " + std::to_string(superblock.blocks));

                return CheckRecordsStart();
            }

            bool CheckRecordsStart()
            {
                auto block = checkpoint.recordsBlock;
                auto offset = checkpoint.recordsOffset;
                auto lowest = block == checkpoint.block ? headerSize + RoundUp(checkpointBytes, programUnit) : headerSize;

                if (block < superblocks || block >= superblock.blocks || !(IsOfType(block, BlockType::checkpoint) || IsOfType(block, BlockType::journal)) || offset % programUnit != 0 || offset < lowest || offset > blockSize)
                    return Fail("the records of the epoch do not start in a checkpoint or journal block");

                return true;
            }

            bool IsMarkedInUse(uint32_t block) const
            {
                return (image[checkpointAddress + checkpointFixedBytes + block / 8] >> (block % 8) & 1) != 0;
            }

            std::optional<Inode> ParseInode(uint32_t number, uint32_t address)
            {
                auto nameLength = image[address + 5];
                auto height = image[address + 6];
                auto root = Load32(address + 12);
                auto inlineLength = Load16(address + 16);
                auto type = image[address + 4];

                if ((type != fileType && type != directoryType) || nameLength > superblock.maxNameBytes || inlineLength > superblock.inlineBytes || height > maxLevels || (root == 0) != (height == 0))
                {
                    Fail("the record of inode " + std::to_string(number) + " is not valid");
                    return std::nullopt;
                }

                auto inlineArea = image.begin() + address + inodeRecordFixedBytes;
                auto nameArea = inlineArea + superblock.inlineBytes;
                return Inode{ Load32(address), type, Load32(address + 8), root, height, { inlineArea, inlineArea + inlineLength }, { nameArea, nameArea + nameLength } };
            }

            std::optional<Node> OpenNode(uint32_t address, Map map, uint32_t level, uint32_t owner)
            {
                auto block = address / blockSize;
                auto offset = address % blockSize;
                auto at = std::to_string(address);

                if (!nodes.insert(address).second)
                    Fail("node at " + at + " is reached twice");
                else if (block < superblocks || block >= superblock.blocks || offset < slotsOffset || (offset - slotsOffset) % nodeSize != 0 || offset + nodeSize > blockSize)
                    Fail("node address " + at + " is not a node slot");
                else if (!IsOfType(block, BlockType::node))
                    Fail("node at " + at + " is not in a node block");
                else if (!IsSealed(address, nodeSize - crcBytes))
                    Fail("node at " + at + " has a bad CRC");
                else if (image[address] != static_cast<uint8_t>(map) || image[address + 1] != level || Load32(address + 4) != owner)
                    Fail("node at " + at + " belongs to another map");
                else
                {
                    nodeBlocks.insert(block);
                    return Node{ static_cast<uint16_t>(Load16(address + 2)), Load32(address + 8), address + 12 };
                }

                return std::nullopt;
            }

            bool WalkTree(uint32_t root, uint8_t height, const TreeWalk& walk)
            {
                return WalkTreeNode(walk, root, height - 1u, 0, largestFileBlocks);
            }

            bool WalkTreeNode(const TreeWalk& walk, uint32_t address, uint32_t level, uint64_t low, uint64_t high)
            {
                auto node = OpenNode(address, walk.map, level, walk.owner);
                if (!node.has_value())
                    return false;

                auto entryBytes = 4 + (level == 0 ? walk.valueBytes : 4);
                if (node->count == 0 || node->count > (nodeSize - nodeOverhead) / entryBytes)
                    return Fail("node at " + std::to_string(address) + " holds " + std::to_string(node->count) + " entries");

                if (Load32(node->entries) != node->lowestKey)
                    return Fail("the lowest key of node at " + std::to_string(address) + " is not its first key");

                for (uint32_t index = 0; index != node->count; ++index)
                {
                    auto entry = node->entries + index * entryBytes;
                    uint64_t key = Load32(entry);
                    uint64_t next = index + 1 != node->count ? Load32(entry + entryBytes) : high;

                    if (key < low || key >= next)
                        return Fail("the keys of node at " + std::to_string(address) + " are not in order");

                    if (level == 0 ? !walk.onLeafEntry(static_cast<uint32_t>(key), entry + 4) : !WalkTreeNode(walk, Load32(entry + 4), level - 1, key, next))
                        return false;
                }

                return true;
            }

            bool WalkRadixNode(uint32_t address, uint32_t level, uint32_t inode, uint64_t first, std::map<uint32_t, uint32_t>& mapped)
            {
                auto node = OpenNode(address, Map::fileData, level, inode);
                if (!node.has_value())
                    return false;

                auto at = std::to_string(address);
                if (node->lowestKey != first)
                    return Fail("radix node at " + at + " covers index " + std::to_string(node->lowestKey) + " instead of " + std::to_string(first));

                auto fanout = (nodeSize - nodeOverhead) / 4;
                uint64_t span = 1;
                for (uint32_t below = 0; below != level; ++below)
                    span *= fanout;

                uint32_t set = 0;
                for (uint32_t slot = 0; slot != fanout; ++slot)
                {
                    auto value = Load32(node->entries + 4 * slot);
                    if (value == 0)
                        continue;

                    ++set;
                    auto index = first + slot * span;
                    if (index >= largestFileBlocks)
                        return Fail("radix node at " + at + " maps beyond the largest file");

                    if (level == 0 ? !MapBlock(inode, static_cast<uint32_t>(index), value, mapped) : !WalkRadixNode(value, level - 1, inode, index, mapped))
                        return false;
                }

                if (set == 0 || set != node->count)
                    return Fail("radix node at " + at + " counts " + std::to_string(node->count) + " entries, " + std::to_string(set) + " are set");

                return true;
            }

            bool MapBlock(uint32_t inode, uint32_t index, uint32_t block, std::map<uint32_t, uint32_t>& mapped)
            {
                if (block < superblocks || block >= superblock.blocks)
                    return Fail("file " + std::to_string(inode) + " maps block " + std::to_string(block) + ", which is not a block of the volume");

                mapped[index] = block;
                return true;
            }

            bool LoadInode(uint32_t number, uint32_t address)
            {
                if (number == 0)
                    return Fail("the inode table holds inode 0");

                auto inode = ParseInode(number, address);
                if (!inode.has_value())
                    return false;

                inodes[number] = std::move(*inode);
                return true;
            }

            bool LoadMapOf(uint32_t number, Inode& inode)
            {
                if (inode.root == 0)
                    return true;

                if (inode.type == fileType)
                    return WalkRadixNode(inode.root, inode.height - 1u, number, 0, inode.blocks);

                return WalkTree(inode.root, inode.height, TreeWalk{ Map::directory, number, 4, [this, &inode](uint32_t key, uint32_t value)
                                                              {
                                                                  inode.children[key] = Load32(value);
                                                                  return true;
                                                              } });
            }

            bool LoadMaps()
            {
                if (checkpoint.height == 0 || checkpoint.height > maxLevels)
                    return Fail("the inode table has height " + std::to_string(checkpoint.height));

                if (!WalkTree(checkpoint.root, checkpoint.height, TreeWalk{ Map::inodeTable, 0, recordBytes, [this](uint32_t key, uint32_t value)
                                                                      {
                                                                          return LoadInode(key, value);
                                                                      } }))
                    return false;

                for (auto& [number, inode] : inodes)
                    if (!LoadMapOf(number, inode))
                        return false;

                if (nodes.size() != checkpoint.liveNodes)
                    return Fail("the checkpoint counts " + std::to_string(checkpoint.liveNodes) + " live nodes, the maps hold " + std::to_string(nodes.size()));

                return true;
            }

            bool Assign(Roles& roles, uint32_t block, NorBlockUse use)
            {
                if (roles[block].has_value())
                    return Fail("block " + std::to_string(block) + " is used twice");

                roles[block] = use;
                return true;
            }

            bool AssignRoles(Roles& roles)
            {
                roles.assign(superblock.blocks, std::nullopt);
                bool assigned = Assign(roles, 0, NorBlockUse::superblock) && Assign(roles, 1, NorBlockUse::superblock) && Assign(roles, checkpoint.block, NorBlockUse::checkpoint);

                for (auto block : epoch)
                    if (block != checkpoint.block)
                        assigned = assigned && Assign(roles, block, NorBlockUse::journal);

                for (auto block : nodeBlocks)
                    assigned = assigned && Assign(roles, block, NorBlockUse::node);

                for (const auto& [number, inode] : inodes)
                    for (const auto& [index, block] : inode.blocks)
                        assigned = assigned && Assign(roles, block, NorBlockUse::data);

                for (uint32_t block = 0; block != superblock.blocks; ++block)
                    if (!roles[block].has_value() && IsMarkedInUse(block) && IsOfType(block, BlockType::node))
                        roles[block] = NorBlockUse::node;

                return assigned;
            }

            bool CheckBitmap()
            {
                epoch = { checkpoint.recordsBlock };
                Roles roles;
                if (!AssignRoles(roles))
                    return false;

                for (uint32_t block = 0; block != superblock.blocks; ++block)
                {
                    if (roles[block].has_value() && !IsMarkedInUse(block))
                        return Fail("block " + std::to_string(block) + " is used but not marked in use");

                    if (!roles[block].has_value() && IsMarkedInUse(block))
                        return Fail("block " + std::to_string(block) + " is marked in use, but nothing uses it");
                }

                return true;
            }

            std::optional<Record> RecordAt(uint32_t block, uint32_t offset) const
            {
                if (offset + 4 > blockSize)
                    return std::nullopt;

                auto address = AddressOfBlock(block) + offset;
                auto length = Load16(address);
                if (length == erasedLength || length < recordOverhead || offset + length > blockSize || !IsSealed(address, length - crcBytes))
                    return std::nullopt;

                return Record{ address, length, image[address + 2] };
            }

            std::optional<std::vector<Entry>> ParseEntries(const Record& record) const
            {
                std::vector<Entry> entries;
                auto address = record.address + 4;
                auto end = record.address + record.length - crcBytes;

                for (uint32_t index = 0; index != record.entries; ++index)
                {
                    auto fields = address < end ? FieldsOf(image[address]) : std::nullopt;
                    if (!fields.has_value() || address + 1 + 4 * *fields > end)
                        return std::nullopt;

                    Entry entry{ static_cast<Kind>(image[address]) };
                    for (uint32_t field = 0; field != *fields; ++field)
                        entry.fields[field] = Load32(address + 1 + 4 * field);
                    address += 1 + 4 * *fields;

                    if (entry.kind == Kind::size && address + 2 <= end)
                    {
                        entry.byteCount = Load16(address);
                        address += 2;
                    }
                    else if (entry.kind == Kind::size)
                        return std::nullopt;
                    else if (entry.kind == Kind::inodePut)
                        entry.byteCount = recordBytes;

                    entry.bytes = address;
                    address += entry.byteCount;
                    if (address > end)
                        return std::nullopt;

                    entries.push_back(entry);
                }

                if (address != end)
                    return std::nullopt;

                return entries;
            }

            bool LinksTo(uint32_t candidate, uint32_t block, uint32_t end) const
            {
                auto record = RecordAt(candidate, headerSize);
                auto entries = record.has_value() ? ParseEntries(*record) : std::nullopt;

                return entries.has_value() && !entries->empty() && entries->front().kind == Kind::link && entries->front().fields[0] == block && entries->front().fields[1] == end;
            }

            std::optional<uint32_t> NextJournalBlock(uint32_t block, uint32_t end) const
            {
                std::optional<uint32_t> next;
                for (uint32_t candidate = superblocks; candidate != superblock.blocks; ++candidate)
                    if (IsOfType(candidate, BlockType::journal) && SequenceOf(candidate) > SequenceOf(block) && (!next.has_value() || SequenceOf(candidate) < SequenceOf(*next)) && LinksTo(candidate, block, end))
                        next = candidate;

                return next;
            }

            std::optional<uint32_t> ReadRecords(uint32_t block, uint32_t offset, bool linked)
            {
                while (auto record = RecordAt(block, offset))
                {
                    auto entries = ParseEntries(*record);
                    if (!entries.has_value())
                    {
                        Fail("the record at offset " + std::to_string(offset) + " of block " + std::to_string(block) + " is malformed");
                        return std::nullopt;
                    }

                    for (auto entry = entries->begin() + (linked ? 1 : 0); entry != entries->end(); ++entry)
                        if (!Apply(*entry))
                            return std::nullopt;

                    linked = false;
                    ++check.records;
                    offset += RoundUp(record->length, programUnit);
                }

                return offset;
            }

            bool Replay()
            {
                auto block = checkpoint.recordsBlock;
                auto end = ReadRecords(block, checkpoint.recordsOffset, false);

                while (end.has_value())
                {
                    auto next = NextJournalBlock(block, *end);
                    if (!next.has_value())
                        return true;

                    block = *next;
                    epoch.push_back(block);
                    end = ReadRecords(block, headerSize, true);
                }

                return false;
            }

            bool Apply(const Entry& entry)
            {
                const auto& fields = entry.fields;

                switch (entry.kind)
                {
                    case Kind::link:
                        return Fail("a link that does not start a journal block");
                    case Kind::inodePut:
                        return PutInode(fields[0], entry.bytes);
                    case Kind::inodeRemove:
                        return RemoveInode(fields[0]);
                    case Kind::directoryPut:
                        return PutChild(fields[0], fields[1], fields[2]);
                    case Kind::directoryRemove:
                        return RemoveChild(fields[0], fields[1]);
                    case Kind::blockSet:
                        return SetBlock(fields[0], fields[1], fields[2], fields[3]);
                    default:
                        return SetSize(fields[0], fields[1], entry);
                }
            }

            Inode* Find(uint32_t number, uint8_t type)
            {
                auto inode = inodes.find(number);
                return inode != inodes.end() && inode->second.type == type ? &inode->second : nullptr;
            }

            bool PutInode(uint32_t number, uint32_t address)
            {
                auto put = ParseInode(number, address);
                if (!put.has_value())
                    return false;

                auto existing = inodes.find(number);
                if (existing == inodes.end())
                {
                    if (number < nextInode || put->root != 0)
                        return Fail("inode " + std::to_string(number) + " is created below the next inode number or with a map");

                    nextInode = number + 1;
                    inodes[number] = std::move(*put);
                    return true;
                }

                if (existing->second.type != put->type)
                    return Fail("inode " + std::to_string(number) + " changes its type");

                existing->second.parent = put->parent;
                existing->second.size = put->size;
                existing->second.inlineBytes = std::move(put->inlineBytes);
                existing->second.name = std::move(put->name);
                return true;
            }

            bool RemoveInode(uint32_t number)
            {
                if (number == rootInode || inodes.erase(number) == 0)
                    return Fail("inode " + std::to_string(number) + " is removed, but cannot be");

                return true;
            }

            bool PutChild(uint32_t directory, uint32_t key, uint32_t child)
            {
                auto* inode = Find(directory, directoryType);
                if (inode == nullptr || !inode->children.emplace(key, child).second)
                    return Fail("an entry is put into directory " + std::to_string(directory) + ", which does not exist or holds its key");

                return true;
            }

            bool RemoveChild(uint32_t directory, uint32_t key)
            {
                auto* inode = Find(directory, directoryType);
                if (inode == nullptr || inode->children.erase(key) == 0)
                    return Fail("an entry is removed from directory " + std::to_string(directory) + ", which does not exist or does not hold its key");

                return true;
            }

            bool SetBlock(uint32_t number, uint32_t index, uint32_t previous, uint32_t block)
            {
                auto* inode = Find(number, fileType);
                if (inode == nullptr)
                    return Fail("a block set names inode " + std::to_string(number) + ", which is not a file");

                auto mapped = inode->blocks.find(index);
                auto current = mapped != inode->blocks.end() ? mapped->second : 0;
                if (current != previous)
                    return Fail("a block set of inode " + std::to_string(number) + " at index " + std::to_string(index) + " expects block " + std::to_string(previous) + ", the map holds block " + std::to_string(current));

                if (block == 0)
                {
                    inode->blocks.erase(index);
                    return true;
                }

                return MapBlock(number, index, block, inode->blocks);
            }

            bool SetSize(uint32_t number, uint32_t size, const Entry& entry)
            {
                auto* inode = Find(number, fileType);
                if (inode == nullptr || entry.byteCount > superblock.inlineBytes)
                    return Fail("a size names inode " + std::to_string(number) + ", which is not a file, or too many inline bytes");

                auto bytes = image.begin() + entry.bytes;
                inode->size = size;
                inode->inlineBytes.assign(bytes, bytes + entry.byteCount);
                std::erase_if(inode->blocks, [this, size](const auto& mapped)
                    {
                        return mapped.first >= BlocksOfSize(size);
                    });
                return true;
            }

            uint64_t BlocksOfSize(uint32_t size) const
            {
                return (uint64_t{ size } + check.layout.payloadSize - 1) / check.layout.payloadSize;
            }

            bool CheckTree()
            {
                const auto* root = Find(rootInode, directoryType);
                if (root == nullptr || root->parent != 0 || !root->name.empty())
                    return Fail("the root directory is missing or not valid");

                std::map<uint32_t, uint32_t> parents;
                for (const auto& [number, inode] : inodes)
                {
                    if (number >= nextInode)
                        return Fail("inode " + std::to_string(number) + " is not below the next inode number " + std::to_string(nextInode));

                    if (inode.type == directoryType && !CheckChildren(number, inode, parents))
                        return false;
                }

                for (const auto& [number, inode] : inodes)
                    if (number != rootInode && !parents.contains(number))
                        return Fail("inode " + std::to_string(number) + " is in no directory");

                return CheckReachable();
            }

            bool CheckChildren(uint32_t directory, const Inode& inode, std::map<uint32_t, uint32_t>& parents)
            {
                std::set<std::string> names;
                auto in = " directory " + std::to_string(directory);

                for (const auto& [key, number] : inode.children)
                {
                    auto child = inodes.find(number);
                    if (child == inodes.end())
                        return Fail(in.substr(1) + " names inode " + std::to_string(number) + ", which does not exist");

                    const auto& name = child->second.name;
                    if (number == rootInode || !parents.emplace(number, directory).second)
                        return Fail("inode " + std::to_string(number) + " is in more than one directory entry");

                    if (child->second.parent != directory)
                        return Fail("inode " + std::to_string(number) + " is in" + in + ", but names directory " + std::to_string(child->second.parent) + " as its parent");

                    if (name.empty() || std::ranges::find(name, '/') != name.end() || key >> 8 != (Crc(name) & 0xffffff))
                        return Fail("the key of " + Text(name) + " in" + in + " does not match its name");

                    if (!names.insert(Text(name)).second)
                        return Fail(in.substr(1) + " holds the name " + Text(name) + " twice");
                }

                return true;
            }

            static uint32_t Crc(const std::vector<uint8_t>& bytes)
            {
                infra::Crc32 crc;
                crc.Update(infra::MakeRange(bytes));
                return crc.Result();
            }

            bool CheckReachable()
            {
                std::vector<uint32_t> pending{ rootInode };
                std::size_t reached = 0;

                while (!pending.empty())
                {
                    auto directory = pending.back();
                    pending.pop_back();
                    ++reached;

                    for (const auto& [key, child] : inodes[directory].children)
                        pending.push_back(child);
                }

                if (reached != inodes.size())
                    return Fail("the root directory does not reach every inode");

                return true;
            }

            bool CheckFiles()
            {
                for (const auto& [number, inode] : inodes)
                {
                    if (inode.type == directoryType && (inode.size != 0 || !inode.inlineBytes.empty()))
                        return Fail("directory " + std::to_string(number) + " has a size or inline bytes");

                    if (inode.type == fileType && !CheckFile(number, inode))
                        return false;
                }

                return true;
            }

            bool CheckFile(uint32_t number, const Inode& inode)
            {
                auto file = "file " + std::to_string(number);
                auto count = BlocksOfSize(inode.size);

                for (const auto& [index, block] : inode.blocks)
                    if (index >= count)
                        return Fail(file + " maps index " + std::to_string(index) + " beyond its size");

                for (uint32_t index = 0; index + 1 < count; ++index)
                    if (!inode.blocks.contains(index))
                        return Fail(file + " has no block for index " + std::to_string(index));

                auto last = count == 0 ? 0 : static_cast<uint32_t>(inode.size - (count - 1) * check.layout.payloadSize);
                auto needed = count != 0 && inode.blocks.contains(static_cast<uint32_t>(count - 1)) ? last % programUnit : last;
                if (inode.inlineBytes.size() != needed)
                    return Fail(file + " holds " + std::to_string(inode.inlineBytes.size()) + " inline bytes, its size needs " + std::to_string(needed));

                return true;
            }

            bool CheckUsedBlock(uint32_t block, NorBlockUse use)
            {
                if (block >= superblocks && !headers[block].eraseCount.has_value())
                    return Fail("block " + std::to_string(block) + " is in use without a valid part A");

                if (use == NorBlockUse::node && SequenceOf(block) > SequenceOf(checkpoint.block))
                    return Fail("node block " + std::to_string(block) + " is newer than the checkpoint");

                return true;
            }

            bool CheckDataOwners()
            {
                for (const auto& [number, inode] : inodes)
                    for (const auto& [index, block] : inode.blocks)
                    {
                        const auto& allocation = headers[block].allocation;
                        if (!IsOfType(block, BlockType::data) || allocation->inode != number || allocation->index != index)
                            return Fail("data block " + std::to_string(block) + " does not name inode " + std::to_string(number) + " and index " + std::to_string(index) + " as its owner");
                    }

                return true;
            }

            bool ClassifyBlocks()
            {
                Roles roles;
                if (!AssignRoles(roles) || !CheckDataOwners())
                    return false;

                check.blocks.resize(superblock.blocks);
                for (uint32_t block = 0; block != superblock.blocks; ++block)
                {
                    if (roles[block].has_value() && !CheckUsedBlock(block, *roles[block]))
                        return false;

                    if (roles[block].has_value())
                        check.blocks[block] = *roles[block];
                    else if (!headers[block].eraseCount.has_value() || !headers[block].partBErased)
                        check.blocks[block] = NorBlockUse::free;
                    else if (IsErased(AddressOfBlock(block) + partBOffset, blockSize - partBOffset))
                        check.blocks[block] = NorBlockUse::freeErased;
                    else
                        return Fail("block " + std::to_string(block) + " looks free and erased, but holds programmed bytes");
                }

                return true;
            }

            void ListEntries()
            {
                std::vector<std::pair<uint32_t, std::string>> pending{ { rootInode, "" } };

                while (!pending.empty())
                {
                    auto [directory, path] = pending.back();
                    pending.pop_back();

                    for (const auto& [key, number] : inodes[directory].children)
                    {
                        const auto& inode = inodes[number];
                        auto childPath = path + "/" + Text(inode.name);
                        check.entries.push_back({ childPath, number, inode.type == directoryType, inode.size, inode.blocks, inode.inlineBytes });

                        if (inode.type == directoryType)
                            pending.emplace_back(number, childPath);
                    }
                }
            }

            const std::vector<uint8_t>& image;
            uint32_t sectorSize;
            uint32_t pageSize;
            uint32_t sectors;
            NorCheck check;
            NorImageSuperblock superblock;
            uint32_t blockSize{ 0 };
            uint32_t programUnit{ 0 };
            uint32_t partBOffset{ 0 };
            uint32_t headerSize{ 0 };
            uint32_t slotsOffset{ 0 };
            uint32_t nodeSize{ 0 };
            uint32_t recordBytes{ 0 };
            uint32_t checkpointBytes{ 0 };
            std::vector<Header> headers;
            Checkpoint checkpoint;
            uint32_t checkpointAddress{ 0 };
            uint32_t nextInode{ 0 };
            std::map<uint32_t, Inode> inodes;
            std::set<uint32_t> nodes;
            std::set<uint32_t> nodeBlocks;
            std::vector<uint32_t> epoch;
        };
    }

    const NorImageEntry* NorCheck::Find(const std::string& path) const
    {
        auto entry = std::ranges::find(entries, path, &NorImageEntry::path);
        return entry != entries.end() ? &*entry : nullptr;
    }

    NorCheck CheckNorImage(const std::vector<uint8_t>& image, uint32_t sectorSize, uint32_t pageSize)
    {
        return Checker{ image, sectorSize, pageSize }.Run();
    }

    std::vector<uint8_t> ReadContents(const std::vector<uint8_t>& image, const NorCheck& check, const NorImageEntry& entry)
    {
        const auto& layout = check.layout;
        std::vector<uint8_t> contents;

        for (const auto& [index, block] : entry.blocks)
        {
            auto inBlock = std::min<std::size_t>(layout.payloadSize, entry.size - contents.size() - entry.inlineBytes.size());
            auto start = image.begin() + static_cast<std::ptrdiff_t>(block * layout.blockSize + layout.headerSize);
            contents.insert(contents.end(), start, start + static_cast<std::ptrdiff_t>(inBlock));
        }

        contents.insert(contents.end(), entry.inlineBytes.begin(), entry.inlineBytes.end());
        return contents;
    }
}
