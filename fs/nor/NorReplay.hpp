#ifndef FS_NOR_NOR_REPLAY_HPP
#define FS_NOR_NOR_REPLAY_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorRecordReader.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorReplay
    {
    public:
        NorReplay(NorVolume& volume, std::optional<NorRecordPosition> checkpointBefore, const infra::Function<void(Result result, std::optional<NorRecordPosition> overflow)>& onDone);
        NorReplay(const NorReplay& other) = delete;
        NorReplay& operator=(const NorReplay& other) = delete;
        ~NorReplay();

        void Start();

    private:
        void ReadRecord();
        void OnRecord(const std::optional<OpenedRecord>& record);
        void ReadLaterRecord();
        void Read(std::size_t epochIndex, uint32_t at, bool later);
        void HoldBlocksOf(const std::optional<OpenedRecord>& record);
        void WriteCheckpoint();
        void Apply(const OpenedRecord& record);
        void Applied(Result result);

    private:
        NorVolume& volume;
        NorRecordReader records;
        std::size_t index{ 0 };
        uint32_t offset{ 0 };
        bool linked{ false };
        bool applied{ false };
        bool readingLater{ false };
        std::optional<NorRecordPosition> checkpointBefore;
        NorRecordPosition current;
        std::size_t laterIndex{ 0 };
        uint32_t laterOffset{ 0 };
        infra::AutoResetFunction<void(Result result, std::optional<NorRecordPosition> overflow)> onDone;
    };
}

#endif
