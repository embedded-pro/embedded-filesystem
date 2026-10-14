#include "fs/nor/NorFileSystem.hpp"
#include <utility>

namespace fs
{
    NorFileSystem::NorFileSystem(const NorVolumeParts& parts, const NorConfig& config, infra::Duration idleDelay)
        : volume(parts, config, idleDelay)
        , lifecycleClaimer(volume.Queue())
        , lifecycle(*this, services::Validated<NorFileSystem>())
    {
        lifecycle.OnEntered<NorMounted>([](NorFileSystem& fileSystem, NorMounted&)
            {
                fileSystem.Entered(Result::success);
            });
        lifecycle.OnEntered<NorFault>([](NorFileSystem& fileSystem, const NorFault& fault)
            {
                fileSystem.Entered(fault.result);
            });
        lifecycle.OnEntered<NorUnmounted>([](NorFileSystem& fileSystem, NorUnmounted&)
            {
                fileSystem.Entered(fileSystem.lifecycleResult);
            });
        lifecycle.Start<NorUnmounted>();
    }

    NorFileSystem::~NorFileSystem() = default;

    void NorFileSystem::Format(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(NorFormatRequested{}) == services::DispatchResult::transitioned)
            onFormatDone = onDone;
        else
            volume.Defer(onDone, Result::busy);
    }

    void NorFileSystem::Mount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(NorMountRequested{}) == services::DispatchResult::transitioned)
            onMountDone = onDone;
        else
            volume.Defer(onDone, Result::busy);
    }

    void NorFileSystem::Unmount(const infra::Function<void(Result result)>& onDone)
    {
        if (lifecycle.Dispatch(NorUnmountRequested{}) == services::DispatchResult::transitioned)
            onUnmountDone = onDone;
        else if (std::holds_alternative<NorUnmounted>(lifecycle.CurrentState()) || std::holds_alternative<NorFault>(lifecycle.CurrentState()))
            volume.Defer(onDone, Result::notMounted);
        else
            volume.Defer(onDone, Result::busy);
    }

    void NorFileSystem::Open(infra::BoundedConstString, OpenMode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFileSystem::OpenDirectory(infra::BoundedConstString, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFileSystem::Stat(infra::BoundedConstString, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFileSystem::Remove(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFileSystem::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFileSystem::MakeDirectory(infra::BoundedConstString, const infra::Function<void(Result result)>& onDone)
    {
        Refuse(onDone);
    }

    void NorFileSystem::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        Refuse(onDone);
    }

    bool NorFileSystem::IsMounted() const
    {
        return std::holds_alternative<NorMounted>(lifecycle.CurrentState());
    }

    template<class Callback>
    void NorFileSystem::Refuse(const Callback& onDone)
    {
        volume.Defer(onDone, IsMounted() ? Result::unsupported : Result::notMounted);
    }

    void NorFileSystem::BeginFormat()
    {
        lifecycleClaimer.Claim([this]
            {
                StartFormat();
            });
    }

    void NorFileSystem::StartFormat()
    {
        lifecycleDone = lifecycle.CompletionWith<void(Result result)>([](Result result)
            {
                return NorFormatDone{ result };
            });
        volume.Operation().emplace<NorFormatter>(volume.Flash(), volume.Config(), volume.NodeStorage()).Format([this](Result result)
            {
                LifecycleOperationDone(result);
            });
    }

    void NorFileSystem::BeginMount()
    {
        lifecycleClaimer.Claim([this]
            {
                StartMount();
            });
    }

    void NorFileSystem::StartMount()
    {
        lifecycleDone = lifecycle.CompletionWith<void(Result result)>([](Result result)
            {
                return NorMountDone{ result };
            });
        volume.Operation().emplace<NorMount>(volume.Flash(), volume.Config(), volume.State(), volume.JournalBuffer(), [this](Result result)
                              {
                                  LifecycleOperationDone(result);
                              })
            .Start();
    }

    void NorFileSystem::BeginUnmount()
    {
        lifecycleClaimer.Claim([this]
            {
                lifecycleDone = lifecycle.CompletionWith<void(Result result)>([](Result result)
                    {
                        return NorUnmountDone{ result };
                    });
                LifecycleOperationDone(Result::success);
            });
    }

    void NorFileSystem::LifecycleOperationDone(Result result)
    {
        lifecycleClaimer.Release();
        auto done = std::exchange(lifecycleDone, nullptr);
        done(result);
    }

    void NorFileSystem::Entered(Result result)
    {
        DeferCompletion(onFormatDone, result);
        DeferCompletion(onMountDone, result);
        DeferCompletion(onUnmountDone, result);
    }

    void NorFileSystem::DeferCompletion(infra::Function<void(Result result)>& onDone, Result result)
    {
        if (onDone)
            volume.Defer(std::exchange(onDone, nullptr), result);
    }
}
