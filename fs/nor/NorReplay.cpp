#include "fs/nor/NorReplay.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/NorVolume.hpp"
#include <variant>

namespace fs
{
    NorReplay::NorReplay(NorVolume& volume, std::optional<NorRecordPosition> checkpointBefore, const infra::Function<void(Result result, std::optional<NorRecordPosition> overflow)>& onDone)
        : volume(volume)
        , records(volume.Flash(), volume.JournalBuffer())
        , checkpointBefore(checkpointBefore)
        , onDone(onDone)
    {}

    NorReplay::~NorReplay() = default;

    void NorReplay::Start()
    {
        index = 0;
        offset = volume.State().checkpoint.recordsOffset;
        linked = offset == volume.Geometry().HeaderSize();
        applied = false;
        ReadRecord();
    }

    void NorReplay::ReadRecord()
    {
        records.Read(volume.Geometry(), volume.State().epoch[index], offset, [this](const std::optional<OpenedRecord>& record)
            {
                OnRecord(record);
            });
    }

    void NorReplay::OnRecord(const std::optional<OpenedRecord>& record)
    {
        if (!record.has_value() && ++index == volume.State().epoch.size())
            onDone(Result::success, std::nullopt);
        else if (!record.has_value())
        {
            offset = volume.Geometry().HeaderSize();
            linked = true;
            ReadRecord();
        }
        else if (checkpointBefore == NorRecordPosition{ volume.State().epoch[index], offset })
        {
            laterIndex = index;
            laterOffset = offset;
            HoldBlocksOf(record);
        }
        else
            Apply(*record);
    }

    void NorReplay::ReadLaterRecord()
    {
        records.Read(volume.Geometry(), volume.State().epoch[laterIndex], laterOffset, [this](const std::optional<OpenedRecord>& record)
            {
                HoldBlocksOf(record);
            });
    }

    void NorReplay::HoldBlocksOf(const std::optional<OpenedRecord>& record)
    {
        auto& state = volume.State();
        if (!record.has_value() && ++laterIndex == state.epoch.size())
        {
            WriteCheckpoint();
            return;
        }

        if (!record.has_value())
            laterOffset = volume.Geometry().HeaderSize();
        else
        {
            laterOffset += volume.Geometry().RoundToProgramUnit(static_cast<uint32_t>(record->size));
            NorEntryReader entries{ *record, NorInodeRecordBytes(volume.Config()) };
            for (auto entry = entries.Next(); entry.has_value(); entry = entries.Next())
                if (const auto* set = std::get_if<NorBlockSet>(&*entry); set != nullptr && set->block < volume.Geometry().blocks && state.blocks.State(set->block) != NorBlockState::inUse)
                    state.blocks.Set(set->block, NorBlockState::pending);
        }

        ReadLaterRecord();
    }

    void NorReplay::WriteCheckpoint()
    {
        auto records = checkpointBefore.value_or(NorRecordPosition{});
        checkpointBefore = std::nullopt;
        volume.Checkpoints().WriteBefore(records, [this](Result result)
            {
                if (result != Result::success)
                    onDone(result, std::nullopt);
                else
                {
                    index = 0;
                    applied = false;
                    ReadRecord();
                }
            });
    }

    void NorReplay::Apply(const OpenedRecord& record)
    {
        current = NorRecordPosition{ volume.State().epoch[index], offset };
        offset += volume.Geometry().RoundToProgramUnit(static_cast<uint32_t>(record.size));
        volume.Cache().UnpinAll();
        volume.RecordApply().Apply(record, linked, [this](Result result)
            {
                Applied(result);
            });
    }

    void NorReplay::Applied(Result result)
    {
        if (result == Result::unsupported && applied)
            onDone(result, current);
        else if (result != Result::success)
            onDone(result, std::nullopt);
        else
        {
            linked = false;
            applied = true;
            ReadRecord();
        }
    }
}
