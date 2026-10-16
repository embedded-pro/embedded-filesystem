#include "BoardProfile.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"

unsigned int hse_value = validation::board::hseValue;

// The Cortex-M7 never releases this core, but any event wakes it from stop mode
int main()
{
    for (;;)
        hal::WaitForCortexM7();
}
