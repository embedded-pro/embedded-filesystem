#ifndef FS_NOR_NOR_STAT_HPP
#define FS_NOR_NOR_STAT_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class NorVolume;

    class NorStat
    {
    public:
        NorStat(NorVolume& volume, NorStatCall&& call, const infra::Function<void(Result result)>& onFinished);
        NorStat(const NorStat& other) = delete;
        NorStat& operator=(const NorStat& other) = delete;
        ~NorStat();

        void Start();

    private:
        void OnFound(Result result);

    private:
        NorVolume& volume;
        NorStatCall call;
        infra::Function<void(Result result)> onFinished;
    };
}

#endif
