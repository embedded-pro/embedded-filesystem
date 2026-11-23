#pragma once

#include "fs/fat/FatPathSlot.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"

namespace fs
{
    class FatDirectory;
    class FatVolume;

    class FatOpenDirectory
    {
    public:
        using Allocate = infra::Function<infra::SharedPtr<FatDirectory>()>;

        FatOpenDirectory(FatVolume& volume, FatOpenDirectoryCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished);
        FatOpenDirectory(const FatOpenDirectory& other) = delete;
        FatOpenDirectory& operator=(const FatOpenDirectory& other) = delete;
        ~FatOpenDirectory();

        void Start();

    private:
        void OnFound(Result result);
        Result Verify(Result result) const;

    private:
        FatVolume& volume;
        FatOpenDirectoryCall call;
        Allocate allocate;
        infra::Function<void(Result result)> onFinished;
    };
}
