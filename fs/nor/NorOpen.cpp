#include "fs/nor/NorOpen.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorVolume.hpp"
#include <algorithm>
#include <utility>

namespace fs
{
    namespace
    {
        constexpr uint32_t blockNumberBytes = 4;
        constexpr uint32_t superblocks = 2;
    }

    NorOpen::NorOpen(NorVolume& volume, NorOpenCall&& call, const Allocate& allocate, const infra::Function<void(Result result)>& onFinished)
        : volume(volume)
        , call(std::move(call))
        , allocate(allocate)
        , onFinished(onFinished)
        , create(volume)
    {}

    NorOpen::~NorOpen() = default;

    void NorOpen::Start()
    {
        if (call.mode == OpenMode::read)
        {
            volume.Walk().Find(volume.State().inodeTable, call.path, [this](Result result)
                {
                    OnFound(result);
                });
            return;
        }

        create.Create(
            call.path, NorInodeType::file, [this]
            {
                return Allocated();
            },
            [this](Result result)
            {
                OnCreated(result);
            });
    }

    void NorOpen::OnFound(Result result)
    {
        const auto& found = volume.Walk().Found();
        if (result == Result::success && found.type == NorInodeType::directory)
            result = Result::isADirectory;

        if (result == Result::success)
            result = Shared(found.inode);

        if (result == Result::success)
            file = allocate();

        if (file != nullptr)
            file->Open(NorOpenFile{ found.inode, found.size, 0, call.mode, found.size });
        else if (result == Result::success)
            result = Result::tooManyOpenFiles;

        Finish(result);
    }

    void NorOpen::OnCreated(Result result)
    {
        const auto& found = create.Found();
        if (result == Result::success)
        {
            file->Open(NorOpenFile{ create.Inode(), 0, 0, call.mode });
            Finish(Result::success);
        }
        else if (result != Result::alreadyExists)
            Finish(result);
        else if (found.type == NorInodeType::directory)
            Finish(Result::isADirectory);
        else if (Shared(found.inode) != Result::success)
            Finish(Result::busy);
        else if (call.mode == OpenMode::write && found.size != 0)
            Finish(Result::unsupported);
        else if (Allocated() != Result::success)
            Finish(Result::tooManyOpenFiles);
        else
            OpenExisting(found);
    }

    void NorOpen::OpenExisting(const NorFoundInode& found)
    {
        auto inlineBytes = create.FoundInlineBytes();
        if (inlineBytes.size() > found.size)
        {
            Finish(Result::corrupted);
            return;
        }

        std::ranges::copy(inlineBytes, file->Buffer().begin());
        opened = NorOpenFile{ found.inode, found.size, 0, call.mode, found.size, found.size - static_cast<uint32_t>(inlineBytes.size()) };
        map = found.map;

        if (found.size == 0)
        {
            file->Open(opened);
            Finish(Result::success);
            return;
        }

        lastIndex = (found.size - 1) / volume.Geometry().PayloadSize();
        volume.Cache().UnpinAll();
        volume.Fetch().Fetch(NorMapId{ NorMap::fileData, found.inode, blockNumberBytes }, map, lastIndex, [this](NorFetchResult result)
            {
                TailFetched(result);
            });
    }

    void NorOpen::TailFetched(NorFetchResult result)
    {
        if (result != NorFetchResult::fetched)
        {
            Finish(Result::corrupted);
            return;
        }

        const auto& geometry = volume.Geometry();
        auto block = NorRadixTree(volume.Cache(), opened.inode).Find(map, lastIndex);
        auto lastLength = opened.size - lastIndex * geometry.PayloadSize();
        auto inlineLength = opened.size - opened.programmed;
        auto consistent = block == 0 ? inlineLength == lastLength : inlineLength < geometry.programUnit && (lastLength - inlineLength) % geometry.programUnit == 0 && IsDataBlock(block);

        if (!consistent)
        {
            Finish(Result::corrupted);
            return;
        }

        if (block != 0)
            opened.tailBlock = block;

        file->Open(opened);
        Finish(Result::success);
    }

    bool NorOpen::IsDataBlock(uint32_t block) const
    {
        return block >= superblocks && block < volume.Geometry().blocks && volume.State().blocks.State(block) == NorBlockState::inUse;
    }

    Result NorOpen::Allocated()
    {
        if (file == nullptr)
            file = allocate();

        return file != nullptr ? Result::success : Result::tooManyOpenFiles;
    }

    Result NorOpen::Shared(uint32_t inode) const
    {
        for (const auto& open : volume.OpenFiles())
            if (open.State().inode == inode && (open.State().mode != OpenMode::read || call.mode != OpenMode::read))
                return Result::busy;

        return Result::success;
    }

    void NorOpen::Finish(Result result)
    {
        volume.Cache().UnpinAll();
        onFinished(result);
        auto handed = std::exchange(file, nullptr);
        if (result != Result::success)
            handed = nullptr;

        call.onDone(result, handed);
    }
}
