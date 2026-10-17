#ifndef FS_FAT_CORO_FAT_CORO_READER_HPP
#define FS_FAT_CORO_FAT_CORO_READER_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/HandlePool.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatLifecycle.hpp"
#include "fs/fat/FatShortName.hpp"
#include "fs/fat/coro/Coroutine.hpp"
#include "fs/fat/coro/FatCoroDirectory.hpp"
#include "fs/fat/coro/FatCoroFile.hpp"
#include "fs/fat/coro/FatCoroVolume.hpp"
#include "fs/fat/coro/FatCoroWindow.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include "services/fsm/StateMachineDefinition.hpp"
#include "services/fsm/TableStateMachine.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace fs
{
    class FatCoroReader
        : public FileSystem
    {
    public:
        static constexpr std::size_t frameBytes = sizeof(void*) == 4 ? 256 : 4096;

        template<std::size_t Files, std::size_t Directories = 1, PathLimits Limits = PathLimits{}>
        class WithStorage;

        using Machine = services::TableStateMachine<FatLifecycleState, FatLifecycleEvent, FatCoroReader>;
        using Initial = FatUnmounted;

        static constexpr std::array<Machine::Transition, 7> Rows();
        static constexpr Machine::Rules Rules();

        void Mount(const infra::Function<void(Result result)>& onDone) override;
        void Unmount(const infra::Function<void(Result result)>& onDone) override;
        void Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone) override;
        void OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone) override;
        void Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone) override;
        void Remove(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) override;
        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone) override;
        void MakeDirectory(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) override;
        void Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone) override;
        bool Idle() const override;

        std::size_t FramePeak() const;

    protected:
        FatCoroReader(hal::BlockDevice& device, infra::ByteRange sector, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const PathLimits& limits);
        FatCoroReader(const FatCoroReader& other) = delete;
        FatCoroReader& operator=(const FatCoroReader& other) = delete;
        ~FatCoroReader() = default;

        FatCoroVolume& Volume();

    private:
        virtual infra::SharedPtr<FatCoroFile> AllocateFile() = 0;
        virtual infra::SharedPtr<FatCoroDirectory> AllocateDirectory() = 0;

        bool IsMounted() const;
        Result Unavailable() const;
        Result Refusal(infra::BoundedConstString path) const;
        Result ReadOnly() const;
        bool PathCallPending() const;
        void Start(infra::BoundedConstString path);
        coro::Detached RunPath(coro::Frames& frames);
        void PathCompleted();
        void StatFound(Result result);
        void OpenFound(Result result);
        void OpenDirectoryFound(Result result);

        void BeginMount();
        coro::Detached RunMount(coro::Frames& frames);
        void BeginUnmount();
        void Entered(Result result);

    private:
        FatCoroWindow window;
        FatCoroVolume volume;
        PathLimits limits;
        infra::ClaimableResource::Claimer lifecycleClaimer;
        infra::ClaimableResource::Claimer pathClaimer;
        coro::FrameStorage<frameBytes> frameStorage;
        coro::Frames frames{ infra::MakeRange(frameStorage.bytes) };
        infra::BoundedConstString path;
        Result pathResult{ Result::success };
        infra::AutoResetFunction<void(Result result, const EntryInfo& info)> onStat;
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<File> file)> onOpen;
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<Directory> directory)> onOpenDirectory;
        Machine::WithStorage<2> lifecycle;
        infra::Function<void(Result result)> onMountDone;
        infra::Function<void(Result result)> onUnmountDone;
    };

    namespace detail
    {
        template<std::size_t Completions, std::size_t NameBytes>
        struct FatCoroReaderStorage
        {
            std::array<uint8_t, 512> sectorStorage{};
            std::array<uint8_t, NameBytes> nameStorage{};
            DeferredCompletions::WithMaxSize<Completions> deferredStorage;
        };
    }

    template<std::size_t Files, std::size_t Directories, PathLimits Limits>
    class FatCoroReader::WithStorage
        : private detail::FatCoroReaderStorage<Files + Directories + 3, Limits.maxNameBytes>
        , public FatCoroReader
    {
        static_assert(Files > 0 && Directories > 0 && Limits.maxNameBytes >= ShortName::maxUtf8Bytes);

    public:
        explicit WithStorage(hal::BlockDevice& device)
            : FatCoroReader(device, infra::MakeRange(this->sectorStorage), infra::MakeRange(this->nameStorage), this->deferredStorage, Limits)
        {}

    private:
        infra::SharedPtr<FatCoroFile> AllocateFile() override
        {
            return files.Allocate();
        }

        infra::SharedPtr<FatCoroDirectory> AllocateDirectory() override
        {
            return directories.Allocate();
        }

    private:
        HandlePool<FatCoroFile, Files> files{ Volume() };
        HandlePool<FatCoroDirectory, Directories> directories{ Volume() };
    };

    constexpr std::array<FatCoroReader::Machine::Transition, 7> FatCoroReader::Rows()
    {
        return {
            Machine::Row<FatUnmounted, FatMountRequested, FatMounting>(nullptr, [](FatCoroReader& reader, FatUnmounted&, const FatMountRequested&)
                {
                    reader.BeginMount();
                    return FatMounting{};
                }),
            Machine::Row<FatFault, FatMountRequested, FatMounting>(nullptr, [](FatCoroReader& reader, FatFault&, const FatMountRequested&)
                {
                    reader.BeginMount();
                    return FatMounting{};
                }),
            Machine::Row<FatMounting, FatMountDone, FatMounted>([](FatCoroReader&, const FatMounting&, const FatMountDone& done)
                {
                    return done.result == Result::success;
                }),
            Machine::Row<FatMounting, FatMountDone, FatFault>(nullptr, [](FatCoroReader&, FatMounting&, const FatMountDone& done)
                {
                    return FatFault{ done.result };
                }),
            Machine::Row<FatMounted, FatUnmountRequested, FatUnmounting>(nullptr, [](FatCoroReader& reader, FatMounted&, const FatUnmountRequested&)
                {
                    reader.BeginUnmount();
                    return FatUnmounting{};
                }),
            Machine::Row<FatMounted, FatMediaRemoved, FatFault>(nullptr, [](FatCoroReader&, FatMounted&, const FatMediaRemoved&)
                {
                    return FatFault{ Result::mediaRemoved };
                }),
            Machine::Row<FatUnmounting, FatUnmountDone, FatUnmounted>(),
        };
    }

    constexpr FatCoroReader::Machine::Rules FatCoroReader::Rules()
    {
        return Machine::Rules{}
            .Allow<FatUnmounted, FatMounting>()
            .Allow<FatMounting, FatMounted, FatFault>()
            .Allow<FatMounted, FatUnmounting, FatFault>()
            .Allow<FatUnmounting, FatUnmounted>()
            .Allow<FatFault, FatMounting>();
    }
}

#endif
