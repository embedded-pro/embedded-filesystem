#ifndef FS_NOR_NOR_JOURNAL_WRITER_HPP
#define FS_NOR_NOR_JOURNAL_WRITER_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorRecord.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorJournalWriter
    {
    public:
        explicit NorJournalWriter(NorVolume& volume);
        NorJournalWriter(const NorJournalWriter& other) = delete;
        NorJournalWriter& operator=(const NorJournalWriter& other) = delete;
        ~NorJournalWriter();

        void Commit(std::size_t entryBytes, const infra::Function<void(CrcRecord& record)>& addEntries, const infra::Function<void(Result result)>& onDone);
        bool IsMoreThanHalfFull() const;

    private:
        void Place();
        void BlockAllocated(std::optional<uint32_t> block);
        void Write();
        void Written();

    private:
        NorVolume& volume;
        uint32_t recordBytes{ 0 };
        std::optional<NorLink> link;
        infra::ConstByteRange sealed;
        infra::AutoResetFunction<void(CrcRecord& record)> addEntries;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
