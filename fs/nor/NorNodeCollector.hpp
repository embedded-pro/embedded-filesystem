#ifndef FS_NOR_NOR_NODE_COLLECTOR_HPP
#define FS_NOR_NOR_NODE_COLLECTOR_HPP

#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorNodeCollector
    {
    public:
        explicit NorNodeCollector(NorVolume& volume);
        NorNodeCollector(const NorNodeCollector& other) = delete;
        NorNodeCollector& operator=(const NorNodeCollector& other) = delete;
        ~NorNodeCollector();

        bool Due() const;
        uint32_t CollectableBlocks() const;
        uint32_t LeftBlocks() const;
        bool Moving() const;
        void Reset();
        void Move(uint32_t block);
        void Collect(bool collectionDue, const infra::Function<void()>& onCollected);
        void Committed();

    private:
        void Examine();
        void HeaderRead();
        void OwnerFetched(NorFetchResult result);
        void Check(const NorMapRoot& map);
        void ParentFetched(NorFetchResult result);
        void Compare(uint32_t address);
        void NodeFetched(NorFetchResult result);
        void Relocate();
        void Next();
        void Skip();
        void Pass();
        void Leave();
        bool HasRoom() const;
        uint32_t FreeSlots() const;
        uint32_t AddressAt(uint8_t level) const;
        uint32_t ChildOf(std::size_t node) const;
        bool IsCollectable(uint32_t candidate) const;
        std::optional<uint32_t> NextBlock() const;
        uint32_t SlotAddress() const;
        NorMapId Map() const;
        NorMapId InodeTable() const;

    private:
        NorVolume& volume;
        std::optional<uint32_t> block;
        uint32_t slot{ 0 };
        std::optional<uint32_t> moving;
        bool collecting{ false };
        uint32_t left{ 0 };
        uint32_t leftByStep{ 0 };
        uint32_t freeSlots{ 0 };
        uint32_t awaitingCommit{ 0 };
        std::array<uint8_t, norNodeHeaderBytes> headerBytes{};
        NorNodeHeader header;
        NorMapRoot root;
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif
