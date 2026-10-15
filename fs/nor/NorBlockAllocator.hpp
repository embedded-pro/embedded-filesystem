#ifndef FS_NOR_NOR_BLOCK_ALLOCATOR_HPP
#define FS_NOR_NOR_BLOCK_ALLOCATOR_HPP

#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorWear.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    enum class NorReserve : uint8_t
    {
        kept,
        used
    };

    class NorBlockAllocator
    {
    public:
        explicit NorBlockAllocator(NorVolume& volume);
        NorBlockAllocator(const NorBlockAllocator& other) = delete;
        NorBlockAllocator& operator=(const NorBlockAllocator& other) = delete;
        ~NorBlockAllocator();

        void Allocate(NorBlockType type, const NorBlockOwner& owner, NorReserve reserve, const infra::Function<void(std::optional<uint32_t> block)>& onDone);
        void Allocate(NorBlockType type, const NorBlockOwner& owner, NorReserve reserve, NorWear wear, const infra::Function<void(std::optional<uint32_t> block)>& onDone);

    private:
        std::optional<uint32_t> ErasedOfWear() const;
        bool IsWornEnough(uint32_t erased) const;
        void TakeErased();
        void ProgramPartB();

    private:
        NorVolume& volume;
        uint32_t block{ 0 };
        NorBlockType type{ NorBlockType::data };
        NorBlockOwner owner;
        NorWear wear{ NorWear::least };
        infra::AutoResetFunction<void(std::optional<uint32_t> block)> onDone;
    };
}

#endif
