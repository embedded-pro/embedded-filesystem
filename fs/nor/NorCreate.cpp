#include "fs/nor/NorCreate.hpp"
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

    NorCreate::NorCreate(NorVolume& volume)
        : volume(volume)
        , resume([this](Result result)
              {
                  Resume(result);
              })
    {}

    NorCreate::~NorCreate() = default;

    void NorCreate::Create(infra::BoundedConstString createdPath, NorInodeType createdType, const infra::Function<Result()>& prepareCreation, const infra::Function<void(Result result)>& onCreated)
    {
        path = createdPath;
        type = createdType;
        prepare = prepareCreation;
        onDone = onCreated;
        Start();
    }

    uint32_t NorCreate::Inode() const
    {
        return inode;
    }

    const NorFoundInode& NorCreate::Found() const
    {
        return found;
    }

    infra::ConstByteRange NorCreate::FoundInlineBytes() const
    {
        return foundInlineBytes;
    }

    void NorCreate::Start()
    {
        if (IsRoot(path))
        {
            found = NorFoundInode{ NorPathWalk::rootInode };
            foundInlineBytes = {};
            Finish(Result::alreadyExists);
            return;
        }

        auto split = SplitLast(path);
        name = split.name;
        volume.Walk().Find(volume.State().inodeTable, split.parent, Then(Step::parentFound));
    }

    void NorCreate::ParentFound(Result result)
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
            volume.NameLookup().Find(volume.State().inodeTable, parent, parentMap, name, Then(Step::nameLookedUp));
        }
    }

    void NorCreate::NameLookedUp(Result result)
    {
        auto freeKey = volume.NameLookup().FreeKey();
        if (result == Result::success)
        {
            const auto& record = volume.NameLookup().FoundRecord();
            found = NorFoundInode{ volume.NameLookup().FoundInode(), record.type, record.size, NorMapRoot{ record.mapRoot, record.mapHeight } };
            foundInlineBytes = record.inlineBytes;
            Finish(Result::alreadyExists);
        }
        else if (result != Result::notFound)
            Finish(result);
        else if (!freeKey.has_value() || volume.State().nextInode == std::numeric_limits<uint32_t>::max())
            Finish(Result::noSpace);
        else if (auto prepared = prepare(); prepared != Result::success)
            Finish(prepared);
        else
        {
            key = freeKey.value_or(0);
            inode = volume.State().nextInode;
            FetchPaths();
        }
    }

    void NorCreate::FetchPaths()
    {
        volume.Cache().UnpinAll();
        Fetch(InodeTable(), volume.State().inodeTable, inode, Step::inodeFetched);
    }

    void NorCreate::Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t fetchKey, Step next)
    {
        step = next;
        volume.Fetch().Fetch(map, root, fetchKey, [this](NorFetchResult result)
            {
                if (result == NorFetchResult::fetched)
                    Fetched();
                else if (result == NorFetchResult::cacheFull)
                    CheckpointAndRestart();
                else
                    Finish(Result::corrupted);
            });
    }

    void NorCreate::Fetched()
    {
        if (step == Step::inodeFetched)
            Fetch(InodeTable(), volume.State().inodeTable, parent, Step::parentFetched);
        else if (step == Step::parentFetched)
            Fetch(DirectoryMap(), parentMap, key, Step::entryFetched);
        else
            CheckRoom();
    }

    void NorCreate::CheckRoom()
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

    void NorCreate::CheckpointAndRestart()
    {
        volume.Checkpoints().Write(Then(Step::checkpointed));
    }

    void NorCreate::Checkpointed(Result result)
    {
        if (result == Result::success)
            Start();
        else
            Finish(result);
    }

    void NorCreate::Commit()
    {
        const auto& config = volume.Config();
        volume.Journal().Commit(norInodePutFixedBytes + NorInodeRecordBytes(config) + norDirectoryEntryPutBytes, [this](CrcRecord& record)
            {
                AddNorInodePut(record, inode, NorInodeRecord{ parent, type, 0, 0, 0, {}, infra::StringAsByteRange(name) }, volume.Config());
                AddNorEntry(record, NorDirectoryEntryPut{ parent, key, inode });
            },
            Then(Step::committed));
    }

    void NorCreate::Committed(Result result)
    {
        const auto& cache = volume.Cache();
        if (result == Result::success && cache.Capacity() - cache.ModifiedNodes() < unmodifiedNodesOfALookup)
            volume.Checkpoints().Write(Then(Step::finish));
        else
            Finish(result);
    }

    void NorCreate::Finish(Result result)
    {
        volume.Cache().UnpinAll();
        onDone(result);
    }

    const infra::Function<void(Result result)>& NorCreate::Then(Step next)
    {
        step = next;
        return resume;
    }

    void NorCreate::Resume(Result result)
    {
        switch (step)
        {
            case Step::parentFound:
                ParentFound(result);
                break;
            case Step::nameLookedUp:
                NameLookedUp(result);
                break;
            case Step::checkpointed:
                Checkpointed(result);
                break;
            case Step::committed:
                Committed(result);
                break;
            default:
                Finish(result);
                break;
        }
    }

    NorMapId NorCreate::InodeTable() const
    {
        return NorMapId{ NorMap::inodeTable, 0, NorInodeRecordBytes(volume.Config()) };
    }

    NorMapId NorCreate::DirectoryMap() const
    {
        return NorMapId{ NorMap::directory, parent, entryValueBytes };
    }
}
