#ifndef FS_NOR_NOR_CHECKPOINT_LOAD_HPP
#define FS_NOR_NOR_CHECKPOINT_LOAD_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorHeaderScan.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Crc.hpp"
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
        void Verify(uint32_t block, uint64_t sequence);
        void OnFixedPart();
        void ReadForCrc();
        void OnStoredCrc();
        void FindPreviousCheckpoint();
        void ReadNextCandidate();
        void OnCandidateHeader();
        void Accept();
        void ReadBitmap();
        void OnBitmap(uint32_t size);
        bool Settle(uint32_t block, bool inUse) const;
        Result Consistency() const;
        bool IsNodeInUse(uint32_t address) const;
        bool AreRecordsInUse() const;
        void Read(uint32_t offset, uint32_t size, const infra::Function<void()>& onRead);

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
        infra::Crc32 crc;
        std::optional<uint32_t> previous;
        uint64_t previousSequence{ 0 };
        infra::AutoResetFunction<void()> onRead;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
