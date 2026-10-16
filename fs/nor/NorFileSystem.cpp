#include "fs/nor/NorFileSystem.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>
#include <utility>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
    }

    NorFileSystem::NorFileSystem(const NorVolumeParts& parts, const NorConfig& config, infra::Duration idleDelay)
        : volume(parts, config, idleDelay)
        , lifecycleClaimer(volume.Queue())
        , lifecycle(*this, services::Validated<NorFileSystem>())
    {
        lifecycle.OnEntered<NorMounted>([](NorFileSystem& fileSystem, NorMounted&)
            {
                fileSystem.volume.MarkMounted();
                fileSystem.Entered(Result::success);
            });
        lifecycle.OnEntered<NorFault>([](NorFileSystem& fileSystem, const NorFault& fault)
            {
                fileSystem.volume.MarkUnmounted();
                fileSystem.Entered(fault.result);
            });
        lifecycle.OnEntered<NorUnmounted>([](NorFileSystem& fileSystem, NorUnmounted&)
            {
                fileSystem.volume.MarkUnmounted();
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

    NorCounters NorFileSystem::Counters() const
    {
        return volume.Counters();
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

    void NorFileSystem::Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone)
    {
        StartPathCall(path, NorOpenCall{ path, mode, onDone });
    }

    void NorFileSystem::OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone)
    {
        StartPathCall(path, NorOpenDirectoryCall{ path, onDone });
    }

    void NorFileSystem::Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone)
    {
        StartPathCall(path, NorStatCall{ path, onDone });
    }

    void NorFileSystem::Remove(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone)
    {
        StartPathCall(path, NorRemoveCall{ path, onDone });
    }

    void NorFileSystem::Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone)
    {
        auto validation = ValidatePath(from, volume.Limits());
        StartPathCall(validation != Result::success ? validation : ValidatePath(to, volume.Limits()), NorRenameCall{ from, to, onDone });
    }

    void NorFileSystem::MakeDirectory(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone)
    {
        StartPathCall(path, NorMakeDirectoryCall{ path, onDone });
    }

    void NorFileSystem::Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone)
    {
        StartCall(NorUsageCall{ onDone });
    }

    bool NorFileSystem::Idle() const
    {
        return volume.Queue().Idle();
    }

    NorVolume& NorFileSystem::Volume()
    {
        return volume;
    }

    void NorFileSystem::AttachPathSlots(infra::MemoryRange<NorPathSlot> slots)
    {
        pathSlots = slots;
    }

    bool NorFileSystem::IsMounted() const
    {
        return std::holds_alternative<NorMounted>(lifecycle.CurrentState());
    }

    template<class Call>
    void NorFileSystem::StartPathCall(infra::BoundedConstString path, Call&& call)
    {
        StartPathCall(ValidatePath(path, volume.Limits()), std::forward<Call>(call));
    }

    template<class Call>
    void NorFileSystem::StartPathCall(Result validation, Call&& call)
    {
        if (IsMounted() && validation != Result::success)
            volume.Defer(call.onDone.Clone(), validation);
        else
            StartCall(std::forward<Call>(call));
    }

    template<class Call>
    void NorFileSystem::StartCall(Call&& call)
    {
        auto* slot = FreePathSlot();

        if (!IsMounted())
            volume.Defer(call.onDone.Clone(), Result::notMounted);
        else if (slot == nullptr)
            volume.Defer(call.onDone.Clone(), Result::busy);
        else
        {
            slot->call = std::forward<Call>(call);
            slot->claimer.Claim([this, slot]
                {
                    RunPathCall(*slot);
                });
        }
    }

    NorPathSlot* NorFileSystem::FreePathSlot() const
    {
        for (auto& slot : pathSlots)
            if (!slot.InUse())
                return &slot;

        return nullptr;
    }

    void NorFileSystem::RunPathCall(NorPathSlot& slot)
    {
        auto call = std::move(*slot.call);
        slot.call = std::nullopt;
        RunOperation(slot, call);
    }

    void NorFileSystem::RunOperation(NorPathSlot& slot, NorPathCall& call)
    {
        auto finished = [&slot](Result)
        {
            slot.claimer.Release();
        };

        if (auto* stat = std::get_if<NorStatCall>(&call))
            volume.Operation().emplace<NorStat>(volume, std::move(*stat), finished).Start();
        else if (auto* openDirectory = std::get_if<NorOpenDirectoryCall>(&call))
            volume.Operation().emplace<NorOpenDirectory>(volume, std::move(*openDirectory), [this]
                                  {
                                      return AllocateDirectory();
                                  },
                                  finished)
                .Start();
        else if (auto* open = std::get_if<NorOpenCall>(&call))
            volume.Operation().emplace<NorOpen>(volume, std::move(*open), [this]
                                  {
                                      return AllocateFile();
                                  },
                                  finished)
                .Start();
        else if (auto* makeDirectory = std::get_if<NorMakeDirectoryCall>(&call))
            volume.Operation().emplace<NorMakeDirectory>(volume, std::move(*makeDirectory), finished).Start();
        else if (auto* remove = std::get_if<NorRemoveCall>(&call))
            volume.Operation().emplace<NorRemove>(volume, std::move(*remove), finished).Start();
        else if (auto* rename = std::get_if<NorRenameCall>(&call))
            volume.Operation().emplace<NorRename>(volume, std::move(*rename), finished).Start();
        else
            ReportUsage(slot, std::get<NorUsageCall>(call));
    }

    void NorFileSystem::ReportUsage(NorPathSlot& slot, NorUsageCall& call)
    {
        const auto& blocks = volume.State().blocks;
        const auto& geometry = volume.Geometry();
        uint64_t payload = geometry.PayloadSize();
        uint64_t total = uint64_t{ geometry.blocks - superblocks - geometry.ReserveBlocks() } * payload;
        auto free = blocks.Count(NorBlockState::freeErased) + blocks.Count(NorBlockState::freeNotErased);
        uint64_t freeBeyondReserve = uint64_t{ free - std::min(free, geometry.ReserveBlocks()) } * payload;

        slot.claimer.Release();
        call.onDone(Result::success, VolumeUsage{ total - freeBeyondReserve, total });
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
                replayCheckpointBefore = std::nullopt;
                StartMount();
            });
    }

    void NorFileSystem::StartMount()
    {
        lifecycleDone = lifecycle.CompletionWith<void(Result result)>([](Result result)
            {
                return NorMountDone{ result };
            });
        volume.Cache().Clear();
        volume.Operation().emplace<NorMount>(volume.Flash(), volume.Config(), volume.State(), volume.JournalBuffer(), [this](Result result)
                              {
                                  if (result == Result::success)
                                      Replay();
                                  else
                                      LifecycleOperationDone(result);
                              })
            .Start();
    }

    void NorFileSystem::Replay()
    {
        volume.Operation().emplace<NorReplay>(volume, replayCheckpointBefore, [this](Result result, std::optional<NorRecordPosition> overflow)
                              {
                                  if (!overflow.has_value())
                                      LifecycleOperationDone(result);
                                  else
                                  {
                                      replayCheckpointBefore = overflow;
                                      infra::EventDispatcher::Instance().Schedule([this]
                                          {
                                              StartMount();
                                          });
                                  }
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
                volume.Operation().emplace<NorUnmount>(volume, [this](Result result)
                                      {
                                          LifecycleOperationDone(result);
                                      })
                    .Start();
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
