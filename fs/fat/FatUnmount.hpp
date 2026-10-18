#ifndef FS_FAT_FAT_UNMOUNT_HPP
#define FS_FAT_FAT_UNMOUNT_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>

namespace fs
{
    class FatVolume;

    class FatUnmount
    {
    public:
        FatUnmount(FatVolume& volume, const infra::Function<void(Result result)>& onDone);
        FatUnmount(const FatUnmount& other) = delete;
        FatUnmount& operator=(const FatUnmount& other) = delete;
        ~FatUnmount();

        void Start();

    private:
        enum class Step : uint8_t
        {
            fileCommitted,
            markedClean,
            flushed
        };

        void CommitNextFile();
        void FileCommitted(Result result);
        void WriteFsInfo();
        void MarkClean(Result previous);
        void Flush(Result previous);
        void Done(Result result) const;
        const infra::Function<void(Result result)>& Then(Step next);
        void Resume(Result result);

    private:
        FatVolume& volume;
        Step step{ Step::fileCommitted };
        Result beforeFlush{ Result::success };
        infra::Function<void(Result result)> onDone;
        infra::Function<void(Result result)> resume;
    };
}

#endif
