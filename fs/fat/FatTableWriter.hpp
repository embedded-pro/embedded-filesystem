#ifndef FS_FAT_FAT_TABLE_WRITER_HPP
#define FS_FAT_FAT_TABLE_WRITER_HPP

#include "fs/common/SectorStore.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatTableWriter
    {
    public:
        FatTableWriter(SectorStore& sectors, const FatGeometry& geometry);
        FatTableWriter(const FatTableWriter& other) = delete;
        FatTableWriter& operator=(const FatTableWriter& other) = delete;
        ~FatTableWriter();

        void Write(uint32_t cluster, uint32_t value, const infra::Function<void(Result result)>& onDone);
        uint32_t SectorOf(uint32_t cluster) const;

    private:
        void WriteSecondPart();

    private:
        SectorStore& sectors;
        const FatGeometry& geometry;
        uint32_t cluster{ 0 };
        uint32_t value{ 0 };
        bool straddles{ false };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
