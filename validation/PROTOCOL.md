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

## Processor cycles

| Command                            | Answer                                                                                                                            |
|------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------|
| `cycles.run <subject> [n=<count>]` | `EVT device` for the RAM device of the subject, one `EVT cycles` per operation, then `OK`; `n` runs from 1 to 200, 100 by default |

The subjects are `embedded-filesystem/fat` and `fatfs/tuned` on `ramblock`, and `embedded-filesystem/nor` and `littlefs/ram-matched` on `ramflash`, the RAM devices in the first 4 MiB of the SDRAM.
An unknown subject answers `ERR unsupported`, a count out of range `ERR range`, and a command while a run is going on `ERR busy`.
A run that does not end within 10 minutes answers `ERR timeout`, and one in which a call of the subject fails prints `EVT cycles.failed` and answers `ERR failed`.

```text
EVT device id=<number> device=ramflash kind=nor sectors=512 sectorSize=4096 memory=sdram
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

