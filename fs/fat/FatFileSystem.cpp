#include "fs/fat/FatFileSystem.hpp"
#include "infra/util/ReallyAssert.hpp"
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
        lifecycle.OnEntered<FatFault>([](FatFileSystem& fileSystem, const FatFault& fault)
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
        if (lifecycle.Dispatch(FatMountRequested{}) == services::DispatchResult::transitioned)
            onMountDone = onDone;
        else
            volume.Defer(onDone, Result::busy);
    }

    void FatFileSystem::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatUnmountRequested{}) == services::DispatchResult::transitioned)
            onUnmountDone = onDone;
        else if (std::holds_alternative<FatMounting>(lifecycle.CurrentState()) || std::holds_alternative<FatUnmounting>(lifecycle.CurrentState()))
            volume.Defer(onDone, Result::busy);
        else
            volume.Defer(onDone, Unavailable());
    }

    void FatFileSystem::Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(FatOpenCall{ path, mode, onDone });
    }

    void FatFileSystem::OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        if (auto refusal = Refusal(path, volume.Operations().openDirectory != nullptr); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(FatOpenDirectoryCall{ path, onDone });
    }

    void FatFileSystem::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(FatStatCall{ path, onDone });
    }

    void FatFileSystem::Remove(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone)
    {
        if (auto refusal = Refusal(path, volume.Operations().remove != nullptr); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(FatRemoveCall{ path, onDone });
    }

    void FatFileSystem::Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone)
    {
        auto validation = ValidatePath(to, volume.Limits());

        if (auto refusal = Refusal(validation != Result::success ? validation : ValidatePath(from, volume.Limits()), volume.Operations().rename != nullptr); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(FatRenameCall{ from, to, onDone });
    }

    void FatFileSystem::MakeDirectory(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone)
    {
        if (auto refusal = Refusal(path, volume.Operations().makeDirectory != nullptr); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(FatMakeDirectoryCall{ path, onDone });
    }

    void FatFileSystem::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        if (auto refusal = Refusal(Result::success, volume.Operations().usage != nullptr); refusal != Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(FatUsageCall{ onDone });
    }

    bool FatFileSystem::Idle() const
    {
        return volume.Idle();
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
        volume.Operation().emplace<FatMount>(volume.Device(), volume.Cache(), volume.State(), volume.Operations().readCleanState, [this](Result result)
                              {
                                  LifecycleOperationDone(result);
                              })
            .Start();
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
        volume.Operation().emplace<FatUnmount>(volume, [this](Result result)
                              {
                                  LifecycleOperationDone(result);
                              })
            .Start();
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
            volume.Defer(std::exchange(onDone, nullptr), result);
    }

    Result FatFileSystem::Refusal(infra::BoundedConstString path, bool supported) const
    {
        return Refusal(ValidatePath(path, volume.Limits()), supported);
    }

    Result FatFileSystem::Refusal(Result validation, bool supported) const
    {
        if (!IsMounted())
            return Unavailable();
        if (!supported)
            return Result::unsupported;
        if (validation != Result::success)
            return validation;
        if (FreePathSlot() == nullptr)
            return Result::busy;
        return Result::success;
    }

    void FatFileSystem::Start(FatPathCall&& call)
    {
        auto* slot = FreePathSlot();
        really_assert(slot != nullptr);
        slot->call = std::move(call);
        slot->claimer.Claim([this, slot]
            {
                RunPathCall(*slot);
            });
    }

    FatPathSlot* FatFileSystem::FreePathSlot() const
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
            std::visit([this](auto& pending)
                {
                    detail::Deliver(pending.onDone.Clone(), Unavailable());
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
            volume.Operation().emplace<FatStat>(volume, std::move(*stat), finished).Start();
        else if (auto* openDirectory = std::get_if<FatOpenDirectoryCall>(&call))
            volume.Operations().openDirectory(volume, std::move(*openDirectory), allocateDirectory, finished);
        else if (auto* open = std::get_if<FatOpenCall>(&call))
            volume.Operation().emplace<FatOpen>(volume, std::move(*open), allocateFile, finished).Start();
        else if (auto* makeDirectory = std::get_if<FatMakeDirectoryCall>(&call))
            volume.Operations().makeDirectory(volume, std::move(*makeDirectory), finished);
        else if (auto* remove = std::get_if<FatRemoveCall>(&call))
            volume.Operations().remove(volume, std::move(*remove), finished);
        else if (auto* rename = std::get_if<FatRenameCall>(&call))
            volume.Operations().rename(volume, std::move(*rename), finished);
        else
            volume.Operations().usage(volume, std::move(std::get<FatUsageCall>(call)), finished);
    }

    namespace detail
    {
        template<class Operation, class Call>
        void StartFatOperation(FatVolume& volume, Call&& call, const infra::Function<void(Result result)>& onFinished)
        {
            volume.Operation().template emplace<Operation>(volume, std::move(call), onFinished).Start();
        }

        template void StartFatOperation<FatMakeDirectory, FatMakeDirectoryCall>(FatVolume& volume, FatMakeDirectoryCall&& call, const infra::Function<void(Result result)>& onFinished);
        template void StartFatOperation<FatRemove, FatRemoveCall>(FatVolume& volume, FatRemoveCall&& call, const infra::Function<void(Result result)>& onFinished);
        template void StartFatOperation<FatRename, FatRenameCall>(FatVolume& volume, FatRenameCall&& call, const infra::Function<void(Result result)>& onFinished);
        template void StartFatOperation<FatUsage, FatUsageCall>(FatVolume& volume, FatUsageCall&& call, const infra::Function<void(Result result)>& onFinished);

        FatBackgroundStorage<true>::FatBackgroundStorage(FatVolume& volume, infra::Duration idleDelay)
            : scan(volume.Queue(), volume.FreeSpace(), idleDelay, [&volume](Result result)
                  {
                      volume.Report(result);
                  })
        {
            volume.AttachBackground(scan);
        }

        void StartFatOpenDirectory(FatVolume& volume, FatOpenDirectoryCall&& call, const FatDirectoryAllocation& allocate, const infra::Function<void(Result result)>& onFinished)
        {
            volume.Operation().emplace<FatOpenDirectory>(volume, std::move(call), allocate, onFinished).Start();
        }

        void StartFatExpectSize(FatVolume& volume, FatOpenFile& file, FatExtentCache& extents, uint32_t size, const infra::Function<void(Result result)>& onDone)
        {
            volume.Operation().emplace<FatExpectSize>(volume, file, extents, size, onDone).Start();
        }

        void MarkFatDirty(FatDirtyFlag& flag, const infra::Function<void(Result result)>& onDone)
        {
            flag.MarkDirty(onDone);
        }

        void MarkFatClean(FatDirtyFlag& flag, const infra::Function<void(Result result)>& onDone)
        {
            flag.MarkClean(onDone);
        }

        void ReadFatCleanState(FatMount& mount)
        {
            mount.ReadCleanState();
        }
    }
}
