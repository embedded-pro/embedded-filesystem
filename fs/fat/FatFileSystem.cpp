#include "fs/fat/FatFileSystem.hpp"
#include <utility>

namespace fs
{
    FatFileSystem::FatFileSystem(const FatVolumeParts& parts, const FatConfig& config)
        : volume(parts, config, [this]
              {
                  lifecycle.Dispatch(FatMediaRemoved{});
              })
        , lifecycleClaimer(volume.Queue())
        , lifecycle(*this, services::Validated<FatFileSystem>())
    {
        lifecycle.OnEntered<FatMounted>([](FatFileSystem& fileSystem, FatMounted&)
            {
                fileSystem.volume.MarkMounted();
                fileSystem.Entered(Result::success);
            });
        lifecycle.OnEntered<FatFault>([](FatFileSystem& fileSystem, FatFault& fault)
            {
                fileSystem.volume.MarkUnmounted(fault.result);
                fileSystem.Entered(fault.result);
            });
        lifecycle.OnEntered<FatUnmounted>([](FatFileSystem& fileSystem, FatUnmounted&)
            {
                fileSystem.volume.MarkUnmounted(Result::notMounted);
                fileSystem.Entered(fileSystem.lifecycleResult);
            });
        lifecycle.Start<FatUnmounted>();
    }

    FatFileSystem::~FatFileSystem() = default;

    void FatFileSystem::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatMountRequested{ onDone }) != services::DispatchResult::transitioned)
            volume.Defer(onDone, Result::busy);
    }

    void FatFileSystem::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatUnmountRequested{ onDone }) == services::DispatchResult::transitioned)
            return;

        if (std::holds_alternative<FatMounting>(lifecycle.CurrentState()) || std::holds_alternative<FatUnmounting>(lifecycle.CurrentState()))
            volume.Defer(onDone, Result::busy);
        else
            volume.Defer(onDone, Unavailable());
    }

    void FatFileSystem::Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        StartPathCall(path, FatOpenCall{ path, mode, onDone });
    }

    void FatFileSystem::OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        StartPathCall(path, FatOpenDirectoryCall{ path, onDone });
    }

    void FatFileSystem::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        StartPathCall(path, FatStatCall{ path, onDone });
    }

    void FatFileSystem::Remove(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    void FatFileSystem::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    void FatFileSystem::MakeDirectory(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        volume.Defer(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    void FatFileSystem::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        volume.Defer(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    FatVolume& FatFileSystem::Volume()
    {
        return volume;
    }

    void FatFileSystem::AttachPathSlots(infra::MemoryRange<FatPathSlot> slots)
    {
        pathSlots = slots;
    }

    Result FatFileSystem::Unavailable() const
    {
        if (const auto* fault = std::get_if<FatFault>(&lifecycle.CurrentState()); fault != nullptr && fault->result == Result::mediaRemoved)
            return Result::mediaRemoved;

        return Result::notMounted;
    }

    bool FatFileSystem::IsMounted() const
    {
        return std::holds_alternative<FatMounted>(lifecycle.CurrentState());
    }

    void FatFileSystem::BeginMount(const FatMountRequested& request)
    {
        onMountDone = request.onDone;
        lifecycleClaimer.Claim([this]
            {
                StartMount();
            });
    }

    void FatFileSystem::StartMount()
    {
        lifecycleDone = lifecycle.CompletionWith<void(Result result)>([](Result result)
            {
                return FatMountDone{ result };
            });
        volume.Operation().emplace<FatMount>(volume.Device(), volume.Cache(), volume.State(), [this](Result result)
                              {
                                  LifecycleOperationDone(result);
                              })
            .Start();
    }

    void FatFileSystem::BeginUnmount(const FatUnmountRequested& request)
    {
        onUnmountDone = request.onDone;
        lifecycleClaimer.Claim([this]
            {
                StartUnmount();
            });
    }

    void FatFileSystem::StartUnmount()
    {
        lifecycleDone = lifecycle.CompletionWith<void(Result result)>([](Result result)
            {
                return FatUnmountDone{ result };
            });
        volume.Cache().Flush([this](Result result)
            {
                volume.Cache().Clear();
                volume.Cache().SetMirroredRegion(std::nullopt);
                LifecycleOperationDone(result);
            });
    }

    void FatFileSystem::LifecycleOperationDone(Result result)
    {
        lifecycleClaimer.Release();
        auto done = std::exchange(lifecycleDone, nullptr);
        done(result);
    }

    void FatFileSystem::Entered(Result result)
    {
        if (onMountDone)
            onMountDone(result);

        if (onUnmountDone)
            onUnmountDone(result);
    }

    template<class Call>
    void FatFileSystem::StartPathCall(infra::BoundedConstString path, Call&& call)
    {
        if (!IsMounted())
            volume.Defer(call.onDone, Unavailable());
        else if (auto validation = ValidatePath(path, volume.Limits()); validation != Result::success)
            volume.Defer(call.onDone, validation);
        else if (auto slot = FreePathSlot())
        {
            slot->call = std::forward<Call>(call);
            slot->claimer.Claim([this, slot]
                {
                    RunPathCall(*slot);
                });
        }
        else
            volume.Defer(call.onDone, Result::busy);
    }

    FatPathSlot* FatFileSystem::FreePathSlot()
    {
        for (auto& slot : pathSlots)
            if (!slot.InUse())
                return &slot;

        return nullptr;
    }

    void FatFileSystem::RunPathCall(FatPathSlot& slot)
    {
        auto call = std::move(*slot.call);
        slot.call = std::nullopt;

        if (volume.Available())
            RunOperation(slot, call);
        else
        {
            slot.claimer.Release();
            std::visit([this](const auto& pending)
                {
                    detail::Deliver(pending.onDone, Unavailable());
                },
                call);
        }
    }

    void FatFileSystem::RunOperation(FatPathSlot& slot, FatPathCall& call)
    {
        auto finished = [this, &slot](Result result)
        {
            slot.claimer.Release();
            volume.Report(result);
        };

        auto allocateDirectory = [this]
        {
            return AllocateDirectory();
        };

        auto allocateFile = [this]
        {
            return AllocateFile();
        };

        if (auto* stat = std::get_if<FatStatCall>(&call))
            volume.Operation().emplace<FatStat>(volume, *stat, finished).Start();
        else if (auto* openDirectory = std::get_if<FatOpenDirectoryCall>(&call))
            volume.Operation().emplace<FatOpenDirectory>(volume, *openDirectory, allocateDirectory, finished).Start();
        else
            volume.Operation().emplace<FatOpen>(volume, std::get<FatOpenCall>(call), allocateFile, finished).Start();
    }
}
