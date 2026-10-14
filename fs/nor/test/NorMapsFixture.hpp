#ifndef FS_NOR_TEST_NOR_MAPS_FIXTURE_HPP
#define FS_NOR_TEST_NOR_MAPS_FIXTURE_HPP

#include "fs/common/LittleEndian.hpp"
#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorNodeCache.hpp"
#include "fs/nor/NorNodeWriteBack.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace fs
{
    class NorMapsFixture
        : public infra::ClockFixture
    {
    public:
        static constexpr uint32_t nodeSize = 64;
        static constexpr uint32_t recordBytes = 20;
        static constexpr NorMapId inodeTable{ NorMap::inodeTable, 0, recordBytes };

        using Headers = infra::BoundedVector<NorNodeHeader>::WithMaxSize<64>;

        static constexpr NorMapId DirectoryMap(uint32_t inode)
        {
            return NorMapId{ NorMap::directory, inode, 4 };
        }

        static constexpr NorMapId DataMap(uint32_t inode)
        {
            return NorMapId{ NorMap::fileData, inode, 4 };
        }

        void AddInodes(uint32_t first, uint32_t last)
        {
            for (auto inode = first; inode <= last; ++inode)
                inodes.Insert(inodeTableRoot, inode)[0] = static_cast<uint8_t>(inode);
        }

        void AddEntries(uint32_t directory, uint32_t first, uint32_t last)
        {
            auto map = MapRootOf(directory);
            NorBTree entries{ cache, DirectoryMap(directory) };
            for (auto key = first; key <= last; key += 10)
                StoreLittle32(entries.Insert(map, key), 0, key + 1);

            SetMapRoot(directory, map);
        }

        void MapBlocks(uint32_t file, std::initializer_list<uint32_t> indexes)
        {
            auto map = MapRootOf(file);
            NorRadixTree blocks{ cache, file };
            for (auto index : indexes)
                blocks.Set(map, index, index + 100);

            SetMapRoot(file, map);
        }

        NorMapRoot MapRootOf(uint32_t inode) const
        {
            auto record = inodes.Find(inodeTableRoot, inode).value_or(infra::ConstByteRange());
            return NorMapRoot{ LoadLittle32(record, norInodeMapRootOffset), record[6] };
        }

        void SetMapRoot(uint32_t inode, const NorMapRoot& map)
        {
            auto record = inodes.Modify(inodeTableRoot, inode);
            StoreLittle32(record, norInodeMapRootOffset, map.address);
            record[6] = map.height;
        }

        Headers WriteBack()
        {
            Headers written;

            for (auto next = writeBack.Next(); next.has_value(); next = writeBack.Next())
            {
                auto node = writeBack.Seal(*next);
                written.push_back(ReadNorNodeHeader(node));
                Program(nextAddress, node);
                writeBack.Written(*next, nextAddress, inodeTableRoot);
                nextAddress += nodeSize;
            }

            return written;
        }

        void Program(uint32_t address, infra::ConstByteRange bytes)
        {
            std::ranges::copy(bytes, flash.sectors[address / 4096].begin() + address % 4096);
        }

        void Corrupt(uint32_t address)
        {
            flash.sectors[address / 4096][address % 4096 + 20] ^= 1;
        }

        hal::FlashStub flash{ 8, 4096 };
        std::array<NorNodeCache::Slot, 64> slots{};
        std::array<uint8_t, 64 * nodeSize> buffers{};
        NorNodeCache cache{ flash, infra::MakeRange(slots), infra::MakeRange(buffers) };
        NorBTree inodes{ cache, inodeTable };
        NorNodeWriteBack writeBack{ cache, recordBytes };
        NorMapRoot inodeTableRoot;
        uint32_t nextAddress{ 2 * 4096 };
    };
}

#endif
