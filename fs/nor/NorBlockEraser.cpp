#include "fs/nor/NorBlockEraser.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
    }

    NorBlockEraser::NorBlockEraser(NorVolume& volume)
        : volume(volume)
    {}

    NorBlockEraser::~NorBlockEraser() = default;

    bool NorBlockEraser::HasWork() const
    {
        return block.has_value() || volume.State().blocks.Count(NorBlockState::freeNotErased) != 0;
    }

    void NorBlockEraser::Step(const infra::Function<void()>& onDone)
    {
        Step(NorWear::least, onDone);
    }

    void NorBlockEraser::Step(NorWear wear, const infra::Function<void()>& onDone)
    {
        onStepped = onDone;
        if (!block.has_value())
        {
            const auto& state = volume.State();
            block = WornBlock(state.blocks, state.eraseCounts, NorBlockState::freeNotErased, wear);
            really_assert(block.has_value());
            sector = volume.Geometry().FirstSectorOfBlock(block.value_or(0));
        }

        volume.Flash().EraseSectors(sector, sector + 1, [this]
            {
                ++volume.Counters().erasedSectors;
                ++sector;
                if (sector == volume.Geometry().FirstSectorOfBlock(block.value_or(0) + 1))
                    ProgramPartA();
                else
                    onStepped();
            });
    }

    void NorBlockEraser::Refill(const infra::Function<void()>& onDone)
    {
        auto call = volume.Queue().ForegroundReleases();
        if (volume.State().blocks.Count(NorBlockState::freeErased) < volume.Geometry().WatermarkBlocks() && HasWork() && refilledCall != call)
        {
            refilledCall = call;
            Step(onDone);
        }
        else
            onDone();
    }

    void NorBlockEraser::CountAsRefill()
    {
        refilledCall = volume.Queue().ForegroundReleases();
    }

    void NorBlockEraser::Reset()
    {
        block = std::nullopt;
    }

    void NorBlockEraser::ProgramPartA()
    {
        auto erased = block.value_or(0);
        auto count = volume.State().eraseCounts.Count(erased) + 1;
        auto partA = infra::Head(volume.JournalBuffer(), volume.Geometry().RoundToProgramUnit(norHeaderPartABytes));
        std::ranges::fill(partA, uint8_t{ 0xff });
        SerializeNorHeaderA(count, partA);
        volume.Flash().WriteBuffer(partA, volume.Geometry().AddressOfBlock(erased), [this]
            {
                auto& state = volume.State();
                auto programmed = block.value_or(0);
                state.eraseCounts.Raise(programmed, superblocks);
                state.blocks.Set(programmed, NorBlockState::freeErased);
                block = std::nullopt;
                onStepped();
            });
    }
}
