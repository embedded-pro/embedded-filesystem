#include "validation/firmware/hil/NoPins.hpp"

namespace validation
{
    bool NoPins::IsValid(services::HilPinId) const
    {
        return false;
    }

    bool NoPins::SupportsFunction(services::HilPinId, uint16_t, uint8_t) const
    {
        return false;
    }

    bool NoPins::SupportsAnalog(services::HilPinId) const
    {
        return false;
    }

    bool NoPins::SupportsInterrupt(services::HilPinId) const
    {
        return false;
    }

    std::optional<uint8_t> NoPins::ParseDrive(infra::BoundedConstString) const
    {
        return std::nullopt;
    }

    hal::GpioPin& NoPins::Construct(std::size_t, services::HilPinId, const services::HilPinOptions&)
    {
        return pin;
    }

    void NoPins::Destroy(std::size_t)
    {}
}
