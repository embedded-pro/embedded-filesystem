#include "fs/nor/NorRemove.hpp"
#include "fs/common/Path.hpp"
#include "fs/nor/NorDirectory.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorVolume.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t entryValueBytes = 4;
        constexpr std::size_t unmodifiedNodesOfALookup = 3;
    }

    NorRemove::NorRemove(NorVolume& volume, NorRemoveCall&& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , onFinished(onFinished)
    {}

    NorRemove::~NorRemove() = default;

    void NorRemove::Start()
    {
        if (IsRoot(call.path))
        {
            Finish(Result::invalidPath);
            return;
        }

        auto split = SplitLast(call.path);
        name = split.name;
        volume.Walk().Find(volume.State().inodeTable, split.parent, [this](Result result)
            {
                ParentFound(result);
            });
    }

    void NorRemove::ParentFound(Result result)
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

    void NorRemove::NameLookedUp(Result result)
    {
        if (result != Result::success)
        {
            Finish(result);
            return;
        }

        const auto& record = volume.NameLookup().FoundRecord();
        inode = volume.NameLookup().FoundInode();
        key = volume.NameLookup().FoundKey();
        map = NorMapRoot{ record.mapRoot, record.mapHeight };

        if (IsOpen())
            Finish(Result::busy);
        else if (record.type == NorInodeType::directory && map.address != 0)
            Finish(Result::notEmpty);
        else
            FetchPaths();
    }

    bool NorRemove::IsOpen() const
    {
        for (const auto& file : volume.OpenFiles())
            if (file.State().inode == inode)
                return true;

        for (const auto& directory : volume.OpenDirectories())
            if (directory.Inode() == inode)
                return true;

        return false;
    }

    void NorRemove::FetchPaths()
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

    void NorRemove::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t fetchKey, const infra::Function<void()>& onFetchedDone)
    {
        onFetched = onFetchedDone;
        volume.Fetch().Fetch(map, root, fetchKey, [this](NorFetchResult result)
            {
                // The lookup has just read every node of these paths, so a fetch fails only for want of room
                if (result == NorFetchResult::fetched)
                    onFetched();
                else
                    CheckpointAndRestart();
            });
    }

    void NorRemove::CheckRoom()
    {
        const auto& cache = volume.Cache();
        if (cache.ModifiedNodes() + cache.UnmodifiedPinnedNodes() + map.height > cache.Capacity())
            CheckpointAndRestart();
        else
            Commit();
    }

    void NorRemove::CheckpointAndRestart()
    {
        volume.Checkpoints().Write([this](Result result)
            {
                if (result == Result::success)
                    Start();
                else
                    Finish(result);
            });
    }

    void NorRemove::Commit()
    {
        volume.Journal().Commit(norDirectoryEntryRemoveBytes + norInodeRemoveBytes, [this](CrcRecord& record)
            {
                AddNorEntry(record, NorDirectoryEntryRemove{ parent, key });
                AddNorEntry(record, NorInodeRemove{ inode });
            },
            [this](Result result)
            {
                Committed(result);
            });
    }

    void NorRemove::Committed(Result result)
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

    void NorRemove::Finish(Result result)
    {
        volume.Cache().UnpinAll();
        onFinished(result);
        call.onDone(result);
    }

    NorMapId NorRemove::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorRemove::DirectoryMap() const
    {
        return NorMapId{ NorMap::directory, parent, entryValueBytes };
    }
}
