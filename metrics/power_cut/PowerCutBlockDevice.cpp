#include "metrics/power_cut/PowerCutBlockDevice.hpp"
#include <algorithm>

namespace metrics
{
    PowerCutBlockDevice::PowerCutBlockDevice(hal::BlockDevice& device, infra::ByteRange storage)
        : device(device)
        , storage(storage)
    {}

    uint32_t PowerCutBlockDevice::BlockSize() const
    {
        return device.BlockSize();
    }

    uint32_t PowerCutBlockDevice::NumberOfBlocks() const
    {
        return device.NumberOfBlocks();
    }

    void PowerCutBlockDevice::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        if (!poweredDown)
            device.ReadBlocks(buffer, firstBlock, onDone);
    }

    void PowerCutBlockDevice::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        if (poweredDown)
            return;

        auto blocks = static_cast<uint32_t>(buffer.size() / BlockSize());
        if (CutsWithin(blocks))
        {
            CutWrite(buffer, firstBlock);
            return;
        }

        RememberContentAtFlush(firstBlock, blocks);
        writeSteps += blocks;
        device.WriteBlocks(buffer, firstBlock, onDone);
    }

    void PowerCutBlockDevice::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        if (poweredDown)
            return;

        if (CutsWithin(1))
        {
            CutDiscard(beginBlock, endBlock);
            return;
        }

        if (beginBlock <= endBlock)
            RememberContentAtFlush(beginBlock, endBlock - beginBlock);

        ++writeSteps;
        device.EraseBlocks(beginBlock, endBlock, onDone);
    }

    void PowerCutBlockDevice::Flush(const infra::Function<void(Result)>& onDone)
    {
        if (poweredDown)
            return;

        onFlushDone = onDone;
        device.Flush([this](Result result)
            {
                if (result == Result::success)
                    contentAtFlush.clear();

                onFlushDone(result);
            });
    }

    void PowerCutBlockDevice::Arm(const BlockCut& newCut)
    {
        cut = newCut;
    }

    void PowerCutBlockDevice::Disarm()
    {
        cut = std::nullopt;
    }

    uint64_t PowerCutBlockDevice::WriteSteps() const
    {
        return writeSteps;
    }

    bool PowerCutBlockDevice::PoweredDown() const
    {
        return poweredDown;
    }

    bool PowerCutBlockDevice::CutsWithin(uint64_t steps) const
    {
        return cut && cut->step >= writeSteps && cut->step < writeSteps + steps;
    }

    void PowerCutBlockDevice::CutWrite(infra::ConstByteRange buffer, uint32_t firstBlock)
    {
        auto completedBlocks = static_cast<uint32_t>(cut->step - writeSteps);
        auto cutBlock = firstBlock + completedBlocks;

        if (cutBlock < NumberOfBlocks())
        {
            RememberContentAtFlush(firstBlock, completedBlocks + 1);
            std::copy(buffer.begin(), buffer.begin() + completedBlocks * BlockSize(), Block(firstBlock).begin());

            auto source = infra::DiscardHead(buffer, completedBlocks * BlockSize());
            if (cut->mode == InterruptedBlock::written)
                std::copy(source.begin(), source.begin() + BlockSize(), Block(cutBlock).begin());
            else if (cut->mode == InterruptedBlock::torn)
                Tear(source, cutBlock);
        }

        PowerDown();
    }

    void PowerCutBlockDevice::Tear(infra::ConstByteRange source, uint32_t block)
    {
        auto destination = Block(block);
        auto tornBytes = std::min(cut->tornBytes.value_or(BlockSize() / 2), BlockSize());

        std::copy(source.begin(), source.begin() + tornBytes, destination.begin());
        std::fill(destination.begin() + tornBytes, destination.end(), tornPattern);
    }

    void PowerCutBlockDevice::CutDiscard(uint32_t beginBlock, uint32_t endBlock)
    {
        if (cut->mode != InterruptedBlock::unwritten && beginBlock <= endBlock && endBlock <= NumberOfBlocks())
        {
            RememberContentAtFlush(beginBlock, endBlock - beginBlock);
            std::fill(Block(beginBlock).begin(), Block(beginBlock).begin() + (endBlock - beginBlock) * BlockSize(), 0x00);
        }

        PowerDown();
    }

    void PowerCutBlockDevice::PowerDown()
    {
        if (cut->loseUnflushedWrites)
            for (const auto& [block, content] : contentAtFlush)
                std::copy(content.begin(), content.end(), Block(block).begin());

        writeSteps = cut->step + 1;
        poweredDown = true;
    }

    void PowerCutBlockDevice::RememberContentAtFlush(uint32_t firstBlock, uint32_t blocks)
    {
        if (firstBlock > NumberOfBlocks() || blocks > NumberOfBlocks() - firstBlock)
            return;

        for (auto block = firstBlock; block != firstBlock + blocks; ++block)
            if (contentAtFlush.count(block) == 0)
                contentAtFlush.emplace(block, std::vector<uint8_t>(Block(block).begin(), Block(block).end()));
    }

    infra::ByteRange PowerCutBlockDevice::Block(uint32_t block) const
    {
        return infra::ByteRange(storage.begin() + block * BlockSize(), storage.begin() + (block + 1) * BlockSize());
    }
}
