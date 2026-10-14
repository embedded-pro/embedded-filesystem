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
        else if (const auto* directoryPut = std::get_if<NorDirectoryEntryPut>(&*entry))
            PutDirectoryEntry(*directoryPut);
        else if (const auto* set = std::get_if<NorBlockSet>(&*entry))
            SetBlock(*set);
        else if (const auto* size = std::get_if<NorSize>(&*entry))
            SetSize(*size);
        else if (std::holds_alternative<NorLink>(*entry) && linked && isFirst)
            ApplyNext();
        else if (std::holds_alternative<NorLink>(*entry))
            onDone(Result::corrupted);
        else
            onDone(Result::unsupported);
    }

    void NorRecordApply::PutInode(const NorInodePut& put)
    {
        if (put.inode == 0 || !ParseNorInodeRecord(put.record, volume.Config()).has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        inodePut = put;
        Fetch(InodeTable(), volume.State().inodeTable, put.inode, [this]
            {
                InodeFetched();
            });
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

        auto map = existing.has_value() ? MapOf(*existing) : NorMapRoot{};
        auto record = existing.has_value() ? inodes.Modify(state.inodeTable, inodePut.inode) : inodes.Insert(state.inodeTable, inodePut.inode);
        std::ranges::copy(inodePut.record, record.begin());
        SetMapOf(record, map);
        state.nextInode = static_cast<uint32_t>(std::max<uint64_t>(state.nextInode, std::min<uint64_t>(uint64_t{ inodePut.inode } + 1, std::numeric_limits<uint32_t>::max())));
        ApplyNext();
    }

    void NorRecordApply::PutDirectoryEntry(const NorDirectoryEntryPut& put)
    {
        entryPut = put;
        Fetch(InodeTable(), volume.State().inodeTable, put.directory, [this]
            {
                DirectoryFetched();
            });
    }

    void NorRecordApply::DirectoryFetched()
    {
        auto record = NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, entryPut.directory);
        if (!record.has_value() || (*record)[norInodeTypeOffset] != static_cast<uint8_t>(NorInodeType::directory))
        {
            onDone(Result::corrupted);
            return;
        }

        directoryMap = MapOf(*record);
        Fetch(DirectoryMap(), directoryMap, entryPut.key, [this]
            {
                EntryFetched();
            });
    }

    void NorRecordApply::EntryFetched()
    {
        auto& state = volume.State();
        NorBTree entries{ volume.Cache(), DirectoryMap() };
        if (entries.Find(directoryMap, entryPut.key).has_value() || !entries.NodesModifiedByInsert(directoryMap, entryPut.key).has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        StoreLittle32(entries.Insert(directoryMap, entryPut.key), 0, entryPut.inode);
        SetMapOf(NorBTree(volume.Cache(), InodeTable()).Modify(state.inodeTable, entryPut.directory), directoryMap);
        ApplyNext();
    }

    void NorRecordApply::SetBlock(const NorBlockSet& set)
    {
        blockSet = set;
        Fetch(InodeTable(), volume.State().inodeTable, set.inode, [this]
            {
                FileFetched();
            });
    }

    void NorRecordApply::FileFetched()
    {
        auto record = FileRecord(blockSet.inode);
        if (!record.has_value())
            onDone(Result::corrupted);
        else if (blockSet.block == 0)
            onDone(Result::unsupported);
        else
        {
            dataMap = MapOf(*record);
            Fetch(NorMapId{ NorMap::fileData, blockSet.inode, entryValueBytes }, dataMap, blockSet.index, [this]
                {
                    SlotFetched();
                });
        }
    }

    void NorRecordApply::SlotFetched()
    {
        auto& state = volume.State();
        NorRadixTree data{ volume.Cache(), blockSet.inode };
        if (data.Find(dataMap, blockSet.index) != blockSet.previous || !IsFreeBlock(blockSet.block) || !data.NodesModifiedBySet(dataMap, blockSet.index).has_value())
        {
            onDone(Result::corrupted);
            return;
        }

        data.Set(dataMap, blockSet.index, blockSet.block);
        SetMapOf(NorBTree(volume.Cache(), InodeTable()).Modify(state.inodeTable, blockSet.inode), dataMap);
        state.blocks.Set(blockSet.block, NorBlockState::inUse);
        if (blockSet.previous != 0 && std::ranges::find(state.epoch, blockSet.previous) == state.epoch.end())
            state.blocks.Set(blockSet.previous, NorBlockState::freeNotErased);

        ApplyNext();
    }

    void NorRecordApply::SetSize(const NorSize& size)
    {
        sizeEntry = size;
        Fetch(InodeTable(), volume.State().inodeTable, size.inode, [this]
            {
                SizedFileFetched();
            });
    }

    void NorRecordApply::SizedFileFetched()
    {
        auto record = FileRecord(sizeEntry.inode);
        if (!record.has_value() || sizeEntry.inlineBytes.size() > volume.Config().inlineBytes || sizeEntry.inlineBytes.size() > sizeEntry.size)
            onDone(Result::corrupted);
        else if (sizeEntry.size < LoadLittle32(*record, norInodeSizeOffset))
            onDone(Result::unsupported);
        else
        {
            auto bytes = NorBTree(volume.Cache(), InodeTable()).Modify(volume.State().inodeTable, sizeEntry.inode);
            auto inlineArea = infra::Head(infra::DiscardHead(bytes, norInodeRecordFixedBytes), volume.Config().inlineBytes);
            StoreLittle32(bytes, norInodeSizeOffset, sizeEntry.size);
            StoreLittle16(bytes, norInodeInlineLengthOffset, static_cast<uint16_t>(sizeEntry.inlineBytes.size()));
            std::ranges::fill(inlineArea, uint8_t{ 0 });
            std::ranges::copy(sizeEntry.inlineBytes, inlineArea.begin());
            ApplyNext();
        }
    }

    std::optional<infra::ConstByteRange> NorRecordApply::FileRecord(uint32_t inode) const
    {
        auto record = NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, inode);
        if (!record.has_value() || (*record)[norInodeTypeOffset] != static_cast<uint8_t>(NorInodeType::file))
            return std::nullopt;

        return record;
    }

    bool NorRecordApply::IsFreeBlock(uint32_t block) const
    {
        const auto& state = volume.State();
        return block >= superblocks && block < volume.Geometry().blocks && state.blocks.State(block) != NorBlockState::inUse;
    }

    void NorRecordApply::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, const infra::Function<void()>& onFetchedDone)
    {
        onFetched = onFetchedDone;
        volume.Fetch().Fetch(map, root, key, [this](NorFetchResult result)
            {
                if (result == NorFetchResult::fetched)
                    onFetched();
                else
                    onDone(result == NorFetchResult::cacheFull ? Result::unsupported : Result::corrupted);
            });
    }

    NorMapId NorRecordApply::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorRecordApply::DirectoryMap() const
    {
        return NorMapId{ NorMap::directory, entryPut.directory, entryValueBytes };
    }
}
