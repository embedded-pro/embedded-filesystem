#include "fs/nor/NorEraseCounts.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t largestRelativeCount = 255;

        uint8_t Clamped(uint32_t relative)
        {
            return static_cast<uint8_t>(std::min(relative, largestRelativeCount));
        }

        uint32_t BaseBelow(uint32_t count)
        {
            return count - std::min(count, NorEraseCounts::headroom);
        }
    }

    NorEraseCounts::NorEraseCounts(infra::ByteRange storage)
        : storage(storage)
    {}

    void NorEraseCounts::Reset(uint32_t blockCount)
    {
        really_assert(blockCount <= storage.size());

        blocks = blockCount;
        base = 0;
        based = false;
        std::ranges::fill(infra::Head(storage, blockCount), uint8_t{ 0 });
    }

    void NorEraseCounts::Record(uint32_t block, uint32_t count)
    {
        really_assert(block < blocks);

        if (!based)
        {
            base = BaseBelow(count);
            based = true;
        }
        else if (count < base)
            MoveBaseDown(BaseBelow(count));

        storage[block] = Clamped(count - base);
    }

    uint32_t NorEraseCounts::Count(uint32_t block) const
    {
        really_assert(block < blocks);

        return base + storage[block];
    }

    uint32_t NorEraseCounts::Base() const
    {
        return base;
    }

    void NorEraseCounts::Normalize(uint32_t firstBlock)
    {
        auto counts = infra::DiscardHead(infra::Head(storage, blocks), firstBlock);
        if (counts.empty())
            return;

        auto lowest = *std::ranges::min_element(counts);
        for (auto& count : counts)
            count -= lowest;

        base += lowest;
    }

    void NorEraseCounts::MoveBaseDown(uint32_t newBase)
    {
        auto shift = base - newBase;
        for (auto& count : infra::Head(storage, blocks))
            count = Clamped(count + shift);

        base = newBase;
    }
}
