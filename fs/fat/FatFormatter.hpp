#ifndef FS_FAT_FAT_FORMATTER_HPP
#define FS_FAT_FAT_FORMATTER_HPP

#include "fs/common/TransferAlignment.hpp"
#include "fs/fat/FatFormatLayout.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class FatFormatter
    {
    public:
        template<std::size_t BufferSectors>
        class WithStorage;

        FatFormatter(hal::BlockDevice& device, infra::ByteRange buffer);
        FatFormatter(const FatFormatter& other) = delete;
        FatFormatter& operator=(const FatFormatter& other) = delete;
        ~FatFormatter() = default;

        void Format(const FatFormatConfig& config, const infra::Function<void(Result result)>& onDone);

    private:
        void WriteMbr();
        void WriteBootSector();
        void WriteFsInfo();
        void WriteBackupBootSector();
        void WriteBackupFsInfo();
        void WriteFatStart();
        void ZeroFatRest();
        void NextFatCopy();
        void ZeroRoot();
        void DiscardData();
        void FlushDevice();

        void WriteBuffer(uint32_t sector, uint32_t sectors, const infra::Function<void()>& onWritten);
        void ZeroSectors(uint32_t begin, uint32_t end, const infra::Function<void()>& onZeroedDone);
        void ZeroNextChunk();
        void OnDevice(hal::BlockDevice::Result deviceResult);
        void Complete(Result completionResult);

        infra::ByteRange FirstSector() const;
        uint32_t FatSector() const;
        bool IsFat32() const;

    private:
        hal::BlockDevice& device;
        infra::ByteRange buffer;
        std::optional<FatFormatLayout> layout;
        uint8_t copy{ 0 };
        uint32_t zeroNext{ 0 };
        uint32_t zeroEnd{ 0 };
        Result result{ Result::success };
        infra::AutoResetFunction<void()> onDeviceDone;
        infra::AutoResetFunction<void()> onZeroed;
        infra::AutoResetFunction<void(Result result)> onDone;
    };

    namespace detail
    {
        template<std::size_t BufferSectors>
        struct FatFormatterStorage
        {
            alignas(bufferAlignment) std::array<uint8_t, BufferSectors * 512> bufferStorage;
        };
    }

    template<std::size_t BufferSectors>
    class FatFormatter::WithStorage
        : private detail::FatFormatterStorage<BufferSectors>
        , public FatFormatter
    {
        static_assert(BufferSectors > 0);

    public:
        explicit WithStorage(hal::BlockDevice& device)
            : FatFormatter(device, infra::MakeRange(this->bufferStorage))
        {}
    };
}

#endif
