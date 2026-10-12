#include "fs/fat/FatStat.hpp"

namespace fs
{
    FatStat::FatStat(FatVolume& volume, const FatStatCall& call, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(call)
        , onFinished(onFinished)
    {}

    FatStat::~FatStat() = default;

    void FatStat::Start()
    {
        volume.Walk().Find(call.path, [this](Result result)
            {
                OnFound(result);
            });
    }

    void FatStat::OnFound(Result result)
    {
        onFinished(result);

        const auto& walk = volume.Walk();

        if (result != Result::success)
            call.onDone(result, EntryInfo{});
        else if (walk.FoundRoot())
            call.onDone(result, EntryInfo{ infra::BoundedConstString(), EntryType::directory, 0 });
        else
            call.onDone(result, InfoOf(walk.Found()));
    }
}
