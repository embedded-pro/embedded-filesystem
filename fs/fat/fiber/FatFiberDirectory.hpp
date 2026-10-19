#ifndef FS_FAT_FIBER_FAT_FIBER_DIRECTORY_HPP
#define FS_FAT_FIBER_FAT_FIBER_DIRECTORY_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/fat/FatDirectoryReader.hpp"
#include "fs/fat/fiber/FatFiberVolume.hpp"
#include "fs/interfaces/Directory.hpp"
#include "infra/event/ClaimableResource.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatFiberDirectory
        : public Directory
        , public PooledHandle
    {
    public:
        explicit FatFiberDirectory(FatFiberVolume& volume);

        void Open(uint32_t firstCluster);
        void Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone) override;

    private:
        void UnReferenced() override;
        void RunNext();
        void NextCompleted();

    private:
        FatFiberVolume& volume;
        infra::ClaimableResource::Claimer claimer;
        uint32_t generation{ 0 };
        FatDirectoryCursor cursor;
        std::optional<EntryInfo> entry;
        Result nextResult{ Result::success };
        infra::SharedPtr<FatFiberDirectory> self;
        infra::AutoResetFunction<void(Result result, std::optional<EntryInfo> entry)> onNext;
    };
}

#endif
