#ifndef FS_NOR_NOR_RECORD_HPP
#define FS_NOR_NOR_RECORD_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/nor/NorLayout.hpp"
#include "infra/util/ByteRange.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace fs
{
    enum class NorEntryKind : uint8_t
    {
        link = 1,
        inodePut = 2,
        inodeRemove = 3,
        directoryEntryPut = 4,
        directoryEntryRemove = 5,
        blockSet = 6,
        size = 7
    };

    struct NorLink
    {
        uint32_t previousBlock{ 0 };
        uint32_t end{ 0 };

        bool operator==(const NorLink& other) const = default;
    };

    struct NorInodePut
    {
        uint32_t inode{ 0 };
        infra::ConstByteRange record;

        bool operator==(const NorInodePut& other) const;
    };

    struct NorInodeRemove
    {
        uint32_t inode{ 0 };

        bool operator==(const NorInodeRemove& other) const = default;
    };

    struct NorDirectoryEntryPut
    {
        uint32_t directory{ 0 };
        uint32_t key{ 0 };
        uint32_t inode{ 0 };

        bool operator==(const NorDirectoryEntryPut& other) const = default;
    };

    struct NorDirectoryEntryRemove
    {
        uint32_t directory{ 0 };
        uint32_t key{ 0 };

        bool operator==(const NorDirectoryEntryRemove& other) const = default;
    };

    struct NorBlockSet
    {
        uint32_t inode{ 0 };
        uint32_t index{ 0 };
        uint32_t previous{ 0 };
        uint32_t block{ 0 };

        bool operator==(const NorBlockSet& other) const = default;
    };

    struct NorSize
    {
        uint32_t inode{ 0 };
        uint32_t size{ 0 };
        infra::ConstByteRange inlineBytes;

        bool operator==(const NorSize& other) const;
    };

    using NorEntry = std::variant<NorLink, NorInodePut, NorInodeRemove, NorDirectoryEntryPut, NorDirectoryEntryRemove, NorBlockSet, NorSize>;

    constexpr std::size_t norLinkEntryBytes = 9;
    constexpr std::size_t norDirectoryEntryPutBytes = 13;
    constexpr std::size_t norDirectoryEntryRemoveBytes = 9;
    constexpr std::size_t norBlockSetEntryBytes = 17;
    constexpr std::size_t norSizeEntryFixedBytes = 11;
    constexpr std::size_t norInodePutFixedBytes = 5;
    constexpr std::size_t norInodeRemoveBytes = 5;
    constexpr std::size_t norUncommittedBlockEntries = 8;

    constexpr std::size_t NorLargestRecordBytes(uint32_t inlineBytes, uint32_t maxNameBytes)
    {
        auto rename = norDirectoryEntryRemoveBytes + norDirectoryEntryPutBytes + norInodePutFixedBytes + NorInodeRecordBytes(inlineBytes, maxNameBytes);
        auto commit = norUncommittedBlockEntries * norBlockSetEntryBytes + norSizeEntryFixedBytes + inlineBytes;
        return CrcRecord::overhead + norLinkEntryBytes + std::max<std::size_t>(rename, commit);
    }

    std::size_t NorEntrySize(const NorEntry& entry);
    void AddNorEntry(CrcRecord& record, const NorEntry& entry);

    class NorEntryReader
    {
    public:
        NorEntryReader(const OpenedRecord& record, uint32_t inodeRecordBytes);

        std::optional<NorEntry> Next();
        bool Malformed() const;

    private:
        std::optional<NorEntry> Read(uint8_t kind);
        std::optional<NorEntry> ReadInodePut();
        std::optional<NorEntry> ReadSize();
        std::optional<infra::ConstByteRange> Take(std::size_t size);

        template<class Entry, std::size_t Fields>
        std::optional<NorEntry> ReadFields();

    private:
        infra::ConstByteRange remaining;
        uint8_t entriesLeft;
        uint32_t inodeRecordBytes;
        bool malformed{ false };
    };
}

#endif
