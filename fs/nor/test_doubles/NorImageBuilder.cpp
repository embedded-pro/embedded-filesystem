#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "fs/common/CrcRecord.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorNodeWriteBack.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <optional>
#include <utility>

namespace fs
{
    namespace
    {
        constexpr std::size_t directoryValueBytes = 4;
        constexpr std::size_t mapHeightOffset = 6;

        std::size_t CachedNodes(std::size_t slots, std::size_t bytes, uint32_t nodeSize)
        {
            return std::min(slots, bytes / nodeSize);
        }
    }

    NorImageBuilder::NorImageBuilder(NorImage& image, const NorConfig& config)
        : image(image)
        , config(config)
        , geometry(*MakeNorGeometry(config, image.SectorSize(), image.Sectors()))
        , cache(flash, infra::Head(infra::MakeRange(slots), CachedNodes(slots.size(), nodes.size(), config.nodeSize)), infra::Head(infra::MakeRange(nodes), CachedNodes(slots.size(), nodes.size(), config.nodeSize) * config.nodeSize))
    {
        FindCheckpoint();

        auto checkpoint = Checkpoint();
        inodeTable = NorMapRoot{ checkpoint.inodeTableRoot, checkpoint.inodeTableHeight };
        nextInode = checkpoint.nextInode;
        liveNodes = checkpoint.liveNodes;
    }

    const NorGeometry& NorImageBuilder::Geometry() const
    {
        return geometry;
    }

    uint32_t NorImageBuilder::CheckpointBlock() const
    {
        return checkpointBlock;
    }

    uint32_t NorImageBuilder::JournalBlock() const
    {
        return journalBlock;
    }

    uint32_t NorImageBuilder::JournalEnd() const
    {
        return journalEnd;
    }

    uint64_t NorImageBuilder::NextSequence() const
    {
        return nextSequence;
    }

    NorCheckpoint NorImageBuilder::Checkpoint() const
    {
        auto bytes = infra::Head(infra::MakeRange(buffer), geometry.CheckpointBytes());
        Read(geometry.AddressOfBlock(checkpointBlock) + geometry.HeaderSize(), bytes);
        return *OpenNorCheckpoint(bytes);
    }

    void NorImageBuilder::SetEraseCount(uint32_t block, uint32_t count)
    {
        EraseBlock(block);
        auto partA = Scratch(geometry.RoundToProgramUnit(norHeaderPartABytes));
        SerializeNorHeaderA(count, partA);
        Program(geometry.AddressOfBlock(block), partA);
    }

    void NorImageBuilder::EraseBlock(uint32_t block)
    {
        for (auto sector = geometry.FirstSectorOfBlock(block); sector != geometry.FirstSectorOfBlock(block + 1); ++sector)
            image.EraseSector(sector);
    }

    uint64_t NorImageBuilder::Allocate(uint32_t block, NorBlockType type, NorBlockOwner owner)
    {
        auto partB = Scratch(geometry.RoundToProgramUnit(norHeaderPartBBytes));
        SerializeNorHeaderB(NorAllocation{ type, nextSequence, owner }, partB);
        Program(geometry.AddressOfBlock(block) + geometry.HeaderPartBOffset(), partB);
        return nextSequence++;
    }

    void NorImageBuilder::AddEmptyNodeBlock(uint32_t block)
    {
        Allocate(block, NorBlockType::node);
        blocksToCommit.push_back(block);
    }

    void NorImageBuilder::WriteCheckpoint(uint32_t block)
    {
        MoveCheckpoint(block, Checkpoint(), {});
    }

    void NorImageBuilder::WriteCheckpoint(uint32_t block, const NorCheckpoint& checkpoint, std::initializer_list<uint32_t> used)
    {
        auto bytes = Scratch(geometry.RoundToProgramUnit(NorCheckpointBytes(checkpoint.blocks)));
        SerializeNorCheckpoint(checkpoint, bytes);
        for (auto inUse : used)
            MarkNorBlockInUse(bytes, inUse);
        ProgramCheckpoint(block, bytes);
    }

    void NorImageBuilder::AppendRecord(std::initializer_list<NorEntry> entries)
    {
        CrcRecord record{ Scratch(static_cast<uint32_t>(buffer.size())) };
        if (std::exchange(linkPending, false))
            AddNorEntry(record, link);
        for (const auto& entry : entries)
            AddNorEntry(record, entry);

        journalEnd = ProgramRecord(journalBlock, journalEnd, record);
    }

    uint32_t NorImageBuilder::AppendRecord(uint32_t block, uint32_t offset, std::initializer_list<NorEntry> entries)
    {
        CrcRecord record{ Scratch(static_cast<uint32_t>(buffer.size())) };
        for (const auto& entry : entries)
            AddNorEntry(record, entry);

        return ProgramRecord(block, offset, record);
    }

    void NorImageBuilder::StartJournalBlock(uint32_t block)
    {
        Allocate(block, NorBlockType::journal);
        link = NorLink{ journalBlock, journalEnd };
        linkPending = true;
        journalBlock = block;
        journalEnd = geometry.HeaderSize();
        epochJournalBlocks.push_back(block);
    }

    void NorImageBuilder::Program(uint32_t address, infra::ConstByteRange bytes)
    {
        image.Program(address, bytes);
    }

    void NorImageBuilder::Read(uint32_t address, infra::ByteRange bytes) const
    {
        image.Read(address, bytes);
    }

    void NorImageBuilder::Corrupt(uint32_t address)
    {
        std::array<uint8_t, 1> byte{};
        Read(address, infra::MakeRange(byte));
        really_assert(byte[0] != 0);
        Program(address, infra::MakeRange(std::array<uint8_t, 1>{ 0 }));
    }

    uint32_t NorImageBuilder::AddDirectory(uint32_t parent, infra::BoundedConstString name)
    {
        return AddInode(parent, name, NorInodeRecord{ parent, NorInodeType::directory, 0, 0, 0, {}, infra::StringAsByteRange(name) });
    }

    uint32_t NorImageBuilder::AddFile(uint32_t parent, infra::BoundedConstString name, infra::ConstByteRange contents)
    {
        NorMapRoot map;
        auto tail = WriteData(nextInode, contents, map);
        return AddInode(parent, name, NorInodeRecord{ parent, NorInodeType::file, map.height, static_cast<uint32_t>(contents.size()), map.address, tail, infra::StringAsByteRange(name) });
    }

    void NorImageBuilder::AddRecord(uint32_t inode, const NorInodeRecord& record)
    {
        Fetch(InodeTableId(), inodeTable, inode);
        NorBTree inodes{ cache, InodeTableId() };
        SerializeNorInodeRecord(record, inodes.Find(inodeTable, inode).has_value() ? inodes.Modify(inodeTable, inode) : inodes.Insert(inodeTable, inode), config);
        nextInode = std::max(nextInode, inode + 1);
        cache.UnpinAll();
    }

    void NorImageBuilder::AddEntry(uint32_t directory, uint32_t key, uint32_t inode)
    {
        auto map = MapOf(directory);
        Fetch(NorMapId{ NorMap::directory, directory, directoryValueBytes }, map, key);
        StoreLittle32(NorBTree(cache, NorMapId{ NorMap::directory, directory, directoryValueBytes }).Insert(map, key), 0, inode);
        SetMapOf(directory, map);
        cache.UnpinAll();
    }

    void NorImageBuilder::MapBlock(uint32_t file, uint32_t index, uint32_t block)
    {
        auto map = MapOf(file);
        Fetch(NorMapId{ NorMap::fileData, file, 4 }, map, index);
        NorRadixTree(cache, file).Set(map, index, block);
        SetMapOf(file, map);
        cache.UnpinAll();
    }

    void NorImageBuilder::Commit()
    {
        NorNodeWriteBack writeBack{ cache, NorInodeRecordBytes(config) };
        infra::BoundedVector<uint32_t>::WithMaxSize<16> nodeBlocks;
        uint32_t offset = 0;
        uint32_t written = 0;

        for (auto next = writeBack.Next(); next.has_value(); next = writeBack.Next())
        {
            auto address = NodeAddress(nodeBlocks, offset);
            Program(address, writeBack.Seal(*next));
            writeBack.Written(*next, address, inodeTable);
            ++written;
        }

        liveNodes = liveNodes + written - cache.ReleasedNodes();
        cache.Clear();

        auto checkpoint = Checkpoint();
        checkpoint.inodeTableRoot = inodeTable.address;
        checkpoint.inodeTableHeight = inodeTable.height;
        checkpoint.nextInode = nextInode;
        checkpoint.liveNodes = liveNodes;
        blocksToCommit.insert(blocksToCommit.end(), nodeBlocks.begin(), nodeBlocks.end());
        MoveCheckpoint(FreeBlock(), checkpoint, infra::MakeRange(blocksToCommit));
        blocksToCommit.clear();
    }

    const NorMapRoot& NorImageBuilder::InodeTable() const
    {
        return inodeTable;
    }

    NorMapRoot NorImageBuilder::MapOf(uint32_t inode)
    {
        auto record = RecordOf(inode);
        really_assert(record.has_value());

        return NorMapRoot{ record->mapRoot, record->mapHeight };
    }

    std::optional<NorInodeRecord> NorImageBuilder::RecordOf(uint32_t inode)
    {
        Fetch(InodeTableId(), inodeTable, inode);
        auto bytes = NorBTree(cache, InodeTableId()).Find(inodeTable, inode);
        if (!bytes.has_value())
            return std::nullopt;

        return ParseNorInodeRecord(*bytes, config);
    }

    std::optional<uint32_t> NorImageBuilder::EntryOf(uint32_t directory, uint32_t key)
    {
        auto map = MapOf(directory);
        Fetch(NorMapId{ NorMap::directory, directory, directoryValueBytes }, map, key);
        auto value = NorBTree(cache, NorMapId{ NorMap::directory, directory, directoryValueBytes }).Find(map, key);
        if (!value.has_value())
            return std::nullopt;

        return LoadLittle32(*value, 0);
    }

    uint32_t NorImageBuilder::AddInode(uint32_t parent, infra::BoundedConstString name, const NorInodeRecord& record)
    {
        auto inode = nextInode;
        AddRecord(inode, record);
        AddEntry(parent, FreeKey(parent, name), inode);
        return inode;
    }

    infra::ConstByteRange NorImageBuilder::WriteData(uint32_t file, infra::ConstByteRange contents, NorMapRoot& map)
    {
        if (contents.empty())
            return contents;

        auto payload = geometry.PayloadSize();
        auto lastIndex = static_cast<uint32_t>((contents.size() - 1) / payload);
        for (uint32_t index = 0; index != lastIndex; ++index)
            WriteDataBlock(file, index, infra::Head(infra::DiscardHead(contents, index * payload), payload), map);

        auto last = infra::DiscardHead(contents, lastIndex * payload);
        if (last.size() <= config.inlineBytes)
            return last;

        auto programmed = last.size() / geometry.programUnit * geometry.programUnit;
        WriteDataBlock(file, lastIndex, infra::Head(last, programmed), map);
        return infra::DiscardHead(last, programmed);
    }

    void NorImageBuilder::WriteDataBlock(uint32_t file, uint32_t index, infra::ConstByteRange bytes, NorMapRoot& map)
    {
        auto block = FreeBlock();
        Allocate(block, NorBlockType::data, NorBlockOwner{ file, index });
        Program(geometry.AddressOfBlock(block) + geometry.HeaderSize(), bytes);
        blocksToCommit.push_back(block);
        NorRadixTree(cache, file).Set(map, index, block);
    }

    uint32_t NorImageBuilder::FreeKey(uint32_t directory, infra::BoundedConstString name)
    {
        auto key = NorDirectoryKey(name);
        while (EntryOf(directory, key).has_value())
            ++key;

        return key;
    }

    void NorImageBuilder::SetMapOf(uint32_t inode, const NorMapRoot& map)
    {
        Fetch(InodeTableId(), inodeTable, inode);
        auto record = NorBTree(cache, InodeTableId()).Modify(inodeTable, inode);
        StoreLittle32(record, norInodeMapRootOffset, map.address);
        record[mapHeightOffset] = map.height;
    }

    void NorImageBuilder::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key)
    {
        std::optional<NorFetchResult> result;
        fetch.Fetch(map, root, key, [&result](NorFetchResult fetched)
            {
                result = fetched;
            });

        really_assert(result == NorFetchResult::fetched);
    }

    NorMapId NorImageBuilder::InodeTableId() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(config) };
    }

    uint32_t NorImageBuilder::FreeBlock()
    {
        auto checkpoint = Scratch(geometry.CheckpointBytes());
        Read(geometry.AddressOfBlock(checkpointBlock) + geometry.HeaderSize(), checkpoint);
        std::array<uint8_t, 128> header{};

        for (uint32_t block = 2; block != geometry.blocks; ++block)
        {
            if (block == checkpointBlock || IsNorBlockInUse(checkpoint, block) || std::ranges::find(epochJournalBlocks, block) != epochJournalBlocks.end())
                continue;

            Read(geometry.AddressOfBlock(block), infra::Head(infra::MakeRange(header), geometry.HeaderSize()));
            if (ParseNorBlockHeader(infra::Head(infra::MakeRange(header), geometry.HeaderSize()), geometry.programUnit).IsFreeAndErased())
                return block;
        }

        really_assert(false);
        return 0;
    }

    uint32_t NorImageBuilder::NodeAddress(infra::BoundedVector<uint32_t>& nodeBlocks, uint32_t& offset)
    {
        if (nodeBlocks.empty() || offset + geometry.nodeSize > geometry.BlockSize())
        {
            auto block = FreeBlock();
            Allocate(block, NorBlockType::node);
            nodeBlocks.push_back(block);
            offset = geometry.NodeSlotsOffset();
        }

        auto address = geometry.AddressOfBlock(nodeBlocks.back()) + offset;
        offset += geometry.nodeSize;
        return address;
    }

    void NorImageBuilder::MoveCheckpoint(uint32_t block, NorCheckpoint checkpoint, infra::MemoryRange<const uint32_t> blocksInUse)
    {
        auto bytes = Scratch(geometry.RoundToProgramUnit(geometry.CheckpointBytes()));
        Read(geometry.AddressOfBlock(checkpointBlock) + geometry.HeaderSize(), infra::Head(bytes, geometry.CheckpointBytes()));
        checkpoint.recordsBlock = block;
        checkpoint.recordsOffset = geometry.HeaderSize() + static_cast<uint32_t>(bytes.size());

        auto rewritten = infra::Head(infra::DiscardHead(infra::MakeRange(buffer), bytes.size()), bytes.size());
        std::ranges::fill(rewritten, uint8_t{ 0xff });
        SerializeNorCheckpoint(checkpoint, rewritten);
        std::ranges::copy(infra::Head(infra::DiscardHead(bytes, norCheckpointFixedBytes), NorCheckpointBitmapBytes(geometry.blocks)), rewritten.begin() + norCheckpointFixedBytes);
        SetInUse(rewritten, checkpointBlock, false);
        for (auto journal : epochJournalBlocks)
            SetInUse(rewritten, journal, false);
        for (auto inUse : blocksInUse)
            SetInUse(rewritten, inUse, true);
        SetInUse(rewritten, block, true);

        Allocate(block, NorBlockType::checkpoint);
        ProgramCheckpoint(block, rewritten);
        checkpointBlock = block;
        journalBlock = block;
        journalEnd = checkpoint.recordsOffset;
        epochJournalBlocks.clear();
        linkPending = false;
    }

    void NorImageBuilder::FindCheckpoint()
    {
        std::optional<uint64_t> checkpointSequence;
        auto header = infra::Head(infra::MakeRange(buffer), geometry.HeaderSize());

        for (uint32_t block = 2; block != geometry.blocks; ++block)
        {
            Read(geometry.AddressOfBlock(block), header);
            auto allocation = ParseNorBlockHeader(header, geometry.programUnit).allocation;
            if (!allocation.has_value())
                continue;

            nextSequence = std::max(nextSequence, allocation->sequence + 1);
            if (allocation->type == NorBlockType::checkpoint && (!checkpointSequence.has_value() || allocation->sequence > *checkpointSequence))
            {
                checkpointSequence = allocation->sequence;
                checkpointBlock = block;
            }
        }

        journalBlock = checkpointBlock;
        journalEnd = Checkpoint().recordsOffset;
    }

    infra::ByteRange NorImageBuilder::Scratch(uint32_t size)
    {
        really_assert(size <= buffer.size());

        auto bytes = infra::Head(infra::MakeRange(buffer), size);
        std::ranges::fill(bytes, uint8_t{ 0xff });
        return bytes;
    }

    uint32_t NorImageBuilder::ProgramRecord(uint32_t block, uint32_t offset, const CrcRecord& record)
    {
        auto rounded = infra::Head(infra::MakeRange(buffer), geometry.RoundToProgramUnit(static_cast<uint32_t>(record.Seal().size())));
        really_assert(offset + rounded.size() <= geometry.BlockSize());
        Program(geometry.AddressOfBlock(block) + offset, rounded);
        return offset + static_cast<uint32_t>(rounded.size());
    }

    void NorImageBuilder::ProgramCheckpoint(uint32_t block, infra::ByteRange bytes)
    {
        SealNorCheckpoint(bytes);
        Program(geometry.AddressOfBlock(block) + geometry.HeaderSize(), bytes);
    }

    void NorImageBuilder::SetInUse(infra::ByteRange checkpoint, uint32_t block, bool inUse) const
    {
        auto& byte = checkpoint[norCheckpointFixedBytes + block / 8];
        auto bit = static_cast<uint8_t>(1u << (block % 8));
        byte = inUse ? static_cast<uint8_t>(byte | bit) : static_cast<uint8_t>(byte & ~bit);
    }
}
