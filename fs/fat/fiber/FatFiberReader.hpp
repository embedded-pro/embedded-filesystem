#ifndef FS_FAT_FIBER_FAT_FIBER_READER_HPP
#define FS_FAT_FIBER_FAT_FIBER_READER_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/HandlePool.hpp"
#include "fs/common/LiteLifecycle.hpp"
#include "fs/common/Path.hpp"
#include "fs/fat/FatShortName.hpp"
#include "fs/fat/fiber/FatFiberDirectory.hpp"
#include "fs/fat/fiber/FatFiberFile.hpp"
#include "fs/fat/fiber/FatFiberVolume.hpp"
#include "fs/fat/fiber/FatFiberWindow.hpp"
#include "fs/fat/fiber/Fiber.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace fs
{
    class FatFiberReader
        : public FileSystem
    {
    public:
        static constexpr std::size_t stackBytes = sizeof(void*) == 4 ? 640 : 65536;

        template<std::size_t Files, std::size_t Directories = 1, PathLimits Limits = PathLimits{}>
        class WithStorage;

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

        std::size_t StackPeak() const;

    protected:
        FatFiberReader(hal::BlockDevice& device, infra::ByteRange sector, std::size_t transferAlignment, infra::ByteRange nameBuffer, DeferredCompletions& deferred, const PathLimits& limits);
        FatFiberReader(const FatFiberReader& other) = delete;
        FatFiberReader& operator=(const FatFiberReader& other) = delete;
        ~FatFiberReader() = default;

        FatFiberVolume& Volume();

    private:
        virtual infra::SharedPtr<FatFiberFile> AllocateFile() = 0;
        virtual infra::SharedPtr<FatFiberDirectory> AllocateDirectory() = 0;

        Result Refusal(infra::BoundedConstString path) const;
        Result ReadOnly() const;
        bool PathCallPending() const;
        void Start(infra::BoundedConstString path);
        void RunPath();
        void PathCompleted();
        void StatFound(Result result);
        void OpenFound(Result result);
        void OpenDirectoryFound(Result result);

        void BeginChange(const infra::Function<void(Result result)>& onDone);
        void RunMount();
        void Entered(Result result);

    private:
        fiber::StackStorage<stackBytes> stackStorage;
        fiber::Runner runner;
        FatFiberWindow window;
        FatFiberVolume volume;
        PathLimits limits;
        infra::ClaimableResource::Claimer lifecycleClaimer;
        infra::ClaimableResource::Claimer pathClaimer;
        infra::BoundedConstString path;
        Result pathResult{ Result::success };
        infra::AutoResetFunction<void(Result result, const EntryInfo& info)> onStat;
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<File> file)> onOpen;
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<Directory> directory)> onOpenDirectory;
        LiteLifecycle lifecycle;
        infra::Function<void(Result result)> onChanged;
    };

    namespace detail
    {
        template<std::size_t Completions, std::size_t NameBytes>
        struct FatFiberReaderStorage
        {
            alignas(bufferAlignment) std::array<uint8_t, 512> sectorStorage{};
            std::array<uint8_t, NameBytes> nameStorage{};
            DeferredCompletions::WithMaxSize<Completions> deferredStorage;
        };
    }

    template<std::size_t Files, std::size_t Directories, PathLimits Limits>
    class FatFiberReader::WithStorage
        : private detail::FatFiberReaderStorage<Files + Directories + 3, Limits.maxNameBytes>
        , public FatFiberReader
    {
        static_assert(Files > 0 && Directories > 0 && Limits.maxNameBytes >= ShortName::maxUtf8Bytes);

    public:
        explicit WithStorage(hal::BlockDevice& device, std::size_t transferAlignment = 1)
            : FatFiberReader(device, infra::MakeRange(this->sectorStorage), transferAlignment, infra::MakeRange(this->nameStorage), this->deferredStorage, Limits)
        {}

    private:
        infra::SharedPtr<FatFiberFile> AllocateFile() override
        {
            return files.Allocate();
        }

        infra::SharedPtr<FatFiberDirectory> AllocateDirectory() override
        {
            return directories.Allocate();
        }

    private:
        HandlePool<FatFiberFile, Files> files{ Volume() };
        HandlePool<FatFiberDirectory, Directories> directories{ Volume() };
    };
}

#endif
