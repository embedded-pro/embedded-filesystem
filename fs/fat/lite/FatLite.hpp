#ifndef FS_FAT_LITE_FAT_LITE_HPP
#define FS_FAT_LITE_FAT_LITE_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/FlushBarrier.hpp"
#include "fs/common/HandlePool.hpp"
#include "fs/common/LiteLifecycle.hpp"
#include "fs/common/Path.hpp"
#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatDirectoryEntry.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatShortName.hpp"
#include "fs/fat/lite/FatLiteFile.hpp"
#include "fs/fat/lite/FatLiteScan.hpp"
#include "fs/fat/lite/FatLiteVolume.hpp"
#include "fs/fat/lite/FatLiteWindow.hpp"
#include "fs/fat/reader/FatReaderDirectory.hpp"
#include "fs/fat/reader/FatReaderFeatures.hpp"
#include "fs/fat/reader/FatReaderMount.hpp"
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
    class FatLite
        : public FileSystem
    {
    public:
        template<std::size_t Files, std::size_t Directories = 1, PathLimits Limits = PathLimits{}, FatReaderFeatures Features = FatReaderFeatures{}>
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

    protected:
        FatLite(hal::BlockDevice& device, infra::ByteRange sector, std::size_t transferAlignment, infra::ByteRange nameBuffer, FatEntryAssembler::Reading reading, DeferredCompletions& deferred, const PathLimits& limits);
        FatLite(const FatLite& other) = delete;
        FatLite& operator=(const FatLite& other) = delete;
        ~FatLite();

        FatLiteVolume& Volume();

    private:
        virtual infra::SharedPtr<FatLiteFile> AllocateFile() = 0;
        virtual infra::SharedPtr<FatReaderDirectory> AllocateDirectory() = 0;

        Result Unsupported() const;
        Result Refusal(infra::BoundedConstString path) const;
        bool PathCallPending() const;
        void Start(infra::BoundedConstString path);
        void Run();
        void Found(Result result);
        void StatFound(Result result);
        void OpenDirectoryFound(Result result);

        void FindParent();
        void ParentFound(Result result);
        void Scanned(Result result);
        void OpenExisting(const FatScannedEntry& found);
        Result Verify(const FatScannedEntry& found);
        void Create();
        void GrowDirectory(uint32_t lastCluster);
        void ZeroNext();
        void WriteEntry(FatEntryPosition position);
        void Opened(Result result);
        void OpenDone(Result result);

        void BeginMount();
        void Mounted(Result result);
        void BeginUnmount();
        void CommitNext();
        void WriteFsInfo();
        void UnmountDone(Result result);
        void Entered(Result result);

    private:
        FlushBarrier barrier;
        FatLiteWindow window;
        FatLiteVolume volume;
        FatReaderMount mount;
        FatLiteScan scan;
        PathLimits limits;
        infra::ClaimableResource::Claimer lifecycleClaimer;
        infra::ClaimableResource::Claimer pathClaimer;
        infra::BoundedConstString path;
        infra::BoundedConstString name;
        OpenMode mode{ OpenMode::read };
        uint32_t parent{ 0 };
        uint32_t grown{ 0 };
        uint32_t zeroed{ 0 };
        FatScannedEntry created;
        infra::SharedPtr<FatLiteFile> opening;
        infra::AutoResetFunction<void(Result result, const EntryInfo& info)> onStat;
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<File> file)> onOpen;
        infra::AutoResetFunction<void(Result result, infra::SharedPtr<Directory> directory)> onOpenDirectory;
        LiteLifecycle lifecycle;
        infra::Function<void(Result result)> onMountDone;
        infra::Function<void(Result result)> onUnmountDone;
    };

    namespace detail
    {
        template<std::size_t Completions, std::size_t NameBytes>
        struct FatLiteStorage
        {
            alignas(bufferAlignment) std::array<uint8_t, 512> sectorStorage{};
            std::array<uint8_t, NameBytes> nameStorage{};
            DeferredCompletions::WithMaxSize<Completions> deferredStorage;
        };
    }

    template<std::size_t Files, std::size_t Directories, PathLimits Limits, FatReaderFeatures Features>
    class FatLite::WithStorage
        : private detail::FatLiteStorage<Files + Directories + 3, Features.longNames ? Limits.maxNameBytes : ShortName::maxUtf8Bytes>
        , public FatLite
    {
        static_assert(Files > 0 && Directories > 0 && Limits.maxNameBytes >= ShortName::maxUtf8Bytes);

    public:
        explicit WithStorage(hal::BlockDevice& device, std::size_t transferAlignment = 1)
            : FatLite(device, infra::MakeRange(this->sectorStorage), transferAlignment, infra::MakeRange(this->nameStorage), NameReadingFor<Features>(), this->deferredStorage, Limits)
        {}

    private:
        infra::SharedPtr<FatLiteFile> AllocateFile() override
        {
            return files.Allocate();
        }

        infra::SharedPtr<FatReaderDirectory> AllocateDirectory() override
        {
            return directories.Allocate();
        }

    private:
        HandlePool<FatLiteFile, Files> files{ Volume() };
        HandlePool<FatReaderDirectory, Directories> directories{ Volume() };
    };
}

#endif
