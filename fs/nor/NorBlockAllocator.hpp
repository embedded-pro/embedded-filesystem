#ifndef FS_NOR_NOR_BLOCK_ALLOCATOR_HPP
#define FS_NOR_NOR_BLOCK_ALLOCATOR_HPP

#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorBlockStates.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorBlockAllocator
    {
    public:
        explicit NorBlockAllocator(NorVolume& volume);
        NorBlockAllocator(const NorBlockAllocator& other) = delete;
        NorBlockAllocator& operator=(const NorBlockAllocator& other) = delete;
        ~NorBlockAllocator();

        void Allocate(NorBlockType type, const NorBlockOwner& owner, const infra::Function<void(std::optional<uint32_t> block)>& onDone);

    private:
        std::optional<uint32_t> LeastWorn(NorBlockState state) const;
        void EraseNextSector();
        void ProgramPartA();
        void ProgramPartB();
        void Program(infra::ConstByteRange bytes, uint32_t address, const infra::Function<void()>& onProgrammedDone);
        infra::ByteRange Rounded(uint32_t size) const;

    private:
        NorVolume& volume;
        uint32_t block{ 0 };
        uint32_t sector{ 0 };
        NorBlockType type{ NorBlockType::data };
        NorBlockOwner owner;
        infra::AutoResetFunction<void()> onProgrammed;
        infra::AutoResetFunction<void(std::optional<uint32_t> block)> onDone;
    };
}

#endif
