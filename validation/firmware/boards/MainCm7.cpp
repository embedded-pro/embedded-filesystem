#include "BoardProfile.hpp"
#include "hal/cortex_m/Reset.hpp"
#include "hal_st/instantiations/StmEventInfrastructure.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/UartStm.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilPinNaming.hpp"
#include "services/hil/HilPinPool.hpp"
#include "services/hil/HilResponse.hpp"
#include "services/hil/HilSystemCommands.hpp"
#include "services/hil/HilTerminal.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/Tracer.hpp"
#include "validation/firmware/boards/BoardInfoStm.hpp"
#include "validation/firmware/boards/Clocks.hpp"
#include "validation/firmware/boards/Memories.hpp"
#include "validation/firmware/hil/FirmwareCommands.hpp"
#include "validation/firmware/hil/NoPins.hpp"
#include DEVICE_HEADER

unsigned int hse_value = validation::board::hseValue;

int main()
{
    const bool cortexM4Stopped = hal::WaitForCortexM4Stop();

    HAL_Init();
    validation::ConfigureSystemClock();
    validation::ConfigureMemories(infra::MakeRange(validation::board::memoryRegions));

    static const char* resetCause = validation::ReadAndClearResetCause();
    static main_::StmEventInfrastructure eventInfrastructure;

    static hal::GpioPinStm terminalTx{ validation::board::terminalPort, validation::board::terminalTx };
    static hal::GpioPinStm terminalRx{ validation::board::terminalPort, validation::board::terminalRx };
    static hal::UartStm uart{ validation::board::terminalUart, terminalTx, terminalRx, hal::UartStm::Config{ validation::board::terminalBaudRate } };
    static services::StreamWriterOnSerialCommunication::WithStorage<2048> writer{ uart };
    static infra::TextOutputStream::WithErrorPolicy stream{ writer };
    static services::TracerToStream tracer{ stream };
    static services::HilResponse response{ tracer };
    static services::HilTerminal::WithMaxQueueAndMaxHistory<256, 1> terminal{ uart, tracer, response };

    static validation::NoPins pinFactory;
    static services::HilPinPool::WithCapacity<1> pins{ pinFactory };
    static services::HilPinNamingDefault naming{ "ABCDEFGHIJK", 15 };
    static services::HilContext context{ response, pins, naming, terminal };

    static validation::BoardInfoStm boardInfo{ validation::board::name, validation::board::family, resetCause };
    static hal::cortex::Reset reset;
    static services::HilSystemCommands system{ context, boardInfo, reset };
    static validation::FirmwareCommands firmware{ context, validation::FirmwareInfo{ EMFS_VALIDATION_COMMIT, cortexM4Stopped } };

    system.PrintBoot();
    eventInfrastructure.Run();
    __builtin_unreachable();
}
