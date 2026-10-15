#include "fs/nor/NorMakeDirectory.hpp"

namespace fs
{
    NorMakeDirectory::NorMakeDirectory(NorVolume& volume, NorMakeDirectoryCall&& call, const infra::Function<void(Result result)>& onFinished)
        : call(std::move(call))
        , onFinished(onFinished)
        , create(volume)
    {}

    NorMakeDirectory::~NorMakeDirectory() = default;

    void NorMakeDirectory::Start()
    {
        create.Create(
            call.path, NorInodeType::directory, []
            {
                return Result::success;
            },
            [this](Result result)
            {
                onFinished(result);
                call.onDone(result);
            });
    }
}
