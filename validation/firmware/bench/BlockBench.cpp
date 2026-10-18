#include "validation/firmware/bench/BlockBench.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace validation
{
    bool BlockBench::BlocksFit(const CountingBlockDevice& device, uint32_t blocks, uint32_t repetitions)
    {
        return Repeatable(repetitions) && blocks >= 1 && blocks <= maximumBlocks && blocks <= device.NumberOfBlocks();
    }

    bool BlockBench::UnitsFit(const CountingBlockDevice& device, uint32_t units, uint32_t repetitions)
    {
        return Repeatable(repetitions) && units >= 1 && units <= maximumUnits && units * repetitions <= maximumRepetitions && units * device.UnitBlocks() <= device.NumberOfBlocks();
    }

    bool BlockBench::FlushFits(const CountingBlockDevice& device, uint32_t repetitions)
    {
        return Repeatable(repetitions) && device.NumberOfBlocks() != 0;
    }

    bool BlockBench::DiscardFits(const CountingBlockDevice& device, uint32_t blocks, uint32_t repetitions)
    {
        return Repeatable(repetitions) && blocks >= 1 && blocks <= device.NumberOfBlocks();
    }

    BlockBench::BlockBench(ResultLines& results, uint32_t clockHz)
        : Benchmark(results, clockHz)
    {
        for (std::size_t position = 0; position != buffer.size(); ++position)
            buffer[position] = static_cast<uint8_t>(position * 7 + 1);
    }

    void BlockBench::Read(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t blocks, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(BlocksFit(device, blocks, repetitions));

        this->blocks = blocks;
        Start(device, { name, "read", blocks * device.BlockSize() }, repetitions, onDone);
        ReadNext();
    }

    void BlockBench::Write(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t blocks, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(BlocksFit(device, blocks, repetitions));

        this->blocks = blocks;
        Start(device, { name, "write", blocks * device.BlockSize() }, repetitions, onDone);
        WriteNext();
    }

    void BlockBench::Units(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t units, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(UnitsFit(device, units, repetitions));

        this->units = units;
        Start(device, { name, "units", device.BlockSize() }, units * repetitions, onDone);
        UnitNext();
    }

    // A block is written before each flush, so that the flush has data to write back
    void BlockBench::Flush(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(FlushFits(device, repetitions));

        blocks = 1;
        Start(device, { name, "flush", device.BlockSize() }, repetitions, onDone);
        FlushNext();
    }

    // The first block of each range is written before it is discarded, so that the discard has data to drop
    void BlockBench::Discard(CountingBlockDevice& device, infra::BoundedConstString name, uint32_t blocks, uint32_t repetitions, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        really_assert(DiscardFits(device, blocks, repetitions));

        this->blocks = blocks;
        Start(device, { name, "discard", blocks * device.BlockSize() }, repetitions, onDone);
        DiscardNext();
    }

    void BlockBench::Start(CountingBlockDevice& device, const Run& run, uint32_t requests, const infra::Function<void(BenchOutcome outcome)>& onDone)
    {
        Begin(device.Meter(), run, onDone);
        this->device = &device;
        this->requests = requests;
        count = 0;
    }

    infra::Function<void(hal::BlockDevice::Result)> BlockBench::Then(Step next, bool measured)
    {
        resumeWith = next;
        if (measured)
            return [this](hal::BlockDevice::Result result)
            {
                Resume(result, true);
            };

        return [this](hal::BlockDevice::Result result)
        {
            Resume(result, false);
        };
    }

    void BlockBench::Resume(hal::BlockDevice::Result result, bool measured)
    {
        if (Expired())
            Stop();
        else if (result != hal::BlockDevice::Result::success)
            Finish(BenchOutcome::failed);
        else
        {
            if (measured)
                Measure();
            (this->*resumeWith)();
        }
    }

    infra::ByteRange BlockBench::Buffer(uint32_t blocks)
    {
        return infra::Head(infra::MakeRange(buffer), blocks * device->BlockSize());
    }

    uint32_t BlockBench::Position() const
    {
        return count % (device->NumberOfBlocks() / blocks) * blocks;
    }

    void BlockBench::ReadNext()
    {
        if (count == requests)
            Completed();
        else
        {
            auto position = Position();
            ++count;
            device->ReadBlocks(Buffer(blocks), position, Then(&BlockBench::ReadNext, true));
        }
    }

    void BlockBench::WriteNext()
    {
        if (count == requests)
            Completed();
        else
        {
            auto position = Position();
            ++count;
            device->WriteBlocks(Buffer(blocks), position, Then(&BlockBench::WriteNext, true));
        }
    }

    void BlockBench::UnitNext()
    {
        if (count == requests)
            Report("units", units);
        else
        {
            auto block = count % units * device->UnitBlocks() + count / units % device->UnitBlocks();
            ++count;
            device->WriteBlocks(Buffer(1), block, Then(&BlockBench::UnitNext, true));
        }
    }

    void BlockBench::FlushNext()
    {
        if (count == requests)
            Completed();
        else
            device->WriteBlocks(Buffer(1), Position(), Then(&BlockBench::FlushWritten, false));
    }

    void BlockBench::FlushWritten()
    {
        ++count;
        device->Flush(Then(&BlockBench::FlushNext, true));
    }

    void BlockBench::DiscardNext()
    {
        if (count == requests)
            Completed();
        else
            device->WriteBlocks(Buffer(1), Position(), Then(&BlockBench::DiscardWritten, false));
    }

    void BlockBench::DiscardWritten()
    {
        auto position = Position();
        ++count;
        device->EraseBlocks(position, position + blocks, Then(&BlockBench::DiscardNext, true));
    }

    void BlockBench::Completed()
    {
        Report();
    }
}
