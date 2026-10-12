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
9. Allocation: the FSINFO hint, the free-run cache, contiguous and aligned runs, the background scan and `Usage`.
10. The independent consistency checker for FAT images.
11. Writing, `Sync` and `Close` of existing files, with the first power-cut sweep.
12. Creating files with long names and short aliases, and `ExpectSize`.
13. `Remove` with discard and `MakeDirectory`, and the FAT subject of the metrics.
14. `Rename` and `Truncate`.
15. Multi-block write-back in the sector cache (M-1).
16. The dirty flag, FSINFO at unmount, and the sweep with reordered unflushed writes.
17. The targets of embedded-filesystem in the metrics, and P-1.

## Phase 7: NOR

1. Format, block headers and the mount scan.
2. Journal, checkpoints and CRC-protected records.
3. The copy-on-write map and the node cache.
4. Inodes, files and directories, with inline data.
5. Background erase, the pre-erased pool and garbage collection of node blocks.
6. Dynamic wear leveling, then static wear leveling.
7. Exhaustive power-cut sweep.

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
