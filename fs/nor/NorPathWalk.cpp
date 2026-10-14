#include "fs/nor/NorPathWalk.hpp"

namespace fs
{
    namespace
    {
        NorFoundInode FoundOf(uint32_t inode, const NorInodeRecord& record)
        {
            return NorFoundInode{ inode, record.type, record.size, NorMapRoot{ record.mapRoot, record.mapHeight } };
        }
    }

    NorPathWalk::NorPathWalk(NorInodeRead& inodeRead, NorNameLookup& nameLookup)
        : inodeRead(inodeRead)
        , nameLookup(nameLookup)
    {}

    void NorPathWalk::Find(const NorMapRoot& inodeTable, infra::BoundedConstString path, const infra::Function<void(Result result)>& onFound)
    {
        this->inodeTable = inodeTable;
        onDone = onFound;
        found = NorFoundInode{ rootInode };
        component = infra::BoundedConstString();
        components.emplace(path);
        inodeRead.Read(inodeTable, rootInode, [this](Result result)
            {
                RootRead(result);
            });
    }

    const NorFoundInode& NorPathWalk::Found() const
    {
        return found;
    }

    infra::BoundedConstString NorPathWalk::FoundName() const
    {
        return component;
    }

    void NorPathWalk::RootRead(Result result)
    {
        if (result != Result::success)
            onDone(result);
        else if (inodeRead.Record().type != NorInodeType::directory)
            onDone(Result::corrupted);
        else
        {
            found = FoundOf(rootInode, inodeRead.Record());
            Descend();
        }
    }

    void NorPathWalk::Descend()
    {
        auto next = components->Next();
        if (!next.has_value())
            onDone(Result::success);
        else if (found.type != NorInodeType::directory)
            onDone(Result::notADirectory);
        else
        {
            component = *next;
            nameLookup.Find(inodeTable, found.inode, found.map, component, [this](Result result)
                {
                    LookedUp(result);
                });
        }
    }

    void NorPathWalk::LookedUp(Result result)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        found = FoundOf(nameLookup.FoundInode(), nameLookup.FoundRecord());
        Descend();
    }
}
