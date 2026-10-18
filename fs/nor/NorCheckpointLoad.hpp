#ifndef FS_NOR_NOR_CHECKPOINT_LOAD_HPP
#define FS_NOR_NOR_CHECKPOINT_LOAD_HPP

#include "fs/common/Crc32.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorHeaderScan.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorCheckpointLoad
    {
    public:
        NorCheckpointLoad(hal::Flash& flash, NorVolumeState& volume, infra::ByteRange buffer, const NorScanSummary& summary, const infra::Function<void(Result result)>& onDone);
        NorCheckpointLoad(const NorCheckpointLoad& other) = delete;
        NorCheckpointLoad& operator=(const NorCheckpointLoad& other) = delete;
        ~NorCheckpointLoad() = default;

        void Start();

    private:
        enum class Step : uint8_t
        {
            fixedPart,
            crcPart,
            storedCrc,
            candidateHeader,
            bitmap
        };

        void Verify(uint32_t block, uint64_t sequence);
        void OnFixedPart();
        void ReadForCrc();
        void OnCrcPart();
        void OnStoredCrc();
        void FindPreviousCheckpoint();
        void ReadNextCandidate();
        void OnCandidateHeader();
        void Accept();
        void ReadBitmap();
        void OnBitmap();
        void Settle(uint32_t block, bool inUse) const;
        Result Consistency() const;
        bool IsNodeInUse(uint32_t address) const;
        bool AreRecordsInUse() const;
        void Read(uint32_t offset, uint32_t size, Step next);
        void ReadAt(uint32_t address, uint32_t size, Step next);
        void Continue();

    private:
        hal::Flash& flash;
        NorVolumeState& volume;
        infra::ByteRange buffer;
        uint32_t averageCount;
        uint32_t candidate{ 0 };
        uint64_t candidateSequence{ 0 };
        NorCheckpoint loaded;
        uint32_t crcOffset{ 0 };
        uint32_t position{ 0 };
        uint32_t readSize{ 0 };
        Step step{ Step::fixedPart };
        Crc32 crc;
        std::optional<uint32_t> previous;
        uint64_t previousSequence{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
