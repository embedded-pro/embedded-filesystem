#ifndef FS_COMMON_SECTOR_SOURCE_HPP
#define FS_COMMON_SECTOR_SOURCE_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class SectorSource
    {
    protected:
        SectorSource() = default;
        SectorSource(const SectorSource& other) = delete;
        SectorSource& operator=(const SectorSource& other) = delete;
        ~SectorSource() = default;

    public:
        virtual uint32_t SectorSize() const = 0;
        virtual void Read(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone) = 0;
    };
}

#endif
