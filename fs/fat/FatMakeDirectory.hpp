#ifndef FS_FAT_FAT_MAKE_DIRECTORY_HPP
#define FS_FAT_FAT_MAKE_DIRECTORY_HPP

#include "fs/fat/FatParentDirectory.hpp"
#include "fs/fat/FatPathSlot.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatVolume;

    class FatMakeDirectory
    {
    public:
        FatMakeDirectory(FatVolume& volume, FatMakeDirectoryCall&& call, const infra::Function<void(Result result)>& onFinished);
        FatMakeDirectory(const FatMakeDirectory& other) = delete;
        FatMakeDirectory& operator=(const FatMakeDirectory& other) = delete;
        ~FatMakeDirectory();

        void Start();

    private:
        enum class Step : uint8_t
        {
            found,
            prepared,
            written,
            finish
        };

        void OnFound();
        void Allocate();
        void OnAllocated(Result result, uint32_t first);
        void InitializeSector();
        void OnInitialized(Result result);
        void WriteDotEntries(infra::ByteRange contents) const;
        void Finish(Result result);
        infra::Function<void(Result result)> Then(Step next);
        void Resume(Result result);

    private:
        FatVolume& volume;
        FatMakeDirectoryCall call;
        infra::Function<void(Result result)> onFinished;
        FatParentDirectory parent;
        uint32_t cluster{ 0 };
        uint32_t sector{ 0 };
        Step step{ Step::found };
    };
}

#endif
