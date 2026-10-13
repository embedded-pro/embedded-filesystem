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
Baselines run under the same conditions: the same device model, the same workload, and caches configured to use the same amount of RAM as embedded-filesystem where a baseline has a cache to size, with the RAM used by every subject reported next to its results.
FatFs has no such cache: it works with one sector buffer per volume and one per file, so it uses less RAM than `embedded-filesystem/fat`, and both numbers are reported.
FatFs and littlefs are each measured twice: with their default configuration, and with the options that help the workload enabled, such as fast seek and `f_expand` for FatFs, so that the comparison is not made against an untuned baseline.
A metric with a numeric target fails the build when the target is missed; a metric marked as reported is tracked, and fails the build when it becomes worse than on `main` by more than 5 %.
Diagnostics are only shown: P-1 with torn sector writes measures a fault that FAT does not protect against, and its share moves with the number of cut points rather than with the safety of the volume.

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

| Model            | Capacity | Geometry                                                | Timing                                                                                                                                                                                                                                                                |
|------------------|----------|---------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `nor-qspi-16m`   | 16 MiB   | 4 KiB sectors, 256 B pages, program unit 1 B            | 128 Mbit QSPI NOR class: page program 0.4 ms typical, 3 ms maximum; 4 KiB erase 45 ms typical, 400 ms maximum; read at 50 MB/s                                                                                                                                        |
| `nor-qspi-1m`    | 1 MiB    | As `nor-qspi-16m`                                       | As `nor-qspi-16m`; used for wear-leveling runs to the end of life                                                                                                                                                                                                     |
| `internal-flash` | 512 KiB  | 2 KiB sectors, program unit 16 B, no second programming | 8 µs per 16 B program unit, 20 ms per sector erase, read at 100 MB/s; maximum timings equal the typical ones                                                                                                                                                          |
| `sd-card-256m`   | 256 MiB  | 512 B blocks, 4 MiB allocation units                    | 100 µs per command, 25 µs per block read, 50 µs per block written, and a 10 ms penalty when a write opens an allocation unit beyond the two most recently written; a flush and a discard cost one command; at maximum timings a write request adds a 250 ms busy time |

The NOR timings follow the datasheets of common 128 Mbit QSPI NOR devices.
The SD card model is a simplification of the behaviour measured on low-cost cards with flashbench, where writes spread over many allocation units are much slower than writes concentrated in a few.
All parameters are assumptions until they are calibrated against real devices in the on-target phase described in [Roadmap.md](Roadmap.md), and every report states the parameters it used.
Typical timings are used for throughput; maximum timings are used for latency bounds.
The models are defined in `metrics/device/DeviceModels.cpp`, and the metered devices `metrics::MeteredFlash` and `metrics::MeteredBlockDevice` delay every completion by the modelled time on a virtual clock, `metrics::VirtualTime`, so that latencies and background work are measured deterministically.

### Simulated time and background work

Total device time is the sum of the simulated time of every request.
Background work is included: a measurement runs until the volume is idle again, so that erases moved to the background are counted, not hidden.
For latency metrics, workloads contain idle periods, during which background work runs, and the latency of a call is the simulated time from the call to its completion.

### Baselines

FatFs and littlefs are fetched with FetchContent at pinned versions, only when `EMFS_BUILD_METRICS` is enabled, and only for the host.
Their storage callbacks are adapted to the metered devices through a synchronous shim that runs the event dispatcher until the device request completes.
The shim exists only in the metrics harness; it is the blocking adapter that an EmIL application would otherwise need.

| Subject                | Version                               | Configuration                                                                                                                                                                                             |
|------------------------|---------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `littlefs/default`     | v2.11.3                               | The example of the littlefs README: read and program size 16 B, cache 16 B, lookahead 16 B, 500 block cycles                                                                                              |
| `littlefs/ram-matched` | v2.11.3                               | Read and program size 16 B, cache 2048 B and lookahead 1536 B, which is 7680 B of buffers with one open file, 500 block cycles, and `lfs_fs_gc` when the volume is idle                                   |
| `fatfs/default`        | R0.16, from the `abbrev/fatfs` mirror | The `ffconf.h` of FatFs, with what the workloads need: formatting, long file names in a static buffer, code page 437 and no real-time clock                                                               |
| `fatfs/tuned`          | R0.16, from the `abbrev/fatfs` mirror | As `fatfs/default`, with fast seek on files opened for reading (a link map of 64 entries), `f_expand` for `ExpectSize`, trim mapped to a discard, and `f_mkfs` aligned to the allocation unit of the card |

littlefs runs on `nor-qspi-16m` and `internal-flash`, and on `nor-qspi-1m` for the lifetime metric WL-3; both FatFs variants run on `sd-card-256m`.
On `internal-flash`, the file sizes of the workloads are divided by 64, so that they fit on 512 KiB.
FatFs symbols are global and its options are compile-time, so every FatFs variant is built from its own copy of the sources next to an `ffconf.h` generated from the upstream one, and runs in its own executable.
The RAM reported for a subject is the size of its state and buffers with one open file, which is what every workload needs.

### Subjects of embedded-filesystem

| Subject                   | Device         | Configuration                                                                                                                                                                                                  |
|---------------------------|----------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `embedded-filesystem/fat` | `sd-card-256m` | `fs::FatFileSystem::WithStorage<2, 8>` with the allocation unit of the model and the default timer service, on a volume formatted by `fs::FatFormatter` with its defaults and the allocation unit of the model |

The subjects of embedded-filesystem measure the defaults a product gets, such as the 2 copies of the FAT that the formatter writes, not a configuration chosen to match a baseline.
P-1 is recorded from the power-cut sweep of each filesystem, on the small device models of the sweep.

### Baseline results

These are the first results of the baselines, on the typical timings of the device models above, except W-6, which uses the maximum timings.
The metrics workflow measures them again on every pull request, and the full report is its `metrics.md`.

On `nor-qspi-16m`, with the littlefs configurations of the [baselines](#baselines):

| Metric | Measurement                                                                | `littlefs/default` | `littlefs/ram-matched` |
|--------|----------------------------------------------------------------------------|--------------------|------------------------|
| W-1    | Sequential write of 4 MiB in 4 KiB chunks (kB/s)                           | 27.7               | 79.4                   |
| W-2    | Append and sync at 4 MiB against 4 KiB, 50 % full                          | 1.01               | 1                      |
| W-3    | Append and sync on a 90 % against a 10 % full volume                       | 1                  | 1                      |
| W-4    | Write amplification of the data logger, 50 % full                          | 33.6               | 35.1                   |
| W-5    | Overwrite in a 4 MiB against a 64 KiB file, 50 % full                      | 64.2               | 60.2                   |
| W-6    | Longest append and sync at maximum timings (ms)                            | 1587               | 848                    |
| R-1    | Sequential read (MB/s)                                                     | 49.2               | 49.1                   |
| R-2    | Device reads for a random 512 B read in a 4 MiB file                       | 39.6               | 8                      |
| R-4    | Mount of a 90 % full volume (ms)                                           | 0.3                | 0.258                  |
| WL-1   | Erase spread with 70 % static data                                         | 13.8               | 13.8                   |
| WL-3   | User data written on `nor-qspi-1m` until a block reaches 1000 erases (MiB) | 315                | 315                    |
|        | RAM (B)                                                                    | 328                | 7944                   |

On `sd-card-256m`, with the FatFs configurations of the [baselines](#baselines):

| Metric | Measurement                                            | `fatfs/default` | `fatfs/tuned` |
|--------|--------------------------------------------------------|-----------------|---------------|
| W-1    | Sequential write of 4 MiB in 4 KiB chunks (MB/s)       | 7.85            | 7.85          |
| W-1    | Sequential write of 4 MiB in 64 B chunks (MB/s)        | 3.41            | 1.84          |
| W-2    | Append and sync at 4 MiB against 4 KiB, 50 % full      | 1               | 1             |
| W-4    | Write amplification of the data logger, 50 % full      | 16.1            | 16.1          |
| W-5    | Overwrite in a 4 MiB against a 64 KiB file, 50 % full  | 7.18            | 8.32          |
| W-6    | Longest append and sync at maximum timings (ms)        | 761             | 761           |
| R-1    | Sequential read (MB/s)                                 | 13.6            | 13.6          |
| R-2    | Device reads for a random 512 B read in a 4 MiB file   | 1.83            | 1             |
| R-3    | Open by path in a directory of 1000 entries (ms)       | 3.99            | 3.99          |
| M-2    | Aligned writes of the data logger, 50 % full (%)       | 0.006           | 0.006         |
| M-3    | Allocation units per MiB of the data logger, 50 % full | 0.75            | 0.5           |
|        | RAM (B)                                                | 1656            | 1920          |

What the baselines show:

* Overwriting inside a file costs littlefs about 60 times more in a 4 MiB file than in a 64 KiB file, because littlefs copies the file from the overwritten offset to its end, and a random read in a 4 MiB file needs 8 device reads to follow the skip list of the file even with large caches.
* Appends with `Sync` keep a flat cost on both, at the price of rewriting a whole block or sector for every 64 B: write amplification is 16 for FatFs and more than 30 for littlefs.
* Without fast seek, FatFs walks the cluster chain to seek: an overwrite in a 4 MiB file costs 7 times as much as in a 64 KiB file, and random reads need 1.8 device reads; the link map of `fatfs/tuned` brings random reads down to one device read.
* `f_expand` makes `fatfs/tuned` slower for writes of 64 B, because writing inside the expanded file reads every sector before writing it.
* littlefs levels wear only dynamically: with 70 % static data, the most erased block is erased 13.8 times as often as the average one.
* `littlefs/default` programs 16 B at a time, and the model charges every program request a whole page program, which is why it writes three times slower than `littlefs/ram-matched`; this is a pessimistic assumption until the models are calibrated on hardware.

## Workloads

All workloads use a fixed random seed.

| Workload | Description                                                                                                                                     |
|----------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| L1       | Write one file of 1 MiB and one of 4 MiB sequentially, in chunks of 64 B, 512 B and 4 KiB, after announcing the size with `ExpectSize`          |
| L2       | Data logger: append 64 B and call `Sync`, until the file reaches 4 MiB, with a 100 ms idle period after each append                             |
| L3       | Overwrite 512 B at random offsets in a file of 64 KiB, 1 MiB and 4 MiB                                                                          |
| L4       | Create, write, close and remove many small files of 100 B to 8 KiB                                                                              |
| L5       | Fill 70 % of the volume with files that are never changed, then rewrite a 4 KiB configuration file repeatedly                                   |
| L6       | Read sequentially, read at random offsets, seek to the end, list a directory, and open files by path in directories of 10, 100 and 1000 entries |
| L7       | Mount the volume when it is 10 %, 50 % and 90 % full                                                                                            |

Workloads L2, L3 and L4 are run on volumes that are 10 %, 50 % and 90 % full, to measure the effect of the fill level.
The fill level is the share of the space that the workload does not need for its own files: static files take that share of the capacity left after the largest size the workload's files reach, so that the workload still fits on a volume that is 90 % full.
L3 counts its largest file twice, because a copy-on-write filesystem such as littlefs copies a file from the overwritten offset to its end.
The volume is filled until the usage reported by the filesystem reaches the fill level, so that the space a filesystem needs for its metadata and for small files counts.

## Write metrics

| Id  | Metric                      | Definition                                                                                              | Workloads | Target                                                                                                                                          |
|-----|-----------------------------|---------------------------------------------------------------------------------------------------------|-----------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| W-1 | Sequential write throughput | User bytes written divided by total device time                                                         | L1        | At least the best baseline                                                                                                                      |
| W-2 | Append scaling              | Device time of one append and `Sync` when the file holds 4 MiB, divided by the same when it holds 4 KiB | L2        | At most 1.1, for NOR and FAT                                                                                                                    |
| W-3 | Fill scaling                | Device time of one append and `Sync` on a 90 % full volume, divided by the same on a 10 % full volume   | L2        | At most 1.2                                                                                                                                     |
| W-4 | Write amplification         | Bytes programmed on the device divided by user bytes written                                            | L1, L2    | At most 1.1 for L1; reported for L2                                                                                                             |
| W-5 | Overwrite scaling           | Device time of one overwrite in a 4 MiB file, divided by the same in a 64 KiB file                      | L3        | At most 1.2 on NOR                                                                                                                              |
| W-6 | Call latency                | 99th percentile and maximum of the simulated latency of one call                                        | L2, L3    | Maximum at maximum timings, independent of file size and fill level: NOR at most one sector erase and one block program; FAT at most one commit |

On FAT, one commit is 2 block writes plus one per copy of the FAT, 3 flushes and one allocation unit opening, plus the one background read that a call may wait for.

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

| Id  | Metric                   | Definition                                                                                             | Workloads  | Target                                      |
|-----|--------------------------|--------------------------------------------------------------------------------------------------------|------------|---------------------------------------------|
| M-1 | Blocks per write request | Mean number of blocks per write request                                                                | L1, L2, L4 | At least FatFs                              |
| M-2 | Aligned writes           | Share of the written blocks that start an allocation unit or follow the block written just before them | L1, L2, L4 | At least FatFs on L1; reported on L2 and L4 |
| M-3 | Allocation units touched | Distinct allocation units written per MiB of user data                                                 | L1, L2, L4 | At most FatFs                               |

## Robustness metrics

| Id  | Metric          | Definition                                                                                                           | Target                                                                                                                                   |
|-----|-----------------|----------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------|
| P-1 | Power-cut sweep | Share of cut points after which the volume mounts consistently and every file holds its previous or its new contents | NOR: 100 %. FAT: 100 % with atomic sector writes, also when unflushed writes are lost or reordered, and reported with torn sector writes |

The sweep is described in [Testing.md](Testing.md).

M-2 counts blocks rather than requests, and every block of a request after its first follows the block before it, so writing the same blocks in fewer, larger requests leaves the share unchanged; only a block that neither starts an allocation unit nor follows the previous one counts against it.
Counted by requests, a filesystem that coalesces small writes would lower its share by sending fewer data requests next to the same number of metadata requests.
On L2, where every `Sync` writes one data block, one block per FAT copy and one directory block, almost no write is aligned for any subject, and on L4 the alignment of small files says little, so M-2 is only reported there.

## On-target metrics

| Id  | Metric           | Definition                                                                                                                           | Target                                                            |
|-----|------------------|--------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------|
| F-1 | Footprint        | Flash and RAM used by each filesystem in a reference configuration on Cortex-M4 and Cortex-M33, measured with `cortex-memory-budget` | Reported, next to FatFs and littlefs in equivalent configurations |
| F-2 | Processor cycles | Cycles spent per operation on the target, measured with the data watchpoint and trace unit or estimated with `cortex-cycle-budget`   | Reported                                                          |

On target, the device timing models are also calibrated: the same workloads run on real NOR flash and SD cards, and the simulated device time is compared with the measured time.

## Reports

Every metric executable writes a fragment with the device models it used and one entry per metric, workload, subject, device model and fill level:

```json
{
  "devices": {
    "nor-qspi-16m": { "sectors": "4096", "sectorSize": "4096", "typical.pageProgramNs": "400000" }
  },
  "results": [
    {
      "metric": "W-2",
      "workload": "L2",
      "subject": "embedded-filesystem/nor",
      "device": "nor-qspi-16m",
      "unit": "ratio",
      "fill": 50,
      "value": 1.02,
      "target": "<= 1.1",
      "pass": true,
      "ramBytes": 7680,
      "better": "lower"
    }
  ]
}
```

A target is either a bound, such as `<= 1.1`, which the metric executable decides, or a comparison with other subjects, such as `>= baselines` (the best of FatFs and littlefs) or `<= fatfs` (the best FatFs configuration), which is decided when the fragments are merged.
`metrics/report/merge_report.py` merges the fragments into `metrics.json`, with the same shape, and writes a Markdown summary of it, `metrics.md`.
Given the `metrics.json` of `main` with `--baseline`, it marks a reported metric of embedded-filesystem as failed when it is worse than on `main` by more than 5 %, in the direction given by `better`; results on a device model whose parameters changed, and results that the metric executables mark as `diagnostic`, are not compared.
The metrics workflow posts the summary as a comment on the pull request, updating the same comment on every push.
The `metrics.json` of `main` is kept as a workflow artifact and used to detect regressions.

### Harness

Every subject, embedded-filesystem and the baselines alike, implements `metrics::FileSystemUnderTest`, an asynchronous interface whose operations complete through callbacks.
`metrics::Runner` calls the subject, checks that no call completes before the event dispatcher runs, runs the virtual clock until the call completes, and records the latency, the device work and the result of every call, together with the phase of the workload it belongs to.
Idle periods and settling are recorded too, so that background work counts.
The workloads in `metrics/workload/Workloads.cpp` drive the runner, and `metrics/workload/MetricComputations.cpp` turns the records of a phase into the metrics of this document.
Phases are named after the workload and its parameters, such as `L1 1048576 64` for 64 B chunks in a 1 MiB file, or `L6 lookup 100` for lookups in a directory of 100 entries.
File contents follow a pattern derived from the file and the offset, so that every byte read back can be checked.

## Running

```bash
cmake --preset metrics
cmake --build --preset metrics
ctest --preset metrics
```

The `metrics` preset builds an optimized Linux host build with `EMFS_BUILD_METRICS` and the tests enabled; it runs the unit tests of the harness as well as the metric tests, and keeps going after a failure so that the report is always written.

The metric tests are GoogleTest executables with the CTest label `metrics`, run as several shards each so that `ctest --parallel` spreads them over the cores; they assert the targets of this document, so a missed target is a failing test, and every call of a workload must succeed.
Each shard writes a report fragment to `build/metrics/metrics/fragments`, and the `metrics.merge` test writes `build/metrics/metrics/metrics.json` and `build/metrics/metrics/metrics.md`.
Configuring with `-DEMFS_METRICS_BASELINE=<path to the metrics.json of main>` enables the 5 % rule; the metrics workflow does this on pull requests, with the report of the latest run on `main`.
