#ifndef FS_FAT_FAT_FILE_HPP
#define FS_FAT_FAT_FILE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatExtentCache.hpp"
#include "fs/fat/FatOpenFile.hpp"
#include "fs/fat/FatVolume.hpp"
#include "fs/interfaces/File.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <cstdint>

namespace fs
{
    class FatFile
        : public File
        , public PooledHandle
    {
    public:
        template<std::size_t Extents>
        class WithExtents;

        FatFile(FatVolume& volume, FatExtentCache& extents);
        FatFile(const FatFile& other) = delete;
        FatFile& operator=(const FatFile& other) = delete;
        ~FatFile();

        void Open(const FatScannedEntry& scanned, OpenMode mode);

        uint32_t Size() const override;
        uint32_t Position() const override;
        void Seek(uint32_t position) override;
        void Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone) override;
        void Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone) override;
        void Truncate(const infra::Function<void(Result result)>& onDone) override;
        void ExpectSize(uint32_t size, const infra::Function<void(Result result)>& onDone) override;
        void Sync(const infra::Function<void(Result result)>& onDone) override;
        void Close(const infra::Function<void(Result result)>& onDone) override;

    private:
        void UnReferenced() override;
        void StartRead();
        void ReadDone(Result result, std::size_t bytesRead);
        Result Unwritable() const;
        void Forget();

    private:
        FatVolume& volume;
        FatExtentCache& extents;
        OperationQueue::Claimer claimer;
        FatOpenFile state;
        uint32_t generation{ 0 };
        bool open{ false };
        infra::ByteRange readBuffer;
        infra::SharedPtr<FatFile> self;
        infra::AutoResetFunction<void(Result result, std::size_t bytesRead)> onRead;
    };

    namespace detail
    {
        template<std::size_t Extents>
        struct FatFileExtents
        {
            FatExtentCache::WithStorage<Extents> extentsStorage;
        };
    }

    template<std::size_t Extents>
    class FatFile::WithExtents
        : private detail::FatFileExtents<Extents>
        , public FatFile
    {
    public:
        explicit WithExtents(FatVolume& volume)
            : FatFile(volume, this->extentsStorage)
        {}
    };
}

#endif
