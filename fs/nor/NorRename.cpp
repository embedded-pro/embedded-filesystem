#include "fs/nor/NorRename.hpp"
#include "fs/common/Path.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorDirectory.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t entryValueBytes = 4;
        constexpr std::size_t unmodifiedNodesOfALookup = 3;
    }

    NorRename::NorRename(NorVolume& volume, NorRenameCall&& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , onFinished(onFinished)
    {}

    NorRename::~NorRename() = default;

    void NorRename::Start()
    {
        if (IsRoot(call.from) || IsRoot(call.to))
        {
            Finish(Result::invalidPath);
            return;
        }

        auto split = SplitLast(call.from);
        fromName = split.name;
        volume.Walk().Find(volume.State().inodeTable, split.parent, [this](Result result)
            {
                FromParentFound(result);
            });
    }

    bool NorRename::IsIntoOwnSubtree() const
    {
        return call.to.size() > call.from.size() && call.to.substr(0, call.from.size()) == call.from && call.to[call.from.size()] == '/';
    }

    void NorRename::FromParentFound(Result result)
    {
        const auto& found = volume.Walk().Found();
        if (result != Result::success)
            Finish(result);
        else if (found.type != NorInodeType::directory)
            Finish(Result::notADirectory);
        else
        {
            fromParent = found.inode;
            fromParentMap = found.map;
            volume.NameLookup().Find(volume.State().inodeTable, fromParent, fromParentMap, fromName, [this](Result lookedUp)
                {
                    FromLookedUp(lookedUp);
                });
        }
    }

    void NorRename::FromLookedUp(Result result)
    {
        if (result != Result::success)
        {
            Finish(result);
            return;
        }

        inode = volume.NameLookup().FoundInode();
        fromKey = volume.NameLookup().FoundKey();

        if (IsOpen())
        {
            Finish(Result::busy);
            return;
        }

        if (volume.NameLookup().FoundRecord().type == NorInodeType::directory && IsIntoOwnSubtree())
        {
            Finish(Result::invalidPath);
            return;
        }

        auto split = SplitLast(call.to);
        toName = split.name;
        volume.Walk().Find(volume.State().inodeTable, split.parent, [this](Result found)
            {
                ToParentFound(found);
            });
    }

    bool NorRename::IsOpen() const
    {
        for (const auto& file : volume.OpenFiles())
            if (file.State().inode == inode)
                return true;

        for (const auto& directory : volume.OpenDirectories())
            if (directory.Inode() == inode)
                return true;

        return false;
    }

    void NorRename::ToParentFound(Result result)
    {
        const auto& found = volume.Walk().Found();
        if (result != Result::success)
            Finish(result);
        else if (found.type != NorInodeType::directory)
            Finish(Result::notADirectory);
        else if (found.inode == fromParent && toName == fromName)
            Finish(Result::success);
        else
        {
            toParent = found.inode;
            toParentMap = found.map;
            volume.NameLookup().Find(volume.State().inodeTable, toParent, toParentMap, toName, [this](Result lookedUp)
                {
                    ToLookedUp(lookedUp);
                });
        }
    }

    void NorRename::ToLookedUp(Result result)
    {
        auto freeKey = volume.NameLookup().FreeKey();
        if (result == Result::success)
            Finish(Result::alreadyExists);
        else if (result != Result::notFound)
            Finish(result);
        else if (!freeKey.has_value())
            Finish(Result::noSpace);
        else
        {
            toKey = freeKey.value_or(0);
            FetchPaths();
        }
    }

    void NorRename::FetchPaths()
    {
        volume.Cache().UnpinAll();
        Fetch(InodeTable(), volume.State().inodeTable, inode, [this]
            {
                Fetch(InodeTable(), volume.State().inodeTable, fromParent, [this]
                    {
                        Fetch(InodeTable(), volume.State().inodeTable, toParent, [this]
                            {
                                Fetch(DirectoryMap(fromParent), fromParentMap, fromKey, [this]
                                    {
                                        Fetch(DirectoryMap(toParent), toParentMap, toKey, [this]
                                            {
                                                CheckRoom();
                                            });
                                    });
                            });
                    });
            });
    }

    void NorRename::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t fetchKey, const infra::Function<void()>& onFetchedDone)
    {
        onFetched = onFetchedDone;
        volume.Fetch().Fetch(map, root, fetchKey, [this](NorFetchResult result)
            {
                // The walks and lookups have just read every node of these paths, so a fetch fails only for want of room
                if (result == NorFetchResult::fetched)
                    onFetched();
                else
                    CheckpointAndRestart();
            });
    }

    void NorRename::CheckRoom()
    {
        const auto& cache = volume.Cache();
        NorBTree entries{ volume.Cache(), DirectoryMap(toParent) };
        auto insert = entries.NodesModifiedByInsert(toParentMap, toKey);

        if (!insert.has_value())
        {
            Finish(Result::noSpace);
            return;
        }

        auto splits = insert.value_or(0) - entries.NodesModifiedByChange(toParentMap, toKey);
        if (cache.ModifiedNodes() + cache.UnmodifiedPinnedNodes() + splits > cache.Capacity())
            CheckpointAndRestart();
        else
            Commit();
    }

    void NorRename::CheckpointAndRestart()
    {
        volume.Checkpoints().Write([this](Result result)
            {
                if (result == Result::success)
                    Start();
                else
                    Finish(result);
            });
    }

    void NorRename::Commit()
    {
        volume.Journal().Commit(norDirectoryEntryRemoveBytes + norDirectoryEntryPutBytes + norInodePutFixedBytes + NorInodeRecordBytes(volume.Config()), [this](CrcRecord& record)
            {
                AddEntries(record);
            },
            [this](Result result)
            {
                Committed(result);
            });
    }

    void NorRename::AddEntries(CrcRecord& record) const
    {
        // The path of the record stays pinned, and a checkpoint before the record keeps pinned nodes cached
        auto bytes = NorBTree(volume.Cache(), InodeTable()).Find(volume.State().inodeTable, inode);
        really_assert(bytes.has_value());
        auto renamed = ParseNorInodeRecord(*bytes, volume.Config());
        really_assert(renamed.has_value());
        renamed->parent = toParent;
        renamed->name = infra::StringAsByteRange(toName);

        // The put comes first, so that it lands in the tree that CheckRoom measured; a removal only frees nodes
        AddNorEntry(record, NorDirectoryEntryPut{ toParent, toKey, inode });
        AddNorEntry(record, NorDirectoryEntryRemove{ fromParent, fromKey });
        AddNorInodePut(record, inode, *renamed, volume.Config());
    }

    void NorRename::Committed(Result result)
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

    void NorRename::Finish(Result result)
    {
        volume.Cache().UnpinAll();
        onFinished(result);
        call.onDone(result);
    }

    NorMapId NorRename::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorRename::DirectoryMap(uint32_t directory) const
    {
        return NorMapId{ NorMap::directory, directory, entryValueBytes };
    }
}
