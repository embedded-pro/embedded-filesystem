#include "fs/nor/NorBlockAllocator.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorVolume.hpp"
#include <algorithm>

namespace fs
{
    NorBlockAllocator::NorBlockAllocator(NorVolume& volume)
        : volume(volume)
    {}

    NorBlockAllocator::~NorBlockAllocator() = default;

    void NorBlockAllocator::Allocate(NorBlockType allocationType, const NorBlockOwner& allocationOwner, NorReserve reserve, const infra::Function<void(std::optional<uint32_t> block)>& onAllocated)
    {
        Allocate(allocationType, allocationOwner, reserve, NorWear::least, onAllocated);
    }

    void NorBlockAllocator::Allocate(NorBlockType allocationType, const NorBlockOwner& allocationOwner, NorReserve reserve, NorWear allocationWear, const infra::Function<void(std::optional<uint32_t> block)>& onAllocated)
    {
        const auto& blocks = volume.State().blocks;
        type = allocationType;
        owner = allocationOwner;
        wear = allocationWear == NorWear::most && blocks.Count(NorBlockState::freeErased) == 0 ? NorWear::mostFree : allocationWear;
        onDone = onAllocated;

        if (reserve == NorReserve::kept && blocks.Count(NorBlockState::freeErased) + blocks.Count(NorBlockState::freeNotErased) <= volume.Geometry().ReserveBlocks())
            onDone(std::nullopt);
        else
            TakeErased();
    }

    std::optional<uint32_t> NorBlockAllocator::ErasedOfWear() const
    {
        const auto& state = volume.State();
        return WornBlock(state.blocks, state.eraseCounts, NorBlockState::freeErased, wear);
    }

    bool NorBlockAllocator::IsWornEnough(uint32_t erased) const
    {
        const auto& state = volume.State();
        auto notErased = WornBlock(state.blocks, state.eraseCounts, NorBlockState::freeNotErased, NorWear::most);
        return wear != NorWear::mostFree || !notErased.has_value() || state.eraseCounts.Count(erased) >= state.eraseCounts.Count(notErased.value_or(0));
    }

    void NorBlockAllocator::TakeErased()
    {
        auto erased = ErasedOfWear();
        auto& eraser = volume.Eraser();
        if (erased.has_value() && IsWornEnough(erased.value_or(0)))
        {
            block = erased.value_or(0);
            ProgramPartB();
        }
        else if (eraser.HasWork())
        {
            eraser.CountAsRefill();
            eraser.Step(wear, [this]
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
                ++volume.Counters().allocatedBlocks;
                volume.Background().Urge();
                volume.Eraser().Refill([this]
                    {
                        onDone(block);
                    });
            });
    }
}
