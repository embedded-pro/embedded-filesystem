#include "validation/firmware/devices/RamFlash.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace validation
{
    RamFlash::RamFlash(infra::ByteRange storage, uint32_t sizeOfEachSector)
        : hal::FlashHomogeneous(static_cast<uint32_t>(storage.size() / sizeOfEachSector), sizeOfEachSector)
        , storage(storage)
    {
        really_assert(storage.size() % sizeOfEachSector == 0);
        std::fill(storage.begin(), storage.end(), 0xff);
    }

    void RamFlash::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        really_assert(address <= storage.size() && buffer.size() <= storage.size() - address);
        std::transform(buffer.begin(), buffer.end(), storage.begin() + address, storage.begin() + address, [](uint8_t programmed, uint8_t stored)
            {
                return static_cast<uint8_t>(programmed & stored);
            });
        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void RamFlash::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        really_assert(address <= storage.size() && buffer.size() <= storage.size() - address);
        std::copy_n(storage.begin() + address, buffer.size(), buffer.begin());
        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void RamFlash::EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone)
    {
        really_assert(beginIndex <= endIndex && endIndex <= NumberOfSectors());
        std::fill(storage.begin() + AddressOfSector(beginIndex), storage.begin() + AddressOfSector(endIndex), 0xff);
        infra::EventDispatcher::Instance().Schedule(onDone);
    }
}
