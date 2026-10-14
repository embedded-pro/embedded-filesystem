#ifndef FS_NOR_NOR_NAME_LOOKUP_HPP
#define FS_NOR_NOR_NAME_LOOKUP_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorEntrySeek.hpp"
#include "fs/nor/NorInodeRead.hpp"
#include "fs/nor/NorNode.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class NorNameLookup
    {
    public:
        NorNameLookup(NorInodeRead& inodeRead, NorEntrySeek& entrySeek);
        NorNameLookup(const NorNameLookup& other) = delete;
        NorNameLookup& operator=(const NorNameLookup& other) = delete;
        ~NorNameLookup() = default;

        void Find(const NorMapRoot& inodeTable, uint32_t directory, const NorMapRoot& map, infra::BoundedConstString name, const infra::Function<void(Result result)>& onDone);
        uint32_t FoundInode() const;
        const NorInodeRecord& FoundRecord() const;

    private:
        void SeekFrom(uint32_t key);
        void Sought(Result result);
        void CandidateRead(Result result);

    private:
        NorInodeRead& inodeRead;
        NorEntrySeek& entrySeek;
        NorMapRoot inodeTable;
        uint32_t directory{ 0 };
        NorMapRoot map;
        infra::BoundedConstString name;
        uint32_t lastKey{ 0 };
        NorEntrySeek::Entry candidate;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
