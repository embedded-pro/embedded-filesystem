#ifndef FS_NOR_NOR_INODE_READ_HPP
#define FS_NOR_NOR_INODE_READ_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class NorInodeRead
    {
    public:
        NorInodeRead(NorNodeCache& cache, NorMapFetch& fetch, const NorConfig& config);
        NorInodeRead(const NorInodeRead& other) = delete;
        NorInodeRead& operator=(const NorInodeRead& other) = delete;
        ~NorInodeRead() = default;

        void Read(const NorMapRoot& inodeTable, uint32_t inode, const infra::Function<void(Result result)>& onDone);
        const NorInodeRecord& Record() const;

    private:
        void Fetched(NorFetchResult result);

    private:
        NorNodeCache& cache;
        NorMapFetch& fetch;
        const NorConfig& config;
        NorMapRoot inodeTable;
        uint32_t inode{ 0 };
        NorInodeRecord record;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
