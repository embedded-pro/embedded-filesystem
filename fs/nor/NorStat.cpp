#include "fs/nor/NorStat.hpp"
#include "fs/nor/NorVolume.hpp"

namespace fs
{
    NorStat::NorStat(NorVolume& volume, NorStatCall&& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , onFinished(onFinished)
    {}

    NorStat::~NorStat() = default;

    void NorStat::Start()
    {
        volume.Walk().Find(volume.State().inodeTable, call.path, [this](Result result)
            {
                OnFound(result);
            });
    }

    void NorStat::OnFound(Result result)
    {
        onFinished(result);

        const auto& found = volume.Walk().Found();
        if (result != Result::success)
            call.onDone(result, EntryInfo{});
        else if (found.type == NorInodeType::directory)
            call.onDone(result, EntryInfo{ volume.Walk().FoundName(), EntryType::directory, 0 });
        else
            call.onDone(result, EntryInfo{ volume.Walk().FoundName(), EntryType::file, found.size });
    }
}
