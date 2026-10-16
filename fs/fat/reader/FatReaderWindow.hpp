#ifndef FS_FAT_READER_FAT_READER_WINDOW_HPP
#define FS_FAT_READER_FAT_READER_WINDOW_HPP

#include "fs/common/SectorSource.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatReaderWindow
        : public SectorSource
    {
    public:
        FatReaderWindow(hal::BlockDevice& device, infra::ByteRange buffer);
        FatReaderWindow(const FatReaderWindow& other) = delete;
        FatReaderWindow& operator=(const FatReaderWindow& other) = delete;
        ~FatReaderWindow();

        uint32_t SectorSize() const override;
        void Read(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone) override;
        void Forget();

    private:
        void Loaded(hal::BlockDevice::Result result);

    private:
        static constexpr uint32_t none = 0xffffffff;

        hal::BlockDevice& device;
        infra::ByteRange buffer;
        uint32_t loaded{ none };
        uint32_t requested{ none };
        infra::AutoResetFunction<void(Result result, infra::ConstByteRange contents)> onDone;
    };
}

#endif
