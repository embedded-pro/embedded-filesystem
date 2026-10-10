# Metrics

## Introduction

Every performance and wear claim of embedded-filesystem is backed by a metric defined in this document.
Each metric has an identifier, a definition, the workloads it is measured on, the subjects it compares and a target.
The metrics run on the host, on deterministic models of the storage devices, so that the same commit always gives the same numbers, and they run in CI on every pull request.
FatFs and littlefs are measured on the same device models and workloads as baselines.
A few metrics, such as code size and processor cycles, can only be measured on a real target and are run separately.

## Principles

The host metrics count work done by the storage device, not time measured on the host computer, because host timing depends on the machine and is not representative of a microcontroller.
Counts are converted into simulated device time with timing models based on datasheets.
Baselines run under the same conditions: the same device model, the same workload, and caches configured to use the same amount of RAM as embedded-filesystem, with the RAM used by every subject reported next to its results.
FatFs and littlefs are each measured twice: with their default configuration, and with the options that help the workload enabled, such as fast seek and `f_expand` for FatFs, so that the comparison is not made against an untuned baseline.
A metric with a numeric target fails the build when the target is missed; a metric marked as reported is tracked, and fails the build when it becomes worse than on `main` by more than 5 %.

## Measurement setup

### Metered devices

`metrics::MeteredFlash` wraps a `hal::Flash`, and `metrics::MeteredBlockDevice` wraps a `hal::BlockDevice`, in the same way as `services::TracingFlash` in EmIL wraps a flash.
They forward every request and record:

* the number of read, program and erase requests, and the bytes read and programmed;
* the number of erases of every block;
* for block devices, the number of blocks per write request, the alignment of every write to the allocation unit, and the allocation units touched;
* the simulated device time of every request, from the device model.

They wrap the EmIL test doubles `hal::FlashStub` and `hal::BlockDeviceStub`, which hold the stored data.
The device models and the metered devices are host-only code in `metrics/`, and are never linked into the libraries.

### Device models

| Model            | Capacity | Geometry                                                | Timing                                                                                                                                                            |
|------------------|----------|---------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `nor-qspi-16m`   | 16 MiB   | 4 KiB sectors, 256 B pages, program unit 1 B            | 128 Mbit QSPI NOR class: page program 0.4 ms typical, 3 ms maximum; 4 KiB erase 45 ms typical, 400 ms maximum; read at 50 MB/s                                    |
| `nor-qspi-1m`    | 1 MiB    | As `nor-qspi-16m`                                       | As `nor-qspi-16m`; used for wear-leveling runs to the end of life                                                                                                 |
| `internal-flash` | 512 KiB  | 2 KiB sectors, program unit 16 B, no second programming | 8 µs per 16 B program unit, 20 ms per sector erase                                                                                                                |
| `sd-card-256m`   | 256 MiB  | 512 B blocks, 4 MiB allocation units                    | 100 µs per command, 25 µs per block read, 50 µs per block written, and a 10 ms penalty when a write opens an allocation unit beyond the two most recently written |

The NOR timings follow the datasheets of common 128 Mbit QSPI NOR devices.
The SD card model is a simplification of the behaviour measured on low-cost cards with flashbench, where writes spread over many allocation units are much slower than writes concentrated in a few.
All parameters are assumptions until they are calibrated against real devices in the on-target phase described in [Roadmap.md](Roadmap.md), and every report states the parameters it used.
Typical timings are used for throughput; maximum timings are used for latency bounds.

### Simulated time and background work

Total device time is the sum of the simulated time of every request.
Background work is included: a measurement runs until the volume is idle again, so that erases moved to the background are counted, not hidden.
For latency metrics, workloads contain idle periods, during which background work runs, and the latency of a call is the simulated time from the call to its completion.

### Baselines

FatFs and littlefs are fetched with FetchContent at pinned versions, only when `EMFS_BUILD_METRICS` is enabled, and only for the host.
Their storage callbacks are adapted to the metered devices through a synchronous shim that runs the event dispatcher until the device request completes.
The shim exists only in the metrics harness; it is the blocking adapter that an EmIL application would otherwise need.

## Workloads

All workloads use a fixed random seed.

| Workload | Description                                                                                                                                     |
|----------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| L1       | Write one file of 1 MiB and one of 4 MiB sequentially, in chunks of 64 B, 512 B and 4 KiB                                                       |
| L2       | Data logger: append 64 B and call `Sync`, until the file reaches 4 MiB, with a 100 ms idle period after each append                             |
| L3       | Overwrite 512 B at random offsets in a file of 64 KiB, 1 MiB and 4 MiB                                                                          |
| L4       | Create, write, close and remove many small files of 100 B to 8 KiB                                                                              |
| L5       | Fill 70 % of the volume with files that are never changed, then rewrite a 4 KiB configuration file repeatedly                                   |
| L6       | Read sequentially, read at random offsets, seek to the end, list a directory, and open files by path in directories of 10, 100 and 1000 entries |
| L7       | Mount the volume when it is 10 %, 50 % and 90 % full                                                                                            |

Workloads L2, L3 and L4 are run on volumes that are 10 %, 50 % and 90 % full, to measure the effect of the fill level.

## Write metrics

| Id  | Metric                      | Definition                                                                                              | Workloads | Target                                                                                                               |
|-----|-----------------------------|---------------------------------------------------------------------------------------------------------|-----------|----------------------------------------------------------------------------------------------------------------------|
| W-1 | Sequential write throughput | User bytes written divided by total device time                                                         | L1        | At least the best baseline                                                                                           |
| W-2 | Append scaling              | Device time of one append and `Sync` when the file holds 4 MiB, divided by the same when it holds 4 KiB | L2        | At most 1.1, for NOR and FAT                                                                                         |
| W-3 | Fill scaling                | Device time of one append and `Sync` on a 90 % full volume, divided by the same on a 10 % full volume   | L2        | At most 1.2                                                                                                          |
| W-4 | Write amplification         | Bytes programmed on the device divided by user bytes written                                            | L1, L2    | At most 1.1 for L1; reported for L2                                                                                  |
| W-5 | Overwrite scaling           | Device time of one overwrite in a 4 MiB file, divided by the same in a 64 KiB file                      | L3        | At most 1.2 on NOR                                                                                                   |
| W-6 | Call latency                | 99th percentile and maximum of the simulated latency of one call                                        | L2, L3    | Maximum at most one sector erase plus one block program, at maximum timings, independent of file size and fill level |

## Read metrics

| Id  | Metric                     | Definition                                                                                  | Workloads | Target                                                                                                                   |
|-----|----------------------------|---------------------------------------------------------------------------------------------|-----------|--------------------------------------------------------------------------------------------------------------------------|
| R-1 | Sequential read throughput | User bytes read divided by total device time                                                | L6        | At least the best baseline                                                                                               |
| R-2 | Random read cost           | Device read requests for one random 512 B read, as a function of the file size              | L6        | NOR: at most 4, with a ratio of at most 1.2 between 64 KiB and 4 MiB files. FAT: 1 once the extent cache covers the file |
| R-3 | Lookup cost                | Device time to open a file by path, as a function of the number of entries in its directory | L6        | Reported                                                                                                                 |
| R-4 | Mount time                 | Device time to mount, as a function of the device size and the fill level                   | L7        | Reported                                                                                                                 |
| R-5 | Read amplification         | Bytes read from the device divided by user bytes read                                       | L6        | At most 1.1 for sequential reads                                                                                         |

## Wear metrics

These apply to the NOR filesystem.
For SD cards and eMMC, wear leveling is done by the card, and the device metrics below measure how friendly the access pattern is to the card instead.

| Id   | Metric              | Definition                                                                                                                     | Workloads | Target                          |
|------|---------------------|--------------------------------------------------------------------------------------------------------------------------------|-----------|---------------------------------|
| WL-1 | Erase spread        | Highest erase count divided by the mean erase count                                                                            | L4, L5    | At most 1.2 on L5               |
| WL-2 | Leveling efficiency | Mean erase count divided by the highest erase count                                                                            | L4, L5    | At least 0.8 on L5              |
| WL-3 | Lifetime            | User bytes written until the first block reaches its endurance, on `nor-qspi-1m` with the endurance scaled down to 1000 erases | L5        | Reported as a ratio to littlefs |
| WL-4 | Leveling overhead   | Erases caused by static wear leveling divided by all erases                                                                    | L5        | At most 5 %                     |

## Device metrics for SD cards and eMMC

| Id  | Metric                   | Definition                                                                                         | Workloads  | Target         |
|-----|--------------------------|----------------------------------------------------------------------------------------------------|------------|----------------|
| M-1 | Blocks per write request | Mean number of blocks per write request                                                            | L1, L2, L4 | At least FatFs |
| M-2 | Aligned writes           | Share of write requests that start on an allocation unit boundary or continue the previous request | L1, L2, L4 | At least FatFs |
| M-3 | Allocation units touched | Distinct allocation units written per MiB of user data                                             | L1, L2, L4 | At most FatFs  |

## Robustness metrics

| Id  | Metric          | Definition                                                                                                           | Target                                                                                 |
|-----|-----------------|----------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------|
| P-1 | Power-cut sweep | Share of cut points after which the volume mounts consistently and every file holds its previous or its new contents | NOR: 100 %. FAT: 100 % with atomic sector writes, and reported with torn sector writes |

The sweep is described in [Testing.md](Testing.md).

## On-target metrics

| Id  | Metric           | Definition                                                                                                                           | Target                                                            |
|-----|------------------|--------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------|
| F-1 | Footprint        | Flash and RAM used by each filesystem in a reference configuration on Cortex-M4 and Cortex-M33, measured with `cortex-memory-budget` | Reported, next to FatFs and littlefs in equivalent configurations |
| F-2 | Processor cycles | Cycles spent per operation on the target, measured with the data watchpoint and trace unit or estimated with `cortex-cycle-budget`   | Reported                                                          |

On target, the device timing models are also calibrated: the same workloads run on real NOR flash and SD cards, and the simulated device time is compared with the measured time.

## Reports

Every run writes `metrics.json`, with one entry per metric, workload, subject and device model:

```json
{
  "metric": "W-2",
  "workload": "L2",
  "subject": "embedded-filesystem/nor",
  "device": "nor-qspi-16m",
  "fill": 50,
  "value": 1.02,
  "target": "<= 1.1",
  "pass": true,
  "ramBytes": 7680
}
```

A Markdown summary with the same content is generated from it, and the metrics workflow posts it as a comment on the pull request, updating the same comment on every push.
The `metrics.json` of `main` is kept as a workflow artifact and used to detect regressions.

## Running

```bash
cmake --preset host -DEMFS_BUILD_METRICS=On
cmake --build --preset host-Debug
ctest --preset metrics
```

The metric tests are GoogleTest executables with the CTest label `metrics`; they assert the targets of this document, so a missed target is a failing test.
