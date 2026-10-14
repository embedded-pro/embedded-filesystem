#ifndef FS_NOR_NOR_ERASE_COUNTS_HPP
#define FS_NOR_NOR_ERASE_COUNTS_HPP

#include "infra/util/ByteRange.hpp"
#include <cstdint>

namespace fs
{
    class NorEraseCounts
    {
    public:
        static constexpr uint32_t headroom = 64;

        explicit NorEraseCounts(infra::ByteRange storage);
        NorEraseCounts(const NorEraseCounts& other) = delete;
        NorEraseCounts& operator=(const NorEraseCounts& other) = delete;
        ~NorEraseCounts() = default;

        void Reset(uint32_t blocks);
        void Record(uint32_t block, uint32_t count);
        uint32_t Count(uint32_t block) const;
        uint32_t Base() const;
        void Normalize(uint32_t firstBlock);

    private:
        void MoveBaseDown(uint32_t newBase);

    private:
        infra::ByteRange storage;
        uint32_t blocks{ 0 };
        uint32_t base{ 0 };
        bool based{ false };
    };
}

#endif
