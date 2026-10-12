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

`fs::Result`, path parsing, handle pools, the sector cache, the per-volume operation queue, and the asynchronous file and directory interfaces.

## Phase 6: FAT

1. Mount: MBR and boot sector, BIOS parameter block, FAT type, FSINFO.
2. Directories: short names, long names, lookup by path.
3. Reading: cluster chains and the extent cache.
4. Allocation: FSINFO hint, free-run cache and contiguous runs.
5. Writing: write, append, create, remove, rename, make directory and truncate.
6. Durability: write-back cache, ordered `Sync`, discard, the dirty flag.
7. Formatting with SD alignment.
8. Power-cut sweep and interoperability tests.

## Phase 7: NOR

1. Format, block headers and the mount scan.
2. Journal and checkpoints.
3. The copy-on-write map and the node cache.
4. Inodes, files and directories, with inline data.
5. Background erase, the pre-erased pool and garbage collection of node blocks.
6. Dynamic wear leveling, then static wear leveling.
7. Exhaustive power-cut sweep.

## Phase 8: on target

The filesystems run on real hardware: NOR flash through the QSPI driver of EmIL on an ST board with hal-st, and SD cards once an SD card driver for `hal::BlockDevice` exists.
The device models are calibrated against measurements, and the footprint and cycle metrics F-1 and F-2 are measured with `cortex-memory-budget` and `cortex-cycle-budget`.

## Later

These are outside the first version, and may be added when a product needs them:

* USB mass storage, as a host and as a device; as a device in front of NOR flash, this needs a translation layer with wear leveling between FAT and the flash;
* raw NAND flash, with bad-block management and error correction;
* exFAT;
* packing of small files, to use less space for many tiny files on NOR;
* a hashed lookup structure that keeps lookups in very large directories constant.
