#ifndef FS_NOR_NOR_RECORD_READER_HPP
#define FS_NOR_NOR_RECORD_READER_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorGeometry.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorRecordReader
    {
    public:
        NorRecordReader(hal::Flash& flash, infra::ByteRange buffer);
        NorRecordReader(const NorRecordReader& other) = delete;
        NorRecordReader& operator=(const NorRecordReader& other) = delete;
        ~NorRecordReader() = default;

        void Read(const NorGeometry& geometry, uint32_t block, uint32_t offset, const infra::Function<void(std::optional<OpenedRecord> record)>& onDone);

    private:
        void OnHeader();
        void OnRecord();

    private:
        hal::Flash& flash;
        infra::ByteRange buffer;
        uint32_t address{ 0 };
        std::size_t room{ 0 };
        std::size_t length{ 0 };
        infra::AutoResetFunction<void(std::optional<OpenedRecord> record)> onDone;
    };
}

#endif
