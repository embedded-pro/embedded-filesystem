#include "BoardProfile.hpp"
#include "hal/cortex_m/Reset.hpp"
#include "hal_st/stm32fxxx/DualCoreHandshakeStm.hpp"
#include "hal_st/stm32fxxx/FmcStm.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "hal_st/stm32fxxx/NorFlashStm.hpp"
#include "hal_st/stm32fxxx/QuadSpiStm.hpp"
#include "hal_st/stm32fxxx/SdRamStm.hpp"
#include "hal_st/stm32fxxx/UartStm.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "metrics/nor/NorConfigurations.hpp"
#include "services/flash/FlashGeometryQuadSfdp.hpp"
#include "services/hil/HilCommand.hpp"
#include "services/hil/HilPinNaming.hpp"
#include "services/hil/HilPinPool.hpp"
#include "services/hil/HilResponse.hpp"
#include "services/hil/HilSystemCommands.hpp"
#include "services/hil/HilTerminal.hpp"
#include "services/tracer/StreamWriterOnSerialCommunication.hpp"
#include "services/tracer/Tracer.hpp"
#include "validation/firmware/bench/BenchCommands.hpp"
#include "validation/firmware/bench/NorBench.hpp"
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
#include "validation/firmware/devices/CountingFlash.hpp"
#include "validation/firmware/devices/NullFlash.hpp"
#include "validation/firmware/devices/QuadSpiNor.hpp"
#include "validation/firmware/devices/RamBlockDevice.hpp"
#include "validation/firmware/devices/RamFlash.hpp"
#include "validation/firmware/hil/DeviceCommands.hpp"
#include "validation/firmware/hil/FirmwareCommands.hpp"
#include "validation/firmware/hil/NoPins.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "validation/firmware/workload/DataLogger.hpp"
#include "validation/firmware/workload/MountScaling.hpp"
#include "validation/firmware/workload/NorWorkloadVolume.hpp"
#include "validation/firmware/workload/RandomOverwrite.hpp"
#include "validation/firmware/workload/ReadPatterns.hpp"
#include "validation/firmware/workload/SequentialWrite.hpp"
#include "validation/firmware/workload/SmallFiles.hpp"
#include "validation/firmware/workload/WorkloadCommands.hpp"
#include "validation/firmware/workload/WorkloadRunner.hpp"
#include <array>
#include <chrono>
#include DEVICE_HEADER

unsigned int hse_value = validation::board::hseValue;

namespace
{
    constexpr std::size_t ramFlashBytes = std::size_t{ validation::cycles::ramFlashSectors } * validation::cycles::ramFlashSectorSize;
    constexpr std::size_t ramBlockBytes = std::size_t{ validation::cycles::ramBlockDeviceBlocks } * validation::RamBlockDevice::blockSize;

    static_assert(ramFlashBytes + ramBlockBytes <= validation::board::sdRamSize);

    const validation::DeviceDescription ramFlash{ "ramflash", validation::DeviceKind::nor, validation::cycles::ramFlashSectors, validation::cycles::ramFlashSectorSize, 0, "", 0, "sdram" };
    const validation::DeviceDescription ramBlock{ "ramblock", validation::DeviceKind::block, validation::cycles::ramBlockDeviceBlocks, validation::RamBlockDevice::blockSize, 0, "", 0, "sdram" };

    using namespace std::chrono_literals;

    constexpr uint32_t quadSpiRegionSectors = 4096;
    constexpr uint32_t quadSpiPageSize = 256;
    constexpr uint32_t parallelNorPageSize = validation::board::ParallelNorConfig().value_or(hal::NorFlashStm::Config{}).writeBufferBytes;
    constexpr validation::FlashGuard quadSpiGuard{ 100ms, 20ms, 2s };
    constexpr validation::FlashGuard parallelNorGuard{ 100ms, 20ms, 8s };
    constexpr std::size_t quadSpiDevice = 0;
    constexpr std::size_t parallelNorDevice = 1;

    std::array<validation::DeviceEntry, 4> devices{ {
        { { "qspi", validation::DeviceKind::nor, 0, 0, 0, "", 0, "" }, false },
        { { "fmc", validation::DeviceKind::nor, 0, 0, 0, "", 0, "" }, false },
        { ramFlash, true },
        { ramBlock, true },
    } };

    infra::BoundedConstString Hex(infra::BoundedString& text, uint32_t value, std::size_t digits)
    {
        text.clear();
        infra::StringOutputStream stream{ text };
        stream << infra::hex << infra::Width(digits, '0') << value;
        return text;
    }

    void DescribeQuadSpiNor(const validation::QuadSpiNor& nor, const services::FlashGeometryQuad& geometry, bool present)
    {
        static infra::BoundedString::WithStorage<8> chip;
        auto& entry = devices[quadSpiDevice];
        entry.present = present && geometry.SizePage() == quadSpiPageSize;

        if (entry.present)
            entry.description = { "qspi", validation::DeviceKind::nor, nor.NumberOfSectors(), nor.SizeOfSector(0), quadSpiPageSize, Hex(chip, nor.JedecId(), 6),
                HAL_RCC_GetHCLKFreq() / (validation::board::QuadSpiConfig().prescaler + 1), "" };
    }

    template<bool present>
    constexpr fs::NorConfig ParallelNorVolumeConfig()
    {
        constexpr auto chip = validation::board::ParallelNorConfig().value_or(hal::NorFlashStm::Config{});
        return { .maxBlocks = chip.numberOfSectors, .programUnit = 2, .pageSize = chip.writeBufferBytes };
    }

    // A template, so that the volume of the parallel NOR is not instantiated for a board without one
    template<bool hasParallelNor = validation::board::ParallelNorConfig().has_value()>
    infra::MemoryRange<const validation::WorkloadTarget> WorkloadTargets(validation::CountingFlash& quadSpi, [[maybe_unused]] validation::CountingFlash& parallelNor)
    {
        static validation::NorWorkloadVolume<metrics::norQspi16mConfig> quadSpiVolume{ quadSpi };
        if constexpr (hasParallelNor)
        {
            static validation::NorWorkloadVolume<ParallelNorVolumeConfig<hasParallelNor>()> parallelNorVolume{ parallelNor };
            static const std::array<validation::WorkloadTarget, 2> targets{ { { devices[quadSpiDevice], quadSpi, quadSpiVolume }, { devices[parallelNorDevice], parallelNor, parallelNorVolume } } };
            return infra::MakeRange(targets);
        }
        else
        {
            static const std::array<validation::WorkloadTarget, 1> targets{ { { devices[quadSpiDevice], quadSpi, quadSpiVolume } } };
            return infra::MakeRange(targets);
        }
    }

    hal::Flash& ParallelNor(hal::FmcStm& fmc)
    {
        constexpr auto config = validation::board::ParallelNorConfig();
        if constexpr (config.has_value())
        {
            static hal::NorFlashStm nor{ fmc, *config };
            static infra::BoundedString::WithStorage<8> chip;
            const auto identification = nor.ReadIdentification();
            auto& entry = devices[parallelNorDevice];
            entry.description = { "fmc", validation::DeviceKind::nor, nor.NumberOfSectors(), nor.SizeOfSector(0), parallelNorPageSize,
                Hex(chip, static_cast<uint32_t>(identification.manufacturer) << 16 | identification.device1, 8), HAL_RCC_GetHCLKFreq(), "" };
            entry.present = identification.manufacturer != 0x0000 && identification.manufacturer != 0xffff;
            return nor;
        }
        else
        {
            static validation::NullFlash absent{ 1, parallelNorPageSize };
            return absent;
        }
    }
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
    // The phases of a workload can end in quick succession, and their records are kilobytes each
    static services::StreamWriterOnSerialCommunication::WithStorage<16384> writer{ uart };
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

    static validation::ResultLines results{ response };
    static validation::DeviceCommands deviceCommands{ context, results, infra::MakeRange(devices) };

    static hal::GpioPinStm quadSpiClock{ validation::board::quadSpiPins[0].first, validation::board::quadSpiPins[0].second };
    static hal::GpioPinStm quadSpiSelect{ validation::board::quadSpiPins[1].first, validation::board::quadSpiPins[1].second };
    static hal::GpioPinStm quadSpiData0{ validation::board::quadSpiPins[2].first, validation::board::quadSpiPins[2].second };
    static hal::GpioPinStm quadSpiData1{ validation::board::quadSpiPins[3].first, validation::board::quadSpiPins[3].second };
    static hal::GpioPinStm quadSpiData2{ validation::board::quadSpiPins[4].first, validation::board::quadSpiPins[4].second };
    static hal::GpioPinStm quadSpiData3{ validation::board::quadSpiPins[5].first, validation::board::quadSpiPins[5].second };
    static hal::QuadSpiStm quadSpi{ quadSpiClock, quadSpiSelect, quadSpiData0, quadSpiData1, quadSpiData2, quadSpiData3, validation::board::QuadSpiConfig() };
    static validation::QuadSpiNor quadSpiNor{ quadSpi, quadSpiRegionSectors };
    static services::FlashGeometryQuadSfdp quadSpiGeometry{ quadSpi, []
        {
            quadSpiNor.Start(quadSpiGeometry, [](bool present)
                {
                    DescribeQuadSpiNor(quadSpiNor, quadSpiGeometry, present);
                });
        } };
    static validation::CountingFlash quadSpiCounting{ quadSpiNor, quadSpiPageSize, eventInfrastructure.cycles, SystemCoreClock, quadSpiGuard };
    static validation::CountingFlash parallelNorCounting{ ParallelNor(fmc), parallelNorPageSize, eventInfrastructure.cycles, SystemCoreClock, parallelNorGuard };
    static const std::array<validation::NorBenchTarget, 2> benchTargets{ { { devices[quadSpiDevice], quadSpiCounting }, { devices[parallelNorDevice], parallelNorCounting } } };
    static validation::NorBench bench{ results, SystemCoreClock };
    static validation::BenchCommands benchCommands{ context, bench, infra::MakeRange(benchTargets) };

    static validation::WorkloadRunner workloadRunner{ results, eventInfrastructure.cycles, SystemCoreClock };
    static validation::WorkloadTools workloadTools{ workloadRunner };
    static validation::SequentialWrite sequentialWrite{ workloadTools };
    static validation::DataLogger dataLogger{ workloadTools };
    static validation::RandomOverwrite randomOverwrite{ workloadTools };
    static validation::SmallFiles smallFiles{ workloadTools };
    static validation::ReadPatterns readPatterns{ workloadTools };
    static validation::MountScaling mountScaling{ workloadTools };
    static const std::array<validation::Workload*, 6> workloads{ { &sequentialWrite, &dataLogger, &randomOverwrite, &smallFiles, &readPatterns, &mountScaling } };
    static validation::WorkloadCommands workloadCommands{ context, workloadRunner, infra::MakeRange(workloads), WorkloadTargets(quadSpiCounting, parallelNorCounting) };

    // The baselines block, so they use views of the RAM devices that complete within the call
    auto flashMemory = infra::Head(sdram.Memory(), ramFlashBytes);
    auto blockMemory = infra::Head(infra::DiscardHead(sdram.Memory(), ramFlashBytes), ramBlockBytes);
    static validation::RamFlash flash{ flashMemory, validation::cycles::ramFlashSectorSize };
    static validation::RamFlash blockingFlash{ flashMemory, validation::cycles::ramFlashSectorSize, validation::RamCompletion::immediate };
    static validation::RamBlockDevice blockDevice{ blockMemory };
    static validation::RamBlockDevice blockingBlockDevice{ blockMemory, validation::RamCompletion::immediate };
    static validation::EmfsFatSubject emfsFat{ blockDevice };
    static validation::EmfsNorSubject emfsNor{ flash };
    static validation::FatFsSubject fatFs{ blockingBlockDevice };
    static validation::LittleFsSubject littleFs{ blockingFlash };
    static const std::array<validation::CycleTarget, 4> targets{ { { emfsFat, ramBlock }, { emfsNor, ramFlash }, { fatFs, ramBlock }, { littleFs, ramFlash } } };
    static validation::CycleRun cycleRun{ results, eventInfrastructure.cycles, eventInfrastructure.idle };
    static validation::CyclesCommands cycles{ context, results, cycleRun, infra::MakeRange(targets) };

    system.PrintBoot();
    eventInfrastructure.Run();
    __builtin_unreachable();
}
