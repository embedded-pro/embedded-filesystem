#include "fs/nor/lite/NorLite.hpp"
#include "fs/common/Path.hpp"
#include <utility>

namespace fs
{
    NorLite::NorLite(const NorVolumeParts& parts, const NorConfig& config)
        : volume(parts, config)
        , leveling(volume)
        , lifecycleClaimer(volume.Queue())
        , pathClaimer(volume.Queue())
    {
        volume.AttachBackground(leveling);
    }

    NorLite::~NorLite() = default;

    NorCounters NorLite::Counters() const
    {
        return volume.Counters();
    }

    void NorLite::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (!lifecycle.StartMount())
            volume.Defer(onDone, Result::busy);
        else
            BeginChange(onDone);
    }

    void NorLite::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.StartUnmount())
            BeginChange(onDone);
        else
            volume.Defer(onDone, lifecycle.Changing() ? Result::busy : Result::notMounted);
    }

    void NorLite::Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(NorOpenCall{ path, mode, onDone });
    }

    void NorLite::OpenDirectory(infra::BoundedConstString, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    void NorLite::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(NorStatCall{ path, onDone });
    }

    void NorLite::Remove(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    void NorLite::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    void NorLite::MakeDirectory(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    void NorLite::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        volume.Defer(onDone, Unsupported());
    }

    bool NorLite::Idle() const
    {
        return volume.Idle();
    }

    NorVolume& NorLite::Volume()
    {
        return volume;
    }

    Result NorLite::Unsupported() const
    {
        return lifecycle.IsMounted() ? Result::unsupported : Result::notMounted;
    }

    Result NorLite::Refusal(infra::BoundedConstString path) const
    {
        if (!lifecycle.IsMounted())
            return Result::notMounted;

        if (auto validation = ValidatePath(path, volume.Limits()); validation != Result::success)
            return validation;

        return pathClaimer.IsClaimed() || pathClaimer.IsQueued() ? Result::busy : Result::success;
    }

    void NorLite::Start(NorLitePathCall&& call)
    {
        pathCall.emplace(std::move(call));
        pathClaimer.Claim([this]
            {
                Run();
            });
    }

    void NorLite::Run()
    {
        auto call = std::move(*pathCall);
        pathCall = std::nullopt;

        auto finished = [this](Result)
        {
            pathClaimer.Release();
        };

        if (auto* stat = std::get_if<NorStatCall>(&call))
            volume.Operation().emplace<NorStat>(volume, std::move(*stat), finished).Start();
        else
            volume.Operation().emplace<NorOpen>(volume, std::move(std::get<NorOpenCall>(call)), [this]
                                  {
                                      return AllocateFile();
                                  },
                                  finished)
                .Start();
    }

    void NorLite::BeginChange(const infra::Function<void(Result result)>& onDone)
    {
        onChanged = onDone;
        lifecycleClaimer.Claim([this]
            {
                if (lifecycle.Mounting())
                    StartMount();
                else
                    volume.Operation().emplace<NorUnmount>(volume, [this](Result result)
                                          {
                                              lifecycleClaimer.Release();
                                              lifecycle.Unmounted();
                                              volume.MarkUnmounted();
                                              Entered(result);
                                          })
                        .Start();
            });
    }

    void NorLite::Mounted(Result result)
    {
        lifecycleClaimer.Release();
        lifecycle.Mounted(result);

        if (result == Result::success)
            volume.MarkMounted();
        else
            volume.MarkUnmounted();

        Entered(result);
    }

    void NorLite::Entered(Result result)
    {
        volume.Defer(std::exchange(onChanged, nullptr), result);
    }
}
