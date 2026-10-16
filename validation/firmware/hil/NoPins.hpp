#ifndef VALIDATION_FIRMWARE_HIL_NO_PINS_HPP
#define VALIDATION_FIRMWARE_HIL_NO_PINS_HPP

#include "hal/interfaces/Gpio.hpp"
#include "services/hil/HilPinPool.hpp"

namespace validation
{
    class NoPins
        : public services::HilPinFactory
    {
    public:
        NoPins() = default;
        NoPins(const NoPins& other) = delete;
        NoPins& operator=(const NoPins& other) = delete;
        ~NoPins() = default;

        bool IsValid(services::HilPinId pin) const override;
        bool SupportsFunction(services::HilPinId pin, uint16_t function, uint8_t instance) const override;
        bool SupportsAnalog(services::HilPinId pin) const override;
        bool SupportsInterrupt(services::HilPinId pin) const override;
        std::optional<uint8_t> ParseDrive(infra::BoundedConstString text) const override;

        hal::GpioPin& Construct(std::size_t slot, services::HilPinId pin, const services::HilPinOptions& options) override;
        void Destroy(std::size_t slot) override;

    private:
        hal::DummyPin pin;
    };
}

#endif
