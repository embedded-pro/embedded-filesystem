#ifndef FS_NOR_NOR_DATA_ALLOCATOR_HPP
#define FS_NOR_NOR_DATA_ALLOCATOR_HPP

#include "fs/nor/NorBlockAllocator.hpp"
#include "fs/nor/NorBlockHeader.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorDataAllocator
    {
    public:
        explicit NorDataAllocator(NorVolume& volume);
        NorDataAllocator(const NorDataAllocator& other) = delete;
        NorDataAllocator& operator=(const NorDataAllocator& other) = delete;
        ~NorDataAllocator();

        void Allocate(const NorBlockOwner& owner, NorReserve reserve, const infra::Function<void(std::optional<uint32_t> block)>& onDone);

    private:
        void TryAllocate();
        void Allocated(std::optional<uint32_t> block);

    private:
        NorVolume& volume;
        NorBlockOwner owner;
        NorReserve reserve{ NorReserve::kept };
        bool checkpointWritten{ false };
        infra::AutoResetFunction<void(std::optional<uint32_t> block)> onDone;
    };
}

#endif
