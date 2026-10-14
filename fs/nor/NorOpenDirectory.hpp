#ifndef FS_NOR_NOR_OPEN_DIRECTORY_HPP
#define FS_NOR_NOR_OPEN_DIRECTORY_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"

namespace fs
{
    class NorDirectory;
    class NorVolume;

    class NorOpenDirectory
    {
    public:
        using Allocate = infra::Function<infra::SharedPtr<NorDirectory>()>;

        NorOpenDirectory(NorVolume& volume, NorOpenDirectoryCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished);
        NorOpenDirectory(const NorOpenDirectory& other) = delete;
        NorOpenDirectory& operator=(const NorOpenDirectory& other) = delete;
        ~NorOpenDirectory();

        void Start();

    private:
        void OnFound(Result result);

    private:
        NorVolume& volume;
        NorOpenDirectoryCall call;
        Allocate allocate;
        infra::Function<void(Result result)> onFinished;
    };
}

#endif
