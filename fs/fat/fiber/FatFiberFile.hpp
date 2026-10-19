#ifndef FS_FAT_FIBER_FAT_FIBER_FILE_HPP
#define FS_FAT_FIBER_FAT_FIBER_FILE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/fiber/FatFiberVolume.hpp"
#include "fs/interfaces/File.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    class FatFiberFile
        : public File
        , public PooledHandle
    {
    public:
        explicit FatFiberFile(FatFiberVolume& volume);

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

    private:
        void UnReferenced() override;
        void RunRead();
        Result ReadAll();
        void ReadCompleted();
        std::size_t Remaining() const;
        uint32_t ClusterBytes() const;
        Result Writable() const;
        Result Settled() const;

    private:
        FatFiberVolume& volume;
        infra::ClaimableResource::Claimer claimer;
        uint32_t generation{ 0 };
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };
        uint32_t position{ 0 };
        uint32_t cluster{ 0 };
        uint32_t clusterIndex{ 0 };
        infra::ByteRange buffer;
        std::size_t bytesRead{ 0 };
        Result readResult{ Result::success };
        infra::SharedPtr<FatFiberFile> self;
        infra::AutoResetFunction<void(Result result, std::size_t bytesRead)> onRead;
    };
}

#endif
