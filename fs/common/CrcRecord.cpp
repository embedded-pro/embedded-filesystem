#include "fs/common/CrcRecord.hpp"
#include "fs/common/LittleEndian.hpp"
#include "infra/util/Crc.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        uint32_t Crc(infra::ConstByteRange bytes)
        {
            infra::Crc32 crc;
            crc.Update(bytes);
            return crc.Result();
        }
    }

    CrcRecord::CrcRecord(infra::ByteRange buffer)
        : buffer(buffer)
    {}

    bool CrcRecord::Fits(std::size_t entrySize) const
    {
        return entries != maxEntries && overhead + payloadSize + entrySize <= std::min(buffer.size(), maxSize);
    }

    infra::ByteRange CrcRecord::Add(std::size_t entrySize)
    {
        really_assert(Fits(entrySize));

        auto entry = infra::Head(infra::DiscardHead(buffer, headerSize + payloadSize), entrySize);
        payloadSize += entrySize;
        ++entries;
        return entry;
    }

    infra::ConstByteRange CrcRecord::Seal()
    {
        auto size = overhead + payloadSize;
        StoreLittle16(buffer, 0, static_cast<uint16_t>(size));
        buffer[2] = entries;
        buffer[3] = 0;
        StoreLittle32(buffer, size - 4, Crc(infra::Head(buffer, size - 4)));
        return infra::Head(buffer, size);
    }

    std::optional<std::size_t> RecordLength(infra::ConstByteRange header)
    {
        std::size_t size = LoadLittle16(header, 0);
        if (size < CrcRecord::overhead || size > CrcRecord::maxSize)
            return std::nullopt;

        return size;
    }

    std::optional<OpenedRecord> OpenRecord(infra::ConstByteRange bytes)
    {
        auto length = RecordLength(bytes);
        if (!length.has_value() || bytes.size() < *length || LoadLittle32(bytes, *length - 4) != Crc(infra::Head(bytes, *length - 4)))
            return std::nullopt;

        return OpenedRecord{ bytes[2], infra::Head(infra::DiscardHead(bytes, CrcRecord::headerSize), *length - CrcRecord::overhead), *length };
    }
}
