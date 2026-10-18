#ifndef FS_FAT_FAT_OPEN_HPP
#define FS_FAT_FAT_OPEN_HPP

#include "fs/fat/FatEntryAssembler.hpp"
#include "fs/fat/FatParentDirectory.hpp"
#include "fs/fat/FatPathSlot.hpp"
#include "fs/fat/FatTruncation.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstdint>

namespace fs
{
    class FatFile;
    class FatVolume;

    class FatOpen
    {
    public:
        using Allocate = infra::Function<infra::SharedPtr<FatFile>()>;

        FatOpen(FatVolume& volume, FatOpenCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished);
        FatOpen(const FatOpen& other) = delete;
        FatOpen& operator=(const FatOpen& other) = delete;
        ~FatOpen();

        void Start();

    private:
        enum class Step : uint8_t
        {
            found,
            truncated,
            prepared,
            created
        };

        void OnFound(Result result);
        void OpenExisting(const FatScannedEntry& found);
        Result VerifyFile(const FatScannedEntry& found) const;
        void OnTruncated(Result result);
        void Create();
        void OnPrepared(Result result);
        void OnCreated(Result result);
        void Finish(Result result);
        const infra::Function<void(Result result)>& Then(Step next);
        void Resume(Result result);

    private:
        FatVolume& volume;
        FatOpenCall call;
        Allocate allocate;
        infra::Function<void(Result result)> onFinished;
        FatParentDirectory parent;
        FatTruncation truncation;
        infra::SharedPtr<FatFile> file;
        Step step{ Step::found };
        infra::Function<void(Result result)> resume;
    };
}

#endif
