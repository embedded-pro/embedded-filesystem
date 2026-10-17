#ifndef FS_FAT_CORO_FAT_CORO_FILE_HPP
#define FS_FAT_CORO_FAT_CORO_FILE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/coro/Coroutine.hpp"
#include "fs/fat/coro/FatCoroVolume.hpp"
#include "fs/interfaces/File.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    class FatCoroFile
        : public File
        , public PooledHandle
    {
    public:
        static constexpr std::size_t frameBytes = sizeof(void*) == 4 ? 192 : 2048;

        explicit FatCoroFile(FatCoroVolume& volume);

        void Open(const FatScannedEntry& entry);

        uint32_t Size() const override;
        uint32_t Position() const override;
        void Seek(uint32_t newPosition) override;
        void Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone) override;
        void Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone) override;
        void Truncate(const infra::Function<void(Result result)>& onDone) override;
        void ExpectSize(uint32_t expected, const infra::Function<void(Result result)>& onDone) override;
        void Sync(const infra::Function<void(Result result)>& onDone) override;
        void Close(const infra::Function<void(Result result)>& onDone) override;

        std::size_t FramePeak() const;

    private:
        void UnReferenced() override;
        coro::Detached RunRead(coro::Frames& frames);
        void ReadCompleted();
        std::size_t Remaining() const;
        uint32_t ClusterBytes() const;
        Result Writable() const;
        Result Settled() const;

    private:
        FatCoroVolume& volume;
        infra::ClaimableResource::Claimer claimer;
        coro::FrameStorage<frameBytes> frameStorage;
        coro::Frames frames{ infra::MakeRange(frameStorage.bytes) };
        uint32_t generation{ 0 };
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };
        uint32_t position{ 0 };
        uint32_t cluster{ 0 };
        uint32_t clusterIndex{ 0 };
        infra::ByteRange buffer;
        std::size_t bytesRead{ 0 };
        Result readResult{ Result::success };
        infra::SharedPtr<FatCoroFile> self;
        infra::AutoResetFunction<void(Result result, std::size_t bytesRead)> onRead;
    };
}

#endif
