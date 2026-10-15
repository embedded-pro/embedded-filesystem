# Roadmap

## Introduction

embedded-filesystem is built in phases.
The documentation comes first, so that the goals, the designs and the metrics are agreed before code is written.
After that, every phase is developed test first, one pull request per increment, as described in [Testing.md](Testing.md).

## Phase 1: documentation

The goals, the comparison with FatFs and littlefs, the architecture, the designs of both filesystems, the metrics and the testing approach are written down and reviewed.

## Phase 2: repository scaffolding

The build follows the layout of embedded-infra-lib, hal-st and hal-ti:

* a top-level `CMakeLists.txt` that fetches EmIL with FetchContent, pinned to a commit and updated automatically;
* CMake presets for the host, coverage, Windows, and compile-only Cortex-M4, Cortex-M7 and Cortex-M33 builds;
* workflows for continuous integration, linting, pull request titles, releases, static analysis with SonarCloud and the EmIL version update;
* a development container and editor settings;
* a first target, `fs.interfaces`, with one test, so that every pipeline is green from the start.

## Phase 3: prerequisites in EmIL

`hal::BlockDevice` gains a `Flush` operation, and `EraseBlocks` is documented as a discard, with the test doubles updated accordingly.
`hal::Flash` stays unchanged.

## Phase 4: metrics harness

The metered devices, the device models, the power-cut devices, the workloads, the FatFs and littlefs baselines and the report generator are built before the filesystems, so that the baseline numbers are recorded and every later increment is measured.
This phase also adds the `EMFS_BUILD_METRICS` option, the `metrics` presets and the metrics workflow that comments on pull requests.
It is delivered in four steps:

1. Device models, metered devices and the virtual clock (`metrics/device`).
2. Power-cut devices and the power-cut sweep.
3. The interface every filesystem under test implements, the workloads, the runner, the metric computations and the report (`metrics/workload`, `metrics/report`).
4. The FatFs and littlefs baselines, the metric tests and the metrics workflow.

## Phase 5: shared building blocks

1. The asynchronous file and directory interfaces next to `fs::Result` (`fs/interfaces`), with their mocks.
2. Path parsing and the mapping of block device results onto `fs::Result` (`fs/common`).
3. The per-volume operation queue, deferred completions and handle pools.
4. The sector cache.

CRC-protected records are added to `fs/common` with the NOR journal in phase 7.

## Phase 6: FAT

The design is settled in [Fat.md](Fat.md) first.
The formatter comes early, because every later test and the metrics need formatted volumes, and the power-cut sweep grows with every step that adds a write path.

1. On-disk structures: the boot sector, the partition table, FSINFO, the geometry of a volume and the entries of the FAT.
2. Names: UTF-8, the rules for names, short names and the codecs of directory entries.
3. Formatting with SD alignment, the sparse block device for FAT32 tests, and the first interoperability tests.
4. Direct transfers, discards and the flush between ranks in the sector cache.
5. Mount and unmount, and the lifecycle of a volume.
6. Reading directory entries, with long names.
7. Lookup by path, `Stat` and listing directories.
8. Reading files through cluster chains and the extent cache.
9. Free space: the background scan that counts free clusters, and `Usage`.
10. The independent consistency checker for FAT images.
11. Allocation: the cursor from the FSINFO hint, the cluster after the last one, next fit, and the flush before a link into another FAT sector.
12. Writing, `Sync` and `Close` of existing files, FSINFO at unmount, and the first power-cut sweep.
13. Creating files with long names and short aliases, and `ExpectSize` with allocation-unit-aligned runs.
14. `Remove` with discard and `MakeDirectory`, and the FAT subject of the metrics.
15. `Rename` and `Truncate`.
16. Multi-block write-back in the sector cache (M-1), and the cache of free runs, with which a file opened in `append` mode starts at an allocation unit.
17. The dirty flag, and the sweep with reordered unflushed writes.
18. The targets of embedded-filesystem in the metrics, and P-1.

## Phase 7: NOR

The design is settled in [NorFilesystem.md](NorFilesystem.md) first.
As for FAT, the formatter and an independent checker come early, and every step that adds a write path adds its cases to the power-cut sweep in `metrics/nor`, which holds at 100 % with the interrupted program or erase not started, partial and completed.

1. On-flash structures: the geometry and the checks of `fs::NorConfig`, the superblock, the block header, the node header and the records, with `fs::CrcRecord` in `fs/common`.
2. The formatter, `fs::NorFormatter`, with the codecs of inode records and checkpoints.
3. The independent checker of NOR images, the small device models of the sweep, and the sweep of `Format`, with P-1 as the first target of `metrics::NorTargets`.
4. `fs::NorImageBuilder`, and mount and unmount with the lifecycle of a volume: the scan of the headers, the erase counts, the checkpoint, the journal chain and the states of the blocks.
5. The copy-on-write B+-tree and radix tree over the node cache.
6. Lookup by path, `Stat`, `OpenDirectory` and `Next`.
7. Reading files, through the data map and the inline area.
8. Writing records and checkpoints, with replay through the same path, and `MakeDirectory` as the first operation in the sweep.
9. Creating files and appending in place, with the buffer of a page, the inline area, `Sync` and `Close`.
10. Overwriting by copy-on-write, `Truncate` and `Remove`.
11. `Rename`, `Usage` without the reserve, and the NOR subject of the metrics, reported without targets; it skips L4, L5, L6 on `internal-flash`, and the call latency of L2 and L3 on `nor-qspi-16m`.
12. Background work: the pre-erased pool and its watermark, dynamic wear leveling, the background checkpoint, and the reserve in `Usage`; the results of the NOR subject become diagnostic.
13. Garbage collection of node blocks: a bit per block in RAM for the node blocks, a collection step in every checkpoint while it is due, and in the background; L4 and L5 join the metrics, and L6 on `internal-flash` with blocks of 2 sectors and names of 32 bytes.
14. Static wear leveling of data and node blocks in the background, with its foreground fallback ahead of the next call, the counters of the volume, and WL-4 on L5.
15. The other targets of the NOR subject in `metrics::NorTargets`: the W-6 bound on L2 on `internal-flash`, the charge of erases, the W-2 periods on a second log, the ratio of R-2, and the wear metrics on the lifetime run with a fallback that erases its destination on an empty pool.
    Also the factor and the ratio in `merge_report.py`, `littlefs/ram-matched` with the most RAM littlefs can use, checkpoints that only the calls start, and the results of the NOR subject compared with `main` again.
16. The exhaustive sweep of every operation and background step, and P-1.
17. A collection that is also due when the free blocks beyond the reserve run low, so that a write does not complete with `noSpace` while node blocks hold unused nodes: L2 on `internal-flash` at 90 % full showed 12 such blocks when its second log ran out of space.

## Phase 8: on target

The filesystems run on real hardware: NOR flash through the QSPI driver of EmIL on an ST board with hal-st, and SD cards once an SD card driver for `hal::BlockDevice` exists.
The device models are calibrated against measurements, and the footprint and cycle metrics F-1 and F-2 are measured with `cortex-memory-budget` and `cortex-cycle-budget`.
The parameters that `fs::FatFormatter` chooses are compared with those of cards formatted by the SD Memory Card Formatter of the SD Association.

## Later

These are outside the first version, and may be added when a product needs them:

* USB mass storage, as a host and as a device; as a device in front of NOR flash, this needs a translation layer with wear leveling between FAT and the flash;
* raw NAND flash, with bad-block management and error correction;
* exFAT;
* packing of small files, to use less space for many tiny files on NOR;
* a hashed lookup structure that keeps lookups in very large directories constant.
