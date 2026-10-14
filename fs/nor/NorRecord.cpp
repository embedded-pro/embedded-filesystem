#include "fs/nor/NorRecord.hpp"
#include "fs/common/LittleEndian.hpp"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <tuple>

namespace fs
{
    namespace
    {
        struct EntrySize
        {
            std::size_t operator()(const NorLink&) const
            {
                return norLinkEntryBytes;
            }

            std::size_t operator()(const NorInodePut& put) const
            {
                return norInodePutFixedBytes + put.record.size();
            }

            std::size_t operator()(const NorInodeRemove&) const
            {
                return norInodeRemoveBytes;
            }

            std::size_t operator()(const NorDirectoryEntryPut&) const
            {
                return norDirectoryEntryPutBytes;
            }

            std::size_t operator()(const NorDirectoryEntryRemove&) const
            {
                return norDirectoryEntryRemoveBytes;
            }

            std::size_t operator()(const NorBlockSet&) const
            {
                return norBlockSetEntryBytes;
            }

            std::size_t operator()(const NorSize& size) const
            {
                return norSizeEntryFixedBytes + size.inlineBytes.size();
            }
        };

        class EntryWriter
        {
        public:
            explicit EntryWriter(infra::ByteRange entry)
                : entry(entry)
            {}

            void operator()(const NorLink& link)
            {
                Put(NorEntryKind::link, { link.previousBlock, link.end });
            }

            void operator()(const NorInodePut& put)
            {
                Put(NorEntryKind::inodePut, { put.inode });
                PutBytes(put.record);
            }

            void operator()(const NorInodeRemove& remove)
            {
                Put(NorEntryKind::inodeRemove, { remove.inode });
            }

            void operator()(const NorDirectoryEntryPut& put)
            {
                Put(NorEntryKind::directoryEntryPut, { put.directory, put.key, put.inode });
            }

            void operator()(const NorDirectoryEntryRemove& remove)
            {
                Put(NorEntryKind::directoryEntryRemove, { remove.directory, remove.key });
            }

            void operator()(const NorBlockSet& set)
            {
                Put(NorEntryKind::blockSet, { set.inode, set.index, set.previous, set.block });
            }

            void operator()(const NorSize& size)
            {
                Put(NorEntryKind::size, { size.inode, size.size });
                StoreLittle16(entry, offset, static_cast<uint16_t>(size.inlineBytes.size()));
                offset += 2;
                PutBytes(size.inlineBytes);
            }

        private:
            void Put(NorEntryKind kind, std::initializer_list<uint32_t> fields)
            {
                entry[offset++] = static_cast<uint8_t>(kind);
                for (auto field : fields)
                {
                    StoreLittle32(entry, offset, field);
                    offset += 4;
                }
            }

            void PutBytes(infra::ConstByteRange bytes)
            {
                std::ranges::copy(bytes, entry.begin() + offset);
                offset += bytes.size();
            }

            infra::ByteRange entry;
            std::size_t offset{ 0 };
        };

        template<std::size_t Fields>
        std::array<uint32_t, Fields> LoadFields(infra::ConstByteRange bytes)
        {
            std::array<uint32_t, Fields> fields{};
            for (std::size_t field = 0; field != Fields; ++field)
                fields[field] = LoadLittle32(bytes, 4 * field);

            return fields;
        }

        template<class Entry, std::size_t Fields>
        Entry MakeEntry(const std::array<uint32_t, Fields>& fields)
        {
            return std::apply([](auto... values)
                {
                    return Entry{ values... };
                },
                fields);
        }
    }

    bool NorInodePut::operator==(const NorInodePut& other) const
    {
        return inode == other.inode && infra::ContentsEqual(record, other.record);
    }

    bool NorSize::operator==(const NorSize& other) const
    {
        return inode == other.inode && size == other.size && infra::ContentsEqual(inlineBytes, other.inlineBytes);
    }

    std::size_t NorEntrySize(const NorEntry& entry)
    {
        return std::visit(EntrySize{}, entry);
    }

    void AddNorEntry(CrcRecord& record, const NorEntry& entry)
    {
        std::visit(EntryWriter{ record.Add(NorEntrySize(entry)) }, entry);
    }

    NorEntryReader::NorEntryReader(const OpenedRecord& record, uint32_t inodeRecordBytes)
        : remaining(record.payload)
        , entriesLeft(record.entries)
        , inodeRecordBytes(inodeRecordBytes)
    {}

    std::optional<NorEntry> NorEntryReader::Next()
    {
        if (entriesLeft == 0)
        {
            malformed = malformed || !remaining.empty();
            return std::nullopt;
        }

        --entriesLeft;
        std::optional<NorEntry> entry;
        if (auto kind = Take(1); kind.has_value())
            entry = Read((*kind)[0]);

        if (!entry.has_value())
        {
            malformed = true;
            entriesLeft = 0;
            remaining = {};
        }

        return entry;
    }

    bool NorEntryReader::Malformed() const
    {
        return malformed;
    }

    std::optional<NorEntry> NorEntryReader::Read(uint8_t kind)
    {
        switch (static_cast<NorEntryKind>(kind))
        {
            case NorEntryKind::link:
                return ReadFields<NorLink, 2>();
            case NorEntryKind::inodePut:
                return ReadInodePut();
            case NorEntryKind::inodeRemove:
                return ReadFields<NorInodeRemove, 1>();
            case NorEntryKind::directoryEntryPut:
                return ReadFields<NorDirectoryEntryPut, 3>();
            case NorEntryKind::directoryEntryRemove:
                return ReadFields<NorDirectoryEntryRemove, 2>();
            case NorEntryKind::blockSet:
                return ReadFields<NorBlockSet, 4>();
            case NorEntryKind::size:
                return ReadSize();
            default:
                return std::nullopt;
        }
    }

    template<class Entry, std::size_t Fields>
    std::optional<NorEntry> NorEntryReader::ReadFields()
    {
        auto bytes = Take(4 * Fields);
        if (!bytes.has_value())
            return std::nullopt;

        return MakeEntry<Entry>(LoadFields<Fields>(*bytes));
    }

    std::optional<NorEntry> NorEntryReader::ReadInodePut()
    {
        auto bytes = Take(4 + inodeRecordBytes);
        if (!bytes.has_value())
            return std::nullopt;

        return NorInodePut{ LoadLittle32(*bytes, 0), infra::DiscardHead(*bytes, 4) };
    }

    std::optional<NorEntry> NorEntryReader::ReadSize()
    {
        auto bytes = Take(norSizeEntryFixedBytes - 1);
        if (!bytes.has_value())
            return std::nullopt;

        auto inlineBytes = Take(LoadLittle16(*bytes, 8));
        if (!inlineBytes.has_value())
            return std::nullopt;

        return NorSize{ LoadLittle32(*bytes, 0), LoadLittle32(*bytes, 4), *inlineBytes };
    }

    std::optional<infra::ConstByteRange> NorEntryReader::Take(std::size_t size)
    {
        if (remaining.size() < size)
            return std::nullopt;

        auto taken = infra::Head(remaining, size);
        remaining = infra::DiscardHead(remaining, size);
        return taken;
    }
}
