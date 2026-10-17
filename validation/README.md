# Validation on boards

## Introduction

This directory runs embedded-filesystem on ST boards with the drivers of hal-st, as [docs/Validation.md](../docs/Validation.md) describes.
`firmware/` is the firmware of the boards, a terminal that answers the commands of [PROTOCOL.md](PROTOCOL.md), and `host/` the Python package `emfs_validation`, whose pytest-bdd scenarios run against it and record what it answers.
This guide is the bench side: building, flashing, running the scenarios, and sending the results back.

## Boards

| Board            | Firmware presets                 | Terminal                                  | Board file                          |
|------------------|----------------------------------|-------------------------------------------|-------------------------------------|
| STM32H757I-EVAL  | `stm32h757-cm7`, `stm32h757-cm4` | USART1 on PB14 and PB15, 115200 baud, 8N1 | `host/boards/stm32h757i_eval.yaml`  |
| STM32H745I-DISCO | `stm32h745-cm7`, `stm32h745-cm4` | USART3 on PB10 and PB11, 115200 baud, 8N1 | `host/boards/stm32h745i_disco.yaml` |

The Cortex-M7 runs the validation firmware, `validation.firmware`, and the Cortex-M4 the parked image, `validation.park`, which keeps it in stop mode; flash both.
The terminal is the one the demos of hal-st use, reached through the virtual COM port of the ST-LINK of the board.

## Building

The build needs the Arm GNU toolchain 15.2.Rel1, CMake, Ninja and `xsltproc`, for the pin tables that hal-st generates:

```bash
cmake --preset stm32h757-cm7
cmake --build --preset stm32h757-cm7-RelWithDebInfo
cmake --preset stm32h757-cm4
cmake --build --preset stm32h757-cm4-RelWithDebInfo
```

The images are in `build/<preset>/validation/firmware/boards/RelWithDebInfo/`, as `.elf`, `.hex` and `.bin`: `validation.firmware` for the Cortex-M7, at `0x08000000`, and `validation.park` for the Cortex-M4, at `0x08100000`.
For the STM32H745I-DISCO, use the `stm32h745-*` presets.

## Flashing

With STM32CubeProgrammer, both images in one session, the parked image first:

```bash
STM32_Programmer_CLI -c port=SWD -w build/stm32h757-cm4/validation/firmware/boards/RelWithDebInfo/validation.park.hex -v
STM32_Programmer_CLI -c port=SWD -w build/stm32h757-cm7/validation/firmware/boards/RelWithDebInfo/validation.firmware.hex -v -rst
```

After the reset, the terminal prints `EVT boot board=stm32h757i-eval family=stm32h7 sysclk=400000000 reset=<cause>`.

## Running the scenarios

The host package needs Python 3.10 or later:

```bash
cd validation/host
python3 -m venv .venv
.venv/bin/pip install ".[dev]"
.venv/bin/python -m pytest tests/hil --board stm32h757i_eval --port /dev/ttyACM0 --no-ad3 --results ~/emfs-run
```

On Windows, the port is a `COM` port, such as `--port COM5`, and the programs of the virtual environment are in `.venv\Scripts` instead of `.venv/bin`.
Without `--port`, the scenarios skip; with `--fake` instead, they run against `emfs_validation.fake_firmware`, as CI does.

| Option               | Default                | Meaning                                                                         |
|----------------------|------------------------|---------------------------------------------------------------------------------|
| `--board`            | `stm32h757i_eval`      | The board file, by name or path; or `EMFS_BOARD`                                |
| `--port`             | none                   | The serial port of the terminal; or `EMFS_PORT`                                 |
| `--baud`             | that of the board file | The baud rate of the terminal                                                   |
| `--command-timeout`  | that of the board file | The seconds to wait for the answer of a command                                 |
| `--results`          | none                   | The directory for the transcript, `run.json` and `junit.xml`; or `EMFS_RESULTS` |
| `--fake`, `--no-ad3` |                        | From ad3-waveforms-bench: the fake firmware, and no Analog Discovery 3          |

The session stops with exit code 3 when the board does not answer `ping`, and refuses a `--results` directory that already holds a run.

| Scenarios        | What they do on the board                                                                                                                                                            | Time                      |
|------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------|
| `boot.feature`   | Resets the board and checks the boot event, `ping`, `info` and `firmware.info`                                                                                                       | Seconds                   |
| `nor.feature`    | Lists and describes the NOR devices, and runs their benchmarks: erases the first 16 MiB of the QSPI flash, and the parallel NOR of the STM32H757I-EVAL, which keep nothing they held | About a minute per device |
| `cycles.feature` | Runs F-2 for the four subjects on the RAM devices in the SDRAM, which it overwrites; nothing else of the board is written                                                            | Up to a few minutes       |

`-k cycles` runs F-2 alone, `-k nor` the NOR devices alone, and `-k boot` the boot scenarios alone.
On a board fresh from ST, the first 16 MiB of the QSPI flash hold the images of its demonstration, which `nor.feature` erases.
F-2 counts cycles with the cycle counter of the Cortex-M7, which the firmware unlocks, so the run needs no debugger; a debugger that keeps the clocks of the core running in sleep changes nothing, as [docs/Validation.md](../docs/Validation.md#processor-cycles) explains.

## Sending the results back

The directory of `--results` holds what a run sends back:

| File             | Contents                                                                                                         |
|------------------|------------------------------------------------------------------------------------------------------------------|
| `transcript.log` | Every line sent to the terminal, as `> <line>`, and every line it printed                                        |
| `junit.xml`      | The JUnit report of pytest                                                                                       |
| `run.json`       | The board, the port, the commits of the firmware and of the host checkout, the options, the times and the result |

It is committed as `validation/results/<board>/<run>/`, where `<board>` is the name of the board file and `<run>` the date, as `2026_10_20`; [results/README.md](results/README.md) describes the conversion.
Before sending a run, its result lines can be checked by converting it, in `validation/host`:

```bash
.venv/bin/python -m emfs_validation.convert --run ~/emfs-run --output-directory ~/emfs-fragments
```

## Unit tests of the host package

```bash
cd validation/host
.venv/bin/ruff check . && .venv/bin/ruff format --check .
.venv/bin/python -m pytest tests/unit -q
```
