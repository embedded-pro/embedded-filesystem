#include "fs/nor/NorDataAllocator.hpp"
#include "fs/nor/NorVolume.hpp"
#include <utility>

namespace fs
{
    NorDataAllocator::NorDataAllocator(NorVolume& volume)
        : volume(volume)
    {}

    NorDataAllocator::~NorDataAllocator() = default;

    void NorDataAllocator::Allocate(const NorBlockOwner& allocationOwner, NorReserve allocationReserve, const infra::Function<void(std::optional<uint32_t> block)>& onAllocated)
    {
        owner = allocationOwner;
        reserve = allocationReserve;
        collected = false;
        onDone = onAllocated;
        TryAllocate();
    }

    void NorDataAllocator::TryAllocate()
    {
        volume.Allocator().Allocate(NorBlockType::data, owner, reserve, [this](std::optional<uint32_t> block)
            {
                Allocated(block);
            });
    }

    void NorDataAllocator::Allocated(std::optional<uint32_t> block)
    {
        if (block.has_value() || std::exchange(collected, true) || !volume.Collector().DueForSpace())
            onDone(block);
        else
            volume.Checkpoints().Write([this](Result result)
                {
                    if (result == Result::success)
                        TryAllocate();
                    else
                        onDone(std::nullopt);
                });
    }
}
