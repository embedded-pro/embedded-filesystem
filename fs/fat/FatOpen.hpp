#ifndef FS_FAT_FAT_OPEN_HPP
#define FS_FAT_FAT_OPEN_HPP

#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatPathSlot.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"

namespace fs
{
    class FatFile;
    class FatVolume;

    class FatOpen
    {
    public:
        using Allocate = infra::Function<infra::SharedPtr<FatFile>()>;

        FatOpen(FatVolume& volume, const FatOpenCall& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished);
        FatOpen(const FatOpen& other) = delete;
        FatOpen& operator=(const FatOpen& other) = delete;
        ~FatOpen();

        void Start();

    private:
        void OnFound(Result result);
        Result Verify(Result result) const;
        Result VerifyFile(const FatScannedEntry& found) const;

    private:
        FatVolume& volume;
        FatOpenCall call;
        Allocate allocate;
        infra::Function<void(Result result)> onFinished;
    };
}

#endif
