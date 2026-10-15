#ifndef FS_NOR_NOR_WEAR_LEVELER_HPP
#define FS_NOR_NOR_WEAR_LEVELER_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorMapFetch.hpp"
#include "fs/nor/NorNode.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorWearLeveler
    {
    public:
        explicit NorWearLeveler(NorVolume& volume);
        NorWearLeveler(const NorWearLeveler& other) = delete;
        NorWearLeveler& operator=(const NorWearLeveler& other) = delete;
        ~NorWearLeveler();

        bool Due() const;
        bool FallbackDue() const;
        void Reset();
        void Move(const infra::Function<void()>& onMoved);

    private:
        bool DueAfter(uint32_t erases) const;
        uint32_t BlockErases() const;
        bool IsWorthAMove() const;
        std::optional<uint32_t> Source() const;
        std::optional<uint32_t> MostWornErased() const;
        uint32_t HighestCount() const;
        bool IsMovable(uint32_t block) const;
        void MoveNodes();
        void MoveData();
        void HeaderRead();
        void RecordFetched(NorFetchResult result);
        void MapFetched(NorFetchResult result);
        bool OpenForWriting() const;
        void Allocated(std::optional<uint32_t> block);
        void CopyNextChunk();
        void Commit();
        void Committed(Result result);
        void Done();
        NorMapId InodeTable() const;
        NorMapId DataMap() const;

    private:
        NorVolume& volume;
        uint32_t erasesAtMove{ 0 };
        uint32_t source{ 0 };
        uint32_t target{ 0 };
        NorBlockOwner owner;
        NorMapRoot dataMap;
        uint32_t length{ 0 };
        uint32_t copied{ 0 };
        uint32_t chunk{ 0 };
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif
