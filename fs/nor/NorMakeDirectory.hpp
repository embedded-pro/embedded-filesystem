#ifndef FS_NOR_NOR_MAKE_DIRECTORY_HPP
#define FS_NOR_NOR_MAKE_DIRECTORY_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorCreate.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class NorVolume;

    class NorMakeDirectory
    {
    public:
        NorMakeDirectory(NorVolume& volume, NorMakeDirectoryCall&& call, const infra::Function<void(Result result)>& onFinished);
        NorMakeDirectory(const NorMakeDirectory& other) = delete;
        NorMakeDirectory& operator=(const NorMakeDirectory& other) = delete;
        ~NorMakeDirectory();

        void Start();

    private:
        NorMakeDirectoryCall call;
        infra::Function<void(Result result)> onFinished;
        NorCreate create;
    };
}

#endif
