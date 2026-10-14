#include "fs/nor/NorReplay.hpp"
#include "fs/nor/NorVolume.hpp"

namespace fs
{
    NorReplay::NorReplay(NorVolume& volume, const infra::Function<void(Result result)>& onDone)
        : volume(volume)
        , records(volume.Flash(), volume.JournalBuffer())
        , onDone(onDone)
    {}

    NorReplay::~NorReplay() = default;

    void NorReplay::Start()
    {
        index = 0;
        offset = volume.State().checkpoint.recordsOffset;
        linked = false;
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
            onDone(Result::success);
        else if (!record.has_value())
        {
            offset = volume.Geometry().HeaderSize();
            linked = true;
            ReadRecord();
        }
        else
        {
            offset += volume.Geometry().RoundToProgramUnit(static_cast<uint32_t>(record->size));
            volume.Cache().UnpinAll();
            volume.RecordApply().Apply(*record, linked, [this](Result result)
                {
                    Applied(result);
                });
        }
    }

    void NorReplay::Applied(Result result)
    {
        if (result != Result::success)
        {
            onDone(result);
            return;
        }

        linked = false;
        ReadRecord();
    }
}
