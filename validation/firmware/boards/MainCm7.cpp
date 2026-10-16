#include "BoardProfile.hpp"
#include "hal/cortex_m/Reset.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "hal_st/stm32fxxx/FmcStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
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
#include "validation/firmware/boards/EventInfrastructure.hpp"
#include "validation/firmware/boards/Memories.hpp"
#include "validation/firmware/cycles/CycleRun.hpp"
#include "validation/firmware/cycles/CyclesCommands.hpp"
#include "validation/firmware/cycles/subjects/CycleConfigurations.hpp"
#include "validation/firmware/cycles/subjects/FatFsSubject.hpp"
#include "validation/firmware/cycles/subjects/FileSystemSubject.hpp"
#include "validation/firmware/cycles/subjects/LittleFsSubject.hpp"
#include "validation/firmware/devices/RamBlockDevice.hpp"
#include "validation/firmware/devices/RamFlash.hpp"
#include "validation/firmware/hil/FirmwareCommands.hpp"
#include "validation/firmware/hil/NoPins.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include <array>
#include DEVICE_HEADER

unsigned int hse_value = validation::board::hseValue;

namespace
{
    constexpr std::size_t ramDeviceBytes = std::size_t{ validation::cycles::ramFlashSectors } * validation::cycles::ramFlashSectorSize;

    static_assert(std::size_t{ validation::cycles::ramBlockDeviceBlocks } * validation::RamBlockDevice::blockSize == ramDeviceBytes);
    static_assert(2 * ramDeviceBytes <= validation::board::sdRamSize);

    const validation::CycleDevice ramFlash{ "ramflash", validation::CycleDeviceKind::nor, validation::cycles::ramFlashSectors, validation::cycles::ramFlashSectorSize };
    const validation::CycleDevice ramBlock{ "ramblock", validation::CycleDeviceKind::block, validation::cycles::ramBlockDeviceBlocks, validation::RamBlockDevice::blockSize };
}

int main()
{
    const bool cortexM4Stopped = hal::WaitForCortexM4Stop();

    HAL_Init();
    validation::ConfigureSystemClock();
    validation::ConfigureMemories(infra::MakeRange(validation::board::memoryRegions));

    static const char* resetCause = validation::ReadAndClearResetCause();
    static validation::EventInfrastructure eventInfrastructure;

    static hal::MultiGpioPinStm fmcPins{ validation::board::fmcPins, hal::Drive::PushPull, hal::Speed::High };
    static hal::FmcStm fmc{ fmcPins };
    static hal::SdRamStm sdram{ fmc, validation::board::SdRamConfig() };

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

    // The baselines block, so they use views of the RAM devices that complete within the call
    auto flashMemory = infra::Head(sdram.Memory(), ramDeviceBytes);
    auto blockMemory = infra::Head(infra::DiscardHead(sdram.Memory(), ramDeviceBytes), ramDeviceBytes);
    static validation::RamFlash flash{ flashMemory, validation::cycles::ramFlashSectorSize };
    static validation::RamFlash blockingFlash{ flashMemory, validation::cycles::ramFlashSectorSize, validation::RamCompletion::immediate };
    static validation::RamBlockDevice blockDevice{ blockMemory };
    static validation::RamBlockDevice blockingBlockDevice{ blockMemory, validation::RamCompletion::immediate };
    static validation::EmfsFatSubject emfsFat{ blockDevice };
    static validation::EmfsNorSubject emfsNor{ flash };
    static validation::FatFsSubject fatFs{ blockingBlockDevice };
    static validation::LittleFsSubject littleFs{ blockingFlash };
    static const std::array<validation::CycleTarget, 4> targets{ { { emfsFat, ramBlock }, { emfsNor, ramFlash }, { fatFs, ramBlock }, { littleFs, ramFlash } } };
    static validation::ResultLines results{ response };
    static validation::CycleRun cycleRun{ results, eventInfrastructure.cycles, eventInfrastructure.idle };
    static validation::CyclesCommands cycles{ context, results, cycleRun, infra::MakeRange(targets) };

    system.PrintBoot();
    eventInfrastructure.Run();
    __builtin_unreachable();
}
