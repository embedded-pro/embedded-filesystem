#ifndef SPIKE_FAT_READER_FAT_READER_FILE_HPP
#define SPIKE_FAT_READER_FAT_READER_FILE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/interfaces/File.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/SharedPtr.hpp"
#include "spike/fat_reader/FatReaderVolume.hpp"
#include <cstddef>
#include <cstdint>

namespace spike
{
    class FatReaderFile
        : public fs::File
        , public fs::PooledHandle
    {
    public:
        explicit FatReaderFile(FatReaderVolume& volume);
        FatReaderFile(const FatReaderFile& other) = delete;
        FatReaderFile& operator=(const FatReaderFile& other) = delete;
        ~FatReaderFile() = default;

        void Open(const FatEntry& entry);

        uint32_t Size() const override;
        uint32_t Position() const override;
        void Seek(uint32_t newPosition) override;
        void Read(infra::ByteRange buffer, const infra::Function<void(fs::Result result, std::size_t bytesRead)>& onDone) override;
        void Write(infra::ConstByteRange data, const infra::Function<void(fs::Result result)>& onDone) override;
        void Truncate(const infra::Function<void(fs::Result result)>& onDone) override;
        void ExpectSize(uint32_t expected, const infra::Function<void(fs::Result result)>& onDone) override;
        void Sync(const infra::Function<void(fs::Result result)>& onDone) override;
        void Close(const infra::Function<void(fs::Result result)>& onDone) override;

    private:
        void UnReferenced() override;
        void Granted();
        void Step();
        void Located(fs::Result result, uint32_t next);
        void Transfer();
        void Copied(fs::Result result);
        void Advance(fs::Result result, std::size_t bytes);
        void Done(fs::Result result);
        std::size_t Remaining() const;
        fs::Result Writable() const;
        fs::Result Settled() const;

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
        std::size_t direct{ 0 };
        infra::SharedPtr<FatReaderFile> self;
        infra::AutoResetFunction<void(fs::Result result, std::size_t bytesRead)> onRead;
    };
}

#endif
