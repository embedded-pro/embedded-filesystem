#ifndef FS_NOR_NOR_BLOCK_STATES_HPP
#define FS_NOR_NOR_BLOCK_STATES_HPP

#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    enum class NorBlockState : uint8_t
    {
        freeNotErased,
        freeErased,
        inUse,
        pending
    };

    constexpr std::size_t NorBlockStateBytes(uint32_t blocks)
    {
        return (std::size_t{ blocks } + 3) / 4;
    }

    class NorBlockStates
    {
    public:
        explicit NorBlockStates(infra::ByteRange storage);
        NorBlockStates(const NorBlockStates& other) = delete;
        NorBlockStates& operator=(const NorBlockStates& other) = delete;
        ~NorBlockStates() = default;

        void Reset(uint32_t blocks);
        uint32_t Blocks() const;
        NorBlockState State(uint32_t block) const;
        void Set(uint32_t block, NorBlockState state) const;
        uint32_t Count(NorBlockState state) const;

    private:
        infra::ByteRange storage;
        uint32_t blocks{ 0 };
    };
}

#endif
