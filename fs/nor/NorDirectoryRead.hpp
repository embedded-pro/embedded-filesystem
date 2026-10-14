#ifndef FS_NOR_NOR_DIRECTORY_READ_HPP
#define FS_NOR_NOR_DIRECTORY_READ_HPP

#include "fs/interfaces/EntryInfo.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorEntrySeek.hpp"
#include "fs/nor/NorInodeRead.hpp"
#include "fs/nor/NorNode.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorDirectoryRead
    {
    public:
        NorDirectoryRead(NorInodeRead& inodeRead, NorEntrySeek& entrySeek);
        NorDirectoryRead(const NorDirectoryRead& other) = delete;
        NorDirectoryRead& operator=(const NorDirectoryRead& other) = delete;
        ~NorDirectoryRead() = default;

        void Read(const NorMapRoot& inodeTable, uint32_t directory, uint32_t fromKey, const infra::Function<void(Result result)>& onDone);
        const std::optional<NorEntrySeek::Entry>& Found() const;
        EntryInfo Info() const;

    private:
        void DirectoryRead(Result result);
        void Sought(Result result);
        void EntryRead(Result result);

    private:
        NorInodeRead& inodeRead;
        NorEntrySeek& entrySeek;
        NorMapRoot inodeTable;
        uint32_t directory{ 0 };
        uint32_t fromKey{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
