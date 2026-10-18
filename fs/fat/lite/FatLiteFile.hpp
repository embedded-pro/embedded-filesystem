#ifndef FS_FAT_LITE_FAT_LITE_FILE_HPP
#define FS_FAT_LITE_FAT_LITE_FILE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/lite/FatLiteVolume.hpp"
#include "fs/interfaces/File.hpp"
#include "fs/interfaces/OpenMode.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/IntrusiveList.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class FatLiteFile
        : public File
        , public PooledHandle
        , public infra::IntrusiveList<FatLiteFile>::NodeType
    {
    public:
        explicit FatLiteFile(FatLiteVolume& volume);
        FatLiteFile(const FatLiteFile& other) = delete;
        FatLiteFile& operator=(const FatLiteFile& other) = delete;
        ~FatLiteFile();

        void Open(const FatScannedEntry& entry, OpenMode mode);
        void Detach();
        bool Conflicts(FatEntryPosition position, OpenMode otherMode) const;
        bool Uncommitted() const;
        void Commit(const infra::Function<void(Result result)>& onDone);

        uint32_t Size() const override;
        uint32_t Position() const override;
        void Seek(uint32_t newPosition) override;
        void Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone) override;
        void Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone) override;
        void Truncate(const infra::Function<void(Result result)>& onDone) override;
        void ExpectSize(uint32_t expected, const infra::Function<void(Result result)>& onDone) override;
        void Sync(const infra::Function<void(Result result)>& onDone) override;
        void Close(const infra::Function<void(Result result)>& onDone) override;

    private:
        enum class Call : uint8_t
        {
            none,
            read,
            write,
            truncate,
            sync,
            close,
            implicitClose
        };

        enum class Step : uint8_t
        {
            link,
            linked,
            truncationCommitted,
            endChain,
            chainEnded,
            freeCut,
            flush,
            done
        };

        const infra::Function<void(Result result)>& Then(Step next);
        void Resume(Result result);

        void UnReferenced() override;
        bool Pending() const;
        bool Writable() const;
        Result Unwritable() const;
        void Claim(Call call);
        void Enqueue(Call call);
        void Granted();

        void Reach(uint32_t index);
        void Follow();
        void Followed(Result result, std::optional<uint32_t> next);
        void Grow();
        void Taken(Result result, uint32_t newCluster);
        void Link();
        void Linked();
        void Reached();

        void ReadStep();
        void ReadPart();
        void WriteStep();
        void WritePart(std::size_t remaining);
        void TransferSectors(uint32_t sectors);
        void Moved(Result result);

        void StartTruncate();
        void Cut(std::optional<uint32_t> next);
        void TruncationCommitted();
        void EndChain();
        void ChainEnded();
        void FreeCut();
        void StartSync();
        void Flush();

        void Done(Result result);
        void Finish(Result result);
        uint32_t SectorInCluster() const;
        uint32_t Sector() const;

    private:
        FatLiteVolume& volume;
        infra::ClaimableResource::Claimer claimer;
        uint32_t generation{ 0 };
        bool open{ false };
        OpenMode mode{ OpenMode::read };
        FatEntryPosition entry{};
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };
        uint32_t position{ 0 };
        uint32_t committedFirstCluster{ 0 };
        uint32_t committedSize{ 0 };
        bool written{ false };
        uint32_t cluster{ 0 };
        uint32_t clusterIndex{ 0 };
        uint32_t target{ 0 };
        uint32_t taken{ 0 };
        uint32_t cut{ 0 };
        uint32_t transferSector{ 0 };
        Call call{ Call::none };
        Step step{ Step::done };
        infra::Function<void(Result result)> resume;
        infra::ByteRange readBuffer;
        infra::ConstByteRange writeData;
        std::size_t transferred{ 0 };
        std::size_t moving{ 0 };
        infra::SharedPtr<FatLiteFile> self;
        infra::AutoResetFunction<void(Result result, std::size_t bytesRead)> onRead;
        infra::AutoResetFunction<void(Result result)> onDone;
        infra::AutoResetFunction<void(Result result)> onCommitted;
    };
}

#endif
