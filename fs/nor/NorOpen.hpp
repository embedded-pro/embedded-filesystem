#ifndef FS_NOR_NOR_OPEN_HPP
#define FS_NOR_NOR_OPEN_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"

namespace fs
{
    class NorFile;
    class NorVolume;

    class NorOpen
    {
    public:
        using Allocate = infra::Function<infra::SharedPtr<NorFile>()>;

        NorOpen(NorVolume& volume, NorOpenCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished);
        NorOpen(const NorOpen& other) = delete;
        NorOpen& operator=(const NorOpen& other) = delete;
        ~NorOpen();

        void Start();

    private:
        void OnFound(Result result);

    private:
        NorVolume& volume;
        NorOpenCall call;
        Allocate allocate;
        infra::Function<void(Result result)> onFinished;
    };
}

#endif
