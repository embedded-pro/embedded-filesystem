#include "fs/nor/NorRecordReader.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    NorRecordReader::NorRecordReader(hal::Flash& flash, infra::ByteRange buffer)
        : flash(flash)
        , buffer(buffer)
    {
        really_assert(buffer.size() >= CrcRecord::overhead);
    }

    void NorRecordReader::Read(const NorGeometry& geometry, uint32_t block, uint32_t offset, const infra::Function<void(std::optional<OpenedRecord> record)>& onRead)
    {
        if (offset + CrcRecord::headerSize > geometry.BlockSize())
        {
            onRead(std::nullopt);
            return;
        }

        onDone = onRead;
        address = geometry.AddressOfBlock(block) + offset;
        room = std::min<std::size_t>(buffer.size(), geometry.BlockSize() - offset);
        flash.ReadBuffer(infra::Head(buffer, CrcRecord::headerSize), address, [this]
            {
                OnHeader();
            });
    }

    void NorRecordReader::OnHeader()
    {
        auto size = RecordLength(infra::Head(buffer, CrcRecord::headerSize));
        if (!size.has_value() || *size > room)
        {
            onDone(std::nullopt);
            return;
        }

        length = *size;
        flash.ReadBuffer(infra::Head(buffer, length), address, [this]
            {
                OnRecord();
            });
    }

    void NorRecordReader::OnRecord()
    {
        onDone(OpenRecord(infra::Head(buffer, length)));
    }
}
