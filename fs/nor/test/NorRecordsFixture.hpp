#ifndef FS_NOR_TEST_NOR_RECORDS_FIXTURE_HPP
#define FS_NOR_TEST_NOR_RECORDS_FIXTURE_HPP

#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorRecord.hpp"
#include "fs/nor/test/NorVolumeFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace fs
{
    template<NorConfig Config, uint32_t Sectors, uint32_t SectorSize>
    class NorRecordsFixture
        : public NorVolumeFixture<Config, Sectors, SectorSize>
    {
    public:
        static constexpr uint32_t recordBytes = NorInodeRecordBytes(Config);
        using Record = std::array<uint8_t, recordBytes>;

        Record Serialized(const NorInodeRecord& record) const
        {
            Record bytes{};
            SerializeNorInodeRecord(record, infra::MakeRange(bytes), Config);
            return bytes;
        }

        Result Commit(std::initializer_list<NorEntry> list)
        {
            entries.assign(list.begin(), list.end());
            std::size_t bytes = 0;
            for (const auto& entry : entries)
                bytes += NorEntrySize(entry);

            std::optional<Result> committed;
            this->Volume().Journal().Commit(bytes, [this](CrcRecord& record)
                {
                    for (const auto& entry : entries)
                        AddNorEntry(record, entry);
                },
                [&committed](Result result)
                {
                    committed = result;
                });
            this->ExecuteAllActions();
            return committed.value_or(Result::busy);
        }

        Result Apply(std::initializer_list<NorEntry> list, bool linked = false)
        {
            CrcRecord record{ infra::MakeRange(recordBuffer) };
            for (const auto& entry : list)
                AddNorEntry(record, entry);

            return ApplyBytes(record.Seal(), linked);
        }

        Result ApplyBytes(infra::ConstByteRange bytes, bool linked = false)
        {
            auto opened = OpenRecord(bytes);
            EXPECT_TRUE(opened.has_value());

            std::optional<Result> applied;
            this->Volume().RecordApply().Apply(opened.value_or(OpenedRecord{}), linked, [&applied](Result result)
                {
                    applied = result;
                });
            this->ExecuteAllActions();
            this->Volume().Cache().UnpinAll();
            return applied.value_or(Result::busy);
        }

        void FillInodeTable(uint32_t lastInode)
        {
            for (uint32_t inode = 2; inode <= lastInode; ++inode)
            {
                this->Builder().AddRecord(inode, NorInodeRecord{ NorImageBuilder::rootInode, NorInodeType::file });
                if (inode % 40 == 0)
                    this->Builder().Commit();
            }
            this->Builder().Commit();
        }

        void FillDirectory(uint32_t directory, uint32_t entries)
        {
            for (uint32_t key = 0; key != entries; ++key)
            {
                this->Builder().AddEntry(directory, key, directory);
                if (key % 200 == 199)
                    this->Builder().Commit();
            }
            this->Builder().Commit();
        }

        NorMapId InodeTableId() const
        {
            return NorMapId{ NorMap::inodeTable, 0, recordBytes };
        }

        NorMapId DirectoryId(uint32_t directory) const
        {
            return NorMapId{ NorMap::directory, directory, 4 };
        }

        void Fetch(const NorMapId& map, const NorMapRoot& root, uint32_t key)
        {
            std::optional<NorFetchResult> fetched;
            this->Volume().Fetch().Fetch(map, root, key, [&fetched](NorFetchResult result)
                {
                    fetched = result;
                });
            this->ExecuteAllActions();
            EXPECT_EQ(NorFetchResult::fetched, fetched);
        }

        std::optional<NorInodeRecord> RecordInVolume(uint32_t inode)
        {
            auto& state = this->State();
            Fetch(InodeTableId(), state.inodeTable, inode);
            auto bytes = NorBTree(this->Volume().Cache(), InodeTableId()).Find(state.inodeTable, inode);
            this->Volume().Cache().UnpinAll();
            if (!bytes.has_value())
                return std::nullopt;

            return ParseNorInodeRecord(*bytes, Config);
        }

        std::optional<uint32_t> EntryInVolume(uint32_t directory, uint32_t key)
        {
            auto record = RecordInVolume(directory);
            if (!record.has_value())
                return std::nullopt;

            NorMapRoot map{ record->mapRoot, record->mapHeight };
            Fetch(DirectoryId(directory), map, key);
            auto value = NorBTree(this->Volume().Cache(), DirectoryId(directory)).Find(map, key);
            this->Volume().Cache().UnpinAll();
            if (!value.has_value())
                return std::nullopt;

            return LoadLittle32(*value, 0);
        }

        infra::BoundedVector<NorEntry>::WithMaxSize<4> entries;
        std::array<uint8_t, 512> recordBuffer{};
    };
}

#endif
