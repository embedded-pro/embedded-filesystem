#ifndef FS_COMMON_CRC_RECORD_HPP
#define FS_COMMON_CRC_RECORD_HPP

#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class CrcRecord
    {
    public:
        static constexpr std::size_t headerSize = 4;
        static constexpr std::size_t overhead = headerSize + 4;
        static constexpr std::size_t maxSize = 0xfffe;
        static constexpr std::size_t maxEntries = 255;

        explicit CrcRecord(infra::ByteRange buffer);

        bool Fits(std::size_t entrySize) const;
        infra::ByteRange Add(std::size_t entrySize);
        infra::ConstByteRange Seal() const;

    private:
        infra::ByteRange buffer;
        std::size_t payloadSize{ 0 };
        uint8_t entries{ 0 };
    };

    struct OpenedRecord
    {
        uint8_t entries{ 0 };
        infra::ConstByteRange payload;
        std::size_t size{ 0 };
    };

    std::optional<std::size_t> RecordLength(infra::ConstByteRange header);
    std::optional<OpenedRecord> OpenRecord(infra::ConstByteRange bytes);
}

#endif
