#ifndef FS_NOR_NOR_UNMOUNT_HPP
#define FS_NOR_NOR_UNMOUNT_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class NorVolume;

    class NorUnmount
    {
    public:
        NorUnmount(NorVolume& volume, const infra::Function<void(Result result)>& onDone);
        NorUnmount(const NorUnmount& other) = delete;
        NorUnmount& operator=(const NorUnmount& other) = delete;
        ~NorUnmount();

        void Start() const;

    private:
        void CommitNextFile() const;
        void Checkpoint() const;

    private:
        NorVolume& volume;
        infra::Function<void(Result result)> onDone;
    };
}

#endif
