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
        , lifecycle(*this, services::Validated<NorLite>())
    {
        volume.AttachBackground(leveling);
        lifecycle.OnEntered<NorMounted>([](NorLite& lite, NorMounted&)
            {
                lite.volume.MarkMounted();
                lite.Entered(Result::success);
            });
        lifecycle.OnEntered<NorFault>([](NorLite& lite, const NorFault& fault)
            {
                lite.volume.MarkUnmounted();
                lite.Entered(fault.result);
            });
        lifecycle.OnEntered<NorUnmounted>([](NorLite& lite, NorUnmounted&)
            {
                lite.volume.MarkUnmounted();
                lite.Entered(lite.lifecycleResult);
            });
        lifecycle.Start<NorUnmounted>();
    }

    NorLite::~NorLite() = default;

    NorCounters NorLite::Counters() const
    {
        return volume.Counters();
    }

    void NorLite::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(NorMountRequested{}) == services::DispatchResult::transitioned)
            onMountDone = onDone;
        else
            volume.Defer(onDone, Result::busy);
    }

    void NorLite::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(NorUnmountRequested{}) == services::DispatchResult::transitioned)
            onUnmountDone = onDone;
        else if (std::holds_alternative<NorUnmounted>(lifecycle.CurrentState()) || std::holds_alternative<NorFault>(lifecycle.CurrentState()))
            volume.Defer(onDone, Result::notMounted);
        else
            volume.Defer(onDone, Result::busy);
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

    NorVolume& NorLite::Volume()
    {
        return volume;
    }

    bool NorLite::IsMounted() const
    {
        return std::holds_alternative<NorMounted>(lifecycle.CurrentState());
    }

    Result NorLite::Unsupported() const
    {
        return IsMounted() ? Result::unsupported : Result::notMounted;
    }

    Result NorLite::Refusal(infra::BoundedConstString path) const
    {
        if (!IsMounted())
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

    void NorLite::BeginMount()
    {
        lifecycleClaimer.Claim([this]
            {
                StartMount();
            });
    }

    void NorLite::Mounted(Result result)
    {
        lifecycleClaimer.Release();
        lifecycle.Dispatch(NorMountDone{ result });
    }

    void NorLite::BeginUnmount()
    {
        lifecycleClaimer.Claim([this]
            {
                volume.Operation().emplace<NorUnmount>(volume, [this](Result result)
                                      {
                                          lifecycleClaimer.Release();
                                          lifecycle.Dispatch(NorUnmountDone{ result });
                                      })
                    .Start();
            });
    }

    void NorLite::Entered(Result result)
    {
        if (onMountDone)
            volume.Defer(std::exchange(onMountDone, nullptr), result);
        if (onUnmountDone)
            volume.Defer(std::exchange(onUnmountDone, nullptr), result);
    }
}
