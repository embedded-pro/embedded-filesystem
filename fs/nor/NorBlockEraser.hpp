#ifndef FS_NOR_NOR_BLOCK_ERASER_HPP
#define FS_NOR_NOR_BLOCK_ERASER_HPP

#include "fs/nor/NorWear.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorBlockEraser
    {
    public:
        explicit NorBlockEraser(NorVolume& volume);
        NorBlockEraser(const NorBlockEraser& other) = delete;
        NorBlockEraser& operator=(const NorBlockEraser& other) = delete;
        ~NorBlockEraser();

        bool HasWork() const;
        void Step(const infra::Function<void()>& onDone);
        void Step(NorWear wear, const infra::Function<void()>& onDone);
        void Refill(const infra::Function<void()>& onDone);
        void CountAsRefill();
        void Reset();

    private:
        void ProgramPartA();

    private:
        NorVolume& volume;
        std::optional<uint32_t> block;
        uint32_t sector{ 0 };
        std::optional<uint32_t> refilledCall;
        infra::AutoResetFunction<void()> onStepped;
    };
}

#endif
