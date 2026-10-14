#include "fs/nor/NorInodeRecord.hpp"
#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorRecord.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        bool IsKnownType(uint8_t type)
        {
            return type == static_cast<uint8_t>(NorInodeType::file) || type == static_cast<uint8_t>(NorInodeType::directory);
        }
    }

    bool NorInodeRecord::operator==(const NorInodeRecord& other) const
    {
        return parent == other.parent && type == other.type && mapHeight == other.mapHeight && size == other.size && mapRoot == other.mapRoot && infra::ContentsEqual(inlineBytes, other.inlineBytes) && infra::ContentsEqual(name, other.name);
    }

    void SerializeNorInodeRecord(const NorInodeRecord& record, infra::ByteRange bytes, const NorConfig& config)
    {
        really_assert(bytes.size() == NorInodeRecordBytes(config) && record.inlineBytes.size() <= config.inlineBytes && record.name.size() <= config.maxNameBytes);

        std::ranges::fill(bytes, uint8_t{ 0 });
        StoreLittle32(bytes, 0, record.parent);
        bytes[4] = static_cast<uint8_t>(record.type);
        bytes[5] = static_cast<uint8_t>(record.name.size());
        bytes[6] = record.mapHeight;
        StoreLittle32(bytes, 8, record.size);
        StoreLittle32(bytes, norInodeMapRootOffset, record.mapRoot);
        StoreLittle16(bytes, 16, static_cast<uint16_t>(record.inlineBytes.size()));
        std::ranges::copy(record.inlineBytes, bytes.begin() + norInodeRecordFixedBytes);
        std::ranges::copy(record.name, bytes.begin() + norInodeRecordFixedBytes + config.inlineBytes);
    }

    std::optional<NorInodeRecord> ParseNorInodeRecord(infra::ConstByteRange bytes, const NorConfig& config)
    {
        really_assert(bytes.size() == NorInodeRecordBytes(config));

        auto inlineLength = LoadLittle16(bytes, 16);
        if (!IsKnownType(bytes[4]) || bytes[5] > config.maxNameBytes || inlineLength > config.inlineBytes)
            return std::nullopt;

        auto inlineArea = infra::DiscardHead(bytes, norInodeRecordFixedBytes);
        return NorInodeRecord{ LoadLittle32(bytes, 0), static_cast<NorInodeType>(bytes[4]), bytes[6], LoadLittle32(bytes, 8), LoadLittle32(bytes, norInodeMapRootOffset), infra::Head(inlineArea, inlineLength), infra::Head(infra::DiscardHead(inlineArea, config.inlineBytes), bytes[5]) };
    }

    void AddNorInodePut(CrcRecord& record, uint32_t inode, const NorInodeRecord& inodeRecord, const NorConfig& config)
    {
        auto entry = record.Add(norInodePutFixedBytes + NorInodeRecordBytes(config));
        entry[0] = static_cast<uint8_t>(NorEntryKind::inodePut);
        StoreLittle32(entry, 1, inode);
        SerializeNorInodeRecord(inodeRecord, infra::DiscardHead(entry, norInodePutFixedBytes), config);
    }
}
