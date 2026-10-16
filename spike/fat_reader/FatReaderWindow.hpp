#ifndef SPIKE_FAT_READER_FAT_READER_WINDOW_HPP
#define SPIKE_FAT_READER_FAT_READER_WINDOW_HPP

#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "spike/fat_reader/FatReaderLayout.hpp"
#include <cstdint>

namespace spike
{
    class FatWindow
    {
    public:
        FatWindow(hal::BlockDevice& device, infra::ByteRange buffer);
        FatWindow(const FatWindow& other) = delete;
        FatWindow& operator=(const FatWindow& other) = delete;
        ~FatWindow() = default;

        hal::BlockDevice& Device() const;
        void Load(uint32_t sector, const infra::Function<void(fs::Result result)>& onDone);
        infra::ConstByteRange Bytes() const;
        void Forget();

    private:
        void Loaded(hal::BlockDevice::Result result);

    private:
        static constexpr uint32_t none = 0xffffffff;

        hal::BlockDevice& device;
        infra::ByteRange buffer;
        uint32_t loaded{ none };
        uint32_t requested{ none };
        infra::AutoResetFunction<void(fs::Result result)> onDone;
    };

    class FatChain
    {
    public:
        FatChain(const FatLayout& layout, FatWindow& window);
        FatChain(const FatChain& other) = delete;
        FatChain& operator=(const FatChain& other) = delete;
        ~FatChain() = default;

        void Next(uint32_t cluster, const infra::Function<void(fs::Result result, uint32_t next)>& onDone);

    private:
        void Load();
        void Collect(fs::Result result);
        void Decode();

    private:
        const FatLayout& layout;
        FatWindow& window;
        uint32_t cluster{ 0 };
        uint32_t offset{ 0 };
        uint32_t value{ 0 };
        uint8_t collected{ 0 };
        infra::AutoResetFunction<void(fs::Result result, uint32_t next)> onDone;
    };
}

#endif
