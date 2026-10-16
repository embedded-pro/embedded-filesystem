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
