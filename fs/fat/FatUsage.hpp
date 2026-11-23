#pragma once

#include "fs/fat/FatPathSlot.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"

namespace fs
{
    class FatVolume;

    class FatUsage
    {
    public:
        FatUsage(FatVolume& volume, FatUsageCall&& call, const infra::Function<void(Result result)>& onFinished);
        FatUsage(const FatUsage& other) = delete;
        FatUsage& operator=(const FatUsage& other) = delete;
        ~FatUsage();

        void Start();

    private:
        void OnStepped(Result result);
        void Complete(Result result);

    private:
        FatVolume& volume;
        FatUsageCall call;
        infra::Function<void(Result result)> onFinished;
    };
}
