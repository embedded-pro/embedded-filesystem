#include "validation/firmware/boards/BoardInfoStm.hpp"
#include "hal_st/stm32fxxx/UniqueDeviceId.hpp"
#include "services/hil/HilArguments.hpp"
#include <array>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        // A power-on reset also sets the brown-out and pin flags, and every other reset the pin flag, so the order decides
        constexpr std::array<services::HilChoice<uint8_t>, 7> resetCauses{ {
            { "iwdg", RCC_FLAG_IWDG1RST },
            { "wwdg", RCC_FLAG_WWDG1RST },
            { "sw", RCC_FLAG_SFTRST },
            { "lpwr", RCC_FLAG_LPWR1RST },
            { "por", RCC_FLAG_PORRST },
            { "bor", RCC_FLAG_BORRST },
            { "pin", RCC_FLAG_PINRST },
        } };
    }

    const char* ReadAndClearResetCause()
    {
        const char* cause = "unknown";

        for (const auto& entry : resetCauses)
            if (__HAL_RCC_GET_FLAG(entry.value) != 0)
            {
                cause = entry.name;
                break;
            }

        __HAL_RCC_CLEAR_RESET_FLAGS();
        return cause;
    }

    BoardInfoStm::BoardInfoStm(const char* name, const char* family, const char* resetCause)
        : name(name)
        , family(family)
        , resetCause(resetCause)
    {}

    const char* BoardInfoStm::Name() const
    {
        return name;
    }

    const char* BoardInfoStm::Family() const
    {
        return family;
    }

    uint32_t BoardInfoStm::SystemClock() const
    {
        return SystemCoreClock;
    }

    const char* BoardInfoStm::ResetCause() const
    {
        return resetCause;
    }

    infra::ConstByteRange BoardInfoStm::UniqueId() const
    {
        return hal::UniqueDeviceId();
    }
}
