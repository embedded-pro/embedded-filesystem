#ifndef FS_FAT_FAT_UNMOUNT_HPP
#define FS_FAT_FAT_UNMOUNT_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"

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

        void Start() const;

    private:
        void CommitNextFile() const;
        void WriteFsInfo() const;
        void MarkClean(Result previous) const;
        void Flush(Result previous) const;
        void Done(Result result) const;

    private:
        FatVolume& volume;
        infra::Function<void(Result result)> onDone;
    };
}

#endif
