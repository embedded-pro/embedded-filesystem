#include "fs/nor/NorReplay.hpp"
#include "fs/nor/NorVolume.hpp"
#include <utility>

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
            WriteCheckpoint();
        else
            Apply(*record);
    }

    void NorReplay::WriteCheckpoint()
    {
        checkpointed = std::exchange(checkpointBefore, std::nullopt);
        volume.Checkpoints().WriteBefore(checkpointed.value_or(NorRecordPosition{}), [this](Result result)
            {
                if (result != Result::success)
                    onDone(result, std::nullopt);
                else
                {
                    index = 0;
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
        if (result == Result::unsupported && checkpointed != current)
            onDone(result, current);
        else if (result != Result::success)
            onDone(result, std::nullopt);
        else
        {
            linked = false;
            ReadRecord();
        }
    }
}
