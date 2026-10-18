#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace fs
{
    SparseBlockDevice::SparseBlockDevice(uint32_t numberOfBlocks, uint32_t blockSize, infra::MemoryRange<uint32_t> blockNumbers, infra::ByteRange contents)
        : numberOfBlocks(numberOfBlocks)
        , blockSize(blockSize)
        , blockNumbers(blockNumbers)
        , contents(contents)
    {
        really_assert(contents.size() == blockNumbers.size() * blockSize);
        std::ranges::fill(blockNumbers, unused);
    }

    uint32_t SparseBlockDevice::BlockSize() const
    {
        return blockSize;
    }

    uint32_t SparseBlockDevice::NumberOfBlocks() const
    {
        return numberOfBlocks;
    }

    void SparseBlockDevice::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        really_assert(buffer.size() % blockSize == 0);
        ++readRequests;
        bufferAddresses |= std::bit_cast<std::uintptr_t>(buffer.begin());
        if (observer)
            observer(Request::read, firstBlock);

        if (!Start(firstBlock, static_cast<uint32_t>(buffer.size() / blockSize), onDone))
            return;

        for (uint32_t index = 0; index != buffer.size() / blockSize; ++index)
            ReadBlock(firstBlock + index, infra::Head(infra::DiscardHead(buffer, index * blockSize), blockSize));

        Complete(Result::success);
    }

    void SparseBlockDevice::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        really_assert(buffer.size() % blockSize == 0);
        ++writeRequests;
        bufferAddresses |= std::bit_cast<std::uintptr_t>(buffer.begin());
        if (observer)
            observer(Request::write, firstBlock);

        if (!Start(firstBlock, static_cast<uint32_t>(buffer.size() / blockSize), onDone))
            return;

        for (uint32_t index = 0; index != buffer.size() / blockSize; ++index)
            WriteBlock(firstBlock + index, infra::Head(infra::DiscardHead(buffer, index * blockSize), blockSize));

        Complete(Result::success);
    }

    void SparseBlockDevice::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        ++eraseRequests;
        if (observer)
            observer(Request::erase, beginBlock);

        if (!Start(beginBlock, endBlock >= beginBlock ? endBlock - beginBlock : std::numeric_limits<uint32_t>::max(), onDone))
            return;

        for (auto& block : blockNumbers)
            if (block != unused && block >= beginBlock && block < endBlock)
                block = unused;

        Complete(Result::success);
    }

    void SparseBlockDevice::Flush(const infra::Function<void(Result)>& onDone)
    {
        ++flushRequests;
        if (observer)
            observer(Request::flush, 0);

        if (Start(0, 0, onDone))
            Complete(Result::success);
    }

    uint32_t SparseBlockDevice::Blocks() const
    {
        return numberOfBlocks;
    }

    void SparseBlockDevice::Observe(const infra::Function<void(Request request, uint32_t firstBlock)>& observer)
    {
        this->observer = observer;
    }

    void SparseBlockDevice::FailNextOperationWith(Result result, uint32_t succeedingFirst)
    {
        injectedFailure = result;
        succeedBeforeFailure = succeedingFirst;
    }

    void SparseBlockDevice::ReadBlock(uint32_t block, infra::ByteRange out) const
    {
        really_assert(out.size() == blockSize && block < numberOfBlocks);

        if (auto slot = SlotOf(block); slot.has_value())
            infra::Copy(infra::MakeConst(Contents(*slot)), out);
        else
            std::ranges::fill(out, uint8_t{ 0 });
    }

    void SparseBlockDevice::WriteBlock(uint32_t block, infra::ConstByteRange data)
    {
        really_assert(data.size() == blockSize && block < numberOfBlocks);

        if (std::ranges::all_of(data, [](uint8_t byte)
                {
                    return byte == 0;
                }))
        {
            Free(block);
            return;
        }

        auto slot = SlotOf(block);

        if (!slot.has_value())
            slot = SlotOf(unused);

        really_assert(slot.has_value());
        blockNumbers[*slot] = block;
        infra::Copy(data, Contents(*slot));
    }

    std::size_t SparseBlockDevice::StoredBlocks() const
    {
        return static_cast<std::size_t>(std::ranges::count_if(blockNumbers, [](uint32_t block)
            {
                return block != unused;
            }));
    }

    uint32_t SparseBlockDevice::ReadRequests() const
    {
        return readRequests;
    }

    uint32_t SparseBlockDevice::WriteRequests() const
    {
        return writeRequests;
    }

    uint32_t SparseBlockDevice::EraseRequests() const
    {
        return eraseRequests;
    }

    uint32_t SparseBlockDevice::FlushRequests() const
    {
        return flushRequests;
    }

    std::size_t SparseBlockDevice::BufferAlignment() const
    {
        return std::size_t{ 1 } << std::countr_zero(bufferAddresses | maximumBufferAlignment);
    }

    bool SparseBlockDevice::Start(uint32_t firstBlock, uint32_t blocks, const infra::Function<void(Result)>& onDone)
    {
        really_assert(!this->onDone);
        this->onDone = onDone;

        if (firstBlock > numberOfBlocks || blocks > numberOfBlocks - firstBlock)
        {
            Complete(Result::outOfRange);
            return false;
        }

        if (injectedFailure && succeedBeforeFailure != 0)
            --succeedBeforeFailure;
        else if (injectedFailure)
        {
            Complete(*std::exchange(injectedFailure, std::nullopt));
            return false;
        }

        return true;
    }

    std::optional<std::size_t> SparseBlockDevice::SlotOf(uint32_t block) const
    {
        auto position = std::ranges::find(blockNumbers, block);

        if (position == blockNumbers.end())
            return std::nullopt;

        return static_cast<std::size_t>(position - blockNumbers.begin());
    }

    infra::ByteRange SparseBlockDevice::Contents(std::size_t slot) const
    {
        return infra::Head(infra::DiscardHead(contents, slot * blockSize), blockSize);
    }

    void SparseBlockDevice::Free(uint32_t block)
    {
        if (auto slot = SlotOf(block); slot.has_value())
            blockNumbers[*slot] = unused;
    }

    void SparseBlockDevice::Complete(Result result)
    {
        infra::EventDispatcher::Instance().Schedule([this, result]
            {
                onDone(result);
            });
    }
}
