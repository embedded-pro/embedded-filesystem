#include "fs/nor/NorRecordApply.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorLayout.hpp"
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
