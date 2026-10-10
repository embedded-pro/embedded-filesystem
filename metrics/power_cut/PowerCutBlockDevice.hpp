#ifndef METRICS_POWER_CUT_POWER_CUT_BLOCK_DEVICE_HPP
#define METRICS_POWER_CUT_POWER_CUT_BLOCK_DEVICE_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace metrics
{
    enum class InterruptedBlock : uint8_t
    {
        unwritten,
        written,
        torn
    };

    struct BlockCut
    {
        uint64_t step;
        InterruptedBlock mode;
        bool loseUnflushedWrites{ false };
        std::optional<uint32_t> tornBytes;
    };

    class PowerCutBlockDevice
        : public hal::BlockDevice
    {
    public:
        PowerCutBlockDevice(hal::BlockDevice& device, infra::ByteRange storage);

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

        void Arm(const BlockCut& cut);
        void Disarm();
        uint64_t WriteSteps() const;
        bool PoweredDown() const;

        static constexpr uint8_t tornPattern = 0xa5;

    private:
        bool CutsWithin(uint64_t steps) const;
        void CutWrite(infra::ConstByteRange buffer, uint32_t firstBlock);
        void Tear(infra::ConstByteRange source, uint32_t block);
        void CutDiscard(uint32_t beginBlock, uint32_t endBlock);
        void PowerDown();
        void RememberContentAtFlush(uint32_t firstBlock, uint32_t blocks);
        infra::ByteRange Block(uint32_t block) const;

    private:
        hal::BlockDevice& device;
        infra::ByteRange storage;
        std::optional<BlockCut> cut;
        uint64_t writeSteps{};
        bool poweredDown{ false };
        std::map<uint32_t, std::vector<uint8_t>> contentAtFlush;
        infra::AutoResetFunction<void(Result)> onFlushDone;
    };
}

#endif
