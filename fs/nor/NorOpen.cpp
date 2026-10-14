#include "fs/nor/NorOpen.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorVolume.hpp"

namespace fs
{
    NorOpen::NorOpen(NorVolume& volume, NorOpenCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , allocate(allocate)
        , onFinished(onFinished)
    {}

    NorOpen::~NorOpen() = default;

    void NorOpen::Start()
    {
        volume.Walk().Find(volume.State().inodeTable, call.path, [this](Result result)
            {
                OnFound(result);
            });
    }

    void NorOpen::OnFound(Result result)
    {
        onFinished(result);

        const auto& found = volume.Walk().Found();
        if (result == Result::success && found.type == NorInodeType::directory)
            result = Result::isADirectory;

        infra::SharedPtr<NorFile> file;
        if (result == Result::success)
            file = allocate();

        if (file != nullptr)
            file->Open(found.inode, found.size, call.mode);
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        call.onDone(result, file);
    }
}
