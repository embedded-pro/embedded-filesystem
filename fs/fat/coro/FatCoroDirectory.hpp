#ifndef FS_FAT_CORO_FAT_CORO_DIRECTORY_HPP
#define FS_FAT_CORO_FAT_CORO_DIRECTORY_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/coro/Coroutine.hpp"
#include "fs/fat/coro/FatCoroVolume.hpp"
#include "fs/interfaces/Directory.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class FatCoroDirectory
        : public Directory
        , public PooledHandle
    {
    public:
        static constexpr std::size_t frameBytes = sizeof(void*) == 4 ? 192 : 2048;

        explicit FatCoroDirectory(FatCoroVolume& volume);

        void Open(uint32_t firstCluster);
        void Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone) override;

        std::size_t FramePeak() const;

    private:
        void UnReferenced() override;
        coro::Detached RunNext(coro::Frames& frames);
        void NextCompleted();

    private:
        FatCoroVolume& volume;
        infra::ClaimableResource::Claimer claimer;
        coro::FrameStorage<frameBytes> frameStorage;
        coro::Frames frames{ infra::MakeRange(frameStorage.bytes) };
        uint32_t generation{ 0 };
        FatDirectoryCursor cursor;
        std::optional<EntryInfo> entry;
        Result nextResult{ Result::success };
        infra::SharedPtr<FatCoroDirectory> self;
        infra::AutoResetFunction<void(Result result, std::optional<EntryInfo> entry)> onNext;
    };
}

#endif
