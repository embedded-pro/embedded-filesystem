#ifndef FS_FAT_LITE_FAT_LITE_ALLOCATOR_HPP
#define FS_FAT_LITE_FAT_LITE_ALLOCATOR_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatLiteVolume;

    class FatLiteAllocator
    {
    public:
        explicit FatLiteAllocator(FatLiteVolume& volume);
        FatLiteAllocator(const FatLiteAllocator& other) = delete;
        FatLiteAllocator& operator=(const FatLiteAllocator& other) = delete;
        ~FatLiteAllocator();

        void Reset();
        void Take(const infra::Function<void(Result result, uint32_t cluster)>& onDone);
        void Free(uint32_t first, const infra::Function<void(Result result)>& onDone);
        uint32_t Cursor() const;
        bool TableChanged() const;

    private:
        void ReadSector();
        void Search(Result result, infra::ConstByteRange contents);
        void Advance();
        void Taken();
        void FreeNext();
        void Freed(Result result);
        uint32_t SectorOf(uint32_t cluster) const;

    private:
        FatLiteVolume& volume;
        uint32_t cursor{ 2 };
        uint32_t candidate{ 0 };
        uint32_t searched{ 0 };
        std::optional<uint32_t> next;
        bool tableChanged{ false };
        infra::AutoResetFunction<void(Result result, uint32_t cluster)> onTaken;
        infra::AutoResetFunction<void(Result result)> onFreed;
    };
}

#endif
