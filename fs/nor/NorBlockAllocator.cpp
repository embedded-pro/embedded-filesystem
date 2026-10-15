#include "fs/nor/NorBlockAllocator.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorVolume.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
    }

    NorBlockAllocator::NorBlockAllocator(NorVolume& volume)
        : volume(volume)
    {}

    NorBlockAllocator::~NorBlockAllocator() = default;

    void NorBlockAllocator::Allocate(NorBlockType allocationType, const NorBlockOwner& allocationOwner, NorReserve reserve, const infra::Function<void(std::optional<uint32_t> block)>& onAllocated)
    {
        type = allocationType;
        owner = allocationOwner;
        onDone = onAllocated;

        const auto& blocks = volume.State().blocks;
        if (reserve == NorReserve::kept && blocks.Count(NorBlockState::freeErased) + blocks.Count(NorBlockState::freeNotErased) <= volume.Geometry().ReserveBlocks())
            onDone(std::nullopt);
        else
            TakeErased();
    }

    std::optional<uint32_t> NorBlockAllocator::LeastWornErased() const
    {
        const auto& state = volume.State();
        std::optional<uint32_t> leastWorn;

        for (uint32_t candidate = superblocks; candidate != volume.Geometry().blocks; ++candidate)
            if (state.blocks.State(candidate) == NorBlockState::freeErased && (!leastWorn.has_value() || state.eraseCounts.Count(candidate) < state.eraseCounts.Count(*leastWorn)))
                leastWorn = candidate;

        return leastWorn;
    }

    void NorBlockAllocator::TakeErased()
    {
        auto erased = LeastWornErased();
        auto& eraser = volume.Eraser();
        if (erased.has_value())
        {
            block = *erased;
            ProgramPartB();
        }
        else if (eraser.HasWork())
        {
            eraser.CountAsRefill();
            eraser.Step([this]
                {
                    TakeErased();
                });
        }
        else
            onDone(std::nullopt);
    }

    void NorBlockAllocator::ProgramPartB()
    {
        const auto& geometry = volume.Geometry();
        auto partB = infra::Head(volume.JournalBuffer(), geometry.RoundToProgramUnit(norHeaderPartBBytes));
        std::ranges::fill(partB, uint8_t{ 0xff });
        SerializeNorHeaderB(NorAllocation{ type, volume.State().nextSequence++, owner }, partB);
        volume.Flash().WriteBuffer(partB, geometry.AddressOfBlock(block) + geometry.HeaderPartBOffset(), [this]
            {
                volume.State().blocks.Set(block, NorBlockState::pending);
                volume.Eraser().Refill([this]
                    {
                        onDone(block);
                    });
            });
    }
}
