#ifndef VALIDATION_FIRMWARE_BOARDS_BOARD_INFO_STM_HPP
#define VALIDATION_FIRMWARE_BOARDS_BOARD_INFO_STM_HPP

#include "services/hil/HilBoardInfo.hpp"

namespace validation
{
    const char* ReadAndClearResetCause();

    class BoardInfoStm
        : public services::HilBoardInfo
    {
    public:
        BoardInfoStm(const char* name, const char* family, const char* resetCause);

        const char* Name() const override;
        const char* Family() const override;
        uint32_t SystemClock() const override;
        const char* ResetCause() const override;
        infra::ConstByteRange UniqueId() const override;

    private:
        const char* name;
        const char* family;
        const char* resetCause;
    };
}

#endif
