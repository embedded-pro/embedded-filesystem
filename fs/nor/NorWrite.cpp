#include "fs/nor/NorWrite.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/NorVolume.hpp"
#include <algorithm>
#include <limits>

namespace fs
{
    NorWrite::NorWrite(NorVolume& volume, NorOpenFile& file, infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone)
        : volume(volume)
        , file(file)
        , data(data)
        , onDone(onDone)
    {}

    NorWrite::~NorWrite() = default;

    void NorWrite::Start()
    {
        if (file.mode == OpenMode::append || file.position == file.size)
        {
            Next();
            return;
        }

        auto overwritten = infra::Head(data, file.size - file.position);
        data = infra::DiscardHead(data, overwritten.size());
        volume.Operations().overwrite(volume, file, overwritten, [this](Result result)
            {
                if (result == Result::success)
                    Next();
                else
                    onDone(result);
            });
    }

    void NorWrite::Next()
    {
        auto more = true;
        while (more)
            more = Step();
    }

    bool NorWrite::Step()
    {
        if (data.empty())
            onDone(Result::success);
        else if (file.size == std::numeric_limits<uint32_t>::max())
            onDone(Result::noSpace);
        else
        {
            auto offset = file.size % volume.Geometry().PayloadSize();
            if (offset == 0)
            {
                file.tailBlock = std::nullopt;
                file.ownsTailBlock = false;
            }

            if (file.tailBlock.has_value())
                return AppendToPage();

            if (offset < volume.Config().inlineBytes)
            {
                Append(volume.Config().inlineBytes - offset);
                return true;
            }

            AllocateTail(file.size / volume.Geometry().PayloadSize());
        }

        return false;
    }

    bool NorWrite::AppendToPage()
    {
        auto boundary = NextBoundary();
        auto appended = Append(boundary - file.size);
        if (file.size != boundary)
            return true;

        FlushTo(boundary, appended);
        return false;
    }

    void NorWrite::AllocateTail(uint32_t index)
    {
        if (!NorRadixTree::CanMap(index, volume.Geometry().nodeSize))
            onDone(Result::noSpace);
        else if (!file.Admits(index, volume.Geometry().PayloadSize(), NorRadixFanout(volume.Geometry().nodeSize)))
            volume.FileCommit().Commit(file, NorReserve::kept, [this, index](Result result)
                {
                    if (result == Result::success)
                        AllocateTail(index);
                    else
                        onDone(result);
                });
        else
            volume.DataAllocator().Allocate(NorBlockOwner{ file.inode, index }, NorReserve::kept, [this, index](std::optional<uint32_t> block)
                {
                    TailAllocated(block, index);
                });
    }

    void NorWrite::TailAllocated(std::optional<uint32_t> block, uint32_t index)
    {
        if (!block.has_value())
        {
            onDone(Result::noSpace);
            return;
        }

        file.entries.push_back(NorBlockEntry{ index, 0, block.value_or(0) });
        file.tailBlock = block;
        file.ownsTailBlock = true;

        auto boundary = LastBoundary();
        if (boundary > file.programmed)
            FlushTo(boundary, 0);
        else
            Next();
    }

    void NorWrite::FlushTo(uint32_t to, uint32_t appended)
    {
        volume.FileFlush().Flush(file, to, NorReserve::kept, [this, appended](Result result)
            {
                if (result == Result::success)
                    Next();
                else
                {
                    file.size -= appended;
                    file.position = file.size;
                    onDone(result);
                }
            });
    }

    uint32_t NorWrite::Append(uint32_t count)
    {
        auto taken = static_cast<uint32_t>(std::min<std::size_t>({ data.size(), count, std::numeric_limits<uint32_t>::max() - file.size }));
        std::ranges::copy(infra::Head(data, taken), file.buffer.begin() + (file.size - file.programmed));
        data = infra::DiscardHead(data, taken);
        file.size += taken;
        file.position = file.size;
        return taken;
    }

    uint32_t NorWrite::BlockStart() const
    {
        auto payload = volume.Geometry().PayloadSize();
        return file.size / payload * payload;
    }

    uint32_t NorWrite::DeviceOffset() const
    {
        return volume.Geometry().HeaderSize() + file.size - BlockStart();
    }

    uint32_t NorWrite::NextBoundary() const
    {
        const auto& geometry = volume.Geometry();
        auto next = std::min((DeviceOffset() / geometry.pageSize + 1) * geometry.pageSize, geometry.BlockSize());
        return BlockStart() + next - geometry.HeaderSize();
    }

    uint32_t NorWrite::LastBoundary() const
    {
        const auto& geometry = volume.Geometry();
        auto last = DeviceOffset() / geometry.pageSize * geometry.pageSize;
        if (last <= geometry.HeaderSize())
            return file.programmed;

        return BlockStart() + last - geometry.HeaderSize();
    }
}
