# Protocol

## Introduction

The validation firmware is a terminal of EmIL, `services::HilTerminal`, with the framing of its `docs/Hil.md`: one command per line, terminated by `\r`; exactly one final line per command, `OK` with optional `key=value` pairs or `ERR <reason>`; and `EVT <kind>` lines for results and events, which may come at any time.
This document lists the commands and the result lines of the firmware; it grows with every step of [docs/Roadmap.md](../docs/Roadmap.md) that adds commands.

## After a reset

```text
EVT boot board=<name> family=stm32h7 sysclk=<hz> reset=<cause>
```

| Key      | Value                                                                                                 |
|----------|-------------------------------------------------------------------------------------------------------|
| `board`  | `stm32h757i-eval` or `stm32h745i-disco`                                                               |
| `family` | `stm32h7`                                                                                             |
| `sysclk` | The clock of the Cortex-M7 in Hz, 400000000                                                           |
| `reset`  | `iwdg`, `wwdg`, `sw`, `lpwr`, `por`, `bor`, `pin` or `unknown`, from the reset flags of the Cortex-M7 |

## System commands

These are the commands of `services::HilSystemCommands` of EmIL.

| Command      | Answer                                                                                                 |
|--------------|--------------------------------------------------------------------------------------------------------|
| `ping`       | `OK`                                                                                                   |
| `info`       | `OK board=<name> family=stm32h7 sysclk=<hz> reset=<cause> uid=<hex>`, with the 96-bit unique device id |
| `reset`      | No final line; the board resets after 20 ms and prints `EVT boot ... reset=sw`                         |
| `delay <ms>` | `OK` after the given time; `ERR range` above 600000 ms, and `ERR busy` while a delay is pending        |
| `board.pins` | `OK`, without aliases: the validation offers no pins                                                   |

An unknown command, or a command with the wrong arguments, answers `ERR usage`.

## Firmware

| Command         | Answer                                      |
|-----------------|---------------------------------------------|
| `firmware.info` | `OK commit=<commit> cm4=<stopped\|running>` |

`commit` is what `git describe --always --dirty --abbrev=40` gave when the preset was configured, so configuring again, as the bench guide does before every build, keeps it up to date.
`cm4` is `stopped` when the Cortex-M4 entered stop mode before the Cortex-M7 set up the clocks, as the parked image does, and `running` when another image on the Cortex-M4 kept it running.

## Result lines

Results are records of `EVT` lines, written by `validation::ResultLines`:

```text
EVT <kind> id=<number> <key>=<value> ...
```

* `id` numbers the records from 1 after a reset.
* No line is longer than 254 characters: a record that does not fit continues on the next line, with the same kind and `id`, and the lines of a record follow one another; a pair longer than a line of its own aborts the firmware.
* Keys hold no spaces and no `=`, and values no spaces; the firmware aborts on one. A list is written with commas, `histogram=0,12,7`; an empty list is `histogram=`; a list that does not fit continues under the same key on the next line, `histogram=3,4`, so that the host joins it as `histogram=0,12,7,3,4`.
* Numbers are decimal and unsigned, up to 64 bits; durations are in nanoseconds, in keys ending in `Ns`, cycles in keys ending in `Cycles`, and sizes in bytes.

`emfs_validation.convert` joins the lines of every record and ignores kinds and keys it does not know, so that a later firmware can add them; only a `device` record keeps every key, as a parameter of the device, except `run`, `model` and `model.<parameter>`, which the conversion adds.
The kinds are described with the commands that print them.

## Devices

| Command             | Answer                                                                                                                    |
|---------------------|---------------------------------------------------------------------------------------------------------------------------|
| `dev.list`          | `OK devices=<device>,...`, the devices that are present, in the order `qspi`, `fmc`, `sd`, `emmc`, `ramflash`, `ramblock` |
| `dev.info <device>` | `EVT device` for the device, then `OK`; a device that is not present answers `ERR unsupported`                            |

`qspi` is the first 16 MiB of the QSPI flash on bank 1, present once the firmware has read its SFDP geometry after the reset and aligned its read dummy cycles, which takes milliseconds; `fmc` is the parallel NOR of the STM32H757I-EVAL, present when it answers its identification.
`sd` is the first 256 MiB of the microSD card of the STM32H757I-EVAL, and `emmc` the first 256 MiB of the eMMC of the STM32H745I-DISCO, with the cache of the eMMC on; each is present when its driver identified the card or the eMMC at the reset, so a card inserted later needs a reset.

```text
EVT device id=<number> device=qspi kind=nor sectors=4096 sectorSize=4096 pageSize=256 chip=<JEDEC identifier> clockHz=<clock of the QSPI bus>
EVT device id=<number> device=fmc kind=nor sectors=128 sectorSize=131072 pageSize=64 chip=<manufacturer><device> clockHz=<kernel clock of the FMC>
EVT device id=<number> device=sd kind=block blocks=524288 blockSize=512 unitBlocks=<blocks> chip=<identification> clockHz=<clock of the SDMMC bus>
EVT device id=<number> device=emmc kind=block blocks=524288 blockSize=512 unitBlocks=<blocks> chip=<identification> clockHz=<clock of the SDMMC bus> cache=on
```

| Key                                              | Value                                                                                                                                                                                                                     |
|--------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `kind`                                           | `nor` or `block`                                                                                                                                                                                                          |
| `sectors`, `sectorSize` or `blocks`, `blockSize` | The geometry of the device                                                                                                                                                                                                |
| `pageSize`                                       | The page of NOR flash that the counting of programs takes: the page of the QSPI flash, the write buffer of the parallel NOR                                                                                               |
| `unitBlocks`                                     | The allocation unit that the counting of a block device takes, in blocks, as `docs/Validation.md` describes                                                                                                               |
| `chip`                                           | The identifier of the chip in hexadecimal: the JEDEC identifier, the manufacturer and device codes of the parallel NOR, or the manufacturer, the OEM, the product name and the revision from the CID of a card or an eMMC |
| `clockHz`                                        | The clock of the bus of the device                                                                                                                                                                                        |
| `memory`                                         | `sdram`, for the RAM devices of F-2                                                                                                                                                                                       |
| `cache`                                          | `on` or `off`, the cache of the eMMC                                                                                                                                                                                      |

## Benchmarks of NOR flash

| Command                                         | Answer                 |
|-------------------------------------------------|------------------------|
| `bench.program <device> size=<bytes> n=<count>` | `EVT bench`, then `OK` |
| `bench.erase <device> n=<count>`                | `EVT bench`, then `OK` |
| `bench.read <device> size=<bytes> n=<count>`    | `EVT bench`, then `OK` |
| `dev.check <device>`                            | `OK` or `ERR failed`   |

The benchmarks run on `qspi` and `fmc`, `n` times, from 1 to 1024, and measure every request on its own, from the request to its completion:

* `bench.program` programs `size` bytes, from 1 to 4096 and at most a sector, at the start of `n` page-aligned slots of whole pages, in sectors it erases before; it erases them again at the end;
* `bench.erase` erases, fills with data and then erases again each of the first `n` sectors, and measures the last erase;
* `bench.read` reads `size` bytes, from 1 to 65536, at consecutive addresses from the start of the device, and from the start again where the next read would not fit.

`dev.check` checks the address and data lines of a device: it erases sector 0 and every sector at a power of two, programs a page that names the sector into each, reads them all back and erases them; a missing or stuck address line maps two of these sectors onto one, and a faulty data line changes what reads back, so either answers `ERR failed`.

What they program is erased when they end, and they erase what the device held before.
A device that is not present answers `ERR unsupported`, a count or a size out of range, or programs that do not fit on the device, `ERR range`, and a benchmark while one runs, or while a workload or a format uses the device, `ERR busy`.
A request that does not complete within its guard answers `ERR timeout`, and the device stays busy until the request completes: a read 100 ms, a program 20 ms per page, and an erase 2 s per sector on `qspi` and 8 s on `fmc`.
`hal::NorFlashStm` has limits of its own, 1 s for a page and 10 s for a sector, past which it aborts the firmware, so a request of `fmc` that never completes ends in a reset of the board.
A benchmark that does not end within 30 minutes answers `ERR timeout` too.

```text
EVT bench id=<number> device=<device> op=<program|erase|read> size=<bytes> n=<count> minNs=<ns> medianNs=<ns> p99Ns=<ns> maxNs=<ns> meanNs=<ns>
```

| Key                                             | Value                                                                                  |
|-------------------------------------------------|----------------------------------------------------------------------------------------|
| `op`                                            | The benchmark                                                                          |
| `size`                                          | The bytes of a program or a read, and the size of a sector for an erase                |
| `minNs`, `medianNs`, `p99Ns`, `maxNs`, `meanNs` | Of the measured requests, with the nearest rank for the median and the 99th percentile |

## Benchmarks of block devices

| Command                                                        | Answer                 |
|----------------------------------------------------------------|------------------------|
| `bench.blocks <device> <read\|write> blocks=<count> n=<count>` | `EVT bench`, then `OK` |
| `bench.units <device> units=<count> n=<count>`                 | `EVT bench`, then `OK` |
| `bench.flush <device> n=<count>`                               | `EVT bench`, then `OK` |
| `bench.discard <device> blocks=<count> n=<count>`              | `EVT bench`, then `OK` |

The benchmarks run on `sd` and `emmc`, and measure every request on its own, from the request to its completion:

* `bench.blocks` reads or writes `blocks` consecutive blocks, from 1 to 128, `n` times, from 1 to 1024, at consecutive places from the start of the device, and from the start again where the next would not fit;
* `bench.units` writes one block into each of the first `units` allocation units in turn, from 1 to 8, for `n` rounds, at the next block of each unit in each round, as flashbench does, at most 1024 writes in all;
* `bench.flush` writes a block, which it does not measure, and then flushes the device, `n` times;
* `bench.discard` writes the first block of `blocks` consecutive blocks, which it does not measure, and then discards them, `n` times, at consecutive places.

What they write stays on the device; they do not restore what it held.
A device that is not present answers `ERR unsupported`, an operation other than `read` or `write` `ERR usage`, a count out of range `ERR range`, a request that does not succeed `ERR failed`, and a benchmark while another runs `ERR busy`.
A request that does not complete within 8 s answers `ERR timeout`, and the device stays busy until the request completes; the driver of hal-st itself ends a transfer after 1 s and a wait for the device after 5 s, with `ERR failed`.
A benchmark that does not end within 30 minutes answers `ERR timeout` too.

```text
EVT bench id=<number> device=<device> op=<read|write|units|flush|discard> size=<bytes> [units=<count>] n=<count> minNs=<ns> medianNs=<ns> p99Ns=<ns> maxNs=<ns> meanNs=<ns>
```

| Key     | Value                                                                 |
|---------|-----------------------------------------------------------------------|
| `size`  | The bytes of a read, a write or a discard, and a block for the others |
| `units` | The allocation units that `bench.units` writes into                   |
| `n`     | The measured requests: `units` times the rounds for `bench.units`     |

## Workloads

| Command                                                         | Answer                                                                              |
|-----------------------------------------------------------------|-------------------------------------------------------------------------------------|
| `fs.format <device>`                                            | `OK`, or `ERR failed`                                                               |
| `wl.run <device> <workload> [fill=<percent>] [scale=<divisor>]` | `EVT workload`, `EVT phase` per phase, `EVT window`, `EVT workload.done`, then `OK` |

The workloads run on the NOR volume of `qspi` and of `fmc`, and on the FAT volume of `sd` and of `emmc`, as `docs/Validation.md` describes:

* `fs.format` formats the volume of the device with `fs::NorFileSystem::Format`, which erases the whole device, or with `fs::FatFormatter`, with the data area at an allocation unit, which discards the blocks of the region after its file system;
* `wl.run` runs `L1`, `L2`, `L3`, `L4`, `L6` or `L7` of `docs/Metrics.md`, formatting the volume first, with the sizes of the files divided by `scale`, from 1 to 1024, 16 by default; `L4` keeps its 200 files;
* `fill`, from 0 to 99, fills the volume to a level before `L2`, `L3` and `L4`, as the host does; `L7` fills to 10, 50 and 90 % itself, and the other workloads take no fill.

The volume is unmounted when a workload ends, so that no background work of the volume runs into a later benchmark, and `EVT workload.done` marks the end; the host counts only a run that has it.
A device that is not present or a workload that is unknown answers `ERR unsupported`, a fill or a scale out of range, or a fill for a workload without one, `ERR range`, and a command while a workload, a format or a benchmark uses the device `ERR busy`.
A workload in which a call fails prints `EVT workload.failed` before `EVT workload.done` and answers `ERR failed`.
A request that does not complete within its guard, or a call that does not complete within 10 minutes, ends the workload with `EVT workload.failed` and `ERR timeout`, as a request past its guard ends a format with `ERR timeout`; what timed out may still complete and go on, so `wl.run` and `fs.format` answer `ERR busy`, and the benchmarks on that device too, until the board is reset.
A workload that does not end within 4 hours, and a format within 30 minutes, answers `ERR timeout` too.

```text
EVT workload id=<number> device=<device> subject=<embedded-filesystem/nor|embedded-filesystem/fat> workload=<workload> fill=<percent> scale=<divisor> ramBytes=<bytes>
EVT phase id=<number> device=<device> subject=<subject> workload=<workload> phase=<phase> fill=<percent> ramBytes=<bytes> elapsedNs=<ns> <counts> <operations>
EVT window id=<number> device=<device> subject=<subject> workload=L2 fill=<percent> log=<first|second> appends=<count> <counts>
EVT workload.failed id=<number> device=<device> subject=<subject> workload=<workload> fill=<percent> phase=<phase> op=<operation> result=<result>
EVT workload.done id=<number> device=<device> subject=<subject> workload=<workload> fill=<percent>
```

`<counts>` are the counts of the counting device in the phase or the window:

* of a NOR device, `readRequests`, `bytesRead`, `readNs`, `programRequests`, `bytesProgrammed`, `pagesProgrammed`, `programNs`, `eraseRequests`, `sectorsErased` and `eraseNs`, and those of the volume, from `Counters()` of the NOR volume across mounts, `allocatedSectors`, `erasedBlocks`, `checkpoints` and `movedBlocks`;
* of a block device, `readRequests`, `blocksRead`, `readNs`, `writeRequests`, `blocksWritten`, `writeNs`, `unitsRecent1` to `unitsRecent4` and `unitsNotRecent`, `alignedBlocks`, `distinctUnits`, `flushRequests`, `flushNs`, `discardRequests`, `blocksDiscarded`, `discardNs` and `failedRequests`; the FAT volume has no counts of its own.
`<operations>` are, for every operation called in the phase, from `format`, `mount`, `unmount`, `open`, `read`, `write`, `seek`, `sync`, `close`, `expectSize`, `remove`, `makeDirectory`, `list`, `usage` and `idle`, the keys `<operation>.calls`, `<operation>.userBytes`, `<operation>.latencyNs`, `<operation>.maxNs` and `<operation>.histogram`.

| Key                                                | Value                                                                                                                                                                                                                                                   |
|----------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `phase`                                            | The phase as the host names it, with commas for spaces, such as `L1,65536,64` or `L2,second,log`; the volume is filled in the phase `fill`                                                                                                              |
| `elapsedNs`                                        | From the start of the phase to its end                                                                                                                                                                                                                  |
| `<operation>.calls`                                | The calls of the operation, with the bytes they wrote or read in `userBytes`, the sum of their latencies in `latencyNs` and the longest in `maxNs`                                                                                                      |
| `<operation>.histogram`                            | The latencies as pairs of a bucket and a count: bucket `b` from 4 holds durations from `(4 + b % 4) << (b / 4 - 1)` ns, four per octave, and buckets 0 to 3 hold 0 to 3 ns                                                                              |
| `log`, `appends`                                   | The window of W-2 and the appends in it: on NOR, at the end of the first log or from the first checkpoint once the second holds 4 KiB, until the fourth checkpoint; on FAT, the last 64 appends of the first log, and the 64 from 4 KiB into the second |
| `unitsRecent1` to `unitsRecent4`, `unitsNotRecent` | The allocation units that writes spanned, by their place among the four most recently written units, the most recent first, or as none of them                                                                                                          |
| `alignedBlocks`, `distinctUnits`                   | The written blocks that start an allocation unit or follow the block written before them, and the allocation units written in the phase                                                                                                                 |
| `op`, `result`                                     | The operation of the first call that failed and what it returned, or `timeout`                                                                                                                                                                          |

## Layout of a card

| Command                  | Answer                                                                         |
|--------------------------|--------------------------------------------------------------------------------|
| `layout.read <device>`   | `EVT layout` for the card and for the formatter, then `OK differences=<count>` |
| `layout.format <device>` | `OK`, or `ERR failed`                                                          |

The commands take `sd`, the whole microSD card of the STM32H757I-EVAL, not its region of 256 MiB:

* `layout.read` reads the first sector of the card and the boot sector of its volume, the first partition of its MBR or the card itself when it starts with a boot sector, and parses them with `validation::FatLayout`, without the code of `fs/fat`;
  it prints that layout, then the one `fs::ComputeFormatLayout` gives for the blocks of the card with the defaults of `fs::FatFormatter`, with the fields in which they differ;
* `layout.format` formats the whole card with `fs::FatFormatter` and its defaults, as a quick format that does not discard the data area, so that the card can be checked on a PC.

A card whose first sector holds neither an MBR with a partition nor a boot sector, or whose volume is not FAT or exFAT, answers `ERR failed`, as does a request that fails; a device other than `sd` answers `ERR unsupported`.
While a command runs, the region of the card takes no benchmark, workload or format, which answer `ERR busy`, and a command answers `ERR busy` while the region is in use.
A read that does not end within 10 seconds, and a format within 30 minutes, answers `ERR timeout`, and the card stays busy until its request completes.

```text
EVT layout id=<number> device=sd source=<card|formatter> blocks=<n> unitBlocks=<n> partitionStart=<sector> partitionSectors=<n> type=<fat12|fat16|fat32|exfat> sectorsPerCluster=<n> reservedSectors=<n> fats=<n> fatSectors=<n> rootEntries=<n> dataStart=<sector> [differs=<field>,...]
```

| Key                                  | Value                                                                                                                  |
|--------------------------------------|------------------------------------------------------------------------------------------------------------------------|
| `blocks`, `unitBlocks`               | The blocks of the whole card, and the allocation unit it reports in its SD status, 0 without one                       |
| `partitionStart`, `partitionSectors` | The first sector and the sectors of the volume: of the first partition, or from sector 0 when there is no MBR          |
| `type`                               | `exfat` for an SDXC card, whose layout gives only `partitionStart` and `partitionSectors`, and compares only the first |
| `dataStart`                          | The first sector of the data area, counted from the start of the card                                                  |
| `differs`                            | On the record of the formatter, the fields in which the layout of the card differs, empty when none does               |

## Processor cycles

| Command                            | Answer                                                                                                                            |
|------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------|
| `cycles.run <subject> [n=<count>]` | `EVT device` for the RAM device of the subject, one `EVT cycles` per operation, then `OK`; `n` runs from 1 to 200, 100 by default |

The subjects are `embedded-filesystem/fat` and `fatfs/tuned` on `ramblock`, and `embedded-filesystem/nor` and `littlefs/ram-matched` on `ramflash`, the RAM devices in the first 6 MiB of the SDRAM.
An unknown subject answers `ERR unsupported`, a count out of range `ERR range`, and a command while a run is going on `ERR busy`.
A run that does not end within 10 minutes answers `ERR timeout`, and one in which a call of the subject fails prints `EVT cycles.failed` and answers `ERR failed`.

```text
EVT device id=<number> device=ramflash kind=nor sectors=1024 sectorSize=4096 memory=sdram
EVT device id=<number> device=ramblock kind=block blocks=4096 blockSize=512 memory=sdram
EVT cycles id=<number> subject=<subject> device=<device> op=<operation> n=<count> ramBytes=<bytes> minCycles=<cycles> medianCycles=<cycles> maxCycles=<cycles> settledCycles=<cycles>
EVT cycles.failed id=<number> subject=<subject> op=<operation> run=<run> status=<code>
```

| Key                                      | Value                                                                                                                                                |
|------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------|
| `op`                                     | `format`, `mount`, `open`, `list`, `read`, `overwrite`, `create`, `append`, `write`, `rename`, `remove` or `unmount`, in this order                  |
| `ramBytes`                               | The RAM of the subject, as the host metrics count it                                                                                                 |
| `minCycles`, `medianCycles`, `maxCycles` | The cycles from the call to its completion, over the `n` runs; the median of an even count is the lower of the two middle values                     |
| `settledCycles`                          | The median of the busy cycles from the call until the volume is idle again, without the cycles of waiting that the calibration of the run measured   |
| `run`, `status`                          | The run, from 0, and the error code of the file system: an `fs::Result`, an `FRESULT` of FatFs or an error of littlefs; 100 is a short read or write |
