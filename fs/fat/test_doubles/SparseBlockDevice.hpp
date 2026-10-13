#ifndef FS_FAT_TEST_DOUBLES_SPARSE_BLOCK_DEVICE_HPP
#define FS_FAT_TEST_DOUBLES_SPARSE_BLOCK_DEVICE_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/MemoryRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class SparseBlockDevice
        : public hal::BlockDevice
    {
    public:
        template<std::size_t Slots, std::size_t BytesPerBlock = 512>
        class WithStorage;

        enum class Request : uint8_t
        {
            read,
            write,
            erase,
            flush
        };

        SparseBlockDevice(uint32_t numberOfBlocks, uint32_t blockSize, infra::MemoryRange<uint32_t> blockNumbers, infra::ByteRange contents);

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

        void FailNextOperationWith(Result result, uint32_t succeedingFirst = 0);
        void Observe(const infra::Function<void(Request request, uint32_t firstBlock)>& observer);
        void ReadBlock(uint32_t block, infra::ByteRange out) const;
        void WriteBlock(uint32_t block, infra::ConstByteRange data);

        std::size_t StoredBlocks() const;
        uint32_t ReadRequests() const;
        uint32_t WriteRequests() const;
        uint32_t EraseRequests() const;
        uint32_t FlushRequests() const;

    private:
        bool Start(uint32_t firstBlock, uint32_t blocks, const infra::Function<void(Result)>& onDone);
        std::optional<std::size_t> SlotOf(uint32_t block) const;
        infra::ByteRange Contents(std::size_t slot) const;
        void Free(uint32_t block);
        void Complete(Result result);

    private:
        static constexpr uint32_t unused = 0xffffffff;

        uint32_t numberOfBlocks;
        uint32_t blockSize;
        infra::MemoryRange<uint32_t> blockNumbers;
        infra::ByteRange contents;
        std::optional<Result> injectedFailure;
        infra::Function<void(Request request, uint32_t firstBlock)> observer;
        uint32_t succeedBeforeFailure{ 0 };
        uint32_t readRequests{ 0 };
        uint32_t writeRequests{ 0 };
        uint32_t eraseRequests{ 0 };
        uint32_t flushRequests{ 0 };
        infra::AutoResetFunction<void(Result)> onDone;
    };

    namespace detail
    {
        template<std::size_t Slots, std::size_t BytesPerBlock>
        struct SparseBlockDeviceStorage
        {
            std::array<uint32_t, Slots> blockNumberStorage;
            std::array<uint8_t, Slots * BytesPerBlock> contentStorage;
        };
    }

    template<std::size_t Slots, std::size_t BytesPerBlock>
    class SparseBlockDevice::WithStorage
        : private detail::SparseBlockDeviceStorage<Slots, BytesPerBlock>
        , public SparseBlockDevice
    {
    public:
        explicit WithStorage(uint32_t numberOfBlocks)
            : SparseBlockDevice(numberOfBlocks, BytesPerBlock, infra::MakeRange(this->blockNumberStorage), infra::MakeRange(this->contentStorage))
        {}
    };
}

#endif
