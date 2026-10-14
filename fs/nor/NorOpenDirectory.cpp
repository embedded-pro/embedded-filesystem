#include "fs/nor/NorOpenDirectory.hpp"
#include "fs/nor/NorDirectory.hpp"
#include "fs/nor/NorVolume.hpp"

namespace fs
{
    NorOpenDirectory::NorOpenDirectory(NorVolume& volume, NorOpenDirectoryCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , allocate(allocate)
        , onFinished(onFinished)
    {}

    NorOpenDirectory::~NorOpenDirectory() = default;

    void NorOpenDirectory::Start()
    {
        volume.Walk().Find(volume.State().inodeTable, call.path, [this](Result result)
            {
                OnFound(result);
            });
    }

    void NorOpenDirectory::OnFound(Result result)
    {
        onFinished(result);

        const auto& found = volume.Walk().Found();
        if (result == Result::success && found.type != NorInodeType::directory)
            result = Result::notADirectory;

        infra::SharedPtr<NorDirectory> directory;
        if (result == Result::success)
            directory = allocate();

        if (directory != nullptr)
            directory->Open(found.inode);
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        call.onDone(result, directory);
    }
}
