#ifndef FS_NOR_NOR_FILE_TRUNCATE_HPP
#define FS_NOR_NOR_FILE_TRUNCATE_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorOpenFile.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class NorVolume;

    class NorFileTruncate
    {
    public:
        explicit NorFileTruncate(NorVolume& volume);
        NorFileTruncate(const NorFileTruncate& other) = delete;
        NorFileTruncate& operator=(const NorFileTruncate& other) = delete;
        ~NorFileTruncate();

        void Truncate(NorOpenFile& file, uint32_t size, const infra::Function<void(Result result)>& onDone);

    private:
        void TruncateCommitted();
        void FreeEntriesFrom(uint32_t count) const;
        void FindTail();
        void TailFetched(NorFetchResult result);
        void TailFound(uint32_t block);
        void Commit();
        bool IsDataBlock(uint32_t block) const;
        NorMapId InodeTable() const;
        NorMapId DataMap() const;

    private:
        NorVolume& volume;
        NorOpenFile* file{ nullptr };
        uint32_t size{ 0 };
        uint32_t lastIndex{ 0 };
        NorMapRoot dataMap;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
