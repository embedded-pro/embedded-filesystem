#ifndef FS_FAT_FIBER_FAT_FIBER_WINDOW_HPP
#define FS_FAT_FIBER_FAT_FIBER_WINDOW_HPP

#include "fs/fat/fiber/Fiber.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>
#include <limits>

namespace fs
{
    class FatFiberWindow
    {
    public:
        FatFiberWindow(fiber::Runner& runner, infra::ByteRange buffer);
        FatFiberWindow(const FatFiberWindow& other) = delete;
        FatFiberWindow& operator=(const FatFiberWindow& other) = delete;
        ~FatFiberWindow() = default;

        Result Read(uint32_t sector);
        infra::ConstByteRange Contents() const;
        uint32_t SectorSize() const;
        void Forget();

    private:
        static constexpr uint32_t none = std::numeric_limits<uint32_t>::max();

        fiber::Runner& runner;
        infra::ByteRange buffer;
        uint32_t loaded{ none };
    };
}

#endif
