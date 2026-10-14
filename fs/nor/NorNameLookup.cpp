#include "fs/nor/NorNameLookup.hpp"
#include "fs/nor/NorDirectoryKey.hpp"

namespace fs
{
    NorNameLookup::NorNameLookup(NorInodeRead& inodeRead, NorEntrySeek& entrySeek)
        : inodeRead(inodeRead)
        , entrySeek(entrySeek)
    {}

    void NorNameLookup::Find(const NorMapRoot& inodeTable, uint32_t directory, const NorMapRoot& map, infra::BoundedConstString name, const infra::Function<void(Result result)>& onFound)
    {
        this->inodeTable = inodeTable;
        this->directory = directory;
        this->map = map;
        this->name = name;
        onDone = onFound;

        auto key = NorDirectoryKey(name);
        lastKey = NorLastDirectoryKey(key);
        SeekFrom(key);
    }

    uint32_t NorNameLookup::FoundInode() const
    {
        return candidate.inode;
    }

    const NorInodeRecord& NorNameLookup::FoundRecord() const
    {
        return inodeRead.Record();
    }

    void NorNameLookup::SeekFrom(uint32_t key)
    {
        entrySeek.Seek(directory, map, key, [this](Result result)
            {
                Sought(result);
            });
    }

    void NorNameLookup::Sought(Result result)
    {
        const auto& found = entrySeek.Found();
        if (result != Result::success)
            onDone(result);
        else if (!found.has_value() || found->key > lastKey)
            onDone(Result::notFound);
        else
        {
            candidate = *found;
            inodeRead.Read(inodeTable, candidate.inode, [this](Result read)
                {
                    CandidateRead(read);
                });
        }
    }

    void NorNameLookup::CandidateRead(Result result)
    {
        const auto& record = inodeRead.Record();
        if (result != Result::success)
            onDone(result);
        else if (record.parent != directory)
            onDone(Result::corrupted);
        else if (infra::ContentsEqual(record.name, infra::StringAsByteRange(name)))
            onDone(Result::success);
        else if (candidate.key == lastKey)
            onDone(Result::notFound);
        else
            SeekFrom(candidate.key + 1);
    }
}
