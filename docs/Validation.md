# Validation

## Introduction

The metrics of [Metrics.md](Metrics.md) run on the host, on models of the storage devices.
Validation runs the filesystems on boards, against real drivers, and measures what the host cannot: the timings that calibrate the device models, the processor cycles of F-2, and the layout that the formatter of the SD Association gives a card.
It follows the validation of hal-st: a firmware that answers commands over the serial port of the board, and a Python package on the host that runs scenarios against it and records what it answers.
The footprint of F-1 needs no board: it is measured in CI on reference images built for Cortex-M.
This document describes the layout, the build, the boards, the firmware, the host package and the procedures; [Roadmap.md](Roadmap.md) lists the steps that build them.

## Layout

```text
validation/
    CMakeLists.txt          fetches hal-st and adds the rest
    README.md               the bench guide: build, flash, run, and send the results
    PROTOCOL.md             the commands of the firmware and its result lines
    firmware/               C++ for the boards
        baselines/          the glue of FatFs and littlefs
        boards/stm32h757/BoardProfile.hpp
        boards/stm32h745/BoardProfile.hpp
        devices/            the null, RAM and counting devices
        hil/                the command groups
        results/            the result lines
    footprint/              the reference images of F-1
    host/                   the Python package emfs_validation
        boards/stm32h757i_eval.yaml
        boards/stm32h745i_disco.yaml
        emfs_validation/
        tests/hil/features/ the scenarios that run on a board
        tests/unit/         the tests of the package itself
    results/<board>/<run>/  the runs on the boards, as committed
```

The C++ of `validation/firmware` is split in two:

* Libraries that need no board: the null, RAM and counting devices, the glue of FatFs and littlefs, the benchmarks, the workload runner, the parser of FAT layouts, the result lines and the command groups.
  They are written against `hal::Flash`, `hal::BlockDevice` and the terminal of EmIL, built and tested on the host, and covered 100 % like `fs/`.
* Board files: the board profiles, the `main` of both cores and the glue to the drivers of hal-st.
  They build only for the boards, and are excluded from the coverage and from SonarCloud.

The firmware follows the rules of `fs/`: no heap, every completion through the event dispatcher, explicit state machines, and tests with `testing::StrictMock` only.
Its namespace is `validation`, and its targets are named `validation.*`; the coverage and the clang-format targets of the top level cover `validation/firmware` next to `fs/`.

## Build

`EMFS_BUILD_VALIDATION`, off by default, adds `validation/` when embedded-filesystem is built by itself.
These presets turn it on, and what it builds depends on the configuration:

| Configuration            | Presets                                                                                 | Built                                                                                                                                                                  |
|--------------------------|-----------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Host                     | `host`, `host-single-Debug`, `host-single-MinSizeRel`, `coverage`, `Windows`, `metrics` | The libraries of `validation/firmware`, and their tests with `EMFS_BUILD_TESTS`; with `EMFS_BUILD_METRICS`, the conversion of the committed runs into report fragments |
| Cortex-M without a board | `cortex-m4`, `cortex-m7`, `cortex-m33`                                                  | The reference images of F-1                                                                                                                                            |
| ST board                 | `stm32h757-cm7`, `stm32h757-cm4`, `stm32h745-cm7`, `stm32h745-cm4`                      | hal-st, and the validation firmware for the Cortex-M7 or the parked image for the Cortex-M4                                                                            |

hal-st is fetched with FetchContent in `validation/CMakeLists.txt` only, and only when `EMFS_STANDALONE` and `EMFS_BUILD_VALIDATION` are on and `TARGET_MCU_VENDOR` is `st`.
Nothing in `fs/` or `metrics/` depends on it, and a project that consumes embedded-filesystem as a library never adds `validation/`, so it never fetches hal-st.
hal-st is pinned to a full commit SHA, `GIT_TAG <sha> # Unreleased`, and `.github/workflows/update-hal-st-git-tag.yml` moves it to the latest commit every day, as `update-emil-git-tag.yml` does for EmIL.
As a subproject, hal-st uses the EmIL of embedded-filesystem, not its own pin; both follow the `main` branch of EmIL, and a bump that no longer builds shows up as a failing pull request of the bump.
The startup code of hal-st is assembly, so the top-level `CMakeLists.txt` enables the ASM language when validation is built for an ST board; nothing else outside `validation/` refers to hal-st.

FatFs and littlefs, which F-1 and F-2 build for Cortex-M, are pinned in `cmake/emfs_third_party.cmake`, which the top level includes when `metrics/` or `validation/` is built, so that both use the same versions.
The file fetches the sources and builds their C code as `third_party.fatfs_default`, `third_party.fatfs_tuned`, `third_party.fatfs_minimal` and `third_party.fatfs_read_only`, with their generated `ffconf.h`, and `third_party.littlefs` and `third_party.littlefs_read_only`, with `LFS_READONLY`.
The glue stays with each user: the host shims in `metrics/baselines`, and in `validation/firmware/baselines` the blocking adapters an EmIL application would need, which wait for the completions of the device with `infra::EventDispatcher::ExecuteUntil`:

* `validation::FatFsDisk`, the `disk_*` functions of FatFs over a `hal::BlockDevice`, one library per variant;
* `validation::LittleFsFlash`, the callbacks of littlefs over a `hal::Flash`, one library per variant.

The board presets follow those of hal-st:

| Configure preset | Core      | Toolchain of EmIL                        | `TARGET_MCU` | Image                 |
|------------------|-----------|------------------------------------------|--------------|-----------------------|
| `stm32h757-cm7`  | Cortex-M7 | `toolchain-arm-gcc-m7-fpv5-d16.cmake`    | `stm32h757`  | `validation.firmware` |
| `stm32h757-cm4`  | Cortex-M4 | `toolchain-arm-gcc-m4-fpv4-sp-d16.cmake` | `stm32h757`  | `validation.park`     |
| `stm32h745-cm7`  | Cortex-M7 | `toolchain-arm-gcc-m7-fpv5-d16.cmake`    | `stm32h745`  | `validation.firmware` |
| `stm32h745-cm4`  | Cortex-M4 | `toolchain-arm-gcc-m4-fpv4-sp-d16.cmake` | `stm32h745`  | `validation.park`     |

Each sets `TARGET_MCU_VENDOR` to `st`, `TARGET_MCU_FAMILY` to `stm32h7xx` and `EMFS_BUILD_VALIDATION`, and has the build presets `<preset>-RelWithDebInfo` and `<preset>-MinSizeRel`.
The images are written with their `.bin` and `.hex` next to the `.elf` in `build/<preset>/validation/firmware/boards/<configuration>/`.
CI builds the four presets next to the Cortex-M presets, with `xsltproc` installed for the pin tables that hal-st generates, and `tools/verify.sh` builds them before a push.

## Boards

| Board            | MCU         | Terminal                | NOR flash                                                     | Block device                   | SDRAM                           |
|------------------|-------------|-------------------------|---------------------------------------------------------------|--------------------------------|---------------------------------|
| STM32H757I-EVAL  | STM32H757XI | USART1 on PB14 and PB15 | 64 MiB QSPI flash on bank 1, and a 16 MiB parallel NOR on FMC | microSD card on SDMMC1, 4 bits | 32 MiB at `0xD0000000`          |
| STM32H745I-DISCO | STM32H745XI | USART3 on PB10 and PB11 | 64 MiB QSPI flash on bank 1                                   | 4 GB eMMC on SDMMC1            | 8 MiB of 16 MiB at `0xD0000000` |

Both boards carry two QSPI flash chips, of which hal-st drives the one on bank 1, a Micron MT25QL512 with 4 KiB subsectors.
The board profiles select the board by `TARGET_MCU`, as in the validation of hal-st, and carry what is specific to it: the name and the pins of the terminal, the clocks, the memories and the devices.
The terminal runs at 115200 baud, as in the demos of hal-st.

### Two cores

The Cortex-M7 runs the validation.
It waits with `hal::WaitForCortexM4Stop()` until the Cortex-M4 has stopped, sets up the clocks, and never releases it.
The Cortex-M4 runs `validation.park`, which calls `hal::WaitForCortexM7()` in a loop, since an event other than the release by the Cortex-M7 can wake it, and so stays in stop mode.
Both images are flashed, so that an image a board held before cannot run on the Cortex-M4 and use the peripherals.

### Clocks, memories and time

The clock setup of the H7 boards is part of the demos of hal-st, not of its libraries, so each board profile carries its own, taken from those demos: the Cortex-M7 runs at 400 MHz, and SDMMC1 and QUADSPI get the kernel clocks the demos use.
Both caches of the Cortex-M7 are on, as in a product, and the memory protection unit corrects what the default memory map of the Cortex-M7 gets wrong for the external memories:

| Memory                     | Default map              | Region of the memory protection unit | Because                                                                          |
|----------------------------|--------------------------|--------------------------------------|----------------------------------------------------------------------------------|
| Parallel NOR, `0x60000000` | Normal memory, cacheable | Device memory                        | Its commands and status reads must reach the chip, not the data cache            |
| SDRAM, `0xD0000000`        | Device memory            | Normal memory, cacheable             | The RAM devices are read and written at any alignment, and cached like a product |

The QSPI flash is used in indirect mode, not mapped into memory, so it needs no region.
`hal::SdCardStm` moves data with the internal DMA of SDMMC1 and keeps the data cache coherent itself, for buffers aligned to 4 bytes, and aligned and sized to the cache line of 32 bytes while the data cache is on; [Fat.md](Fat.md#direct-transfers) describes how FAT aligns its transfers for such a driver.

Durations are measured with the cycle counter of the data watchpoint and trace unit of the Cortex-M7, through `hal::cortex::DataWatchpointAndTrace` of EmIL, which unlocks the counter so that it counts without a debugger.
At 400 MHz the counter wraps after 10.7 s, so a duration is the difference of two readings of the free-running counter, and spans that may be longer, such as a phase of a workload, are measured with `infra::Now()`.

## Devices

| Device in commands | Board | Driver                                                                                                | Volume                                               | Configuration                                                                                                                |
|--------------------|-------|-------------------------------------------------------------------------------------------------------|------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------|
| `qspi`             | Both  | `hal::QuadSpiStm`, with `services::FlashGeometryQuadSfdp` and `services::FlashQuadSpiGeneric` of EmIL | `services::FlashRegion` of the first 16 MiB          | The `fs::NorConfig` of the NOR subject on `nor-qspi-16m`: 4096 blocks of one 4 KiB sector, program unit 1, page of 256 bytes |
| `fmc`              | EVAL  | `hal::NorFlashStm`                                                                                    | The whole 16 MiB                                     | 128 blocks of one 128 KiB sector, program unit 2, and the write buffer of the chip as the page                               |
| `sd`               | EVAL  | `hal::SdCardStm`                                                                                      | `validation::BlockDeviceRegion` of the first 256 MiB | The FAT subject of `sd-card-256m`: `fs::FatFileSystem::WithStorage<2, 8>`, with the allocation unit of the card              |
| `emmc`             | DISCO | The eMMC driver of hal-st                                                                             | `validation::BlockDeviceRegion` of the first 256 MiB | As `sd`, with the high-capacity erase group of the eMMC as the allocation unit                                               |
| `ramflash`         | Both  | `validation::RamFlash`, NOR semantics in SDRAM                                                        | 2 MiB                                                | For F-2: as `qspi`, with 512 blocks                                                                                          |
| `ramblock`         | Both  | `validation::RamBlockDevice` in SDRAM                                                                 | 2 MiB                                                | For F-2: as `sd`, which `fs::FatFormatter` formats as FAT12 at this size, without an allocation unit                         |

The QSPI flash is set up as the demos of hal-st do it: `services::FlashGeometryQuadSfdp` reads the geometry and the read command from the SFDP tables, the firmware aligns the dummy cycles of the volatile configuration register, and `services::FlashQuadSpiGeneric` runs with `Protocol::extendedSpi`.
`services::FlashQuadSpiGeneric` and `services::FlashRegion` copy the geometry when they are constructed, so they are constructed once the SFDP tables have been read.
The firmware drives A20 to A22 of the parallel NOR, on PE4 to PE6, which the demo of hal-st leaves to the audio interface they share, so that all 16 MiB are reachable.
`validation::BlockDeviceRegion` maps a range of blocks of a block device that starts on an allocation unit; the region of `sd` and `emmc` holds a FAT16 volume formatted by `fs::FatFormatter`, as `sd-card-256m` does on the host.
The volumes have the geometry of the models and are filled to the fill levels of the host, so that C-1 compares a model with the device on the requests the host would make.

Every device request runs under a guard timer, and a request that does not complete in time ends the command with `ERR timeout` instead of leaving the board waiting.

### What validation erases

The commands that write erase what the device held: the first 16 MiB of the QSPI flash of both boards, which on a board fresh from ST hold the images of its demonstration, the whole parallel NOR, the first 256 MiB of the microSD card and of the eMMC, with the file system that starts there, and the whole card for `layout.format`.
`layout.read` only reads, and runs before anything else writes a card.

### Prerequisites in hal-st and EmIL

The drivers need changes before the devices that use them can be validated; each is a pull request in hal-st or EmIL, checked on the boards before the step that needs it:

1. `hal::QuadSpiStm` completes transfers and status polls from interrupts, without DMA, since on the H7 the QUADSPI is served only by the MDMA: the status-match interrupt ends `PollStatus`, and the transfer interrupts end `SendData` and `ReceiveData`; a failed request still calls its completion.
   Today every transfer busy-waits, and an erase holds the event dispatcher for as long as the flash is busy.
2. `hal::NorFlashStm` programs through the write buffer of the chip, a buffer at a time instead of a half-word, and completes programs and erases from the ready/busy output of the chip where the board wires it to an interrupt, and from a timer otherwise, instead of busy-waiting.
3. `hal::SdCardStm` reads the SD status and the CID, and gives the size of the allocation unit and the identification of the card through accessors of its own, since `hal::BlockDevice` has none.
4. An eMMC driver implements `hal::BlockDevice` on SDMMC1 of the STM32H745I-DISCO, with the buffer rules of `hal::SdCardStm`: `Flush` flushes the cache of the eMMC, `EraseBlocks` trims, and the driver gives the high-capacity erase group size of the EXT_CSD register and the CID.
5. In EmIL, `hal::cortex::DataWatchpointAndTrace` unlocks the lock access register of the Cortex-M7, which the cycle counter needs to count without a debugger.

## Firmware

### Commands

The firmware is a `services::HilTerminal` of EmIL with the system commands of EmIL, `ping`, `info`, `reset`, `delay` and `board.pins`, and the command groups of validation, built with `services::HilBind` as EmIL describes for extensions.
After a reset it prints `EVT boot board=<name> family=stm32h7 sysclk=<hz> reset=<cause>`.
Every command answers with one `OK` or `ERR` line, and results are `EVT` lines, printed while the command runs.
A command that runs for minutes is a `services::HilPendingOperation` whose timeout is part of the command.

| Command                                                                             | Does                                                                                           | Answers                                      |
|-------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------|----------------------------------------------|
| `firmware.info`                                                                     | Names the commit the firmware was built from, and whether the Cortex-M4 stopped                | `OK commit=<commit> cm4=<stopped\|running>`  |
| `dev.list`                                                                          | Lists the devices of the board                                                                 | `OK devices=qspi,fmc,sd,ramflash,ramblock`   |
| `dev.info <device>`                                                                 | Identifies a device and prints its geometry                                                    | `EVT device`, `OK`                           |
| `bench.program <device> size=<bytes> n=<count>`                                     | Programs `size` bytes at `n` erased, page-aligned addresses                                    | `EVT bench`, `OK`                            |
| `bench.erase <device> n=<count>`                                                    | Erases `n` sectors                                                                             | `EVT bench`, `OK`                            |
| `bench.read <device> size=<bytes> n=<count>`                                        | Reads `size` bytes `n` times                                                                   | `EVT bench`, `OK`                            |
| `bench.blocks <device> <read\|write> blocks=<count> n=<count>`                      | Reads or writes `n` requests of `blocks` consecutive blocks                                    | `EVT bench`, `OK`                            |
| `bench.units <device> units=<count> n=<count>`                                      | Writes one block into each of `units` allocation units in turn, `n` rounds                     | `EVT bench`, `OK`                            |
| `bench.flush <device> n=<count>`, `bench.discard <device> blocks=<count> n=<count>` | Flushes or discards `n` times                                                                  | `EVT bench`, `OK`                            |
| `fs.format <device>`                                                                | Formats the volume of the device, with `fs::NorFileSystem::Format` or `fs::FatFormatter`       | `OK`                                         |
| `wl.run <device> <workload> [fill=<percent>] [scale=<divisor>]`                     | Runs a workload on the volume of the device                                                    | `EVT phase`, `EVT window`, `OK`              |
| `cycles.run <subject> [n=<count>]`                                                  | Runs the operations of F-2 on a RAM device                                                     | `EVT cycles` per operation, `OK`             |
| `layout.read <device>`                                                              | Reads the MBR and the boot sector of the whole card, and compares them with `fs::FatFormatter` | `EVT layout` twice, `OK differences=<count>` |
| `layout.format <device>`                                                            | Formats the whole card with `fs::FatFormatter`                                                 | `OK`                                         |

`PROTOCOL.md` in `validation/` describes every command and every key, and grows with every step that adds commands, as hal-st does for its validation.

### Result lines

Results follow the framing of the terminal of EmIL, so the host reads them with the same line parser as the answers:

```text
EVT <kind> id=<number> <key>=<value> ...
```

* The `id` numbers the records from 1 after a reset; a record may take several lines with the same `id`, which follow one another, so that every line stays shorter than 255 characters, and the host joins them; a list that does not fit continues under the same key.
* Values contain no spaces, and lists are separated by commas: the phase `L1 1048576 64` is written `phase=L1,1048576,64`.
* Numbers are decimal; durations are in nanoseconds, in keys ending in `Ns`, cycles in keys ending in `Cycles`, and sizes in bytes.
* The host ignores kinds and keys it does not know, so a later firmware can add them without breaking the conversion of older runs; only a `device` record keeps every key, as a parameter of the device.

| Kind     | Keys                                                                                                                                                                                                                                                         | Record                               |
|----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------|
| `device` | `device`, `kind` (`nor` or `block`), the geometry, `chip` (the JEDEC identifier or the CID), `unitBlocks`, `clockHz`                                                                                                                                         | The device a run used                |
| `bench`  | `device`, `op`, `size`, `n`, `minNs`, `medianNs`, `p99Ns`, `maxNs`, `meanNs`                                                                                                                                                                                 | One benchmark                        |
| `phase`  | `device`, `subject`, `phase`, `fill`, `ramBytes`, `elapsedNs`, the counts of the volume, and per operation of the phase, idle periods included: `calls`, `userBytes`, the sum, maximum and histogram of the latencies, and the counts of the counting device | One phase of a workload              |
| `window` | `device`, `subject`, `fill`, `log` (`first` or `second`), `appends`, and the counts of the counting device and of the volume                                                                                                                                 | One window of W-2                    |
| `cycles` | `subject`, `op`, `n`, `ramBytes`, `minCycles`, `medianCycles`, `maxCycles`, `settledCycles`                                                                                                                                                                  | The cycles of one operation, for F-2 |
| `layout` | `source` (`card` or `formatter`), `partitionStart`, `partitionSectors`, `type`, `sectorsPerCluster`, `reservedSectors`, `fats`, `fatSectors`, `rootEntries`, `dataStart`, `differs`                                                                          | One layout of a card                 |

### Counting devices

`validation::CountingFlash` wraps a `hal::Flash` and `validation::CountingBlockDevice` a `hal::BlockDevice`, as `metrics::MeteredFlash` and `metrics::MeteredBlockDevice` do on the host, but without a model: they forward every request, measure the time from the request to its completion, and count:

| Device             | Counts                                                                                                                                                                                                                                                                                                                                |
|--------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `hal::Flash`       | Read requests and bytes, program requests, bytes and pages spanned, erase requests and sectors, and the measured time of each kind                                                                                                                                                                                                    |
| `hal::BlockDevice` | Read and write requests and blocks, the allocation units that writes span, each by its place among the four most recently written units or as none of them, written blocks that start an allocation unit or follow the block before them, distinct allocation units written, flushes and discards, and the measured time of each kind |

Pages spanned and allocation units spanned are counted as `metrics::MeteredFlash` and `metrics::MeteredBlockDevice` count them, so that the time a model gives the same requests follows from the counts, for a model that keeps up to four allocation units open; the size of the allocation unit is the one the card reports, which the calibrated model takes.
The counts of the volume, the blocks it allocated, the sectors it erased, its checkpoints and the blocks it moved, come from `Counters()` of the NOR volume, as on the host.

### Benchmarks

The benchmarks measure each parameter of a model on its own, on the volume of the device, and leave it erased:

| Device | Benchmark                                                                          | Gives                                                                                 |
|--------|------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
| NOR    | Programs of 1, 16, 64 and 256 bytes at erased, page-aligned addresses              | `pageProgram`, and whether the time of a program depends on the bytes it programs     |
| NOR    | Erases of one sector                                                               | `sectorErase`                                                                         |
| NOR    | Reads of 256 bytes to 64 KiB                                                       | `readBytesPerSecond`, and whether a read has a fixed cost                             |
| Block  | Reads and writes of 1 to 128 consecutive blocks                                    | `command`, `blockRead` and `blockWritten`                                             |
| Block  | One block written into each of 1 to 8 allocation units in turn, as flashbench does | `allocationUnitOpening`, `recentAllocationUnits`, and the allocation unit of the card |
| Block  | Flushes, and discards of one allocation unit                                       | The cost of a flush and of a discard                                                  |

Each benchmark keeps the duration of every repetition, at most 1024, in a fixed array, and reports their minimum, median, 99th percentile, maximum and mean.

### Workloads

`validation::WorkloadRunner` runs the workloads of [Metrics.md](Metrics.md#workloads) on a volume of embedded-filesystem, through the API of `fs/interfaces` and the counting device in front of the device.
Each workload is an explicit state machine, as every multi-step operation of `fs/` is, and unlike `metrics::Runner` on the host, the runner keeps no record per call: it aggregates the calls of a phase per operation and prints one `phase` record when the phase ends.
The latency of a call is measured from the call to its completion, and kept in a histogram with logarithmic buckets, so that the host merges the histograms of several phases for the 99th percentile of W-6.
The windows of W-2 depend on the order of the calls, so the runner keeps them itself and prints a `window` record for each: on NOR, a ring of the last checkpoint periods of the first log and the four periods from 4 KiB into the second, and on FAT, the last 64 appends of the first log and the 64 from 4 KiB into the second.
From these records, the host computes W-1 to W-6, R-1 to R-5 and M-1 to M-3 as `metrics/workload/MetricComputations.cpp` computes them from the records of the calls.

L1, L2, L3, L4, L6 and L7 run at the fill levels of the host, with the sizes of the files divided by a scale divisor, 16 by default, as `metrics::WorkloadParameters::scaleDivisor` divides them on the host; L4 keeps its 200 small files, since a divisor of 16 would leave fewer than the 16 it keeps before it removes the oldest.
The phases carry the scaled sizes in their names, such as `L1 65536 64`, so a run is compared with the models through C-1, not with the results of the host.
A device takes one to two hours: `fs::NorFileSystem::Format` erases every block, about 3.4 minutes on the region of the QSPI flash, before each workload and fill level, and L2 makes 6144 appends with their 100 ms idle periods, about 10 minutes per fill level.
The host package runs one device at a time, so that a board takes an evening of unattended runs.
L5 does not run on the boards: its lifetime run would wear the flash of the board.
Only the subjects of embedded-filesystem run the workloads on the boards; the baselines run there only for F-2.

### Processor cycles

`cycles.run <subject>` measures F-2 on a RAM device in the SDRAM, where a request takes no device time, so that every cycle of a call is spent in the filesystem, the event dispatcher and the copies of the RAM device.
The subjects are `embedded-filesystem/fat` on `ramblock`, `embedded-filesystem/nor` on `ramflash`, and the baselines closest in RAM, `fatfs/tuned` on `ramblock` and `littlefs/ram-matched` on `ramflash`, through the glue of [Build](#build); FatFs symbols are global, so one image holds one FatFs configuration.
The operations are those of a product:

| Operation   | Call                                                                        |
|-------------|-----------------------------------------------------------------------------|
| `format`    | Formatting the RAM device                                                   |
| `mount`     | Mounting a volume holding a directory of 100 files                          |
| `open`      | Opening a file of that directory by path                                    |
| `create`    | Creating a file                                                             |
| `append`    | Writing 64 bytes at the end of a file, followed by `Sync`                   |
| `write`     | Writing 4 KiB                                                               |
| `read`      | Reading 512 bytes at a random offset of a file of 1 MiB                     |
| `overwrite` | Writing 512 bytes at a random offset of a file of 1 MiB, followed by `Sync` |
| `list`      | Listing the directory of 100 files                                          |
| `rename`    | Renaming a file                                                             |
| `remove`    | Removing a file                                                             |
| `unmount`   | Unmounting                                                                  |

Each operation runs `n` times, 100 by default, and reports the minimum, median and maximum of the cycles from the call to its completion.
It also reports the cycles the processor is busy from the call until the volume is idle again, without the time the event dispatcher sleeps, which a strategy of `infra::LowPowerEventDispatcher` measures, so that the background work an operation leaves, such as the erases of NOR and the scan of the FAT, counts too.
F-2 records the medians of both, as `<operation>` and `<operation> settled`.

### Formatter comparison

`fs::FatFormatter` chooses the type, the cluster size and the alignment of a volume from the capacity of the card, as [Fat.md](Fat.md#formatting) describes, and is compared with the SD Memory Card Formatter of the SD Association:

1. A card is formatted on a PC with the SD Memory Card Formatter.
2. `layout.read sd` reads the number of blocks of the whole card, the allocation unit from the SD status, the MBR and the boot sector, and prints the layout of the card: the start and the size of the partition, the FAT type, the sectors per cluster, the reserved sectors, the number and the size of the FATs, the entries of the root directory and the start of the data area.
3. It prints the layout that `fs::ComputeFormatLayout` gives for the same number of blocks with the defaults of `fs::FatFormatter`, and `differs` names the fields that are not the same.
4. `layout.format sd` formats the whole card with `fs::FatFormatter`, and the card is checked on a PC with `fsck.fat -n` or `chkdsk`, and written and read there.

`validation::FatLayout` parses the MBR and the boot sector without the code of `fs/fat`, as the checker of the power-cut sweep does, so that the formatter is not compared with its own parser.
The cards cover the rows of the table of [Fat.md](Fat.md#formatting) that SD cards have: up to 2 GB, where the SD Association formats FAT16, and from 4 GB to 32 GB, where it formats FAT32; an SDXC card is formatted with exFAT, so only the start of its partition is compared.
The layouts of every card and the result of the check on the PC are recorded in [Fat.md](Fat.md#formatting), and a difference changes the table of the formatter in the step that records it.

## Host package

`validation/host` is the Python package `emfs_validation`, built like the host package of the validation of hal-st:

* the scenarios are pytest-bdd feature files in `tests/hil/features`, bound to steps in `tests/hil`, and run with the port of a board, `--port`, or on the fake firmware of the package, `--fake`;
* the board file, `--board boards/<board>.yaml`, holds the devices of the board, the scale divisor and the timeouts;
* the terminal is the `FirmwareTerminal` of `ad3-waveforms-bench`, pinned to a commit as hal-st pins it, with `--no-ad3`, since validation needs no Analog Discovery;
* a run writes the transcript of the terminal, every line sent and received, the JUnit report of pytest and `run.json`, into `--results <directory>`;
* the unit tests in `tests/unit` test the parser, the conversion and the steps against a fake firmware, and CI runs them with ruff, as hal-st does.

| Feature          | Scenarios                                                                                      |
|------------------|------------------------------------------------------------------------------------------------|
| `boot.feature`   | The board boots, answers `ping`, `info` and `firmware.info`, and has parked the Cortex-M4      |
| `nor.feature`    | The benchmarks and the workloads on every NOR device of the board                              |
| `block.feature`  | The benchmarks and the workloads on the block device of the board                              |
| `cycles.feature` | F-2 for the four subjects                                                                      |
| `layout.feature` | The comparison of a card formatted by the SD Memory Card Formatter, and the format of the card |

`emfs_validation.convert` turns the transcript of a run into fragments of the report described in [Metrics.md](Metrics.md#reports), which `merge_report.py` takes like any other fragment:

* a `device` record becomes a device of the fragment, named `<board>/<device>`, such as `stm32h757i-eval/qspi`, with the geometry, the chip, the clock, the run and the parameters of the model the run is converted with as its parameters;
* `bench` records become the measured parameters of the device, and the parameters a model would take from them;
* `phase` and `window` records become the metrics of the phases, computed as the host computes them, and C-1 and C-2, from the device models;
* `cycles` records become F-2, with the operation as the workload;
* every result carries `ramBytes`, as the merge needs: the size of the state of the subject on the board.

The device models come from `metrics.device_models`, a host program of the metrics harness that prints the parameters of every model of `metrics/device/DeviceModels.cpp`, and the W-6 bound that `metrics::NorTargets` and `metrics::FatTargets` give for the configuration of every device of the boards.
It writes them as a JSON object keyed by the device of a board, which the conversion takes with `--models`: `{"stm32h757i-eval/qspi": {"model": "nor-qspi-16m", "sectorErase": "..."}}`; a device takes `model` and every other parameter as `model.<parameter>` among its parameters, and a device without an entry, such as a RAM device, takes the run only.
Each kind of record has its converter, and the step that adds a kind adds its converter; records of other kinds are ignored.
The conversion imports only the standard library of Python, and the package imports `ad3-waveforms-bench` only in the modules that talk to a board, so the metrics build runs `python3 -m emfs_validation.convert` without installing the package.

## Results

A run that is sent back is committed in `validation/results/<board>/<run>/`, with the transcript, the JUnit report and `run.json`, which names the commit of the firmware, the board, the card or the chip, and the options of the run; `<run>` is the date, as `2026_10_20`.
The metrics build, with `EMFS_BUILD_VALIDATION` in the `metrics` preset, converts the latest run of every board, by the date in the name of its directory, in the test `validation.convert_runs`, with the current device models once `metrics.device_models` writes them, and `metrics.merge` takes the fragments with the others, so that the comment on the pull request shows them.
The parameters of a device of a run hold the run and the parameters of the model, so a new run or a recalibration is not compared with `main`, while a pull request that changes the conversion of an unchanged run is held to the 5 % rule by `merge_report.py --check`.
Because the committed run is the transcript and not the fragments, C-1 and C-2 follow a recalibration of the models without running the boards again.
The results of the runs are summarized in [Metrics.md](Metrics.md#on-target-results), and the layouts of the cards in [Fat.md](Fat.md#formatting).

## Calibration

The models of [Metrics.md](Metrics.md#device-models) are calibrated from the benchmarks of the runs:

1. The typical timings of a model take the median of the benchmarks; its maximum timings stay at the maxima of the datasheet of the chip, because a run of hours does not see the worst case of a device, and a measured maximum above the datasheet is reported as a finding.
2. `nor-qspi-16m` and `sd-card-256m` keep their names and their geometry, and take the timings of the QSPI flash of the boards and of the microSD card of the run; a card is one of many, so the model records which card it was measured on.
3. `nor-parallel-16m` and a model of the eMMC are added from their datasheets with their devices, and calibrated like the others; they serve C-1 and C-2, and the host metrics do not run on them.
4. When the benchmarks show a cost that a model does not have, such as a program time that grows with the bytes programmed, the model gains that parameter in the step that calibrates it.
5. The host metrics run again on the calibrated models, and [Metrics.md](Metrics.md) gets their new results; `merge_report.py` does not compare results on a changed model with `main`.

C-1 then checks, phase by phase, that a model gives the device time that the device took for the same requests, and C-2 that the longest call of the data logger stays within the W-6 bound of the model.
Both are reported until the first runs show their spread, and a later step sets their targets from the data.
`internal-flash` is not calibrated in this phase: hal-st builds no driver for the internal flash of the H7.

## F-1 reference images

`validation/footprint` builds one reference image per subject for the Cortex-M4, Cortex-M7 and Cortex-M33 presets, the target `validation.footprint`:

| Image                                          | Subject                         | Configuration                                                                                                        |
|------------------------------------------------|---------------------------------|----------------------------------------------------------------------------------------------------------------------|
| `validation.footprint.empty`                   |                                 | The platform alone                                                                                                   |
| `validation.footprint.fat`                     | `embedded-filesystem/fat`       | `fs::FatFileSystem::WithStorage<1, 8>`, the allocation unit of `sd-card-256m`, and `fs::FatFormatter`                |
| `validation.footprint.nor`                     | `embedded-filesystem/nor`       | `fs::NorFileSystem::WithStorage` with the configuration of `nor-qspi-16m` and one file                               |
| `validation.footprint.fatfs_default`, `_tuned` | `fatfs/default`, `/tuned`       | One `FIL`, through `validation::FatFsDisk`; the tuned variant with the allocation unit and a link map of 64 entries  |
| `validation.footprint.littlefs_default`        | `littlefs/default`              | Caches of 16 bytes and one file, through `validation::LittleFsFlash`                                                 |
| `validation.footprint.littlefs_ram_matched`    | `littlefs/ram-matched`          | Caches of one sector, the lookahead of `nor-qspi-16m`, one file and `lfs_fs_gc`, through `validation::LittleFsFlash` |
| `validation.footprint.fat_reads`               | `embedded-filesystem/fat-reads` | As `fat`, without the formatter, with only the calls that read                                                       |
| `validation.footprint.nor_reads`               | `embedded-filesystem/nor-reads` | As `nor`, without `Format`, with only the calls that read                                                            |
| `validation.footprint.fatfs_minimal`           | `fatfs/minimal`                 | As `fatfs/default`, without long names and without `f_mkfs`                                                          |
| `validation.footprint.fatfs_read_only`         | `fatfs/read-only`               | `FF_FS_READONLY` with long names, with only the calls that read                                                      |
| `validation.footprint.littlefs_read_only`      | `littlefs/read-only`            | `LFS_READONLY` with the caches of `littlefs/default`, with only the calls that read                                  |

The configurations are those of the host metrics, with one open file, as their `RamBytes()` counts; `Configurations.hpp` holds them, and `metrics.baselines_test` checks that they equal the device models and the configurations of the host.
Each image calls every operation of the API of its subject, and the formatter, on a device that does nothing, so that the linker keeps every function a product could call; `validation::footprint::Exercise` makes the calls of `fs::FileSystem` for both subjects of embedded-filesystem.
The images of the calls that read call only `Mount`, `Open` in `read` mode, `Seek`, `Read`, `Close`, `OpenDirectory`, `Next`, `Stat` and `Unmount`, through `validation::footprint::ExerciseReads`, or the same functions of FatFs and littlefs, so that F-1 shows how much of its code a product that only reads still links.
`fatfs/minimal` and the read-only baselines are what a smaller profile of embedded-filesystem is compared with; FatFs and littlefs built read-only have no free-space query, so the images of the calls that read leave out `Usage`.
The volumes and their buffers are static, so that their RAM is counted, in `infra::StaticStorage`, so that the destructors a product never runs are not linked; the formatter of `fs/fat`, the work area of `f_mkfs` and the directory and entry structures of FatFs and littlefs are on the stack of `main`, as `RamBytes()` leaves them out on the host, and the buffer of the reads and writes is in the platform.
F-1 counts the code of EmIL that a subject uses and the empty image does not, such as `infra::SharedPtr`, `infra::ClaimableResource` and the timers of the idle work of NOR, since a product that has none of the subjects does not link it either.

Every image holds the same platform, `validation::footprint::Platform`: the interrupt table, an `infra::EventDispatcherWithWeakPtr`, the SysTick timer service of `hal.cortex_m`, `validation::NullFlash` and `validation::NullBlockDevice` with the geometries of `nor-qspi-16m` and `sd-card-256m`, which complete every request through the event dispatcher, and a buffer of 64 bytes for the reads and writes.
The empty image holds the platform alone, and F-1 is the difference between the report of the image of a subject and that of the empty image, so that it is what the subject adds to an EmIL application, its glue included; `--baseline-elf` adds `memory_diff.md` to each report, the symbols that differ.
The images need no board: they are linked with `Footprint.ld`, a generic linker script with one `MEMORY` block for the flash and one for the RAM, `Startup.cpp`, a minimal vector table and reset handler, and the system call stubs of `hal.cortex_m.runtime`, in `MinSizeRel`.

`validation/footprint/measure.sh <directory>` runs `cortex-memory-budget`, pinned by hash in `requirements.txt`, on every image of the three presets, with `memory_budget.json`, into `<directory>/reports`, and `python3 -m emfs_validation.footprint` turns the reports into the F-1 fragment, `<directory>/fragments/footprint.json`.
Each subject has two results per core, the flash and the RAM it adds, each with the RAM in `ramBytes`, and each core is a device with the compiler, as the `.comment` of the images names it, and the configuration as its parameters:

```json
{
  "devices": {
    "cortex-m4": { "toolchain": "arm-none-eabi-gcc (Arm GNU Toolchain 15.2.Rel1) 15.2.1", "configuration": "MinSizeRel" }
  },
  "results": [
    { "metric": "F-1", "workload": "flash", "subject": "embedded-filesystem/fat", "device": "cortex-m4", "unit": "B", "value": 70968, "ramBytes": 7748, "better": "lower" },
    { "metric": "F-1", "workload": "ram", "subject": "embedded-filesystem/fat", "device": "cortex-m4", "unit": "B", "value": 7748, "ramBytes": 7748, "better": "lower" }
  ]
}
```

The `Footprint` job of the metrics workflow builds the images, measures them and uploads the directory as the artifact `footprint`; the `Metrics` job downloads it and passes `-DEMFS_METRICS_EXTRA_FRAGMENTS=<directory>/fragments`, so F-1 is reported next to the baselines and held to the 5 % rule like any reported metric.
When the `Footprint` job fails, the `Metrics` job still runs, and the report lacks F-1.

## Running on a board

`validation/README.md` is the bench guide; in short:

```bash
cmake --preset stm32h757-cm7
cmake --build --preset stm32h757-cm7-RelWithDebInfo
cmake --preset stm32h757-cm4
cmake --build --preset stm32h757-cm4-RelWithDebInfo

cd validation/host
pip install ".[dev]"
pytest tests/hil --board boards/stm32h757i_eval.yaml --port /dev/ttyACM0 --no-ad3 --results ~/emfs-run -k qspi
```

The directory of `--results` is what a run sends back, and it is committed as `validation/results/<board>/<run>/`.
