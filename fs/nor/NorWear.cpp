#include "fs/nor/NorWear.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
    }

    std::optional<uint32_t> WornBlock(const NorBlockStates& blocks, const NorEraseCounts& counts, NorBlockState state, NorWear wear)
    {
        std::optional<uint32_t> chosen;

        for (uint32_t candidate = superblocks; candidate < blocks.Blocks(); ++candidate)
        {
            if (blocks.State(candidate) != state)
                continue;

            auto count = counts.Count(candidate);
            auto chosenCount = counts.Count(chosen.value_or(candidate));
            if (!chosen.has_value() || (wear == NorWear::least ? count < chosenCount : count > chosenCount))
                chosen = candidate;
        }

        return chosen;
    }
}
