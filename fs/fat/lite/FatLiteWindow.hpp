#ifndef FS_FAT_LITE_FAT_LITE_WINDOW_HPP
#define FS_FAT_LITE_FAT_LITE_WINDOW_HPP

#include "fs/common/FlushBarrier.hpp"
#include "fs/common/SectorCache.hpp"
#include "fs/common/SectorStore.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatLiteWindow
        : public SectorStore
    {
    public:
        FatLiteWindow(FlushBarrier& barrier, infra::ByteRange buffer);
        FatLiteWindow(const FatLiteWindow& other) = delete;
        FatLiteWindow& operator=(const FatLiteWindow& other) = delete;
        ~FatLiteWindow();

        uint32_t SectorSize() const override;
        void Read(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone) override;
        void Modify(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone) override;
        void Overwrite(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone);
        void Flush(const infra::Function<void(Result result)>& onDone);

        void SetMirroredRegion(std::optional<SectorCache::MirroredRegion> region);
        void Forget();
        void Drop(uint32_t firstSector, uint32_t sectors);
        void Overlay(uint32_t firstSector, infra::ByteRange data) const;

    private:
        enum class Access : uint8_t
        {
            read,
            modify,
            overwrite,
            flush
        };

        void Start(Access access, uint32_t sector, uint8_t rank);
        void Proceed();
        void Transfer();
        void Transferred(hal::BlockDevice::Result result);
        void Deliver(Result result);
        uint8_t Copies() const;

    private:
        static constexpr uint32_t none = 0xffffffff;

        FlushBarrier& barrier;
        infra::ByteRange buffer;
        std::optional<SectorCache::MirroredRegion> mirrored;
        uint32_t loaded{ none };
        uint32_t requested{ none };
        uint8_t rank{ 0 };
        uint8_t requestedRank{ 0 };
        uint8_t copy{ 0 };
        bool dirty{ false };
        Access access{ Access::read };
        infra::AutoResetFunction<void(Result result, infra::ConstByteRange contents)> onRead;
        infra::AutoResetFunction<void(Result result, infra::ByteRange contents)> onModify;
        infra::AutoResetFunction<void(Result result)> onFlushed;
    };
}

#endif
