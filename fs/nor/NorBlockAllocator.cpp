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

    void NorBlockAllocator::Allocate(NorBlockType allocationType, const NorBlockOwner& allocationOwner, const infra::Function<void(std::optional<uint32_t> block)>& onAllocated)
    {
        type = allocationType;
        owner = allocationOwner;
        onDone = onAllocated;

        auto erased = LeastWorn(NorBlockState::freeErased);
        auto notErased = LeastWorn(NorBlockState::freeNotErased);
        if (erased.has_value())
        {
            block = *erased;
            ProgramPartB();
        }
        else if (notErased.has_value())
        {
            block = *notErased;
            sector = volume.Geometry().FirstSectorOfBlock(block);
            EraseNextSector();
        }
        else
            onDone(std::nullopt);
    }

    std::optional<uint32_t> NorBlockAllocator::LeastWorn(NorBlockState state) const
    {
        const auto& volumeState = volume.State();
        std::optional<uint32_t> leastWorn;

        for (uint32_t candidate = superblocks; candidate != volume.Geometry().blocks; ++candidate)
            if (volumeState.blocks.State(candidate) == state && (!leastWorn.has_value() || volumeState.eraseCounts.Count(candidate) < volumeState.eraseCounts.Count(*leastWorn)))
                leastWorn = candidate;

        return leastWorn;
    }

    void NorBlockAllocator::EraseNextSector()
    {
        if (sector == volume.Geometry().FirstSectorOfBlock(block + 1))
        {
            ProgramPartA();
            return;
        }

        volume.Flash().EraseSectors(sector, sector + 1, [this]
            {
                ++sector;
                EraseNextSector();
            });
    }

    void NorBlockAllocator::ProgramPartA()
    {
        auto& state = volume.State();
        auto count = state.eraseCounts.Count(block) + 1;
        auto partA = Rounded(norHeaderPartABytes);
        SerializeNorHeaderA(count, partA);
        Program(partA, volume.Geometry().AddressOfBlock(block), [this, count]
            {
                auto& programmed = volume.State();
                programmed.eraseCounts.Record(block, count);
                programmed.blocks.Set(block, NorBlockState::freeErased);
                ProgramPartB();
            });
    }

    void NorBlockAllocator::ProgramPartB()
    {
        auto partB = Rounded(norHeaderPartBBytes);
        SerializeNorHeaderB(NorAllocation{ type, volume.State().nextSequence++, owner }, partB);
        Program(partB, volume.Geometry().AddressOfBlock(block) + volume.Geometry().HeaderPartBOffset(), [this]
            {
                volume.State().blocks.Set(block, NorBlockState::pending);
                onDone(block);
            });
    }

    void NorBlockAllocator::Program(infra::ConstByteRange bytes, uint32_t address, const infra::Function<void()>& onProgrammedDone)
    {
        onProgrammed = onProgrammedDone;
        volume.Flash().WriteBuffer(bytes, address, [this]
            {
                onProgrammed();
            });
    }

    infra::ByteRange NorBlockAllocator::Rounded(uint32_t size) const
    {
        auto bytes = infra::Head(volume.JournalBuffer(), volume.Geometry().RoundToProgramUnit(size));
        std::ranges::fill(bytes, uint8_t{ 0xff });
        return bytes;
    }
}
