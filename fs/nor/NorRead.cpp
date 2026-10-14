#include "fs/nor/NorRead.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorVolume.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
        constexpr uint32_t blockNumberBytes = 4;
    }

    NorRead::NorRead(NorVolume& volume, NorOpenFile& file, infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)
        : volume(volume)
        , file(file)
        , buffer(buffer)
        , onDone(onDone)
    {}

    NorRead::~NorRead() = default;

    void NorRead::Start()
    {
        volume.InodeRead().Read(volume.State().inodeTable, file.inode, [this](Result result)
            {
                RecordRead(result);
            });
    }

    void NorRead::RecordRead(Result result)
    {
        const auto& record = volume.InodeRead().Record();
        if (result != Result::success || (!file.Writable() && record.inlineBytes.size() > record.size))
        {
            Done(result == Result::success ? Result::corrupted : result);
            return;
        }

        auto size = file.Writable() ? file.size : record.size;
        auto tail = file.Writable() ? infra::ConstByteRange(file.Pending()) : record.inlineBytes;
        readEnd = file.position + static_cast<uint32_t>(std::min<std::size_t>(buffer.size(), size - std::min(file.position, size)));
        auto inlineStart = size - static_cast<uint32_t>(tail.size());
        auto from = std::max(file.position, inlineStart);
        if (readEnd > from)
            infra::Copy(infra::Head(infra::DiscardHead(tail, from - inlineStart), readEnd - from), infra::Head(infra::DiscardHead(buffer, from - file.position), readEnd - from));

        dataEnd = std::min(readEnd, inlineStart);
        map = NorMapRoot{ record.mapRoot, record.mapHeight };
        current = file.position;
        NextBlock();
    }

    void NorRead::NextBlock()
    {
        if (current >= dataEnd)
        {
            Done(Result::success);
            return;
        }

        auto uncommitted = file.UncommittedBlock(current / volume.Geometry().PayloadSize());
        if (uncommitted.has_value())
        {
            ReadFrom(uncommitted.value_or(0));
            return;
        }

        volume.Cache().UnpinAll();
        volume.Fetch().Fetch(NorMapId{ NorMap::fileData, file.inode, blockNumberBytes }, map, current / volume.Geometry().PayloadSize(), [this](NorFetchResult result)
            {
                Fetched(result);
            });
    }

    void NorRead::Fetched(NorFetchResult result)
    {
        really_assert(result != NorFetchResult::cacheFull);

        if (result == NorFetchResult::corrupted)
        {
            Done(Result::corrupted);
            return;
        }

        const auto& geometry = volume.Geometry();
        auto block = NorRadixTree(volume.Cache(), file.inode).Find(map, current / geometry.PayloadSize());
        if (!IsDataBlock(block))
        {
            Done(Result::corrupted);
            return;
        }

        ReadFrom(block);
    }

    void NorRead::ReadFrom(uint32_t block)
    {
        const auto& geometry = volume.Geometry();
        auto offset = current % geometry.PayloadSize();
        chunk = std::min(geometry.PayloadSize() - offset, dataEnd - current);
        volume.Flash().ReadBuffer(infra::Head(infra::DiscardHead(buffer, current - file.position), chunk), geometry.AddressOfBlock(block) + geometry.HeaderSize() + offset, [this]
            {
                DataRead();
            });
    }

    void NorRead::DataRead()
    {
        current += chunk;
        NextBlock();
    }

    void NorRead::Done(Result result) const
    {
        std::size_t bytesRead = 0;
        if (result == Result::success)
        {
            bytesRead = readEnd - file.position;
            file.position = readEnd;
        }

        onDone(result, bytesRead);
    }

    bool NorRead::IsDataBlock(uint32_t block) const
    {
        return block >= superblocks && block < volume.Geometry().blocks && volume.State().blocks.State(block) == NorBlockState::inUse;
    }
}
