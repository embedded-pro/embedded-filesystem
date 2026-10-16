#ifndef FS_FAT_FAT_TABLE_READER_HPP
#define FS_FAT_FAT_TABLE_READER_HPP

#include "fs/common/SectorSource.hpp"
#include "fs/fat/FatGeometry.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatTableReader
    {
    public:
        FatTableReader(SectorSource& sectors, const FatGeometry& geometry);
        FatTableReader(const FatTableReader& other) = delete;
        FatTableReader& operator=(const FatTableReader& other) = delete;
        ~FatTableReader();

        void Read(uint32_t cluster, const infra::Function<void(Result result, uint32_t value)>& onDone);
        void Next(uint32_t cluster, const infra::Function<void(Result result, std::optional<uint32_t> next)>& onDone);

    private:
        void Locate(uint32_t cluster);
        void OnSector(Result result, infra::ConstByteRange contents);
        void OnNextSector(Result result, infra::ConstByteRange contents);
        void Complete(Result result, uint32_t value);

    private:
        SectorSource& sectors;
        const FatGeometry& geometry;
        uint32_t cluster{ 0 };
        uint32_t sector{ 0 };
        uint32_t offset{ 0 };
        uint8_t lowByte{ 0 };
        infra::AutoResetFunction<void(Result result, uint32_t value)> onRead;
        infra::AutoResetFunction<void(Result result, std::optional<uint32_t> next)> onNext;
    };
}

#endif
