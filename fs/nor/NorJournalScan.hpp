#ifndef FS_NOR_NOR_JOURNAL_SCAN_HPP
#define FS_NOR_NOR_JOURNAL_SCAN_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorRecordReader.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorJournalScan
    {
    public:
        NorJournalScan(hal::Flash& flash, NorVolumeState& volume, infra::ByteRange buffer, uint32_t inodeRecordBytes, const infra::Function<void(Result result)>& onDone);
        NorJournalScan(const NorJournalScan& other) = delete;
        NorJournalScan& operator=(const NorJournalScan& other) = delete;
        ~NorJournalScan() = default;

        void Start();

    private:
        enum class Step : uint8_t
        {
            firstHeader,
            candidateHeader,
            record,
            candidateRecord
        };

        void OnFirstHeader();
        void ReadRecord();
        void OnRecord(const std::optional<OpenedRecord>& record);
        void FindNextBlock();
        void ReadNextCandidate();
        void OnCandidateHeader();
        void OnCandidateRecord(const std::optional<OpenedRecord>& record);
        bool LinksToEnd(const OpenedRecord& record) const;
        void Follow(uint32_t next);
        void Finish();
        void ReadHeader(uint32_t block, Step next);
        void ReadRecordAt(uint32_t block, uint32_t offset, Step next);
        NorAllocation AllocationInBuffer() const;

    private:
        hal::Flash& flash;
        NorVolumeState& volume;
        infra::ByteRange buffer;
        uint32_t inodeRecordBytes;
        NorRecordReader records;
        uint32_t block{ 0 };
        uint32_t end{ 0 };
        Step step{ Step::firstHeader };
        uint64_t sequence{ 0 };
        uint32_t candidate{ 0 };
        uint64_t candidateSequence{ 0 };
        std::optional<uint32_t> next;
        uint64_t nextSequence{ 0 };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
