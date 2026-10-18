#ifndef FS_COMMON_SECTOR_CACHE_HPP
#define FS_COMMON_SECTOR_CACHE_HPP

#include "fs/common/FlushBarrier.hpp"
#include "fs/common/SectorStore.hpp"
#include "fs/common/TransferAlignment.hpp"
#include "fs/interfaces/Result.hpp"
#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/IntrusiveList.hpp"
#include "infra/util/MemoryRange.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class SectorCache
        : public SectorStore
    {
    public:
        struct Entry
            : infra::IntrusiveList<Entry>::NodeType
        {
            uint32_t sector{ 0 };
            uint8_t rank{ 0 };
            bool valid{ false };
            bool dirty{ false };
        };

        struct MirroredRegion
        {
            uint32_t firstSector;
            uint32_t sectors;
            uint8_t copies;
        };

        template<std::size_t Sectors, std::size_t BytesPerSector = 512>
        class WithStorage;

        SectorCache(FlushBarrier& barrier, infra::MemoryRange<Entry> entries, infra::ByteRange buffers);
        SectorCache(const SectorCache& other) = delete;
        SectorCache& operator=(const SectorCache& other) = delete;
        ~SectorCache() = default;

        uint32_t SectorSize() const final;
        void SetMirroredRegion(std::optional<MirroredRegion> region);

        void Read(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone) final;
        void ReadOnce(uint32_t sector, const infra::Function<void(Result result, infra::ConstByteRange contents)>& onDone);
        void Modify(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone) final;
        void Overwrite(uint32_t sector, uint8_t rank, const infra::Function<void(Result result, infra::ByteRange contents)>& onDone);
        void WriteBack(const infra::Function<void(Result result)>& onDone);
        void Flush(const infra::Function<void(Result result)>& onDone);

        void Discard(uint32_t beginSector, uint32_t endSector);
        void Overlay(uint32_t firstSector, infra::ByteRange buffer) const;
        void Clear();

    private:
        enum class Request : uint8_t
        {
            none,
            read,
            readOnce,
            modify,
            overwrite,
            writeBack,
            flush
        };

        enum class Step : uint8_t
        {
            load,
            write,
            flush
        };

        void Start(Request newRequest, uint32_t sector, uint8_t rank);
        void Access();
        void Hit();
        void Miss();
        void Load();
        void OnLoaded(hal::BlockDevice::Result deviceResult);
        void Available();
        void MarkModified();
        void WriteBackAll();
        void WriteNextModified();
        void AllWritten();
        void WriteRunOf(Entry& entry);
        void Transfer(Step next);
        void Transferred(hal::BlockDevice::Result deviceResult);
        void OnWritten(hal::BlockDevice::Result deviceResult);
        void Complete(Result completionResult);
        void Deliver();

        Entry& SlotFor(uint32_t sector);
        Entry* AfterPrevious(uint32_t sector) const;
        Entry* FirstAvailable(Entry* from) const;
        void Relocate();
        std::size_t IndexOf(const Entry& entry) const;
        void Touch(Entry& entry);
        void MakeLeastRecent(Entry& entry);
        infra::ByteRange BufferOf(const Entry& entry) const;

    private:
        FlushBarrier& barrier;
        infra::MemoryRange<Entry> entries;
        infra::ByteRange buffers;
        uint32_t sectorSize;
        infra::IntrusiveList<Entry> leastRecentlyUsed;
        std::optional<MirroredRegion> mirroredRegion;

        Request request{ Request::none };
        uint32_t requestedSector{ 0 };
        uint8_t requestedRank{ 0 };
        Entry* target{ nullptr };
        Entry* relocation{ nullptr };
        infra::MemoryRange<Entry> writing;
        uint8_t copy{ 0 };
        bool writingVictim{ false };
        Step step{ Step::load };
        Result result{ Result::success };

        infra::AutoResetFunction<void(Result result, infra::ConstByteRange contents)> onRead;
        infra::AutoResetFunction<void(Result result, infra::ByteRange contents)> onModify;
        infra::AutoResetFunction<void(Result result)> onWriteBack;
    };

    namespace detail
    {
        template<std::size_t Sectors, std::size_t BytesPerSector>
        struct SectorCacheStorage
        {
            std::array<SectorCache::Entry, Sectors> entryStorage;
            alignas(bufferAlignment) std::array<uint8_t, Sectors * BytesPerSector> bufferStorage;
        };
    }

    template<std::size_t Sectors, std::size_t BytesPerSector>
    class SectorCache::WithStorage
        : private detail::SectorCacheStorage<Sectors, BytesPerSector>
        , public SectorCache
    {
        static_assert(Sectors > 0 && BytesPerSector > 0);

    public:
        explicit WithStorage(FlushBarrier& barrier)
            : SectorCache(barrier, infra::MakeRange(this->entryStorage), infra::MakeRange(this->bufferStorage))
        {}
    };
}

#endif
