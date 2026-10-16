#ifndef FS_FAT_READER_FAT_READER_FILE_HPP
#define FS_FAT_READER_FAT_READER_FILE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/reader/FatReaderVolume.hpp"
#include "fs/interfaces/File.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class FatReaderFile
        : public File
        , public PooledHandle
    {
    public:
        explicit FatReaderFile(FatReaderVolume& volume);
        FatReaderFile(const FatReaderFile& other) = delete;
        FatReaderFile& operator=(const FatReaderFile& other) = delete;
        ~FatReaderFile() = default;

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
        void Granted();
        void Step();
        void Located(Result result, std::optional<uint32_t> next);
        void Transfer();
        void TransferPart(uint32_t sector);
        void TransferSectors(uint32_t sector, uint32_t sectors);
        void Transferred(Result result, std::size_t bytes);
        void Done(Result result);
        std::size_t Remaining() const;
        uint32_t ClusterBytes() const;
        Result Writable() const;
        Result Settled() const;

    private:
        FatReaderVolume& volume;
        infra::ClaimableResource::Claimer claimer;
        uint32_t generation{ 0 };
        uint32_t firstCluster{ 0 };
        uint32_t size{ 0 };
        uint32_t position{ 0 };
        uint32_t cluster{ 0 };
        uint32_t clusterIndex{ 0 };
        infra::ByteRange buffer;
        std::size_t bytesRead{ 0 };
        std::size_t transferBytes{ 0 };
        infra::SharedPtr<FatReaderFile> self;
        infra::AutoResetFunction<void(Result result, std::size_t bytesRead)> onRead;
    };
}

#endif
