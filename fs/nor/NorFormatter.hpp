#ifndef FS_NOR_NOR_FORMATTER_HPP
#define FS_NOR_NOR_FORMATTER_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorGeometry.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorFormatter
    {
    public:
        template<NorConfig Config>
        class WithStorage;

        static constexpr uint32_t BufferBytes(const NorConfig& config)
        {
            return std::max(config.nodeSize, RoundUpTo(NorCheckpointBytes(config.maxBlocks), config.programUnit));
        }

        NorFormatter(hal::Flash& flash, const NorConfig& config, infra::ByteRange buffer);
        NorFormatter(const NorFormatter& other) = delete;
        NorFormatter& operator=(const NorFormatter& other) = delete;
        ~NorFormatter() = default;

        void Format(const infra::Function<void(Result result)>& onDone);

    private:
        void ReadEraseCount();
        void EraseSuperblocks();
        void FormatBlock();
        void ProgramPartA();
        void WriteNodeAllocation();
        void WriteRootNode();
        void WriteCheckpointAllocation();
        void WriteCheckpoint();
        void WriteSuperblock();

        void ReadHeader(uint32_t block, const infra::Function<void()>& onRead);
        void EraseSectors(uint32_t first, uint32_t end, const infra::Function<void()>& onErasedDone);
        void EraseNextSector();
        void Program(infra::ConstByteRange bytes, uint32_t address, const infra::Function<void()>& onProgrammed);
        void Complete(Result completionResult);

        bool HasEqualSectors() const;
        uint32_t AverageEraseCount() const;
        infra::ByteRange Rounded(uint32_t size) const;

    private:
        hal::Flash& flash;
        NorConfig config;
        infra::ByteRange buffer;
        std::optional<NorGeometry> geometry;
        uint32_t block{ 0 };
        uint32_t sector{ 0 };
        uint32_t sectorEnd{ 0 };
        uint64_t eraseCountSum{ 0 };
        uint32_t eraseCountsFound{ 0 };
        uint32_t eraseCount{ 0 };
        Result result{ Result::success };
        infra::AutoResetFunction<void()> onDeviceDone;
        infra::AutoResetFunction<void()> onErased;
        infra::AutoResetFunction<void(Result result)> onDone;
    };

    namespace detail
    {
        template<NorConfig Config>
        struct NorFormatterStorage
        {
            std::array<uint8_t, NorFormatter::BufferBytes(Config)> bufferStorage;
        };
    }

    template<NorConfig Config>
    class NorFormatter::WithStorage
        : private detail::NorFormatterStorage<Config>
        , public NorFormatter
    {
        static_assert(IsValidNorConfig(Config));

    public:
        explicit WithStorage(hal::Flash& flash)
            : NorFormatter(flash, Config, infra::MakeRange(this->bufferStorage))
        {}
    };
}

#endif
