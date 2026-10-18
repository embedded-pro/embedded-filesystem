#include "fs/nor/NorFileCommit.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorVolume.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t blockNumberBytes = 4;
        constexpr std::size_t unmodifiedNodesOfALookup = 3;
    }

    NorFileCommit::NorFileCommit(NorVolume& volume)
        : volume(volume)
        , resume([this](Result result)
              {
                  Resume(result);
              })
    {}

    NorFileCommit::~NorFileCommit() = default;

    void NorFileCommit::Commit(NorOpenFile& committed, NorReserve commitReserve, const infra::Function<void(Result result)>& onCommitted)
    {
        file = &committed;
        reserve = commitReserve;
        onDone = onCommitted;

        if (!file->Uncommitted())
        {
            onDone(Result::success);
            return;
        }

        auto whole = file->size / volume.Geometry().programUnit * volume.Geometry().programUnit;
        if (file->tailBlock.has_value() && whole > file->programmed)
            volume.FileFlush().Flush(*file, whole, reserve, Then(Step::retry));
        else
            FetchPaths();
    }

    void NorFileCommit::Retry(Result result)
    {
        if (result == Result::success)
            FetchPaths();
        else
            Finish(result);
    }

    void NorFileCommit::FetchPaths()
    {
        volume.Cache().UnpinAll();
        Fetch(InodeTable(), volume.State().inodeTable, file->inode, Step::recordFetched);
    }

    void NorFileCommit::RecordFetched()
    {
        auto record = NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, file->inode);
        if (!record.has_value())
        {
            Finish(Result::corrupted);
            return;
        }

        dataMap = NorMapRoot{ LoadLittle32(*record, norInodeMapRootOffset), (*record)[norInodeMapHeightOffset] };
        secondLeafIndex = file->SecondLeafIndex(NorRadixFanout(volume.Geometry().nodeSize));
        if (file->entries.empty())
            CheckRoom();
        else
            Fetch(DataMap(), dataMap, file->entries.front().index, Step::firstLeafFetched);
    }

    void NorFileCommit::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key, Step next)
    {
        step = next;
        volume.Fetch().Fetch(map, root, key, [this](NorFetchResult result)
            {
                if (result == NorFetchResult::fetched)
                    Fetched();
                else if (result == NorFetchResult::cacheFull)
                    CheckpointAndRetry();
                else
                    Finish(Result::corrupted);
            });
    }

    void NorFileCommit::Fetched()
    {
        if (step == Step::recordFetched)
            RecordFetched();
        else if (step == Step::firstLeafFetched)
            Fetch(DataMap(), dataMap, secondLeafIndex.value_or(file->entries.front().index), Step::secondLeafFetched);
        else
            CheckRoom();
    }

    void NorFileCommit::CheckRoom()
    {
        auto& cache = volume.Cache();
        NorRadixTree data{ cache, file->inode };
        uint32_t created = 0;

        if (!file->entries.empty())
            created = data.NodesCreatedBySet(dataMap, file->entries.front().index).value_or(0) + (secondLeafIndex.has_value() ? data.NodesCreatedBySet(dataMap, secondLeafIndex.value_or(0)).value_or(0) : 0);

        if (file->size < file->committedSize)
            created += dataMap.height;

        if (cache.ModifiedNodes() + cache.UnmodifiedPinnedNodes() + created > cache.Capacity())
            CheckpointAndRetry();
        else
            Write();
    }

    void NorFileCommit::CheckpointAndRetry()
    {
        volume.Checkpoints().Write(Then(Step::retry));
    }

    void NorFileCommit::Write()
    {
        auto bytes = file->entries.size() * norBlockSetEntryBytes + norSizeEntryFixedBytes + file->Pending().size();
        volume.Journal().Commit(bytes, [this](CrcRecord& record)
            {
                for (const auto& entry : file->entries)
                    AddNorEntry(record, NorBlockSet{ file->inode, entry.index, entry.previous, entry.block });
                AddNorEntry(record, NorSize{ file->inode, file->size, file->Pending() });
            },
            Then(Step::committed));
    }

    void NorFileCommit::Committed(Result result)
    {
        const auto& cache = volume.Cache();
        if (result != Result::success)
        {
            Finish(result);
            return;
        }

        file->committedSize = file->size;
        file->entries.clear();
        if (cache.Capacity() - cache.ModifiedNodes() < unmodifiedNodesOfALookup)
            volume.Checkpoints().Write(Then(Step::finish));
        else
            Finish(Result::success);
    }

    void NorFileCommit::Finish(Result result)
    {
        volume.Cache().UnpinAll();
        onDone(result);
    }

    const infra::Function<void(Result result)>& NorFileCommit::Then(Step next)
    {
        step = next;
        return resume;
    }

    void NorFileCommit::Resume(Result result)
    {
        switch (step)
        {
            case Step::retry:
                Retry(result);
                break;
            case Step::committed:
                Committed(result);
                break;
            default:
                Finish(result);
                break;
        }
    }

    NorMapId NorFileCommit::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorFileCommit::DataMap() const
    {
        return NorMapId{ NorMap::fileData, file->inode, blockNumberBytes };
    }
}
