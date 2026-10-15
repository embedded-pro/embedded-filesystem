#ifndef FS_NOR_NOR_WEAR_HPP
#define FS_NOR_NOR_WEAR_HPP

#include "fs/nor/NorBlockStates.hpp"
#include "fs/nor/NorEraseCounts.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    enum class NorWear : uint8_t
    {
        least,
        most,
        mostFree
    };

    std::optional<uint32_t> WornBlock(const NorBlockStates& blocks, const NorEraseCounts& counts, NorBlockState state, NorWear wear);
}

#endif
