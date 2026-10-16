#ifndef FS_COMMON_SECTOR_STORE_HPP
#define FS_COMMON_SECTOR_STORE_HPP

#include "fs/common/SectorSource.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class SectorStore
        : public SectorSource
    {
    protected:
        SectorStore() = default;
        SectorStore(const SectorStore& other) = delete;
        SectorStore& operator=(const SectorStore& other) = delete;
        ~SectorStore() = default;

    public:
        virtual void Modify(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone) = 0;
    };
}

#endif
