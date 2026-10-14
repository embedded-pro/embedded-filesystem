#include "fs/nor/NorJournalScan.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorRecord.hpp"
#include <variant>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
    }

    NorJournalScan::NorJournalScan(hal::Flash& flash, NorVolumeState& volume, infra::ByteRange buffer, uint32_t inodeRecordBytes, const infra::Function<void(Result result)>& onDone)
        : flash(flash)
        , volume(volume)
        , buffer(buffer)
        , inodeRecordBytes(inodeRecordBytes)
        , records(flash, buffer)
        , onDone(onDone)
    {}

    void NorJournalScan::Start()
    {
        block = volume.checkpoint.recordsBlock;
        end = volume.checkpoint.recordsOffset;
        epochBlocks = 1;
        ReadHeader(block, [this]
            {
                OnFirstHeader();
            });
    }

    void NorJournalScan::OnFirstHeader()
    {
        auto allocation = AllocationInBuffer();
        if (allocation.type != NorBlockType::checkpoint && allocation.type != NorBlockType::journal)
        {
            onDone(Result::corrupted);
            return;
        }

        sequence = allocation.sequence;
        ReadRecord();
    }

    void NorJournalScan::ReadRecord()
    {
        records.Read(*volume.geometry, block, end, [this](const std::optional<OpenedRecord>& record)
            {
                OnRecord(record);
            });
    }

    void NorJournalScan::OnRecord(const std::optional<OpenedRecord>& record)
    {
        if (!record.has_value())
        {
            FindNextBlock();
            return;
        }

        end += volume.geometry->RoundToProgramUnit(static_cast<uint32_t>(record->size));
        ReadRecord();
    }

    void NorJournalScan::FindNextBlock()
    {
        next = std::nullopt;
        candidate = superblocks;
        ReadNextCandidate();
    }

    void NorJournalScan::ReadNextCandidate()
    {
        const auto& geometry = *volume.geometry;
        while (candidate != geometry.blocks && volume.blocks.State(candidate) != NorBlockState::pending)
            ++candidate;

        if (candidate == geometry.blocks && next.has_value())
            Follow(*next);
        else if (candidate == geometry.blocks)
            Finish();
        else
            ReadHeader(candidate, [this]
                {
                    OnCandidateHeader();
                });
    }

    void NorJournalScan::OnCandidateHeader()
    {
        auto allocation = AllocationInBuffer();
        if (allocation.type == NorBlockType::journal && allocation.sequence > sequence && (!next.has_value() || allocation.sequence < nextSequence))
        {
            candidateSequence = allocation.sequence;
            records.Read(*volume.geometry, candidate, volume.geometry->HeaderSize(), [this](const std::optional<OpenedRecord>& record)
                {
                    OnCandidateRecord(record);
                });
            return;
        }

        ++candidate;
        ReadNextCandidate();
    }

    void NorJournalScan::OnCandidateRecord(const std::optional<OpenedRecord>& record)
    {
        if (record.has_value() && LinksToEnd(*record))
        {
            next = candidate;
            nextSequence = candidateSequence;
        }

        ++candidate;
        ReadNextCandidate();
    }

    bool NorJournalScan::LinksToEnd(const OpenedRecord& record) const
    {
        NorEntryReader reader{ record, inodeRecordBytes };
        auto first = reader.Next();
        const auto* link = first.has_value() ? std::get_if<NorLink>(&*first) : nullptr;

        return link != nullptr && link->previousBlock == block && link->end == end;
    }

    void NorJournalScan::Follow(uint32_t following)
    {
        volume.blocks.Set(following, NorBlockState::inUse);
        block = following;
        sequence = nextSequence;
        end = volume.geometry->HeaderSize();
        ++epochBlocks;
        ReadRecord();
    }

    void NorJournalScan::Finish()
    {
        for (uint32_t index = superblocks; index != volume.geometry->blocks; ++index)
            if (volume.blocks.State(index) == NorBlockState::pending)
                volume.blocks.Set(index, NorBlockState::freeNotErased);

        volume.journal = NorJournal{ block, end, epochBlocks };
        onDone(Result::success);
    }

    void NorJournalScan::ReadHeader(uint32_t headerBlock, const infra::Function<void()>& onRead)
    {
        const auto& geometry = *volume.geometry;
        onHeader = onRead;
        flash.ReadBuffer(infra::Head(buffer, geometry.HeaderSize()), geometry.AddressOfBlock(headerBlock), [this]
            {
                onHeader();
            });
    }

    NorAllocation NorJournalScan::AllocationInBuffer() const
    {
        const auto& geometry = *volume.geometry;
        return ParseNorBlockHeader(infra::Head(buffer, geometry.HeaderSize()), geometry.programUnit).allocation.value_or(NorAllocation{});
    }
}
