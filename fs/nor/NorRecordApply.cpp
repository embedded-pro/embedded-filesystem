#include "fs/nor/NorRecordApply.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorVolume.hpp"
#include <algorithm>
#include <limits>
#include <utility>
#include <variant>

namespace fs
{
    namespace
    {
        constexpr uint32_t entryValueBytes = 4;
        constexpr uint32_t superblocks = 2;
        constexpr uint32_t rootInode = 1;

        NorMapRoot MapOf(infra::ConstByteRange record)
        {
            return NorMapRoot{ LoadLittle32(record, norInodeMapRootOffset), record[norInodeMapHeightOffset] };
        }

        void SetMapOf(infra::ByteRange record, const NorMapRoot& map)
        {
            StoreLittle32(record, norInodeMapRootOffset, map.address);
            record[norInodeMapHeightOffset] = map.height;
        }
    }

    NorRecordApply::NorRecordApply(NorVolume& volume)
        : volume(volume)
    {}

    NorRecordApply::~NorRecordApply() = default;

    void NorRecordApply::Apply(const OpenedRecord& record, bool linkedRecord, const infra::Function<void(Result result)>& onApplied)
    {
        reader.emplace(record, NorInodeRecordBytes(volume.Config()));
        linked = linkedRecord;
        first = true;
        onDone = onApplied;
        ApplyNext();
    }

    void NorRecordApply::ApplyNext()
    {
        auto entry = reader->Next();
        auto isFirst = std::exchange(first, false);

        if (!entry.has_value())
            onDone(reader->Malformed() ? Result::corrupted : Result::success);
        else if (const auto* put = std::get_if<NorInodePut>(&*entry))
            PutInode(*put);
        else if (const auto* inodeRemove = std::get_if<NorInodeRemove>(&*entry))
            RemoveInode(*inodeRemove);
        else if (const auto* directoryPut = std::get_if<NorDirectoryEntryPut>(&*entry))
            PutDirectoryEntry(*directoryPut);
        else if (const auto* directoryRemove = std::get_if<NorDirectoryEntryRemove>(&*entry))
            RemoveDirectoryEntry(*directoryRemove);
        else if (const auto* set = std::get_if<NorBlockSet>(&*entry))
            SetBlock(*set);
        else if (const auto* size = std::get_if<NorSize>(&*entry))
            SetSize(*size);
        else if (linked && isFirst)
            ApplyNext();
        else
            onDone(Result::corrupted);
    }

    void NorRecordApply::PutInode(const NorInodePut& put)
    {
        if (put.inode == 0 || !ParseNorInodeRecord(put.record, volume.Config()).has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        inodePut = put;
        Fetch(InodeTable(), volume.State().inodeTable, put.inode, Step::inodeFetched);
    }

    void NorRecordApply::InodeFetched()
    {
        auto& state = volume.State();
        NorBTree inodes{ volume.Cache(), InodeTable() };
        auto existing = inodes.Find(state.inodeTable, inodePut.inode);
        auto changesType = existing.has_value() && (*existing)[norInodeTypeOffset] != inodePut.record[norInodeTypeOffset];

        if (changesType || (!existing.has_value() && !inodes.NodesModifiedByInsert(state.inodeTable, inodePut.inode).has_value()))
        {
            onDone(Result::corrupted);
            return;
        }

        if (!existing.has_value() && !HasRoomFor(inodes.NodesModifiedByInsert(state.inodeTable, inodePut.inode).value_or(0) - inodes.NodesModifiedByChange(state.inodeTable, inodePut.inode)))
        {
            onDone(Result::unsupported);
            return;
        }

        auto map = existing.has_value() ? MapOf(*existing) : NorMapRoot{};
        auto record = existing.has_value() ? inodes.Modify(state.inodeTable, inodePut.inode) : inodes.Insert(state.inodeTable, inodePut.inode);
        std::ranges::copy(inodePut.record, record.begin());
        SetMapOf(record, map);
        state.nextInode = static_cast<uint32_t>(std::max<uint64_t>(state.nextInode, std::min<uint64_t>(uint64_t{ inodePut.inode } + 1, std::numeric_limits<uint32_t>::max())));
        ApplyNext();
    }

    void NorRecordApply::RemoveInode(const NorInodeRemove& remove)
    {
        removedInode = remove.inode;
        Fetch(InodeTable(), volume.State().inodeTable, remove.inode, Step::removedInodeFetched);
    }

    void NorRecordApply::RemovedInodeFetched()
    {
        auto record = NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, removedInode);
        if (!record.has_value() || removedInode == rootInode)
            onDone(Result::corrupted);
        else if ((*record)[norInodeTypeOffset] == static_cast<uint8_t>(NorInodeType::directory))
        {
            if (MapOf(*record).address != 0)
                onDone(Result::corrupted);
            else
                RemoveRecord();
        }
        else
        {
            dataInode = removedInode;
            dataMap = MapOf(*record);
            ReleaseBlocks(0, BlocksOfSize(LoadLittle32(*record, norInodeSizeOffset)), Step::removeRecord);
        }
    }

    void NorRecordApply::RemoveRecord()
    {
        Fetch(InodeTable(), volume.State().inodeTable, removedInode, Step::recordFetched);
    }

    void NorRecordApply::RecordFetched()
    {
        NorBTree(volume.Cache(), InodeTable()).Remove(volume.State().inodeTable, removedInode);
        ApplyNext();
    }

    void NorRecordApply::PutDirectoryEntry(const NorDirectoryEntryPut& put)
    {
        entryPut = put;
        FetchDirectory(put.directory, put.key, Step::entryFetched);
    }

    void NorRecordApply::EntryFetched()
    {
        auto& state = volume.State();
        NorBTree entries{ volume.Cache(), DirectoryMap(entryPut.directory) };
        if (entries.Find(directoryMap, entryPut.key).has_value() || !entries.NodesModifiedByInsert(directoryMap, entryPut.key).has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        if (!HasRoomFor(entries.NodesModifiedByInsert(directoryMap, entryPut.key).value_or(0) - entries.NodesModifiedByChange(directoryMap, entryPut.key)))
        {
            onDone(Result::unsupported);
            return;
        }

        StoreLittle32(entries.Insert(directoryMap, entryPut.key), 0, entryPut.inode);
        SetMapOf(NorBTree(volume.Cache(), InodeTable()).Modify(state.inodeTable, entryPut.directory), directoryMap);
        ApplyNext();
    }

    void NorRecordApply::RemoveDirectoryEntry(const NorDirectoryEntryRemove& remove)
    {
        entryRemove = remove;
        FetchDirectory(remove.directory, remove.key, Step::removedEntryFetched);
    }

    void NorRecordApply::RemovedEntryFetched()
    {
        auto& state = volume.State();
        NorBTree entries{ volume.Cache(), DirectoryMap(entryRemove.directory) };
        if (!entries.Find(directoryMap, entryRemove.key).has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        entries.Remove(directoryMap, entryRemove.key);
        SetMapOf(NorBTree(volume.Cache(), InodeTable()).Modify(state.inodeTable, entryRemove.directory), directoryMap);
        ApplyNext();
    }

    void NorRecordApply::FetchDirectory(uint32_t directory, uint32_t key, Step next)
    {
        afterDirectory = next;
        directoryInode = directory;
        directoryKey = key;
        Fetch(InodeTable(), volume.State().inodeTable, directory, Step::directoryFetched);
    }

    void NorRecordApply::DirectoryFetched()
    {
        auto record = RecordOfType(directoryInode, NorInodeType::directory);
        if (!record.has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        directoryMap = MapOf(*record);
        Fetch(DirectoryMap(directoryInode), directoryMap, directoryKey, afterDirectory);
    }

    void NorRecordApply::SetBlock(const NorBlockSet& set)
    {
        blockSet = set;
        Fetch(InodeTable(), volume.State().inodeTable, set.inode, Step::fileFetched);
    }

    void NorRecordApply::FileFetched()
    {
        auto record = RecordOfType(blockSet.inode, NorInodeType::file);
        if (!record.has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        dataMap = MapOf(*record);
        Fetch(DataMap(blockSet.inode), dataMap, blockSet.index, Step::slotFetched);
    }

    void NorRecordApply::SlotFetched()
    {
        auto& state = volume.State();
        NorRadixTree data{ volume.Cache(), blockSet.inode };
        auto sets = blockSet.block != 0;
        auto ofEpoch = IsOfEpoch(blockSet.block);
        if (data.Find(dataMap, blockSet.index) != blockSet.previous || (blockSet.previous != 0 && !IsInVolume(blockSet.previous)) || (sets && ((!IsFreeBlock(blockSet.block) && !ofEpoch) || !data.NodesModifiedBySet(dataMap, blockSet.index).has_value())))
        {
            onDone(Result::corrupted);
            return;
        }

        if (sets && !HasRoomFor(data.NodesCreatedBySet(dataMap, blockSet.index).value_or(0)))
        {
            onDone(Result::unsupported);
            return;
        }

        if (sets)
        {
            data.Set(dataMap, blockSet.index, blockSet.block);
            if (!ofEpoch)
                state.blocks.Set(blockSet.block, NorBlockState::inUse);
        }
        else
            data.Clear(dataMap, blockSet.index);

        SetMapOf(NorBTree(volume.Cache(), InodeTable()).Modify(state.inodeTable, blockSet.inode), dataMap);
        if (blockSet.previous != 0)
            Release(blockSet.previous);

        ApplyNext();
    }

    void NorRecordApply::SetSize(const NorSize& size)
    {
        sizeEntry = size;
        Fetch(InodeTable(), volume.State().inodeTable, size.inode, Step::sizedFileFetched);
    }

    void NorRecordApply::SizedFileFetched()
    {
        auto record = RecordOfType(sizeEntry.inode, NorInodeType::file);
        if (!record.has_value() || sizeEntry.inlineBytes.size() > volume.Config().inlineBytes || sizeEntry.inlineBytes.size() > sizeEntry.size)
            onDone(Result::corrupted);
        else
        {
            dataInode = sizeEntry.inode;
            dataMap = MapOf(*record);
            ReleaseBlocks(BlocksOfSize(sizeEntry.size), BlocksOfSize(LoadLittle32(*record, norInodeSizeOffset)), Step::sizeReleased);
        }
    }

    void NorRecordApply::SizeReleased()
    {
        Fetch(InodeTable(), volume.State().inodeTable, sizeEntry.inode, Step::writeSize);
    }

    void NorRecordApply::WriteSize()
    {
        auto bytes = NorBTree(volume.Cache(), InodeTable()).Modify(volume.State().inodeTable, sizeEntry.inode);
        auto inlineArea = infra::Head(infra::DiscardHead(bytes, norInodeRecordFixedBytes), volume.Config().inlineBytes);
        SetMapOf(bytes, dataMap);
        StoreLittle32(bytes, norInodeSizeOffset, sizeEntry.size);
        StoreLittle16(bytes, norInodeInlineLengthOffset, static_cast<uint16_t>(sizeEntry.inlineBytes.size()));
        std::ranges::fill(inlineArea, uint8_t{ 0 });
        std::ranges::copy(sizeEntry.inlineBytes, inlineArea.begin());
        ApplyNext();
    }

    void NorRecordApply::ReleaseBlocks(uint32_t from, uint32_t to, Step next)
    {
        releaseFrom = from;
        releaseIndex = std::min(to, NorRadixTree::IndicesCovered(dataMap, volume.Geometry().nodeSize));
        afterRelease = next;
        ReleaseNextBlock();
    }

    void NorRecordApply::ReleaseNextBlock()
    {
        auto fetchedAtOnce = true;
        while (fetchedAtOnce)
        {
            if (releaseIndex <= releaseFrom)
            {
                Continue(afterRelease);
                return;
            }

            --releaseIndex;
            releasing = true;
            fetchedWhileReleasing = false;
            Fetch(DataMap(dataInode), dataMap, releaseIndex, Step::released);
            releasing = false;
            fetchedAtOnce = fetchedWhileReleasing;
        }
    }

    void NorRecordApply::BlockFetched()
    {
        NorRadixTree data{ volume.Cache(), dataInode };
        auto block = data.Find(dataMap, releaseIndex);
        if (block != 0 && !IsInVolume(block))
        {
            onDone(Result::corrupted);
            return;
        }

        if (block != 0)
        {
            data.Clear(dataMap, releaseIndex);
            Release(block);
        }

        if (releasing)
            fetchedWhileReleasing = true;
        else
            ReleaseNextBlock();
    }

    void NorRecordApply::Release(uint32_t block) const
    {
        if (IsOfEpoch(block))
            return;

        volume.State().blocks.Set(block, NorBlockState::freeNotErased);
        volume.State().nodeBlocks.Remove(block);
    }

    bool NorRecordApply::IsOfEpoch(uint32_t block) const
    {
        const auto& epoch = volume.State().epoch;
        return std::ranges::find(epoch, block) != epoch.end();
    }

    uint32_t NorRecordApply::BlocksOfSize(uint32_t size) const
    {
        auto payload = volume.Geometry().PayloadSize();
        return static_cast<uint32_t>((uint64_t{ size } + payload - 1) / payload);
    }

    std::optional<infra::ConstByteRange> NorRecordApply::RecordOfType(uint32_t inode, NorInodeType type) const
    {
        auto record = NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, inode);
        if (!record.has_value() || (*record)[norInodeTypeOffset] != static_cast<uint8_t>(type))
            return std::nullopt;

        return record;
    }

    bool NorRecordApply::HasRoomFor(uint32_t createdNodes) const
    {
        const auto& cache = volume.Cache();
        return cache.ModifiedNodes() + cache.UnmodifiedPinnedNodes() + createdNodes <= cache.Capacity();
    }

    bool NorRecordApply::IsInVolume(uint32_t block) const
    {
        return block >= superblocks && block < volume.Geometry().blocks;
    }

    bool NorRecordApply::IsFreeBlock(uint32_t block) const
    {
        return IsInVolume(block) && volume.State().blocks.State(block) != NorBlockState::inUse;
    }

    void NorRecordApply::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, Step next)
    {
        step = next;
        volume.Fetch().Fetch(map, root, key, [this](NorFetchResult result)
            {
                if (result == NorFetchResult::fetched)
                    Continue(step);
                else
                    onDone(result == NorFetchResult::cacheFull ? Result::unsupported : Result::corrupted);
            });
    }

    void NorRecordApply::Continue(Step next)
    {
        switch (next)
        {
            case Step::inodeFetched:
                InodeFetched();
                break;
            case Step::removedInodeFetched:
                RemovedInodeFetched();
                break;
            case Step::removeRecord:
                RemoveRecord();
                break;
            case Step::recordFetched:
                RecordFetched();
                break;
            case Step::directoryFetched:
                DirectoryFetched();
                break;
            case Step::entryFetched:
                EntryFetched();
                break;
            case Step::removedEntryFetched:
                RemovedEntryFetched();
                break;
            case Step::fileFetched:
                FileFetched();
                break;
            case Step::slotFetched:
                SlotFetched();
                break;
            case Step::sizedFileFetched:
                SizedFileFetched();
                break;
            case Step::sizeReleased:
                SizeReleased();
                break;
            case Step::writeSize:
                WriteSize();
                break;
            default:
                BlockFetched();
                break;
        }
    }

    NorMapId NorRecordApply::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorRecordApply::DirectoryMap(uint32_t directory) const
    {
        return NorMapId{ NorMap::directory, directory, entryValueBytes };
    }

    NorMapId NorRecordApply::DataMap(uint32_t file) const
    {
        return NorMapId{ NorMap::fileData, file, entryValueBytes };
    }
}
