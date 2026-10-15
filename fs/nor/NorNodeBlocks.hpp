#ifndef FS_NOR_NOR_NODE_BLOCKS_HPP
#define FS_NOR_NOR_NODE_BLOCKS_HPP

#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    constexpr std::size_t NorNodeBlockBytes(uint32_t blocks)
    {
        return (std::size_t{ blocks } + 7) / 8;
    }

    class NorNodeBlocks
    {
    public:
        explicit NorNodeBlocks(infra::ByteRange storage);
        NorNodeBlocks(const NorNodeBlocks& other) = delete;
        NorNodeBlocks& operator=(const NorNodeBlocks& other) = delete;
        ~NorNodeBlocks() = default;

        void Reset(uint32_t blocks);
        bool Contains(uint32_t block) const;
        void Add(uint32_t block);
        void Remove(uint32_t block);
        uint32_t Count() const;

    private:
        infra::ByteRange storage;
        uint32_t blocks{ 0 };
        uint32_t count{ 0 };
    };
}

#endif
