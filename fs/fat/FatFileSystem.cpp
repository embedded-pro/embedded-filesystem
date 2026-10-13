#include "fs/fat/FatFileSystem.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <utility>

namespace fs
{
    namespace
    {
        void Deliver(const infra::Function<void(Result result)>& onDone, Result result)
        {
            onDone(result);
        }

        void Deliver(const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone, Result result)
        {
            onDone(result, nullptr);
        }

        void Deliver(const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone, Result result)
        {
            onDone(result, nullptr);
        }

        void Deliver(const infra::Function<void(Result result, const EntryInfo& info)>& onDone, Result result)
        {
            onDone(result, EntryInfo{});
        }

        void Deliver(const infra::Function<void(Result result, VolumeUsage usage)>& onDone, Result result)
        {
            onDone(result, VolumeUsage{});
        }
    }

    FatFileSystem::FatFileSystem(hal::BlockDevice& device, SectorCache& cache, DeferredCompletions& deferred, const FatConfig& config, std::size_t maxNameBytes)
        : device(device)
        , cache(cache)
        , deferred(deferred)
        , config(config)
        , pathLimits{ maxNameBytes, config.maxPathBytes }
        , queue(config.idleDelay, [] {})
        , lifecycleClaimer(queue)
        , lifecycle(*this, services::Validated<FatFileSystem>())
    {
        lifecycle.OnEntered<FatMounted>([](FatFileSystem& volume, FatMounted&)
            {
                volume.Entered(Result::success);
            });
        lifecycle.OnEntered<FatFault>([](FatFileSystem& volume, const FatFault& fault)
            {
                volume.Entered(fault.result);
            });
        lifecycle.OnEntered<FatUnmounted>([](FatFileSystem& volume, FatUnmounted&)
            {
                volume.Entered(volume.lifecycleResult);
            });
        lifecycle.Start<FatUnmounted>();
    }

    FatFileSystem::~FatFileSystem() = default;

    void FatFileSystem::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatMountRequested{}) == services::DispatchResult::transitioned)
            onMountDone = onDone;
        else
            Reject(onDone, Result::busy);
    }

    void FatFileSystem::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatUnmountRequested{}) == services::DispatchResult::transitioned)
            onUnmountDone = onDone;
        else if (std::holds_alternative<FatMounting>(lifecycle.CurrentState()) || std::holds_alternative<FatUnmounting>(lifecycle.CurrentState()))
            Reject(onDone, Result::busy);
        else
            Reject(onDone, Unavailable());
    }

    void FatFileSystem::Open(infra::BoundedConstString, OpenMode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        Reject(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    void FatFileSystem::OpenDirectory(infra::BoundedConstString, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        Reject(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    void FatFileSystem::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        if (!IsMounted())
        {
            Reject(onDone, Unavailable());
            return;
        }

        if (auto validation = ValidatePath(path, pathLimits); validation != Result::success)
        {
            Reject(onDone, validation);
            return;
        }

        if (auto slot = FreePathSlot())
            StartPathCall(*slot, FatStatCall{ path, onDone });
        else
            Reject(onDone, Result::busy);
    }

    void FatFileSystem::Remove(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        Reject(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    void FatFileSystem::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        Reject(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    void FatFileSystem::MakeDirectory(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        Reject(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    void FatFileSystem::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        Reject(onDone, IsMounted() ? Result::unsupported : Unavailable());
    }

    OperationQueue& FatFileSystem::Queue()
    {
        return queue;
    }

    void FatFileSystem::AttachPathSlots(infra::MemoryRange<FatPathSlot> slots)
    {
        pathSlots = slots;
    }

    template<class Callback>
    void FatFileSystem::Reject(const Callback& onDone, Result result)
    {
        deferred.Defer([onDone, result]
            {
                Deliver(onDone, result);
            });
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

    void FatFileSystem::BeginMount()
    {
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
        operation.emplace<FatMount>(device, cache, volume, [this](Result result)
            {
                LifecycleOperationDone(result);
            });
        std::get<FatMount>(operation).Start();
    }

    void FatFileSystem::BeginUnmount()
    {
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
        cache.Flush([this](Result result)
            {
                cache.Clear();
                cache.SetMirroredRegion(std::nullopt);
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
        DeferCompletion(onMountDone, result);
        DeferCompletion(onUnmountDone, result);
    }

    void FatFileSystem::DeferCompletion(infra::Function<void(Result result)>& onDone, Result result)
    {
        if (onDone)
            deferred.Defer([done = std::exchange(onDone, nullptr), result]
                {
                    done(result);
                });
    }

    FatPathSlot* FatFileSystem::FreePathSlot()
    {
        for (auto& slot : pathSlots)
            if (!slot.InUse())
                return &slot;

        return nullptr;
    }

    void FatFileSystem::StartPathCall(FatPathSlot& slot, FatPathCall&& call)
    {
        slot.call = std::move(call);
        slot.claimer.Claim([this, &slot]
            {
                RunPathCall(slot);
            });
    }

    void FatFileSystem::RunPathCall(FatPathSlot& slot)
    {
        auto call = std::exchange(slot.call, std::monostate{});
        slot.claimer.Release();

        if (auto* stat = std::get_if<FatStatCall>(&call))
            RunStat(*stat);
    }

    void FatFileSystem::RunStat(FatStatCall& call) const
    {
        if (IsRoot(call.path))
            call.onDone(Result::success, EntryInfo{ infra::BoundedConstString(), EntryType::directory, 0 });
        else
            call.onDone(Result::unsupported, EntryInfo{});
    }
}
