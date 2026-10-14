#include "fs/nor/NorMakeDirectory.hpp"
#include "fs/common/Path.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorVolume.hpp"
#include <limits>

namespace fs
{
    namespace
    {
        constexpr uint32_t entryValueBytes = 4;
        constexpr std::size_t unmodifiedNodesOfALookup = 3;
    }

    NorMakeDirectory::NorMakeDirectory(NorVolume& volume, NorMakeDirectoryCall&& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , onFinished(onFinished)
    {}

    NorMakeDirectory::~NorMakeDirectory() = default;

    void NorMakeDirectory::Start()
    {
        if (IsRoot(call.path))
        {
            Finish(Result::alreadyExists);
            return;
        }

        auto split = SplitLast(call.path);
        name = split.name;
        volume.Walk().Find(volume.State().inodeTable, split.parent, [this](Result result)
            {
                ParentFound(result);
            });
    }

    void NorMakeDirectory::ParentFound(Result result)
    {
        const auto& found = volume.Walk().Found();
        if (result != Result::success)
            Finish(result);
        else if (found.type != NorInodeType::directory)
            Finish(Result::notADirectory);
        else
        {
            parent = found.inode;
            parentMap = found.map;
            volume.NameLookup().Find(volume.State().inodeTable, parent, parentMap, name, [this](Result lookedUp)
                {
                    NameLookedUp(lookedUp);
                });
        }
    }

    void NorMakeDirectory::NameLookedUp(Result result)
    {
        auto freeKey = volume.NameLookup().FreeKey();
        if (result == Result::success)
            Finish(Result::alreadyExists);
        else if (result != Result::notFound)
            Finish(result);
        else if (!freeKey.has_value() || volume.State().nextInode == std::numeric_limits<uint32_t>::max())
            Finish(Result::noSpace);
        else
        {
            key = freeKey.value_or(0);
            inode = volume.State().nextInode;
            FetchPaths();
        }
    }

    void NorMakeDirectory::FetchPaths()
    {
        volume.Cache().UnpinAll();
        Fetch(InodeTable(), volume.State().inodeTable, inode, [this]
            {
                Fetch(InodeTable(), volume.State().inodeTable, parent, [this]
                    {
                        Fetch(DirectoryMap(), parentMap, key, [this]
                            {
                                CheckRoom();
                            });
                    });
            });
    }

    void NorMakeDirectory::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t fetchKey, const infra::Function<void()>& onFetchedDone)
    {
        onFetched = onFetchedDone;
        volume.Fetch().Fetch(map, root, fetchKey, [this](NorFetchResult result)
            {
                if (result == NorFetchResult::fetched)
                    onFetched();
                else if (result == NorFetchResult::cacheFull)
                    CheckpointAndRestart();
                else
                    Finish(Result::corrupted);
            });
    }

    void NorMakeDirectory::CheckRoom()
    {
        auto& cache = volume.Cache();
        const auto& inodeTable = volume.State().inodeTable;
        NorBTree inodes{ cache, InodeTable() };
        NorBTree entries{ cache, DirectoryMap() };
        auto inodeInsert = inodes.NodesModifiedByInsert(inodeTable, inode);
        auto entryInsert = entries.NodesModifiedByInsert(parentMap, key);

        if (!inodeInsert.has_value() || !entryInsert.has_value())
        {
            Finish(Result::noSpace);
            return;
        }

        auto splits = inodeInsert.value_or(0) - inodes.NodesModifiedByChange(inodeTable, inode) + entryInsert.value_or(0) - entries.NodesModifiedByChange(parentMap, key);
        if (cache.ModifiedNodes() + cache.UnmodifiedPinnedNodes() + splits > cache.Capacity())
            CheckpointAndRestart();
        else
            Commit();
    }

    void NorMakeDirectory::CheckpointAndRestart()
    {
        volume.Checkpoints().Write([this](Result result)
            {
                if (result == Result::success)
                    Start();
                else
                    Finish(result);
            });
    }

    void NorMakeDirectory::Commit()
    {
        const auto& config = volume.Config();
        volume.Journal().Commit(norInodePutFixedBytes + NorInodeRecordBytes(config) + norDirectoryEntryPutBytes, [this](CrcRecord& record)
            {
                AddNorInodePut(record, inode, NorInodeRecord{ parent, NorInodeType::directory, 0, 0, 0, {}, infra::StringAsByteRange(name) }, volume.Config());
                AddNorEntry(record, NorDirectoryEntryPut{ parent, key, inode });
            },
            [this](Result result)
            {
                Committed(result);
            });
    }

    void NorMakeDirectory::Committed(Result result)
    {
        const auto& cache = volume.Cache();
        if (result == Result::success && cache.Capacity() - cache.ModifiedNodes() < unmodifiedNodesOfALookup)
            volume.Checkpoints().Write([this](Result written)
                {
                    Finish(written);
                });
        else
            Finish(result);
    }

    void NorMakeDirectory::Finish(Result result)
    {
        volume.Cache().UnpinAll();
        onFinished(result);
        call.onDone(result);
    }

    NorMapId NorMakeDirectory::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorMakeDirectory::DirectoryMap() const
    {
        return NorMapId{ NorMap::directory, parent, entryValueBytes };
    }
}
