#include "fs/nor/NorBlockStates.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t bitsPerState = 2;
        constexpr uint32_t statesPerByte = 4;
        constexpr uint8_t stateMask = 3;

        uint32_t ShiftOf(uint32_t block)
        {
            return bitsPerState * (block % statesPerByte);
        }
    }

    NorBlockStates::NorBlockStates(infra::ByteRange storage)
        : storage(storage)
    {}

    void NorBlockStates::Reset(uint32_t blockCount)
    {
        really_assert(NorBlockStateBytes(blockCount) <= storage.size());

        blocks = blockCount;
        std::ranges::fill(infra::Head(storage, NorBlockStateBytes(blockCount)), uint8_t{ 0 });
    }

    uint32_t NorBlockStates::Blocks() const
    {
        return blocks;
    }

    NorBlockState NorBlockStates::State(uint32_t block) const
    {
        really_assert(block < blocks);

        return static_cast<NorBlockState>((storage[block / statesPerByte] >> ShiftOf(block)) & stateMask);
    }

    void NorBlockStates::Set(uint32_t block, NorBlockState state) const
    {
        really_assert(block < blocks);

        auto& byte = storage[block / statesPerByte];
        byte = static_cast<uint8_t>((byte & ~(stateMask << ShiftOf(block))) | static_cast<uint8_t>(state) << ShiftOf(block));
    }

    uint32_t NorBlockStates::Count(NorBlockState state) const
    {
        uint32_t count = 0;
        for (uint32_t block = 0; block != blocks; ++block)
            if (State(block) == state)
                ++count;

        return count;
    }
}
