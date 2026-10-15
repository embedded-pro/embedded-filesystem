#include "fs/nor/NorHeaderScan.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;

        bool IsCheckpointOrJournal(const NorAllocation& allocation)
        {
            return allocation.type == NorBlockType::checkpoint || allocation.type == NorBlockType::journal;
        }
    }

    uint32_t NorScanSummary::AverageCount() const
    {
        if (countsKnown == 0)
            return 0;

        return static_cast<uint32_t>(countSum / countsKnown);
    }

    NorHeaderScan::NorHeaderScan(hal::Flash& flash, NorVolumeState& volume, infra::ByteRange buffer, NorScanSummary& summary, const infra::Function<void()>& onDone)
        : flash(flash)
        , volume(volume)
        , buffer(buffer)
        , summary(summary)
        , onDone(onDone)
    {}

    void NorHeaderScan::Start()
    {
        summary = NorScanSummary{};
        block = superblocks;
        ReadHeader();
    }

    void NorHeaderScan::ReadHeader()
    {
        const auto& geometry = *volume.geometry;
        flash.ReadBuffer(infra::Head(buffer, geometry.HeaderSize()), geometry.AddressOfBlock(block), [this]
            {
                OnHeader();
            });
    }

    void NorHeaderScan::OnHeader()
    {
        const auto& geometry = *volume.geometry;
        auto header = ParseNorBlockHeader(infra::Head(buffer, geometry.HeaderSize()), geometry.programUnit);
        Count(header);
        Classify(header);

        if (++block != geometry.blocks)
            ReadHeader();
        else
            onDone();
    }

    void NorHeaderScan::Count(const NorBlockHeader& header) const
    {
        if (!header.eraseCount.has_value())
            return;

        volume.eraseCounts.Record(block, *header.eraseCount);
        summary.countSum += *header.eraseCount;
        ++summary.countsKnown;
    }

    void NorHeaderScan::Classify(const NorBlockHeader& header) const
    {
        if (header.allocation.has_value())
            summary.highestSequence = std::max(summary.highestSequence, header.allocation->sequence);

        if (!header.eraseCount.has_value())
            volume.blocks.Set(block, norUnknownEraseCount);
        else if (header.allocation.has_value() && IsCheckpointOrJournal(*header.allocation))
        {
            volume.blocks.Set(block, NorBlockState::pending);

            if (header.allocation->type == NorBlockType::checkpoint && (!summary.checkpointCandidate.has_value() || header.allocation->sequence > summary.checkpointSequence))
            {
                summary.checkpointCandidate = block;
                summary.checkpointSequence = header.allocation->sequence;
            }
        }
        else if (header.IsFreeAndErased())
            volume.blocks.Set(block, NorBlockState::freeErased);
        else if (header.allocation.has_value() && header.allocation->type == NorBlockType::node)
            volume.nodeBlocks.Add(block);
    }
}
