#ifndef SPIKE_FAT_READER_FAT_READER_HPP
#define SPIKE_FAT_READER_FAT_READER_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/HandlePool.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "services/fsm/StateMachineDefinition.hpp"
#include "services/fsm/TableStateMachine.hpp"
#include "spike/fat_reader/FatReaderDirectory.hpp"
#include "spike/fat_reader/FatReaderFile.hpp"
#include "spike/fat_reader/FatReaderLifecycle.hpp"
#include "spike/fat_reader/FatReaderVolume.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <variant>

namespace spike
{
    using FatReaderPathCall = std::variant<infra::AutoResetFunction<void(fs::Result result, const fs::EntryInfo& info)>,
        infra::AutoResetFunction<void(fs::Result result, infra::SharedPtr<fs::File> file)>,
        infra::AutoResetFunction<void(fs::Result result, infra::SharedPtr<fs::Directory> directory)>>;

    class FatReader
        : public fs::FileSystem
    {
    public:
        template<std::size_t Files, std::size_t Directories>
        class WithStorage;

        using Machine = services::TableStateMachine<FatReaderState, FatReaderEvent, FatReader>;
        using Initial = FatReaderUnmounted;

        static constexpr std::array<Machine::Transition, 6> Rows();
        static constexpr Machine::Rules Rules();

        void Mount(const infra::Function<void(fs::Result result)>& onDone) override;
        void Unmount(const infra::Function<void(fs::Result result)>& onDone) override;
        void Open(infra::BoundedConstString path, fs::OpenMode mode, const infra::Function<void(fs::Result result, infra::SharedPtr<fs::File> file)>& onDone) override;
        void OpenDirectory(infra::BoundedConstString path, const infra::Function<void(fs::Result result, infra::SharedPtr<fs::Directory> directory)>& onDone) override;
        void Stat(infra::BoundedConstString path, const infra::Function<void(fs::Result result, const fs::EntryInfo& info)>& onDone) override;
        void Remove(infra::BoundedConstString path, const infra::Function<void(fs::Result result)>& onDone) override;
        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(fs::Result result)>& onDone) override;
        void MakeDirectory(infra::BoundedConstString path, const infra::Function<void(fs::Result result)>& onDone) override;
        void Usage(const infra::Function<void(fs::Result result, fs::VolumeUsage usage)>& onDone) override;

    protected:
        FatReader(hal::BlockDevice& device, infra::ByteRange sector, fs::DeferredCompletions& deferred);
        FatReader(const FatReader& other) = delete;
        FatReader& operator=(const FatReader& other) = delete;
        ~FatReader() = default;

        FatReaderVolume& Volume();

    private:
        virtual infra::SharedPtr<FatReaderFile> AllocateFile() = 0;
        virtual infra::SharedPtr<FatReaderDirectory> AllocateDirectory() = 0;

        bool IsMounted() const;
        fs::Result Refusal(infra::BoundedConstString path) const;
        fs::Result Unavailable() const;
        void Start(infra::BoundedConstString path, FatReaderPathCall&& call);
        void Run();
        void Found(fs::Result result, const FatEntry& entry);
        void OpenFile(infra::AutoResetFunction<void(fs::Result result, infra::SharedPtr<fs::File> file)>& onDone, fs::Result result, const FatEntry& entry);
        void OpenDirectoryAt(infra::AutoResetFunction<void(fs::Result result, infra::SharedPtr<fs::Directory> directory)>& onDone, fs::Result result, const FatEntry& entry);

        void BeginMount();
        void FirstSectorRead(fs::Result result);
        void BootSectorRead(fs::Result result, uint32_t start);
        void Mounted(fs::Result result);
        void BeginUnmount();
        void Entered(fs::Result result);

    private:
        FatReaderVolume volume;
        infra::ClaimableResource::Claimer lifecycleClaimer;
        infra::ClaimableResource::Claimer pathClaimer;
        infra::BoundedConstString path;
        std::optional<FatReaderPathCall> call;
        Machine::WithStorage<2> lifecycle;
        infra::Function<void(fs::Result result)> onMountDone;
        infra::Function<void(fs::Result result)> onUnmountDone;
    };

    namespace detail
    {
        template<std::size_t Completions>
        struct FatReaderStorage
        {
            std::array<uint8_t, fatSectorBytes> sectorStorage{};
            fs::DeferredCompletions::WithMaxSize<Completions> deferredStorage;
        };
    }

    template<std::size_t Files, std::size_t Directories>
    class FatReader::WithStorage
        : private detail::FatReaderStorage<Files + Directories + 3>
        , public FatReader
    {
        static_assert(Files > 0 && Directories > 0);

    public:
        explicit WithStorage(hal::BlockDevice& device)
            : FatReader(device, infra::MakeRange(this->sectorStorage), this->deferredStorage)
        {}

    private:
        infra::SharedPtr<FatReaderFile> AllocateFile() override
        {
            return files.Allocate();
        }

        infra::SharedPtr<FatReaderDirectory> AllocateDirectory() override
        {
            return directories.Allocate();
        }

    private:
        fs::HandlePool<FatReaderFile, Files> files{ Volume() };
        fs::HandlePool<FatReaderDirectory, Directories> directories{ Volume() };
    };

    constexpr std::array<FatReader::Machine::Transition, 6> FatReader::Rows()
    {
        return {
            Machine::Row<FatReaderUnmounted, FatReaderMountRequested, FatReaderMounting>(nullptr, [](FatReader& reader, FatReaderUnmounted&, const FatReaderMountRequested&)
                {
                    reader.BeginMount();
                    return FatReaderMounting{};
                }),
            Machine::Row<FatReaderFault, FatReaderMountRequested, FatReaderMounting>(nullptr, [](FatReader& reader, FatReaderFault&, const FatReaderMountRequested&)
                {
                    reader.BeginMount();
                    return FatReaderMounting{};
                }),
            Machine::Row<FatReaderMounting, FatReaderMountDone, FatReaderMounted>([](FatReader&, const FatReaderMounting&, const FatReaderMountDone& done)
                {
                    return done.result == fs::Result::success;
                }),
            Machine::Row<FatReaderMounting, FatReaderMountDone, FatReaderFault>(nullptr, [](FatReader&, FatReaderMounting&, const FatReaderMountDone& done)
                {
                    return FatReaderFault{ done.result };
                }),
            Machine::Row<FatReaderMounted, FatReaderUnmountRequested, FatReaderUnmounting>(nullptr, [](FatReader& reader, FatReaderMounted&, const FatReaderUnmountRequested&)
                {
                    reader.BeginUnmount();
                    return FatReaderUnmounting{};
                }),
            Machine::Row<FatReaderUnmounting, FatReaderUnmountDone, FatReaderUnmounted>(),
        };
    }

    constexpr FatReader::Machine::Rules FatReader::Rules()
    {
        return Machine::Rules{}
            .Allow<FatReaderUnmounted, FatReaderMounting>()
            .Allow<FatReaderMounting, FatReaderMounted, FatReaderFault>()
            .Allow<FatReaderMounted, FatReaderUnmounting>()
            .Allow<FatReaderUnmounting, FatReaderUnmounted>()
            .Allow<FatReaderFault, FatReaderMounting>();
    }
}

#endif
