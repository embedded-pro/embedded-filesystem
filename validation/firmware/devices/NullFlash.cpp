#include "validation/firmware/devices/NullFlash.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>

namespace validation
{
    NullFlash::NullFlash(uint32_t numberOfSectors, uint32_t sizeOfEachSector)
        : hal::FlashHomogeneous(numberOfSectors, sizeOfEachSector)
    {}

    void NullFlash::WriteBuffer(infra::ConstByteRange, uint32_t, infra::Function<void()> onDone)
    {
        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void NullFlash::ReadBuffer(infra::ByteRange buffer, uint32_t, infra::Function<void()> onDone)
    {
        std::fill(buffer.begin(), buffer.end(), 0xff);
        infra::EventDispatcher::Instance().Schedule(onDone);
    }

    void NullFlash::EraseSectors(uint32_t, uint32_t, infra::Function<void()> onDone)
    {
        infra::EventDispatcher::Instance().Schedule(onDone);
    }
}
